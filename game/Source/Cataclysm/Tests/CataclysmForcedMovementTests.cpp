// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPinnedLine.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the five verbs `ForcedMovement` may name.
 *
 * WHAT THESE GUARD. Nine skill rows across the Spear, the Warhammer and the
 * Whip state one or more of Knockdown, Launch, Pull, Drag and Pin, and until
 * 2026-09-01 nothing read the column at all: every one of those rows ran as
 * though the cell were empty. `docs/Cataclysm_GDD_v2.md` section VI decides how
 * two of the five behave and is silent about a third, so the reasoning is worth
 * stating here as well as in the code.
 *
 *   Knockdown  a hard stop. All three anti-stun-lock rules, and it shares one
 *              immunity window with the stun rather than having its own
 *   Pin        movement only. None of the three rules, which is a decision
 *              rather than a derivation -- issue #1149
 *   Pull/Drag  a displacement toward the caster, halved on repeat
 *   Launch     a displacement straight up, halved on repeat
 *
 * WHAT THEY DELIBERATELY DO NOT COVER: that a tag falls off when its duration
 * ends. Expiry belongs to the Gameplay Ability System's duration effects and
 * runs on the world's timer manager, which a world built by `UWorld::CreateWorld`
 * never ticks. `CataclysmStunTests.cpp` says the same about the stun and for the
 * same reason. What is checked here is which holds are allowed to start, how far
 * a displacement moves a target, and what a held creature may still do.
 *
 * A DISPLACEMENT IS MEASURED AS A POSITION AND NOT AS A RETURN VALUE, because a
 * function that reported success and moved nobody is the failure most worth
 * catching. `AddActorWorldOffset` sweeps, so the numbers below leave room around
 * each fighter rather than standing them next to one another.
 */

namespace CataclysmForcedMovementTest
{
	/** Metres, so the tests read the way the design document does. */
	constexpr float M = 100.0f;

	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** A character with health, on a side, able to be an instigator. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where, ECataclysmTeam Team,
					   float Health = 1000.0f)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));

			// SetHealth sets the MAXIMUM, which is what the damage threshold is
			// a percentage of. A thousand makes the threshold a round hundred.
			Actor->SetHealth(Health);
			Actor->SetAttackDamage(50.0f);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			const UAbilitySystemComponent* AbilitySystem =
				UCataclysmTargeting::AbilitySystemOf(Actor);
			return AbilitySystem ? AbilitySystem->GetNumericAttribute(Attribute)
								 : 0.0f;
		}

		/** What share of an incoming hit this character takes, in percent. */
		float DamageTaken() const
		{
			return Get(UCataclysmCombatAttributeSet::GetDamageTakenAttribute());
		}

		FVector Where() const { return Actor->GetActorLocation(); }

		ACataclysmEnemyCharacter* Actor = nullptr;
	};
}

// EVERY TEST BELOW OPENS THIS NAMESPACE INSIDE ITS OWN BODY RATHER THAN AT FILE
// SCOPE, AND THAT IS NOT A STYLE PREFERENCE. This module is built as a unity
// blob: several .cpp files are concatenated into one translation unit, so a
// `using namespace` written out here reaches all of them. Doing that broke two
// things in CataclysmGatekeeperTests.cpp -- its own `MakeWorldThatHasBegunPlay`
// became an ambiguous call against the one above, and the `M` above hid a
// declaration of the same name inside an engine header.
//
// IT DID NOT SHOW UNTIL THE WORK WAS COMMITTED. Unreal keeps modified and
// untracked files out of the blob and compiles them separately, so a build over
// a dirty tree cannot see this class of collision at all.

// --------------------------------------------------------------------------
// The vocabulary
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForcedMovementTagsExistTest,
	"Cataclysm.ForcedMovement.TheTwoHoldTagsExistInTheVocabulary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForcedMovementTagsExistTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// WITHOUT THIS THE WHOLE FEATURE FAILS SILENTLY. Both tags are requested by
	// name with ErrorIfNotFound false, so a vocabulary that has lost them returns
	// an invalid tag, `ApplyTagForDuration` refuses it, `ApplyPin` returns false,
	// and nothing anywhere reports that pinning stopped working. They come from
	// the Tags sheet of docs/All_Things_Cataclysm.xlsx by way of
	// tools/generate_gameplay_tags.py, so an edit to the workbook can remove them
	// without touching a line of C++.
	TestTrue(TEXT("State.Pinned is a known tag"),
		UCataclysmSkillEffects::PinnedTag().IsValid());
	TestTrue(TEXT("State.KnockedDown is a known tag"),
		UCataclysmSkillEffects::KnockedDownTag().IsValid());

	// FOUR DISTINCT TAGS AND NOT TWO NAMES FOR THE SAME ONE, which a copy-paste
	// in any accessor would make them. A pin sharing the stun's tag would take
	// the whole anti-stun-lock rule with it and stop a pinned creature acting.
	TestNotEqual(TEXT("a pin is not a knockdown"),
		UCataclysmSkillEffects::PinnedTag(),
		UCataclysmSkillEffects::KnockedDownTag());
	TestNotEqual(TEXT("a pin is not a stun"),
		UCataclysmSkillEffects::PinnedTag(),
		UCataclysmSkillEffects::StunnedTag());
	TestNotEqual(TEXT("a knockdown is not a stun"),
		UCataclysmSkillEffects::KnockedDownTag(),
		UCataclysmSkillEffects::StunnedTag());

	return true;
}

// --------------------------------------------------------------------------
// Pin -- a hold that is not a hard stop
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPinHoldsWithoutStoppingTest,
	"Cataclysm.ForcedMovement.APinHoldsATargetWithoutStoppingItActing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPinHoldsWithoutStoppingTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(4 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	TestFalse(TEXT("nothing is pinned to begin with"),
		UCataclysmSkillEffects::IsPinned(Target.Actor));

	// SEVEN SECONDS, WHICH NO ROW AND NO SLOT STATES. The four pinning rows use
	// 3, 4, 4 and 6, so a duration read out of the wrong cell could not produce
	// this one by accident.
	TestTrue(TEXT("a pin lands"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Target.Actor, /*DurationSeconds=*/7.0f));

	TestTrue(TEXT("and the target is pinned"),
		UCataclysmSkillEffects::IsPinned(Target.Actor));

	// THE WHOLE POINT OF THE VERB, AND WHAT SEPARATES IT FROM A STUN. Everything
	// that refuses to drive a character asks `CannotAct`, so a pin answering yes
	// here would silently take a pinned creature's attacks away as well as its
	// feet -- which is a stun with a different name, and is exactly the mistake
	// issue #1149 is about.
	TestFalse(TEXT("but it is not stunned"),
		UCataclysmSkillEffects::IsStunned(Target.Actor));
	TestFalse(TEXT("nor knocked down"),
		UCataclysmSkillEffects::IsKnockedDown(Target.Actor));
	TestFalse(TEXT("and it may still act"),
		UCataclysmSkillEffects::CannotAct(Target.Actor));

	// A pin of no length is not a pin, the same way a stun of no length is not
	// one. Without this a row that left ForcedMovementDuration empty would grant
	// a tag with no duration, which the engine treats as lasting for ever.
	FScopedFighter Untouched(World, FVector(8 * M, 0.0f, 0.0f),
							 ECataclysmTeam::Monsters);
	TestFalse(TEXT("a pin of zero seconds is refused"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Untouched.Actor, /*DurationSeconds=*/0.0f));
	TestFalse(TEXT("and leaves nothing behind"),
		UCataclysmSkillEffects::IsPinned(Untouched.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPinnedTargetTakesMoreDamageTest,
	"Cataclysm.ForcedMovement.APinCarriesTheDamageIncreaseItsRowStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPinnedTargetTakesMoreDamageTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// THE SECOND SENTENCE OF THE SPEAR'S IMPALE: "while a target is pinned it
	// takes 30% more damage from every source". Its row states EffectMagnitude=30
	// and, before this, `EffectMagnitude` was read only by a named status effect
	// -- and Impale names none, so the 30 reached nothing at all.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(4 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	// A HUNDRED IS UNCHANGED, which is the convention this attribute and area of
	// effect and the three damage over time stats all share.
	TestEqual(TEXT("a target starts taking an ordinary share of a hit"),
		Target.DamageTaken(), 100.0f);

	// THIRTY-SEVEN, NOT THIRTY. Impale's real figure is 30 and the attribute's
	// own baseline is 100, so a bug that read the wrong number could land on
	// either. Thirty-seven can only have come from the argument.
	TestTrue(TEXT("a pin with a magnitude lands"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Target.Actor, /*DurationSeconds=*/7.0f,
			/*DamageTakenIncrease=*/37.0f));

	TestEqual(TEXT("and the pinned target now takes that much more"),
		Target.DamageTaken(), 137.0f);

	// THE INCREASE RIDES ON THE PIN'S OWN EFFECT, so removing the pin removes
	// it. If the two were separate, an early release -- which the Spear's Skewer
	// does on every death in its line -- would leave a creature permanently
	// softer with nothing left saying why.
	TestTrue(TEXT("releasing the pin reports it removed something"),
		UCataclysmSkillEffects::ReleasePin(Target.Actor));
	TestFalse(TEXT("the target is no longer pinned"),
		UCataclysmSkillEffects::IsPinned(Target.Actor));
	TestEqual(TEXT("and it takes an ordinary share again"),
		Target.DamageTaken(), 100.0f);

	// A PIN THAT STATES NO MAGNITUDE MOVES NOTHING, which is three of the four
	// pinning rows: Nail Down, Skewer and Thicket hold a target and say nothing
	// about what it then takes.
	FScopedFighter Plain(World, FVector(8 * M, 0.0f, 0.0f),
						 ECataclysmTeam::Monsters);
	TestTrue(TEXT("a pin with no magnitude still lands"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Plain.Actor, /*DurationSeconds=*/7.0f));
	TestTrue(TEXT("and holds the target"),
		UCataclysmSkillEffects::IsPinned(Plain.Actor));
	TestEqual(TEXT("but leaves what it takes alone"),
		Plain.DamageTaken(), 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPinTakesNoAntiStunLockRuleTest,
	"Cataclysm.ForcedMovement.APinIsNotCoveredByTheAntiStunLockRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPinTakesNoAntiStunLockRuleTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// THIS TEST EXISTS TO MAKE A DECISION VISIBLE RATHER THAN TO PROVE A FACT.
	// Section VI of the design document lists seven effects and says which are
	// covered by the three anti-stun-lock rules; pinning is not one of the seven,
	// because it arrived with the Spear kit after the table was written. The
	// reading taken is that a pin is not covered, on the Disarm precedent -- a
	// pinned target still turns, attacks and casts. Issue #1149 puts it to the
	// project owner, and if the answer comes back the other way this test is what
	// has to change, deliberately.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);

	// RULE THREE: A BOSS. Thicket pins everything within twelve metres for six
	// seconds, and a boss caught by it is held where it stands and still fights.
	FScopedFighter Boss(World, FVector(4 * M, 0.0f, 0.0f),
						ECataclysmTeam::Monsters);
	Boss.Actor->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	TestTrue(TEXT("step 4 is a boss"), Boss.Actor->IsBoss());

	TestTrue(TEXT("a boss can be pinned, which a boss cannot be stunned"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Boss.Actor, /*DurationSeconds=*/7.0f));
	TestTrue(TEXT("and it is pinned afterwards"),
		UCataclysmSkillEffects::IsPinned(Boss.Actor));

	// AND IT IS STILL A BOSS TO A STUN, which is what says the pin did not
	// quietly route through the stun path and take its rules with it.
	TestFalse(TEXT("the same boss still refuses the strongest stun"),
		UCataclysmSkillEffects::ApplyStun(
			Spearman.Actor, Boss.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));

	// RULE TWO: THE FIVE SECOND WINDOW. A creature just stunned carries
	// State.StunImmune, which is what stops a second stun and a knockdown. A pin
	// does not read it.
	FScopedFighter Held(World, FVector(8 * M, 0.0f, 0.0f),
						ECataclysmTeam::Monsters);
	TestTrue(TEXT("a designed stun lands on an ordinary creature"),
		UCataclysmSkillEffects::ApplyStun(
			Spearman.Actor, Held.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));
	TestTrue(TEXT("and it may still be pinned inside the immunity window"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Held.Actor, /*DurationSeconds=*/7.0f));

	// AND A PIN DOES NOT OPEN A WINDOW EITHER, so pinning first does not protect
	// a creature from being pinned again. Two pins in a row is what four Spear
	// skills sharing one weapon would produce, and nothing in the design bounds
	// it.
	FScopedFighter Twice(World, FVector(12 * M, 0.0f, 0.0f),
						 ECataclysmTeam::Monsters);
	TestTrue(TEXT("a first pin lands"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Twice.Actor, /*DurationSeconds=*/7.0f));
	TestTrue(TEXT("and a second one lands straight after it"),
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Twice.Actor, /*DurationSeconds=*/7.0f));

	// RULE ONE: THE DAMAGE THRESHOLD. `ApplyPin` takes no damage figure at all,
	// which is the strongest form this can be checked in: there is no argument a
	// caller could pass that would make a light hit fail to pin.

	return true;
}

// --------------------------------------------------------------------------
// Knockdown -- a hard stop, sharing the stun's window
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKnockdownIsAHardStopTest,
	"Cataclysm.ForcedMovement.AKnockdownStopsATargetActingAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKnockdownIsAHardStopTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(4 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	TestFalse(TEXT("nothing is knocked down to begin with"),
		UCataclysmSkillEffects::IsKnockedDown(Target.Actor));

	TestTrue(TEXT("a designed knockdown lands"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Target.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/0.0f, /*bKnockdownIsDesigned=*/true));

	TestTrue(TEXT("and the target is on the floor"),
		UCataclysmSkillEffects::IsKnockedDown(Target.Actor));

	// THE POINT OF THE VERB. Section VI: "the target cannot act at all; it is
	// simply on the floor while it happens." Everything that refuses to drive a
	// character asks `CannotAct`, so this failing would leave a knocked-down
	// creature fighting from the floor.
	TestTrue(TEXT("and it cannot act"),
		UCataclysmSkillEffects::CannotAct(Target.Actor));

	// IT IS NOT A STUN, which matters because the two are separate tags that a
	// careless implementation would collapse into one. A knocked-down creature
	// answers no to `IsStunned` and yes to `CannotAct`.
	TestFalse(TEXT("but it is not stunned"),
		UCataclysmSkillEffects::IsStunned(Target.Actor));
	TestFalse(TEXT("and it is not pinned"),
		UCataclysmSkillEffects::IsPinned(Target.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKnockdownTakesTheStunRulesTest,
	"Cataclysm.ForcedMovement.AKnockdownTakesAllThreeAntiStunLockRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKnockdownTakesTheStunRulesTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);

	// RULE THREE: A BOSS CANNOT BE KNOCKED DOWN AT ALL. Section VI is explicit
	// that the exemption for a DESIGNED effect covers the damage threshold only:
	// it "does not ignore boss immunity or the immunity window".
	FScopedFighter Boss(World, FVector(4 * M, 0.0f, 0.0f),
						ECataclysmTeam::Monsters);
	Boss.Actor->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	TestTrue(TEXT("step 4 is a boss"), Boss.Actor->IsBoss());

	TestFalse(TEXT("a boss refuses the strongest knockdown the game can express"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Boss.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/1000.0f, /*bKnockdownIsDesigned=*/true));
	TestFalse(TEXT("and it is not on the floor afterwards"),
		UCataclysmSkillEffects::IsKnockedDown(Boss.Actor));

	// RULE ONE: THE DAMAGE THRESHOLD, WHICH ONLY AN UNDESIGNED KNOCKDOWN TAKES.
	// The fighters carry a thousand maximum health, so the tenth is a hundred.
	FScopedFighter Grazed(World, FVector(8 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);
	TestFalse(TEXT("an undesigned knockdown from a scratch is refused"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Grazed.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/99.0f, /*bKnockdownIsDesigned=*/false));
	TestTrue(TEXT("and one that takes a tenth of maximum health lands"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Grazed.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/100.0f, /*bKnockdownIsDesigned=*/false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKnockdownSharesTheStunWindowTest,
	"Cataclysm.ForcedMovement.AKnockdownAndAStunShareOneImmunityWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKnockdownSharesTheStunWindowTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// THE DESIGN'S OWN SENTENCE, CHECKED IN BOTH DIRECTIONS: "The two share one
	// window rather than one each, because two 3-second holds taken in turn is
	// exactly the failure the window exists to stop." Two separate windows would
	// pass a test that only tried one order.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);

	// STUN FIRST, THEN KNOCKDOWN.
	FScopedFighter Stunned(World, FVector(4 * M, 0.0f, 0.0f),
						   ECataclysmTeam::Monsters);
	TestTrue(TEXT("a designed stun lands"),
		UCataclysmSkillEffects::ApplyStun(
			Hammer.Actor, Stunned.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));
	TestFalse(TEXT("and a knockdown inside its window is refused"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Stunned.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/1000.0f, /*bKnockdownIsDesigned=*/true));
	TestFalse(TEXT("so the target is not on the floor"),
		UCataclysmSkillEffects::IsKnockedDown(Stunned.Actor));

	// KNOCKDOWN FIRST, THEN STUN. This is the half that a second window of its
	// own would let through.
	FScopedFighter Floored(World, FVector(8 * M, 0.0f, 0.0f),
						   ECataclysmTeam::Monsters);
	TestTrue(TEXT("a designed knockdown lands"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Floored.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/0.0f, /*bKnockdownIsDesigned=*/true));
	TestFalse(TEXT("and a stun inside its window is refused"),
		UCataclysmSkillEffects::ApplyStun(
			Hammer.Actor, Floored.Actor, /*DurationSeconds=*/1.5f,
			/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true));
	TestFalse(TEXT("so the target is not stunned"),
		UCataclysmSkillEffects::IsStunned(Floored.Actor));

	// AND A SECOND KNOCKDOWN IS REFUSED TOO, which is the case the shared window
	// was actually written for.
	TestFalse(TEXT("nor is a second knockdown allowed"),
		UCataclysmSkillEffects::ApplyKnockdown(
			Hammer.Actor, Floored.Actor, /*DurationSeconds=*/7.0f,
			/*DamageDealt=*/1000.0f, /*bKnockdownIsDesigned=*/true));

	return true;
}

// --------------------------------------------------------------------------
// Pull, drag and launch -- displacements
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPullHaulsTowardTheCasterTest,
	"Cataclysm.ForcedMovement.APullHaulsATargetTowardWhateverAppliedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPullHaulsTowardTheCasterTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Whip(World, FVector::ZeroVector, ECataclysmTeam::Players);

	// A STATED DISTANCE MOVES THAT FAR AND NO FURTHER. Ten metres out, hauled
	// three: six hundred centimetres should be left. No row states a distance
	// today, so this is the branch a future row would take.
	FScopedFighter Far(World, FVector(10 * M, 0.0f, 0.0f),
					   ECataclysmTeam::Monsters);
	const float StartedAt = Far.Where().X;
	TestTrue(TEXT("a pull of three metres lands"),
		UCataclysmSkillEffects::ApplyPull(
			Whip.Actor, Far.Actor, /*DistanceCm=*/3 * M));

	// READ INTO A FLOAT BEFORE COMPARING. An FVector's components are doubles,
	// and TestEqual has an overload for each width, so handing it a double and a
	// float tolerance is ambiguous rather than merely imprecise. Every other test
	// in this project that measures a position does the same.
	const float EndedAt = Far.Where().X;
	TestTrue(TEXT("and the target ends up closer than it started"),
		EndedAt < StartedAt);
	TestEqual(TEXT("by exactly the distance stated"),
		EndedAt, StartedAt - 3 * M, /*Tolerance=*/1.0f);

	// THE DIRECTION IS THE OPPOSITE OF A KNOCKBACK'S, which is the single most
	// likely thing to be wrong: the two share one displacement body and differ
	// only in the sign of the vector handed to it. A pull that pushed would still
	// return true and still move the target.
	FScopedFighter Shoved(World, FVector(0.0f, 10 * M, 0.0f),
						  ECataclysmTeam::Monsters);
	const float ShovedFrom = Shoved.Where().Y;
	TestTrue(TEXT("a knockback of three metres lands"),
		UCataclysmSkillEffects::ApplyKnockback(
			Whip.Actor, Shoved.Actor, /*DistanceCm=*/3 * M));
	TestTrue(TEXT("and it pushes the target further away"),
		Shoved.Where().Y > ShovedFrom);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPullWithNoDistanceGoesAllTheWayTest,
	"Cataclysm.ForcedMovement.APullWithNoDistanceBringsTheTargetToTheCaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPullWithNoDistanceGoesAllTheWayTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// WHAT BOTH ROWS THAT HAUL ACTUALLY ASK FOR. The Whip's The Gathering brings
	// its catch "into a burning heap at your feet" and its Reel dumps them "at
	// your feet"; neither states a distance, because a distance in a cell could
	// only repeat the skill's own range. Zero therefore has to mean the whole
	// way, and a reading that made zero mean "do nothing" would leave both rows
	// hauling nobody -- silently, because the tag half of the skill still works.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Whip(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Caught(World, FVector(0.0f, 14 * M, 0.0f),
						  ECataclysmTeam::Monsters);

	const float StartedAt = Caught.Where().Y;
	TestTrue(TEXT("a pull stating no distance lands"),
		UCataclysmSkillEffects::ApplyPull(
			Whip.Actor, Caught.Actor, /*DistanceCm=*/0.0f));

	// SWEPT, SO IT STOPS AT THE CASTER'S BODY RATHER THAN INSIDE IT. What is
	// being checked is that the target crossed most of the gap, not that it
	// reached an exact point: the two capsules cannot occupy one another. Twelve
	// of the fourteen metres is well past anything a partial move would produce
	// and well short of what standing still would.
	TestTrue(TEXT("and the target is hauled at least twelve of the fourteen metres"),
		StartedAt - Caught.Where().Y >= 12 * M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLaunchThrowsUpwardTest,
	"Cataclysm.ForcedMovement.ALaunchThrowsATargetStraightUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLaunchThrowsUpwardTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Hammer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Target(World, FVector(4 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	const FVector StartedAt = Target.Where();
	TestTrue(TEXT("a launch of three metres lands"),
		UCataclysmSkillEffects::ApplyLaunch(
			Hammer.Actor, Target.Actor, /*DistanceCm=*/3 * M));

	// READ INTO FLOATS BEFORE COMPARING, for the reason the pull test above
	// records: an FVector's components are doubles and TestEqual has an overload
	// for each width.
	const float RoseTo = Target.Where().Z;
	const float RoseFrom = StartedAt.Z;
	TestEqual(TEXT("and the target rises by exactly that much"),
		RoseTo, RoseFrom + 3 * M, /*Tolerance=*/1.0f);

	// STRAIGHT UP AND NOT AWAY. The instigator stands four metres off along X, so
	// a launch that reused the knockback's direction would move the target
	// sideways as well. Nothing in "thrown into the air" says sideways.
	const float AlongTheGround = FVector::Dist2D(Target.Where(), StartedAt);
	TestEqual(TEXT("and does not move along the ground at all"),
		AlongTheGround, 0.0f, /*Tolerance=*/1.0f);

	// A LAUNCH WITH NO DISTANCE THROWS NOBODY, deliberately, because there is no
	// height a launch obviously means. Upthrust's row states three metres; before
	// 2026-09-01 it stated none, and guessing one in C++ would have hidden that.
	FScopedFighter Untouched(World, FVector(8 * M, 0.0f, 0.0f),
							 ECataclysmTeam::Monsters);
	const float Stayed = Untouched.Where().Z;
	TestFalse(TEXT("a launch of no distance is refused"),
		UCataclysmSkillEffects::ApplyLaunch(
			Hammer.Actor, Untouched.Actor, /*DistanceCm=*/0.0f));
	const float StillThere = Untouched.Where().Z;
	TestEqual(TEXT("and the target has not moved"),
		StillThere, Stayed, /*Tolerance=*/0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryDisplacementIsHalvedOnRepeatTest,
	"Cataclysm.ForcedMovement.APullIsHalvedOnRepeatTheWayAKnockbackIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryDisplacementIsHalvedOnRepeatTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// SECTION VI LIMITS DISPLACEMENT BY HALVING AND NOT BY IMMUNITY: "each
	// displacement applied to a target that has already been displaced within the
	// last 5 seconds moves it half as far as the one before". That rule was
	// written for a knockback and it says "displacement", so a pull and a launch
	// take it too. This is the test that says the three verbs really do share one
	// body rather than each having a copy that forgot the rule.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Whip(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Caught(World, FVector(20 * M, 0.0f, 0.0f),
						  ECataclysmTeam::Monsters);

	// FOUR METRES, WHICH HALVES TO TWO AND THEN TO ONE. Whole metres at every
	// step, so a wrong share shows as a wrong number rather than as rounding.
	const float First = Caught.Where().X;
	UCataclysmSkillEffects::ApplyPull(Whip.Actor, Caught.Actor, /*DistanceCm=*/4 * M);
	const float Second = Caught.Where().X;
	TestEqual(TEXT("the first pull moves the whole four metres"),
		First - Second, 4 * M, /*Tolerance=*/1.0f);

	UCataclysmSkillEffects::ApplyPull(Whip.Actor, Caught.Actor, /*DistanceCm=*/4 * M);
	const float Third = Caught.Where().X;
	TestEqual(TEXT("the second moves half as far"),
		Second - Third, 2 * M, /*Tolerance=*/1.0f);

	UCataclysmSkillEffects::ApplyPull(Whip.Actor, Caught.Actor, /*DistanceCm=*/4 * M);
	const float Fourth = Caught.Where().X;
	TestEqual(TEXT("and the third moves half again"),
		Third - Fourth, 1 * M, /*Tolerance=*/1.0f);

	return true;
}

// --------------------------------------------------------------------------
// The pinned line -- killing one frees the rest
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillingOneOfALineFreesTheRestTest,
	"Cataclysm.ForcedMovement.KillingOneOfAPinnedLineFreesTheOthers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKillingOneOfALineFreesTheRestTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// THE LAST SENTENCE OF THE SPEAR'S SKEWER: "Killing any one of them frees the
	// rest." Its row states `OnDeath=Release`, which was one of the three values
	// `OnDeath` may take that nothing handled.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter First(World, FVector(4 * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FScopedFighter Second(World, FVector(8 * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FScopedFighter Third(World, FVector(12 * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	// A BYSTANDER OUTSIDE THE LINE, so the test can tell "freed the others" from
	// "removed every pin in the level". Without it, a `ReleasePin` that swept
	// everything would pass.
	FScopedFighter Bystander(World, FVector(16 * M, 0.0f, 0.0f),
							 ECataclysmTeam::Monsters);

	for (FScopedFighter* Each : {&First, &Second, &Third, &Bystander})
	{
		UCataclysmSkillEffects::ApplyPin(
			Spearman.Actor, Each->Actor, /*DurationSeconds=*/7.0f);
	}

	const TArray<AActor*> Line = {First.Actor, Second.Actor, Third.Actor};
	TestEqual(TEXT("three creatures are bound into one line"),
		UCataclysmPinnedLine::BindTogether(Line), 3);

	TestTrue(TEXT("all three are pinned"),
		UCataclysmSkillEffects::IsPinned(First.Actor)
			&& UCataclysmSkillEffects::IsPinned(Second.Actor)
			&& UCataclysmSkillEffects::IsPinned(Third.Actor));

	// MARKED DEAD DIRECTLY, because the full death path needs a possessed player
	// pawn, a loot table and an enemy score. `MarkDead` is the one place a death
	// is recorded for the player and for every creature, and it is where the
	// release is hooked, so this is the same call the game makes.
	TestTrue(TEXT("the middle one dies"),
		UCataclysmSkillEffects::MarkDead(Second.Actor));

	TestFalse(TEXT("the first is freed"),
		UCataclysmSkillEffects::IsPinned(First.Actor));
	TestFalse(TEXT("and so is the third"),
		UCataclysmSkillEffects::IsPinned(Third.Actor));

	// THE ONE THAT SAYS IT FREED A LINE AND NOT THE LEVEL.
	TestTrue(TEXT("but a creature outside the line is still pinned"),
		UCataclysmSkillEffects::IsPinned(Bystander.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmALineOfOneBindsNothingTest,
	"Cataclysm.ForcedMovement.ALineOfOneCreatureIsNotBoundAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmALineOfOneBindsNothingTest::RunTest(const FString&)
{
	using namespace CataclysmForcedMovementTest;

	// SKEWER PIERCES UP TO 99 TARGETS AND OFTEN CATCHES EXACTLY ONE, so this is
	// the ordinary case rather than an edge. "Killing any one of them frees the
	// rest" says nothing when there is no rest, and binding a lone creature would
	// leave a component behind that could never do anything.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Spearman(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FScopedFighter Alone(World, FVector(4 * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	const TArray<AActor*> One = {Alone.Actor};
	TestEqual(TEXT("a line of one binds nobody"),
		UCataclysmPinnedLine::BindTogether(One), 0);

	const TArray<AActor*> None;
	TestEqual(TEXT("and an empty line binds nobody"),
		UCataclysmPinnedLine::BindTogether(None), 0);

	// AND THE SAME CREATURE LISTED TWICE IS STILL ONE CREATURE. Without the
	// AddUnique in BindTogether this would count as a line of two and bind a
	// creature to itself, so its own death would try to free it.
	const TArray<AActor*> Twice = {Alone.Actor, Alone.Actor};
	TestEqual(TEXT("and the same creature listed twice is still one creature"),
		UCataclysmPinnedLine::BindTogether(Twice), 0);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
