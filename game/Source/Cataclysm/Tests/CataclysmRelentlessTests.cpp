// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * RELENTLESS. The Ravager tree rework of 2026-11-24.
 *
 * `Ravager_keystone_d_kA` Relentless now reads "Your Movement Speed and Attack Speed cannot be reduced by any effect,
 * and you gain 1% increased Attack Speed for every 2% increased Movement Speed you have." Three rows: the movement
 * flag it always had, `attack_speed_reduction_suppressed`, and `attack_speed` increased 1 scaled by the new scale
 * `increased_movement_speed` with a step of 2.
 *
 * THE ATTACK SPEED HALF IS READ IN ONE PLACE, `UCataclysmBasicAttack::AttackSpeedAfterSlows`, which the basic attack's
 * swing and a projectile skill's rack both ask. Under the flag every row stating a figure below nought is dropped and
 * Cripple's and Wither's share is not applied; the earned rows stay.
 *
 * THE SCALE IS FILLED BY `UCataclysmAbilitySystemComponent::WithEnemiesInReach` from the INCREASES bucket of the
 * character's own `movement_speed` line, only when a row asks, and leaving out any movement row that itself asks for
 * the scale.
 *
 * WRITTEN WITHOUT A BUILD, from the source, on 2026-11-24.
 */
namespace CataclysmRelentlessTest
{
	/** A bare swinger whose attack speed attribute is one a second. */
	struct FRelentlessSwinger
	{
		explicit FRelentlessSwinger(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
		}

		~FRelentlessSwinger()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	FCataclysmStatModifier RelentlessRow(ECataclysmStatBucket Bucket, float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		return Row;
	}

	/** An attack speed line of one a second carrying +40% and -50%, and the flag when `bHeld`. */
	TMap<FName, FCataclysmStatInputs> FasterAndSlowerLine(bool bHeld)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Speed = Inputs.FindOrAdd(FName(TEXT("attack_speed")));
		Speed.Base = 1.0f;
		Speed.Modifiers = {RelentlessRow(ECataclysmStatBucket::Increased, 40.0f),
						   RelentlessRow(ECataclysmStatBucket::Increased, -50.0f)};
		if (bHeld)
		{
			Inputs.FindOrAdd(FName(UCataclysmBasicAttack::AttackSpeedReductionSuppressedStat)).Modifiers =
				{RelentlessRow(ECataclysmStatBucket::Flat, 1.0f)};
		}
		return Inputs;
	}
}

#define CATACLYSM_RELENTLESS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// RL1. A row lowering attack speed is dropped and an earned one kept.
// --------------------------------------------------------------------------------------------------------------------
/**
 * One a second, +40% and -50%. Without the flag the increases sum to -10%: 0.9 a second, 1.111 seconds a swing. With it
 * the -50% is dropped and the +40% kept: 1.4 a second, 0.714 seconds. Flooring the sum instead would give one a
 * second, which is why the assertion is on 1.4. Prints "RELENTLESS ROWS".
 */
CATACLYSM_RELENTLESS_TEST(FCataclysmRelentlessDropsReducingRowsTest,
	"Cataclysm.Relentless.ARowLoweringAttackSpeedIsDroppedAndAnEarnedOneKept")
{
	using namespace CataclysmRelentlessTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FRelentlessSwinger Swinger(World);

	Swinger.AbilitySystem->SetStatInputs(FasterAndSlowerLine(false));
	const float Without = UCataclysmBasicAttack::SecondsBetweenSwingsFor(Swinger.AbilitySystem);
	Swinger.AbilitySystem->SetStatInputs(FasterAndSlowerLine(true));
	const float Held = UCataclysmBasicAttack::SecondsBetweenSwingsFor(Swinger.AbilitySystem);
	AddInfo(FString::Printf(TEXT("RELENTLESS ROWS: without %.4f s, held %.4f s"), Without, Held));

	TestEqual(TEXT("CONTROL: without the flag the -50% wins out over the +40%: 1/0.9 seconds"), Without,
			  1.0f / 0.9f, 0.0005f);
	TestEqual(TEXT("**with it the -50% is dropped and the +40% kept: 1/1.4 seconds**"), Held, 1.0f / 1.4f, 0.0005f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// RL2. Cripple does not slow the swing of a player holding the flag.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A real player at one swing a second, carrying a Cripple of 30. Without the flag the swing waits 1/0.7 seconds; with
 * it, one second. The player is built the way `CataclysmPlayerMovementTests.cpp` builds one, because Cripple's share
 * is read off a player character only. Prints "RELENTLESS CRIPPLE".
 */
CATACLYSM_RELENTLESS_TEST(FCataclysmRelentlessIgnoresCrippleTest,
	"Cataclysm.Relentless.CrippleDoesNotSlowTheSwingOfAPlayerHoldingIt")
{
	using namespace CataclysmRelentlessTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player state, an ability system and a pawn"), State && AbilitySystem && Character))
	{
		return false;
	}
	Character->SetPlayerState(State);
	Character->OnRep_PlayerState();
	AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);

	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	if (!TestTrue(TEXT("set-up: the player carries a Cripple of 30"),
				  Cripple.IsValid()
					  && UCataclysmSkillEffects::ApplyTagForDuration(Character, Character, Cripple, 30.0f, 30.0f)))
	{
		return false;
	}

	const auto Line = [](bool bHeld)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		Inputs.FindOrAdd(FName(TEXT("attack_speed"))).Base = 1.0f;
		if (bHeld)
		{
			Inputs.FindOrAdd(FName(UCataclysmBasicAttack::AttackSpeedReductionSuppressedStat)).Modifiers =
				{RelentlessRow(ECataclysmStatBucket::Flat, 1.0f)};
		}
		return Inputs;
	};
	AbilitySystem->SetStatInputs(Line(false));
	const float Without = UCataclysmBasicAttack::SecondsBetweenSwingsFor(AbilitySystem);
	AbilitySystem->SetStatInputs(Line(true));
	const float Held = UCataclysmBasicAttack::SecondsBetweenSwingsFor(AbilitySystem);
	AddInfo(FString::Printf(TEXT("RELENTLESS CRIPPLE: without %.4f s, held %.4f s, share %.3f"), Without, Held,
							UCataclysmSkillEffects::CrippleMultiplierOn(Character)));

	TestEqual(TEXT("set-up: the Cripple leaves 70% of a player's speed"),
			  UCataclysmSkillEffects::CrippleMultiplierOn(Character), 0.7f, 0.001f);
	TestEqual(TEXT("CONTROL: without the flag the Cripple slows the swing: 1/0.7 seconds"), Without, 1.0f / 0.7f,
			  0.0005f);
	TestEqual(TEXT("**with it the swing is not slowed: one second**"), Held, 1.0f, 0.0005f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// RL3. Attack speed grows a step for every two points of increased movement speed.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Relentless's third row on a line of one a second: 1% increased for every 2% increased movement speed. A movement
 * line carrying +25% gives twelve whole steps, +12%: 1.12 a second. A second movement row scaled by the same scale is
 * left out of the reading, so the answer is the same and the lookup ends. With no increases, or increases summing
 * below nought, nothing. Prints "RELENTLESS SCALE".
 */
CATACLYSM_RELENTLESS_TEST(FCataclysmRelentlessScaleTest,
	"Cataclysm.Relentless.AttackSpeedGrowsAStepForEveryTwoPointsOfIncreasedMovementSpeed")
{
	using namespace CataclysmRelentlessTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FRelentlessSwinger Swinger(World);

	const auto RatePerSecond = [&Swinger](const TArray<FCataclysmStatModifier>& MovementRows)
	{
		FCataclysmStatModifier PerStep = RelentlessRow(ECataclysmStatBucket::Increased, 1.0f);
		PerStep.Scale = ECataclysmStatScale::PerPercentOfIncreasedMovementSpeed;
		PerStep.ScaleStep = 2.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Speed = Inputs.FindOrAdd(FName(TEXT("attack_speed")));
		Speed.Base = 1.0f;
		Speed.Modifiers = {PerStep};
		FCataclysmStatInputs& Movement = Inputs.FindOrAdd(FName(TEXT("movement_speed")));
		Movement.Base = 4.0f;
		Movement.Modifiers = MovementRows;
		Swinger.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		const float Seconds = UCataclysmBasicAttack::SecondsBetweenSwingsFor(Swinger.AbilitySystem);
		return Seconds > 0.0f ? 1.0f / Seconds : 0.0f;
	};

	FCataclysmStatModifier AskingForItself = RelentlessRow(ECataclysmStatBucket::Increased, 10.0f);
	AskingForItself.Scale = ECataclysmStatScale::PerPercentOfIncreasedMovementSpeed;
	AskingForItself.ScaleStep = 1.0f;

	const float WithTwentyFive = RatePerSecond({RelentlessRow(ECataclysmStatBucket::Increased, 25.0f)});
	const float WithItself = RatePerSecond({RelentlessRow(ECataclysmStatBucket::Increased, 25.0f), AskingForItself});
	const float WithNone = RatePerSecond({});
	const float WithLess = RatePerSecond({RelentlessRow(ECataclysmStatBucket::Increased, -30.0f)});
	AddInfo(FString::Printf(TEXT("RELENTLESS SCALE: +25%% %.4f; with a self-asking row %.4f; none %.4f; -30%% %.4f"),
							WithTwentyFive, WithItself, WithNone, WithLess));

	TestEqual(TEXT("**+25% increased movement speed is twelve whole steps: 1.12 a second**"), WithTwentyFive, 1.12f,
			  0.0005f);
	TestEqual(TEXT("**a movement row asking for the scale is left out of the reading, so the same 1.12**"), WithItself,
			  1.12f, 0.0005f);
	TestEqual(TEXT("CONTROL: no increased movement speed, one a second"), WithNone, 1.0f, 0.0005f);
	TestEqual(TEXT("and increases below nought count nothing, one a second"), WithLess, 1.0f, 0.0005f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// RL4. The flag is recorded and the scale is named.
// --------------------------------------------------------------------------------------------------------------------
CATACLYSM_RELENTLESS_TEST(FCataclysmRelentlessNamesTest,
	"Cataclysm.Relentless.TheFlagIsRecordedAndTheScaleIsNamed")
{
	TestTrue(TEXT("attack_speed_reduction_suppressed is on the exemption list"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmBasicAttack::AttackSpeedReductionSuppressedStat)));
	ECataclysmStatScale Read = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("the pipeline knows the scale by the data's name"),
			 UCataclysmStatPipeline::ScaleNamed(TEXT("increased_movement_speed"), Read));
	TestTrue(TEXT("and it is the scale this file grants"),
			 Read == ECataclysmStatScale::PerPercentOfIncreasedMovementSpeed);
	return true;
}

#undef CATACLYSM_RELENTLESS_TEST

#endif // WITH_AUTOMATION_TESTS
