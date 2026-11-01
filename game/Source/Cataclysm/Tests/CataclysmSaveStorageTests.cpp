// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Save/CataclysmSaveRecord.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmTestSaves.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
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

namespace CataclysmSaveStorageRaceTest
{
	/**
	 * How long the first queued write's label is, in characters, and so roughly
	 * its size on disk in bytes: 32 MB.
	 *
	 * LARGE SO THAT IT IS STILL WRITING WHEN THE SYNCHRONOUS WRITE COMES. Every
	 * record is built before the first is queued, so between that write being
	 * handed over and the synchronous write opening the file the game thread
	 * builds only three small records and one more. A 32 MB write takes
	 * milliseconds; that gap is microseconds. Read, not measured.
	 */
	static constexpr int32 LargeLabelLength = 32 * 1024 * 1024;

	/** How many small writes are queued behind the large one. */
	static constexpr int32 SmallWritesQueued = 3;

	/** How long to let queued writes finish before giving up. */
	static constexpr double LongestWaitSeconds = 30.0;

	static UCataclysmSaveExampleRecord* ExampleRecordOfSize(int32 Gold, int32 LabelLength)
	{
		UCataclysmSaveExampleRecord* Record = NewObject<UCataclysmSaveExampleRecord>();
		Record->SchemaVersion = UCataclysmSaveExampleRecord::SchemaVersionNow;
		Record->Label = FString::ChrN(LabelLength, TEXT('x'));
		Record->Gold = Gold;
		return Record;
	}

	/** Wait for every write to a slot to finish. Returns whether they did. */
	static bool WaitUntilNothingIsWriting(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + LongestWaitSeconds;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName))
		{
			if (FPlatformTime::Seconds() >= GiveUpAt)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		return true;
	}
}

/**
 * A synchronous write to a slot waits for the asynchronous writes queued to it,
 * then succeeds, and its bytes are what stays in the file. Issue #2494.
 *
 * THE PLAY EFFECT: a character that dies just after picking something up keeps
 * its death record. A pickup writes the character asynchronously and a death
 * writes it synchronously; with the pickup's write still holding the file open,
 * the death's write was refused.
 *
 * THE CONTROL COMES FIRST: the same synchronous write with nothing queued.
 *
 * WITHOUT THE WAIT, BY READING, IT FAILS EITHER WAY ROUND. If the large write
 * has opened the file first, the synchronous open is refused; if the
 * synchronous write gets in first, the queued writes land after it and the
 * file ends holding the last of them. In both, a write is still in flight when
 * the synchronous call returns. The callbacks are unset on purpose: a write
 * refused while the guard is in would otherwise log an error in a later test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWaitsForItsSlot,
	"Cataclysm.SaveStorage.ASynchronousWriteWaitsForTheAsynchronousWritesToItsSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWaitsForItsSlot::RunTest(const FString&)
{
	using namespace CataclysmSaveStorageTest;
	using namespace CataclysmSaveStorageRaceTest;

	const FString SlotName = Slot(TEXT("SynchronousAfterAsynchronous"));
	Cleanup(SlotName);

	UCataclysmSaveExampleRecord* Synchronous = ExampleRecordOfSize(99, 0);
	FString Expected;
	FString JsonError;
	if (!FCataclysmSaveStorage::ToJson(Synchronous, Expected, JsonError))
	{
		AddError(FString::Printf(TEXT("the synchronous record would not turn into JSON: %s"),
			*JsonError));
		return false;
	}

	// THE CONTROL: nothing queued.
	{
		FString Error;
		const bool bWritten =
			FCataclysmSaveStorage::WriteToSlot(Synchronous, SlotName, UserIndex, Error);
		const bool bStillWriting = FCataclysmSaveStorage::HasAWriteInFlight(SlotName);
		FString OnDisk;
		TextInSlot(SlotName, OnDisk);

		TestTrue(FString::Printf(TEXT("control: with nothing queued the synchronous write succeeds (%s)"),
			*Error), bWritten);
		TestFalse(TEXT("control: and nothing is in flight when it returns"), bStillWriting);
		TestTrue(TEXT("control: and the file holds the synchronous record's bytes"),
			OnDisk == Expected);
	}

	// EVERY RECORD IS BUILT BEFORE ANY WRITE IS QUEUED, so the gap between the
	// first write being handed over and the synchronous one is as short as it
	// can be made.
	TArray<UCataclysmSaveExampleRecord*> Queued;
	Queued.Add(ExampleRecordOfSize(1, LargeLabelLength));
	for (int32 Index = 0; Index < SmallWritesQueued; ++Index)
	{
		Queued.Add(ExampleRecordOfSize(2 + Index, 0));
	}

	int32 Started = 0;
	FString QueueError;
	for (const UCataclysmSaveExampleRecord* Record : Queued)
	{
		if (FCataclysmSaveStorage::WriteToSlotAsync(Record, SlotName, UserIndex, nullptr, QueueError))
		{
			++Started;
		}
	}

	// AT ONCE. Nothing between the queuing above and this call.
	FString Error;
	const bool bWritten =
		FCataclysmSaveStorage::WriteToSlot(Synchronous, SlotName, UserIndex, Error);
	const bool bStillWriting = FCataclysmSaveStorage::HasAWriteInFlight(SlotName);

	// AND THEN EVERYTHING IS LET FINISH, so a queued write that would land after
	// the synchronous one has landed before the file is read.
	const bool bDrained = WaitUntilNothingIsWriting(SlotName);
	FString OnDisk;
	if (bDrained)
	{
		TextInSlot(SlotName, OnDisk);
	}

	TestEqual(TEXT("every queued write was handed over"), Started, SmallWritesQueued + 1);
	TestTrue(FString::Printf(TEXT("with writes queued the synchronous write still succeeds (%s)"),
		*Error), bWritten);
	TestFalse(TEXT("and no write to the slot is in flight when it returns"), bStillWriting);
	TestTrue(TEXT("every queued write finished within the wait"), bDrained);
	TestTrue(TEXT("and the file holds the synchronous record's bytes, not a queued one's"),
		OnDisk == Expected);

	if (bDrained)
	{
		Cleanup(SlotName);
	}
	return true;
}

/**
 * The slots a test writes are deleted when it ends, and nothing that was there before. Issue #2658, ruled 2026-11-01.
 * `FCataclysmTestSaves::DeleteWhatTheTestWrote` is what the end of every test runs; this runs it in the middle and looks:
 *
 * - a slot this test made with `WriteToSlot` is deleted;
 * - a slot that was there before, written over by `WriteToSlot`, is kept;
 * - an asynchronous write's slot is noted when the write is queued, before it lands, and is deleted;
 * - a migration's backup of a slot this test made goes with it;
 * - and a migration's backup of a slot that was there before is kept, with the slot.
 *
 * "THERE BEFORE" IS A SLOT PUT DOWN WITHOUT THE STORAGE LAYER, as a save from before the test would have been. That the
 * framework runs the clean-up at every test's end is `EachTestsEndLogsTheRunsTestSavesLine`'s to show. Prints "TEST SAVES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTestSavesDeletedTest,
	"Cataclysm.SaveStorage.ATestsSavesAreDeletedWhenItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTestSavesDeletedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveStorageTest;

	const FString Made = Slot(TEXT("TestSavesMade"));
	const FString Before = Slot(TEXT("TestSavesThereBefore"));
	const FString Queued = Slot(TEXT("TestSavesQueued"));
	const FString MadeOld = Slot(TEXT("TestSavesMadeOld"));
	const FString BeforeOld = Slot(TEXT("TestSavesThereBeforeOld"));
	const TArray<FString> Slots = {Made, Before, Queued, MadeOld, BeforeOld};
	for (const FString& Each : Slots)
	{
		Cleanup(Each);
	}
	ON_SCOPE_EXIT
	{
		for (const FString& Each : Slots)
		{
			Cleanup(Each);
		}
	};
	// WHATEVER EARLIER STEPS OF THIS TEST NOTED, forgotten, so only what follows is measured.
	FCataclysmTestSaves::DeleteWhatTheTestWrote(UserIndex);

	FString Version1Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Example_v1.json"), Version1Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	UCataclysmSaveExampleRecord* Record = NewObject<UCataclysmSaveExampleRecord>();
	Record->SchemaVersion = UCataclysmSaveExampleRecord::SchemaVersionNow;
	Record->Label = TEXT("A test's record");

	// MADE BY THE TEST, and THERE BEFORE IT, written over.
	FString Error;
	const bool bMade = FCataclysmSaveStorage::WriteToSlot(Record, Made, UserIndex, Error);
	const bool bBeforePut = PutTextInSlot(Before, TEXT("{ \"SchemaVersion\": 3 }"));
	const bool bBeforeWritten = FCataclysmSaveStorage::WriteToSlot(Record, Before, UserIndex, Error);

	// QUEUED: noted before it lands.
	const bool bQueued = FCataclysmSaveStorage::WriteToSlotAsync(Record, Queued, UserIndex, nullptr, Error);
	const bool bNotedWhenQueued = FCataclysmTestSaves::IsNoted(Queued);

	// A MIGRATION OF EACH: the test's own slot, noted by hand as a write would note it, and one there before.
	FCataclysmTestSaves::NoteAWrite(MadeOld, UserIndex);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	const bool bOldsPut = PutTextInSlot(MadeOld, Version1Text) && PutTextInSlot(BeforeOld, Version1Text);
	const bool bMadeOldRead = FCataclysmSaveStorage::ReadFromSlot(MadeOld, UserIndex,
		UCataclysmSaveExampleRecord::StaticClass(), GetTransientPackage(), Result, Message) != nullptr;
	const bool bBeforeOldRead = FCataclysmSaveStorage::ReadFromSlot(BeforeOld, UserIndex,
		UCataclysmSaveExampleRecord::StaticClass(), GetTransientPackage(), Result, Message) != nullptr;
	const FString MadeBackup = FCataclysmSaveStorage::BackupSlotName(MadeOld, 1);
	const FString BeforeBackup = FCataclysmSaveStorage::BackupSlotName(BeforeOld, 1);
	const bool bBackupsWritten = UGameplayStatics::DoesSaveGameExist(MadeBackup, UserIndex)
		&& UGameplayStatics::DoesSaveGameExist(BeforeBackup, UserIndex);

	const TArray<FString> Deleted = FCataclysmTestSaves::DeleteWhatTheTestWrote(UserIndex);
	auto Exists = [](const FString& Name) { return UGameplayStatics::DoesSaveGameExist(Name, UserIndex); };
	AddInfo(FString::Printf(TEXT("TEST SAVES: deleted [%s]; made %s, there before %s, queued %s (noted when queued: %s), "
								 "made old %s and its backup %s, there before old %s and its backup %s"),
							*FString::Join(Deleted, TEXT(" | ")), Exists(Made) ? TEXT("KEPT") : TEXT("gone"),
							Exists(Before) ? TEXT("kept") : TEXT("GONE"), Exists(Queued) ? TEXT("KEPT") : TEXT("gone"),
							bNotedWhenQueued ? TEXT("yes") : TEXT("NO"), Exists(MadeOld) ? TEXT("KEPT") : TEXT("gone"),
							Exists(MadeBackup) ? TEXT("KEPT") : TEXT("gone"), Exists(BeforeOld) ? TEXT("kept") : TEXT("GONE"),
							Exists(BeforeBackup) ? TEXT("kept") : TEXT("GONE")));

	if (!TestTrue(TEXT("set-up: every slot was written"), bMade && bBeforePut && bBeforeWritten && bQueued && bOldsPut)
		|| !TestTrue(TEXT("set-up: both old records were read and migrated, each backed up"),
					 bMadeOldRead && bBeforeOldRead && bBackupsWritten))
	{
		return false;
	}
	TestFalse(TEXT("a slot the test made is deleted when it ends"), Exists(Made));
	TestTrue(TEXT("a slot that was there before the test wrote over it is kept"), Exists(Before));
	TestTrue(TEXT("an asynchronous write's slot is noted when the write is queued, before it lands"), bNotedWhenQueued);
	TestFalse(TEXT("and is deleted"), Exists(Queued));
	TestFalse(TEXT("a backup of a slot the test made is deleted with it"), Exists(MadeBackup) || Exists(MadeOld));
	TestTrue(TEXT("a slot that was there before a migration is kept"), Exists(BeforeOld));
	TestTrue(TEXT("and so is its backup"), Exists(BeforeBackup));
	return true;
}

namespace CataclysmSaveStorageTest
{
	/** Every `LogCataclysm` line logged while it lives that begins "Test saves: ". */
	struct FTestSavesLines : public FOutputDevice
	{
		TArray<FString> Lines;
		FTestSavesLines() { if (GLog) { GLog->AddOutputDevice(this); } }
		virtual ~FTestSavesLines() { if (GLog) { GLog->RemoveOutputDevice(this); } }
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type, const FName& Category) override
		{
			if (Category == FName(TEXT("LogCataclysm")) && FCString::Strncmp(Text, TEXT("Test saves: "), 12) == 0)
			{
				Lines.Add(FString(Text));
			}
		}
		/** The lines caught, after the log is flushed: a line from another thread is buffered until then. #1522. */
		TArray<FString> Caught()
		{
			if (GLog)
			{
				GLog->Flush();
			}
			return Lines;
		}
	};
}

/**
 * The end of every test logs the run's "Test saves:" line as it stands, after deleting what the test wrote, so a run
 * whose forced exit loses the exit-time line still has one: the last test's. Issue #2677, ruled 2026-11-01; window w131
 * measured a narrow run whose log ended at "Engine exit requested" with no such line.
 *
 * THE FRAMEWORK'S CALL IS ASKED OF THE FRAMEWORK: `IsCalledAtEachTestsEnd` is whether `OnTestEndEvent` holds the hook
 * object, as `CataclysmCollectGarbageBetweenTests.cpp` asks of its own. What the call does is run here in the middle of
 * the test, where its log line can be caught. Prints "TEST SAVES LINE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTestSavesLineTest,
	"Cataclysm.SaveStorage.EachTestsEndLogsTheRunsTestSavesLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTestSavesLineTest::RunTest(const FString&)
{
	using namespace CataclysmSaveStorageTest;

	const FString Made = Slot(TEXT("TestSavesLine"));
	Cleanup(Made);
	ON_SCOPE_EXIT { Cleanup(Made); };
	// WHATEVER EARLIER STEPS OF THIS TEST NOTED, forgotten, so only what follows is measured.
	FCataclysmTestSaves::DeleteWhatTheTestWrote(UserIndex);

	UCataclysmSaveExampleRecord* Record = NewObject<UCataclysmSaveExampleRecord>();
	Record->SchemaVersion = UCataclysmSaveExampleRecord::SchemaVersionNow;
	Record->Label = TEXT("A test's record");
	FString Error;
	const bool bMade = FCataclysmSaveStorage::WriteToSlot(Record, Made, UserIndex, Error);

	FString Returned;
	TArray<FString> Logged;
	{
		FTestSavesLines Capture;
		Returned = FCataclysmTestSaves::EndATest(UserIndex);
		Logged = Capture.Caught();
	}
	const bool bGone = !UGameplayStatics::DoesSaveGameExist(Made, UserIndex);
	const bool bCalled = FCataclysmTestSaves::IsCalledAtEachTestsEnd();
	AddInfo(FString::Printf(TEXT("TEST SAVES LINE: called at each test's end %s; returned '%s'; logged %d [%s]; the slot %s"),
							bCalled ? TEXT("yes") : TEXT("NO"), *Returned, Logged.Num(), *FString::Join(Logged, TEXT(" | ")),
							bGone ? TEXT("gone") : TEXT("KEPT")));

	if (!TestTrue(TEXT("set-up: the test wrote a slot of its own"), bMade))
	{
		return false;
	}
	TestTrue(TEXT("the framework calls it at the end of each test"), bCalled);
	TestTrue(TEXT("it deletes what the test wrote"), bGone);
	TestTrue(TEXT("its line is in the form tools/unreal_build.py reads"),
			 Returned.StartsWith(TEXT("Test saves: ")) && Returned.Contains(TEXT(" written and deleted, "))
				 && Returned.Contains(TEXT(" left behind")));
	TestEqual(TEXT("it logs one line"), Logged.Num(), 1);
	TestTrue(TEXT("and the line logged is the line returned"), Logged.Num() == 1 && Logged[0] == Returned);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
