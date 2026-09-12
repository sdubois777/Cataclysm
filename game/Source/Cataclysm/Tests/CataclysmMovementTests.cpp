// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * What a character knows about its own movement. Issue #41, slice 2.
 *
 * WHAT THESE GUARD. Nothing in the game could say whether a character was
 * moving, how long it had stood still, or how far it had walked, so two rows of
 * `game/Data/DungeonModifiers.csv` did nothing and a list of passive nodes and
 * enchantment rows had nothing to read. The readings live on the ability system
 * component and are written by `UCataclysmMovement::SampleStep`.
 *
 * THEY DRIVE THE SAMPLER DIRECTLY RATHER THAN WAITING FOR THE TIMER. The sampler
 * runs as one job in the quarter-second step that already pays regeneration, and
 * a world built by `UWorld::CreateWorld` is never ticked:
 * `ACataclysmCharacterBase::IsRegenerating`'s own comment records that a test
 * "can ask whether the timer is running but can never watch it fire". That the
 * step calls this job at all is checked instead by
 * `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`,
 * which reads the source text, so deleting the call fails a test even though
 * continuous integration compiles no C++.
 *
 * THEY SPAWN A CREATURE RATHER THAN THE PLAYER, and that is deliberate.
 * `ACataclysmCharacterBase` does not own an ability system component: the
 * player's lives on its player state and a creature's on the pawn itself. A bare
 * spawned player character therefore has no component at all, and these
 * readings are written for every character alike, so a creature exercises the
 * same code without a player state, a controller and a possession.
 *
 * WHY -1 MEANS ONLY "NO CHARACTER TO READ". A character's clocks start when it
 * spawns, so "has never moved" is a real zero rather than an unknown. The
 * character sheet, which asks about nobody, is the one thing that reads -1. That
 * the conditions refuse -1 is covered by
 * `Cataclysm.StatPipeline.AnIncreaseCanDependOnWhetherTheCharacterIsMoving`.
 */

namespace CataclysmMovementTest
{
	/** A move that counts: twice the threshold, in centimetres. */
	constexpr float AboveThresholdCm =
		UCataclysmMovement::MovedMetresThreshold
		* UCataclysmMovement::CentimetresPerMetre * 2.0f;

	/** A move smaller than the threshold: an animation's drift, not movement. */
	constexpr float BelowThresholdCm =
		UCataclysmMovement::MovedMetresThreshold
		* UCataclysmMovement::CentimetresPerMetre * 0.5f;

	/** A creature and its ability system component, which it owns itself. */
	struct FWalker
	{
		explicit FWalker(UWorld* World)
		{
			Character = World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			AbilitySystem = Character
				? Cast<UCataclysmAbilitySystemComponent>(
					Character->GetAbilitySystemComponent())
				: nullptr;
		}

		bool IsUsable() const { return Character && AbilitySystem; }

		/** Put the character here and take one sample. */
		void MoveTo(float X) const
		{
			Character->SetActorLocation(FVector(X, 0.0f, 0.0f));
			UCataclysmMovement::SampleStep(Character);
		}

		ACataclysmEnemyCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementWalkingTest,
	"Cataclysm.Movement.AWalkingCharacterIsMovingAndAStandingOneIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMovementWalkingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmMovementTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWalker Walker(World);
	if (!TestTrue(TEXT("a creature with an ability system spawned"),
				  Walker.IsUsable()))
	{
		return false;
	}

	// THE FIRST SAMPLE HAS NOTHING TO COMPARE WITH, so it only records where the
	// character is. Without that, a character would read as moving from the
	// moment it spawned, because its first sample would measure its whole
	// distance from the origin.
	Walker.MoveTo(0.0f);
	TestTrue(TEXT("the first sample records where the character is"),
			 Walker.AbilitySystem->HasSampledLocation());
	TestFalse(TEXT("and reports no movement"), Walker.AbilitySystem->IsMoving());

	// A REAL WALK.
	Walker.MoveTo(AboveThresholdCm);
	TestTrue(TEXT("a character that walked is moving"),
			 Walker.AbilitySystem->IsMoving());
	TestEqual(TEXT("and its standing-still clock reads zero, not unknown"),
			  Walker.AbilitySystem->SecondsSinceMoved(), 0.0f, 0.001f);
	TestTrue(TEXT("and the walk was counted against the tally"),
			 Walker.AbilitySystem->MetresMovedSinceOwnAttack() > 0.0f);
	TestTrue(TEXT("and against the total"),
			 Walker.AbilitySystem->MetresWalkedTotal() > 0.0f);

	// STANDING STILL. One sample at the same place is enough to stop moving.
	const float Walked = Walker.AbilitySystem->MetresWalkedTotal();
	Walker.MoveTo(AboveThresholdCm);
	TestFalse(TEXT("a character that stayed put is not moving"),
			  Walker.AbilitySystem->IsMoving());
	TestEqual(TEXT("and standing still adds no distance"),
			  Walker.AbilitySystem->MetresWalkedTotal(), Walked, 0.001f);

	// AN ANIMATION'S DRIFT IS NOT MOVEMENT. That is what the threshold is for,
	// and it answers a failure Diablo IV players reported: tiny unintended steps
	// while attacking in place reset a standing-still bonus.
	Walker.MoveTo(AboveThresholdCm + BelowThresholdCm);
	TestFalse(TEXT("a move below the threshold is not movement"),
			  Walker.AbilitySystem->IsMoving());
	TestEqual(TEXT("and adds no distance"),
			  Walker.AbilitySystem->MetresWalkedTotal(), Walked, 0.001f);

	// HEIGHT IS NOT WALKING. The distance is measured in two dimensions, so a
	// character falling or jumping on the spot is standing still as far as these
	// readings are concerned.
	Walker.Character->SetActorLocation(
		FVector(AboveThresholdCm + BelowThresholdCm, 0.0f, 500.0f));
	UCataclysmMovement::SampleStep(Walker.Character);
	TestFalse(TEXT("a character that only changed height is not moving"),
			  Walker.AbilitySystem->IsMoving());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementClocksTest,
	"Cataclysm.Movement.TheStandingStillClockGrowsAndAMoveResetsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMovementClocksTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmMovementTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWalker Walker(World);
	if (!TestTrue(TEXT("a creature with an ability system spawned"),
				  Walker.IsUsable()))
	{
		return false;
	}

	// A CHARACTER THAT HAS NEVER MOVED READS A REAL ZERO, not an unknown. Said
	// first, because the whole -1 convention rests on it.
	Walker.MoveTo(0.0f);
	TestEqual(TEXT("a character that has never moved reads zero seconds"),
			  Walker.AbilitySystem->SecondsSinceMoved(), 0.0f, 0.001f);

	// THE CLOCK GROWS WITH THE WORLD'S, moved by hand because a test world is
	// never ticked.
	CataclysmTestWorld::RunClock(World, 3.0f);
	TestEqual(TEXT("three seconds later it reads three"),
			  Walker.AbilitySystem->SecondsSinceMoved(), 3.0f, 0.1f);

	// AND A MOVE PUTS IT BACK TO NOTHING. This is what clears every Forced March
	// stack at once without a stack being stored anywhere.
	Walker.MoveTo(AboveThresholdCm);
	TestEqual(TEXT("a move puts the clock back to zero"),
			  Walker.AbilitySystem->SecondsSinceMoved(), 0.0f, 0.001f);

	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("and it starts growing again"),
			  Walker.AbilitySystem->SecondsSinceMoved(), 1.0f, 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementAttackResetTest,
	"Cataclysm.Movement.ItsOwnAttackResetsTheTallyButNotTheTotal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMovementAttackResetTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmMovementTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWalker Walker(World);
	if (!TestTrue(TEXT("a creature with an ability system spawned"),
				  Walker.IsUsable()))
	{
		return false;
	}

	Walker.MoveTo(0.0f);
	Walker.MoveTo(500.0f);
	const float Tally = Walker.AbilitySystem->MetresMovedSinceOwnAttack();
	const float Total = Walker.AbilitySystem->MetresWalkedTotal();
	TestTrue(TEXT("walking counted against both"), Tally > 0.0f && Total > 0.0f);
	TestEqual(TEXT("and both agree before any attack"), Tally, Total, 0.001f);

	// AN ATTACK CLEARS THE TALLY. This is what makes the next attack "the first
	// one after moving": the Ravager capstone option Headlong asks about the walk
	// since the character's last attack.
	Walker.AbilitySystem->NoteOwnAttack();
	TestEqual(TEXT("its own attack clears the tally"),
			  Walker.AbilitySystem->MetresMovedSinceOwnAttack(), 0.0f, 0.001f);
	TestEqual(TEXT("and its attack clock reads zero"),
			  Walker.AbilitySystem->SecondsSinceOwnAttack(), 0.0f, 0.001f);

	// AND LEAVES THE TOTAL ALONE, which is the reason the total exists at all.
	// The Nihil's Embrace takes resistance for distance walked and has to survive
	// every attack the character makes until a kill cleanses it.
	TestEqual(TEXT("the total survives the attack"),
			  Walker.AbilitySystem->MetresWalkedTotal(), Total, 0.001f);

	// A SECOND ATTACK WITH NO WALKING BETWEEN READS NOTHING WALKED, which is what
	// stops Headlong applying to every swing of a stationary fight.
	Walker.AbilitySystem->NoteOwnAttack();
	TestEqual(TEXT("a second attack with no walking between reads nothing"),
			  Walker.AbilitySystem->MetresMovedSinceOwnAttack(), 0.0f, 0.001f);

	// WALKING AGAIN COUNTS AGAIN, against the tally and the total both.
	Walker.MoveTo(1000.0f);
	TestTrue(TEXT("walking after an attack counts again"),
			 Walker.AbilitySystem->MetresMovedSinceOwnAttack() > 0.0f);
	TestTrue(TEXT("and the total kept everything"),
			 Walker.AbilitySystem->MetresWalkedTotal() > Total);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementTeleportTest,
	"Cataclysm.Movement.AnInstantRelocationIsNeitherWalkingNorMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMovementTeleportTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmMovementTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWalker Walker(World);
	if (!TestTrue(TEXT("a creature with an ability system spawned"),
				  Walker.IsUsable()))
	{
		return false;
	}

	Walker.MoveTo(0.0f);
	Walker.MoveTo(500.0f);
	const float Total = Walker.AbilitySystem->MetresWalkedTotal();
	CataclysmTestWorld::RunClock(World, 2.0f);

	// A TELEPORT IS ANNOUNCED RATHER THAN INFERRED, because it cannot be told
	// from a very fast walk by sampling position. Each place that relocates a
	// character at once says so, so a new instant move cannot be missed silently.
	//
	// PATH OF EXILE 2 STATES THE RULE OUTRIGHT for its own distance bonus:
	// "Teleportation does not count towards the distance travelled".
	Walker.Character->SetActorLocation(FVector(100000.0f, 0.0f, 0.0f));
	Walker.AbilitySystem->NoteRelocatedInstantly();
	Walker.AbilitySystem->NoteSampledAt(Walker.Character->GetActorLocation());

	TestEqual(TEXT("a teleport adds no distance"),
			  Walker.AbilitySystem->MetresWalkedTotal(), Total, 0.001f);
	TestFalse(TEXT("and does not count as moving"),
			  Walker.AbilitySystem->IsMoving());

	// AND THE NEXT SAMPLE MEASURES FROM WHERE IT LANDED. Without re-basing the
	// sample, the step after a teleport would count the whole jump as a walk.
	Walker.MoveTo(100000.0f);
	TestEqual(TEXT("the sample after a teleport measures from the landing place"),
			  Walker.AbilitySystem->MetresWalkedTotal(), Total, 0.001f);

	// THE STANDING-STILL CLOCK STARTS AFRESH, which is this project's judgement
	// and differs from Path of Exile 1, where a teleport resets a bonus that
	// builds while standing still because "you did move". Here a teleport neither
	// earns distance nor counts as having moved.
	TestEqual(TEXT("a teleport starts the standing-still clock again"),
			  Walker.AbilitySystem->SecondsSinceMoved(), 0.0f, 0.1f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
