// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A debuff of the wearer's cuts what its carrier deals. Ruled 2026-10-13 under the owner's delegation, for
 * "Applying a debuff to an enemy reduces their damage output by 10%-20%".
 *
 * WHAT IS BUILT. One flat stat, `debuffed_enemies_deal_less_percent`. Where a debuff is put on another character
 * (`ApplyDamageOverTime`, `ApplyShareOfHealthOverTime`, `ApplyTagForDuration`, `ApplyNamedEffectOnly`),
 * `UCataclysmDebuffs::NoteLaid` records on the carrier what the applier's stat states, by the debuff's tag.
 * `UCataclysmSkillEffects::WeaponDamageOf` takes the largest record whose debuff the carrier holds off what the
 * carrier's attacks are worth. No row is authored, so the stat is written by hand as the `flat` line a row
 * would be.
 *
 * THE RIG IS THE ONE `CataclysmDebuffTests.cpp` USES: a plain actor holding an ability system, a vital set and a
 * combat set. A plain actor has no root component and no body, so nothing here overlaps or is searched for.
 * Each carrier's attack damage is written as 100.
 *
 * EVERY FIGURE IS A SHARE OF WHAT THE SAME CARRIER'S ATTACKS WERE WORTH BEFORE ANYTHING WAS LAID ON IT, read
 * first. Each test reads a control before the figure it is about.
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmDebuffCutsDamageTest
{
	using CutEffects = UCataclysmSkillEffects;
	using CutDebuffs = UCataclysmDebuffs;

	FGameplayTag CutTagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** A plain actor with an ability system, 1,000 health and attacks worth 100. */
	struct FCutRig
	{
		explicit FCutRig(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper
			// rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FCutRig()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** What its attacks are worth now. */
		float Worth() const { return CutEffects::WeaponDamageOf(AbilitySystem); }

		bool Carries(const FGameplayTag& Tag) const { return AbilitySystem->HasMatchingGameplayTag(Tag); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Write this ability system's whole stat line: the row's stat at `Percent`, the Conduit flag when asked, or
	 * nothing at all when both are nought. WHOLESALE, because `SetStatInputs` is.
	 */
	void WriteTheRow(UCataclysmAbilitySystemComponent* System, float Percent, bool bConduit = false)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		const auto Add = [&Stats](const TCHAR* Stat, float Value)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Value;

			FCataclysmStatInputs Line;
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
			Stats.Add(FName(Stat), Line);
		};
		if (Percent > 0.0f)
		{
			Add(CutDebuffs::DebuffedEnemiesDealLessPercentStat, Percent);
		}
		if (bConduit)
		{
			Add(CutDebuffs::ZoneDebuffsCountUnderStat, 1.0f);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** A bleed of 10 a tick for `Seconds`, through `ApplyDamageOverTime`, not scaled by the applier. */
	bool Bleed(AActor* By, AActor* On, float PerTick = 10.0f, float Seconds = 20.0f)
	{
		return CutEffects::ApplyDamageOverTime(By, On, PerTick, Seconds, CutDebuffs::BleedTag(),
											   /*bScalesWithInstigator=*/false);
	}

	/** Cripple for `Seconds`, through `ApplyTagForDuration`. */
	bool Cripple(AActor* By, AActor* On, float Seconds = 20.0f)
	{
		return CutEffects::ApplyTagForDuration(By, On, CutDebuffs::CrippleTag(), Seconds);
	}

	using CataclysmTestWorld::RunClock;
}

#define CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageOnceTest,
	"Cataclysm.DebuffCutsDamage.AnEnemyCarryingOneOrTwoOfTheWearersDebuffsDealsTheRowsPercentLessOnce")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Wearer(World);
	const FCutRig None(World);
	const FCutRig One(World);
	const FCutRig Two(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);

	const float NoneBefore = None.Worth();
	const float OneBefore = One.Worth();
	const float TwoBefore = Two.Worth();
	if (!TestTrue(TEXT("set-up: each enemy's attacks are worth something, and the vocabulary has Bleed and Cripple"),
				  NoneBefore > 0.0f && OneBefore > 0.0f && TwoBefore > 0.0f && CutDebuffs::BleedTag().IsValid()
					  && CutDebuffs::CrippleTag().IsValid())
		|| !TestTrue(TEXT("set-up: the wearer's Cripple lands on the second enemy"), Cripple(Wearer.Actor, One.Actor))
		|| !TestTrue(TEXT("set-up: the wearer's Cripple lands on the third"), Cripple(Wearer.Actor, Two.Actor))
		|| !TestTrue(TEXT("set-up: and the wearer's bleed lands on the third"), Bleed(Wearer.Actor, Two.Actor)))
	{
		return false;
	}

	TestEqual(TEXT("control: an enemy carrying no debuff deals what it dealt"), None.Worth() / NoneBefore, 1.0f,
			  0.0005f);
	TestEqual(TEXT("an enemy carrying one debuff of the wearer's deals 20% less"), One.Worth() / OneBefore, 0.8f,
			  0.0005f);
	TestEqual(TEXT("set-up: the third enemy carries two debuffs"), CutDebuffs::CountOn(Two.AbilitySystem), 2);
	TestEqual(TEXT("and one carrying two deals 20% less, the same cut and not two of them"),
			  Two.Worth() / TwoBefore, 0.8f, 0.0005f);
	TestEqual(TEXT("the wearer's own attacks are worth what they were"), Wearer.Worth(), 100.0f, 0.0005f);
	return true;
}

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageEndsTest,
	"Cataclysm.DebuffCutsDamage.TheCutEndsWhenTheLastOfTheWearersDebuffsLeavesAndARefreshKeepsIt")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Wearer(World);
	const FCutRig Enemy(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);
	const FGameplayTag CrippleTag = CutDebuffs::CrippleTag();
	const FGameplayTag BleedTag = CutDebuffs::BleedTag();

	const float Before = Enemy.Worth();
	if (!TestTrue(TEXT("set-up: its attacks are worth something"), Before > 0.0f)
		|| !TestTrue(TEXT("set-up: both tags"), CrippleTag.IsValid() && BleedTag.IsValid()))
	{
		return false;
	}
	const auto Share = [&]() { return Enemy.Worth() / Before; };
	TestEqual(TEXT("control: with no debuff the enemy deals what it dealt"), Share(), 1.0f, 0.0005f);

	Cripple(Wearer.Actor, Enemy.Actor);
	Bleed(Wearer.Actor, Enemy.Actor);
	TestEqual(TEXT("with two of the wearer's debuffs it deals 20% less"), Share(), 0.8f, 0.0005f);

	CutEffects::RemoveEffectsGranting(Enemy.Actor, BleedTag);
	if (!TestTrue(TEXT("set-up: the bleed is gone and the Cripple is not"),
				  !Enemy.Carries(BleedTag) && Enemy.Carries(CrippleTag)))
	{
		return false;
	}
	TestEqual(TEXT("with one of the two gone it still deals 20% less"), Share(), 0.8f, 0.0005f);

	TestTrue(TEXT("set-up: the wearer's Cripple is applied again"), Cripple(Wearer.Actor, Enemy.Actor));
	TestEqual(TEXT("a refresh keeps the cut"), Share(), 0.8f, 0.0005f);

	CutDebuffs::Cleanse(Enemy.Actor);
	if (!TestFalse(TEXT("set-up: a cleanse ends the Cripple"), Enemy.Carries(CrippleTag)))
	{
		return false;
	}
	TestEqual(TEXT("and with the last of them cleansed it deals what it dealt"), Share(), 1.0f, 0.0005f);

	// BY TIME. A Cripple of 2 seconds, and the clock moved 2.5 seconds by hand.
	TestTrue(TEXT("set-up: a Cripple of 2 seconds lands"), Cripple(Wearer.Actor, Enemy.Actor, 2.0f));
	TestEqual(TEXT("control: while those 2 seconds run it deals 20% less"), Share(), 0.8f, 0.0005f);
	RunClock(World, 2.5f);
	if (!TestFalse(TEXT("set-up: 2.5 seconds on the Cripple has run out"), Enemy.Carries(CrippleTag)))
	{
		return false;
	}
	TestEqual(TEXT("and with it run out the enemy deals what it dealt"), Share(), 1.0f, 0.0005f);
	return true;
}

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageMultipliesTest,
	"Cataclysm.DebuffCutsDamage.TheCutMultipliesWithWeakenAndWithThePoisonRider")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Plain(World);
	const FCutRig Wearer(World);
	const FCutRig WeakenedByPlain(World);
	const FCutRig WeakenedByWearer(World);
	const FCutRig Poisoned(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);

	const FGameplayTag Weaken = CutDebuffs::WeakenTag();
	const FGameplayTag Poison = CutTagNamed(TEXT("Keyword.DoT.Poison"));
	const float PlainBefore = WeakenedByPlain.Worth();
	const float WearerBefore = WeakenedByWearer.Worth();
	const float PoisonedBefore = Poisoned.Worth();
	if (!TestTrue(TEXT("set-up: Weaken and Poison are in the vocabulary and attacks are worth something"),
				  Weaken.IsValid() && Poison.IsValid() && PlainBefore > 0.0f && WearerBefore > 0.0f
					  && PoisonedBefore > 0.0f))
	{
		return false;
	}

	// WEAKEN AT 20, STATED, through the function every Weaken arrives by. Its row moves attack damage by a
	// proportion, which is read from the status effect table's asset.
	TestTrue(TEXT("set-up: a Weaken of 20 from a character with no row lands"),
			 CutEffects::ApplyNamedEffect(Plain.Actor, WeakenedByPlain.Actor, Weaken, 20.0f, 20.0f));
	const float WeakenAlone = WeakenedByPlain.Worth() / PlainBefore;
	if (!TestEqual(TEXT("control: Weaken at 20 from a character with no row leaves four fifths. If this is 1, the "
						"status effect table's asset did not load or Weaken's row moves no stat"),
				   WeakenAlone, 0.8f, 0.0005f))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the same Weaken from the wearer lands"),
			 CutEffects::ApplyNamedEffect(Wearer.Actor, WeakenedByWearer.Actor, Weaken, 20.0f, 20.0f));
	TestEqual(TEXT("the wearer's Weaken leaves four fifths of four fifths: the two multiply"),
			  WeakenedByWearer.Worth() / WearerBefore, 0.64f, 0.0005f);

	// THE POISON RIDER, 4%, as the worn action "Poisoned enemies deal 2%-4% less damage" would be at its top.
	FCataclysmPoolAction Rider;
	Rider.Rider = ECataclysmAilmentRider::DamageDealt;
	Rider.Ailment = Poison;
	Rider.Percent = 4.0f;
	Wearer.AbilitySystem->SetPoolActions({Rider});
	TestTrue(TEXT("set-up: the wearer's poison lands"),
			 CutEffects::ApplyDamageOverTime(Wearer.Actor, Poisoned.Actor, 10.0f, 20.0f, Poison,
											 /*bScalesWithInstigator=*/false));
	TestEqual(TEXT("with the poison rider at 4 and the cut at 20 it deals 0.96 of four fifths"),
			  Poisoned.Worth() / PoisonedBefore, 0.768f, 0.0005f);
	WriteTheRow(Wearer.AbilitySystem, 0.0f);
	CutEffects::ApplyDamageOverTime(Wearer.Actor, Poisoned.Actor, 10.0f, 20.0f, Poison,
									/*bScalesWithInstigator=*/false);
	TestEqual(TEXT("control: with the row gone and the poison applied again, the rider alone leaves 0.96"),
			  Poisoned.Worth() / PoisonedBefore, 0.96f, 0.0005f);
	return true;
}

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageSpreadCopyTest,
	"Cataclysm.DebuffCutsDamage.ACopyASpreadMakesDoesNotCount")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Wearer(World);
	const FCutRig Applied(World);
	const FCutRig Copied(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);
	const FGameplayTag BleedTag = CutDebuffs::BleedTag();

	const float AppliedBefore = Applied.Worth();
	const float CopiedBefore = Copied.Worth();
	if (!TestTrue(TEXT("set-up: both worth something, and the bleed tag"),
				  AppliedBefore > 0.0f && CopiedBefore > 0.0f && BleedTag.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("set-up: the wearer's bleed lands on the first enemy"), Bleed(Wearer.Actor, Applied.Actor));
	TestEqual(TEXT("control: the enemy the wearer applied the bleed to deals 20% less"),
			  Applied.Worth() / AppliedBefore, 0.8f, 0.0005f);

	// THE SAME BLEED AS A SPREAD'S COPY, in the wearer's name, which is how every spread hands one on.
	CutEffects::FRunningAilment Running;
	Running.Ailment = BleedTag;
	Running.DamagePerSecond = 10.0f;
	Running.SecondsLeft = 20.0f;
	Running.Applier = Wearer.Actor;
	if (!TestTrue(TEXT("set-up: the copy lands on the second enemy"),
				  CutEffects::ApplySpreadCopy(Wearer.Actor, Copied.Actor, Running))
		|| !TestTrue(TEXT("set-up: and the second enemy is bleeding"), Copied.Carries(BleedTag)))
	{
		return false;
	}
	TestEqual(TEXT("an enemy carrying only a spread's copy deals what it dealt"), Copied.Worth() / CopiedBefore,
			  1.0f, 0.0005f);

	CutEffects::ApplySpreadCopy(Wearer.Actor, Applied.Actor, Running);
	TestEqual(TEXT("and a copy landing on the bleed the wearer applied leaves that enemy's cut standing"),
			  Applied.Worth() / AppliedBefore, 0.8f, 0.0005f);
	return true;
}

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageAnothersTest,
	"Cataclysm.DebuffCutsDamage.AnotherCharactersDebuffDoesNotCountAndTheWearersOwnIsTakenDownOnlyByTheWearer")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Wearer(World);
	const FCutRig Other(World);
	const FCutRig Enemy(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);

	const float Before = Enemy.Worth();
	if (!TestTrue(TEXT("set-up: its attacks are worth something"), Before > 0.0f))
	{
		return false;
	}
	const auto Share = [&]() { return Enemy.Worth() / Before; };

	TestTrue(TEXT("set-up: another character's Cripple lands"), Cripple(Other.Actor, Enemy.Actor));
	TestTrue(TEXT("set-up: and another character's bleed"), Bleed(Other.Actor, Enemy.Actor));
	TestEqual(TEXT("an enemy carrying two debuffs another character applied deals what it dealt"), Share(), 1.0f,
			  0.0005f);

	TestTrue(TEXT("set-up: the wearer's weaker bleed is taken, and only refreshes that bleed"),
			 Bleed(Wearer.Actor, Enemy.Actor, 5.0f));
	TestEqual(TEXT("control: the wearer's application counts, though it only refreshes: 20% less"), Share(), 0.8f,
			  0.0005f);

	Bleed(Other.Actor, Enemy.Actor);
	TestEqual(TEXT("another character applying the bleed again leaves the wearer's cut standing"), Share(), 0.8f,
			  0.0005f);

	WriteTheRow(Wearer.AbilitySystem, 0.0f);
	Cripple(Wearer.Actor, Enemy.Actor);
	TestEqual(TEXT("with the row gone, the wearer applying ANOTHER debuff leaves the bleed's cut standing"), Share(),
			  0.8f, 0.0005f);
	Bleed(Wearer.Actor, Enemy.Actor, 5.0f);
	TestEqual(TEXT("and the wearer's next application of the bleed takes its cut down"), Share(), 1.0f, 0.0005f);

	// A DEBUFF THAT HAS ENDED LEAVES NOTHING FOR THE NEXT APPLIER. The wearer's Cripple, cleansed, then another's.
	WriteTheRow(Wearer.AbilitySystem, 20.0f);
	CutDebuffs::Cleanse(Enemy.Actor);
	Cripple(Wearer.Actor, Enemy.Actor);
	TestEqual(TEXT("control: the wearer's Cripple alone cuts 20%"), Share(), 0.8f, 0.0005f);
	CutDebuffs::Cleanse(Enemy.Actor);
	Cripple(Other.Actor, Enemy.Actor);
	TestEqual(TEXT("and once cleansed, another character's Cripple does not bring the cut back"), Share(), 1.0f,
			  0.0005f);

	// AND WHAT A CHARACTER PUTS ON ITSELF IS NOT APPLYING ONE TO AN ENEMY.
	const float WearerBefore = Wearer.Worth();
	Cripple(Wearer.Actor, Wearer.Actor);
	TestEqual(TEXT("a debuff the wearer puts on itself cuts nothing of the wearer's"), Wearer.Worth() / WearerBefore,
			  1.0f, 0.0005f);
	return true;
}

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageConduitTest,
	"Cataclysm.DebuffCutsDamage.AMinionsOrAZonesDebuffCountsOnlyWhileTheWearerHoldsConduit")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Wearer(World);
	const FCutRig Enemy(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);

	// THE MINION STANDS 50 M FROM THE ORIGIN, where nothing else in this test has a body.
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Wearer.Actor, FVector(5000.0f, 0.0f, 0.0f), /*Lifetime=*/60.0f,
													/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	const float Before = Enemy.Worth();
	if (!TestNotNull(TEXT("set-up: a minion of the wearer's"), Imp)
		|| !TestNotNull(TEXT("set-up: with an ability system"), UCataclysmTargeting::AbilitySystemOf(Imp))
		|| !TestTrue(TEXT("set-up: the enemy's attacks are worth something"), Before > 0.0f))
	{
		return false;
	}
	const auto Share = [&]() { return Enemy.Worth() / Before; };

	// THE ZONE. What a ground zone does around the effects it lays is hold `FCataclysmZoneLaying`; the zone
	// itself is not spawned here.
	TestFalse(TEXT("control: no zone is laying anything"), FCataclysmZoneLaying::IsLaying());
	{
		const FCataclysmZoneLaying Laying;
		TestTrue(TEXT("set-up: a zone is laying its effects"), FCataclysmZoneLaying::IsLaying());
		TestTrue(TEXT("set-up: the Cripple a zone lays in the wearer's name lands"),
				 Cripple(Wearer.Actor, Enemy.Actor));
	}
	TestFalse(TEXT("set-up: and it has stopped"), FCataclysmZoneLaying::IsLaying());
	TestEqual(TEXT("a zone's debuff does not count without Conduit"), Share(), 1.0f, 0.0005f);

	TestTrue(TEXT("set-up: the minion's bleed lands"), Bleed(Imp, Enemy.Actor));
	TestEqual(TEXT("and a minion's debuff does not count without Conduit"), Share(), 1.0f, 0.0005f);

	TestTrue(TEXT("set-up: the wearer's own Cripple, by no zone, lands on the one the zone laid"),
			 Cripple(Wearer.Actor, Enemy.Actor));
	TestEqual(TEXT("control: the wearer's own application counts: 20% less"), Share(), 0.8f, 0.0005f);
	CutDebuffs::Cleanse(Enemy.Actor);
	TestEqual(TEXT("set-up: cleansed, it deals what it dealt"), Share(), 1.0f, 0.0005f);

	WriteTheRow(Wearer.AbilitySystem, 20.0f, /*bConduit=*/true);
	TestTrue(TEXT("set-up: the minion's bleed lands again"), Bleed(Imp, Enemy.Actor));
	TestEqual(TEXT("with Conduit held a minion's debuff counts: 20% less"), Share(), 0.8f, 0.0005f);
	CutDebuffs::Cleanse(Enemy.Actor);
	{
		const FCataclysmZoneLaying Laying;
		Cripple(Wearer.Actor, Enemy.Actor);
	}
	TestEqual(TEXT("and with Conduit held a zone's debuff counts: 20% less"), Share(), 0.8f, 0.0005f);
	return true;
}

CATACLYSM_DEBUFF_CUTS_DAMAGE_TEST(FCataclysmDebuffCutsDamageNotADebuffTest,
	"Cataclysm.DebuffCutsDamage.AStaggerAPinAndAKnockdownAreNotDebuffsAndCutNothing")
{
	using namespace CataclysmDebuffCutsDamageTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCutRig Wearer(World);
	const FCutRig Enemy(World);
	WriteTheRow(Wearer.AbilitySystem, 20.0f);

	const float Before = Enemy.Worth();
	if (!TestTrue(TEXT("set-up: its attacks are worth something"), Before > 0.0f))
	{
		return false;
	}
	const auto Share = [&]() { return Enemy.Worth() / Before; };

	// EACH TAG LAID AS ITS OWN FUNCTION LAYS IT, through `ApplyTagForDuration`, so no rule of a stagger, a pin
	// or a knockdown decides whether it lands here.
	for (const FGameplayTag& NotADebuff :
		 {CutEffects::StaggeredTag(), CutEffects::PinnedTag(), CutEffects::KnockedDownTag()})
	{
		if (!TestTrue(TEXT("set-up: the tag is in the vocabulary"), NotADebuff.IsValid()))
		{
			return false;
		}
		TestFalse(*FString::Printf(TEXT("%s is not a debuff"), *NotADebuff.ToString()),
				  CutDebuffs::IsADebuff(NotADebuff));
		TestTrue(*FString::Printf(TEXT("set-up: %s lands"), *NotADebuff.ToString()),
				 CutEffects::ApplyTagForDuration(Wearer.Actor, Enemy.Actor, NotADebuff, 20.0f));
		TestEqual(*FString::Printf(TEXT("carrying %s from the wearer the enemy deals what it dealt"),
								   *NotADebuff.ToString()),
				  Share(), 1.0f, 0.0005f);
	}

	TestTrue(TEXT("Cripple is a debuff"), CutDebuffs::IsADebuff(CutDebuffs::CrippleTag()));
	TestTrue(TEXT("and so is a stun"), CutDebuffs::IsADebuff(CutEffects::StunnedTag()));
	TestTrue(TEXT("set-up: the wearer's stun lands"),
			 CutEffects::ApplyTagForDuration(Wearer.Actor, Enemy.Actor, CutEffects::StunnedTag(), 20.0f));
	TestEqual(TEXT("control: stunned by the wearer it deals 20% less"), Share(), 0.8f, 0.0005f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
