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
 *   - **What a player commands, at a floor change.** Issue #1202, ruled
 *     2026-09-30: a player's thralls and summoned creatures go down the stairs
 *     with the player, and `ACataclysmDungeonGameMode::BringFollowersTo` puts
 *     them beside the player on the new floor. A thrall is a taken
 *     `ACataclysmEnemyCharacter`, so the sweep below skips any whose commander
 *     is a player; `ACataclysmMinion` descends from `ACataclysmCharacterBase`
 *     and the sweep never reached one. **Deployed gadgets are the exception**
 *     and are destroyed with the floor: a gadget stays where it was put
 *     rather than following its deployer, and the spot it was put on is gone.
 *     A judgement, not a ruling of the design. All of this only when
 *     `bCarryFollowers` is set; a save restoring a floor clears as before.
 */
UCLASS()
class CATACLYSM_API UCataclysmFloorContents : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Destroy every creature, drop, projectile, ground effect, patch of
	 * terrain, telegraph marker and floor hazard source in the world.
	 *
	 * THE HAZARD SOURCE IS THE ACTOR A FLOOR'S HAZARDS ARE DEALT IN THE NAME
	 * OF. `ACataclysmFloorHazardSource` exists only to carry an
	 * ability system component, because every route in
	 * `UCataclysmSkillEffects` refuses a source that has none.
	 * Its whole life is one floor.
	 *
	 * CREATURES INCLUDE ONES NOBODY IS TRACKING.
	 * `ACataclysmDungeonGameMode::ClearFloorEnemies` walks the array the floor
	 * populator filled, and a Gatekeeper's called Imps were never put in it --
	 * they are spawned straight into the world mid-fight. This sweeps by class,
	 * so it reaches them.
	 *
	 * @param bCarryFollowers  true at a floor change: what a player commands is
	 *                         kept, except deployed gadgets, which are destroyed.
	 *                         False, the default, keeps the behaviour a save's
	 *                         restore has always had.
	 * @return how many actors were destroyed
	 */
	static int32 ClearTheFloor(UWorld& World, bool bCarryFollowers = false);
};
