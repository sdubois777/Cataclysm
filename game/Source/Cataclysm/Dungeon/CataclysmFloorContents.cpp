// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorContents.h"

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"

namespace
{
	/**
	 * Destroy every actor of one class in a world. Returns how many.
	 *
	 * NAMED FOR THIS FILE. Unreal merges a module's `.cpp` files into one
	 * translation unit, so two files defining the same helper in an anonymous
	 * namespace collide -- and only once both are committed.
	 * `tools/tests/test_no_two_files_share_an_anonymous_helper.py` holds it.
	 */
	template <typename TActor>
	int32 FloorContentsDestroyEvery(UWorld& World)
	{
		TArray<TActor*> Doomed;
		for (TActorIterator<TActor> It(&World); It; ++It)
		{
			if (IsValid(*It))
			{
				Doomed.Add(*It);
			}
		}

		// GATHERED FIRST, THEN DESTROYED. Destroying from inside TActorIterator
		// modifies the level's actor array while it is being walked.
		int32 Destroyed = 0;
		for (TActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
				++Destroyed;
			}
		}
		return Destroyed;
	}
}

int32 UCataclysmFloorContents::ClearTheFloor(UWorld& World)
{
	int32 Destroyed = 0;

	Destroyed += FloorContentsDestroyEvery<ACataclysmEnemyCharacter>(World);
	Destroyed += FloorContentsDestroyEvery<ACataclysmDroppedItem>(World);

	// AND THE THREE THINGS SECTION 6 OF THE SAVE DESIGN SAYS ARE NOT RESTORED. A
	// projectile still flying across a floor that has just been rebuilt, a
	// burning patch of ground under a creature that was never there, or a marker
	// telegraphing a wind-up that is not happening are each worse than a floor
	// that simply starts still. "Every creature resumes from a still moment."
	//
	// THE SAME THREE ARE WRONG ON A NEW FLOOR, for the same reason and without
	// needing a second argument: the player has walked down a flight of stairs
	// and the last floor's fight is over.
	Destroyed += FloorContentsDestroyEvery<ACataclysmProjectile>(World);
	Destroyed += FloorContentsDestroyEvery<ACataclysmGroundZone>(World);
	Destroyed += FloorContentsDestroyEvery<ACataclysmTelegraphMarker>(World);

	return Destroyed;
}
