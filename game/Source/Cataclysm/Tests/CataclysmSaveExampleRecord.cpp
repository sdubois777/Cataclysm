// Copyright Stephen Dubois. All Rights Reserved.

#include "Tests/CataclysmSaveExampleRecord.h"

const FName UCataclysmSaveExampleRecord::TypeName = FName(TEXT("Example"));
const TCHAR* UCataclysmSaveExampleRecord::TitleFieldInVersion1 = TEXT("Title");
const TCHAR* UCataclysmSaveExampleRecord::TotalCopperFieldBeforeVersion3 = TEXT("TotalCopper");

namespace
{
	/**
	 * Version 1 to version 2: `Title` was renamed to `Label`.
	 *
	 * A MISSING FIELD IS NOT A FAILURE. A version 1 file that never had a title
	 * migrates to a version 2 file that has no label, and the record reads it as
	 * an empty string. Refusing would turn an optional field into a required one
	 * at the moment nobody can go back and add it.
	 */
	bool Migrate_1_to_2(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		const TCHAR* Old = UCataclysmSaveExampleRecord::TitleFieldInVersion1;
		const TSharedPtr<FJsonValue> Title = Record->TryGetField(Old);
		if (!Title.IsValid() || Title->IsNull())
		{
			return true;
		}

		FString AsString;
		if (!Title->TryGetString(AsString))
		{
			OutError = FString::Printf(TEXT("'%s' is not text"), Old);
			return false;
		}

		Record->SetStringField(TEXT("Label"), AsString);
		Record->RemoveField(Old);
		return true;
	}

	/**
	 * Version 2 to version 3: one `TotalCopper` became `Gold` and `Copper`.
	 *
	 * THE RATE IS FROZEN INTO THE STEP AND NOT READ FROM ANYWHERE. Section 5,
	 * rule 4: a migration transforms one schema into the next using only what is
	 * in the record and constants frozen into the step. If a hundred copper to
	 * the gold ever changes, THIS step still has to use a hundred, because that
	 * is what the version 2 files it reads were written with.
	 */
	bool Migrate_2_to_3(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		const TCHAR* Old = UCataclysmSaveExampleRecord::TotalCopperFieldBeforeVersion3;
		const TSharedPtr<FJsonValue> Total = Record->TryGetField(Old);
		if (!Total.IsValid() || Total->IsNull())
		{
			return true;
		}

		double AsNumber = 0.0;
		if (!Total->TryGetNumber(AsNumber) || AsNumber != FMath::TruncToDouble(AsNumber))
		{
			OutError = FString::Printf(TEXT("'%s' is not a whole number"), Old);
			return false;
		}

		const int32 TotalCopper = static_cast<int32>(AsNumber);
		const int32 Rate = UCataclysmSaveExampleRecord::CopperPerGold;

		Record->SetNumberField(TEXT("Gold"), TotalCopper / Rate);
		Record->SetNumberField(TEXT("Copper"), TotalCopper % Rate);
		Record->RemoveField(Old);
		return true;
	}

	/**
	 * The chain, newest step last.
	 *
	 * THE ORDER HERE IS FOR A READER AND NOT FOR THE MACHINERY:
	 * `FCataclysmSaveMigration::Migrate` looks a step up by the version it leads
	 * out of, so a chain listed backwards would still run forwards. Writing it in
	 * order anyway means a reader can see a gap.
	 */
	const FCataclysmSaveMigrationStep ExampleSteps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },
		{ 2, TEXT("Migrate_2_to_3"), &Migrate_2_to_3 },
	};
}

TArrayView<const FCataclysmSaveMigrationStep> UCataclysmSaveExampleRecord::MigrationSteps() const
{
	return ExampleSteps;
}
