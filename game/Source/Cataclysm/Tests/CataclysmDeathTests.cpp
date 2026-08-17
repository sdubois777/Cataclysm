// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Player/CataclysmPlayerState.h"

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
 * A PLAYER'S DEATH IS NOW COVERED TOO, at the end of this file, and it is a
 * different shape: the player is marked and stopped like a creature is, and then
 * stands back up rather than being removed. Issue #570. What it still does not
 * charge is the designed penalty -- days off the empire clock, a per-piece
 * equipment drop and a respawn at the capital -- because the running game has
 * none of the four things that would carry it.
 *
 * WHAT THESE DELIBERATELY DO NOT COVER.
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
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
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

	/**
	 * A player pawn with its ability system wired up the way the game wires it.
	 *
	 * THE OWNER AND THE AVATAR MUST DIFFER, which is the whole reason this is
	 * not one SpawnActor call. The player's ability system lives on the player
	 * state, because that survives death, and the pawn is only the avatar. A
	 * pawn spawned on its own has no player state and therefore no ability
	 * system at all, so nothing could damage it.
	 *
	 * OnRep_PlayerState IS THE CLIENT PATH, driven directly because a test world
	 * has no controller to possess with and no network to replicate over. It
	 * calls the same InitAbilityActorInfo the server reaches from PossessedBy.
	 * The same shape is used by CataclysmPlayerMovementTests.cpp.
	 */
	static ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World,
												  const FVector& Where = FVector::ZeroVector)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
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

// --------------------------------------------------------------------------
// Nothing attacks a corpse. Issue #570
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmNothingTargetsTheDeadTest,
	"Cataclysm.Death.NothingFindsADeadCharacterAsATarget")
{
	// THE MEASURED DEFECT THIS IS FOR. Issue #570 recorded fifty-six attacks
	// over seventy seconds landing on a player already at zero health, each
	// dealing exactly nothing, because a creature had no way to ask whether its
	// target was finished. UCataclysmTargeting is where it can ask now.
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Hunter = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Prey = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Players);

	if (TestNotNull(TEXT("a hunter"), Hunter) && TestNotNull(TEXT("prey"), Prey))
	{
		TestTrue(TEXT("alive, it is hostile to the hunter"),
			UCataclysmTargeting::IsHostileTo(Prey, Hunter));
		TestEqual(TEXT("and a search finds it"),
			UCataclysmTargeting::FindEnemiesInSphere(
				World, Hunter, Hunter->GetActorLocation(), 1000.0f).Num(), 1);

		TestTrue(TEXT("now it is dead"), UCataclysmSkillEffects::MarkDead(Prey));

		TestFalse(TEXT("dead, it is no longer hostile"),
			UCataclysmTargeting::IsHostileTo(Prey, Hunter));
		TestEqual(TEXT("and the same search finds nothing"),
			UCataclysmTargeting::FindEnemiesInSphere(
				World, Hunter, Hunter->GetActorLocation(), 1000.0f).Num(), 0);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmNothingHealsTheDeadTest,
	"Cataclysm.Death.ADeadCharacterIsNotAnAllyEither")
{
	// AN AURA MUST NOT BUFF A CORPSE. The same filter answers both searches, so
	// this is what proves it was put where both questions are asked rather than
	// only on the hostile path.
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Caster = CataclysmDeathTest::SpawnEnemy(
		World, FVector::ZeroVector, ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Friend = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	if (TestNotNull(TEXT("a caster"), Caster) && TestNotNull(TEXT("a friend"), Friend))
	{
		TestTrue(TEXT("alive, it is an ally"),
			UCataclysmTargeting::IsFriendlyTo(Friend, Caster));

		TestTrue(TEXT("now it is dead"), UCataclysmSkillEffects::MarkDead(Friend));

		TestFalse(TEXT("dead, it is not an ally"),
			UCataclysmTargeting::IsFriendlyTo(Friend, Caster));
		TestEqual(TEXT("and an ally search finds nothing"),
			UCataclysmTargeting::FindAlliesInSphere(
				World, Caster, Caster->GetActorLocation(), 1000.0f).Num(), 0);
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// A player's death. Issue #570
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmPlayerDiesTest,
	"Cataclysm.Death.APlayerAtZeroHealthIsMarkedDeadAndStops")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer))
	{
		TestFalse(TEXT("it starts alive"), UCataclysmSkillEffects::IsDead(Player));
		TestFalse(TEXT("and not awaiting a respawn"), Player->IsAwaitingRespawn());

		// Far more than the placeholder 100 the attribute set starts at, so the
		// hit cannot leave a sliver behind and make this pass for the wrong
		// reason.
		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);

		TestEqual(TEXT("its health reached zero"),
			CataclysmDeathTest::HealthOf(Player), 0.0f, 0.01f);
		TestTrue(TEXT("it is marked dead"),
			UCataclysmSkillEffects::IsDead(Player));
		TestTrue(TEXT("and it says so"), Player->IsAwaitingRespawn());

		if (const UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
		{
			TestEqual(TEXT("and it is not allowed to move"),
				static_cast<int32>(Movement->MovementMode),
				static_cast<int32>(MOVE_None));
		}

		// NOT REMOVED FROM THE LEVEL, unlike a creature. The design says ordinary
		// death continues the run and that nothing in play destroys a character.
		TestTrue(TEXT("and it is still in the world, unlike a dead creature"),
			IsValid(Player));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerDiesOnceTest,
	"Cataclysm.Death.APlayerDiesOnceHoweverManyHitsLand")
{
	// THE FIFTY-SIX HITS, IN A TEST. A burn ticking and two blows in one frame
	// both write health at zero again, and a second death would restart the
	// respawn timer and hold the player down for ever.
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer))
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		for (int32 Hit = 0; Hit < 10; ++Hit)
		{
			UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		}

		TestFalse(TEXT("a second killing blow does not kill it again"),
			UCataclysmSkillEffects::MarkDead(Player));
		TestTrue(TEXT("and it is still dead, not resurrected"),
			UCataclysmSkillEffects::IsDead(Player));
		TestEqual(TEXT("and still at zero health"),
			CataclysmDeathTest::HealthOf(Player), 0.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerRevivesTest,
	"Cataclysm.Death.APlayerStandsBackUpRatherThanBeingRemoved")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer))
	{
		const float FullHealth = CataclysmDeathTest::HealthOf(Player);
		TestTrue(TEXT("it starts with some health"), FullHealth > 0.0f);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		// DRIVEN DIRECTLY RATHER THAN WAITED FOR. A world built by
		// UWorld::CreateWorld is never ticked, so its timers never fire. Revive
		// is public for exactly this.
		Player->Revive();

		TestFalse(TEXT("it is no longer dead"),
			UCataclysmSkillEffects::IsDead(Player));
		TestFalse(TEXT("and no longer awaiting a respawn"),
			Player->IsAwaitingRespawn());
		TestEqual(TEXT("its health is full again"),
			CataclysmDeathTest::HealthOf(Player), FullHealth, 0.01f);

		if (const UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
		{
			TestEqual(TEXT("and it can walk again"),
				static_cast<int32>(Movement->MovementMode),
				static_cast<int32>(MOVE_Walking));
		}

		// AND IT CAN BE FOUGHT AGAIN, which is the whole point of coming back.
		TestTrue(TEXT("a creature can find it once more"),
			UCataclysmTargeting::IsHostileTo(Player, Killer));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerStandsUpAtThePlayerStartTest,
	"Cataclysm.Death.APlayerStandsUpAtThePlayerStart")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FVector StartHere(1234.0f, -567.0f, 89.0f);
	APlayerStart* Start = World->SpawnActor<APlayerStart>(
		StartHere, FRotator::ZeroRotator);

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	if (TestNotNull(TEXT("a player start"), Start)
		&& TestNotNull(TEXT("a player"), Player)
		&& TestNotNull(TEXT("a killer"), Killer))
	{
		// Moved away from the start first, so arriving there is a move rather
		// than never having left.
		Player->SetActorLocation(FVector(-4000.0f, 4000.0f, 0.0f));

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		Player->Revive();

		TestTrue(TEXT("it stood up at the player start"),
			Player->GetActorLocation().Equals(StartHere, 1.0f));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmRevivingTheLivingDoesNothingTest,
	"Cataclysm.Death.RevivingSomethingThatIsNotDeadChangesNothing")
{
	// OTHERWISE Revive IS A FREE FULL HEAL for anything that calls it by
	// mistake, and the mistake would be invisible.
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);

	if (TestNotNull(TEXT("a player"), Player))
	{
		UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Player);
		if (!TestNotNull(TEXT("the player has an ability system"), AbilitySystem))
		{
			World->DestroyWorld(false);
			return false;
		}

		// WRITTEN RATHER THAN DEALT, so the wounded figure is exact and this test
		// measures Revive alone instead of also measuring the mitigation
		// pipeline.
		const float FullHealth = CataclysmDeathTest::HealthOf(Player);
		const float Wounded = FullHealth / 2.0f;
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), Wounded);

		TestTrue(TEXT("it is hurt"), Wounded < FullHealth);
		TestFalse(TEXT("and alive"), UCataclysmSkillEffects::IsDead(Player));

		TestFalse(TEXT("clearing a mark that is not there reports nothing done"),
			UCataclysmSkillEffects::ClearDead(Player));

		Player->Revive();

		TestEqual(TEXT("and reviving the living does not heal it"),
			CataclysmDeathTest::HealthOf(Player), Wounded, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

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
 * IT COST NOTHING WHEN IT WAS WRITTEN, because a player's death was not built
 * and the note at the top of this file said so. It costs something now: issue
 * #570 built one, so the lookup this pins is on the path a real player death
 * takes. The player death tests above would fail if it regressed, and this one
 * still says which lookup is wrong rather than only that something is.
 *
 * The arrangement below is artificial on purpose: an enemy is given an ability
 * system whose owner is some other actor, so that owner and avatar differ the
 * way they do for the player. It exercises the distinction without depending on
 * anything a player character does.
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

#endif  // WITH_AUTOMATION_TESTS
