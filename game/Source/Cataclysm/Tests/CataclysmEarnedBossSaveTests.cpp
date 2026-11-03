// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"

/**
 * The run record keeps its Cataclysm boss dungeon, at version 4. Issue #1324
 * slice 6b, 2026-11-02.
 *
 * WHAT IS KEPT: whether it has opened, the day it opened and the floors it
 * rolled (`UCataclysmRunSave::bCataclysmDungeonOpened` and the two beside it).
 * Its depth is worked out from the roll and `BasicDungeonsCleared`, which was
 * saved already, and the dungeon itself is in `Dungeons` with a timer like any
 * other. `Migrate_3_to_4` writes a boss not yet opened into a version 3 file.
 *
 * `Cataclysm.EarnedBossSave.` IS A NEW GROUP and selects these and nothing else;
 * the empire half is `Cataclysm.EarnedBoss.`, in the `CataclysmEmpire` module.
 */
namespace CataclysmEarnedBossSaveTest
{
	/** One dungeon of this kind, built by hand on the first city and cleared
	 *  through `ClearDungeon`. It draws nothing. */
	void ClearOneByHand(UCataclysmEmpireRun& Run, ECataclysmDungeonType Kind,
						ECataclysmType Sender)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId++;
		Dungeon.Type = Kind;
		Dungeon.Cataclysm = Sender;
		Dungeon.CityId = Run.Map->Cities[0].CityId;

		Run.Dungeons.Add(Dungeon);
		Run.ClearDungeon(Dungeon.DungeonId);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEarnedBossSaveRoundTripTest,
	"Cataclysm.EarnedBossSave.AnOpenedBossSurvivesTheRecordItsJsonAndARestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A tier 1 run defeats three ordinary dungeons, finishes its one Cataclysm, which
 * opens the boss, and defeats one more ordinary dungeon. It is gathered as the
 * writer gathers it, written to JSON, read back and put into a new run with
 * `FCataclysmSaveApply::RunInto`: the three fields, the depth, the boss on no
 * city and its timer all come back, and a quest clear on the restored run opens
 * no second boss. CONTROL: a run that never opened one comes back with none.
 * Prints "BOSS SAVED".
 */
bool FCataclysmEarnedBossSaveRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEarnedBossSaveTest;

	const auto SavedAndRestored = [this](const UCataclysmEmpireRun& Live) -> UCataclysmEmpireRun*
	{
		UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
		Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
		FString Why;
		FCataclysmSaveGather::RunClockFrom(Live, *Record);
		FCataclysmSaveGather::CitiesFrom(Live, *Record);
		FCataclysmSaveGather::SurgeScheduleFrom(Live, *Record);
		FCataclysmSaveGather::ProgressFrom(Live, *Record);
		FCataclysmSaveGather::IdentityFrom(Live, *Record);
		FString Json;
		if (!FCataclysmSaveGather::DungeonsFrom(Live, *Record, Why) || !FCataclysmSaveStorage::ToJson(Record, Json, Why))
		{
			AddError(FString::Printf(TEXT("set-up: the run could not be written: %s"), *Why));
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		const UCataclysmRunSave* Back = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why));
		UCataclysmEmpireRun* Restored = NewObject<UCataclysmEmpireRun>();
		if (Back == nullptr || !FCataclysmSaveApply::RunInto(*Back, *Restored, Why))
		{
			AddError(FString::Printf(TEXT("set-up: the run could not be read back and restored: %s"), *Why));
			return nullptr;
		}
		return Restored;
	};

	const auto Begun = []()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* Seed */ 11, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* DifficultyTier */ 1);
		Run->AdvanceDays(4);
		return Run;
	};

	UCataclysmEmpireRun* Live = Begun();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ClearOneByHand(*Live, ECataclysmDungeonType::Basic, ECataclysmType::None);
	}
	const ECataclysmType Only = Live->ActiveCataclysms[0];
	for (int32 Index = 0; Index < UCataclysmRoster::QuestObjectivesFor(Only); ++Index)
	{
		ClearOneByHand(*Live, ECataclysmDungeonType::Quest, Only);
	}
	ClearOneByHand(*Live, ECataclysmDungeonType::Basic, ECataclysmType::None);

	const FCataclysmDungeon* LiveBoss = Live->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: the live run opened its boss"), LiveBoss)
		|| !TestEqual(TEXT("set-up: four ordinary dungeons were defeated"), Live->BasicDungeonsCleared, 4))
	{
		return false;
	}
	const int32 BossId = LiveBoss->DungeonId;
	const int32 LiveFloors = LiveBoss->Floors;

	UCataclysmEmpireRun* Restored = SavedAndRestored(*Live);
	UCataclysmEmpireRun* Unopened = Begun();
	UCataclysmEmpireRun* RestoredUnopened = SavedAndRestored(*Unopened);
	if (!TestNotNull(TEXT("set-up: the run with a boss was restored"), Restored)
		|| !TestNotNull(TEXT("set-up: the control was restored"), RestoredUnopened))
	{
		return false;
	}

	const FCataclysmDungeon* Boss = Restored->FindDungeon(BossId);
	const FCataclysmDungeonTimer* Timer = Restored->Clock->FindTimer(BossId);

	AddInfo(FString::Printf(
		TEXT("BOSS SAVED: opened %s on day %d, rolled %d, %d now against %d live, the "
			 "dungeon %s on city %d, timer %.1f; the control opened %s"),
		Restored->bCataclysmDungeonOpened ? TEXT("yes") : TEXT("no"), Restored->CataclysmDungeonOpenedDay,
		Restored->CataclysmDungeonBaseFloors, Restored->CataclysmDungeonFloorsNow(), LiveFloors,
		Boss ? TEXT("standing") : TEXT("GONE"), Boss ? Boss->CityId : -2,
		Timer ? Timer->DaysUntilResolve : -1.0f,
		RestoredUnopened->bCataclysmDungeonOpened ? TEXT("yes") : TEXT("no")));

	TestTrue(TEXT("it comes back opened"), Restored->bCataclysmDungeonOpened);
	TestEqual(TEXT("on the day it opened"), Restored->CataclysmDungeonOpenedDay, Live->CataclysmDungeonOpenedDay);
	TestEqual(TEXT("with the floors it rolled"), Restored->CataclysmDungeonBaseFloors,
			  Live->CataclysmDungeonBaseFloors);
	TestEqual(TEXT("and as deep as it was"), Restored->CataclysmDungeonFloorsNow(), LiveFloors);
	TestTrue(TEXT("the dungeon stands with its floors"), Boss != nullptr && Boss->Floors == LiveFloors);
	TestTrue(TEXT("on no city"), Boss != nullptr && Boss->CityId == INDEX_NONE);
	TestTrue(TEXT("with its timer unchanged by the JSON"),
			 Timer != nullptr && Timer->DaysUntilResolve == UCataclysmSurgeScheduler::NeverResolveDays);

	// AND THE RESTORED RUN DOES NOT OPEN A SECOND ONE.
	const int32 StandingBefore = Restored->DungeonCount();
	ClearOneByHand(*Restored, ECataclysmDungeonType::Quest, Only);
	TestEqual(TEXT("a quest clear on the restored run opens no second boss"), Restored->DungeonCount(),
			  StandingBefore);

	TestFalse(TEXT("CONTROL: a run that never opened one comes back with none"),
			  RestoredUnopened->bCataclysmDungeonOpened);
	TestEqual(TEXT("CONTROL: and no depth"), RestoredUnopened->CataclysmDungeonFloorsNow(), 0);
	TestNull(TEXT("CONTROL: and no boss dungeon"), RestoredUnopened->FindCataclysmDungeon());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEarnedBossSaveMigrationTest,
	"Cataclysm.EarnedBossSave.ARunFileFromBeforeTheBossDungeonMigratesUnopened",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v3.json`, written before the run kept its boss dungeon, through the chain:
 * `Migrate_3_to_4` writes the three fields with their defaults, one assertion
 * each; the record arrives at the current version with no boss opened; and every
 * other field is kept. Prints "BOSS MIGRATED".
 */
bool FCataclysmEarnedBossSaveMigrationTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v3.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v3.json is a JSON object"), Stepped.IsValid())
		|| !TestFalse(TEXT("set-up: and holds no boss dungeon"), Stepped->HasField(TEXT("bCataclysmDungeonOpened"))))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);

	bool bOpened = true;
	double Day = -1.0;
	double Floors = -1.0;
	const bool bWroteOpened = Stepped->TryGetBoolField(TEXT("bCataclysmDungeonOpened"), bOpened) && !bOpened;
	const bool bWroteDay = Stepped->TryGetNumberField(TEXT("CataclysmDungeonOpenedDay"), Day) && Day == 0.0;
	const bool bWroteFloors = Stepped->TryGetNumberField(TEXT("CataclysmDungeonBaseFloors"), Floors) && Floors == 0.0;

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v3.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// AND NOTHING ELSE MOVED: the version 3 file with the version and the three keys, compared with what the record
	// writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	Expected->SetBoolField(TEXT("bCataclysmDungeonOpened"), false);
	Expected->SetNumberField(TEXT("CataclysmDungeonOpenedDay"), 0);
	Expected->SetNumberField(TEXT("CataclysmDungeonBaseFloors"), 0);
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("BOSS MIGRATED: the step %d (%s); wrote opened %s, day %s, floors %s; the record at "
								 "version %d, opened %s; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWroteOpened ? TEXT("yes") : TEXT("NO"),
							bWroteDay ? TEXT("yes") : TEXT("NO"), bWroteFloors ? TEXT("yes") : TEXT("NO"),
							Read->SchemaVersion, Read->bCataclysmDungeonOpened ? TEXT("yes") : TEXT("no"),
							bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 3 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes the boss not opened"), bWroteOpened);
	TestTrue(TEXT("the step writes day 0"), bWroteDay);
	TestTrue(TEXT("the step writes no floors"), bWroteFloors);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestFalse(TEXT("with no boss opened"), Read->bCataclysmDungeonOpened);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEarnedBossSaveCommittedFileTest,
	"Cataclysm.EarnedBossSave.TheCommittedRunFileReadsIntoTheBossDungeonItStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v4.json` reads into the three values it states, none of them a default, so
 * each is read and not re-defaulted. Prints "BOSS FILE".
 */
bool FCataclysmEarnedBossSaveCommittedFileTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v4.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v4.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	AddInfo(FString::Printf(TEXT("BOSS FILE: version %d; opened %s on day %d, rolled %d"),
							Read->SchemaVersion, Read->bCataclysmDungeonOpened ? TEXT("yes") : TEXT("no"),
							Read->CataclysmDungeonOpenedDay, Read->CataclysmDungeonBaseFloors));

	TestEqual(TEXT("it says it is version 4"), Read->SchemaVersion, 4);
	TestTrue(TEXT("the boss opened"), Read->bCataclysmDungeonOpened);
	TestEqual(TEXT("on day 109"), Read->CataclysmDungeonOpenedDay, 109);
	TestEqual(TEXT("having rolled 137 floors"), Read->CataclysmDungeonBaseFloors, 137);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
