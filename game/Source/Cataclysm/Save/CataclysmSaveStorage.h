// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UCataclysmSaveRecord;

/** What happened when a record was read. */
enum class ECataclysmSaveLoadResult : uint8
{
	/** Read, at the version this build writes. */
	Loaded,

	/** There is nothing in that slot. NOT a failure: it is what a new player has. */
	SlotIsEmpty,

	/** There is something in the slot but it could not be read back. */
	SlotCouldNotBeRead,

	/** What was there is not JSON. */
	NotValidJson,

	/** It is JSON but the top of it is not an object. */
	NotARecord,

	/**
	 * Its version is not this build's: older, newer, missing or not real. `Describe` and the message say which. An
	 * older record is refused and left on disk, the owner's ruling of 2026-11-27 (issue #2928 slice 12).
	 */
	VersionRefusedIt,

	/** It is at this build's version, but the fields would not read back into the record. */
	FieldsWouldNotRead,
};

/**
 * Writing a record to a slot as JSON, and reading one back.
 *
 * WHY JSON AND NOT THE ENGINE'S BINARY ARCHIVE. `docs/Save_System_Design.md`
 * section 4 weighs both against shipped games in the genre -- Last Epoch writes
 * JSON, Grim Dawn writes a custom binary -- and picks JSON, because this game
 * will change constantly through development and a readable file can be
 * inspected, diffed and tested by hand. The cost is size
 * and load time, and the document says to measure before ever switching.
 *
 * WHICH ENGINE CALL, AND WHY NOT THE OBVIOUS ONE.
 * `UGameplayStatics::SaveGameToSlot` takes a `USaveGame` and serialises it with
 * the engine's binary archive, which throws away the readable file the design
 * chose JSON for. `SaveDataToSlot` and `LoadDataFromSlot` take raw bytes, so the
 * JSON is built here and the bytes handed to the engine. That keeps the platform
 * abstraction -- `ISaveGameSystem` underneath, so a packaged console build still
 * works, which section 4 requires -- while the file stays readable.
 *
 * EVERYTHING HERE IS SYNCHRONOUS, and that is right for the one write that
 * cannot be relaxed: section 6 requires death to be written in the same frame
 * health reaches zero. The clock-driven and event-driven writes should not stall
 * the frame, and the route for those is to build the JSON on the game thread and
 * hand the bytes to a background task. NOTHING CALLS ANY OF THIS
 * YET: the triggers, the clock and the death write are the next piece of issue
 * #529 and are not built.
 *
 * SINCE #2494 THAT TASK IS THIS FILE'S OWN, on its own pipe, and not
 * `ISaveGameSystem::SaveGameAsync`, so that the synchronous write can wait for
 * the asynchronous writes to its slot. The sentence above saying nothing calls
 * this was already out of date: `UCataclysmSaveWriter` does.
 *
 * WHERE FILES GO is `ISaveGameSystem`'s business and not this file's. In the
 * editor that is `game/Saved/SaveGames/`; in a packaged build it is the
 * platform's own location. Section 4: do not hard-code a path.
 */
class CATACLYSM_API FCataclysmSaveStorage
{
public:
	/**
	 * Turn a record into the text that goes in the file.
	 *
	 * ONLY FIELDS MARKED `UPROPERTY(SaveGame)` ARE WRITTEN. That is the whole
	 * point of the flag: the set of persisted fields is declared on the field
	 * rather than kept in a separate list that drifts from it. It reaches into
	 * nested structures too, so a struct held by a record persists only the
	 * members that carry the flag themselves.
	 *
	 * THE VERSION IS WRITTEN FIRST, which section 5 rule 1 requires so that it
	 * can be read without interpreting the rest.
	 */
	static bool ToJson(const UCataclysmSaveRecord* Record, FString& OutJson, FString& OutError);

	/**
	 * Read the schema version out of the text, without interpreting the record.
	 *
	 * THIS IS WHAT RULE 1 IS FOR. Whether to refuse a record is decided before
	 * anything knows what shape the rest of the file is.
	 */
	static bool ReadSchemaVersion(const FString& Json, int32& OutVersion);

	/**
	 * Build a record from the text, refusing it whole when its version is not this build's.
	 *
	 * @param Outer       what owns the new object. `GetTransientPackage()` when
	 *                    nothing else does.
	 * @param OutMessage  why, whenever the answer is not Loaded.
	 * @return the record, or null. A null answer with `SlotIsEmpty` is normal.
	 */
	static UCataclysmSaveRecord* FromJson(
		const FString& Json,
		TSubclassOf<UCataclysmSaveRecord> RecordClass,
		UObject* Outer,
		ECataclysmSaveLoadResult& OutResult,
		FString& OutMessage);

	/**
	 * Write a record to a slot. Synchronous.
	 *
	 * THE ROUTE FOR THE DEATH WRITE, which section 6 says must happen in the
	 * same frame health reaches zero. Every other write should use
	 * `WriteToSlotAsync` below.
	 *
	 * IT WAITS FOR THE ASYNCHRONOUS WRITES TO THE SAME SLOT FIRST. Issue #2494:
	 * one still writing holds the file open, and the open here would fail.
	 */
	static bool WriteToSlot(const UCataclysmSaveRecord* Record, const FString& SlotName,
							int32 UserIndex, FString& OutError);

	/**
	 * Write a record to a slot without stalling the frame.
	 *
	 * THE JSON IS BUILT HERE, ON THE CALLING THREAD, AND ONLY THE BYTES GO
	 * AWAY. That is not an optimisation, it is the whole reason this exists:
	 * `UGameplayStatics::AsyncSaveGameToSlot` takes the record object and
	 * serialises it with the engine's binary archive, which would throw away
	 * the readable file section 4 chose JSON for. Building the text first and
	 * handing `ISaveGameSystem::SaveGameAsync` a byte array is the same call
	 * `AsyncSaveGameToSlot` makes once it has finished serialising.
	 *
	 * SINCE #2494 THE BYTES GO TO THIS FILE'S OWN PIPE INSTEAD, whose task does
	 * what `SaveGameAsync` does: the blocking `ISaveGameSystem::SaveGame`, then
	 * `WhenDone` on the core ticker. The engine returns no task to wait on, and
	 * `WriteToSlot` must wait for this slot's writes before it writes.
	 *
	 * IT ALSO MEANS THE RECORD IS READ BEFORE THIS RETURNS, so the caller may
	 * change or discard it immediately. A record handed to a background thread
	 * and read there would be a race against the next frame's gather.
	 *
	 * @param WhenDone  called on the GAME THREAD when the write finishes, with
	 *                  whether it worked. May be unset.
	 * @return whether the write was STARTED. A true answer says the bytes were
	 *         built and handed over, not that they reached the disk.
	 */
	static bool WriteToSlotAsync(const UCataclysmSaveRecord* Record,
								 const FString& SlotName, int32 UserIndex,
								 TFunction<void(bool)> WhenDone, FString& OutError);

	/**
	 * Whether an asynchronous write to this slot has been handed over and has not
	 * yet finished writing. Issue #2494.
	 *
	 * "FINISHED" MEANS THE FILE IS WRITTEN AND CLOSED, not that the `WhenDone`
	 * callback has run: that waits for the core ticker, a frame later.
	 * `WriteToSlot` waits for this to be false before it writes.
	 */
	static bool HasAWriteInFlight(const FString& SlotName);

	/**
	 * Read a record from a slot.
	 *
	 * NOTHING IS WRITTEN. A record of another version is refused and the file is
	 * left exactly as it was; there is no migration, so there is nothing to back up
	 * and nothing to write back (issue #2928 slice 12, 2026-11-27).
	 */
	static UCataclysmSaveRecord* ReadFromSlot(
		const FString& SlotName,
		int32 UserIndex,
		TSubclassOf<UCataclysmSaveRecord> RecordClass,
		UObject* Outer,
		ECataclysmSaveLoadResult& OutResult,
		FString& OutMessage);

	/** The result in words, for a log line or a test failure. */
	static const TCHAR* Describe(ECataclysmSaveLoadResult Result);

	/** Whether a record came back usable. */
	static bool Succeeded(ECataclysmSaveLoadResult Result)
	{
		return Result == ECataclysmSaveLoadResult::Loaded;
	}

	/**
	 * What the backups written before a migration had in the middle of their slot names, `<slot>_BackupOfVersion<N>`.
	 *
	 * NOTHING WRITES ONE ANY MORE: the migration chain and its backups were removed on 2026-11-27 (issue #2928 slice
	 * 12). Kept so that deleting a character still deletes the backups an older build left beside its slots.
	 */
	static const TCHAR* BackupInfix;
};
