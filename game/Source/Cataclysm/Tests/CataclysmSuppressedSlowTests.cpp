// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

/**
 * A row saying nothing may lower the character's speed is an immunity to Slow (the Death trees' slice C6d2,
 * 2026-11-25, ruled by the coordinating session as a fix to bundle 1's ruling that an immunity to Slow works
 * everywhere). `UCataclysmSkillTemplate::IsImmuneTo` answers true for Slow while `movement_speed_reduction_suppressed`
 * stands above nought, so Unstoppable, Nothing Moves You and Relentless are no longer slowed by a War aura's movement
 * reduction, which only that answer drops.
 */

namespace CataclysmSuppressedSlowTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	/** How the player is set up before Blood and Iron's 15% enemy movement reduction is held on it. */
	enum class EHolds : uint8
	{
		Nothing,
		UnstoppablesRow,
		RelentlessAttribute,
	};

	struct FWalkResult
	{
		bool bMadeIt = false;
		float Unslowed = 0.0f;
		float Slowed = 0.0f;
		bool bImmune = false;
	};

	/**
	 * A player at 5 m a second with a monster 2 metres away, holding `Holds`, then a 15% movement reduction held on it
	 * as Blood and Iron holds one on an enemy.
	 */
	FWalkResult WalkUnderAReduction(EHolds Holds)
	{
		FWalkResult Result;
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return Result;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		UCataclysmAbilitySystemComponent* AbilitySystem =
			PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		ACataclysmEnemyCharacter* Enemy =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(2.0f * M, 0, 0), FRotator::ZeroRotator);
		if (!AbilitySystem || !Character || !Enemy)
		{
			return Result;
		}
		Enemy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Enemy->SetHealth(1'000'000.0f);
		Enemy->SetAttackDamage(0.0f);
		AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(), 5.0f);
		Character->SetPlayerState(PlayerState);
		Character->OnRep_PlayerState();

		if (Holds == EHolds::UnstoppablesRow)
		{
			// UNSTOPPABLE'S ROW AS THE SHEET WRITES IT: flat 1 while an enemy is within 4 metres.
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = 1.0f;
			Row.Condition = ECataclysmStatCondition::EnemiesInReachAtLeast;
			Row.ConditionValue = 1.0f;
			Row.ReachMetres = 4.0f;
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.FindOrAdd(FName(TEXT("movement_speed_reduction_suppressed"))).Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Lines));
		}
		else if (Holds == EHolds::RelentlessAttribute)
		{
			// RELENTLESS'S ROW HAS NO CONDITION, so it is folded into the attribute and no line states it.
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetMovementSpeedReductionSuppressedAttribute(), 1.0f);
		}

		auto Walk = [Character]()
		{
			Character->RefreshMovementSpeed();
			return Character->GetCharacterMovement()->MaxWalkSpeed / 100.0f;
		};
		Result.Unslowed = Walk();
		AbilitySystem->HoldAuraShare(ECataclysmAuraShareKind::MovementReduced, Character, 15.0f, 60.0f);
		Result.Slowed = Walk();
		Result.bImmune = UCataclysmSkillTemplate::IsImmuneTo(Character, TEXT("Slow"));
		Result.bMadeIt = true;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSuppressedSlowUnstoppableTest,
	"Cataclysm.SuppressedSlow.UnstoppablesRowIsNotSlowedByAWarAurasMovementReduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A player holding Unstoppable's row, with an enemy 2 metres away, is immune to Slow and walks at 5 under a 15%
 * movement reduction held as Blood and Iron holds one. CONTROL: a player holding nothing walks at 4.25 and is not
 * immune. Before the slice the row's holder walked at 4.25 too, because the suppression branch of
 * `RefreshMovementSpeed` dropped the reducing rows of `movement_speed` and multiplied the aura's share in.
 */
bool FCataclysmSuppressedSlowUnstoppableTest::RunTest(const FString&)
{
	using namespace CataclysmSuppressedSlowTest;

	const FWalkResult Holder = WalkUnderAReduction(EHolds::UnstoppablesRow);
	const FWalkResult Plain = WalkUnderAReduction(EHolds::Nothing);
	if (!TestTrue(TEXT("set-up: both worlds were made"), Holder.bMadeIt && Plain.bMadeIt))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("UNSTOPPABLE UNDER A 15%% REDUCTION: holder %.4f -> %.4f, immune %s; plain %.4f -> "
								 "%.4f, immune %s (metres a second)"),
							Holder.Unslowed, Holder.Slowed, Holder.bImmune ? TEXT("yes") : TEXT("no"), Plain.Unslowed,
							Plain.Slowed, Plain.bImmune ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("set-up: the holder walks at 5"), Holder.Unslowed, 5.0f, 0.01f);
	TestTrue(TEXT("Unstoppable's row is an immunity to Slow"), Holder.bImmune);
	TestEqual(TEXT("so the War aura's reduction is dropped and the walk stays 5"), Holder.Slowed, 5.0f, 0.01f);
	TestEqual(TEXT("CONTROL: a player holding nothing walks at 5"), Plain.Unslowed, 5.0f, 0.01f);
	TestFalse(TEXT("CONTROL: is not immune"), Plain.bImmune);
	TestEqual(TEXT("CONTROL: and is slowed to 4.25"), Plain.Slowed, 4.25f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSuppressedSlowRelentlessTest,
	"Cataclysm.SuppressedSlow.RelentlessFoldedIntoTheAttributeIsNotSlowedByAWarAurasMovementReduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Relentless's row has no condition, so it is folded into the `MovementSpeedReductionSuppressed` attribute and no stat
 * line states it. `IsImmuneTo` asks the stat with the attribute as the answer when no row states it, as the movement
 * refresh does, so the attribute alone makes the player immune: 5 under the 15% reduction.
 */
bool FCataclysmSuppressedSlowRelentlessTest::RunTest(const FString&)
{
	using namespace CataclysmSuppressedSlowTest;

	const FWalkResult Holder = WalkUnderAReduction(EHolds::RelentlessAttribute);
	if (!TestTrue(TEXT("set-up: the world was made"), Holder.bMadeIt))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("RELENTLESS UNDER A 15%% REDUCTION: %.4f -> %.4f, immune %s (metres a second)"),
							Holder.Unslowed, Holder.Slowed, Holder.bImmune ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("set-up: the holder walks at 5"), Holder.Unslowed, 5.0f, 0.01f);
	TestTrue(TEXT("the attribute alone is an immunity to Slow"), Holder.bImmune);
	TestEqual(TEXT("so the War aura's reduction is dropped and the walk stays 5"), Holder.Slowed, 5.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
