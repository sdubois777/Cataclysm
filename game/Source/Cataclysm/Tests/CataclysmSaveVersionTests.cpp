// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Save/CataclysmSaveLoad.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecord.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveVersion.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveExampleRecord.h"

/**
 * The version rule every saved record comes in by: only this build's version is read.
 *
 * THE OWNER'S RULING OF 2026-11-27, their chosen option verbatim: "Remove; old saves reset". Issue #2928 slice 12
 * removed the 45 upgrade steps, the tests that held them up and the example files of every older version. A record older
 * than the current version is refused, listed as unreadable, and left on disk as it is; a newer one is refused as
 * before; one with no usable version is refused.
 */
namespace CataclysmSaveVersionTest
{
	/** A record holding nothing but a version. */
	TSharedRef<FJsonObject> AtVersion(int32 Version)
	{
		const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetNumberField(UCataclysmSaveRecord::SchemaVersionField, Version);
		return Record;
	}

	/** The bytes of `Text`, as the storage layer writes a record. */
	TArray<uint8> BytesOf(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Bytes;
	}

	/** What a slot holds now; empty when it holds nothing or does not exist. */
	TArray<uint8> SlotBytes(const FString& SlotName)
	{
		TArray<uint8> Bytes;
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::LoadDataFromSlot(Bytes, SlotName, UCataclysmSaveWriter::UserIndex);
		}
		return Bytes;
	}

	/** The row of `Rows` for `Id`, or null. */
	const FCataclysmCharacterSummary* RowFor(const TArray<FCataclysmCharacterSummary>& Rows, const FGuid& Id)
	{
		return Rows.FindByPredicate([&Id](const FCataclysmCharacterSummary& Row) { return Row.CharacterId == Id; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveOlderRecordIsRefusedTest,
	"Cataclysm.SaveVersion.AnOlderRecordIsRefusedAndACurrentOneLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character record one version older than this build's is refused at every door, and one at this build's version
 * reads. CONTROL, THE CURRENT RECORD: written the same way, beside it, it reads whole and lists as readable, so a refusal
 * of both would fail.
 *
 * - The check answers `OlderThanThisBuild`, with a message naming both versions and saying the file is left as it is.
 * - Read from its slot, nothing comes back, the answer is `VersionRefusedIt`, and the slot holds exactly the bytes it held
 *   before the read: not rewritten, not deleted, and no `<slot>_BackupOfVersion<N>` beside it.
 * - The character list shows it as an unreadable row whose reason says an older build wrote it, the row the select
 *   screen shows as "Unreadable save, <slot>: <reason>".
 *
 * THE OLDER FILE IS THE CURRENT ONE WITH ITS VERSION LOWERED BY ONE, so the two differ in that number alone: whatever
 * refuses the older one is the version rule and nothing else in the file. Both slots are deleted before and after, passed
 * or failed. Prints "OLDER RECORD".
 */
bool FCataclysmSaveOlderRecordIsRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveVersionTest;

	const int32 Now = UCataclysmCharacterSave::SchemaVersionNow;
	const int32 Older = Now - 1;

	// 1. THE CHECK ITSELF.
	FString OlderMessage;
	const ECataclysmSaveVersionResult OlderCheck = FCataclysmSaveVersion::Check(Now, AtVersion(Older), OlderMessage);
	FString CurrentMessage;
	const ECataclysmSaveVersionResult CurrentCheck = FCataclysmSaveVersion::Check(Now, AtVersion(Now), CurrentMessage);
	TestEqual(TEXT("a record one version older is refused as older"), static_cast<int32>(OlderCheck),
			  static_cast<int32>(ECataclysmSaveVersionResult::OlderThanThisBuild));
	TestTrue(TEXT("and the refusal names both versions and says the file is left as it is"),
			 OlderMessage.Contains(FString::Printf(TEXT("version %d"), Older))
				 && OlderMessage.Contains(FString::Printf(TEXT("version %d"), Now))
				 && OlderMessage.Contains(TEXT("left as it is")));
	TestEqual(TEXT("CONTROL: a record at this build's version passes"), static_cast<int32>(CurrentCheck),
			  static_cast<int32>(ECataclysmSaveVersionResult::Current));

	// 2. THE TWO FILES, ONE VERSION APART AND OTHERWISE THE SAME.
	const FGuid CurrentId(0x5A7E0001, 0x00002928, 0x00000012, 0x00000001);
	const FGuid OlderId(0x5A7E0001, 0x00002928, 0x00000012, 0x00000002);
	const FString CurrentSlot = UCataclysmSavePartition::CharacterSlotName(CurrentId);
	const FString OlderSlot = UCataclysmSavePartition::CharacterSlotName(OlderId);
	const FString BackupSlot = FString::Printf(TEXT("%s%s%d"), *OlderSlot, FCataclysmSaveStorage::BackupInfix, Older);
	const auto DeleteAll = [&]()
	{
		for (const FString& Slot : {CurrentSlot, OlderSlot, BackupSlot})
		{
			if (UGameplayStatics::DoesSaveGameExist(Slot, UCataclysmSaveWriter::UserIndex))
			{
				UGameplayStatics::DeleteGameInSlot(Slot, UCataclysmSaveWriter::UserIndex);
			}
		}
	};
	DeleteAll();
	ON_SCOPE_EXIT { DeleteAll(); };

	FString CurrentText;
	FString OlderText;
	for (const TPair<FGuid, FString*>& Each : {TPair<FGuid, FString*>(CurrentId, &CurrentText),
											   TPair<FGuid, FString*>(OlderId, &OlderText)})
	{
		UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
		Record->SchemaVersion = Now;
		Record->CharacterId = Each.Key;
		Record->CharacterName = Each.Key == CurrentId ? TEXT("Current Build") : TEXT("Older Build");
		Record->Level = 3;
		FString Error;
		if (!TestTrue(TEXT("set-up: the record becomes text"), FCataclysmSaveStorage::ToJson(Record, *Each.Value, Error)))
		{
			AddError(Error);
			return false;
		}
	}
	const FString NowField = FString::Printf(TEXT("\"%s\": %d"), UCataclysmSaveRecord::SchemaVersionField, Now);
	const FString OlderField = FString::Printf(TEXT("\"%s\": %d"), UCataclysmSaveRecord::SchemaVersionField, Older);
	if (!TestTrue(TEXT("set-up: the text states this build's version once"), OlderText.Contains(NowField)))
	{
		return false;
	}
	OlderText.ReplaceInline(*NowField, *OlderField, ESearchCase::CaseSensitive);
	const TArray<uint8> OlderBytes = BytesOf(OlderText);
	if (!TestTrue(TEXT("set-up: the current file is written to its slot"),
				  UGameplayStatics::SaveDataToSlot(BytesOf(CurrentText), CurrentSlot, UCataclysmSaveWriter::UserIndex))
		|| !TestTrue(TEXT("set-up: and the older file to its slot"),
					 UGameplayStatics::SaveDataToSlot(OlderBytes, OlderSlot, UCataclysmSaveWriter::UserIndex)))
	{
		return false;
	}

	// 3. READ FROM THEIR SLOTS.
	ECataclysmSaveLoadResult CurrentResult = ECataclysmSaveLoadResult::NotValidJson;
	FString CurrentReadMessage;
	const UCataclysmCharacterSave* CurrentRead = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::ReadFromSlot(
		CurrentSlot, UCataclysmSaveWriter::UserIndex, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(),
		CurrentResult, CurrentReadMessage));
	ECataclysmSaveLoadResult OlderResult = ECataclysmSaveLoadResult::Loaded;
	FString OlderReadMessage;
	const UCataclysmSaveRecord* OlderRead = FCataclysmSaveStorage::ReadFromSlot(
		OlderSlot, UCataclysmSaveWriter::UserIndex, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(),
		OlderResult, OlderReadMessage);
	const TArray<uint8> OlderBytesAfter = SlotBytes(OlderSlot);
	const bool bBackupWritten = UGameplayStatics::DoesSaveGameExist(BackupSlot, UCataclysmSaveWriter::UserIndex);

	// 4. AND LISTED.
	const TArray<FCataclysmCharacterSummary> Rows = FCataclysmSaveLoad::SavedCharacters(UCataclysmSaveWriter::UserIndex);
	const FCataclysmCharacterSummary* CurrentRow = RowFor(Rows, CurrentId);
	const FCataclysmCharacterSummary* OlderRow = RowFor(Rows, OlderId);

	AddInfo(FString::Printf(TEXT("OLDER RECORD: this build reads character version %d; the check on %d \"%s\"; read "
								 "current %s, older %s (%s); older slot unchanged %d, backup written %d; listed current "
								 "readable %d, older readable %d, reason \"%s\""),
							Now, Older, *OlderMessage, FCataclysmSaveStorage::Describe(CurrentResult),
							FCataclysmSaveStorage::Describe(OlderResult), *OlderReadMessage,
							OlderBytesAfter == OlderBytes ? 1 : 0, bBackupWritten ? 1 : 0,
							CurrentRow && CurrentRow->bReadable ? 1 : 0, OlderRow && OlderRow->bReadable ? 1 : 0,
							OlderRow ? *OlderRow->Why : TEXT("(not listed)")));

	if (!TestNotNull(TEXT("CONTROL: the current record reads from its slot"), CurrentRead))
	{
		AddError(CurrentReadMessage);
		return false;
	}
	TestEqual(TEXT("CONTROL: as loaded"), static_cast<int32>(CurrentResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("CONTROL: whole, its name read back"), CurrentRead->CharacterName, FString(TEXT("Current Build")));

	TestNull(TEXT("**the older record does not read**"), OlderRead);
	TestEqual(TEXT("refused for its version"), static_cast<int32>(OlderResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::VersionRefusedIt));
	TestTrue(TEXT("and the reason says an older build wrote it"), OlderReadMessage.Contains(TEXT("older build")));
	TestTrue(TEXT("its file is left exactly as it was, not rewritten or deleted"), OlderBytesAfter == OlderBytes);
	TestFalse(TEXT("and no backup is written beside it"), bBackupWritten);

	if (!TestNotNull(TEXT("CONTROL: the current character is listed"), CurrentRow)
		|| !TestNotNull(TEXT("and so is the older one, not hidden"), OlderRow))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: the current character is listed readable"), CurrentRow->bReadable);
	TestFalse(TEXT("the older one is listed unreadable"), OlderRow->bReadable);
	TestTrue(TEXT("with the reason that an older build wrote it"), OlderRow->Why.Contains(TEXT("older build")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveNewerRecordIsRefusedTest,
	"Cataclysm.SaveVersion.ARecordFromANewerBuildIsRefusedRatherThanGuessedAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A record written by a newer build is refused rather than loaded.
 *
 * SECTION 5, RULE 5, AND IT IS A RULE RATHER THAN A PREFERENCE. Loading a newer save silently drops whatever the newer
 * build added, and the player finds out when the thing they care about is gone. Kept from the migration tests' file,
 * `Cataclysm.SaveMigration.ARecordFromANewerBuildIsRefusedRatherThanGuessedAt`, when that file went (2026-11-27).
 */
bool FCataclysmSaveNewerRecordIsRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveVersionTest;

	const int32 Now = UCataclysmSaveExampleRecord::SchemaVersionNow;
	FString Message;
	const ECataclysmSaveVersionResult Result = FCataclysmSaveVersion::Check(Now, AtVersion(Now + 1), Message);
	TestEqual(TEXT("a record one version newer is refused as newer"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveVersionResult::NewerThanThisBuild));
	TestTrue(TEXT("and the refusal says both version numbers"),
			 Message.Contains(FString::Printf(TEXT("version %d"), Now + 1))
				 && Message.Contains(FString::Printf(TEXT("version %d"), Now)));

	// AND IT IS REFUSED ALL THE WAY OUT. A caller that only looked at whether an object came back would otherwise never
	// see it.
	const FString Json = FString::Printf(TEXT("{ \"%s\": %d, \"Label\": \"from the future\" }"),
										 UCataclysmSaveRecord::SchemaVersionField, Now + 1);
	ECataclysmSaveLoadResult LoadResult = ECataclysmSaveLoadResult::Loaded;
	FString LoadMessage;
	UCataclysmSaveRecord* Loaded = FCataclysmSaveStorage::FromJson(
		Json, UCataclysmSaveExampleRecord::StaticClass(), GetTransientPackage(), LoadResult, LoadMessage);
	TestNull(TEXT("nothing comes back from a newer record"), Loaded);
	TestEqual(TEXT("and the answer is a version refusal"), static_cast<int32>(LoadResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::VersionRefusedIt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveUnversionedRecordIsRefusedTest,
	"Cataclysm.SaveVersion.ARecordWithNoUsableVersionIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A record with no version field, a fractional one, or one below the first real version is refused; a version written
 * as text is read as the number it states. Ported from the migration tests' file when that went (2026-11-27), whose test
 * of the same name ran these through the chain rather than through the check.
 */
bool FCataclysmSaveUnversionedRecordIsRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveVersionTest;

	const int32 Now = UCataclysmSaveExampleRecord::SchemaVersionNow;
	FString Message;

	const TSharedRef<FJsonObject> NoField = MakeShared<FJsonObject>();
	NoField->SetStringField(TEXT("Label"), TEXT("nothing said what shape this is"));
	TestEqual(TEXT("a record with no version field is refused"),
			  static_cast<int32>(FCataclysmSaveVersion::Check(Now, NoField, Message)),
			  static_cast<int32>(ECataclysmSaveVersionResult::NoVersionField));

	const TSharedRef<FJsonObject> NotAWholeNumber = MakeShared<FJsonObject>();
	NotAWholeNumber->SetNumberField(UCataclysmSaveRecord::SchemaVersionField, Now + 0.5);
	TestEqual(TEXT("a fractional version is refused rather than truncated"),
			  static_cast<int32>(FCataclysmSaveVersion::Check(Now, NotAWholeNumber, Message)),
			  static_cast<int32>(ECataclysmSaveVersionResult::NoVersionField));

	TestEqual(TEXT("a version of zero is refused"),
			  static_cast<int32>(FCataclysmSaveVersion::Check(Now, AtVersion(0), Message)),
			  static_cast<int32>(ECataclysmSaveVersionResult::VersionIsNotReal));
	TestEqual(TEXT("and so is a negative one"),
			  static_cast<int32>(FCataclysmSaveVersion::Check(Now, AtVersion(-3), Message)),
			  static_cast<int32>(ECataclysmSaveVersionResult::VersionIsNotReal));

	// A VERSION WRITTEN AS TEXT IS READ AS ITS NUMBER: a file edited by hand may quote it.
	const TSharedRef<FJsonObject> AsText = MakeShared<FJsonObject>();
	AsText->SetStringField(UCataclysmSaveRecord::SchemaVersionField, FString::FromInt(Now));
	TestEqual(TEXT("this build's version written as text passes"),
			  static_cast<int32>(FCataclysmSaveVersion::Check(Now, AsText, Message)),
			  static_cast<int32>(ECataclysmSaveVersionResult::Current));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
