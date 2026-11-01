// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmTestSaves.h"

#include "Cataclysm.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "PlatformFeatures.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "SaveGameSystem.h"

namespace CataclysmTestSaves
{
	struct FState
	{
		FCriticalSection Lock;
		TSet<FString> Noted;
		int32 WrittenAndDeleted = 0;
		bool bFolderTaken = false;
		TSet<FString> FolderAtStart;
		bool bSummaryLogged = false;
	};

	FState& State()
	{
		static FState Shared;
		return Shared;
	}

	TArray<FString> TheFolder(int32 UserIndex)
	{
		TArray<FString> Names;
		if (ISaveGameSystem* System = IPlatformFeaturesModule::Get().GetSaveGameSystem())
		{
			System->GetSaveGameNames(Names, UserIndex);
		}
		return Names;
	}

	/** The folder as the run found it, taken once: when the session starts, or else at the first test. */
	void TakeTheFolder()
	{
		FState& S = State();
		FScopeLock Guard(&S.Lock);
		if (S.bFolderTaken)
		{
			return;
		}
		S.bFolderTaken = true;
		S.FolderAtStart = TSet<FString>(TheFolder(UCataclysmSaveWriter::UserIndex));
	}

	/** The run's totals, logged once: when the session stops, or else when the engine exits. */
	void LogTheSummary()
	{
		FState& S = State();
		TArray<FString> LeftBehind;
		int32 Deleted = 0;
		{
			FScopeLock Guard(&S.Lock);
			if (S.bSummaryLogged || !S.bFolderTaken)
			{
				return;
			}
			S.bSummaryLogged = true;
			Deleted = S.WrittenAndDeleted;
			for (const FString& Name : TheFolder(UCataclysmSaveWriter::UserIndex))
			{
				if (!S.FolderAtStart.Contains(Name))
				{
					LeftBehind.Add(Name);
				}
			}
		}
		LeftBehind.Sort();
		UE_LOG(LogCataclysm, Display, TEXT("%s"), *FCataclysmTestSaves::SummaryLine(Deleted, LeftBehind));
	}
}

void FCataclysmTestSaves::NoteAWrite(const FString& SlotName, int32 UserIndex)
{
	// NOTHING AT ALL WHEN NO TEST RUNS, not even the existence check: a player's every save comes through here.
	if (!GIsAutomationTesting || SlotName.IsEmpty())
	{
		return;
	}
	if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex)) // A test's save: one that was there before is never noted.
	{
		return;
	}
	CataclysmTestSaves::FState& S = CataclysmTestSaves::State();
	FScopeLock Guard(&S.Lock);
	S.Noted.Add(SlotName);
}

void FCataclysmTestSaves::NoteABackup(const FString& BackupName, const FString& SourceSlot, int32 UserIndex)
{
	// NOTHING AT ALL WHEN NO TEST RUNS, not even the lock: a player's every migration comes through here.
	if (!GIsAutomationTesting)
	{
		return;
	}
	if (IsNoted(SourceSlot)) // A test's save: a backup is noted only with the slot it backs up.
	{
		NoteAWrite(BackupName, UserIndex);
	}
}

bool FCataclysmTestSaves::IsNoted(const FString& SlotName)
{
	CataclysmTestSaves::FState& S = CataclysmTestSaves::State();
	FScopeLock Guard(&S.Lock);
	return S.Noted.Contains(SlotName);
}

TArray<FString> FCataclysmTestSaves::DeleteWhatTheTestWrote(int32 UserIndex)
{
	CataclysmTestSaves::FState& S = CataclysmTestSaves::State();
	TArray<FString> Noted;
	{
		FScopeLock Guard(&S.Lock);
		Noted = S.Noted.Array();
		S.Noted.Reset();
	}
	TArray<FString> Deleted;
	for (const FString& Slot : Noted)
	{
		// WRITES IN FLIGHT FINISH FIRST, as every test's own `Delete` waits for them.
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(Slot) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(Slot, UserIndex) && UGameplayStatics::DeleteGameInSlot(Slot, UserIndex))
		{
			Deleted.Add(Slot);
		}
	}
	FScopeLock Guard(&S.Lock);
	S.WrittenAndDeleted += Deleted.Num();
	return Deleted;
}

FString FCataclysmTestSaves::SummaryLine(int32 WrittenAndDeleted, const TArray<FString>& LeftBehind)
{
	FString Line = FString::Printf(TEXT("Test saves: %d written and deleted, %d left behind"), WrittenAndDeleted,
								   LeftBehind.Num());
	if (!LeftBehind.IsEmpty())
	{
		// THE FIRST TEN NAMES, so a run that leaves thousands does not write a line of thousands.
		TArray<FString> First(LeftBehind.GetData(), FMath::Min(LeftBehind.Num(), 10));
		Line += TEXT(": ") + FString::Join(First, TEXT(", "));
		if (LeftBehind.Num() > First.Num())
		{
			Line += FString::Printf(TEXT(" and %d more"), LeftBehind.Num() - First.Num());
		}
	}
	return Line;
}

#if WITH_AUTOMATION_TESTS
namespace CataclysmTestSaves
{
	/**
	 * THE HOOKS, REGISTERED AS THE TESTS THEMSELVES ARE: a static object, made when the module loads, whose constructor
	 * adds to the automation framework's events, as `IMPLEMENT_SIMPLE_AUTOMATION_TEST` adds each test. The end of each
	 * test deletes what it wrote; the start of the session, or else the first test, takes the folder; the end of the
	 * session, or else the engine's exit, logs the totals.
	 */
	struct FHooks
	{
		FHooks()
		{
			FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
			Framework.OnBeforeAllTestsEvent.AddStatic(&TakeTheFolder);
			Framework.OnTestStartEvent.AddLambda([](FAutomationTestBase*) { TakeTheFolder(); });
			Framework.OnTestEndEvent.AddLambda([](FAutomationTestBase*)
			{
				FCataclysmTestSaves::DeleteWhatTheTestWrote(UCataclysmSaveWriter::UserIndex); // A test's save: deleted when it ends.
			});
			Framework.OnAfterAllTestsEvent.AddStatic(&LogTheSummary);
			FCoreDelegates::OnEnginePreExit.AddStatic(&LogTheSummary);
		}
	};
	static FHooks Hooks;
}
#endif
