// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

/**
 * Tests for an enemy whose health reaches zero.
 *
 * WHAT THESE GUARD. Issue #517: nothing in the project reacted to health
 * reaching zero. Damage was dealt and health did drop, and then nothing
 * happened -- an enemy at zero health kept chasing, kept swinging, and could not
 * be removed from the level. Combat had no outcome, which is why the project
 * owner reported on 2026-08-12 that "None of the actual combat is implemented I
 * don't think. Or at least I can't tell when playing it."
 *
 * WHAT THESE DELIBERATELY DO NOT COVER.
 *
 * A PLAYER'S DEATH, which is not built. `ACataclysmCharacterBase::HandleDeath`
 * is inert on the base and only the enemy overrides it. A player's death owes a
 * death penalty, a corruption cost and the Last Stand mechanic, none of which is
 * designed, so #517 does the enemy half alone.
 *
 * THE DESTRUCTION ITSELF. `HandleDeath` schedules it for the next tick, and a
 * world built by `UWorld::CreateWorld` is never ticked, so nothing here can
 * watch the actor go. What IS checked is everything that decides it should:
 * the creature is marked dead, it has stopped, and its brain refuses to drive
 * it. Claiming to test the removal in a world with no clock would be claiming
 * more than the evidence supports.
 */

namespace CataclysmDeathTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/** An enemy with health, on a side, able to attack and be attacked. */
	static ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, const FVector& Where,
												ECataclysmTeam Team,
												float Health = 1000.0f)
	{
		ACataclysmEnemyCharacter* Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
			Where, FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			// SetHealth sets the MAXIMUM, and the current value follows it.
			Actor->SetHealth(Health);
			Actor->SetAttackDamage(Health * 10.0f);
		}
		return Actor;
	}

	static float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The vocabulary
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmDeadTagExistsTest,
	"Cataclysm.Death.TheDeadTagExistsInTheVocabulary")
{
	// WITHOUT THIS THE WHOLE THING FAILS SILENTLY. The tag is requested by name
	// with ErrorIfNotFound false, so a vocabulary that has lost it returns an
	// invalid tag, MarkDead refuses, HandleDeath returns early, and a creature
	// at zero health goes back to fighting with nothing reporting it. The tag
	// comes from the Tags sheet of docs/All_Things_Cataclysm.xlsx by way of
	// tools/generate_gameplay_tags.py, so an edit to the workbook can remove it
	// without touching a line of C++.
	TestTrue(TEXT("State.Dead is a known tag"),
		UCataclysmSkillEffects::DeadTag().IsValid());

	TestNotEqual(TEXT("and it is not the stun tag"),
		UCataclysmSkillEffects::DeadTag(),
		UCataclysmSkillEffects::StunnedTag());

	return true;
}

// --------------------------------------------------------------------------
// Reaching zero health
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmZeroHealthMarksAnEnemyDeadTest,
	"Cataclysm.Death.AnEnemyAtZeroHealthIsMarkedDead")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Victim = CataclysmDeathTest::SpawnEnemy(
		World, FVector(500.0f, 0.0f, 0.0f), ECataclysmTeam::Players);

	if (TestNotNull(TEXT("an attacker"), Attacker)
		&& TestNotNull(TEXT("a victim"), Victim))
	{
		TestFalse(TEXT("it starts alive"),
			UCataclysmSkillEffects::IsDead(Victim));

		// THE DEFECT IN ISSUE #517. Health reached zero and nothing happened.
		UCataclysmSkillEffects::ApplyHit(Attacker, Victim, 100.0f);

		TestEqual(TEXT("its health reached zero"),
			CataclysmDeathTest::HealthOf(Victim), 0.0f, 0.01f);
		TestTrue(TEXT("and it is marked dead"),
			UCataclysmSkillEffects::IsDead(Victim));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmSurvivingAHitDoesNotMarkDeadTest,
	"Cataclysm.Death.AnEnemyThatSurvivesAHitIsNotMarkedDead")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Victim = CataclysmDeathTest::SpawnEnemy(
		World, FVector(500.0f, 0.0f, 0.0f), ECataclysmTeam::Players);

	if (TestNotNull(TEXT("an attacker"), Attacker)
		&& TestNotNull(TEXT("a victim"), Victim))
	{
		// A tenth of the victim's health. Without this the test above would pass
		// against a build that marked everything dead the moment it was hit,
		// which is the opposite mistake and just as wrong.
		Attacker->SetAttackDamage(100.0f);
		UCataclysmSkillEffects::ApplyHit(Attacker, Victim, 100.0f);

		TestTrue(TEXT("it still has health"),
			CataclysmDeathTest::HealthOf(Victim) > 0.0f);
		TestFalse(TEXT("and it is not marked dead"),
			UCataclysmSkillEffects::IsDead(Victim));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmADeadEnemyStopsMovingTest,
	"Cataclysm.Death.ADeadEnemyStopsWhereItFell")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Victim = CataclysmDeathTest::SpawnEnemy(
		World, FVector(500.0f, 0.0f, 0.0f), ECataclysmTeam::Players);

	if (TestNotNull(TEXT("an attacker"), Attacker)
		&& TestNotNull(TEXT("a victim"), Victim))
	{
		// MID-CHARGE, WHICH IS THE CASE THAT MATTERS. A charge advances per
		// frame from Tick, so a creature killed during one would otherwise carry
		// its corpse across the room. Same reasoning as being stunned mid-charge,
		// issue #499.
		Victim->BeginCharge(FVector(2000.0f, 0.0f, 0.0f),
							/*SpeedCmPerSecond=*/800.0f,
							/*HalfWidthCm=*/150.0f, /*DamagePercent=*/100.0f);
		TestTrue(TEXT("it is charging"), Victim->IsCharging());

		// GIVEN A VELOCITY BY HAND, and that is not decoration. A world built by
		// UWorld::CreateWorld is never ticked, so nothing here ever moves and the
		// component's velocity is zero the whole time. Asserting it is zero after
		// death would then pass whether or not anything stopped it -- which is
		// what happened: removing StopMovementImmediately broke nothing, and
		// tools/unreal_build.prove_cpp_guard reported this test as unguarded.
		UCharacterMovementComponent* Movement = Victim->GetCharacterMovement();
		if (TestNotNull(TEXT("it has a movement component"), Movement))
		{
			Movement->Velocity = FVector(800.0f, 0.0f, 0.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Attacker, Victim, 100.0f);

		TestTrue(TEXT("it is dead"), UCataclysmSkillEffects::IsDead(Victim));
		TestFalse(TEXT("and the charge stopped with it"), Victim->IsCharging());

		if (Movement)
		{
			TestEqual(TEXT("the speed it had is gone"),
				Movement->Velocity.Size(), 0.0, 0.01);
			TestEqual(TEXT("and it is not allowed to move again"),
				static_cast<int32>(Movement->MovementMode), static_cast<int32>(MOVE_None));
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmADeadEnemyIsNotDrivenTest,
	"Cataclysm.Death.ADeadEnemysBrainRefusesToDriveIt")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	// TWO ATTACKERS RATHER THAN ONE THOUGHT TWICE, and that is what makes this
	// test able to fail. A brain carries state between passes -- when it last
	// attacked, what it was winding up -- so asking one creature before and after
	// it died measured that state as much as the death check, and the whole test
	// passed with the death check deleted. Two brains that have never thought
	// before, at the same distance from the same target, differ in one thing.
	//
	// AND MARKED DEAD DIRECTLY RATHER THAN KILLED, for the same reason.
	// HandleDeath also turns collision off, and a creature with no collision
	// finds nothing to fight, so killing it here would leave it idle for a second
	// reason. Setting only the tag leaves it otherwise able to act, so what is
	// measured is the controller refusing and nothing else. Both of these were
	// found by tools/unreal_build.prove_cpp_guard reporting the branch unguarded.
	// The same distance from the same target, on opposite sides, so the two
	// brains differ in one thing and not in what they can see.
	ACataclysmEnemyCharacter* Victim = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Living = CataclysmDeathTest::SpawnEnemy(
		World, FVector(200.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Slain = CataclysmDeathTest::SpawnEnemy(
		World, FVector(-200.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	if (TestNotNull(TEXT("a victim"), Victim)
		&& TestNotNull(TEXT("a living attacker"), Living)
		&& TestNotNull(TEXT("an attacker to kill"), Slain))
	{
		ACataclysmEnemyController* LivingBrain =
			Cast<ACataclysmEnemyController>(Living->GetController());
		ACataclysmEnemyController* SlainBrain =
			Cast<ACataclysmEnemyController>(Slain->GetController());

		// THE TARGET HAS TO SURVIVE THE FIRST PASS. Think does not only report an
		// intention, it acts on one, and SpawnEnemy arms a creature with ten
		// times its own health in attack damage. So the living attacker killed
		// the shared target outright, the dead one then found nothing to fight,
		// and the test passed with the death check deleted --
		// tools/unreal_build.prove_cpp_guard reported it unguarded twice before
		// this was found. One point of damage against a large pool leaves the
		// target alive and the second brain with something to refuse.
		Living->SetAttackDamage(1.0f);
		Slain->SetAttackDamage(1.0f);
		Victim->SetHealth(100000.0f);

		if (TestNotNull(TEXT("the living one has a brain"), LivingBrain)
			&& TestNotNull(TEXT("the dead one has a brain"), SlainBrain))
		{
			// ALIVE, IT HAS SOMETHING TO DO. Without this the check below would
			// pass against a creature that was idle for some other reason -- no
			// target in sight, no sight radius -- and would say nothing about
			// death at all.
			TestNotEqual(TEXT("alive, it does something about the enemy in front of it"),
				LivingBrain->Think(), ECataclysmBrainAction::Idle);

			TestTrue(TEXT("the other one is marked dead"),
				UCataclysmSkillEffects::MarkDead(Slain));

			TestEqual(TEXT("and its brain does nothing with it"),
				SlainBrain->Think(), ECataclysmBrainAction::Idle);
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmDyingHappensOnceTest,
	"Cataclysm.Death.AnEnemyDiesOnceHoweverManyHitsLand")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Victim = CataclysmDeathTest::SpawnEnemy(
		World, FVector(500.0f, 0.0f, 0.0f), ECataclysmTeam::Players);

	if (TestNotNull(TEXT("an attacker"), Attacker)
		&& TestNotNull(TEXT("a victim"), Victim))
	{
		UCataclysmSkillEffects::ApplyHit(Attacker, Victim, 100.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Victim));

		// HITTING A CORPSE IS ORDINARY, not an edge case: a burn keeps ticking
		// after the blow that killed, and two attacks can land in one frame.
		// Dying schedules the creature's removal, so running it a second time
		// would schedule the removal of something already leaving.
		TestFalse(TEXT("a second killing blow does not kill it again"),
			UCataclysmSkillEffects::MarkDead(Victim));

		UCataclysmSkillEffects::ApplyHit(Attacker, Victim, 100.0f);
		TestTrue(TEXT("and it is still dead, not resurrected"),
			UCataclysmSkillEffects::IsDead(Victim));
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS

// --------------------------------------------------------------------------
// Whose death is it
// --------------------------------------------------------------------------

/**
 * The regression test for issue #565.
 *
 * An attribute set's GetOwningActor answers with the ability system's OWNER, not
 * its avatar. Every creature in this file has the two as the same object, so
 * every test above passes whichever accessor the code reads -- which is exactly
 * how this survived.
 *
 * THE PLAYER IS THE CASE WHERE THEY DIFFER.
 * ACataclysmPlayerCharacter::InitAbilityActorInfo makes the player state the
 * owner, because it survives death, and the pawn the avatar. A player state is
 * not a character, so the cast in NotifyIfHealthReachedZero failed and the
 * function returned early.
 *
 * IT COSTS NOTHING TODAY. HandleDeath is inert on the base by design, and a
 * player's death is not built -- see the note at the top of this file. This test
 * exists so that when somebody does build one, it fires, instead of failing
 * silently in an actor lookup two files away from the code they are writing.
 *
 * The arrangement below is artificial on purpose: an enemy is given an ability
 * system whose owner is some other actor, so that owner and avatar differ the
 * way they do for the player. It is the only way to exercise the distinction
 * while a player's death does nothing.
 */
// Spelled out rather than using this file's CATACLYSM_TEST macro, which is
// undefined at a line above where this test sits.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFollowsTheAvatarNotTheOwnerTest,
	"Cataclysm.Death.DeathFollowsTheAvatarNotTheOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathFollowsTheAvatarNotTheOwnerTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Victim = CataclysmDeathTest::SpawnEnemy(
		World, FVector(500.0f, 0.0f, 0.0f), ECataclysmTeam::Players);

	// Stands in for the player state: holds the ability system, is not a
	// character, and would fail the cast.
	AActor* NotACharacter = World->SpawnActor<AActor>();

	if (TestNotNull(TEXT("an attacker"), Attacker)
		&& TestNotNull(TEXT("a victim"), Victim)
		&& TestNotNull(TEXT("something to own the ability system"), NotACharacter))
	{
		UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Victim);
		if (!TestNotNull(TEXT("the victim has an ability system"), AbilitySystem))
		{
			World->DestroyWorld(false);
			return false;
		}

		// Split them apart, the way the player has them.
		AbilitySystem->InitAbilityActorInfo(NotACharacter, Victim);

		if (!TestEqual(TEXT("the owner is now something that is not a character"),
				AbilitySystem->GetOwnerActor(), (AActor*)NotACharacter))
		{
			World->DestroyWorld(false);
			return false;
		}
		TestEqual(TEXT("and the avatar is still the creature in the world"),
			AbilitySystem->GetAvatarActor(), (AActor*)Victim);

		TestFalse(TEXT("it starts alive"),
			UCataclysmSkillEffects::IsDead(Victim));

		UCataclysmSkillEffects::ApplyHit(Attacker, Victim, 100.0f);

		TestEqual(TEXT("its health reached zero"),
			CataclysmDeathTest::HealthOf(Victim), 0.0f, 0.01f);

		// THE ASSERTION THAT MATTERS. Reading the owner finds an actor that is
		// not a character, the cast fails, and nothing dies.
		TestTrue(TEXT("and it is marked dead, even though the ability system is "
					  "owned by something that is not a character"),
			UCataclysmSkillEffects::IsDead(Victim));
	}

	World->DestroyWorld(false);
	return true;
}
