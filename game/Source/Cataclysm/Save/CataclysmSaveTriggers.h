// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSaveTriggers.generated.h"

/**
 * What made the game decide to write itself to disk.
 *
 * TWO KINDS AND BOTH ARE NEEDED. `docs/Save_System_Design.md` section 6: a clock,
 * "so nothing is ever far from being written", and an immediate write on the
 * events that matter, which "is what makes the gap inside a fight near zero
 * without writing constantly while a player walks around a city".
 *
 * NUMBERED, BECAUSE A TEST NAMES THEM AND A LOG LINE PRINTS THEM. Nothing
 * persists this value, so the numbers are not a compatibility promise the way
 * `ECataclysmLethality`'s are.
 */
UENUM(BlueprintType)
enum class ECataclysmSaveTrigger : uint8
{
	/** The interval elapsed. Nothing in particular happened. */
	Clock = 0,

	/** A creature noticed somebody and started fighting. */
	FightStarted = 1,

	/** A creature died. */
	CreatureDied = 2,

	/** The character's health fell through the threshold. */
	HealthCrossedThreshold = 3,

	/**
	 * The party moved to another floor: `UCataclysmSaveWriter::SetFloor`, which the dungeon raises on the stairs, and,
	 * since issue #2733 slice 3, a walk begun on arriving from the capital. This said nothing raised it until then,
	 * which `SetFloor` had stopped making true.
	 */
	ChangedFloor = 4,

	/** An item or a material entered or left the carried inventory. */
	InventoryChanged = 5,

	/**
	 * The character died.
	 *
	 * THE ONE THAT CANNOT BE RELAXED, and the reason the whole feature exists.
	 * Section 6: for a Hardcore character, death "is written in the same frame
	 * the health reaches zero, through the synchronous write, before the death
	 * is otherwise processed".
	 */
	CharacterDied = 6,

	/**
	 * Something in the account record changed: banked empire upgrade points, the
	 * shared stash, the list of characters.
	 *
	 * A DEFEATED DUNGEON RAISES IT, since issue #52's first slice (2026-11-03):
	 * `ACataclysmDungeonGameMode::ClearEmpireDungeon` banks the defeat's empire
	 * upgrade points into the account record the game instance holds, and the
	 * writer writes that record. Nothing else raises it yet -- there is no stash.
	 * This said nothing raised it until then.
	 */
	AccountChanged = 7,

	/**
	 * A death has been paid for: the dungeon ended, the days charged, and the character stood back up. Issue #753,
	 * the start-up step, ruled 2026-10-30.
	 *
	 * WHY IT EXISTS. The death write above leaves a record holding the character dead, and a load runs the death
	 * again from it, which is right until the death has been paid for. Without this the record would keep the
	 * unpaid death until the next clock write, and a crash in that gap would charge the death twice on load.
	 * `ACataclysmPlayerCharacter::Revive` raises it last.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, OF THE RUN RECORD ONLY. Processing a death changes nothing the character record
	 * holds: the days are the run's, and the health is in the run record's floor placement.
	 */
	DeathProcessed = 8,

	/**
	 * A character made on the front end's new-character screen, its name, partition and creation choice applied when
	 * its level begins (`ACataclysmGameMode::LoadThePendingCharacter`). Issue #753, select-screen step 4a, ruled
	 * 2026-11-01.
	 *
	 * WHY IT EXISTS. Without it the new character's record waited for its first item or its first death, and a player
	 * who quit before either found the character missing from the select screen. AN ORDINARY ASYNCHRONOUS WRITE, of the
	 * character record and the run record.
	 */
	CharacterCreated = 9,

	/**
	 * A run was won: the Cataclysm boss dungeon cleared, the first-kill passive points paid, the character's tier
	 * raised and its next run begun (`UCataclysmGameInstance::BeginTheRunAfterAWin`, which raises it last). Issue
	 * #1324 slice 6e, ruled 2026-11-02.
	 *
	 * WHY IT EXISTS. The coordinating session's comment of 2026-11-01 on #1324: the tier is raised "before the
	 * character record is written". Nothing else a win does raises a trigger -- the clock's write does not write the
	 * character record -- so without this the raised tier, the boss defeated and the new run waited for the next item
	 * or death, and a player who quit first came back to the run that was won, at the old tier. Raised once, after all
	 * of it, so each record is written once and holds the win.
	 *
	 * AN ORDINARY ASYNCHRONOUS WRITE, of the character record and the run record, as `CharacterCreated` is.
	 */
	RunWon = 10,

	/**
	 * The character is leaving this level for another: the capital for a dungeon, or a dungeon for the capital at the
	 * end of a walk (`UCataclysmSaveWriter::HandOver`). Issue #2733 slice 3, ruled by the coordinating session on
	 * 2026-11-17.
	 *
	 * WHY IT EXISTS. A trip between levels is a save and a Continue: the next level loads the character from the two
	 * records this writes, by the identifier the game instance carries across. Whatever these records do not hold, the
	 * trip loses.
	 *
	 * WRITTEN BEFORE THE FRAME ENDS, the second trigger that is, because the next level reads what it wrote: an
	 * asynchronous write could still be in flight when that level's load reads the slot. A loading screen follows it,
	 * so the stall is not seen. Of the character record and the run record, and whatever else wrote the run record
	 * this frame -- the last floor's clear, a win's `RunWon`, a death's `DeathProcessed`.
	 */
	LeavingTheLevel = 11,
};

/**
 * When a save is written, decided, and none of the writing.
 *
 * EVERY JUDGEMENT HERE IS A STATIC OVER PLAIN VALUES, so all of it is testable
 * without a world, a clock or a filesystem -- the same split
 * `UCataclysmEnemyDeath` makes between deciding what dying looks like and
 * playing it. `UCataclysmSaveWriter` is what joins these answers to a world.
 *
 * WHY THE RULES ARE WORTH WRITING DOWN SEPARATELY. Every one of them fails
 * quietly. A death written asynchronously still writes, a moment later, and the
 * moment later is exactly the window a player closing the game would use. An
 * account record written on the clock still holds the right thing, and costs a
 * 600-slot stash serialised every interval for nothing.
 */
UCLASS()
class CATACLYSM_API UCataclysmSaveTriggers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Whether this trigger must be written before the frame ends, rather than
	 * handed to a background thread.
	 *
	 * TRUE FOR EXACTLY TWO TRIGGERS: the character dying, and the hand-over before a level change. Section 6 names
	 * death as the write that cannot be relaxed, and says "Everything else may be written asynchronously" -- may, and
	 * the hand-over is the one that may not: the next level reads it (`LeavingTheLevel`). Any other trigger made
	 * synchronous would be trading frame time for nothing, and making death asynchronous would give back the escape
	 * this feature closes. Exactly one until issue #2733 slice 3, ruled by the coordinating session on 2026-11-17.
	 */
	static bool MustBeWrittenBeforeTheFrameEnds(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the run record.
	 *
	 * TRUE FOR EVERY TRIGGER EXCEPT THE ACCOUNT ONE. The run is the thing that
	 * changes constantly -- the day, the floor, every creature's health -- and
	 * section 6 says it is what the cadence is for.
	 */
	static bool WritesTheRunRecord(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the account record.
	 *
	 * TRUE FOR EXACTLY ONE TRIGGER, and that is the point of the split. Section
	 * 6: "the run record on the cadence above, the account record only when it
	 * actually changes. That split was made in section 1 for a different reason
	 * and it is what makes a frequent save affordable." The account record is
	 * the one carrying a 600-slot stash.
	 */
	static bool WritesTheAccountRecord(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the character record.
	 *
	 * THE INVENTORY AND DEATH, AND NOTHING ELSE -- and since then a new character (`CharacterCreated`, 2026-11-01) and
	 * a won run (`RunWon`, issue #1324 slice 6e, 2026-11-03). What a character is carrying
	 * lives in its own record rather than in the run, so an item picked up has
	 * to reach that record; and a death has to be written whole, which means the
	 * character as well as the floor it fell on.
	 */
	static bool WritesTheCharacterRecord(ECataclysmSaveTrigger Trigger);

	/**
	 * Whether this trigger writes the run record even when another write already wrote it in the same frame.
	 *
	 * THE DEATH AND THE PROCESSING OF ONE. The run record is otherwise written at most once a frame, which is right
	 * when several creatures die together. A death must be written whatever else happened; the processing write must
	 * replace the unpaid death on disk, and a trigger raised on the way out of the dungeon in the same frame would
	 * otherwise stop it. Issue #753, the start-up step.
	 *
	 * AND A WON RUN, since issue #1324 slice 6e (2026-11-03), for the processing write's reason: it replaces the won
	 * run's record with the next run's in the same slot, and a run record written earlier in the frame -- a creature
	 * dying, a floor changing -- would otherwise leave the won run on disk until the next write, for a load to resume.
	 *
	 * AND A NEW CHARACTER, since issue #2733, for the same reason. `ACataclysmGameMode::MakeThePendingCharacter` gives
	 * the character its chosen weapon (`ApplyCreationChoice`) before it begins the run; the weapon it wore goes into the
	 * bag, which raises `InventoryChanged` and writes a run record with no run in it; and `CharacterCreated`, in the same
	 * frame, was skipped, so a Continue before the clock's next write found no empire and began a new run. The two
	 * writes go through `FCataclysmSaveStorage`'s one pipe, which runs them in the order they were started, so the
	 * creation's lands last.
	 *
	 * AND THE HAND-OVER BEFORE A LEVEL CHANGE, since issue #2733 slice 3: the last floor's clear, a win's `RunWon` and
	 * a death's `DeathProcessed` all write the run record in the frame the player leaves, and the next level loads
	 * from what the hand-over writes. Synchronous, it waits for those earlier writes to the slot to finish first, so
	 * it lands last.
	 *
	 * AND A CHANGE OF FLOOR, since issue #2733 slice 3 (ruled 2026-11-18): a write earlier in the same frame gathered
	 * the floor before the change, and the one after it must replace it. Window travel-w1's first run found the walk
	 * an arrival begins skipped so, and the record on disk still saying the capital. Every place that raises it does
	 * so once per change of floor; `docs/DECISIONS.md`'s 2026-11-18 entry lists them.
	 */
	static bool WritesEvenIfTheRunWasWrittenThisFrame(ECataclysmSaveTrigger Trigger);

	/** Whether enough time has passed for the clock to write again. */
	static bool ClockHasElapsed(float SecondsSinceLastWrite, float Interval);

	/**
	 * A share of maximum health, from 0 to 1.
	 *
	 * A MAXIMUM OF ZERO ANSWERS ZERO rather than dividing. A character whose
	 * maximum health has not been set yet is not at full health; it has no
	 * health at all, and the threshold below should not fire on the frame the
	 * attributes arrive.
	 */
	static float HealthFraction(float Health, float MaxHealth);

	/**
	 * Whether health just fell through the threshold.
	 *
	 * FALLING ONLY, AND ONLY ON THE CROSSING. A character sitting below the
	 * threshold does not write again every frame, and one healing back through
	 * it does not write at all. Without the first of those, a wounded character
	 * standing still would write the run record on every tick for as long as it
	 * stood there.
	 */
	static bool HealthCrossedTheThreshold(float Before, float After, float Threshold);

	/** The trigger in words, for a log line or a test failure. */
	static const TCHAR* NameOf(ECataclysmSaveTrigger Trigger);

	/** Every trigger there is, so a test can walk the whole set. */
	static TArray<ECataclysmSaveTrigger> Every();

	/**
	 * How long the clock waits between writes, in seconds.
	 *
	 * A STARTING FIGURE, NOT A DERIVED ONE, and section 7 says so: "The exact
	 * autosave interval, in seconds [...] is a tuning constant that needs a
	 * measured write cost." There is still almost nothing in a record to
	 * measure, so this is chosen rather than measured.
	 *
	 * WHY FIFTEEN. The clock only decides what a player loses when NOTHING
	 * happened, because everything that happens inside a fight raises a trigger
	 * of its own. So it is the amount of walking around a city a player would
	 * have to do again, and fifteen seconds of walking is a small loss against
	 * a write every fifteen seconds while idle. **It needs playing.**
	 */
	static constexpr float SecondsBetweenClockWrites = 15.0f;

	/**
	 * The share of maximum health that forces an immediate write, as a fraction.
	 *
	 * ALSO A STARTING FIGURE. Death is already written synchronously, so what
	 * this catches is the APPROACH to death: the fight going badly, before the
	 * blow that ends it. Half is chosen because it is the point at which a fight
	 * has clearly turned, and because a threshold close to zero would fire so
	 * near the death write as to be the same write twice. **It needs playing.**
	 */
	static constexpr float HealthFractionThatForcesAWrite = 0.5f;
};
