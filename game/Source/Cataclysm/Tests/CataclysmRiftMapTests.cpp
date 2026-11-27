// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The capital map shows each Rift and the days until it next sends a dungeon: issue #51 slice B2. A city holding a Rift
 * says so on its box ("Bulwark 80%, Rift"), and the city's detail line names the Rift and its next send ("Infernal Rift
 * of 4 floors, 12 days left, sends a dungeon in 5 days"). Every system needs a basic UI.
 *
 * EACH HAS A CONTROL: a Quest dungeon another Cataclysm sent, which is no Rift and reads as it always did.
 */
namespace CataclysmRiftMapTest
{
	UCataclysmEmpireRun* MakeEmptyRun()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(1, ECataclysmSurgeMode::Static);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	void PlaceQuest(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmType Sender, int32 CityId)
	{
		const FCataclysmCity* City = Run.Map->Find(CityId);
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = ECataclysmDungeonType::Quest;
		Dungeon.SubType = Sender == ECataclysmType::Demonic ? ECataclysmDungeonSubType::Elite
															: ECataclysmDungeonSubType::Horde;
		Dungeon.Cataclysm = Sender;
		Dungeon.CityId = CityId;
		Dungeon.CityTier = City->Tier;
		Dungeon.Floors = 4;
		Dungeon.ResolveDays = 12.0f;
		Dungeon.SpawnedDay = Run.Day();
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, Dungeon.ResolveDays);
	}

	/** An ordinary dungeon that never runs out, on the Pillar, so no empty-board wave lands while days pass. */
	void HoldTheBoard(UCataclysmEmpireRun& Run)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = 9999;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.CityId = Run.Map->PillarId;
		Dungeon.CityTier = ECataclysmCityTier::Pillar;
		Dungeon.Floors = 4;
		Dungeon.ResolveDays = 10000.0f;
		Dungeon.SpawnedDay = Run.Day();
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays);
	}

	int32 FirstOutpost(const UCataclysmEmpireRun& Run, int32 Skip = 0)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == ECataclysmCityTier::Outpost && Skip-- <= 0)
			{
				return City.CityId;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftMapLineTest,
	"Cataclysm.RiftMap.ARiftsDetailLineNamesItAndTheDaysUntilItSends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftMapLineTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftMapTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	PlaceQuest(*Run, 1, ECataclysmType::Demonic, FirstOutpost(*Run));
	// PESTILENCE AND NOT DEATH: since slice B5 (bundle 4) a Death Quest dungeon is an Abomination lair, with a line of
	// its own, and this control wants a Quest dungeon read as one always was. Window b4-w1 found it.
	PlaceQuest(*Run, 2, ECataclysmType::Pestilence, FirstOutpost(*Run, 1));

	const auto Expected = [Run](int32 Sends)
	{
		return FString::Printf(TEXT("Infernal Rift of 4 floors, %.0f days left, sends a dungeon in %d day%s"),
							   Run->Clock->DaysUntilResolveFor(1), Sends, Sends == 1 ? TEXT("") : TEXT("s"));
	};

	const FString Opened = UCataclysmEmpireMapLayout::CityDetailDungeonLine(Run, 1);
	AddInfo(FString::Printf(TEXT("RIFT LINE: \"%s\""), *Opened));
	TestEqual(TEXT("on the day it opened it sends in five days"), Opened, Expected(5));

	Run->AdvanceDays(4);
	TestEqual(TEXT("four days on, in one day"), UCataclysmEmpireMapLayout::CityDetailDungeonLine(Run, 1), Expected(1));
	Run->AdvanceDay();
	TestEqual(TEXT("and the day it sent, in five again"), UCataclysmEmpireMapLayout::CityDetailDungeonLine(Run, 1),
			  Expected(5));

	// CONTROL: a Quest dungeon Pestilence sent reads as a Quest dungeon always did, with no send.
	const FString Horde = UCataclysmEmpireMapLayout::CityDetailDungeonLine(Run, 2);
	TestTrue(TEXT("a Quest dungeon that is no Rift is not called one"), !Horde.Contains(TEXT("Rift"))
		&& !Horde.Contains(TEXT("sends")) && Horde.Contains(TEXT("Quest dungeon of 4 floors")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftMapMarkTest,
	"Cataclysm.RiftMap.ACityHoldingRiftsSaysSoOnItsBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftMapMarkTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftMapTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	const int32 Outpost = FirstOutpost(*Run);
	TestEqual(TEXT("nothing with no Rift"), UCataclysmEmpireMapLayout::RiftMark(Run, Outpost), FString());

	// CONTROL FIRST: a Quest dungeon Death sent marks nothing.
	PlaceQuest(*Run, 1, ECataclysmType::Death, Outpost);
	TestEqual(TEXT("a Quest dungeon that is no Rift marks nothing"), UCataclysmEmpireMapLayout::RiftMark(Run, Outpost),
			  FString());

	PlaceQuest(*Run, 2, ECataclysmType::Demonic, Outpost);
	TestEqual(TEXT("one Rift"), UCataclysmEmpireMapLayout::RiftMark(Run, Outpost), FString(TEXT(", Rift")));
	PlaceQuest(*Run, 3, ECataclysmType::Demonic, Outpost);
	TestEqual(TEXT("two Rifts"), UCataclysmEmpireMapLayout::RiftMark(Run, Outpost), FString(TEXT(", 2 Rifts")));
	TestEqual(TEXT("and only on their own city"), UCataclysmEmpireMapLayout::RiftMark(Run, FirstOutpost(*Run, 1)),
			  FString());
	TestEqual(TEXT("nothing for no run"), UCataclysmEmpireMapLayout::RiftMark(nullptr, Outpost), FString());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
