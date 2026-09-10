// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStacks.h"
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
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayEffect.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
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

	/** Fervour, for a Masochist. Every class shares this one attribute. */
	static float ClassResourceOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute())
			: -1.0f;
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

/**
 * A respawn empties the class resource, though it refills the three vitals.
 *
 * ISSUE #956, decided by the project owner on 2026-08-26. A player that died at
 * a full Fervour bar used to stand back up at a full Fervour bar, because
 * `Revive` refills health with `SetNumericAttributeBase`, which is a direct write
 * and does not run through `UCataclysmRegeneration::TopUp`. Fervour is emptied by
 * HEALING, so the largest amount of health a character ever gets back at once
 * removed none of it, and a player could bank a full bar through a death.
 *
 * THE RESOURCE IS SET DIRECTLY RATHER THAN EARNED, so this does not depend on
 * which class the test player is or on what its class line supplies. What the
 * issue describes is "had Fervour when it died", and a direct write is that.
 *
 * IT IS CHECKED AFTER THE DEATH AND BEFORE THE REVIVE, which is what makes this
 * test able to fail for the right reason. If dying cleared the resource on its
 * own, the last assertion would pass while `Revive` did nothing at all.
 */
CATACLYSM_TEST(FCataclysmRespawnEmptiesTheClassResourceTest,
	"Cataclysm.Death.ARespawnEmptiesTheClassResource")
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
		UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Player);
		if (TestNotNull(TEXT("an ability system"), System))
		{
			const float FullHealth = CataclysmDeathTest::HealthOf(Player);

			System->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
				60.0f);
			TestEqual(TEXT("it is holding some of its class resource"),
				CataclysmDeathTest::ClassResourceOf(Player), 60.0f, 0.01f);

			UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
			TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

			// THE ASSERTION THAT MAKES THE ONE BELOW MEAN SOMETHING. Dying does
			// not empty the resource; standing back up is what does.
			TestEqual(TEXT("dying on its own did not empty it"),
				CataclysmDeathTest::ClassResourceOf(Player), 60.0f, 0.01f);

			Player->Revive();

			TestEqual(TEXT("standing back up emptied it"),
				CataclysmDeathTest::ClassResourceOf(Player), 0.0f, 0.01f);

			// AND THE THREE VITALS STILL COME BACK FULL, because emptying the
			// resource is an exception to that rule rather than a change to it.
			TestEqual(TEXT("and health is still refilled"),
				CataclysmDeathTest::HealthOf(Player), FullHealth, 0.01f);
		}
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

// --------------------------------------------------------------------------
// A respawn clears everything temporary. Issues #1535 and #1013
// --------------------------------------------------------------------------
//
// THE PROJECT OWNER'S RULING OF 2026-09-10, which `docs/DECISIONS.md` records:
// the passive tree, equipment, and anything that says it is permanent keep
// working, and every temporary buff, debuff and stack is cleared. That covers
// stacks earned through a passive node, and the Masochist's health debt for
// every character, The Reckoning included.
//
// EVERY TEST HERE CHECKS AFTER THE DEATH AND BEFORE THE REVIVE, the pattern
// `ARespawnEmptiesTheClassResource` above set, so something dying cleared on its
// own cannot make an assertion about `Revive` pass.
//
// AND FOUR OF THEM CHECK WHAT MUST SURVIVE. A clear that removed too much -- a
// skill's cooldown, the passive tree, what is worn, or anything at all on a
// character that was not dead -- would pass every test of the first kind.

namespace CataclysmDeathTest
{
	/** This project's ability system on an actor, or null. */
	static UCataclysmAbilitySystemComponent* CataclysmSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** One attribute's current value, or -1 with no ability system. */
	static float AttributeOf(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Attribute) : -1.0f;
	}

	/** A tag by name, or an invalid tag if the vocabulary has lost it. */
	static FGameplayTag TagNamed(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name),
												/*ErrorIfNotFound=*/false);
	}

	/**
	 * A player a controller has possessed, which is what puts the real class
	 * stat line on it. `SpawnPlayer` above drives the client path and leaves the
	 * attribute sets' placeholder numbers, which is enough for everything except
	 * the passive tree and gear, whose whole point is that they move a real stat
	 * line.
	 *
	 * THE SAME HELPER `CataclysmPassiveTreeTests.cpp` USES, and for its reason:
	 * `AController::Possess` rather than `APawn::PossessedBy`, because only the
	 * first tells the controller which pawn it has.
	 */
	static ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Actor = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!State || !Controller || !Actor)
		{
			return nullptr;
		}

		Controller->SetPlayerState(State);
		Controller->Possess(Actor);
		return Actor;
	}

	/**
	 * A skill template granted into a slot and given a row's parameters, the way
	 * `CataclysmSkillTemplateTests.cpp` grants one to its fighters.
	 */
	template <typename TSkill>
	static TSkill* GrantSkill(ACataclysmPlayerCharacter* Player,
							  ECataclysmAbilitySlot Slot, const FString& ParamText,
							  const FString& Name, const FString& TagCell)
	{
		UCataclysmAbilitySystemComponent* System = CataclysmSystemOf(Player);
		if (!System)
		{
			return nullptr;
		}

		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			TSkill::StaticClass(), Slot, /*Level=*/100, Player);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		TSkill* Skill = Spec ? Cast<TSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = Name;
			Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Skill;
	}

	/** Use a granted skill, as its key would. */
	static bool Activate(ACataclysmPlayerCharacter* Player, UGameplayAbility* Skill)
	{
		UCataclysmAbilitySystemComponent* System = CataclysmSystemOf(Player);
		return System && Skill
			&& System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(),
										  /*bAllowRemoteActivation=*/false);
	}

	/** The longest time left on anything granting this tag, or zero. */
	static float SecondsLeftOn(const UAbilitySystemComponent* System,
							   const FGameplayTag& Tag)
	{
		float Longest = 0.0f;
		for (const float Left : System->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
					 FGameplayTagContainer(Tag))))
		{
			Longest = FMath::Max(Longest, Left);
		}
		return Longest;
	}
}

/** Every kind of stack a character held when it died is gone when it stands up. */
CATACLYSM_TEST(FCataclysmRespawnClearsEveryStackTest,
	"Cataclysm.Death.ARespawnClearsEveryKindOfStack")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		// EVERY KIND, TAKEN FROM THE ENUMERATION RATHER THAN LISTED, so a kind
		// added later is covered without anybody remembering it here. Three are a
		// Masochist's own and come from passive nodes -- Sanguine Momentum,
		// Bloodlust and Carnage -- and the ruling clears those too: the node stays
		// and the stacks build again from zero. The fourth, Infernal Brand, is a
		// debuff a creature puts on the player, and a brand kept through a death
		// is what issue #1535 was filed about.
		//
		// TWO OF EACH, so that "the next one is the first" below can tell a count
		// that was emptied from one that was merely left alone.
		for (int32 Index = 0; Index < UCataclysmStacks::KindCount; ++Index)
		{
			const ECataclysmStackKind Kind = static_cast<ECataclysmStackKind>(Index);
			for (int32 Grant = 0; Grant < 2; ++Grant)
			{
				System->GrantStack(Kind, UCataclysmStacks::WindowSecondsFor(Kind),
								   UCataclysmStacks::CapFor(Kind));
			}
		}

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		// STILL STANDING ON THE CORPSE. At least two rather than exactly two,
		// because taking damage grants Bloodlust and the killing blow may add one.
		for (int32 Index = 0; Index < UCataclysmStacks::KindCount; ++Index)
		{
			const ECataclysmStackKind Kind = static_cast<ECataclysmStackKind>(Index);
			TestTrue(*FString::Printf(TEXT("dying on its own left its %s standing"),
									  UCataclysmStacks::NameOf(Kind)),
					 UCataclysmStacks::Held(System, Kind) >= 2);
		}

		Player->Revive();
		TestFalse(TEXT("it stood back up"), UCataclysmSkillEffects::IsDead(Player));

		for (int32 Index = 0; Index < UCataclysmStacks::KindCount; ++Index)
		{
			const ECataclysmStackKind Kind = static_cast<ECataclysmStackKind>(Index);
			TestEqual(*FString::Printf(TEXT("standing back up cleared its %s"),
									   UCataclysmStacks::NameOf(Kind)),
					  UCataclysmStacks::Held(System, Kind), 0);

			// AND THEY BUILD AGAIN FROM ZERO, which is the ruling's own words.
			System->GrantStack(Kind, UCataclysmStacks::WindowSecondsFor(Kind),
							   UCataclysmStacks::CapFor(Kind));
			TestEqual(*FString::Printf(TEXT("and its next %s is the first"),
									   UCataclysmStacks::NameOf(Kind)),
					  UCataclysmStacks::Held(System, Kind), 1);
		}
	}

	World->DestroyWorld(false);
	return true;
}

/** A character that died owing health stands back up owing nothing. #1013. */
CATACLYSM_TEST(FCataclysmRespawnClearsTheHealthDebtTest,
	"Cataclysm.Death.ARespawnClearsTheHealthDebt")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		const FGameplayAttribute Owed =
			UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();

		// DEFERRED THROUGH THE REAL FUNCTION, so the amount and the time it falls
		// due are written the way Deferred Payment writes them, and then pushed
		// out once the way Rolling Debt pushes one.
		UCataclysmHealthDebt::Defer(System, 300.0f);
		System->ExtendHealthDebtDueBy(/*Seconds=*/1.0f, /*MostAltogether=*/3.0f);
		TestEqual(TEXT("it owes the deferred cost"),
			CataclysmDeathTest::AttributeOf(Player, Owed), 300.0f, 0.01f);
		TestTrue(TEXT("and the debt has a time to fall due"),
			System->HealthDebtDueAt() >= 0.0f);
		TestEqual(TEXT("which has been pushed out once"),
			System->HealthDebtExtensionApplied(), 1.0f, 0.01f);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		// ISSUE #1013 AS IT WAS FILED: a character that dies owing health still
		// owes it.
		TestEqual(TEXT("dying on its own did not clear the debt"),
			CataclysmDeathTest::AttributeOf(Player, Owed), 300.0f, 0.01f);
		TestTrue(TEXT("or the time it falls due"),
			System->HealthDebtDueAt() >= 0.0f);

		Player->Revive();

		TestEqual(TEXT("standing back up cleared what was owed"),
			CataclysmDeathTest::AttributeOf(Player, Owed), 0.0f, 0.01f);
		TestTrue(TEXT("and forgot when it would have fallen due"),
			System->HealthDebtDueAt() < 0.0f);
		TestFalse(TEXT("so nothing is due"), System->IsHealthDebtDue());
		TestEqual(TEXT("and the next debt has its whole Rolling Debt allowance"),
			System->HealthDebtExtensionApplied(), 0.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

/** The same for a character carrying The Reckoning. Issue #1013. */
CATACLYSM_TEST(FCataclysmRespawnClearsTheReckoningsDebtTest,
	"Cataclysm.Death.ARespawnClearsTheHealthDebtUnderTheReckoningToo")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		const FGameplayAttribute Owed =
			UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();

		// THE RECKONING'S FLAG, written as its one node writes it: a flat 1. Its
		// debt "is cleared by killing an enemy and never by time", which says what
		// clears it in play and does not say it survives a death, so the ruling
		// clears it with everybody else's. A respawn that cleared a debt only by
		// the rules a kill or a timer follows would miss exactly this character.
		System->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetHealthDebtClearedOnlyByAKillAttribute(),
			1.0f);
		TestTrue(TEXT("the character carries The Reckoning"),
			UCataclysmHealthDebt::IsClearedOnlyByAKill(System));

		UCataclysmHealthDebt::Defer(System, 300.0f);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));
		TestEqual(TEXT("dying on its own did not clear the debt"),
			CataclysmDeathTest::AttributeOf(Player, Owed), 300.0f, 0.01f);

		Player->Revive();

		TestEqual(TEXT("standing back up cleared it anyway"),
			CataclysmDeathTest::AttributeOf(Player, Owed), 0.0f, 0.01f);
		TestTrue(TEXT("and forgot when it would have fallen due"),
			System->HealthDebtDueAt() < 0.0f);

		// THE KEYSTONE ITSELF IS KEPT. It is part of the passive tree; only the
		// debt it built up was temporary.
		TestTrue(TEXT("while the character still carries The Reckoning"),
			UCataclysmHealthDebt::IsClearedOnlyByAKill(System));
	}

	World->DestroyWorld(false);
	return true;
}

/** Every timed effect on a character when it died is gone when it stands up. */
CATACLYSM_TEST(FCataclysmRespawnRemovesTimedEffectsTest,
	"Cataclysm.Death.ARespawnRemovesEveryTimedEffectOnTheCharacter")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		// ONE OF EACH KIND OF TIMED EFFECT A CREATURE LEAVES ON A PLAYER, each put
		// on by the function the game uses: a burn, which is damage over time; a
		// curse, which is a tag held for a duration; and a stun, which leaves stun
		// immunity behind it. Each lasts longer than the three second respawn
		// delay, so in the running game each would still be on the character when
		// it stood up.
		const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
		const FGameplayTag Curse =
			CataclysmDeathTest::TagNamed(TEXT("Status.Debuff.Cripple"));
		const FGameplayTag Stunned = UCataclysmSkillEffects::StunnedTag();
		const FGameplayTag StunImmune = UCataclysmSkillEffects::StunImmuneTag();
		if (!TestTrue(TEXT("the vocabulary has all four tags"),
				Burn.IsValid() && Curse.IsValid() && Stunned.IsValid()
				&& StunImmune.IsValid()))
		{
			World->DestroyWorld(false);
			return false;
		}

		TestTrue(TEXT("a burn lands"), UCataclysmSkillEffects::ApplyBurn(
			Killer, Player, 100.0f, /*bScalesWithInstigator=*/true,
			/*bBurnIsDesigned=*/true));
		TestTrue(TEXT("a curse lands"), UCataclysmSkillEffects::ApplyTagForDuration(
			Killer, Player, Curse, 30.0f));
		TestTrue(TEXT("a stun lands"), UCataclysmSkillEffects::ApplyStun(
			Killer, Player, 2.0f, /*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true));

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		TestTrue(TEXT("the burn is still on the corpse"),
			UCataclysmSkillEffects::HasTag(Player, Burn));
		TestTrue(TEXT("so is the curse"), UCataclysmSkillEffects::HasTag(Player, Curse));
		TestTrue(TEXT("so is the stun"),
			UCataclysmSkillEffects::HasTag(Player, Stunned));
		TestTrue(TEXT("and the stun immunity"),
			UCataclysmSkillEffects::HasTag(Player, StunImmune));

		Player->Revive();

		TestFalse(TEXT("standing back up put the burn out"),
			UCataclysmSkillEffects::HasTag(Player, Burn));
		TestFalse(TEXT("lifted the curse"),
			UCataclysmSkillEffects::HasTag(Player, Curse));
		TestFalse(TEXT("ended the stun"),
			UCataclysmSkillEffects::HasTag(Player, Stunned));
		TestFalse(TEXT("and the stun immunity with it"),
			UCataclysmSkillEffects::HasTag(Player, StunImmune));
		TestEqual(TEXT("so it carries no debuff at all"),
			UCataclysmDebuffs::CountOnActor(Player), 0);
		TestEqual(TEXT("and no timed effect of any kind is left on it"),
			System->GetNumActiveGameplayEffects(), 0);
	}

	World->DestroyWorld(false);
	return true;
}

/**
 * A skill's cooldown is not cleared by a respawn. A judgement rather than the
 * owner's words: `UCataclysmAbilitySystemComponent::ClearWhatDeathEnds` says why.
 */
CATACLYSM_TEST(FCataclysmRespawnKeepsCooldownsTest,
	"Cataclysm.Death.ARespawnLeavesASkillsCooldownRunning")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);

	// FAR OUTSIDE THE SWING'S FOUR METRES, so the skill used below cannot touch
	// the creature that is about to kill the player.
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(1500.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		// A REAL SKILL, USED THE WAY ITS KEY USES IT, so the cooldown on the
		// character is the effect `UCataclysmGameplayAbility::ApplyCooldown`
		// builds rather than one made here to look like it.
		UCataclysmStrikeSkill* Strike =
			CataclysmDeathTest::GrantSkill<UCataclysmStrikeSkill>(
				Player, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
				TEXT("Molten Cleave"), TEXT("Element.Demonic"));
		if (!TestNotNull(TEXT("the skill is granted"), Strike))
		{
			World->DestroyWorld(false);
			return false;
		}

		// LONGER THAN ANYTHING HERE, and stated rather than read off the Heavy
		// slot's row, so the test does not move when that number does.
		Strike->CooldownOverride = 30.0f;
		TestTrue(TEXT("the skill is used"),
			CataclysmDeathTest::Activate(Player, Strike));

		const FGameplayTag Cooldown =
			UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);
		TestTrue(TEXT("and it is waiting to be used again"),
			UCataclysmSkillEffects::HasTag(Player, Cooldown));
		const float Left = CataclysmDeathTest::SecondsLeftOn(System, Cooldown);
		TestTrue(TEXT("with time left on it"), Left > 0.0f);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));
		TestTrue(TEXT("dying left the cooldown running"),
			UCataclysmSkillEffects::HasTag(Player, Cooldown));

		Player->Revive();

		TestTrue(TEXT("and standing back up left it running too"),
			UCataclysmSkillEffects::HasTag(Player, Cooldown));
		TestEqual(TEXT("with the same time left on it"),
			CataclysmDeathTest::SecondsLeftOn(System, Cooldown), Left, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

/** A self buff still running when its caster died is ended when it stands up. */
CATACLYSM_TEST(FCataclysmRespawnEndsASelfBuffTest,
	"Cataclysm.Death.ARespawnEndsASelfBuffThatWasStillRunning")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Alight = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(-300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("something to set alight"), Alight)
		&& TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		// SOMETHING BURNING INSIDE FIFTEEN METRES, because Burning Wrath is worth
		// "4% more fire damage for every enemy currently burning within 15
		// meters" and grants nothing with none. The same arrangement
		// `Cataclysm.Skills.ABuffsIncreaseIsTakenAwayWhenTheBuffEnds` uses.
		UCataclysmSkillEffects::ApplyBurn(Player, Alight, 100.0f,
			/*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true);

		UCataclysmSelfBuffSkill* Buff =
			CataclysmDeathTest::GrantSkill<UCataclysmSelfBuffSkill>(
				Player, ECataclysmAbilitySlot::Support,
				TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"),
				TEXT("Burning Wrath"), TEXT("Element.Demonic"));
		if (!TestNotNull(TEXT("the buff is granted"), Buff))
		{
			World->DestroyWorld(false);
			return false;
		}

		TestTrue(TEXT("the buff is used"), CataclysmDeathTest::Activate(Player, Buff));
		TestEqual(TEXT("and puts one modifier on the character"),
			System->GetStatModifiers().Num(), 1);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		// NOT ENDED BY THE DEATH, AND THAT IS DELIBERATE. `HandleDeath` runs
		// inside the effect that dealt the killing blow and does not cancel
		// abilities from there, so the buff runs on into the respawn unless
		// something ends it then.
		TestTrue(TEXT("dying on its own did not end the buff"), Buff->IsActive());
		TestEqual(TEXT("so its modifier is still on the corpse"),
			System->GetStatModifiers().Num(), 1);

		Player->Revive();

		TestFalse(TEXT("standing back up ended the buff"), Buff->IsActive());
		TestEqual(TEXT("and took its modifier away"),
			System->GetStatModifiers().Num(), 0);
		TestEqual(TEXT("so it reports granting nothing"), Buff->GrantedIncrease, 0.0f);
	}

	World->DestroyWorld(false);
	return true;
}

/**
 * The windows a recent event opened are shut when a character stands up, and
 * the waits beside them are not.
 */
CATACLYSM_TEST(FCataclysmRespawnClosesWindowsTest,
	"Cataclysm.Death.ARespawnClosesTheWindowsARecentEventOpened")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		// OPENED ON THE CORPSE RATHER THAN BEFORE THE DEATH, and that is the one
		// departure from the pattern above. An open Breaking Point conversion
		// turns the damage a character takes into Bleeding, so opened first it
		// would have turned the killing blow into a bleed and the character would
		// not have died. What is measured is the same either way: what `Revive`
		// does to a window that is open when it runs.
		//
		// EACH ONE OPENED THROUGH THE CALL THE GAME MAKES WHEN THE EVENT HAPPENS.
		System->NoteHealthCostPaid();
		System->NoteForeignDamageTaken();
		System->NoteDamageConversionStarted(/*WindowSeconds=*/4.0f,
											/*CooldownSeconds=*/10.0f);
		System->TakeNextDisplacementShare();

		FCataclysmLeechPayment Promised;
		Promised.Pool = ECataclysmLeechPool::Health;
		Promised.Remaining = 50.0f;
		Promised.SecondsLeft = 3.0f;
		System->AddLeechPayment(Promised);

		// AND THREE PASSIVE NODES' OWN WAITS, which a respawn keeps. The fourth
		// wait, The Breaking Point's, was started by the conversion above.
		System->NoteNovaReleased(5.0f);
		System->NoteAuraApplied(3.0f);
		System->NoteLowHealthReliefTaken(30.0f);

		TestTrue(TEXT("a health cost counts as recent"),
			System->SecondsSinceHealthCostPaid() >= 0.0f);
		TestTrue(TEXT("so does foreign damage"),
			System->SecondsSinceForeignDamageTaken() >= 0.0f);
		TestTrue(TEXT("damage is being turned into Bleeding"),
			System->IsConvertingDamageToBleeding());
		TestEqual(TEXT("one shove is counted"), System->DisplacementsInWindow(), 1);
		TestEqual(TEXT("and one hit's leech is owed"),
			System->GetLeechPayments().Num(), 1);

		Player->Revive();

		TestTrue(TEXT("no health cost counts as recent any more"),
			System->SecondsSinceHealthCostPaid() < 0.0f);
		TestTrue(TEXT("nor any foreign damage"),
			System->SecondsSinceForeignDamageTaken() < 0.0f);
		TestFalse(TEXT("damage is no longer turned into Bleeding"),
			System->IsConvertingDamageToBleeding());
		TestEqual(TEXT("the next shove moves it the whole distance"),
			System->DisplacementsInWindow(), 0);
		TestEqual(TEXT("and no leech is left to pay out"),
			System->GetLeechPayments().Num(), 0);

		// THE WAITS ARE KEPT, for the reason a skill's cooldown is.
		TestFalse(TEXT("The Breaking Point still has to wait"),
			System->MayStartDamageConversion());
		TestFalse(TEXT("so does the Unstable Aura's nova"), System->MayReleaseNova());
		TestFalse(TEXT("so does Beacon of Despair"), System->MayApplyAura());
		TestFalse(TEXT("and so does Rock Bottom"), System->MayTakeLowHealthRelief());
	}

	World->DestroyWorld(false);
	return true;
}

/**
 * A timed effect that lowered a maximum is lifted before the refill, so the
 * character stands up at its whole maximum.
 */
CATACLYSM_TEST(FCataclysmRespawnFillsToTheWholeMaximumTest,
	"Cataclysm.Death.ARespawnFillsHealthToAMaximumATimedEffectHadLowered")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		const FGameplayAttribute MaxHealth =
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
		const float WholeMaximum = CataclysmDeathTest::AttributeOf(Player, MaxHealth);

		// A STAND-IN FOR WITHERING TOUCH, WHICH IS NOT BUILT. That dungeon
		// modifier's row describes a debuff that "reduces your max HP and max
		// mana", and the ruling ends it at death with everything else limited to
		// a dungeon. No effect that is built lowers a maximum, so this one is made
		// here: a timed effect taking two fifths off maximum health.
		UObject* Outer = GetTransientPackage();
		UGameplayEffect* Lowering = NewObject<UGameplayEffect>(
			Outer, MakeUniqueObjectName(Outer, UGameplayEffect::StaticClass(),
										FName(TEXT("DeathTest_LowerMaximumHealth"))));
		Lowering->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Lowering->DurationMagnitude =
			FGameplayEffectModifierMagnitude(FScalableFloat(60.0f));
		FGameplayModifierInfo& Cut = Lowering->Modifiers.AddDefaulted_GetRef();
		Cut.Attribute = MaxHealth;
		Cut.ModifierOp = EGameplayModOp::Additive;
		Cut.ModifierMagnitude = FScalableFloat(-WholeMaximum * 0.4f);
		System->ApplyGameplayEffectToSelf(Lowering, /*Level=*/1.0f,
										  System->MakeEffectContext());

		const float Lowered = CataclysmDeathTest::AttributeOf(Player, MaxHealth);
		TestEqual(TEXT("the effect took two fifths off maximum health"),
			Lowered, WholeMaximum * 0.6f, 0.01f);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));
		TestEqual(TEXT("and the maximum is still lowered on the corpse"),
			CataclysmDeathTest::AttributeOf(Player, MaxHealth), Lowered, 0.01f);

		Player->Revive();

		TestEqual(TEXT("standing back up lifted the effect"),
			CataclysmDeathTest::AttributeOf(Player, MaxHealth), WholeMaximum, 0.01f);

		// THE ASSERTION THE ORDER IN `Revive` IS FOR. Refilled before the effect
		// came off, health would stand at the lowered figure under a maximum that
		// had gone back up.
		TestEqual(TEXT("and filled health to the whole maximum, not the lowered one"),
			CataclysmDeathTest::HealthOf(Player), WholeMaximum, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

/** What the passive tree grants works the same after a respawn as before it. */
CATACLYSM_TEST(FCataclysmRespawnKeepsThePassiveTreeTest,
	"Cataclysm.Death.ARespawnKeepsWhatThePassiveTreeGrants")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPossessedPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	ACataclysmPlayerState* State =
		Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;

	if (TestNotNull(TEXT("a possessed player"), Player)
		&& TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with a player state"), State))
	{
		const FGameplayAttribute MaxHealth =
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
		const FGameplayAttribute Armour =
			UCataclysmCombatAttributeSet::GetArmorAttribute();
		const float HealthBefore = CataclysmDeathTest::AttributeOf(Player, MaxHealth);
		const float ArmourBefore = CataclysmDeathTest::AttributeOf(Player, Armour);

		// THE NODE `Cataclysm.Passives.SpendingAPointRaisesMaximumHealthWithNothing
		// ElseTouched` spends into, through the same call: Pain Tolerance, behind
		// the Masochist root, which raises maximum health and armour.
		const FName Root(TEXT("Masochist_basic_spine_000"));
		const FName PainTolerance(TEXT("Masochist_basic_spine_001"));
		const int32 Points = 10;

		FString Reason;
		if (!TestTrue(TEXT("the root takes a point"),
				State->SpendPassivePoint(Root, Reason)))
		{
			AddError(Reason);
		}
		for (int32 Point = 0; Point < Points; ++Point)
		{
			if (!State->SpendPassivePoint(PainTolerance, Reason))
			{
				AddError(FString::Printf(
					TEXT("point %d into Pain Tolerance was refused: %s"),
					Point + 1, *Reason));
				break;
			}
		}

		const float TreeHealth = CataclysmDeathTest::AttributeOf(Player, MaxHealth);
		const float TreeArmour = CataclysmDeathTest::AttributeOf(Player, Armour);
		TestTrue(*FString::Printf(TEXT("the points raised maximum health: %.1f to %.1f"),
								  HealthBefore, TreeHealth),
				 TreeHealth > HealthBefore);
		TestTrue(*FString::Printf(TEXT("and armour: %.1f to %.1f"),
								  ArmourBefore, TreeArmour),
				 TreeArmour > ArmourBefore);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		Player->Revive();

		TestEqual(TEXT("the tree's maximum health survived the respawn"),
			CataclysmDeathTest::AttributeOf(Player, MaxHealth), TreeHealth, 0.01f);
		TestEqual(TEXT("and so did its armour"),
			CataclysmDeathTest::AttributeOf(Player, Armour), TreeArmour, 0.01f);
		TestEqual(TEXT("the character came back filled to the raised maximum"),
			CataclysmDeathTest::HealthOf(Player), TreeHealth, 0.01f);
		TestEqual(TEXT("and every point is still on the node"),
			State->GetPassiveAllocation().PointsIn(PainTolerance), Points);
	}

	World->DestroyWorld(false);
	return true;
}

/** What worn gear grants works the same after a respawn as before it. */
CATACLYSM_TEST(FCataclysmRespawnKeepsWornGearTest,
	"Cataclysm.Death.ARespawnKeepsWhatWornGearGrants")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPossessedPlayer(World);
	ACataclysmEnemyCharacter* Killer = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);
	UCataclysmEquipmentComponent* Equipment =
		Player ? Player->GetEquipment() : nullptr;

	if (TestNotNull(TEXT("a possessed player"), Player)
		&& TestNotNull(TEXT("a killer"), Killer)
		&& TestNotNull(TEXT("with this project's ability system"), System)
		&& TestNotNull(TEXT("and something to wear things"), Equipment))
	{
		const FGameplayAttribute MaxHealth =
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
		const float Bare = CataclysmDeathTest::AttributeOf(Player, MaxHealth);

		// A HELM CARRYING ONE PERFECTLY ROLLED FLAT MAXIMUM HEALTH AFFIX, the item
		// `Cataclysm.Equipment.WearingAnItemRaisesTheAttributeAndRemovingItLowersIt`
		// wears.
		FCataclysmItem Helm;
		Helm.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledAffix Health;
		Health.Affix = FName(TEXT("Stat_Flat_maximum_health"));
		Health.Tier = UCataclysmItemValues::MaxAffixTier;
		Health.Roll = 1.0f;
		Helm.Affixes.Add(Health);

		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;
		TestEqual(TEXT("the helm goes on and nothing comes off"),
			static_cast<int32>(Equipment->Equip(Helm, Removed, AlsoRemoved, Slot)),
			static_cast<int32>(ECataclysmEquipResult::Equipped));

		// REFRESHED HERE AS WELL AS BY THE CHARACTER'S OWN HANDLER, so this does
		// not depend on the equipment broadcast having reached one.
		Equipment->RefreshAttributes(System);

		const float Wearing = CataclysmDeathTest::AttributeOf(Player, MaxHealth);
		TestTrue(*FString::Printf(TEXT("wearing it raised maximum health: %.1f to %.1f"),
								  Bare, Wearing),
				 Wearing > Bare);

		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
		TestTrue(TEXT("it died"), UCataclysmSkillEffects::IsDead(Player));

		Player->Revive();

		TestEqual(TEXT("the helm's maximum health survived the respawn"),
			CataclysmDeathTest::AttributeOf(Player, MaxHealth), Wearing, 0.01f);
		TestEqual(TEXT("and the character came back filled to it"),
			CataclysmDeathTest::HealthOf(Player), Wearing, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

/**
 * `Revive` on a character that is not dead clears nothing, for the reason it
 * heals nothing: otherwise it would be a free cleanse for anything that called
 * it by mistake.
 */
CATACLYSM_TEST(FCataclysmRevivingTheLivingClearsNothingTest,
	"Cataclysm.Death.RevivingSomethingThatIsNotDeadClearsNothing")
{
	UWorld* World = CataclysmDeathTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmDeathTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Curser = CataclysmDeathTest::SpawnEnemy(
		World, FVector(300.0f, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmAbilitySystemComponent* System =
		CataclysmDeathTest::CataclysmSystemOf(Player);

	if (TestNotNull(TEXT("a player"), Player) && TestNotNull(TEXT("a curser"), Curser)
		&& TestNotNull(TEXT("with this project's ability system"), System))
	{
		const ECataclysmStackKind Brand = ECataclysmStackKind::InfernalBrand;
		System->GrantStack(Brand, UCataclysmStacks::WindowSecondsFor(Brand),
						   UCataclysmStacks::CapFor(Brand));
		UCataclysmHealthDebt::Defer(System, 300.0f);
		const FGameplayTag Curse =
			CataclysmDeathTest::TagNamed(TEXT("Status.Debuff.Cripple"));
		TestTrue(TEXT("a curse lands"), UCataclysmSkillEffects::ApplyTagForDuration(
			Curser, Player, Curse, 30.0f));

		TestFalse(TEXT("it is alive"), UCataclysmSkillEffects::IsDead(Player));

		Player->Revive();

		TestEqual(TEXT("its stack is still standing"),
			UCataclysmStacks::Held(System, Brand), 1);
		TestEqual(TEXT("it still owes"),
			CataclysmDeathTest::AttributeOf(Player,
				UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute()),
			300.0f, 0.01f);
		TestTrue(TEXT("and it still carries the curse"),
			UCataclysmSkillEffects::HasTag(Player, Curse));
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
