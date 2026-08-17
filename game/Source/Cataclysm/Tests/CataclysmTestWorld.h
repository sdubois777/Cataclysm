// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

/**
 * The world every automation test runs in, in one place.
 *
 * WHY THIS FILE EXISTS AT ALL. Thirty test files each carried their own private
 * copy of a world helper, and the copies drifted into three shapes under two
 * names. Twenty of them called theirs `MakeWorldThatHasBegunPlay` and it did not
 * begin play, one carried the identical body under the honest name `MakeWorld`,
 * and two files stated in comments that "actors spawned after this point get
 * their BeginPlay called as they spawn", which was false. Issue #654.
 *
 * WHAT WAS ACTUALLY WRONG. `UWorld::BeginPlay` does nothing unless the world has
 * an authority game mode: it is one call to `GetAuthGameMode()->StartPlay()`, and
 * `AWorldSettings::NotifyBeginPlay` is the only thing that ever sets the world's
 * begun-play flag. A world built by `UWorld::CreateWorld` has no game instance
 * and therefore no game mode, so the flag stayed false. Every actor spawned into
 * it then skipped `BeginPlay` silently, because `AActor::PostActorConstruction`
 * dispatches only when `UWorld::HasBegunPlay()` is true.
 *
 * IT LOOKED FINE, WHICH IS WHY IT LASTED. `InitializeActorsForPlay` does real
 * work -- it is what makes `PreInitializeComponents`, `InitializeComponents` and
 * `PostInitializeComponents` run on a spawn -- so components were registered and
 * actors looked alive. Only `BeginPlay` was missing, and what starts there is a
 * character's regeneration timer, the player's automatic basic attack, an enemy's
 * attribute application, two creatures' real art, and a ground zone's damage
 * timer. All of that was untested and nothing said so.
 *
 * WHY THE FLAG IS SET RATHER THAN A GAME MODE BEING BUILT. Giving the test world
 * a real game mode is the other way to fix this and it was rejected deliberately:
 * `ACataclysmGameMode` is what supplies the difficulty tier, and
 * `ACataclysmGameMode::DifficultyTierIn` answers tier 1 for a world without one.
 * Tests rely on that -- the tier decides what armour is worth, since armour
 * removes `armor / (armor + 800 x tier)` -- so introducing a game mode would
 * change armour arithmetic across the suite as a side effect of a harness fix.
 * Setting the flag changes exactly the one thing that was wrong.
 */
namespace CataclysmTestWorld
{
	/**
	 * Turn the critical strike roll off for the rest of the run.
	 *
	 * WHY A WORLD HELPER TOUCHES A CONSOLE VARIABLE. A critical strike is the
	 * only random roll in the project that fires on an ordinary hit -- a
	 * defender's evasion and block chance are zero unless something sets them,
	 * so those rolls never fire by accident, while an attacker's critical strike
	 * chance is 5% for a player holding any weapon and 5% to 15% for every enemy
	 * archetype. Any test that asserts an exact damage figure is therefore a
	 * coin toss.
	 *
	 * IT GOT WORSE WHEN THE TEST WORLD STARTED BEGINNING PLAY, under issue #654.
	 * An enemy's archetype attributes are applied in `BeginPlay`, so creatures
	 * that used to sit at a chance of zero in a test world now carry their real
	 * one, and tests that had never been at risk became intermittent.
	 *
	 * AND THE WORST KIND CANNOT BE FOUND BY SWEEPING. Running the whole suite
	 * with the roll forced on, and again forced off, finds every test that
	 * asserts one damage figure -- and finds none of the tests that compare two,
	 * because forcing the roll makes both hits agree. `ASandboxEnemysArmour`
	 * `ActuallyReducesAHit` is exactly that: it passed both sweeps and failed a
	 * plain run. There is no sweep that finds those, so they are not hunted; the
	 * randomness is switched off instead.
	 *
	 * A TEST THAT WANTS A CRITICAL STRIKE ASKS FOR ONE, with FScopedCritRoll
	 * below. `Cataclysm.Crit.*` and `Cataclysm.Overlay.*` do exactly that, so the
	 * roll itself is still covered.
	 *
	 * SET AT THE CONSOLE'S OWN PRIORITY. Unreal remembers who set a console
	 * variable and silently discards a write from code once the command line has
	 * set one. A plain `Set(100.0f)` looked present in the source and was absent
	 * from the run.
	 */
	inline void SilenceCriticalStrikes()
	{
		if (IConsoleVariable* Variable =
				IConsoleManager::Get().FindConsoleVariable(
					TEXT("Cataclysm.CritRoll")))
		{
			// 100 never critically strikes, because the roll is compared with
			// strictly less than and a chance is capped at 100.
			Variable->Set(100.0f, ECVF_SetByConsole);
		}
	}

	/**
	 * Pins the critical strike roll for as long as it is in scope.
	 *
	 * 0 always critically strikes, because every chance above zero beats it.
	 * 100 never does. For a test whose subject IS the roll.
	 */
	struct FScopedCritRoll
	{
		explicit FScopedCritRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.CritRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FScopedCritRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/**
	 * A world in which an actor spawned later actually receives `BeginPlay`.
	 *
	 * USE THIS BY DEFAULT. It is the one that behaves like the running game, and
	 * a test that does not care is not harmed by it.
	 *
	 * `Cataclysm.TestWorld.AnActorSpawnedIntoABegunWorldReceivesBeginPlay` is the
	 * test that proves the claim in this comment, and it exists because the same
	 * claim was written in two test files for weeks while being false.
	 */
	inline UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (!World)
		{
			return nullptr;
		}

		FURL URL;
		World->InitializeActorsForPlay(URL);

		// THE CALL THAT USED TO BE HERE DID NOTHING. `World->BeginPlay()` needs a
		// game mode and there is none, so this sets the flag that call would have
		// set. From then on every spawn dispatches `BeginPlay` by itself, through
		// `AActor::PostActorConstruction`, in the same order the running game
		// uses.
		//
		// `UWorld::HasBegunPlay` ALSO REQUIRES THE PERSISTENT LEVEL TO HOLD AN
		// ACTOR, which is why this is not simply a flag. A brand new world's
		// persistent level is not empty -- `InitializeActorsForPlay` leaves the
		// world settings actor in it -- but that is a property of the engine
		// rather than of this code, so the test named above spawns an actor and
		// asks it, instead of trusting the reasoning.
		World->SetBegunPlay(true);
		SilenceCriticalStrikes();

		return World;
	}

	/**
	 * Make an existing world begin play, for a test that created its own.
	 *
	 * FOR THE FEW PLACES THAT BUILD A WORLD INLINE rather than calling the
	 * helper above. It is the same two steps, so the rule stays in one file even
	 * where the creation does not.
	 */
	inline void BeginPlayIn(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		FURL URL;
		World->InitializeActorsForPlay(URL);
		World->SetBegunPlay(true);
		SilenceCriticalStrikes();
	}

	/**
	 * A world in which nothing receives `BeginPlay`, said out loud.
	 *
	 * FOR A TEST THAT WOULD BE DISTURBED BY IT rather than for one that does not
	 * care. A creature whose `BeginPlay` loads its real art, or a character that
	 * starts a repeating timer, is doing work a test about something else may not
	 * want. Say so here rather than by using a helper whose name is silent.
	 */
	inline UWorld* MakeWorldThatHasNotBegunPlay()
	{
		SilenceCriticalStrikes();
		return UWorld::CreateWorld(EWorldType::Game,
								   /*bInformEngineOfWorld=*/false);
	}
}
