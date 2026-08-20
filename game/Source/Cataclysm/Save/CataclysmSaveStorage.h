// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UCataclysmSaveRecord;

/** What happened when a record was read. */
enum class ECataclysmSaveLoadResult : uint8
{
	/** Read, and it was already at the version this build understands. */
	Loaded,

	/** Read, and it was brought up to this build's version on the way in. */
	Migrated,

	/** There is nothing in that slot. NOT a failure: it is what a new player has. */
	SlotIsEmpty,

	/** There is something in the slot but it could not be read back. */
	SlotCouldNotBeRead,

	/** What was there is not JSON. */
	NotValidJson,

	/** It is JSON but the top of it is not an object. */
	NotARecord,

	/** The migration chain refused it. `Describe` and the message say why. */
	MigrationRefusedIt,

	/** It migrated, but the fields would not read back into the record. */
	FieldsWouldNotRead,

	/** It was read, but writing the migrated record back to the slot failed. */
	MigratedButCouldNotBeWrittenBack,
};

/**
 * Writing a record to a slot as JSON, and reading one back.
 *
 * WHY JSON AND NOT THE ENGINE'S BINARY ARCHIVE. `docs/Save_System_Design.md`
 * section 4 weighs both against shipped games in the genre -- Last Epoch writes
 * JSON, Grim Dawn writes a custom binary -- and picks JSON, because this game
 * will change constantly through development and a migration written against a
 * readable format can be inspected, diffed and tested by hand. The cost is size
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
 * hand the bytes to `ISaveGameSystem::SaveGameAsync`. NOTHING CALLS ANY OF THIS
 * YET: the triggers, the clock and the death write are the next piece of issue
 * #529 and are not built.
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
	 * THIS IS WHAT RULE 1 IS FOR. Deciding whether to migrate, whether to refuse,
	 * and whether to write a backup all happen before anything knows what shape
	 * the rest of the file is.
	 */
	static bool ReadSchemaVersion(const FString& Json, int32& OutVersion);

	/**
	 * Build a record from the text, migrating it first if it is older.
	 *
	 * @param Outer       what owns the new object. `GetTransientPackage()` when
	 *                    nothing else does.
	 * @param OutMessage  why, whenever the answer is not Loaded or Migrated.
	 * @return the record, or null. A null answer with `SlotIsEmpty` is normal.
	 */
	static UCataclysmSaveRecord* FromJson(
		const FString& Json,
		TSubclassOf<UCataclysmSaveRecord> RecordClass,
		UObject* Outer,
		ECataclysmSaveLoadResult& OutResult,
		FString& OutMessage);

	/** Write a record to a slot. Synchronous. */
	static bool WriteToSlot(const UCataclysmSaveRecord* Record, const FString& SlotName,
							int32 UserIndex, FString& OutError);

	/**
	 * Read a record from a slot, migrating it if it is older than this build.
	 *
	 * THE BACKUP IS WRITTEN BEFORE ANY STEP RUNS, and the original slot is only
	 * replaced once the migrated record has been read back successfully. Section
	 * 5, rule 6. So a migration that goes wrong costs the player nothing: the
	 * file they had is still there under its backup name.
	 */
	static UCataclysmSaveRecord* ReadFromSlot(
		const FString& SlotName,
		int32 UserIndex,
		TSubclassOf<UCataclysmSaveRecord> RecordClass,
		UObject* Outer,
		ECataclysmSaveLoadResult& OutResult,
		FString& OutMessage);

	/**
	 * What the backup written before a migration is called.
	 *
	 * IT NAMES THE VERSION IT HOLDS, so a player who migrates through three
	 * versions keeps three separate backups rather than overwriting the first
	 * one, which is the copy furthest from the current build and the one most
	 * likely to be wanted.
	 */
	static FString BackupSlotName(const FString& SlotName, int32 FromVersion);

	/** The result in words, for a log line or a test failure. */
	static const TCHAR* Describe(ECataclysmSaveLoadResult Result);

	/** Whether a record came back usable. */
	static bool Succeeded(ECataclysmSaveLoadResult Result)
	{
		return Result == ECataclysmSaveLoadResult::Loaded
			|| Result == ECataclysmSaveLoadResult::Migrated;
	}

	/** What every backup slot name has in the middle of it. */
	static const TCHAR* BackupInfix;
};
