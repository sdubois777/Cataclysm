// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * One step of a migration, from one schema version to the next one.
 *
 * A STEP IS ALWAYS EXACTLY ONE VERSION WIDE. `docs/Save_System_Design.md`
 * section 5, rule 3: "Never write a migration that jumps versions; a chain of
 * small steps is testable and a jump is not." `FromVersion` is the version the
 * record is at when the step runs, and it is at `FromVersion + 1` afterwards.
 *
 * IT WORKS ON THE PARSED FILE AND NOT ON THE RECORD OBJECT, and that is forced
 * rather than chosen. A migration exists precisely because the old shape is not
 * the current shape, so the old fields may have no C++ property to be read into.
 * Deserialising first and migrating afterwards would throw away everything the
 * migration was written to rescue.
 *
 * IT MUST NOT READ `game/Data/`. Section 5, rule 4: a migration transforms one
 * schema into the next using only what is in the record and constants frozen
 * into the step itself. A step that reads a data table breaks the moment that
 * table changes, which is the thing most likely to change.
 *
 * `Name` IS FOR THE MESSAGE WHEN A STEP FAILS, and the design's naming is
 * `Migrate_N_to_N+1` -- `Migrate_3_to_4`, `Migrate_4_to_5`.
 * `tools/tests/test_save_migrations_are_single_steps.py` reads the source and
 * fails if a step is named for a jump, or if its name disagrees with the version
 * it is registered against.
 */
struct FCataclysmSaveMigrationStep
{
	/** The version a record is at when this step runs. */
	int32 FromVersion = 0;

	/** The step's own name, `Migrate_N_to_N+1`, for messages. */
	const TCHAR* Name = nullptr;

	/**
	 * Transform the record in place.
	 *
	 * A PLAIN FUNCTION POINTER AND NOT A LAMBDA, so every step is a named
	 * function that can be found by grep, broken by a guard proof and read
	 * beside the version it belongs to.
	 *
	 * The step must NOT write the version field; the chain does that, so a step
	 * that forgets cannot leave a record claiming to be older than it is.
	 */
	bool (*Apply)(const TSharedRef<FJsonObject>& Record, FString& OutError) = nullptr;
};

/** What happened when a record was put through the chain. */
enum class ECataclysmSaveMigrationResult : uint8
{
	/** The record was already at the version this build understands. */
	AlreadyCurrent,

	/** The record was older and every step from its version to the current one ran. */
	Migrated,

	/** There is no version field, or it is not a whole number. */
	NoVersionField,

	/** The version is below the first real one. Nothing wrote it. */
	VersionIsNotReal,

	/**
	 * The record is NEWER than this build understands, and is refused.
	 *
	 * SECTION 5, RULE 5, AND IT IS A RULE RATHER THAN A PREFERENCE: loading a
	 * newer save silently loses whatever the newer build added, and the player
	 * finds out when the field they care about is gone.
	 */
	NewerThanThisBuild,

	/** A version in the middle of the chain has no step leading out of it. */
	NoStepForVersion,

	/** A step ran and reported that it could not do its work. */
	StepFailed,
};

/**
 * Bringing a saved record up to the version this build understands.
 *
 * STATIC FUNCTIONS OVER A PARSED FILE, with no world, no slot and no filesystem,
 * so the whole of it can be tested by passing JSON in. `FCataclysmSaveStorage`
 * is what joins this to a file on disk.
 *
 * NOTHING HERE HOLDS A REGISTRY OF RECORD TYPES. Each record class answers with
 * its own chain, from `UCataclysmSaveRecord::MigrationSteps`, beside the version
 * number that chain has to reach. One place, so the two cannot drift; a central
 * table here would be a second copy of a number that already exists.
 */
class CATACLYSM_API FCataclysmSaveMigration
{
public:
	/**
	 * Apply every step from the record's own version up to `TargetVersion`.
	 *
	 * The record's version field is rewritten as each step completes, so a
	 * record that fails halfway is left saying which version it actually
	 * reached rather than which one it started at.
	 *
	 * @param Steps          the chain for this record type, in any order
	 * @param TargetVersion  the version this build understands
	 * @param Record         the parsed file, transformed in place
	 * @param OutMessage     why, when the answer is not AlreadyCurrent or Migrated
	 */
	static ECataclysmSaveMigrationResult Migrate(
		TArrayView<const FCataclysmSaveMigrationStep> Steps,
		int32 TargetVersion,
		const TSharedRef<FJsonObject>& Record,
		FString& OutMessage);

	/**
	 * Whether the chain has exactly one step leading out of every version from
	 * the first up to one below the target.
	 *
	 * WHY THIS IS CHECKED SEPARATELY FROM RUNNING IT. A gap in the chain only
	 * shows up when somebody loads a file at exactly the missing version, which
	 * for an old version may be years after the gap was introduced. A test asks
	 * this of every record class, so a version bump that forgets its step fails
	 * at once instead.
	 */
	static bool StepsCoverEveryVersion(
		TArrayView<const FCataclysmSaveMigrationStep> Steps,
		int32 TargetVersion,
		FString& OutReason);

	/** Read the schema version out of a parsed record. */
	static bool ReadVersion(const TSharedRef<FJsonObject>& Record, int32& OutVersion);

	/** The result in words, for a log line or a test failure. */
	static const TCHAR* Describe(ECataclysmSaveMigrationResult Result);

	/** Whether the record came out of the chain usable. */
	static bool Succeeded(ECataclysmSaveMigrationResult Result)
	{
		return Result == ECataclysmSaveMigrationResult::AlreadyCurrent
			|| Result == ECataclysmSaveMigrationResult::Migrated;
	}
};
