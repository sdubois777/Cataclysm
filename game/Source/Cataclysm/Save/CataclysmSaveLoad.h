// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/CataclysmSavePartition.h"

class ACataclysmPlayerCharacter;
class UWorld;

/** What a load did. */
enum class ECataclysmLoadOutcome : uint8
{
	/** The character and its run were put back. */
	Loaded,

	/** The character was put back, and a new run was begun because its run could not be read. */
	LoadedWithANewRun,

	/** Nothing was changed. */
	Refused,
};

/** What a load did, and why, in a sentence a person can read. */
struct CATACLYSM_API FCataclysmLoadReport
{
	ECataclysmLoadOutcome Outcome = ECataclysmLoadOutcome::Refused;

	/** Why it was refused, why a new run was begun, or what came back. */
	FString Why;

	/** Saved skill choices `AssignSkill` refused; see `FCataclysmSaveApply::CharacterInto`. */
	int32 SkillsRefused = 0;

	/** Creatures put back on the floor. */
	int32 CreaturesRestored = 0;
};

/**
 * Reading a saved character and its run back into a running game. Issue #753, the start-up step, ruled 2026-10-30.
 *
 * WHAT CHOOSES THE CHARACTER IS A CONSOLE COMMAND FOR NOW, `Cataclysm.LoadCharacter <character id>`, as the ruling
 * of 2026-10-29 says: the test path now, a character select screen as the design (#49).
 *
 * THE ORDER, AND WHY. Everything that can refuse is asked before anything is changed, so a refusal changes nothing:
 *   1. the character record is read; a record that cannot be read, or one written by a newer build, is refused;
 *   2. the run record `CurrentRunId` names is read; one written by a newer build refuses the whole load;
 *   3. a run saved inside an empire dungeon refuses the whole load unless this level's dungeon can enter it again
 *      at its floor; a run saved outside one, on a floor this level is not, refuses it too;
 *   4. the run is put into a new run object, which nothing holds yet;
 *   5. the character is put back, `FCataclysmSaveApply::CharacterInto`, which refuses before changing anything.
 * Then the run becomes the game's, the run's kills go back, a dungeon is entered again at the saved floor
 * (`ACataclysmDungeonGameMode::ResumeEmpireDungeon`), the floor's creatures and drops go back, the writer is told the
 * loaded identifiers and handed the loaded records, and the character's placement goes back LAST and once: a
 * character saved at no health dies there, and the death's own write reaches the loaded slots.
 *
 * A RUN THAT CANNOT BE READ BEGINS A NEW ONE AND SAYS SO: no run named, an empty slot, a file that will not read, or a
 * record `FCataclysmSaveApply::RunInto` refuses. #753's acceptance criteria. A RUN WRITTEN BY A NEWER BUILD IS NOT
 * ONE OF THOSE, ruled 2026-10-30: it refuses the whole load, because a new run would replace the character's
 * `CurrentRunId` at its next write and lose the newer run (`docs/Save_System_Design.md` section 5, rule 5).
 *
 * A RUN SAVED INSIDE AN EMPIRE DUNGEON IS ENTERED AGAIN, since the dungeon step of 2026-10-30, because the death's
 * days are charged only while the dungeon is entered. Where it cannot be (no dungeon in this level, a walk already
 * under way, a dungeon or floor the record does not hold) it is refused whole. THE WALK'S AND THE FLOOR'S RULE STATE
 * IS NOT SAVED, and comes back as a floor starting; #753's next step saves what carries across floors.
 */
/**
 * One saved character, as the character select screen lists it. Issue #753, select-screen step 2, ruled 2026-10-31.
 *
 * AN UNREADABLE SLOT IS LISTED, NOT HIDDEN (ruling 2): `bReadable` false, `Why` saying what reading it said, and only the
 * slot and the identifier its name gives.
 */
struct CATACLYSM_API FCataclysmCharacterSummary
{
	FString SlotName;
	FGuid CharacterId;
	bool bReadable = false;
	FString Why;

	FString Name;
	int32 Level = 0;
	FCataclysmCharacterPartition Partition;

	/** The first worn weapon's type, in the weapon slots' order, else the type chosen at creation, else empty. */
	FString WeaponType;

	/** Whether the record names a run, and whether that run's record could be read for its day and floor. */
	bool bInARun = false;
	bool bRunReadable = false;
	int32 Day = 0;

	/** The dungeon and floor the run was saved on; `Floor` 0 when it was saved outside a dungeon. */
	FName Dungeon;
	int32 Floor = 0;

	/** When the record was last written; `bTimed` false for a record written before the time was saved. */
	bool bTimed = false;
	FDateTime LastWrittenUtc;
};

struct CATACLYSM_API FCataclysmSaveLoad
{
	/**
	 * Load one character, and the run it is in, into `Character` and the world's game.
	 *
	 * NEEDS a world whose save writer is writing, which is what names this level's floor, and a game instance of this
	 * game's class, which is what holds a run. Refused without either.
	 */
	static FCataclysmLoadReport CharacterAndRun(UWorld& World, ACataclysmPlayerCharacter& Character,
												const FGuid& CharacterId);

	/** The outcome in words. */
	static const TCHAR* Describe(ECataclysmLoadOutcome Outcome);

	/**
	 * Every saved character, newest first by its record's last write, then those with no time by name, then by slot.
	 *
	 * FOUND BY SCANNING THE `Character_` SLOTS the save system holds (ruling 1), not from the account records: a Solo
	 * Self-Found character has no account record, and the account record's list is never written. A migration's backups
	 * are left out.
	 *
	 * READS AND WRITES NOTHING BACK. A record older than this build is migrated in memory only, as `FromJson` does, so a
	 * listing never rewrites or backs up a file; a load does that when the player picks one.
	 */
	static TArray<FCataclysmCharacterSummary> SavedCharacters(int32 UserIndex);
};
