// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * The movement skill's two clocks (the Death trees' slice C6b2, 2026-11-25): Arrival counts from the END of a movement
 * skill use that was paid for, and Glancing Cuts from a landed blow of the asker's movement skill on the creature asked
 * about, a record kept on that creature. Two tests use a real skill; two ask the stat pipeline with rows given by hand;
 * one reads the real rows.
 */

namespace CataclysmMovementClocksTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float ClockMetre = 100.0f;

	/** A character on the players' side at `Where`, with 100 attack damage and 1,000 mana. */
	UCataclysmAbilitySystemComponent* AskerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Holder)
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Holder->SetAttackDamage(100.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
		if (System)
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}
		return System;
	}

	/** A creature of the monsters' side at `Where`, with 1,000 health. */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000.0f);
		}
		return Made;
	}

	/** A movement skill, a blink of `Params`, granted in `Slot`; null if it could not be granted. */
	UCataclysmMovementSkill* GrantBlink(UCataclysmAbilitySystemComponent* System, ECataclysmAbilitySlot Slot,
										FGameplayAbilitySpecHandle& OutHandle)
	{
		OutHandle = System->GiveAbilityInSlot(UCataclysmMovementSkill::StaticClass(), Slot, /*Level=*/100,
											  System->GetAvatarActor());
		FGameplayAbilitySpec* Spec = OutHandle.IsValid() ? System->FindAbilitySpecFromHandle(OutHandle) : nullptr;
		UCataclysmMovementSkill* Blink = Spec ? Cast<UCataclysmMovementSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Blink)
		{
			Blink->SkillName = TEXT("A test blink");
			Blink->Params = UCataclysmSkillShapes::ParseParams(TEXT("Mode=Blink; Range=4; Radius=1"));
		}
		return Blink;
	}

	FCataclysmStatModifier ClockRow(float Value, ECataclysmStatCondition Condition, float ConditionValue)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		return Row;
	}

	/** One `attack_damage` line at 100 with `Row`, and nothing else. */
	void GiveDamageLine(UCataclysmAbilitySystemComponent* System, const FCataclysmStatModifier& Row)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line = Lines.Add(FName(TEXT("attack_damage")));
		Line.Base = 100.0f;
		Line.Modifiers = {Row};
		System->SetStatInputs(MoveTemp(Lines));
	}

	/** `attack_damage` against `Target`. */
	float DamageAgainst(const UCataclysmAbilitySystemComponent* System, const AActor* Target)
	{
		return System->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f, -1.0f,
									FCataclysmBlowContext(), -1.0f, -1.0f, false, Target);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementClocksArrivalTest,
	"Cataclysm.MovementClocks.ArrivalCountsFromTheEndOfTheMoveAndNotItsUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Arrival's row, 3% increased attack damage on a base of 100 within 1 second after the movement skill ends: 100 before
 * anything, still 100 after a USE alone (the use's clock is a different one), and 103 once the end is noted. THE
 * BOUNDARY, asked of the pipeline: 1 second after reads true (inclusive), 1.5 false, and never (-1) false.
 */
bool FCataclysmMovementClocksArrivalTest::RunTest(const FString&)
{
	using namespace CataclysmMovementClocksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = AskerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(3 * ClockMetre, 0, 0));
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	GiveDamageLine(System, ClockRow(3.0f, ECataclysmStatCondition::WithinSecondsOfMovementSkillEnd, 1.0f));

	TestEqual(TEXT("control: before any move, 100"), DamageAgainst(System, Creature), 100.0f, 0.001f);
	System->NoteMovementSkillUsed();
	TestEqual(TEXT("control: a use alone, 100"), DamageAgainst(System, Creature), 100.0f, 0.001f);
	System->NoteMovementSkillEnded();
	TestEqual(TEXT("the move has ended: 103"), DamageAgainst(System, Creature), 103.0f, 0.001f);

	FCataclysmStatConditions State;
	const ECataclysmStatCondition Clock = ECataclysmStatCondition::WithinSecondsOfMovementSkillEnd;
	State.SecondsSinceMovementSkillEnded = 1.0f;
	TestTrue(TEXT("1 second after: holds (inclusive)"), UCataclysmStatPipeline::ConditionHolds(Clock, 1.0f, State));
	State.SecondsSinceMovementSkillEnded = 1.5f;
	TestFalse(TEXT("control: 1.5 seconds after, no"), UCataclysmStatPipeline::ConditionHolds(Clock, 1.0f, State));
	State.SecondsSinceMovementSkillEnded = -1.0f;
	TestFalse(TEXT("control: never, no"), UCataclysmStatPipeline::ConditionHolds(Clock, 1.0f, State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementClocksRealEndTest,
	"Cataclysm.MovementClocks.ARealBlinkInTheMovementSlotNotesItsEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A real blink, granted in the Movement slot and used once, has ended by the time the use returns (a blink arrives in
 * the call that used it), so the end's clock reads 0 seconds. THE CONTROL: the same blink granted in the Special slot
 * leaves the clock unread (-1), as the use's clock is.
 */
bool FCataclysmMovementClocksRealEndTest::RunTest(const FString&)
{
	using namespace CataclysmMovementClocksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* Mover = AskerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Other = AskerAt(World, FVector(0, 20 * ClockMetre, 0));
	if (!TestNotNull(TEXT("set-up: a mover"), Mover) || !TestNotNull(TEXT("set-up: another"), Other))
	{
		return false;
	}
	FGameplayAbilitySpecHandle InMovement;
	FGameplayAbilitySpecHandle InSpecial;
	if (!TestNotNull(TEXT("set-up: a blink in the Movement slot"),
					 GrantBlink(Mover, ECataclysmAbilitySlot::Movement, InMovement))
		|| !TestNotNull(TEXT("set-up: a blink in the Special slot"),
						GrantBlink(Other, ECataclysmAbilitySlot::Special, InSpecial)))
	{
		return false;
	}
	TestEqual(TEXT("control: before the move, never"), Mover->SecondsSinceMovementSkillEnded(), -1.0f, 0.001f);
	if (!TestTrue(TEXT("the Movement slot's blink was used"), Mover->TryActivateAbility(InMovement))
		|| !TestTrue(TEXT("the Special slot's blink was used"), Other->TryActivateAbility(InSpecial)))
	{
		return false;
	}
	TestEqual(TEXT("the Movement slot's blink has ended: 0 seconds ago"), Mover->SecondsSinceMovementSkillEnded(), 0.0f,
			  0.001f);
	TestEqual(TEXT("control: the Special slot's blink, never"), Other->SecondsSinceMovementSkillEnded(), -1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementClocksMarkTest,
	"Cataclysm.MovementClocks.AMovementSkillsLandedBlowMarksWhatItStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `ApplyHit` with an area blow (which cannot be evaded) naming a blink granted in the Movement slot marks the creature
 * struck: its record for that striker reads 0 seconds. THE CONTROLS: the same blow naming a blink granted in the Special
 * slot leaves a second creature unmarked (-1), and the first creature's record for another striker reads -1.
 */
bool FCataclysmMovementClocksMarkTest::RunTest(const FString&)
{
	using namespace CataclysmMovementClocksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* Striker = AskerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Other = AskerAt(World, FVector(0, 20 * ClockMetre, 0));
	ACataclysmEnemyCharacter* Struck = CreatureAt(World, FVector(1 * ClockMetre, 0, 0));
	ACataclysmEnemyCharacter* StruckBySpecial = CreatureAt(World, FVector(0, 21 * ClockMetre, 0));
	if (!TestNotNull(TEXT("set-up: a striker"), Striker) || !TestNotNull(TEXT("set-up: another"), Other) || !Struck
		|| !StruckBySpecial)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}
	FGameplayAbilitySpecHandle Unused;
	UCataclysmMovementSkill* MovementBlink = GrantBlink(Striker, ECataclysmAbilitySlot::Movement, Unused);
	UCataclysmMovementSkill* SpecialBlink = GrantBlink(Other, ECataclysmAbilitySlot::Special, Unused);
	UCataclysmAbilitySystemComponent* StruckSystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Struck));
	UCataclysmAbilitySystemComponent* SpecialSystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(StruckBySpecial));
	if (!TestNotNull(TEXT("set-up: the Movement blink"), MovementBlink)
		|| !TestNotNull(TEXT("set-up: the Special blink"), SpecialBlink) || !TestNotNull(TEXT("set-up: struck"), StruckSystem)
		|| !TestNotNull(TEXT("set-up: struck by Special"), SpecialSystem))
	{
		return false;
	}

	FCataclysmHitDelivery ByMovement = FCataclysmHitDelivery::Area();
	ByMovement.Skill = MovementBlink;
	UCataclysmSkillEffects::ApplyHit(Striker->GetAvatarActor(), Struck, 10.0f, FGameplayTagContainer(), ByMovement);
	FCataclysmHitDelivery BySpecial = FCataclysmHitDelivery::Area();
	BySpecial.Skill = SpecialBlink;
	UCataclysmSkillEffects::ApplyHit(Other->GetAvatarActor(), StruckBySpecial, 10.0f, FGameplayTagContainer(),
									 BySpecial);

	TestEqual(TEXT("struck by the Movement slot's skill: 0 seconds ago"),
			  StruckSystem->SecondsSinceStruckByMovementSkillOf(Striker), 0.0f, 0.001f);
	TestEqual(TEXT("control: struck by the Special slot's skill, never"),
			  SpecialSystem->SecondsSinceStruckByMovementSkillOf(Other), -1.0f, 0.001f);
	TestEqual(TEXT("control: the record is the striker's, another reads never"),
			  StruckSystem->SecondsSinceStruckByMovementSkillOf(Other), -1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementClocksGlancingTest,
	"Cataclysm.MovementClocks.GlancingCutsHoldsForFourSecondsAgainstWhatYourMoveStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Glancing Cuts' row, 2% increased attack damage on a base of 100 against a creature your movement skill struck within 4
 * seconds: 102 against one marked for this asker. THE CONTROLS: 100 against one never marked, and 100 against one marked
 * for another striker. THE BOUNDARY, asked of the pipeline: 4 seconds reads true (inclusive), 4.5 false.
 */
bool FCataclysmMovementClocksGlancingTest::RunTest(const FString&)
{
	using namespace CataclysmMovementClocksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = AskerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Other = AskerAt(World, FVector(0, 20 * ClockMetre, 0));
	ACataclysmEnemyCharacter* Marked = CreatureAt(World, FVector(3 * ClockMetre, 0, 0));
	ACataclysmEnemyCharacter* Unmarked = CreatureAt(World, FVector(-3 * ClockMetre, 0, 0));
	ACataclysmEnemyCharacter* TheirMark = CreatureAt(World, FVector(0, 3 * ClockMetre, 0));
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !TestNotNull(TEXT("set-up: another"), Other) || !Marked
		|| !Unmarked || !TheirMark)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}
	UCataclysmAbilitySystemComponent* MarkedSystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Marked));
	UCataclysmAbilitySystemComponent* TheirSystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(TheirMark));
	if (!TestNotNull(TEXT("set-up: marked"), MarkedSystem) || !TestNotNull(TEXT("set-up: their mark"), TheirSystem))
	{
		return false;
	}
	MarkedSystem->NoteStruckByMovementSkillOf(System);
	TheirSystem->NoteStruckByMovementSkillOf(Other);
	GiveDamageLine(System,
				   ClockRow(2.0f, ECataclysmStatCondition::TargetStruckByYourMovementSkillWithinSeconds, 4.0f));

	TestEqual(TEXT("struck by your movement skill: 102"), DamageAgainst(System, Marked), 102.0f, 0.001f);
	TestEqual(TEXT("control: never struck, 100"), DamageAgainst(System, Unmarked), 100.0f, 0.001f);
	TestEqual(TEXT("control: struck by another's movement skill, 100"), DamageAgainst(System, TheirMark), 100.0f,
			  0.001f);

	FCataclysmStatConditions State;
	const ECataclysmStatCondition Clock = ECataclysmStatCondition::TargetStruckByYourMovementSkillWithinSeconds;
	State.SecondsSinceStruckByYourMovementSkill = 4.0f;
	TestTrue(TEXT("4 seconds after: holds (inclusive)"), UCataclysmStatPipeline::ConditionHolds(Clock, 4.0f, State));
	State.SecondsSinceStruckByYourMovementSkill = 4.5f;
	TestFalse(TEXT("control: 4.5 seconds after, no"), UCataclysmStatPipeline::ConditionHolds(Clock, 4.0f, State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementClocksRealRowsTest,
	"Cataclysm.MovementClocks.TheTwoNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Arrival's and Glancing Cuts' real rows, read from the passive effects table: each stat, value, condition and seconds. */
bool FCataclysmMovementClocksRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		const TCHAR* Stat;
		const TCHAR* Condition;
		float Value;
		float Seconds;
	};
	const FExpected Expected[] = {
		{TEXT("Shadow_basic_c_stem1"), TEXT("attack_damage"), TEXT("seconds_after_movement_skill_ends"), 3.0f, 1.0f},
		{TEXT("Shadow_basic_c_stem1"), TEXT("spell_damage"), TEXT("seconds_after_movement_skill_ends"), 3.0f, 1.0f},
		{TEXT("Shadow_basic_c_b0"), TEXT("attack_damage"), TEXT("target_struck_by_your_movement_skill_within_seconds"),
		 2.0f, 4.0f},
		{TEXT("Shadow_basic_c_b0"), TEXT("spell_damage"), TEXT("target_struck_by_your_movement_skill_within_seconds"),
		 2.0f, 4.0f},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			if (Row->Stat != Want.Stat)
			{
				continue;
			}
			++Found;
			TestEqual(*FString::Printf(TEXT("%s %s: the condition"), Want.Node, Want.Stat), Row->Condition,
					  FString(Want.Condition));
			TestEqual(*FString::Printf(TEXT("%s %s: the value"), Want.Node, Want.Stat), Row->ValuePerPoint, Want.Value,
					  0.001f);
			TestEqual(*FString::Printf(TEXT("%s %s: the seconds"), Want.Node, Want.Stat), Row->ConditionValue,
					  Want.Seconds, 0.001f);
		}
		TestEqual(*FString::Printf(TEXT("%s %s: rows"), Want.Node, Want.Stat), Found, 1);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
