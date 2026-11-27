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
#include "Tests/CataclysmTestRun.h"

/**
 * The run record at version 13: a city's build list and the days left on each build. Issue #2825, ruled 2026-11-21.
 * The upgrade step to version 13 (removed 2026-11-27) wrote an empty `Building` on every city and a `BuildDaysLeft` of 0 on every upgrade, since a
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
	const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
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
