// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/CataclysmSaveRecord.h"
#include "CataclysmSaveExampleRecord.generated.h"

/**
 * A record that exists only to exercise the migration chain. It holds no game
 * state and nothing in the game reads or writes it.
 *
 * WHY IT EXISTS AT ALL. The account, character and run records are all at schema
 * version 1, and version 1 is the first, so not one of them has a migration to
 * run. A chain with nothing in it proves nothing about a chain. The alternative
 * would be to invent a fake version bump on a real record, which would mean
 * committing an example save file for a version of the character record that
 * never existed, and every future reader having to work out that it is a lie.
 * This is a separate record whose only purpose is to be old.
 *
 * IT IS COMPILED INTO THE GAME RATHER THAN GUARDED BEHIND
 * `WITH_AUTOMATION_TESTS`, because Unreal Header Tool generates the reflection
 * for a `UCLASS` whether or not the surrounding code is compiled, and a class
 * whose reflection exists while its constructor does not fails to link. It costs
 * three fields' worth of a shipped binary and it holds nothing.
 *
 * THE TWO STEPS ARE THE TWO KINDS OF CHANGE `docs/Save_System_Design.md` section
 * 5 names as needing a version bump:
 *
 *   version 1 -> 2   RENAMING A FIELD. `Title` became `Label`.
 *   version 2 -> 3   SPLITTING ONE FIELD INTO TWO. `TotalCopper` became `Gold`
 *                    and `Copper`, at a hundred copper to the gold.
 *
 * Adding a field with a sensible default is deliberately NOT one of them, and
 * that is why there is no third step: the design says a missing field already
 * reads as its default, so that case needs no migration and would be a step that
 * did nothing.
 *
 * ITS EXAMPLE SAVE FILES ARE `game/Tests/SaveFixtures/Example_v1.json`,
 * `Example_v2.json` and `Example_v3.json`. They are committed, they were not
 * written by this build, and they are what
 * `Cataclysm.SaveMigration.*` loads. Section 5: "a test that writes a save with
 * the current code and reads it back proves only that the code agrees with
 * itself".
 */
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmSaveExampleRecord : public UCataclysmSaveRecord
{
	GENERATED_BODY()

public:
	static const FName TypeName;
	static constexpr int32 SchemaVersionNow = 3;

	virtual FName RecordType() const override { return TypeName; }
	virtual int32 CurrentSchemaVersion() const override { return SchemaVersionNow; }
	virtual TArrayView<const FCataclysmSaveMigrationStep> MigrationSteps() const override;

	/** Called `Title` in a version 1 file. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FString Label;

	/** Together with `Copper`, this was one field called `TotalCopper` in a
	 *  version 1 or 2 file. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Gold = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Copper = 0;

	/** How many copper make one gold. Frozen into the migration step as well,
	 *  because a step must not depend on a number that can change under it. */
	static constexpr int32 CopperPerGold = 100;

	//~ The field names as the older versions spelled them. Public so a test can
	//~ check a fixture without a second copy of the spelling.

	/** What `Label` was called in a version 1 file. */
	static const TCHAR* TitleFieldInVersion1;

	/** What `Gold` and `Copper` were one field called, in versions 1 and 2. */
	static const TCHAR* TotalCopperFieldBeforeVersion3;
};
