// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveRecords.h"

// THE NAMES ARE WRITTEN OUT RATHER THAN TAKEN FROM THE CLASS. Same rule as the
// slot names in UCataclysmSavePartition: these are how a record type is spoken
// about outside the code, so renaming a C++ class must not change them.
const FName UCataclysmAccountSave::TypeName = FName(TEXT("Account"));
const FName UCataclysmCharacterSave::TypeName = FName(TEXT("Character"));
const FName UCataclysmRunSave::TypeName = FName(TEXT("Run"));

/**
 * A NAMED NAMESPACE AND NOT AN ANONYMOUS ONE, WHICH IS THE ORDINARY CHOICE.
 *
 * Three rules apply to a migration step and an anonymous namespace cannot
 * satisfy all three at once, because `CataclysmSaveExampleRecord.cpp` already
 * has a `Migrate_1_to_2` and both files are in the Cataclysm module:
 *
 *   1. The registered label reads `Migrate_N_to_N+1`. Section 5 rule 3 of
 *      `docs/Save_System_Design.md`, checked by
 *      `tools/tests/test_save_migrations_are_single_steps.py`.
 *   2. The label matches the function it names, so a failure message points at
 *      the right step. Same test.
 *   3. A helper in an anonymous namespace is unique across its module, because
 *      Unreal merges a module's .cpp files into one translation unit. Checked
 *      by `tools/tests/test_no_two_files_share_an_anonymous_helper.py`.
 *
 * Naming the namespace keeps rules 1 and 2 -- the function really is called
 * `Migrate_1_to_2` -- and answers rule 3's actual concern, which is a symbol
 * collision in the unity blob, by qualifying the symbol instead of hiding it.
 * That collision is not hypothetical: it compiles locally and fails on
 * `development`, because UnrealBuildTool keeps modified files out of the blob.
 */
namespace CataclysmCharacterSaveMigration
{
	/**
	 * A character written before attribute allocation existed has spent nothing.
	 *
	 * THE FIELD IS WRITTEN OUT RATHER THAN LEFT TO DEFAULT. Deserialisation
	 * leaves a missing struct zeroed anyway, so this step could be empty and
	 * still be correct. It is not empty because a migrated record ought to
	 * describe itself completely: somebody reading the file should not have to
	 * know what a C++ default is to know what the character spent.
	 *
	 * IT MUST NOT WRITE THE VERSION FIELD. `FCataclysmSaveMigration::Migrate`
	 * owns that, and a step doing it as well would be two places disagreeing.
	 */
	bool Migrate_1_to_2(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		const TSharedRef<FJsonObject> Nothing = MakeShared<FJsonObject>();
		for (const FString& Name : FCataclysmAttributePoints::Names())
		{
			// CAPITALISED, BECAUSE THAT IS HOW THE STRUCT SPELLS ITS FIELDS and
			// the JSON is written from the struct's own property names.
			// FCataclysmAttributePoints::Names answers the data table's
			// spelling, which is lower case.
			FString Field = Name;
			Field[0] = FChar::ToUpper(Field[0]);
			Nothing->SetNumberField(Field, 0);
		}

		Record->SetObjectField(TEXT("SpentAttributePoints"), Nothing);
		OutError.Empty();
		return true;
	}

	/**
	 * The chain, newest step last.
	 *
	 * The order is for a reader rather than for the machinery, which looks a
	 * step up by the version it leads out of. Writing it in order anyway means a
	 * reader can see a gap.
	 */
	const FCataclysmSaveMigrationStep CharacterSteps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },
	};
}

TArrayView<const FCataclysmSaveMigrationStep>
UCataclysmCharacterSave::MigrationSteps() const
{
	return CataclysmCharacterSaveMigration::CharacterSteps;
}

TArray<TSubclassOf<UCataclysmSaveRecord>> CataclysmSaveRecordClasses()
{
	return {
		UCataclysmAccountSave::StaticClass(),
		UCataclysmCharacterSave::StaticClass(),
		UCataclysmRunSave::StaticClass(),
	};
}
