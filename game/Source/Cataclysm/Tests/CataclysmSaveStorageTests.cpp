// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Save/CataclysmSaveRecord.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveExampleRecord.h"
#include "Tests/CataclysmSaveFixtures.h"

/**
 * Writing a record to a slot and reading it back, through the engine's own save
 * system. Issue #529.
 *
 * THESE TESTS TOUCH THE DISK, and that is the point of them: everything else
 * about the save system can be tested by passing strings around, and the one
 * thing that cannot is whether a record survives the round trip through
 * `ISaveGameSystem`. In the editor that writes to `game/Saved/SaveGames/`, which
 * is not in version control.
 *
 * EVERY SLOT THEY USE IS NAMED FOR THIS TEST AND DELETED AFTERWARDS, so a run
 * cannot overwrite a real save and cannot leave anything behind. `Cleanup` is
 * called on every path out, including the failing ones.
 */
namespace CataclysmSaveStorageTest
{
	/** Nothing a player would ever have. */
	static const TCHAR* SlotPrefix = TEXT("CataclysmAutomationTest_");

	static constexpr int32 UserIndex = 0;

	static FString Slot(const TCHAR* Suffix)
	{
		return FString(SlotPrefix) + Suffix;
	}

	/** Delete a slot and its backup, whether or not they are there. */
	static void Cleanup(const FString& SlotName)
	{
		for (int32 Version = 0; Version <= 4; ++Version)
		{
			const FString Backup = FCataclysmSaveStorage::BackupSlotName(SlotName, Version);
			if (UGameplayStatics::DoesSaveGameExist(Backup, UserIndex))
			{
				UGameplayStatics::DeleteGameInSlot(Backup, UserIndex);
			}
		}

		if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
		}
	}

	/** Put text into a slot without going through the storage layer at all. */
	static bool PutTextInSlot(const FString& SlotName, const FString& Text)
	{
		const FTCHARToUTF8 Converted(*Text);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		return UGameplayStatics::SaveDataToSlot(Bytes, SlotName, UserIndex);
	}

	/** Read a slot's bytes back as text, without going through the storage layer. */
	static bool TextInSlot(const FString& SlotName, FString& OutText)
	{
		TArray<uint8> Bytes;
		if (!UGameplayStatics::LoadDataFromSlot(Bytes, SlotName, UserIndex))
		{
			return false;
		}
		FFileHelper::BufferToString(OutText, Bytes.GetData(), Bytes.Num());
		return true;
	}
}

/**
 * A record written to a slot comes back with its fields intact.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveSlotRoundTrip,
	"Cataclysm.SaveStorage.ARecordWrittenToASlotComesBackWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveSlotRoundTrip::RunTest(const FString&)
{
	using namespace CataclysmSaveStorageTest;

	const FString SlotName = Slot(TEXT("RoundTrip"));
	Cleanup(SlotName);

	UCataclysmAccountSave* Written = NewObject<UCataclysmAccountSave>();
	Written->SchemaVersion = UCataclysmAccountSave::SchemaVersionNow;
	Written->Lethality = ECataclysmLethality::Heretic;
	Written->Population = ECataclysmPopulation::Offline;
	Written->EmpireUpgradePoints = 37;
	Written->Characters.Add(FGuid(0x11112222, 0x33334444, 0x55556666, 0x77778888));

	FString Error;
	if (!FCataclysmSaveStorage::WriteToSlot(Written, SlotName, UserIndex, Error))
	{
		AddError(FString::Printf(TEXT("could not write to slot '%s': %s"), *SlotName, *Error));
		Cleanup(SlotName);
		return false;
	}

	// WHAT LANDED ON DISK IS READABLE TEXT, which is the whole reason section 4
	// chose JSON over the engine's binary archive. A binary write would still
	// pass every other check in this test.
	FString OnDisk;
	if (!TextInSlot(SlotName, OnDisk) || !OnDisk.Contains(TEXT("\"SchemaVersion\"")))
	{
		AddError(TEXT("what reached the slot is not the readable JSON the design asks for"));
		Cleanup(SlotName);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::ReadFromSlot(
		SlotName, UserIndex, UCataclysmAccountSave::StaticClass(), GetTransientPackage(),
		Result, Message));

	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("nothing came back from slot '%s': %s -- %s"),
			*SlotName, FCataclysmSaveStorage::Describe(Result), *Message));
		Cleanup(SlotName);
		return false;
	}

	TestEqual(TEXT("it was read rather than migrated"),
		static_cast<int32>(Result), static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("the lethality mode came back"), static_cast<int32>(Read->Lethality),
		static_cast<int32>(ECataclysmLethality::Heretic));
	TestEqual(TEXT("the population came back"), static_cast<int32>(Read->Population),
		static_cast<int32>(ECataclysmPopulation::Offline));
	TestEqual(TEXT("the banked points came back"), Read->EmpireUpgradePoints, 37);
	TestEqual(TEXT("the character list came back"), Read->Characters.Num(), 1);
	TestEqual(TEXT("and it is the same character"), Read->Characters[0], Written->Characters[0]);
	TestEqual(TEXT("the record knows which slot it came from"),
		Read->SlotItWasReadFrom, SlotName);

	Cleanup(SlotName);
	return true;
}

/**
 * An older record in a slot is backed up before anything touches it, migrated,
 * and only then written back over the original.
 *
 * SECTION 5, RULE 6, AND IT IS THE RULE THAT DECIDES WHAT A BROKEN MIGRATION
 * COSTS A PLAYER. Without it, a migration that goes wrong overwrites the only
 * copy of a character somebody has been playing for a month.
 *
 * THE BACKUP IS CHECKED BY ITS CONTENTS AND NOT BY ITS EXISTENCE. A backup
 * written after the migration, or written from the migrated record, would exist
 * and would be worthless; this reads it and checks it still holds the version 1
 * text, `Title` and all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveOlderSlotIsBackedUpThenMigrated,
	"Cataclysm.SaveStorage.AnOlderRecordIsBackedUpBeforeItIsMigrated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveOlderSlotIsBackedUpThenMigrated::RunTest(const FString&)
{
	using namespace CataclysmSaveStorageTest;

	const FString SlotName = Slot(TEXT("OldExample"));
	Cleanup(SlotName);

	// THE SLOT IS FILLED FROM THE COMMITTED VERSION 1 FILE, not from anything
	// this build wrote. That is what makes this a test of loading an old save
	// rather than of the code agreeing with itself.
	FString Version1Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Example_v1.json"), Version1Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	if (!PutTextInSlot(SlotName, Version1Text))
	{
		AddError(FString::Printf(TEXT("could not put the version 1 text into slot '%s'"), *SlotName));
		Cleanup(SlotName);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	UCataclysmSaveExampleRecord* Read = Cast<UCataclysmSaveExampleRecord>(
		FCataclysmSaveStorage::ReadFromSlot(SlotName, UserIndex,
			UCataclysmSaveExampleRecord::StaticClass(), GetTransientPackage(), Result, Message));

	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("nothing came back from slot '%s': %s -- %s"),
			*SlotName, FCataclysmSaveStorage::Describe(Result), *Message));
		Cleanup(SlotName);
		return false;
	}

	TestEqual(TEXT("it says it was migrated"),
		static_cast<int32>(Result), static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("and it reached the current version"), Read->SchemaVersion,
		UCataclysmSaveExampleRecord::SchemaVersionNow);
	TestEqual(TEXT("the label came through the rename"), Read->Label,
		FString(TEXT("The Example Record")));
	TestEqual(TEXT("and 725 copper split into 7 gold"), Read->Gold, 7);
	TestEqual(TEXT("and 25 copper"), Read->Copper, 25);

	// THE BACKUP HOLDS THE FILE THAT WAS THERE, UNTOUCHED.
	const FString BackupName = FCataclysmSaveStorage::BackupSlotName(SlotName, 1);
	if (!UGameplayStatics::DoesSaveGameExist(BackupName, UserIndex))
	{
		AddError(FString::Printf(TEXT("no backup was written to '%s' before the migration"),
			*BackupName));
		Cleanup(SlotName);
		return false;
	}

	FString BackupText;
	if (!TextInSlot(BackupName, BackupText))
	{
		AddError(TEXT("the backup slot exists but could not be read"));
		Cleanup(SlotName);
		return false;
	}

	TestTrue(TEXT("the backup still says it is version 1"),
		BackupText.Contains(TEXT("\"SchemaVersion\": 1")));
	TestTrue(TEXT("the backup still holds the field the migration renamed away"),
		BackupText.Contains(TEXT("Title")));
	TestFalse(TEXT("the backup is not the migrated record"), BackupText.Contains(TEXT("Gold")));

	// AND THE ORIGINAL SLOT NOW HOLDS THE MIGRATED RECORD, so the next load has
	// no work to do.
	FString SlotText;
	if (!TextInSlot(SlotName, SlotText))
	{
		AddError(TEXT("the slot could not be read after the migration"));
		Cleanup(SlotName);
		return false;
	}
	TestTrue(TEXT("the slot was rewritten at the current version"),
		SlotText.Contains(TEXT("\"SchemaVersion\": 3")));
	TestFalse(TEXT("and no longer holds the old field name"), SlotText.Contains(TEXT("Title")));

	ECataclysmSaveLoadResult Again = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString AgainMessage;
	UCataclysmSaveRecord* Second = FCataclysmSaveStorage::ReadFromSlot(SlotName, UserIndex,
		UCataclysmSaveExampleRecord::StaticClass(), GetTransientPackage(), Again, AgainMessage);

	TestNotNull(TEXT("reading it a second time works"), Second);
	TestEqual(TEXT("and the second read has nothing to migrate"),
		static_cast<int32>(Again), static_cast<int32>(ECataclysmSaveLoadResult::Loaded));

	Cleanup(SlotName);
	return true;
}

/**
 * An empty slot is an answer and not a failure, and rubbish in a slot is refused.
 *
 * THE FIRST IS WHAT A NEW PLAYER HAS, and what a Solo Self-Found character's
 * account slot always has, so a caller that treated it as an error would report
 * one every time the game started.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveEmptyAndBrokenSlots,
	"Cataclysm.SaveStorage.AnEmptySlotIsNotAFailureAndRubbishIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveEmptyAndBrokenSlots::RunTest(const FString&)
{
	using namespace CataclysmSaveStorageTest;

	const FString Missing = Slot(TEXT("NothingHere"));
	Cleanup(Missing);

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::Loaded;
	FString Message;
	UCataclysmSaveRecord* Nothing = FCataclysmSaveStorage::ReadFromSlot(Missing, UserIndex,
		UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message);

	TestNull(TEXT("an empty slot yields no record"), Nothing);
	TestEqual(TEXT("and says so plainly"), static_cast<int32>(Result),
		static_cast<int32>(ECataclysmSaveLoadResult::SlotIsEmpty));
	TestFalse(TEXT("which is not one of the failures"),
		FCataclysmSaveStorage::Succeeded(Result));

	// AN EMPTY SLOT NAME IS THE ANSWER `UCataclysmSavePartition` GIVES FOR A
	// SOLO SELF-FOUND CHARACTER'S ACCOUNT RECORD, and writing to it anyway would
	// make one account record shared by every Solo Self-Found character there is.
	UCataclysmAccountSave* Record = NewObject<UCataclysmAccountSave>();
	Record->SchemaVersion = UCataclysmAccountSave::SchemaVersionNow;
	FString WriteError;
	TestFalse(TEXT("writing to an empty slot name is refused"),
		FCataclysmSaveStorage::WriteToSlot(Record, FString(), UserIndex, WriteError));
	TestFalse(TEXT("and it says why"), WriteError.IsEmpty());

	const FString Broken = Slot(TEXT("Rubbish"));
	Cleanup(Broken);
	if (!PutTextInSlot(Broken, TEXT("this is not JSON at all")))
	{
		AddError(TEXT("could not put anything in the slot to break it"));
		Cleanup(Broken);
		return false;
	}

	ECataclysmSaveLoadResult BrokenResult = ECataclysmSaveLoadResult::Loaded;
	FString BrokenMessage;
	UCataclysmSaveRecord* FromRubbish = FCataclysmSaveStorage::ReadFromSlot(Broken, UserIndex,
		UCataclysmAccountSave::StaticClass(), GetTransientPackage(), BrokenResult, BrokenMessage);

	TestNull(TEXT("nothing comes back from a slot holding rubbish"), FromRubbish);
	TestFalse(TEXT("and it is reported as a failure"),
		FCataclysmSaveStorage::Succeeded(BrokenResult));

	// AND NO BACKUP WAS WRITTEN FOR IT. There was no version to back up from,
	// and a backup named for a version nobody could read would be noise.
	for (int32 Version = 0; Version <= 4; ++Version)
	{
		const FString Backup = FCataclysmSaveStorage::BackupSlotName(Broken, Version);
		if (UGameplayStatics::DoesSaveGameExist(Backup, UserIndex))
		{
			AddError(FString::Printf(TEXT("a backup '%s' was written for an unreadable slot"),
				*Backup));
			Cleanup(Broken);
			return false;
		}
	}

	Cleanup(Broken);
	return true;
}

/**
 * A backup slot is named for the version it holds, so migrating through several
 * versions keeps several backups.
 *
 * THE COPY WORTH KEEPING IS THE OLDEST ONE, which is the one furthest from this
 * build and the one a player would want if the newest migration is the broken
 * step. A single backup name would have each migration overwrite it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveBackupNamesTheVersion,
	"Cataclysm.SaveStorage.ABackupIsNamedForTheVersionItHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveBackupNamesTheVersion::RunTest(const FString&)
{
	const FString First = FCataclysmSaveStorage::BackupSlotName(TEXT("Character_ABC"), 1);
	const FString Second = FCataclysmSaveStorage::BackupSlotName(TEXT("Character_ABC"), 2);

	TestNotEqual(TEXT("two versions get two different backups"), First, Second);
	TestTrue(TEXT("a backup name starts with the slot it belongs to"),
		First.StartsWith(TEXT("Character_ABC")));
	TestTrue(TEXT("and it names the version it holds"), First.EndsWith(TEXT("1")));
	TestTrue(TEXT("and the other one names its own"), Second.EndsWith(TEXT("2")));
	TestNotEqual(TEXT("a backup is never the slot itself"), First, FString(TEXT("Character_ABC")));

	// AN EMPTY SLOT NAME HAS NO BACKUP NAME EITHER, rather than one called
	// nothing followed by a number, which every empty-named record would share.
	TestTrue(TEXT("an empty slot name has no backup name"),
		FCataclysmSaveStorage::BackupSlotName(FString(), 1).IsEmpty());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
