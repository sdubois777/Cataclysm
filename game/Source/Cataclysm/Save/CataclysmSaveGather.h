// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/CataclysmSaveRecords.h"

class ACataclysmDroppedItem;
class ACataclysmEnemyCharacter;
class ACataclysmPlayerCharacter;
class UCataclysmInventoryComponent;
class UWorld;

/**
 * Reading a running game into a save record.
 *
 * THE OTHER HALF IS `FCataclysmSaveApply`, which puts a record back. The two are
 * separate files rather than one because they fail differently: reading a world
 * can be done to any world at any time and cannot break anything, while writing
 * one destroys what is there first.
 *
 * WHAT IT GATHERS IS THE LEFT-HAND COLUMN OF THE TABLE IN SECTION 6 of
 * `docs/Save_System_Design.md`, and nothing else. Which floor, where everybody
 * is standing, the health each creature has, its rarity and its modifiers, and
 * what is lying on the floor. **What a creature was in the middle of doing is
 * deliberately not read**, and section 6 says why: it is the data whose shape
 * changes every patch, and persisting it would cost a migration every patch for
 * state nobody wants preserved.
 *
 * PART OF THE CHARACTER RECORD STILL HAS NO SOURCE. The passive tree and the 18
 * equipped slots do not exist in the running game, so `CharacterFrom` fills the
 * carried inventory, the attribute allocation and the level, and leaves the rest
 * alone. Issues #38 and #42 are what bring the remainder.
 *
 * THE ATTRIBUTE ALLOCATION JOINED THE CARRIED INVENTORY ON 2026-08-24, when a
 * character could first spend a point, and the level and the progress into it
 * joined them the same day. Issue #50 still brings the passive tree, and
 * nothing yet AWARDS experience: the design says an enemy's Enemy Score is what
 * a kill grants, and Enemy Score has no port at all. That is issue #926.
 */
class CATACLYSM_API FCataclysmSaveGather
{
public:
	/**
	 * One creature, as it stands.
	 *
	 * A DEAD CREATURE IS STILL GATHERED IF IT IS PASSED IN, and the caller is
	 * what decides. `FloorFrom` below skips them, because a corpse waiting out
	 * its death animation is not a creature the player has to fight again.
	 */
	static FCataclysmSavedCreature CreatureFrom(const ACataclysmEnemyCharacter& Creature);

	/** Where one character was standing and what it had left. */
	static FCataclysmSavedCharacterPlacement PlacementFrom(
		const ACataclysmPlayerCharacter& Character, const FGuid& CharacterId);

	/** One drop, gear or crafting materials. */
	static FCataclysmSavedGroundItem GroundItemFrom(const ACataclysmDroppedItem& Drop);

	/**
	 * The whole floor: every creature alive, every drop still lying there, and
	 * every character standing on it.
	 *
	 * IT SKIPS THE DEAD, both creatures and characters, and that is the same
	 * decision in two places. A creature killed a moment ago is still an actor
	 * for as long as its death clip runs -- up to 4 seconds, see
	 * `UCataclysmEnemyDeath::LongestCorpseSeconds` -- and writing it into the
	 * record would put it back on its feet when the floor was restored. That is
	 * the exact escape section 6 exists to close, running backwards.
	 *
	 * @param SoloCharacterId  the identifier the one character's placement is
	 *                         recorded against. **SOLO PLAY IS WHAT EXISTS**, and
	 *                         co-operative play needs an identifier per
	 *                         character rather than one for the world; the run
	 *                         record already holds an array of placements for
	 *                         that reason.
	 */
	static FCataclysmSavedFloor FloorFrom(const UWorld& World, FName Dungeon,
										  int32 Floor, const FGuid& SoloCharacterId);

	/** The 48 carried slots, exactly as the component holds them. */
	static void CarriedSlotsFrom(const UCataclysmInventoryComponent& Inventory,
								 TArray<FCataclysmCarriedSlot>& OutSlots);

	/**
	 * Fill in the part of a character record that the running game can answer.
	 *
	 * THAT IS THE CARRIED INVENTORY AND NOTHING ELSE TODAY. Every other field
	 * the design lists is left untouched rather than zeroed, so a record loaded
	 * from disk and then refreshed does not lose what it already held.
	 *
	 * @return whether anything was read at all
	 */
	static bool CharacterFrom(const ACataclysmPlayerCharacter& Character,
							  UCataclysmCharacterSave& Record);

	/**
	 * The player character in a world, or null.
	 *
	 * THE FIRST ONE, because solo play is what exists. It is here rather than
	 * inline in three callers so that the day co-operative play arrives there is
	 * one place that answers "which characters are on this floor".
	 */
	static ACataclysmPlayerCharacter* CharacterIn(const UWorld& World);

	/** Health, mana and energy shield off a character's attributes. */
	static void VitalsOf(const AActor& Actor, float& OutHealth, float& OutMana,
						 float& OutEnergyShield);

	/**
	 * What health and energy shield would be at full.
	 *
	 * READ AS WELL AS THE CURRENT FIGURES, because a creature's maximum is set
	 * by the encounter rather than by its class. See
	 * `FCataclysmSavedCreature::MaxHealth` for the failure that showed it.
	 */
	static void MaximumsOf(const AActor& Actor, float& OutMaxHealth,
						   float& OutMaxEnergyShield);
};
