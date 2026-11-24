// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"

/**
 * The run record at version 13: a city's build list and the days left on each build. Issue #2825, ruled 2026-11-21.
 * `Migrate_12_to_13` writes an empty `Building` on every city and a `BuildDaysLeft` of 0 on every upgrade, since a
 * version 12 run could build nothing; a build under way is saved with its days left and goes on after a load.
 */

namespace CataclysmCityUpgradeBuildSaveTest
{
	UCataclysmRunSave* ReadRunText(FAutomationTestBase& Test, const FString& Text, const TCHAR* Name)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(Result), *Message));
		}
		return Read;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildSaveMigrationTest,
	"Cataclysm.CityUpgradeBuildSave.ARunFileFromVersion12MigratesBuildingNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v12.json`, written before version 13, migrates with neither city building anything and both its upgrades
 * finished, every other field kept. CONTROL: the committed run file, `Run_v18.json` since issue #2864 (`Run_v17.json` from issue #2840, `Run_v16.json` from issue #2838, `Run_v15.json` from issue #2741 slice 9), reads its first city building
 * `Architect_Increase_max_defense_by_20` with 4 days left and its second building nothing. Prints "BUILD MIGRATED".
 */
bool FCataclysmCityUpgradeBuildSaveMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildSaveTest;

	FString Text;
	FString CurrentText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v12.json"), Text, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Run_v18.json"), CurrentText, Reason))
	{
		AddError(Reason);
		return false;
	}
	const UCataclysmRunSave* Migrated = ReadRunText(*this, Text, TEXT("Run_v12.json"));
	const UCataclysmRunSave* Current = ReadRunText(*this, CurrentText, TEXT("Run_v18.json"));
	if (!Migrated || !Current || !TestEqual(TEXT("set-up: Run_v12.json has two cities"), Migrated->Cities.Num(), 2)
		|| !TestEqual(TEXT("set-up: Run_v18.json has two cities"), Current->Cities.Num(), 2))
	{
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 12 file at the current version with what the step writes, compared with what
	// the record writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion13Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion14Defaults(*Expected); // And 14, #46 G3 and G5: socket lists, no gem on the floor.
	CataclysmSaveFixtures::AddTheRunsVersion15Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheRunsVersion16Defaults(*Expected); // And issue #2838's step.
	CataclysmSaveFixtures::AddTheRunsVersion17Defaults(*Expected); // And issue #2840's step.
	CataclysmSaveFixtures::AddTheRunsVersion18Defaults(*Expected); // And issue #2864's step.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Migrated, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	int32 MigratedUpgrades = 0;
	int32 MigratedDaysLeft = 0;
	for (const FCataclysmCity& City : Migrated->Cities)
	{
		for (const FCataclysmCityUpgrade& Upgrade : City.Upgrades)
		{
			++MigratedUpgrades;
			MigratedDaysLeft += Upgrade.BuildDaysLeft;
		}
	}
	const TArray<FCataclysmCityUpgrade>& CurrentBuild = Current->Cities[0].Building;

	AddInfo(FString::Printf(TEXT("BUILD MIGRATED: version %d; building %d and %d; %d upgrade(s) with %d day(s) left in "
								 "all; the rest %s; Run_v18.json building %d (%s, %d left) and %d"),
							Migrated->SchemaVersion, Migrated->Cities[0].Building.Num(),
							Migrated->Cities[1].Building.Num(), MigratedUpgrades, MigratedDaysLeft,
							bSame ? TEXT("kept") : *Where, CurrentBuild.Num(),
							CurrentBuild.IsEmpty() ? TEXT("") : *CurrentBuild[0].RowName.ToString(),
							CurrentBuild.IsEmpty() ? -1 : CurrentBuild[0].BuildDaysLeft,
							Current->Cities[1].Building.Num()));

	TestEqual(TEXT("it arrives at the current version"), Migrated->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestTrue(TEXT("its first city builds nothing"), Migrated->Cities[0].Building.IsEmpty());
	TestTrue(TEXT("nor its second"), Migrated->Cities[1].Building.IsEmpty());
	TestEqual(TEXT("set-up: it holds two upgrades"), MigratedUpgrades, 2);
	TestEqual(TEXT("both finished"), MigratedDaysLeft, 0);
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestEqual(TEXT("CONTROL: Run_v18.json's first city builds one upgrade"), CurrentBuild.Num(), 1);
	TestTrue(TEXT("CONTROL: the raised maximum, with 4 days left"),
			 CurrentBuild.Num() == 1 && CurrentBuild[0].RowName == FName(TEXT("Architect_Increase_max_defense_by_20"))
				 && CurrentBuild[0].BuildDaysLeft == 4);
	TestTrue(TEXT("CONTROL: its second builds nothing"), Current->Cities[1].Building.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildSaveRestoreTest,
	"Cataclysm.CityUpgradeBuildSave.ABuildUnderWayIsSavedAndGoesOnAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A raised maximum bought on the Pillar with 6 days to build, two days into it: written out by the save and read back
 * into a fresh run, it is still being built with 4 days left, and four more days in the loaded run finish it. Prints
 * "BUILD SAVED".
 */
bool FCataclysmCityUpgradeBuildSaveRestoreTest::RunTest(const FString&)
{
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(/* InSeed */ 2825, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
	const int32 PillarId = Run->Map->PillarId;

	FCataclysmCityUpgrade Walls;
	Walls.RowName = FName(TEXT("Test_Walls"));
	Walls.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
	Walls.Value = 0.2f;
	Run->BuyCityUpgrade(PillarId, Walls, 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(1));
	Run->AdvanceDays(2);

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	FString Why;
	FCataclysmSaveGather::RunClockFrom(*Run, *Record);
	FCataclysmSaveGather::CitiesFrom(*Run, *Record);
	FCataclysmSaveGather::SurgeScheduleFrom(*Run, *Record);
	FCataclysmSaveGather::ProgressFrom(*Run, *Record);
	FCataclysmSaveGather::IdentityFrom(*Run, *Record);
	FString Json;
	const bool bWritten = FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why)
		&& FCataclysmSaveStorage::ToJson(Record, Json, Why);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	UCataclysmRunSave* Read = bWritten ? Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why)) : nullptr;
	if (!TestNotNull(TEXT("set-up: the record was written and read back"), Read))
	{
		AddError(Why);
		return false;
	}

	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	const bool bLoaded = FCataclysmSaveApply::RunInto(*Read, *Loaded, Why);
	const FCataclysmCityUpgrade* Build = bLoaded ? Loaded->BuildOn(PillarId) : nullptr;
	const int32 LeftAfterTheLoad = Build != nullptr ? Build->BuildDaysLeft : -1;
	const bool bJsonNamesIt = Json.Contains(TEXT("\"Building\"")) && Json.Contains(TEXT("\"BuildDaysLeft\": 4"));

	if (bLoaded)
	{
		Loaded->AdvanceDays(4);
	}
	const FCataclysmCity* City = bLoaded ? Loaded->Map->Find(PillarId) : nullptr;

	AddInfo(FString::Printf(TEXT("BUILD SAVED: loaded %s; the record names the build %s; %d day(s) left after the load; "
								 "four days later %d finished, %d building"),
							bLoaded ? TEXT("yes") : *Why, bJsonNamesIt ? TEXT("yes") : TEXT("NO"), LeftAfterTheLoad,
							City ? City->Upgrades.Num() : -1, City ? City->Building.Num() : -1));

	TestTrue(TEXT("set-up: it loaded"), bLoaded);
	TestTrue(TEXT("the record holds the build and its days left"), bJsonNamesIt);
	TestEqual(TEXT("the loaded run is still building it, with 4 days left"), LeftAfterTheLoad, 4);
	TestTrue(TEXT("four more days finish it"), City != nullptr && City->Upgrades.Num() == 1 && City->Building.IsEmpty());
	TestTrue(TEXT("CONTROL: no dungeon ever stood on the Pillar"), bLoaded && Loaded->DungeonsOn(PillarId).IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
