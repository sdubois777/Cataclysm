// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveVersion.h"

#include "Save/CataclysmSaveRecord.h"

bool FCataclysmSaveVersion::ReadVersion(const TSharedRef<FJsonObject>& Record, int32& OutVersion)
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
		// A FRACTION IS NOT A VERSION. 2.5 is neither version 2 nor version 3, so it is refused rather than rounded.
		if (AsNumber != FMath::TruncToDouble(AsNumber))
		{
			return false;
		}
		OutVersion = static_cast<int32>(AsNumber);
		return true;
	}

	FString AsString;
	if (Field->TryGetString(AsString) && AsString.IsNumeric() && !AsString.Contains(TEXT(".")))
	{
		OutVersion = FCString::Atoi(*AsString);
		return true;
	}

	return false;
}

ECataclysmSaveVersionResult FCataclysmSaveVersion::Check(int32 CurrentVersion, const TSharedRef<FJsonObject>& Record,
														 FString& OutMessage)
{
	OutMessage.Reset();

	int32 Version = 0;
	if (!ReadVersion(Record, Version))
	{
		OutMessage = FString::Printf(
			TEXT("the record has no whole-number '%s' field, so there is no way to know what shape it is"),
			UCataclysmSaveRecord::SchemaVersionField);
		return ECataclysmSaveVersionResult::NoVersionField;
	}

	if (Version < UCataclysmSaveRecord::FirstSchemaVersion)
	{
		OutMessage = FString::Printf(TEXT("the record says it is version %d; the first real version is %d"),
									 Version, UCataclysmSaveRecord::FirstSchemaVersion);
		return ECataclysmSaveVersionResult::VersionIsNotReal;
	}

	if (Version > CurrentVersion)
	{
		OutMessage = FString::Printf(
			TEXT("the record is version %d and this build reads version %d. It was written by a newer build, and is "
				 "refused rather than read with whatever that build added left out"),
			Version, CurrentVersion);
		return ECataclysmSaveVersionResult::NewerThanThisBuild;
	}

	if (Version < CurrentVersion)
	{
		OutMessage = FString::Printf(
			TEXT("the record is version %d and this build reads version %d. It was written by an older build; saves "
				 "from before version %d are not kept, and the file is left as it is"),
			Version, CurrentVersion, CurrentVersion);
		return ECataclysmSaveVersionResult::OlderThanThisBuild;
	}

	return ECataclysmSaveVersionResult::Current;
}

const TCHAR* FCataclysmSaveVersion::Describe(ECataclysmSaveVersionResult Result)
{
	switch (Result)
	{
		case ECataclysmSaveVersionResult::Current:            return TEXT("at the current version");
		case ECataclysmSaveVersionResult::NoVersionField:     return TEXT("no schema version field");
		case ECataclysmSaveVersionResult::VersionIsNotReal:   return TEXT("the schema version is below the first real one");
		case ECataclysmSaveVersionResult::NewerThanThisBuild: return TEXT("written by a newer build and refused");
		case ECataclysmSaveVersionResult::OlderThanThisBuild: return TEXT("written by an older build and refused");
	}
	return TEXT("an unnamed result");
}
