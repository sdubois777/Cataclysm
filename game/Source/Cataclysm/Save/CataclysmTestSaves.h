// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The save slots a running automation test creates, deleted when that test ends. Issue #2658, ruled 2026-11-01.
 *
 * WHY. A test world whose game mode starts play begins saving a run (`ACataclysmGameMode::BeginSavingThisRun`), and its
 * writer writes on every kill and clock tick; nothing deleted those slots, so a worktree's `game/Saved/SaveGames` held
 * thousands after one window, and the character select screen lists every `Character_` slot it finds.
 *
 * WHAT IS NOTED. `FCataclysmSaveStorage` calls `NoteAWrite` before each write, on the thread that asks for it -- the game
 * thread -- and BEFORE an asynchronous write is queued, not when it lands. A slot is noted only while an automation test
 * runs and ONLY IF IT DID NOT EXIST BEFORE THAT WRITE, so a slot that was there before, a player's or anything else's, is
 * never noted and never deleted. A migration's backup is noted only when the slot it backs up is noted (`NoteABackup`),
 * so migrating a slot that existed before never has its backup deleted.
 *
 * WHEN. `FAutomationTestFramework::OnTestEndEvent`, after the test's result is set, deletes exactly the noted slots once
 * their writes in flight finish, and then logs the run's totals so far, as "Test saves: N written and deleted, M left
 * behind: <names>" (`EndATest`). The same line is logged once more when the automation session stops or, failing that,
 * when the application terminates. `tools/unreal_build.py` reads the LAST such line in the log and prints it beside its
 * "Tests:" line; since #2673 a run that leaves a slot behind, or logs no such line, fails.
 *
 * A LINE AT EVERY TEST'S END, NOT ONLY AT EXIT. Issue #2677: window w131 measured a run whose log ends at "Engine exit
 * requested" with neither exit-time line in it, this one and `CataclysmTestMemorySummary:`; the forced exit that
 * `-testexit` makes can end the process before a line logged at termination reaches the file. A line logged at a test's
 * end is followed by the framework's own "Test Completed" lines, which that log did keep.
 */
struct CATACLYSM_API FCataclysmTestSaves
{
	/** Before a write to `SlotName`: noted when an automation test runs and the slot does not exist yet. */
	static void NoteAWrite(const FString& SlotName, int32 UserIndex);

	/** Before writing `BackupName`, a migration's backup of `SourceSlot`: noted only when `SourceSlot` is noted. */
	static void NoteABackup(const FString& BackupName, const FString& SourceSlot, int32 UserIndex);

	/** Whether `SlotName` is noted for deletion at the end of the running test. */
	static bool IsNoted(const FString& SlotName);

	/**
	 * Deletes every noted slot that still exists, once its writes in flight finish, and forgets them all. What the end
	 * of each test runs; public so a test can run it and see what it does.
	 *
	 * @return the slots deleted
	 */
	static TArray<FString> DeleteWhatTheTestWrote(int32 UserIndex);

	/**
	 * What the end of every test runs: `DeleteWhatTheTestWrote`, then the run's "Test saves:" line as it stands, logged
	 * and returned. "Left behind" is measured then: the slots in the folder that were not there when the run took it.
	 * Empty, and nothing logged, before the run has taken the folder. Issue #2677.
	 */
	static FString EndATest(int32 UserIndex);

	/** Whether the automation framework calls `EndATest` at the end of each test. False in a build without tests. */
	static bool IsCalledAtEachTestsEnd();

	/** The line the run's totals are logged as. */
	static FString SummaryLine(int32 WrittenAndDeleted, const TArray<FString>& LeftBehind);
};
