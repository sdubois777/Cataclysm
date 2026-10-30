// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
 *   3. a run saved inside an empire dungeon, or on a floor this level is not, refuses the whole load;
 *   4. the run is put into a new run object, which nothing holds yet;
 *   5. the character is put back, `FCataclysmSaveApply::CharacterInto`, which refuses before changing anything.
 * Then the run becomes the game's, the run's kills and its floor go back, the writer is told the loaded identifiers
 * and handed the loaded records, and the character's placement goes back LAST: a character saved at no health dies
 * there, and the death's own write must reach the loaded slots.
 *
 * A RUN THAT CANNOT BE READ BEGINS A NEW ONE AND SAYS SO: no run named, an empty slot, a file that will not read, or a
 * record `FCataclysmSaveApply::RunInto` refuses. #753's acceptance criteria. A RUN WRITTEN BY A NEWER BUILD IS NOT
 * ONE OF THOSE, ruled 2026-10-30: it refuses the whole load, because a new run would replace the character's
 * `CurrentRunId` at its next write and lose the newer run (`docs/Save_System_Design.md` section 5, rule 5).
 *
 * A RUN SAVED INSIDE AN EMPIRE DUNGEON IS REFUSED, and that is required rather than convenient: the death's days are
 * charged only while the dungeon is entered, and nothing here enters one, so a character who died inside would stand
 * up without paying. Re-entering the dungeon at the saved floor is #753's next step.
 */
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
};
