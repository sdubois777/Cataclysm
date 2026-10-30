// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/CataclysmSaveRecords.h"
#include "Templates/SubclassOf.h"

class ACataclysmDroppedItem;
class ACataclysmEnemyCharacter;
class ACataclysmPlayerCharacter;
class UWorld;
class UCataclysmEmpireRun;

/** What putting a floor back actually did. */
struct CATACLYSM_API FCataclysmFloorRestored
{
	/** How many actors were destroyed before anything was put back. */
	int32 Removed = 0;

	int32 Creatures = 0;
	int32 GroundItems = 0;
	int32 Characters = 0;

	/**
	 * Entries in the record that could not be put back.
	 *
	 * ZERO IS THE ONLY GOOD ANSWER and it is reported rather than logged,
	 * because the cases that produce it are all silent: a creature whose
	 * archetype no longer names a class, a drop the world refused to spawn, a
	 * placement for a character that is not in this world.
	 */
	int32 Refused = 0;

	bool IsEverything(const FCataclysmSavedFloor& Record) const
	{
		return Refused == 0
			&& Creatures == Record.Creatures.Num()
			&& GroundItems == Record.GroundItems.Num();
	}
};

/**
 * Putting a saved floor back into a running game.
 *
 * A NEUTRAL RESTART WITH THE DAMAGE KEPT. That is the project owner's decision
 * of 2026-08-20, and `docs/Save_System_Design.md` section 6 is the table of it.
 * Every creature that was alive comes back with the health it had -- a boss
 * keeps every point taken off it -- standing where it stood, with its rarity and
 * its modifiers. Nothing comes back mid-blow.
 *
 * SO THE FLOOR IS CLEARED FIRST, AND THAT INCLUDES MORE THAN THE CREATURES.
 * Projectiles already in flight, ground effects, and the markers that telegraph
 * a wind-up are all destroyed rather than left, because section 6's right-hand
 * column says none of them is restored -- and a projectile left flying across a
 * floor that has just been rebuilt is worse than one that never existed. **This
 * destroys actors. It is not safe to call on a world you did not mean to
 * rebuild.**
 *
 * WHAT IT DOES NOT TOUCH: minions. A summoned minion belongs to the character
 * that summoned it rather than to the floor, and the record has no place for
 * one. Nothing in the design says what happens to a minion across a save, so
 * nothing here decides it.
 */
class CATACLYSM_API FCataclysmSaveApply
{
public:
	/**
	 * Which actor class a saved archetype name spawns.
	 *
	 * READ OFF THE CLASSES THEMSELVES RATHER THAN FROM A TABLE. Every creature
	 * class already declares its own `ArchetypeRow` in its constructor, so a map
	 * built by asking each class default object what it is cannot drift from the
	 * classes it maps -- which a hand-kept table beside them would.
	 *
	 * @return null when no class claims that name. **That is a real outcome and
	 *         not an impossible one**: a creature removed from the game leaves
	 *         its name in every save file that ever held one, and the honest
	 *         answer is to leave it out of the restored floor rather than to
	 *         substitute a different creature.
	 */
	static TSubclassOf<ACataclysmEnemyCharacter> ClassForArchetype(FName ArchetypeRow);

	/** Every archetype name a class currently claims, for a test to walk. */
	static TArray<FName> KnownArchetypes();

	/**
	 * Put one creature back.
	 *
	 * @return the creature, or null when its archetype names no class, when the
	 *         world refuses the spawn, or when the record says it had no health
	 *         left. **A creature at zero health is refused rather than spawned
	 *         and killed**: the record should never hold one, because
	 *         `FCataclysmSaveGather::FloorFrom` skips the dead, so one arriving
	 *         here came from a file somebody edited.
	 */
	static ACataclysmEnemyCharacter* CreatureInto(UWorld& World,
												  const FCataclysmSavedCreature& Saved);

	/** Put one drop back, gear or crafting materials. */
	static ACataclysmDroppedItem* GroundItemInto(UWorld& World,
												 const FCataclysmSavedGroundItem& Saved);

	/**
	 * Move a character to where it was and give it back the health, mana and
	 * energy shield it had.
	 *
	 * @return whether anything was applied
	 */
	static bool PlacementInto(ACataclysmPlayerCharacter& Character,
							  const FCataclysmSavedCharacterPlacement& Saved);

	/**
	 * Clear the floor and rebuild it from the record.
	 *
	 * A RECORD WITH NO FLOOR IN IT CLEARS AND PUTS NOTHING BACK, which is what
	 * a party standing in a city looks like. `FCataclysmSavedFloor::IsOccupied`
	 * is what tells the two apart.
	 */
	static FCataclysmFloorRestored FloorInto(UWorld& World,
											 const FCataclysmSavedFloor& Saved);

	/** Write health, mana and energy shield onto a character's attributes. */
	static bool VitalsInto(AActor& Actor, float Health, float Mana, float EnergyShield);

	/**
	 * Write what health and energy shield are at full.
	 *
	 * MUST HAPPEN BEFORE `VitalsInto`. The vital attribute set clamps health to
	 * the maximum, so writing the current figure first would clamp it down to
	 * whatever the old maximum was. It is the same ordering rule
	 * `ACataclysmEnemyCharacter::ApplyStartingAttributes` states.
	 *
	 * A MAXIMUM OF ZERO IS LEFT ALONE rather than written, so a creature keeps
	 * whatever its class gives it. That is what a record written before the
	 * field existed reads back as.
	 */
	static bool MaximumsInto(AActor& Actor, float MaxHealth, float MaxEnergyShield);

	/**
	 * Puts a saved run back into a run object, so that it advances exactly as
	 * the run it was saved from. Issue #753, its first step, ruled 2026-10-29.
	 *
	 * REFUSED, AND NOTHING CHANGED, when the record does not hold the 25 cities
	 * at their own ids, or when its dungeons and their timers disagree --
	 * `UCataclysmEmpireRun::DungeonsAgreeWithTimers`, the check the writer makes
	 * before it writes them. `OutWhy` says which.
	 *
	 * THE ORDER: the modifier pool is filled, as
	 * `UCataclysmGameInstance::BeginEmpireRun` fills it; `Begin` builds the map,
	 * the clock and the scheduler, with the record's mode, rung and tier and a
	 * seed of 0; then every saved value is written over what `Begin` made. Each
	 * city's saved fields go onto the rebuilt map, which supplies what is not
	 * saved (names, rings, lanes, the central city). The streams are put back with
	 * `Initialize` from their saved positions, which continues each sequence;
	 * their starting seeds are overwritten, and nothing reads those.
	 *
	 * IT TOUCHES NOTHING OUTSIDE THE RUN. `Begin` creates the run's own map,
	 * clock and scheduler with the run as their owner and sets its fields; it
	 * logs nothing and raises nothing.
	 *
	 * NOT COVERED: choosing which run to load, which `FCataclysmSaveLoad` does,
	 * and the floor, which it puts back with `FloorInto` (#753).
	 */
	static bool RunInto(const UCataclysmRunSave& Record, UCataclysmEmpireRun& Run, FString& OutWhy);

	/**
	 * Puts a saved character back into a possessed player. Issue #753, its character step, ruled 2026-10-29.
	 *
	 * REFUSED, AND NOTHING CHANGED, when the character has no player state, inventory or equipment, when the record
	 * does not hold the inventory's 48 slots, or when a worn item names a slot that does not exist or one already
	 * named. `OutWhy` says which.
	 *
	 * THE ORDER: the level, the experience and the kills; the creation choice; the spent attribute points; the worn
	 * gear, silently (`UCataclysmEquipmentComponent::RestoreWornGear` says why); the 48 bag slots by position; the
	 * passive allocation, the bosses and the figure an allocation was handed with, after the gear as the 2026-10-09
	 * "order a save loader must keep" entry requires, without computing the figure again; then ONE announcement, which
	 * refreshes the stat line and refills the skill slots from the worn weapons; then each saved skill choice through
	 * `AssignSkill`, which checks it against those skills. `OutSkillsRefused` counts the choices it refused.
	 *
	 * `EquipInto` CHECKS NO LEVEL AND NO ATTRIBUTE, read before this was written: only that the item exists and suits
	 * the slot. So the attribute points need not come before the gear for gear to be accepted; they come first here
	 * because the one announcement must find them in place.
	 *
	 * NOT COVERED: vitals and position, which the run record's floor placement holds and `PlacementInto` puts back;
	 * the name, the partition, residue and the private stash, which have no home on the player; and choosing which
	 * character to load, which `FCataclysmSaveLoad` does (#753).
	 */
	static bool CharacterInto(const UCataclysmCharacterSave& Record, ACataclysmPlayerCharacter& Character,
							  int32& OutSkillsRefused, FString& OutWhy);
};
