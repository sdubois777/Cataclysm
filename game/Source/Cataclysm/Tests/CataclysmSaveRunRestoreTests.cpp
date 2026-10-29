// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "UObject/Package.h"

namespace CataclysmSaveRunRestoreTest
{
	/** The seed, mode, rung and tier every run in these tests is begun with. */
	constexpr int32 Seed = 4242;
	constexpr int32 Rung = 2;
	constexpr int32 Tier = 4;

	/** A run begun as the game instance begins one, with its modifier pool, and advanced `Days` days. */
	UCataclysmEmpireRun* PlayedRun(int32 Days)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();
		Run->Begin(Seed, ECataclysmSurgeMode::Both, Rung, Tier);
		for (int32 Day = 0; Day < Days; ++Day)
		{
			Run->AdvanceDay();
		}
		return Run;
	}

	/** The record the writer would make of `Run`, written to JSON and read back. Null, with `OutWhy`, if it fails. */
	UCataclysmRunSave* SavedAndReadBack(const UCataclysmEmpireRun& Run, FString& OutWhy)
	{
		UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
		Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
		FCataclysmSaveGather::RunClockFrom(Run, *Record);
		FCataclysmSaveGather::CitiesFrom(Run, *Record);
		FCataclysmSaveGather::SurgeScheduleFrom(Run, *Record);
		FCataclysmSaveGather::ProgressFrom(Run, *Record);
		FCataclysmSaveGather::IdentityFrom(Run, *Record);
		if (!FCataclysmSaveGather::DungeonsFrom(Run, *Record, OutWhy))
		{
			return nullptr;
		}
		FString Json;
		if (!FCataclysmSaveStorage::ToJson(Record, Json, OutWhy))
		{
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		return Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, OutWhy));
	}

	/** What a player sees of the run: the map as drawn, and every dungeon on it. */
	FString VisibleValues(const UCataclysmEmpireRun& Run)
	{
		FString Out = Run.Map->Render();
		for (const FCataclysmDungeon& Dungeon : Run.Dungeons)
		{
			Out += FString::Printf(TEXT("|d%d c%d f%d s%d k%d"), Dungeon.DungeonId, Dungeon.CityId, Dungeon.Floors,
								   static_cast<int32>(Dungeon.SubType), static_cast<int32>(Dungeon.Cataclysm));
			for (const FName& Modifier : Dungeon.Modifiers)
			{
				Out += TEXT(" ") + Modifier.ToString();
			}
		}
		return Out;
	}

	/** Everything else the run holds that decides what it does next: the clock, the scheduler, the streams, the
	 *  tallies, the identity, and each city's saved fields. */
	FString InternalValues(const UCataclysmEmpireRun& Run)
	{
		FString Out = FString::Printf(TEXT("day %d %.4f cur %d owed %d | surge %d %d %.4f %d | streams %d %d %d stalker %d"
										   " | next %d tier %d slots %d"),
									  Run.Clock->Day, Run.Clock->PartialDay, Run.Clock->CurrentDungeonId, Run.OwedDays,
									  Run.Surges->SurgeIndex, Run.Surges->SurgesFired, Run.Surges->NextSurgeDay,
									  Run.Surges->LastSurgeDay, Run.Stream.GetCurrentSeed(),
									  Run.CataclysmStream.GetCurrentSeed(), Run.ModifierStream.GetCurrentSeed(),
									  Run.CorruptedStalkerSeed, Run.NextDungeonId, Run.DifficultyTier,
									  Run.Map->UpgradeSlots);
		for (const FCataclysmDungeonTimer& Timer : Run.Clock->Timers)
		{
			Out += FString::Printf(TEXT("|t%d %d %.4f %.4f %d"), Timer.DungeonId, Timer.Floors, Timer.DaysUntilResolve,
								   Timer.ResolveDays, Timer.TimesResolved);
		}
		Out += FString::Printf(TEXT(" | tallies %d %d %d %d %d %d | hit %d %d %d move %d %d %d | faces"),
							   Run.QuestObjectives, Run.BasicDungeonsCleared, Run.DungeonsCleared, Run.DungeonsDetonated,
							   Run.QuestHits, Run.SurgesFromEmptyBoard, Run.LastHitDay, Run.LastHitCityId,
							   Run.bLastHitWasAQuestTimer ? 1 : 0, Run.LastMoveDay, Run.LastMoveFromCityId,
							   Run.LastMoveToCityId);
		for (const ECataclysmType Type : Run.ActiveCataclysms)
		{
			Out += FString::Printf(TEXT(" %d"), static_cast<int32>(Type));
		}
		// THE WHOLE MAP, KEYS INCLUDED, in key order so two maps holding the same entries print the same.
		TArray<ECataclysmType> Keys;
		Run.QuestObjectivesByCataclysm.GetKeys(Keys);
		Keys.Sort();
		Out += TEXT(" | by Cataclysm");
		for (const ECataclysmType Key : Keys)
		{
			Out += FString::Printf(TEXT(" %d=%d"), static_cast<int32>(Key), Run.QuestObjectivesByCataclysm.FindRef(Key));
		}
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Out += FString::Printf(TEXT("|c%d %.3f %.3f %.3f %.3f %d%d%d v%d e%s %d u%d"), City.CityId, City.MaxDefence,
								   City.MaxPopulation, City.Defence, City.Population, City.bFallen ? 1 : 0,
								   City.bDoomed ? 1 : 0, City.bErased ? 1 : 0, City.VoidStacks,
								   *City.LeftEdict.ToString(), City.LeftEdictUntilDay, City.Upgrades.Num());
			for (const FCataclysmCityUpgrade& Upgrade : City.Upgrades)
			{
				Out += FString::Printf(TEXT(" %s@%d"), *Upgrade.RowName.ToString(), Upgrade.NextTriggerDay);
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRunRestoreAdvancesTheSameTest,
	"Cataclysm.SaveApply.ARestoredRunAdvancesExactlyAsTheRunItWasSavedFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #753, its first step. A run played 60 days is saved, written to JSON, read back and put into a new run with
 * `FCataclysmSaveApply::RunInto`; the two then advance 200 days side by side and agree on every compared value on
 * every day, from day 0 before either moves. CONTROL: a second run begun the same way and played the same 60 days
 * agrees with the first on every value too, so the run reads nothing outside its seed, and a restored run that
 * differed would be the restore's fault.
 */
bool FCataclysmSaveRunRestoreAdvancesTheSameTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSaveRunRestoreTest;
	constexpr int32 PlayedDays = 60;
	constexpr int32 ComparedDays = 200;

	UCataclysmEmpireRun* Live = PlayedRun(PlayedDays);
	UCataclysmEmpireRun* Twin = PlayedRun(PlayedDays);
	if (!TestTrue(TEXT("set-up: the modifier table loaded"), Live->ModifierPool.Num() > 0))
	{
		return false;
	}

	// SET-UP: THE RUN HAS USED WHAT THE RESTORE MUST CARRY. A surge has fired and left its day; a dungeon carries
	// modifiers, so the modifier stream has moved; the Cataclysm stream has moved off its starting point.
	bool bAnyModifiers = false;
	for (const FCataclysmDungeon& Dungeon : Live->Dungeons)
	{
		bAnyModifiers |= Dungeon.Modifiers.Num() > 0;
	}
	FRandomStream CataclysmStart;
	CataclysmStart.Initialize(UCataclysmRoster::MixedSeed(Seed, UCataclysmRoster::WaveSalt));
	if (!TestTrue(TEXT("set-up: a surge has fired"), Live->Surges->SurgesFired > 0)
		|| !TestTrue(TEXT("set-up: and left its day"), Live->Surges->LastSurgeDay > 0)
		|| !TestTrue(TEXT("set-up: a dungeon carries modifiers"), bAnyModifiers)
		|| !TestTrue(TEXT("set-up: the Cataclysm stream has moved"),
					 Live->CataclysmStream.GetCurrentSeed() != CataclysmStart.GetCurrentSeed()))
	{
		return false;
	}

	FString Why;
	UCataclysmRunSave* Back = SavedAndReadBack(*Live, Why);
	if (!TestNotNull(TEXT("set-up: the run is saved and read back"), Back))
	{
		AddError(Why);
		return false;
	}
	UCataclysmEmpireRun* Restored = NewObject<UCataclysmEmpireRun>();
	if (!TestTrue(TEXT("set-up: the record is put back into a run"), FCataclysmSaveApply::RunInto(*Back, *Restored, Why)))
	{
		AddError(Why);
		return false;
	}

	int32 RestoredInternal = INDEX_NONE;
	int32 RestoredVisible = INDEX_NONE;
	int32 TwinDiffers = INDEX_NONE;
	FString FirstDifference;
	for (int32 Day = 0; Day <= ComparedDays; ++Day)
	{
		if (Day > 0)
		{
			Live->AdvanceDay();
			Restored->AdvanceDay();
			Twin->AdvanceDay();
		}
		const FString LiveInternal = InternalValues(*Live);
		const FString LiveVisible = VisibleValues(*Live);
		if (RestoredInternal == INDEX_NONE && InternalValues(*Restored) != LiveInternal)
		{
			RestoredInternal = Day;
			FirstDifference = FString::Printf(TEXT("live [%s] restored [%s]"), *LiveInternal.Left(300),
											  *InternalValues(*Restored).Left(300));
		}
		if (RestoredVisible == INDEX_NONE && VisibleValues(*Restored) != LiveVisible)
		{
			RestoredVisible = Day;
		}
		if (TwinDiffers == INDEX_NONE && (InternalValues(*Twin) != LiveInternal || VisibleValues(*Twin) != LiveVisible))
		{
			TwinDiffers = Day;
		}
	}

	AddInfo(FString::Printf(TEXT("RUN RESTORE: after %d days played, over %d days compared the restored run first differs "
								 "on day %d in its internal values and on day %d in what is visible; the twin run on "
								 "day %d (-1 is never); %d surges fired, last on day %d; %d dungeons"),
							PlayedDays, ComparedDays, RestoredInternal, RestoredVisible, TwinDiffers,
							Live->Surges->SurgesFired, Live->Surges->LastSurgeDay, Live->DungeonCount()));
	if (RestoredInternal != INDEX_NONE)
	{
		AddInfo(FString::Printf(TEXT("RUN RESTORE FIRST DIFFERENCE: %s"), *FirstDifference));
	}
	TestEqual(TEXT("the restored run agrees with the live one on every internal value on every day"), RestoredInternal,
			  static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and on everything visible"), RestoredVisible, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("CONTROL: a twin begun and played the same way agrees with the live run on every day"), TwinDiffers,
			  static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRunRestoreRefusesTest,
	"Cataclysm.SaveApply.ARunRecordThatCannotBeRestoredChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #753. A record whose dungeons and timers disagree is refused with a reason, and the run it was handed is left
 * exactly as it was. CONTROL: the same record with the two agreeing is accepted.
 */
bool FCataclysmSaveRunRestoreRefusesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSaveRunRestoreTest;

	UCataclysmEmpireRun* Played = PlayedRun(10);
	FString Why;
	UCataclysmRunSave* Back = SavedAndReadBack(*Played, Why);
	if (!TestNotNull(TEXT("set-up: the run is saved and read back"), Back)
		|| !TestTrue(TEXT("set-up: it holds a dungeon and its timer"), Back->DungeonTimers.Num() > 0))
	{
		AddError(Why);
		return false;
	}

	UCataclysmRunSave* Broken = DuplicateObject<UCataclysmRunSave>(Back, GetTransientPackage());
	Broken->DungeonTimers.Pop();

	UCataclysmEmpireRun* Target = PlayedRun(3);
	const FString Before = InternalValues(*Target) + VisibleValues(*Target);
	FString Refusal;
	const bool bRefusedAccepted = FCataclysmSaveApply::RunInto(*Broken, *Target, Refusal);
	const FString After = InternalValues(*Target) + VisibleValues(*Target);

	UCataclysmEmpireRun* Accepting = PlayedRun(3);
	FString AcceptWhy;
	const bool bAccepted = FCataclysmSaveApply::RunInto(*Back, *Accepting, AcceptWhy);

	AddInfo(FString::Printf(TEXT("RUN REFUSAL: a record with %d dungeons and %d timers was %s (\"%s\"); the run handed "
								 "over %s; the same record whole was %s"),
							Broken->Dungeons.Num(), Broken->DungeonTimers.Num(),
							bRefusedAccepted ? TEXT("accepted") : TEXT("refused"), *Refusal,
							Before == After ? TEXT("is unchanged") : TEXT("CHANGED"),
							bAccepted ? TEXT("accepted") : TEXT("refused")));
	TestFalse(TEXT("a record whose dungeons and timers disagree is refused"), bRefusedAccepted);
	TestFalse(TEXT("with a reason"), Refusal.IsEmpty());
	TestEqual(TEXT("and the run it was handed is unchanged"), After, Before);
	TestTrue(TEXT("CONTROL: the same record with the two agreeing is accepted"), bAccepted);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
