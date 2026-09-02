// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmFloorContents.generated.h"

class UWorld;

/**
 * What a dungeon floor holds besides the floor itself, and how to empty it.
 *
 * EVERY FLOOR IS BUILT IN THE SAME PLACE. `ACataclysmDungeonGameMode::BuildFloor`
 * spawns one `ACataclysmDungeonFloor` at the world origin and then reuses that
 * actor for every later floor, so floor 5 occupies the world coordinates floor 1
 * did. Anything left behind is standing inside the new floor rather than
 * somewhere the player has walked away from, which is why leaving it is not an
 * option and moving on is not enough.
 *
 * THIS USED TO LIVE ON `FCataclysmSaveApply`, which is the only thing that ever
 * emptied a floor: loading a save rebuilds one, and section 6 of
 * `docs/Save_System_Design.md` says what must not survive that. Going down a
 * flight of stairs is the same question and had no answer at all -- issue #1176,
 * reported from play as the game slowing down after four or five floors. It sits
 * here rather than there because a save restoring a floor is a reasonable thing
 * for the save system to ask the dungeon about, and the reverse is not.
 *
 * WHAT IT DOES NOT DESTROY, AND THAT IS DELIBERATE:
 *
 *   - **The floor.** `ACataclysmDungeonFloor` is reused rather than replaced.
 *   - **The stairs.** `ACataclysmDungeonStairs` is moved to the new exit.
 *   - **The player.** Nothing here reaches `ACataclysmPlayerCharacter`.
 *   - **A player's minions.** `ACataclysmMinion` descends from
 *     `ACataclysmCharacterBase` and not from `ACataclysmEnemyCharacter`, so the
 *     sweep below does not reach one. Whether a summon should follow its owner
 *     down the stairs, be dismissed, or stand where it was is a design question
 *     nobody has answered; issue #1176 asks it. Leaving them is the behaviour
 *     that was already there, so this changes nothing about them either way.
 */
UCLASS()
class CATACLYSM_API UCataclysmFloorContents : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Destroy every creature, drop, projectile, ground effect and telegraph
	 * marker in the world.
	 *
	 * CREATURES INCLUDE ONES NOBODY IS TRACKING.
	 * `ACataclysmDungeonGameMode::ClearFloorEnemies` walks the array the floor
	 * populator filled, and a Gatekeeper's called Imps were never put in it --
	 * they are spawned straight into the world mid-fight. This sweeps by class,
	 * so it reaches them.
	 *
	 * @return how many actors were destroyed
	 */
	static int32 ClearTheFloor(UWorld& World);
};
