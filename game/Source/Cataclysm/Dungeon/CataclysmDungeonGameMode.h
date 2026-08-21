// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/CataclysmGameMode.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "CataclysmDungeonGameMode.generated.h"

class ACataclysmDungeonFloor;

/**
 * Puts the player on a generated dungeon floor when play begins.
 *
 * WHAT IT IS FOR. `FCataclysmFloorGenerator` decides which cells are walkable,
 * `ACataclysmDungeonFloor` turns that into blocks, and until this existed nothing
 * asked either of them to. The whole thing was reachable only from automation
 * tests. This is what makes pressing Play put a person on a dungeon floor.
 *
 * IT DERIVES FROM THE SANDBOX'S GAME MODE and turns one thing off. What it keeps
 * is worth keeping: `ACataclysmGameMode` is what answers the difficulty tier that
 * every armour calculation reads, and what starts the save writer. What it turns
 * off is `bSpawnsSandboxCreatures`, because every sandbox spawner places its
 * creatures at a fixed offset from the world origin -- which is where the
 * sandbox's flat floor is, and is as likely to be inside solid rock on a
 * generated floor as on the ground.
 *
 * WHAT IT DOES NOT DO YET. It places no enemies, and taking the stairs does
 * nothing: `FloorNumber` is a setting rather than something the game advances.
 * Both are the next piece of work.
 */
UCLASS(Config = Game)
class CATACLYSM_API ACataclysmDungeonGameMode : public ACataclysmGameMode
{
	GENERATED_BODY()

public:
	ACataclysmDungeonGameMode();

	virtual void StartPlay() override;

	// ----------------------------------------------------------------------
	// Which floor
	// ----------------------------------------------------------------------

	/**
	 * The dungeon's seed. Every floor of one dungeon shares it.
	 *
	 * A SETTING RATHER THAN SOMETHING CHOSEN, because there is no dungeon object
	 * to take it from -- that is issue #41.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	int32 DungeonSeed = 1;

	/** Which floor of it, counted from 1, the way `FCataclysmSavedFloor` counts. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon", meta = (ClampMin = "1"))
	int32 FloorNumber = 1;

	/** Which layout family carves it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	ECataclysmFloorLayout Layout = ECataclysmFloorLayout::Halls;

	// ----------------------------------------------------------------------
	// Looking at another floor without rebuilding
	// ----------------------------------------------------------------------
	//
	// THE THREE SETTINGS ABOVE CANNOT BE CHANGED WITHOUT A REBUILD. They are
	// `EditDefaultsOnly` on a C++ class, and `L_Dungeon`'s world settings point
	// at that class directly rather than at a Blueprint, so there is nothing in
	// the editor to edit them on. Three console variables answer each of them
	// instead, and each is read once when the floor is built:
	//
	//     Cataclysm.DungeonSeed    0 uses the setting, above 0 is that dungeon,
	//                              -1 rolls a new one every time play begins
	//     Cataclysm.DungeonFloor   0 uses the setting, above 0 is that floor
	//     Cataclysm.DungeonLayout  -1 uses the setting, 0 Halls, 1 Caverns,
	//                              2 Arena
	//
	// ROLLING A SEED DOES NOT MAKE GENERATION RANDOM, and the difference
	// matters. The generator is deterministic and has to stay so: a dungeon must
	// look the same when the player leaves and returns, and a bug in a floor has
	// to be reproducible from its seed. What -1 changes is which seed is handed
	// to it, which is the empire layer's job once that exists.

	/**
	 * The dungeon seed that will actually be used, console variable included.
	 *
	 * PUBLIC AND TAKING ITS OWN ENTROPY, so a test can ask what it will choose
	 * without depending on the clock. A test that rolled a seed twice and
	 * expected two different numbers would be a test that usually passes.
	 *
	 * @param Entropy what to roll from when the console variable asks for a new
	 *                seed. Zero means read the clock, which is what play does.
	 * @return a seed above zero
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 ChooseSeed(int64 Entropy = 0) const;

	/** The floor number that will actually be used, console variable included. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 ChooseFloorNumber() const;

	/** The layout family that will actually be used, console variable included. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	ECataclysmFloorLayout ChooseLayout() const;

	/**
	 * What the save record calls this dungeon.
	 *
	 * A PLACEHOLDER AND SAID TO BE ONE. A dungeon's real name comes from the
	 * empire layer, which does not exist. What matters today is that the record
	 * stops saying "Sandbox" while the player is standing in a dungeon.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Dungeon")
	FName DungeonName = FName(TEXT("Dungeon"));

	// ----------------------------------------------------------------------
	// Building it
	// ----------------------------------------------------------------------

	/**
	 * Generates the floor and builds its geometry, replacing any already there.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, which is the same reason the sandbox's
	 * spawners are public and says the same thing: `StartPlay` wants a player
	 * controller and a pawn, so an automation test cannot call it, and a step
	 * left only inside it is a step nothing covers.
	 *
	 * @return the floor, or null if it could not be built
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	ACataclysmDungeonFloor* BuildFloor();

	/**
	 * Stands a pawn on the floor where the player arrives.
	 *
	 * WHY IT IS NOT A PLAIN `SetActorLocation`. A character is a capsule whose
	 * origin is its middle, so putting that origin on the walking surface leaves
	 * the lower half inside the ground. The pawn is raised by its own half
	 * height, read from the pawn rather than assumed, because the three designed
	 * classes are not all the same size.
	 *
	 * @return whether the pawn was moved
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool PlaceAtEntrance(APawn* Pawn);

	/** The floor being stood on, once `BuildFloor` has run. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<ACataclysmDungeonFloor> CurrentFloor;

	/** The dungeon's name, so the save record does not say "Sandbox". */
	virtual FName RunFloorName() const override { return DungeonName; }

	/** Which floor of it. See `RunFloorName`. */
	virtual int32 RunFloorNumber() const override { return FloorNumber; }
};
