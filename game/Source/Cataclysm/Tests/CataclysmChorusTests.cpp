// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmChorus.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Chorus, the Ritualist's `Ritualist_capstone_200` option 3. Issue #1515.
 *
 * "Your minions repeat each skill you cast, dealing 30% of its damage." The
 * owner's decision of 2026-09-25: "No it should be 30% each", so each minion's
 * repeat is worth 30% of the hit and N minions add N x 30%.
 *
 * THE FIGURES ARE CHOSEN SO THE DECISION CAN BE TOLD FROM ITS ALTERNATIVE, 30%
 * shared between them. With two minions a 250 hit gains 75 each, 150 in all,
 * where shared it would gain 75; with three a 1000 hit gains 900 where shared
 * it would gain 300.
 *
 * THE STAT IS GIVEN BY HAND, as the row will give it: the option has no row yet.
 * The minions stand more than ten metres behind the caster so no skill here
 * reaches them.
 */
namespace CataclysmChorusTest
{
	constexpr float M = 100.0f;
	constexpr float WeaponDamage = 100.0f;
	constexpr float Pool = 100000.0f;

	/** An actor a sphere overlap can find, with the sets a blow needs. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* Vitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* Combat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* Resist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Vitals);
			AbilitySystem->AddAttributeSetSubobject(Combat);
			AbilitySystem->AddAttributeSetSubobject(Resist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Pool);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), Pool);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), WeaponDamage);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** Give this character flat figures, as passive rows would. */
		void Hold(const TArray<const TCHAR*>& Stats)
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TCHAR* Stat : Stats)
			{
				FCataclysmStatModifier Flat;
				Flat.Bucket = ECataclysmStatBucket::Flat;
				Flat.Source = ECataclysmModifierSource::PassiveKeystone;
				Flat.Value = 1.0f;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
				Line.Base = 0.0f;
				Line.Modifiers = {Flat};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Grant a template into a slot at level 100 and return its live instance. */
	template <typename T>
	T* GrantSkill(FScopedFighter& Caster, ECataclysmAbilitySlot Slot, const FString& ParamText)
	{
		const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
			T::StaticClass(), Slot, /*Level=*/100, Caster.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid()
			? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle)
			: nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = TEXT("Chorus Test Skill");
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
		}
		return Instance;
	}

	bool Activate(FScopedFighter& Caster, UGameplayAbility* Ability)
	{
		return Ability && Caster.AbilitySystem->TryActivateAbility(
			Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
	}

	/** `Count` imps for `Summoner`, standing 15 metres behind it. */
	TArray<ACataclysmMinion*> SummonImps(FAutomationTestBase& Test, UWorld* World,
										 AActor* Summoner, int32 Count)
	{
		TArray<ACataclysmMinion*> Imps;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
				Summoner, FVector(-15.0f * M, (Index - 1) * 2.0f * M, 0.0f),
				/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
			if (!Test.TestNotNull(TEXT("an imp"), Imp))
			{
				return {};
			}
			if (Imp->TypeName != FString(TEXT("Imp")))
			{
				Test.AddError(TEXT("DT_MinionTypes could not supply the Imp row. Run "
								   "tools/generate_datatable_assets.py"));
				return {};
			}
			Imps.Add(Imp);
		}
		return Imps;
	}

	/** Every hit notice, kept, while in scope. */
	struct FHeardHits
	{
		explicit FHeardHits(UWorld* World)
			: Events(UCataclysmCombatEvents::In(World))
		{
			if (Events)
			{
				Handle = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
				{
					FCataclysmHitNotice Kept = Notice;
					Kept.EffectTags = nullptr;
					Kept.GrantedTags = nullptr;
					Kept.SkillTags = nullptr;
					Hits.Add(Kept);
				});
			}
		}

		~FHeardHits()
		{
			if (Events)
			{
				Events->OnHit.Remove(Handle);
			}
		}

		/** The hits dealt by any of `Dealers`. */
		TArray<FCataclysmHitNotice> DealtByAnyOf(const TArray<ACataclysmMinion*>& Dealers) const
		{
			return Hits.FilterByPredicate([&Dealers](const FCataclysmHitNotice& Hit)
			{
				return Dealers.Contains(Cast<ACataclysmMinion>(Hit.DealtBy));
			});
		}

		UCataclysmCombatEvents* Events = nullptr;
		FDelegateHandle Handle;
		TArray<FCataclysmHitNotice> Hits;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChorusTwoMinionsTest,
	"Cataclysm.Chorus.AStrikeWithTwoMinionsIsRepeatedForThirtyPercentEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A real Heavy strike, 250% of a 100 weapon, by a caster holding Chorus with two
 * imps: the enemy loses 250 and then 75 from each imp, 400 in all. Shared it
 * would be 325.
 */
bool FCataclysmChorusTwoMinionsTest::RunTest(const FString&)
{
	using namespace CataclysmChorusTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2.0f * M, 0.0f, 0.0f));
	Caster.Hold({UCataclysmChorus::Stat});
	const TArray<ACataclysmMinion*> Imps = SummonImps(*this, World, Caster.Actor, 2);
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"));
	if (Imps.Num() != 2 || !TestNotNull(TEXT("a strike"), Strike))
	{
		return false;
	}

	FHeardHits Heard(World);
	TestTrue(TEXT("the strike activates"), Activate(Caster, Strike));

	TestEqual(TEXT("the enemy lost the strike's 250 and the chorus's 150"),
			  Pool - Enemy.Health(), 400.0f, 0.01f);
	const TArray<FCataclysmHitNotice> Repeats = Heard.DealtByAnyOf(Imps);
	if (!TestEqual(TEXT("each of the two imps repeated the hit once"), Repeats.Num(), 2))
	{
		return false;
	}
	for (const FCataclysmHitNotice& Repeat : Repeats)
	{
		TestEqual(TEXT("each repeat is 30% of 250"), Repeat.DealtToHealth, 75.0f, 0.01f);
		TestTrue(TEXT("on the enemy the strike landed on"), Repeat.Target == Enemy.Actor);
	}
	TestTrue(TEXT("by different imps"), Repeats[0].DealtBy != Repeats[1].DealtBy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChorusThreeMinionsTest,
	"Cataclysm.Chorus.ThreeMinionsEachRepeatThirtyPercentOfTheHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A hit that sent 1000, three imps: three repeats of 300, 900 in all, not 300. */
bool FCataclysmChorusThreeMinionsTest::RunTest(const FString&)
{
	using namespace CataclysmChorusTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2.0f * M, 0.0f, 0.0f));
	Caster.Hold({UCataclysmChorus::Stat});
	const TArray<ACataclysmMinion*> Imps = SummonImps(*this, World, Caster.Actor, 3);
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"));
	if (Imps.Num() != 3 || !TestNotNull(TEXT("a strike"), Strike))
	{
		return false;
	}

	FHeardHits Heard(World);
	TestEqual(TEXT("three repeats"),
			  UCataclysmChorus::Repeat(Caster.Actor, Enemy.Actor, 1000.0f, Strike), 3);
	TestEqual(TEXT("worth 30% of 1000 three times"), Pool - Enemy.Health(), 900.0f, 0.01f);
	for (const FCataclysmHitNotice& Repeat : Heard.DealtByAnyOf(Imps))
	{
		TestEqual(TEXT("each 30% of 1000"), Repeat.DealtToHealth, 300.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChorusProjectileTest,
	"Cataclysm.Chorus.AProjectilesContactIsRepeated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A projectile a skill fired, 100% of a 100 weapon, reaching an enemy: two imps
 * add 30 each. A projectile no skill fired -- an enemy's thrown rock -- is
 * not repeated.
 */
bool FCataclysmChorusProjectileTest::RunTest(const FString&)
{
	using namespace CataclysmChorusTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(4.0f * M, 0.0f, 0.0f));
	Caster.Hold({UCataclysmChorus::Stat});
	const TArray<ACataclysmMinion*> Imps = SummonImps(*this, World, Caster.Actor, 2);
	UCataclysmProjectileSkill* Firing = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Speed=1200; Radius=0.5"));
	if (Imps.Num() != 2 || !TestNotNull(TEXT("a projectile skill"), Firing))
	{
		return false;
	}

	FHeardHits Heard(World);
	const auto FireAtTheEnemy = [&](const UGameplayAbility* Skill)
	{
		ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
			Caster.Actor, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
			/*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f, /*InPierce=*/0,
			/*bInReturns=*/false, /*InDamagePercent=*/100.0f, FGameplayTagContainer(),
			/*bInBurns=*/false, /*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f,
			/*InCritChancePercent=*/0.0f, /*InSkillHealthCostPercent=*/-1.0f, Skill);
		for (int32 Step = 0; Shot && Step < 100 && Shot->Step(0.05f); ++Step)
		{
		}
	};

	FireAtTheEnemy(Firing);
	TestEqual(TEXT("a skill's projectile: 100 and 60 more from the imps"),
			  Pool - Enemy.Health(), 160.0f, 0.01f);
	TestEqual(TEXT("two repeats"), Heard.DealtByAnyOf(Imps).Num(), 2);

	const float Before = Enemy.Health();
	FireAtTheEnemy(nullptr);
	TestEqual(TEXT("a projectile no skill fired deals its 100 and nothing more"),
			  Before - Enemy.Health(), 100.0f, 0.01f);
	TestEqual(TEXT("so still two repeats"), Heard.DealtByAnyOf(Imps).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChorusNothingTest,
	"Cataclysm.Chorus.NoRepeatWithoutTheOptionAMinionOrASkillOtherThanTheBasicAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Each missing condition on its own stops the repeat: the option, a minion,
 * a skill, a skill other than the basic attack, and something sent.
 */
bool FCataclysmChorusNothingTest::RunTest(const FString&)
{
	using namespace CataclysmChorusTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Holder(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2.0f * M, 0.0f, 0.0f));
	Holder.Hold({UCataclysmChorus::Stat});
	UCataclysmStrikeSkill* Heavy = GrantSkill<UCataclysmStrikeSkill>(
		Holder, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"));
	if (!TestNotNull(TEXT("a strike"), Heavy))
	{
		return false;
	}

	TestEqual(TEXT("a holder with no minion repeats nothing"),
			  UCataclysmChorus::Repeat(Holder.Actor, Enemy.Actor, 1000.0f, Heavy), 0);

	if (SummonImps(*this, World, Holder.Actor, 2).Num() != 2)
	{
		return false;
	}
	UCataclysmStrikeSkill* Basic = GrantSkill<UCataclysmStrikeSkill>(
		Holder, ECataclysmAbilitySlot::BasicAttack, TEXT("Radius=4; Angle=120"));
	if (!TestNotNull(TEXT("a basic attack"), Basic))
	{
		return false;
	}
	TestEqual(TEXT("the basic attack is not repeated"),
			  UCataclysmChorus::Repeat(Holder.Actor, Enemy.Actor, 1000.0f, Basic), 0);
	TestEqual(TEXT("a hit with no skill behind it is not"),
			  UCataclysmChorus::Repeat(Holder.Actor, Enemy.Actor, 1000.0f, nullptr), 0);
	TestEqual(TEXT("a hit that sent nothing is not"),
			  UCataclysmChorus::Repeat(Holder.Actor, Enemy.Actor, 0.0f, Heavy), 0);
	TestEqual(TEXT("so the enemy is untouched"), Enemy.Health(), Pool);

	FScopedFighter Plain(World, FVector(0.0f, 30.0f * M, 0.0f));
	UCataclysmStrikeSkill* PlainHeavy = GrantSkill<UCataclysmStrikeSkill>(
		Plain, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"));
	if (SummonImps(*this, World, Plain.Actor, 2).Num() != 2
		|| !TestNotNull(TEXT("a strike for the plain caster"), PlainHeavy))
	{
		return false;
	}
	TestEqual(TEXT("a caster with minions but without the option repeats nothing"),
			  UCataclysmChorus::Repeat(Plain.Actor, Enemy.Actor, 1000.0f, PlainHeavy), 0);

	TestEqual(TEXT("while the holder, with both, does"),
			  UCataclysmChorus::Repeat(Holder.Actor, Enemy.Actor, 1000.0f, Heavy), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChorusAuraTest,
	"Cataclysm.Chorus.AnAurasPulseComesAfterTheCastAndIsNotRepeated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** An aura held by a Chorus holder with two imps: its pulse hurts, the imps do nothing. */
bool FCataclysmChorusAuraTest::RunTest(const FString&)
{
	using namespace CataclysmChorusTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	Caster.Hold({UCataclysmChorus::Stat});
	const TArray<ACataclysmMinion*> Imps = SummonImps(*this, World, Caster.Actor, 2);
	UCataclysmAuraSkill* Aura = GrantSkill<UCataclysmAuraSkill>(
		Caster, ECataclysmAbilitySlot::Aura, TEXT("Radius=10; Interval=1"));
	if (Imps.Num() != 2 || !TestNotNull(TEXT("an aura"), Aura))
	{
		return false;
	}

	FHeardHits Heard(World);
	TestTrue(TEXT("the aura activates"), Activate(Caster, Aura));
	Aura->Pulse();
	TestTrue(TEXT("its pulse hurt the enemy"), Enemy.Health() < Pool);
	TestEqual(TEXT("and no imp repeated it"), Heard.DealtByAnyOf(Imps).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChorusOwnBlowTest,
	"Cataclysm.Chorus.ARepeatIsTheMinionsOwnBlowAndConduitMakesItYours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A repeat is dealt by the imp and, without Conduit, credited to it. It moves
 * nothing and sets nothing alight. With Conduit it is credited to the caster.
 */
bool FCataclysmChorusOwnBlowTest::RunTest(const FString&)
{
	using namespace CataclysmChorusTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FVector Where(2.0f * M, 0.0f, 0.0f);
	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, Where);
	Caster.Hold({UCataclysmChorus::Stat});
	const TArray<ACataclysmMinion*> Imps = SummonImps(*this, World, Caster.Actor, 1);
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"));
	if (Imps.Num() != 1 || !TestNotNull(TEXT("a strike"), Strike))
	{
		return false;
	}

	{
		FHeardHits Heard(World);
		TestEqual(TEXT("one repeat"),
				  UCataclysmChorus::Repeat(Caster.Actor, Enemy.Actor, 1000.0f, Strike), 1);
		const TArray<FCataclysmHitNotice> Repeats = Heard.DealtByAnyOf(Imps);
		if (TestEqual(TEXT("heard once"), Repeats.Num(), 1))
		{
			TestTrue(TEXT("credited to the imp, since the caster lacks Conduit"),
					 Repeats[0].Attacker == Imps[0]);
		}
		TestEqual(TEXT("the enemy was not moved"), Enemy.Actor->GetActorLocation(), Where);
		TestFalse(TEXT("and not set alight"),
				  UCataclysmSkillEffects::HasTag(Enemy.Actor, UCataclysmSkillEffects::BurnTag()));
	}

	Caster.Hold({UCataclysmChorus::Stat, TEXT("minion_hits_count_as_yours")});
	{
		FHeardHits Heard(World);
		UCataclysmChorus::Repeat(Caster.Actor, Enemy.Actor, 1000.0f, Strike);
		const TArray<FCataclysmHitNotice> Repeats = Heard.DealtByAnyOf(Imps);
		if (TestEqual(TEXT("heard once with Conduit"), Repeats.Num(), 1))
		{
			TestTrue(TEXT("credited to the caster, who holds Conduit"),
					 Repeats[0].Attacker == Caster.Actor);
			TestTrue(TEXT("and still dealt by the imp"), Repeats[0].DealtBy == Imps[0]);
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
