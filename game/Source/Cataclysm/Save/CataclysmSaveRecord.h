// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Save/CataclysmSaveMigration.h"
#include "CataclysmSaveRecord.generated.h"

/**
 * What every saved record has in common: a name for its kind, the schema version
 * the file on disk was written by, and the chain that brings an older one up to
 * date.
 *
 * THREE RECORDS AND NOT ONE FILE. `docs/Save_System_Design.md` section 1: the
 * account, the character and the run have three different lifetimes and, in
 * co-operative play, three different owners. An account record is permanent, a
 * character record survives a failed run, and a run record is discarded when the
 * run ends and is shared by the whole party. Putting them in one file is what
 * makes co-operative play impossible to retrofit.
 *
 * THE VERSION IS PER RECORD TYPE. Section 5, rule 2. Bumping the character
 * record's version does not touch the account record's, so a change to one does
 * not force a migration of the other. That is why the number is answered by each
 * subclass rather than held once here, and why the chain that reaches it is
 * answered right beside it.
 *
 * `SchemaVersion` IS THE VERSION THE FILE WAS WRITTEN BY, not the version this
 * build understands. They are the same only after a record has been loaded and
 * migrated. `CurrentSchemaVersion()` is what this build understands.
 */
UCLASS(Abstract)
class CATACLYSM_API UCataclysmSaveRecord : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * The schema version this record was written by.
	 *
	 * WRITTEN FIRST IN THE FILE, which section 5 rule 1 requires "so it can be
	 * read without parsing the rest, which is what lets a migration run before
	 * the record is interpreted". A JSON object has no inherent order, so being
	 * first is something `FCataclysmSaveStorage::ToJson` arranges rather than
	 * something the language gives; there is a test that reads the produced text
	 * and checks the first key really is this one.
	 *
	 * ZERO MEANS NOTHING WROTE IT. Every real record is version 1 or higher, so
	 * a record holding zero came from a file with no version field or from an
	 * object nobody filled in, and both are refused rather than treated as
	 * version 1.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Save")
	int32 SchemaVersion = 0;

	/**
	 * Which slot this record was last read from or written to, when that is
	 * known.
	 *
	 * NOT PERSISTED, ON PURPOSE, and it is the one field here that is not. A
	 * record carries its own contents; where it happened to be stored is not one
	 * of them, and writing it into the file would mean a copied file disagreeing
	 * with the slot it sits in.
	 *
	 * IT IS ALSO WHAT PROVES THE `SaveGame` FLAG IS DOING SOMETHING.
	 * `Cataclysm.SaveRecords.AFieldWithoutTheSaveGameFlagDoesNotReachTheFile`
	 * fills this in and checks it stays out of the file. Without a field that must
	 * NOT be written, a test cannot tell a converter that honours the flag from
	 * one that writes every property it can see.
	 *
	 * AND THAT IS WHY IT IS NOT MARKED `Transient`, WHICH IS THE SPECIFIER A
	 * READER WOULD EXPECT HERE. It was, and the guard was worthless: breaking
	 * `FCataclysmSaveStorage::ToJson` to convert every property rather than only
	 * the marked ones left the test passing. `UStructToJsonAttributesWithContainer`
	 * in the engine's JsonObjectConverter.cpp begins by adding
	 * `CPF_Deprecated | CPF_Transient` to its skip flags whenever the caller passes
	 * none, so a Transient property is dropped whatever the check flags say. The
	 * field was being kept out by a second, unrelated rule, and the test could not
	 * tell the two apart. Plain, it is kept out by the `SaveGame` rule alone.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Save")
	FString SlotItWasReadFrom;

	/**
	 * Which of the three records this is: Account, Character or Run.
	 *
	 * A NAME AND NOT THE C++ CLASS NAME. It appears in messages and it is how a
	 * record type is spoken about outside the code, so renaming the class must
	 * not change it. Same reasoning as the slot names in
	 * `UCataclysmSavePartition`.
	 */
	virtual FName RecordType() const
		PURE_VIRTUAL(UCataclysmSaveRecord::RecordType, return NAME_None;);

	/** The schema version THIS BUILD writes and understands. */
	virtual int32 CurrentSchemaVersion() const
		PURE_VIRTUAL(UCataclysmSaveRecord::CurrentSchemaVersion, return 0;);

	/**
	 * The single steps that bring an older record of this type up to
	 * `CurrentSchemaVersion`, in any order.
	 *
	 * EMPTY FOR A RECORD TYPE AT VERSION 1, which is all three of them today:
	 * version 1 is the first, so there is nothing to migrate from. The first
	 * bump adds a step here and an example save file for the version it leaves
	 * behind.
	 *
	 * ANSWERED BESIDE THE VERSION NUMBER RATHER THAN FROM A TABLE SOMEWHERE
	 * ELSE. A central registry would be a second copy of a number that already
	 * exists on the class, and the failure it invites -- bumping the version and
	 * forgetting the table -- is silent until somebody loads an old file.
	 */
	virtual TArrayView<const FCataclysmSaveMigrationStep> MigrationSteps() const
	{
		return TArrayView<const FCataclysmSaveMigrationStep>();
	}

	/**
	 * The name of the version field, as it appears in the file.
	 *
	 * ONE COPY, because the storage layer writes it, the migration chain reads
	 * and rewrites it, and the tests look for it. Three spellings of the same
	 * key is a class of bug that only shows up on a file written by an older
	 * build, which is the one case that cannot be tried out by hand.
	 */
	static const TCHAR* SchemaVersionField;

	/** The lowest version any real record can carry. */
	static constexpr int32 FirstSchemaVersion = 1;
};
