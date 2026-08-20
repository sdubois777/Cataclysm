// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveStorage.h"

#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecord.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const TCHAR* FCataclysmSaveStorage::BackupInfix = TEXT("_BackupOfVersion");

namespace
{
	/** Parse the text. Null when it is not JSON, or is JSON that is not an object. */
	TSharedPtr<FJsonObject> ParseRecord(const FString& Json)
	{
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Parsed))
		{
			return nullptr;
		}
		return Parsed;
	}

	/** The text as the bytes that go in the file. UTF-8, and no byte order mark. */
	void ToUtf8(const FString& Json, TArray<uint8>& OutBytes)
	{
		const FTCHARToUTF8 Converted(*Json);
		OutBytes.Reset(Converted.Length());
		OutBytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}
}

bool FCataclysmSaveStorage::ToJson(const UCataclysmSaveRecord* Record, FString& OutJson, FString& OutError)
{
	OutJson.Reset();
	OutError.Reset();

	if (Record == nullptr)
	{
		OutError = TEXT("there is no record to write");
		return false;
	}

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();

	// THE VERSION FIELD IS CLAIMED FIRST, BEFORE ANY OTHER FIELD EXISTS, and
	// that is the whole of how section 5 rule 1 is honoured. A JSON object has
	// no inherent order; what it has is the order its keys were added in, which
	// is the order they are written out in. Putting this key in the map before
	// the converter runs is what puts it at the top of the file. The converter
	// then overwrites the VALUE of a key that is already there, which does not
	// move it. `Cataclysm.SaveStorage.TheSchemaVersionIsTheFirstThingInTheFile`
	// reads the produced text and checks that this actually worked, rather than
	// trusting the paragraph you are reading.
	Json->SetNumberField(UCataclysmSaveRecord::SchemaVersionField, Record->SchemaVersion);

	// CPF_SaveGame IS WHAT DECIDES WHAT IS PERSISTED. Section 4: mark persisted
	// fields UPROPERTY(SaveGame), so the set is declared on the field rather than
	// maintained in a list beside it. Passing 0 here would write every property
	// the class has, including whatever is transient.
	//
	// SkipStandardizeCase KEEPS THE JSON KEYS SPELLED THE WAY THE C++ FIELDS ARE.
	// Without it the converter lowercases the first letter, so `SchemaVersion`
	// would be written as `schemaVersion` while the reader looks the property up
	// under its authored name. Reading would still work, because an FString map
	// key compares without regard to case -- but a hand-written fixture, a
	// migration reaching for a key, and the C++ field would then be spelled three
	// ways for no reason.
	if (!FJsonObjectConverter::UStructToJsonAttributes(
			Record->GetClass(), Record, Json, CPF_SaveGame, 0, nullptr,
			EJsonObjectConversionFlags::SkipStandardizeCase))
	{
		OutError = FString::Printf(TEXT("could not convert a %s record to JSON"),
			*Record->RecordType().ToString());
		return false;
	}

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Json, Writer))
	{
		OutError = FString::Printf(TEXT("could not write a %s record out as text"),
			*Record->RecordType().ToString());
		return false;
	}
	Writer->Close();

	return true;
}

bool FCataclysmSaveStorage::ReadSchemaVersion(const FString& Json, int32& OutVersion)
{
	OutVersion = 0;

	const TSharedPtr<FJsonObject> Parsed = ParseRecord(Json);
	if (!Parsed.IsValid())
	{
		return false;
	}

	double AsNumber = 0.0;
	if (Parsed->TryGetNumberField(UCataclysmSaveRecord::SchemaVersionField, AsNumber)
		&& AsNumber == FMath::TruncToDouble(AsNumber))
	{
		OutVersion = static_cast<int32>(AsNumber);
		return true;
	}

	FString AsString;
	if (Parsed->TryGetStringField(UCataclysmSaveRecord::SchemaVersionField, AsString)
		&& AsString.IsNumeric())
	{
		OutVersion = FCString::Atoi(*AsString);
		return true;
	}

	return false;
}

UCataclysmSaveRecord* FCataclysmSaveStorage::FromJson(
	const FString& Json,
	TSubclassOf<UCataclysmSaveRecord> RecordClass,
	UObject* Outer,
	ECataclysmSaveLoadResult& OutResult,
	FString& OutMessage)
{
	OutMessage.Reset();
	OutResult = ECataclysmSaveLoadResult::NotValidJson;

	const UCataclysmSaveRecord* Default = RecordClass.GetDefaultObject();
	if (Default == nullptr)
	{
		OutMessage = TEXT("no record class was given to read into");
		return nullptr;
	}

	const TSharedPtr<FJsonObject> Parsed = ParseRecord(Json);
	if (!Parsed.IsValid())
	{
		OutResult = Json.TrimStartAndEnd().StartsWith(TEXT("{"))
			? ECataclysmSaveLoadResult::NotValidJson
			: ECataclysmSaveLoadResult::NotARecord;
		OutMessage = FString::Printf(TEXT("a %s record could not be parsed as a JSON object"),
			*Default->RecordType().ToString());
		return nullptr;
	}

	const TSharedRef<FJsonObject> Record = Parsed.ToSharedRef();

	// MIGRATE BEFORE INTERPRETING, NOT AFTER. A migration exists because the old
	// shape is not the current shape, so an old field may have no C++ property to
	// be read into. Deserialising first would throw the old field away and leave
	// the migration nothing to work from.
	FString MigrationMessage;
	const ECataclysmSaveMigrationResult Migration = FCataclysmSaveMigration::Migrate(
		Default->MigrationSteps(),
		Default->CurrentSchemaVersion(),
		Record,
		MigrationMessage);

	if (!FCataclysmSaveMigration::Succeeded(Migration))
	{
		OutResult = ECataclysmSaveLoadResult::MigrationRefusedIt;
		OutMessage = FString::Printf(TEXT("a %s record was refused: %s -- %s"),
			*Default->RecordType().ToString(),
			FCataclysmSaveMigration::Describe(Migration),
			*MigrationMessage);
		return nullptr;
	}

	UCataclysmSaveRecord* Loaded = NewObject<UCataclysmSaveRecord>(
		Outer != nullptr ? Outer : GetTransientPackage(), RecordClass);

	FText FailReason;
	if (!FJsonObjectConverter::JsonObjectToUStruct(
			Record, RecordClass, Loaded, CPF_SaveGame, 0, false, &FailReason))
	{
		OutResult = ECataclysmSaveLoadResult::FieldsWouldNotRead;
		OutMessage = FString::Printf(TEXT("a %s record parsed but its fields would not read back: %s"),
			*Default->RecordType().ToString(), *FailReason.ToString());
		return nullptr;
	}

	// THE VERSION IS NOT STAMPED ON AFTERWARDS. It is read out of the record like
	// every other field, so a build where the version field stopped being
	// persisted produces a record saying 0 and the tests notice. Stamping it here
	// would hide exactly that.
	OutResult = Migration == ECataclysmSaveMigrationResult::Migrated
		? ECataclysmSaveLoadResult::Migrated
		: ECataclysmSaveLoadResult::Loaded;
	return Loaded;
}

bool FCataclysmSaveStorage::WriteToSlot(const UCataclysmSaveRecord* Record, const FString& SlotName,
										int32 UserIndex, FString& OutError)
{
	OutError.Reset();

	if (SlotName.IsEmpty())
	{
		// AN EMPTY SLOT NAME IS AN ANSWER `UCataclysmSavePartition` GIVES ON
		// PURPOSE: a Solo Self-Found character has no account record. Writing to
		// it anyway would create one shared by every Solo Self-Found character in
		// the game, which is the opposite of what the mode means.
		OutError = TEXT("there is no slot name to write to");
		return false;
	}

	FString Json;
	if (!ToJson(Record, Json, OutError))
	{
		return false;
	}

	TArray<uint8> Bytes;
	ToUtf8(Json, Bytes);

	if (!UGameplayStatics::SaveDataToSlot(Bytes, SlotName, UserIndex))
	{
		OutError = FString::Printf(TEXT("the save system would not write to slot '%s'"), *SlotName);
		return false;
	}

	return true;
}

UCataclysmSaveRecord* FCataclysmSaveStorage::ReadFromSlot(
	const FString& SlotName,
	int32 UserIndex,
	TSubclassOf<UCataclysmSaveRecord> RecordClass,
	UObject* Outer,
	ECataclysmSaveLoadResult& OutResult,
	FString& OutMessage)
{
	OutMessage.Reset();

	if (SlotName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		// NOT A FAILURE. It is what a player who has never saved has, and what a
		// Solo Self-Found character's account slot always has.
		OutResult = ECataclysmSaveLoadResult::SlotIsEmpty;
		return nullptr;
	}

	TArray<uint8> Bytes;
	if (!UGameplayStatics::LoadDataFromSlot(Bytes, SlotName, UserIndex) || Bytes.Num() == 0)
	{
		OutResult = ECataclysmSaveLoadResult::SlotCouldNotBeRead;
		OutMessage = FString::Printf(TEXT("slot '%s' exists but nothing could be read out of it"), *SlotName);
		return nullptr;
	}

	FString Json;
	FFileHelper::BufferToString(Json, Bytes.GetData(), Bytes.Num());

	const UCataclysmSaveRecord* Default = RecordClass.GetDefaultObject();
	const int32 Target = Default != nullptr ? Default->CurrentSchemaVersion() : 0;

	// THE BACKUP GOES DOWN BEFORE ANY STEP RUNS. Section 5, rule 6. The bytes
	// written are the ones just read, untouched, so the backup is the file the
	// player had rather than this build's idea of it.
	int32 OnDisk = 0;
	const bool bHaveVersion = ReadSchemaVersion(Json, OnDisk);
	const bool bWillMigrate = bHaveVersion && OnDisk >= UCataclysmSaveRecord::FirstSchemaVersion
		&& OnDisk < Target;

	if (bWillMigrate)
	{
		const FString Backup = BackupSlotName(SlotName, OnDisk);
		if (!UGameplayStatics::SaveDataToSlot(Bytes, Backup, UserIndex))
		{
			// REFUSED RATHER THAN MIGRATED WITHOUT ONE. A migration with no way
			// back is the case rule 6 exists to prevent, so failing to write the
			// backup stops the load instead of proceeding without it.
			OutResult = ECataclysmSaveLoadResult::SlotCouldNotBeRead;
			OutMessage = FString::Printf(
				TEXT("slot '%s' holds a version %d record and this build is at version %d, ")
				TEXT("but the backup '%s' could not be written, so nothing was migrated"),
				*SlotName, OnDisk, Target, *Backup);
			return nullptr;
		}
	}

	UCataclysmSaveRecord* Loaded = FromJson(Json, RecordClass, Outer, OutResult, OutMessage);
	if (Loaded == nullptr)
	{
		return nullptr;
	}

	// WHERE IT CAME FROM, WHICH IS NOT PART OF THE RECORD. It is Transient, so
	// it never reaches the file; it is here so a caller writing the record back
	// does not have to be told the slot a second time.
	Loaded->SlotItWasReadFrom = SlotName;

	if (OutResult == ECataclysmSaveLoadResult::Migrated)
	{
		// AND ONLY NOW IS THE ORIGINAL REPLACED, with the record having been read
		// back successfully -- which is the line above.
		FString WriteError;
		if (!WriteToSlot(Loaded, SlotName, UserIndex, WriteError))
		{
			OutResult = ECataclysmSaveLoadResult::MigratedButCouldNotBeWrittenBack;
			OutMessage = FString::Printf(
				TEXT("a %s record migrated from version %d to version %d but could not be ")
				TEXT("written back to slot '%s': %s"),
				*Loaded->RecordType().ToString(), OnDisk, Target, *SlotName, *WriteError);
			return Loaded;
		}
	}

	return Loaded;
}

FString FCataclysmSaveStorage::BackupSlotName(const FString& SlotName, int32 FromVersion)
{
	if (SlotName.IsEmpty())
	{
		return FString();
	}

	return FString::Printf(TEXT("%s%s%d"), *SlotName, BackupInfix, FromVersion);
}

const TCHAR* FCataclysmSaveStorage::Describe(ECataclysmSaveLoadResult Result)
{
	switch (Result)
	{
		case ECataclysmSaveLoadResult::Loaded:            return TEXT("read");
		case ECataclysmSaveLoadResult::Migrated:          return TEXT("read, and migrated on the way in");
		case ECataclysmSaveLoadResult::SlotIsEmpty:       return TEXT("there is nothing in that slot");
		case ECataclysmSaveLoadResult::SlotCouldNotBeRead: return TEXT("the slot could not be read");
		case ECataclysmSaveLoadResult::NotValidJson:      return TEXT("what is in the slot is not valid JSON");
		case ECataclysmSaveLoadResult::NotARecord:        return TEXT("what is in the slot is not a JSON object");
		case ECataclysmSaveLoadResult::MigrationRefusedIt: return TEXT("the migration chain refused it");
		case ECataclysmSaveLoadResult::FieldsWouldNotRead: return TEXT("its fields would not read back");
		case ECataclysmSaveLoadResult::MigratedButCouldNotBeWrittenBack:
			return TEXT("it migrated but could not be written back");
	}

	return TEXT("an unnamed result");
}
