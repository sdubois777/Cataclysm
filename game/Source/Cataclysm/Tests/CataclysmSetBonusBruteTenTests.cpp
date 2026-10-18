// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGroundPound.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Brute's Heart's ten-piece bonus, the mechanism. The owner's ruling #5 of 2026-10-17 (it fires by itself once ready
 * and an enemy is inside its radius) and the coordinating session's rulings of 2026-10-18 (Q1-Q5).
 *
 * "Every 30 seconds, you can unleash a powerful "Ground Pound" that deals 500% of your armor as damage to all enemies
 * in a large radius and stuns them for 3 seconds. The cooldown of this ability is reduced by 0.5 seconds per kill"
 *
 * THE ROWS ARE GIVEN AS STAT LINES, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: the five Ground Pound stats
 * at a flat 30, 500, 8, 3 and 0.5, each with no tags. The real rows, worn at ten pieces with a control at nine, are
 * the enchantment session's test.
 *
 * THE WEARER IS A BARE ACTOR holding 1000 armour, at the origin. It has no team, so every creature is its enemy. THE
 * ENEMIES ARE CREATURES of the monsters' side with no armour, evasion, block or resistance and health enough that no
 * pound kills one; creatures of one side are not each other's enemies. Critical strikes are silenced by the world
 * helper. The pound is driven by calling `UCataclysmGroundPound::Step` with the world's clock moved by hand, as the
 * regeneration step would call it; the stun's end is reached with `CataclysmTestWorld::RunClock`, which fires the
 * effect's timer. A kill is raised on the wearer by hand, as `ACataclysmPlayerCharacter::OnSomethingDied` raises it
 * for a kill it is credited with.
 */
namespace CataclysmSetBonusBruteTenTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** A creature's health, large enough that no pound here empties it. */
	constexpr float BruteTenEnemyHealth = 100000.0f;

	/** The wearer's armour, and so the blow at 500%: 5000. */
	constexpr float BruteTenArmour = 1000.0f;

	/** The ruled figures. */
	constexpr float BruteTenEverySeconds = 30.0f;
	constexpr float BruteTenArmourPercent = 500.0f;
	constexpr float BruteTenRadiusMetres = 8.0f;
	constexpr float BruteTenStunSeconds = 3.0f;
	constexpr float BruteTenSecondsOffPerKill = 0.5f;

	/** How far a float clock may fall from a figure it was built to equal, in seconds. */
	constexpr float BruteTenClockAllowance = 0.001f;

	/** The bare wearer: a sphere a search can find, an ability system, health, 1000 armour and the resistances. */
	struct FBruteTenWearer
	{
		explicit FBruteTenWearer(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(FVector::ZeroVector);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), BruteTenEnemyHealth);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), BruteTenEnemyHealth);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), BruteTenArmour);
		}

		~FBruteTenWearer()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** The five stats as flat stat lines, all at once, since `SetStatInputs` replaces what was there. */
		void Wear(float EverySeconds = BruteTenEverySeconds) const
		{
			const TMap<FName, float> Flats = {
				{FName(UCataclysmGroundPound::EverySecondsStat), EverySeconds},
				{FName(UCataclysmGroundPound::ArmourPercentStat), BruteTenArmourPercent},
				{FName(UCataclysmGroundPound::RadiusMetresStat), BruteTenRadiusMetres},
				{FName(UCataclysmGroundPound::StunSecondsStat), BruteTenStunSeconds},
				{FName(UCataclysmGroundPound::SecondsOffPerKillStat), BruteTenSecondsOffPerKill}};
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const TPair<FName, float>& Each : Flats)
			{
				if (Each.Value == 0.0f)
				{
					continue;
				}
				FCataclysmStatModifier Modifier;
				Modifier.Bucket = ECataclysmStatBucket::Flat;
				Modifier.Source = ECataclysmModifierSource::Enchantment;
				Modifier.Value = Each.Value;
				FCataclysmStatInputs Inputs;
				Inputs.Base = 0.0f;
				Inputs.Modifiers.Add(Modifier);
				Stats.Add(Each.Key, Inputs);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** One regeneration step's worth of the pound: how many enemies it struck. */
		int32 Step() const
		{
			return UCataclysmGroundPound::Step(Actor);
		}

		float SecondsUntilReady() const
		{
			return UCataclysmGroundPound::SecondsUntilReady(AbilitySystem);
		}

		/** A kill credited to the wearer, raised as the player character raises it. */
		void Kill() const
		{
			AbilitySystem->ActOnEvent(FName(TEXT("kill")));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * A creature of the monsters' side at `Where`, with nothing to meet a blow with, on the rarity rung asked for.
	 * THE RUNG FIRST, because setting it applies the creature's starting attributes again.
	 */
	ACataclysmEnemyCharacter* BruteTenEnemyAt(UWorld* World, const FVector& Where, int32 RarityStep = 0)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			if (RarityStep > 0)
			{
				Made->SetRarityStep(RarityStep);
			}
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(BruteTenEnemyHealth);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}

	float BruteTenHealthOf(const AActor* Enemy)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Enemy);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : -1.0f;
	}

	/** What a creature of the Common rung has lost since it was made. */
	float BruteTenLost(const AActor* Enemy)
	{
		return BruteTenEnemyHealth - BruteTenHealthOf(Enemy);
	}

	/** The longest time left on a stun on `Enemy`, or nought. */
	float BruteTenStunLeft(const AActor* Enemy)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Enemy);
		const FGameplayTag Stunned = UCataclysmSkillEffects::StunnedTag();
		float Longest = 0.0f;
		if (System && Stunned.IsValid())
		{
			for (const float Seconds : System->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Stunned))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	bool BruteTenIsStunned(const AActor* Enemy)
	{
		return UCataclysmSkillEffects::HasTag(Enemy, UCataclysmSkillEffects::StunnedTag());
	}

	/** Destroy the creatures still standing at the end of a case. */
	void BruteTenClear(const TArray<ACataclysmEnemyCharacter*>& Enemies)
	{
		for (ACataclysmEnemyCharacter* Enemy : Enemies)
		{
			if (IsValid(Enemy))
			{
				Enemy->Destroy();
			}
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a `using
// namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteTenNotReadyTest,
	"Cataclysm.SetBonus.Brute10.ItIsNotReadyUntilThirtySecondsAfterItIsArmedAndThenStrikesAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The clock, Q5 and the owner's #5. An enemy stands 5 m off from the start. THE CONTROL IS READ FIRST: the first step
 * arms the clock and strikes nothing. A quarter of a second before 30 s it still strikes nothing; at 30 s it strikes
 * the enemy, and the clock is a full 30 s again.
 */
bool FCataclysmBruteTenNotReadyTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteTenWearer Wearer(World);
	ACataclysmEnemyCharacter* Enemy = BruteTenEnemyAt(World, FVector(5 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an enemy 5 m off"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteTenClear({Enemy}); };
	Wearer.Wear();

	TestEqual(TEXT("control: the first step strikes nothing"), Wearer.Step(), 0);
	TestEqual(TEXT("and it arms the clock at 30 s"), Wearer.SecondsUntilReady(), BruteTenEverySeconds,
			  BruteTenClockAllowance);

	World->TimeSeconds += BruteTenEverySeconds - 0.25f;
	TestEqual(TEXT("29.75 s on, with an enemy inside, it strikes nothing"), Wearer.Step(), 0);
	TestEqual(TEXT("and the enemy has lost nothing"), BruteTenLost(Enemy), 0.0f, 0.01f);

	World->TimeSeconds += 0.25f;
	TestEqual(TEXT("at 30 s it strikes the one enemy"), Wearer.Step(), 1);
	TestTrue(FString::Printf(TEXT("which has lost something: %.2f"), BruteTenLost(Enemy)), BruteTenLost(Enemy) > 0.0f);
	TestEqual(TEXT("and the clock is 30 s again"), Wearer.SecondsUntilReady(), BruteTenEverySeconds,
			  BruteTenClockAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteTenHoldsFireTest,
	"Cataclysm.SetBonus.Brute10.ReadyWithNoEnemyInsideItHoldsFireAndStrikesTheFirstStepOneIsInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The firing rule, the owner's #5. An enemy stands 12 m off, outside the 8 m. THE CONTROL IS READ FIRST: at 30 s the
 * pound is ready and strikes nothing. Fifteen seconds later it still holds. A second enemy placed 5 m off is struck at
 * the next step, and the one at 12 m is not.
 */
bool FCataclysmBruteTenHoldsFireTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteTenWearer Wearer(World);
	ACataclysmEnemyCharacter* Far = BruteTenEnemyAt(World, FVector(12 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an enemy 12 m off"), Far))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteTenClear({Far}); };
	Wearer.Wear();
	Wearer.Step();

	World->TimeSeconds += BruteTenEverySeconds;
	TestEqual(TEXT("control: at 30 s, with no enemy inside, it strikes nothing"), Wearer.Step(), 0);
	TestEqual(TEXT("and it is ready"), Wearer.SecondsUntilReady(), 0.0f, BruteTenClockAllowance);

	World->TimeSeconds += 15.0f;
	TestEqual(TEXT("15 s later it still holds"), Wearer.Step(), 0);
	TestEqual(TEXT("and the enemy at 12 m has lost nothing"), BruteTenLost(Far), 0.0f, 0.01f);

	ACataclysmEnemyCharacter* Near = BruteTenEnemyAt(World, FVector(0, 5 * M, 0));
	if (!TestNotNull(TEXT("set-up: an enemy placed 5 m off"), Near))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteTenClear({Near}); };
	TestEqual(TEXT("the next step strikes one enemy"), Wearer.Step(), 1);
	TestTrue(FString::Printf(TEXT("the one 5 m off: it lost %.2f"), BruteTenLost(Near)), BruteTenLost(Near) > 0.0f);
	TestEqual(TEXT("and not the one at 12 m"), BruteTenLost(Far), 0.0f, 0.01f);
	TestEqual(TEXT("and the clock is 30 s again"), Wearer.SecondsUntilReady(), BruteTenEverySeconds,
			  BruteTenClockAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteTenBlowTest,
	"Cataclysm.SetBonus.Brute10.TheBlowIsFiveHundredPercentOfArmourWithinEightMetresAndNothingBeyond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The blow, Q1 and Q4: 500% of the wearer's armour to every enemy within 8 m, as `smite_nearby_by_armor` deals it.
 * Enemies at 7 m and 10 m. THE CONTROL IS READ FIRST: neither has lost anything before the pound. With 1000 armour the
 * one at 7 m loses 5000 and the one at 10 m nothing. The armour is read when the pound fires: raised to 2000, the next
 * pound takes 10000 more.
 */
bool FCataclysmBruteTenBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteTenWearer Wearer(World);
	ACataclysmEnemyCharacter* Inside = BruteTenEnemyAt(World, FVector(7 * M, 0, 0));
	ACataclysmEnemyCharacter* Outside = BruteTenEnemyAt(World, FVector(0, 10 * M, 0));
	if (!TestNotNull(TEXT("set-up: an enemy 7 m off"), Inside)
		|| !TestNotNull(TEXT("set-up: an enemy 10 m off"), Outside))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteTenClear({Inside, Outside}); };
	Wearer.Wear();
	Wearer.Step();

	TestEqual(TEXT("control: before the pound the one at 7 m has lost nothing"), BruteTenLost(Inside), 0.0f, 0.01f);
	TestEqual(TEXT("control: nor has the one at 10 m"), BruteTenLost(Outside), 0.0f, 0.01f);

	World->TimeSeconds += BruteTenEverySeconds;
	TestEqual(TEXT("the pound strikes one enemy"), Wearer.Step(), 1);
	TestEqual(TEXT("the one at 7 m loses 500% of the wearer's 1000 armour"), BruteTenLost(Inside),
			  BruteTenArmour * BruteTenArmourPercent / 100.0f, 0.5f);
	TestEqual(TEXT("the one at 10 m loses nothing"), BruteTenLost(Outside), 0.0f, 0.01f);

	const float AfterTheFirst = BruteTenLost(Inside);
	Wearer.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(),
												  2.0f * BruteTenArmour);
	World->TimeSeconds += BruteTenEverySeconds;
	Wearer.Step();
	TestEqual(TEXT("with 2000 armour the next pound takes 10000 more: the armour is read as it fires"),
			  BruteTenLost(Inside) - AfterTheFirst, 2.0f * BruteTenArmour * BruteTenArmourPercent / 100.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteTenStunTest,
	"Cataclysm.SetBonus.Brute10.EachEnemyStruckIsStunnedForThreeSecondsLessItsResistanceAndABossIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The stun, Q2: the ordinary stun for 3 s on each enemy struck, shortened by crowd control resistance; a boss, as the
 * stun's own rules treat one, is never stunned. Two creatures, one holding 50 crowd control resistance and a boss
 * inside 8 m. THE CONTROL IS READ FIRST: none is stunned before the pound. After it each plain creature has 3 s of
 * stun, the resisting one 1.5 s, and the boss none, though the boss was struck. 2.9 s on the two are still stunned and
 * the resisting one is not, and 0.2 s after that neither is.
 */
bool FCataclysmBruteTenStunTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteTenWearer Wearer(World);
	ACataclysmEnemyCharacter* First = BruteTenEnemyAt(World, FVector(4 * M, 0, 0));
	ACataclysmEnemyCharacter* Second = BruteTenEnemyAt(World, FVector(-4 * M, 0, 0));
	ACataclysmEnemyCharacter* Resisting = BruteTenEnemyAt(World, FVector(0, -4 * M, 0));
	ACataclysmEnemyCharacter* Boss = BruteTenEnemyAt(World, FVector(0, 5 * M, 0), /*RarityStep=*/4);
	if (!TestNotNull(TEXT("set-up: a creature 4 m east"), First)
		|| !TestNotNull(TEXT("set-up: a creature 4 m west"), Second)
		|| !TestNotNull(TEXT("set-up: a creature 4 m south"), Resisting)
		|| !TestNotNull(TEXT("set-up: a creature 5 m north"), Boss))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteTenClear({First, Second, Resisting, Boss}); };
	Resisting->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 50.0f);
	if (!TestTrue(TEXT("set-up: the Boss rung is a boss"), Boss->IsBoss()))
	{
		return false;
	}
	Wearer.Wear();
	Wearer.Step();
	const float BossBefore = BruteTenHealthOf(Boss);

	TestFalse(TEXT("control: before the pound the first creature is not stunned"), BruteTenIsStunned(First));
	TestFalse(TEXT("control: nor the second"), BruteTenIsStunned(Second));

	World->TimeSeconds += BruteTenEverySeconds;
	TestEqual(TEXT("the pound strikes all four"), Wearer.Step(), 4);
	TestEqual(TEXT("the first creature is stunned for 3 s"), BruteTenStunLeft(First), BruteTenStunSeconds, 0.01f);
	TestEqual(TEXT("and the second"), BruteTenStunLeft(Second), BruteTenStunSeconds, 0.01f);
	TestEqual(TEXT("the one holding 50 crowd control resistance for 1.5 s"), BruteTenStunLeft(Resisting),
			  BruteTenStunSeconds * 0.5f, 0.01f);
	const float BossLost = BossBefore - BruteTenHealthOf(Boss);
	TestTrue(FString::Printf(TEXT("the boss was struck: it lost %.2f"), BossLost), BossLost > 0.0f);
	TestFalse(TEXT("and is not stunned"), BruteTenIsStunned(Boss));

	CataclysmTestWorld::RunClock(World, 2.9f);
	TestTrue(TEXT("2.9 s on the first is still stunned"), BruteTenIsStunned(First));
	TestTrue(TEXT("and the second"), BruteTenIsStunned(Second));
	TestFalse(TEXT("and the resisting one is not"), BruteTenIsStunned(Resisting));
	CataclysmTestWorld::RunClock(World, 0.2f);
	TestFalse(TEXT("0.2 s after that the first is not"), BruteTenIsStunned(First));
	TestFalse(TEXT("nor the second"), BruteTenIsStunned(Second));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteTenKillTest,
	"Cataclysm.SetBonus.Brute10.EachKillTakesHalfASecondOffNeverBelowNoughtAndAKillWhileReadyBanksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The kill rule, Q3. No enemy stands inside at first. THE CONTROL IS READ FIRST: the clock is 30 s. A kill leaves 29.5
 * and three more 28. With 0.1 s left a kill makes it ready and not less than ready. Three kills while it is ready, and
 * an enemy then placed 5 m off: the pound strikes it and the clock is a whole 30 s, nothing banked. One kill after
 * that leaves 29.5.
 */
bool FCataclysmBruteTenKillTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteTenWearer Wearer(World);
	Wearer.Wear();
	Wearer.Step();
	TestEqual(TEXT("control: the clock is 30 s"), Wearer.SecondsUntilReady(), BruteTenEverySeconds,
			  BruteTenClockAllowance);

	Wearer.Kill();
	TestEqual(TEXT("a kill leaves 29.5 s"), Wearer.SecondsUntilReady(), 29.5f, BruteTenClockAllowance);
	Wearer.Kill();
	Wearer.Kill();
	Wearer.Kill();
	TestEqual(TEXT("three more leave 28 s"), Wearer.SecondsUntilReady(), 28.0f, BruteTenClockAllowance);

	World->TimeSeconds += 27.9f;
	TestEqual(TEXT("27.9 s on, 0.1 s is left"), Wearer.SecondsUntilReady(), 0.1f, 0.01f);
	Wearer.Kill();
	TestEqual(TEXT("a kill makes it ready, and never less than ready"), Wearer.SecondsUntilReady(), 0.0f,
			  BruteTenClockAllowance);
	TestEqual(TEXT("with no enemy inside it holds"), Wearer.Step(), 0);

	Wearer.Kill();
	Wearer.Kill();
	Wearer.Kill();
	ACataclysmEnemyCharacter* Near = BruteTenEnemyAt(World, FVector(5 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an enemy placed 5 m off"), Near))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteTenClear({Near}); };
	TestEqual(TEXT("after three kills while ready the pound strikes the enemy"), Wearer.Step(), 1);
	TestEqual(TEXT("and the clock is a whole 30 s: the kills while ready banked nothing"), Wearer.SecondsUntilReady(),
			  BruteTenEverySeconds, BruteTenClockAllowance);
	Wearer.Kill();
	TestEqual(TEXT("one kill after it leaves 29.5 s"), Wearer.SecondsUntilReady(), 29.5f, BruteTenClockAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteTenTakenOffTest,
	"Cataclysm.SetBonus.Brute10.TakingTheBonusOffForgetsTheClockAndPuttingItOnStartsItFromThirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q5, "first ready 30 s after the tenth piece goes on". THE CONTROL IS READ FIRST: a wearer with no such stat has no
 * clock. Put on, it is 30 s; 20 s on, 10 s. Taken off, the clock is forgotten; put on again, it is 30 s and not 10.
 */
bool FCataclysmBruteTenTakenOffTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteTenWearer Wearer(World);
	Wearer.Wear(/*EverySeconds=*/0.0f);
	Wearer.Step();
	TestEqual(TEXT("control: with no seconds stated there is no clock"), Wearer.SecondsUntilReady(), -1.0f,
			  BruteTenClockAllowance);

	Wearer.Wear();
	Wearer.Step();
	TestEqual(TEXT("put on, it is ready in 30 s"), Wearer.SecondsUntilReady(), BruteTenEverySeconds,
			  BruteTenClockAllowance);
	World->TimeSeconds += 20.0f;
	TestEqual(TEXT("20 s on, in 10 s"), Wearer.SecondsUntilReady(), 10.0f, 0.01f);

	Wearer.Wear(/*EverySeconds=*/0.0f);
	Wearer.Step();
	TestEqual(TEXT("taken off, the clock is forgotten"), Wearer.SecondsUntilReady(), -1.0f, BruteTenClockAllowance);

	Wearer.Wear();
	Wearer.Step();
	TestEqual(TEXT("put on again, it is ready in 30 s and not in 10"), Wearer.SecondsUntilReady(),
			  BruteTenEverySeconds, BruteTenClockAllowance);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
