// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Misc/ScopeExit.h"

/**
 * A player whose set-up runs twice hears each death, hit and skill use once. Issue #2290 part (a), ruled by the
 * coordinating session on 2026-10-23.
 *
 * WHAT THESE GUARD. `ACataclysmPlayerCharacter::InitAbilityActorInfo` binds the character to the world's
 * `UCataclysmCombatEvents::OnDeath`, `OnHit` and `OnSkillUsed`. Until #2290 it added those three with no handle and no
 * removal, while the six other bindings in the same function were removed before they were added, so a second run
 * left two bindings of each, and the character heard every death, hit and skill use twice.
 *
 * HOW THE SET-UP RUNS TWICE. The player is possessed, as `CataclysmKillCounterTest::SpawnPossessedPlayer` in
 * `CataclysmEnchantmentEffectTests.cpp` possesses one: a player state put on a player controller, then
 * `AController::Possess`, which reaches `PossessedBy` and so the first run. The second run is the client path,
 * `OnRep_PlayerState`, called by hand as `CataclysmDeathTests.cpp` calls it; it is public, and `InitAbilityActorInfo`
 * is protected. Nothing else in the tests runs it twice on one pawn.
 *
 * WHAT IS COUNTED, ONCE FOR EACH THING DONE. A listener the test binds to the player's own
 * `UCataclysmAbilitySystemComponent::OnActionEvent`, which `ActOnEvent` announces before either of its early returns,
 * so an event is counted whether or not a worn row acts on it. Three names: `kill` (raised only by `OnSomethingDied`),
 * `hit_dealt` (only by `OnSomethingWasHit`) and `skill_use` (only by `ActOnSkillUse`, which only `OnSkillWasUsed`
 * reaches here). And the player state's run kill count, which only `OnSomethingDied` raises in this world. And the
 * announcer's own counts, which say each thing was announced once, so a count of two can only be two listeners.
 *
 * THE WORLD. No game mode, so the player's binding is the only listener. The player at the origin, on the players'
 * team by its constructor; one creature on the monsters' team at 1.5 metres, with 100000 health and no attack damage.
 * Critical strikes pinned off after the world is made. Nothing waits on a tick.
 */
namespace CataclysmPlayerListenersBoundOnceTest
{
	/** A possessed player in `World`, set up a second time when `bSetUpTwice`. Null when anything could not spawn. */
	ACataclysmPlayerCharacter* SpawnListeningPlayer(UWorld* World, bool bSetUpTwice)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Player)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Player);
		if (bSetUpTwice)
		{
			Player->OnRep_PlayerState();
		}
		return Player;
	}

	/** The creature the player strikes and kills. */
	ACataclysmEnemyCharacter* SpawnListenedCreature(UWorld* World)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(150.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetRarityStep(0);
			Made->SetHealth(100000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** What one hit, one kill and one skill use were heard as. */
	struct FHeardCounts
	{
		int32 Kills = 0;
		int32 HitsDealt = 0;
		int32 SkillUses = 0;
	};

	/**
	 * One hit, one death and one skill use by a player set up once or twice, in a world of its own. False when the
	 * set-up failed; the failure is reported on `Test`.
	 */
	bool HearOneOfEach(FAutomationTestBase& Test, bool bSetUpTwice, FHeardCounts& Out)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("set-up: a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

		ACataclysmPlayerCharacter* Player = SpawnListeningPlayer(World, bSetUpTwice);
		ACataclysmPlayerState* State = Player ? Player->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		UCataclysmAbilitySystemComponent* System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
		ACataclysmEnemyCharacter* Creature = SpawnListenedCreature(World);
		UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
		if (!Test.TestNotNull(TEXT("set-up: a possessed player"), Player)
			|| !Test.TestNotNull(TEXT("set-up: its player state"), State)
			|| !Test.TestNotNull(TEXT("set-up: its ability system"), System)
			|| !Test.TestNotNull(TEXT("set-up: a creature"), Creature)
			|| !Test.TestNotNull(TEXT("set-up: the world's announcer"), Events))
		{
			return false;
		}

		static const FName KillEvent(TEXT("kill"));
		static const FName HitDealtEvent(TEXT("hit_dealt"));
		static const FName SkillUseEvent(TEXT("skill_use"));
		const FDelegateHandle Listening = System->OnActionEvent.AddLambda([&Out](FName Raised)
		{
			if (Raised == KillEvent)
			{
				++Out.Kills;
			}
			else if (Raised == HitDealtEvent)
			{
				++Out.HitsDealt;
			}
			else if (Raised == SkillUseEvent)
			{
				++Out.SkillUses;
			}
		});
		ON_SCOPE_EXIT { System->OnActionEvent.Remove(Listening); };

		// THE CONTROL, READ FIRST: nothing has been heard and nothing counted.
		const int32 KillsBefore = State->GetRunKills();
		const uint32 HitsSentBefore = Events->HitsSent();
		const uint32 DeathsSentBefore = Events->DeathsSent();
		const uint32 SkillUsesSentBefore = Events->SkillUsesSent();
		Test.TestEqual(TEXT("control: no kill counted before"), KillsBefore, 0);
		Test.TestEqual(TEXT("control: and no event heard before"), Out.Kills + Out.HitsDealt + Out.SkillUses, 0);

		// ONE HIT, THEN THE DEATH ANNOUNCED AS `Cataclysm.KillCounter.APlayersKillRaisesBothCountsAndTheConditionsReadThem`
		// announces it: the hit puts the player on the creature's record as its last blow, and the death names it the killer.
		UCataclysmSkillEffects::ApplyHit(Player, Creature, 100.0f, FGameplayTagContainer());
		if (!Test.TestFalse(TEXT("set-up: the hit leaves the creature alive"), UCataclysmSkillEffects::IsDead(Creature)))
		{
			return false;
		}
		UCataclysmCombatEvents::NoteDeath(Creature);

		// ONE SKILL USE, NOT THE BASIC ATTACK, which raises no `skill_use`.
		const FGameplayTagContainer NoTags;
		const FVector Aim = Creature->GetActorLocation();
		UCataclysmCombatEvents::NoteSkillUsed(Player, TEXT("A skill the player does not hold"), NoTags,
											  ECataclysmAbilitySlot::Heavy, &Aim);

		Test.TestEqual(TEXT("set-up: the hit was announced once"),
					   static_cast<int32>(Events->HitsSent() - HitsSentBefore), 1);
		Test.TestEqual(TEXT("set-up: the death was announced once"),
					   static_cast<int32>(Events->DeathsSent() - DeathsSentBefore), 1);
		Test.TestEqual(TEXT("set-up: the skill use was announced once"),
					   static_cast<int32>(Events->SkillUsesSent() - SkillUsesSentBefore), 1);
		Test.TestEqual(TEXT("the kill counted on the player state"), State->GetRunKills() - KillsBefore, 1);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerListenersBoundOnceControlTest,
	"Cataclysm.PlayerListenersBoundOnce.APlayerSetUpOnceHearsOneKillOneHitAndOneSkillUseOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL FOR THE TEST BELOW: possession alone, the one set-up single play makes. */
bool FCataclysmPlayerListenersBoundOnceControlTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerListenersBoundOnceTest;

	FHeardCounts Heard;
	if (!HearOneOfEach(*this, /*bSetUpTwice=*/false, Heard))
	{
		return false;
	}
	TestEqual(TEXT("set up once: the kill is heard once"), Heard.Kills, 1);
	TestEqual(TEXT("set up once: the hit is heard once"), Heard.HitsDealt, 1);
	TestEqual(TEXT("set up once: the skill use is heard once"), Heard.SkillUses, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerListenersBoundOnceTwiceTest,
	"Cataclysm.PlayerListenersBoundOnce.APlayerSetUpTwiceStillHearsOneKillOneHitAndOneSkillUseOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Possession, then `OnRep_PlayerState` on the same pawn: `InitAbilityActorInfo` runs twice. Each thing done is still
 * heard once, as in the control above. Before #2290 each count here was two.
 */
bool FCataclysmPlayerListenersBoundOnceTwiceTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerListenersBoundOnceTest;

	FHeardCounts Heard;
	if (!HearOneOfEach(*this, /*bSetUpTwice=*/true, Heard))
	{
		return false;
	}
	TestEqual(TEXT("set up twice: the kill is heard once"), Heard.Kills, 1);
	TestEqual(TEXT("set up twice: the hit is heard once"), Heard.HitsDealt, 1);
	TestEqual(TEXT("set up twice: the skill use is heard once"), Heard.SkillUses, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
