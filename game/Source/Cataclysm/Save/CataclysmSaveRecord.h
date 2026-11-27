// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CataclysmSaveRecord.generated.h"

/**
 * What every saved record has in common: a name for its kind and the schema
 * version the file on disk was written by.
 *
 * NO UPGRADE STEPS. A record is read only at the version this build writes
 * (`FCataclysmSaveVersion`); an older one is refused and left on disk. The
 * owner's ruling of 2026-11-27, "Remove; old saves reset", issue #2928 slice 12.
 *
 * THREE RECORDS AND NOT ONE FILE. `docs/Save_System_Design.md` section 1: the
 * account, the character and the run have three different lifetimes and, in
 * co-operative play, three different owners. An account record is permanent, a
 * character record survives a failed run, and a run record is discarded when the
 * run ends and is shared by the whole party. Putting them in one file is what
 * makes co-operative play impossible to retrofit.
 *
 * THE VERSION IS PER RECORD TYPE. Section 5, rule 2. Bumping the character
 * record's version does not touch the account record's, so a change to one
 * refuses only the old files of that one. That is why the number is answered by
 * each subclass rather than held once here.
 *
 * `SchemaVersion` IS THE VERSION THE FILE WAS WRITTEN BY, not the version this
 * build understands. A record that loaded holds the same number as
 * `CurrentSchemaVersion()`, because no other version is read.
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
	 * read without parsing the rest", which is what lets the version check refuse
	 * a record before it is interpreted. A JSON object has no inherent order, so being
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
	 * The name of the version field, as it appears in the file.
	 *
	 * ONE COPY, because the storage layer writes it, the version check reads it,
	 * and the tests look for it. Two spellings of the same key would make every
	 * file look as if it had no version.
	 */
	static const TCHAR* SchemaVersionField;

	/** The lowest version any real record can carry. */
	static constexpr int32 FirstSchemaVersion = 1;
};
