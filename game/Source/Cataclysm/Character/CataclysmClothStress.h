// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/WeakObjectPtr.h"

class ACataclysmEnemyCharacter;
class UWorld;

/**
 * A reproduction for the renderer's cloth check, run from the console. Issue
 * #1545.
 *
 * WHAT IT REPRODUCES. The project owner's Horde playtest on 2026-09-10 stopped
 * the editor with "Assertion failed: bPrevious" at GPUSkinVertexFactory.cpp
 * line 1348, inside the renderer's handling of a skinned model's simulated
 * cloth. Three creatures wear a model that carries cloth: the Brute (Rampage),
 * the Abyssal Warden (GruxMolten) and the Succubus (SM_Countess). The other
 * creatures' models and the player's carry none.
 *
 * WHY A CONSOLE COMMAND AND NOT AN AUTOMATION TEST. The automation command runs
 * with -nullrhi, so nothing is drawn and the render thread never reaches that
 * check. Issue #559 records the same limit for particle effects. This has to run
 * in a game that draws.
 *
 * WHAT IT DOES. `Cataclysm.Debug.ClothStress` kills every enemy already in the
 * level, so nothing attacks the player while it runs, then repeats one cycle
 * until it is stopped or its world ends:
 *
 *   1. spawn a batch of those three creatures in a ring around the player,
 *      taking them in turn, with no brain, so they stand and animate;
 *   2. with `lod`, force each of them to a different level of detail every
 *      frame. Each level of detail has its own pair of cloth buffers, created
 *      by the first two cloth updates at that level. Reading the engine found
 *      that the second of those updates switches to a buffer that does not
 *      exist yet and creates it a moment later, which is the one gap found in
 *      which the renderer's check could fail;
 *   3. after the cycle's seconds, write one log line counting what the living
 *      creatures carry, then kill them through ACataclysmEnemyCharacter::
 *      HandleDeath, the path a real death takes, so their death clips play and
 *      their bodies are removed the way a fight removes them.
 *
 * THE LOG LINE IS THE EVIDENCE. It says how many creatures the renderer is being
 * handed cloth data for. A run that shows none is not reproducing anything,
 * whatever else it shows.
 */
namespace CataclysmClothStress
{
	/** The nearest and furthest a creature is put from the centre, in cm. */
	constexpr float InnerRingCm = 350.0f;
	constexpr float OuterRingCm = 900.0f;

	/** What a count of the stress's living creatures found. */
	struct FClothCount
	{
		/** Living creatures counted. A dead one is not. */
		int32 Creatures = 0;

		/** How many wear a model that carries cloth at all. */
		int32 WearingClothModels = 0;

		/**
		 * How many have cloth switched on: the component allows clothing actors
		 * and does not disable the simulation. Either switch alone is enough to
		 * keep cloth data away from the renderer.
		 */
		int32 ClothSwitchedOn = 0;

		/**
		 * How many the renderer is being handed simulated cloth for, asked
		 * through USkeletalMeshComponent::GetUpdateClothSimulationData_AnyThread,
		 * which is the call the renderer's own copy is filled from.
		 *
		 * NOT GetCurrentClothingData_GameThread, which answers an empty map unless
		 * the component waits for its cloth task, and switches that wait on as a
		 * side effect of being asked. A count built on it could not tell a
		 * creature with cloth from one without, and would change the creature it
		 * was counting.
		 */
		int32 CarryingClothData = 0;
	};

	/** The three creatures whose models carry cloth, in the order a batch takes them. */
	CATACLYSM_API TArray<TSubclassOf<ACataclysmEnemyCharacter>> CreatureClasses();

	/**
	 * Spawn `Count` creatures in a ring around `Centre`, with no brain.
	 *
	 * @param Seed  decides where on the ring each one stands, so the same seed
	 *              puts the same creatures in the same places.
	 * @return the creatures that spawned, in the order they were asked for.
	 */
	CATACLYSM_API TArray<ACataclysmEnemyCharacter*> SpawnBatch(
		UWorld* World, const FVector& Centre, int32 Count, int32 Seed);

	/**
	 * Kill every living creature in `Creatures` through its own death path, then
	 * empty the array. One already dead, or already gone, is not counted.
	 *
	 * @return how many were killed.
	 */
	CATACLYSM_API int32 KillAll(
		TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>>& Creatures);

	/** Count what the living creatures in `Creatures` carry. */
	CATACLYSM_API FClothCount CountCloth(
		const TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>>& Creatures);
}
