// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

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
 * Whether the cleanse city upgrade has been bought in the run, saved since run record version 16. Issue #2838, ruled
 * by the coordinating session on 2026-11-22: once a run, empire-wide, held in a run flag.
 */

namespace CataclysmCityCleanseSaveTest
{
	const UCataclysmRunSave* ReadRunFixture(FAutomationTestBase& Test, const TCHAR* Name,
											ECataclysmSaveLoadResult& OutResult)
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Name, Text, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		FString Message;
		OutResult = ECataclysmSaveLoadResult::NotValidJson;
		const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), OutResult, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(OutResult), *Message));
		}
		return Read;
	}

	/** The run written out as a save writes it and read back into a fresh run; null, with the reason, on failure. */
	UCataclysmEmpireRun* SavedAndLoaded(const UCataclysmEmpireRun& Run, FString& OutWhy)
	{
		UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
		Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
		FCataclysmSaveGather::RunClockFrom(Run, *Record);
		FCataclysmSaveGather::CitiesFrom(Run, *Record);
		FCataclysmSaveGather::SurgeScheduleFrom(Run, *Record);
		FCataclysmSaveGather::ProgressFrom(Run, *Record);
		FCataclysmSaveGather::IdentityFrom(Run, *Record);
		FString Json;
		if (!FCataclysmSaveGather::DungeonsFrom(Run, *Record, OutWhy) || !FCataclysmSaveStorage::ToJson(Record, Json, OutWhy))
		{
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, OutWhy));
		UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
		return Read != nullptr && FCataclysmSaveApply::RunInto(*Read, *Loaded, OutWhy) ? Loaded : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityCleanseSaveTest,
	"Cataclysm.CityCleanseSave.WhetherTheCleanseWasBoughtIsSaved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A run at difficulty tier 3 that bought the cleanse on the Pillar, two days into its build, is saved and loaded: the
 * loaded run has bought it, and refuses it on another city. CONTROL: a run that bought nothing loads as not having
 * bought it. `Run_v22.json`, the committed run file, reads bought, the figure it states. Its half on `Run_v15.json`,
 * written before the flag, went with the save migrations on 2026-11-27 (issue #2928 slice 12), and the test's name
 * lost "AndAnOlderRunHasNotBoughtIt". Prints "CLEANSE SAVED".
 */
bool FCataclysmCityCleanseSaveTest::RunTest(const FString&)
{
	using namespace CataclysmCityCleanseSaveTest;

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(/* InSeed */ 2838, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* InDifficultyTier */ 3);
	UCataclysmEmpireRun* Untouched = NewObject<UCataclysmEmpireRun>();
	Untouched->Begin(/* InSeed */ 2838, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* InDifficultyTier */ 3);
	const int32 PillarId = Run->Map->PillarId;

	FCataclysmCityUpgrade Cleanse;
	Cleanse.RowName = FName(TEXT("Test_Cleanse"));
	Cleanse.Effect = ECataclysmCityUpgradeEffect::CleanseEveryCity;
	Cleanse.bOneTimeUse = true;
	const ECataclysmCityUpgradeResult Bought =
		Run->BuyCityUpgrade(PillarId, Cleanse, 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(1));
	Run->AdvanceDays(2);

	FString Why;
	FString UntouchedWhy;
	const UCataclysmEmpireRun* Loaded = SavedAndLoaded(*Run, Why);
	const UCataclysmEmpireRun* LoadedUntouched = SavedAndLoaded(*Untouched, UntouchedWhy);
	int32 OtherCity = INDEX_NONE;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (City.CityId != PillarId && !City.bFallen)
		{
			OtherCity = City.CityId;
			break;
		}
	}
	const ECataclysmCityUpgradeResult AfterTheLoad = Loaded != nullptr
		? Loaded->WouldBuyCityUpgrade(OtherCity, Cleanse, 0, 0)
		: ECataclysmCityUpgradeResult::RunHasNotBegun;

	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmRunSave* Committed = ReadRunFixture(*this, TEXT("Run_v22.json"), NewResult);

	AddInfo(FString::Printf(TEXT("CLEANSE SAVED: bought \"%s\"; loaded %s, bought %d, another city \"%s\"; untouched "
								 "loaded %s, bought %d; Run_v22.json %s, bought %d"),
							*UCataclysmCityUpgradeRules::ResultText(Bought), Loaded ? TEXT("yes") : *Why,
							Loaded && Loaded->bCleanseBoughtThisRun ? 1 : 0,
							*UCataclysmCityUpgradeRules::ResultText(AfterTheLoad),
							LoadedUntouched ? TEXT("yes") : *UntouchedWhy,
							LoadedUntouched && LoadedUntouched->bCleanseBoughtThisRun ? 1 : 0,
							FCataclysmSaveStorage::Describe(NewResult), Committed && Committed->bCleanseBoughtThisRun ? 1 : 0));

	TestEqual(TEXT("set-up: the cleanse is bought at difficulty tier 3"), Bought, ECataclysmCityUpgradeResult::Bought);
	if (!TestNotNull(TEXT("the run is saved and loaded"), Loaded)
		|| !TestNotNull(TEXT("and so is the run that bought nothing"), LoadedUntouched)
		|| !TestNotNull(TEXT("Run_v22.json loads"), Committed))
	{
		return false;
	}
	TestTrue(TEXT("the loaded run has bought the cleanse"), Loaded->bCleanseBoughtThisRun);
	TestEqual(TEXT("and refuses it on another city"), AfterTheLoad, ECataclysmCityUpgradeResult::AlreadyBoughtThisRun);
	TestFalse(TEXT("CONTROL: a run that bought nothing loads as not having bought it"),
			  LoadedUntouched->bCleanseBoughtThisRun);
	TestEqual(TEXT("Run_v22.json is read"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestTrue(TEXT("and states the cleanse bought"), Committed->bCleanseBoughtThisRun);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
