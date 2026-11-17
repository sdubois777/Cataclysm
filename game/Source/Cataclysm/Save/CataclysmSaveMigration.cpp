// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveMigration.h"

#include "Save/CataclysmSaveRecord.h"

bool FCataclysmSaveMigration::ReadVersion(const TSharedRef<FJsonObject>& Record, int32& OutVersion)
{
	OutVersion = 0;

	const TSharedPtr<FJsonValue> Field = Record->TryGetField(UCataclysmSaveRecord::SchemaVersionField);
	if (!Field.IsValid() || Field->IsNull())
	{
		return false;
	}

	double AsNumber = 0.0;
	if (Field->TryGetNumber(AsNumber))
	{
		// A VERSION IS A WHOLE NUMBER. 1.5 is not a version, and truncating it
		// would silently migrate a record from a version that never existed.
		if (AsNumber != FMath::TruncToDouble(AsNumber))
		{
			return false;
		}
		OutVersion = static_cast<int32>(AsNumber);
		return true;
	}

	// A STRING IS ACCEPTED TOO. A file edited by hand is the ordinary way an old
	// record reaches a new build, and a person editing JSON writes
	// `"SchemaVersion": "2"` often enough that refusing it would be a refusal
	// about punctuation rather than about the record.
	FString AsString;
	if (Field->TryGetString(AsString) && AsString.IsNumeric() && !AsString.Contains(TEXT(".")))
	{
		OutVersion = FCString::Atoi(*AsString);
		return true;
	}

	return false;
}

ECataclysmSaveMigrationResult FCataclysmSaveMigration::Migrate(
	TArrayView<const FCataclysmSaveMigrationStep> Steps,
	int32 TargetVersion,
	const TSharedRef<FJsonObject>& Record,
	FString& OutMessage)
{
	OutMessage.Reset();

	int32 Version = 0;
	if (!ReadVersion(Record, Version))
	{
		OutMessage = FString::Printf(
			TEXT("the record has no whole-number '%s' field, so there is no way to know what shape it is"),
			UCataclysmSaveRecord::SchemaVersionField);
		return ECataclysmSaveMigrationResult::NoVersionField;
	}

	if (Version < UCataclysmSaveRecord::FirstSchemaVersion)
	{
		OutMessage = FString::Printf(
			TEXT("the record says it is version %d; the first real version is %d"),
			Version, UCataclysmSaveRecord::FirstSchemaVersion);
		return ECataclysmSaveMigrationResult::VersionIsNotReal;
	}

	if (Version > TargetVersion)
	{
		// REFUSED RATHER THAN GUESSED AT. Section 5, rule 5.
		OutMessage = FString::Printf(
			TEXT("the record is version %d and this build understands version %d. ")
			TEXT("It was written by a newer build, and is refused rather than loaded with ")
			TEXT("whatever that build added left out"),
			Version, TargetVersion);
		return ECataclysmSaveMigrationResult::NewerThanThisBuild;
	}

	if (Version == TargetVersion)
	{
		return ECataclysmSaveMigrationResult::AlreadyCurrent;
	}

	while (Version < TargetVersion)
	{
		const FCataclysmSaveMigrationStep* Step = Steps.FindByPredicate(
			[Version](const FCataclysmSaveMigrationStep& Candidate)
			{
				return Candidate.FromVersion == Version && Candidate.Apply != nullptr;
			});

		if (Step == nullptr)
		{
			OutMessage = FString::Printf(
				TEXT("no migration step leads out of version %d, so a version %d record ")
				TEXT("cannot be brought up to version %d"),
				Version, Version, TargetVersion);
			return ECataclysmSaveMigrationResult::NoStepForVersion;
		}

		FString StepError;
		if (!Step->Apply(Record, StepError))
		{
			OutMessage = FString::Printf(TEXT("%s failed: %s"),
				Step->Name != nullptr ? Step->Name : TEXT("an unnamed step"),
				*StepError);
			return ECataclysmSaveMigrationResult::StepFailed;
		}

		// THE CHAIN WRITES THE VERSION, NOT THE STEP. A step that forgot would
		// otherwise leave the record claiming to be older than it is, and the
		// next load would run the same step over an already-migrated record.
		++Version;
		Record->SetNumberField(UCataclysmSaveRecord::SchemaVersionField, Version);
	}

	return ECataclysmSaveMigrationResult::Migrated;
}

bool FCataclysmSaveMigration::StepsCoverEveryVersion(
	TArrayView<const FCataclysmSaveMigrationStep> Steps,
	int32 TargetVersion,
	FString& OutReason)
{
	OutReason.Reset();

	if (TargetVersion < UCataclysmSaveRecord::FirstSchemaVersion)
	{
		OutReason = FString::Printf(TEXT("the target version is %d, below the first real version %d"),
			TargetVersion, UCataclysmSaveRecord::FirstSchemaVersion);
		return false;
	}

	// THE MISSING VERSION IS NAMED BEFORE ANY COUNT IS MENTIONED. A chain with
	// a gap in it fails both this and the count below, and "version 2 has no
	// step leading out of it" is the sentence somebody can act on. A count that
	// is one short says only that something is missing.
	for (int32 Version = UCataclysmSaveRecord::FirstSchemaVersion; Version < TargetVersion; ++Version)
	{
		int32 Found = 0;
		for (const FCataclysmSaveMigrationStep& Step : Steps)
		{
			if (Step.FromVersion == Version)
			{
				++Found;
			}
		}

		if (Found != 1)
		{
			OutReason = FString::Printf(
				TEXT("version %d has %d step(s) leading out of it, and it needs exactly one"),
				Version, Found);
			return false;
		}
	}

	// AND NO STEP LEADS OUT OF A VERSION THE CHAIN DOES NOT REACH. The scan
	// above walks the versions that matter, so it cannot see a step registered
	// against version 7 in a chain whose target is 4. That step would never
	// run, and a step that never runs is a version bump somebody started and
	// did not finish.
	const int32 Expected = TargetVersion - UCataclysmSaveRecord::FirstSchemaVersion;
	if (Steps.Num() != Expected)
	{
		OutReason = FString::Printf(
			TEXT("version %d needs %d step(s) to reach it from version %d, and the chain has %d"),
			TargetVersion, Expected, UCataclysmSaveRecord::FirstSchemaVersion, Steps.Num());
		return false;
	}

	for (const FCataclysmSaveMigrationStep& Step : Steps)
	{
		if (Step.Apply == nullptr)
		{
			OutReason = FString::Printf(TEXT("the step out of version %d has no function to run"),
				Step.FromVersion);
			return false;
		}

		if (Step.Name == nullptr || FCString::Strlen(Step.Name) == 0)
		{
			OutReason = FString::Printf(TEXT("the step out of version %d has no name"),
				Step.FromVersion);
			return false;
		}
	}

	return true;
}

const TCHAR* FCataclysmSaveMigration::Describe(ECataclysmSaveMigrationResult Result)
{
	switch (Result)
	{
		case ECataclysmSaveMigrationResult::AlreadyCurrent:     return TEXT("already at the current version");
		case ECataclysmSaveMigrationResult::Migrated:           return TEXT("migrated to the current version");
		case ECataclysmSaveMigrationResult::NoVersionField:     return TEXT("no schema version field");
		case ECataclysmSaveMigrationResult::VersionIsNotReal:   return TEXT("the schema version is below the first real one");
		case ECataclysmSaveMigrationResult::NewerThanThisBuild: return TEXT("written by a newer build and refused");
		case ECataclysmSaveMigrationResult::NoStepForVersion:   return TEXT("a gap in the migration chain");
		case ECataclysmSaveMigrationResult::StepFailed:         return TEXT("a migration step failed");
	}

	// A RESULT ADDED WITHOUT A WORDING. Named rather than fallen through to one
	// of the others, so a message that says this is a missing case and not a
	// misdescribed record.
	return TEXT("an unnamed result");
}
