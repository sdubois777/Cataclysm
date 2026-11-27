// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"
#include "Tests/CataclysmTestRun.h"

/**
 * War's quest: Quest Sieges. Issue #51 slice B4.
 *
 * THE RULES. The owner, 2026-10-16, verbatim: "War quest dungeons WILL be sieges." 2026-10-17, verbatim: "the first
 * option, but make it up to 2 a wave. So they player isn't just stuck there forever waiting for the next surge to
 * progress." -- at most two, a quarter of an ordinary Siege's bite, growth counted from the day it reached its city.
 * The coordinating session, 2026-10-16: one drawn onto a besieged city re-draws its city. The plan's reading of
 * section XI, accepted by the owner on 2026-11-24: a city's fall to War starts the next one. The coordinating session,
 * 2026-11-25: the fall is to War when the felling hit was sent by War (W1); at most two are made on one day, waves and
 * falls together (W2); what a fall makes is a Quest dungeon (W3).
 *
 * EVERY RULE HAS A CONTROL that the rule must leave alone. The model's half is
 * `sim/tests/test_a_war_quest_siege_is_capped_and_bites_a_quarter.py` and
 * `sim/tests/test_a_fall_to_war_starts_the_next_siege.py`, and `tools/tests/test_war_quest_sieges_port.py` holds the
 * two to the same rules.
 */
namespace CataclysmWarQuestSiegesTest
{
	/** A run begun on `Seed` facing only `Facing`, a day passed, and its board and timers cleared. */
	UCataclysmEmpireRun* MakeEmptyRun(int32 Seed = 1, ECataclysmType Facing = ECataclysmType::Death)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(Seed, ECataclysmSurgeMode::Static);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		Run->ActiveCataclysms = { Facing };
		return Run;
	}

	/** A dungeon put down by hand, with its timer, on `City`, made on `Spawned`. Returns its identifier. */
	int32 Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, ECataclysmDungeonSubType SubType,
				ECataclysmType Sender, const FCataclysmCity& City, float ResolveDays = 10000.0f,
				int32 Spawned = INDEX_NONE)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.Cataclysm = Sender;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 4;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Spawned == INDEX_NONE ? Run.Day() : Spawned;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
		return DungeonId;
	}

	/** An ordinary dungeon that never runs out, on the Pillar, so no empty-board wave lands. */
	void HoldTheBoard(UCataclysmEmpireRun& Run)
	{
		Place(Run, 9999, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, ECataclysmType::None,
			  *Run.Map->Find(Run.Map->PillarId));
	}

	/** An exposed city that is not the Pillar and has at least one exposed neighbour. */
	const FCataclysmCity* AFrontCityWithExposedNeighbours(const UCataclysmEmpireRun& Run)
	{
		for (const int32 CityId : Run.Map->ExposedCities())
		{
			const FCataclysmCity* City = Run.Map->Find(CityId);
			if (City == nullptr)
			{
				continue;
			}
			for (const int32 Next : UCataclysmSurgeScheduler::AdjacentCities(*City))
			{
				if (Next != Run.Map->PillarId && Run.Map->IsExposed(Next))
				{
					return City;
				}
			}
		}
		return nullptr;
	}

	/**
	 * Fells `City` tomorrow with an ordinary dungeon `Sender` sent: the city at 1 defence, the dungeon's timer a day
	 * and its damage far past it. Returns the day's report.
	 */
	FCataclysmDayReport FellTomorrow(UCataclysmEmpireRun& Run, int32 CityId, ECataclysmType Sender)
	{
		FCataclysmCity* City = Run.Map->FindMutable(CityId);
		City->Defence = 1.0f;
		Place(Run, 9001, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, Sender, *City, 1.0f);
		Run.Dungeons.Last().DefenceDamage = 1000000.0f;
		return Run.AdvanceDay();
	}

	/** The War Quest Sieges on the map. */
	TArray<const FCataclysmDungeon*> WarQuestSieges(const UCataclysmEmpireRun& Run)
	{
		TArray<const FCataclysmDungeon*> Found;
		for (const FCataclysmDungeon& Dungeon : Run.Dungeons)
		{
			if (Dungeon.Type == ECataclysmDungeonType::Quest && Dungeon.SubType == ECataclysmDungeonSubType::Siege
				&& Dungeon.Cataclysm == ECataclysmType::War)
			{
				Found.Add(&Dungeon);
			}
		}
		return Found;
	}

	/** How many Quest dungeons a wave holds. */
	int32 QuestsIn(const TArray<FCataclysmDungeon>& Wave)
	{
		int32 Quests = 0;
		for (const FCataclysmDungeon& Dungeon : Wave)
		{
			Quests += Dungeon.Type == ECataclysmDungeonType::Quest ? 1 : 0;
		}
		return Quests;
	}
}

// ---------------------------------------------------------------------------
// The sub-type and the two numbers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesNumbersTest,
	"Cataclysm.WarQuestSieges.WarsQuestDungeonsAreSiegesWithTheModelsTwoNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesNumbersTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("War's Quest dungeons are Sieges"),
			  static_cast<int32>(UCataclysmSurgeScheduler::QuestSubTypeFor(ECataclysmType::War)),
			  static_cast<int32>(ECataclysmDungeonSubType::Siege));
	TestEqual(TEXT("at most two made on one day"), UCataclysmSurgeScheduler::QuestSiegesPerDay, 2);
	TestEqual(TEXT("a quarter of the bite"), UCataclysmSurgeScheduler::QuestSiegeBiteShare, 0.25f);

	FCataclysmDungeon Dungeon;
	Dungeon.Type = ECataclysmDungeonType::Quest;
	TestEqual(TEXT("a Quest dungeon bites the quarter"), Dungeon.SiegeBiteShare(), 0.25f);
	Dungeon.Type = ECataclysmDungeonType::Basic;
	TestEqual(TEXT("CONTROL: an ordinary one bites the whole"), Dungeon.SiegeBiteShare(), 1.0f);
	TestEqual(TEXT("CONTROL: Death's are still Horde"),
			  static_cast<int32>(UCataclysmSurgeScheduler::QuestSubTypeFor(ECataclysmType::Death)),
			  static_cast<int32>(ECataclysmDungeonSubType::Horde));
	return true;
}

// ---------------------------------------------------------------------------
// The wave: the day's two, and the re-draw
// ---------------------------------------------------------------------------

/**
 * A wave War sends makes no more War Quest Sieges than the day has left: with two already made today it makes none,
 * every Quest roll made Basic; with one, at most one in any wave. CONTROL: with none made, some waves make two, and a
 * wave Death sends with two already made still makes Quest dungeons. Over 400 seeds; Quest dungeons are 12 in 100.
 * Prints "WAR WAVES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesWaveCapTest,
	"Cataclysm.WarQuestSieges.AWaveMakesNoMoreWarQuestSiegesThanTheDayHasLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesWaveCapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmSurgeScheduler* Scheduler = NewObject<UCataclysmSurgeScheduler>();
	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();

	const auto Sends = [](ECataclysmType Sender) { return [Sender]() { return Sender; }; };
	const TArray<int32> NoneStanding;

	int32 MostWithOneLeft = 0;
	int32 QuestsWithNoneLeft = 0;
	int32 WavesMakingTwo = 0;
	int32 DeathQuestsWithNoneLeft = 0;
	int32 SiegesNotQuest = 0;
	for (int32 Seed = 1; Seed <= 400; ++Seed)
	{
		FRandomStream Full(Seed);
		FRandomStream One(Seed);
		FRandomStream Fresh(Seed);
		FRandomStream Death(Seed);
		const TArray<FCataclysmDungeon> WithNoneLeft = Scheduler->RollWave(*Map, 1, 0, Full, NoneStanding, NoneStanding,
																		 Sends(ECataclysmType::War), false, 7, 2);
		const TArray<FCataclysmDungeon> WithOneLeft = Scheduler->RollWave(*Map, 1, 0, One, NoneStanding, NoneStanding,
																		Sends(ECataclysmType::War), false, 7, 1);
		const TArray<FCataclysmDungeon> WithTwoLeft = Scheduler->RollWave(*Map, 1, 0, Fresh, NoneStanding, NoneStanding,
																		Sends(ECataclysmType::War), false, 7, 0);
		const TArray<FCataclysmDungeon> DeathWave = Scheduler->RollWave(*Map, 1, 0, Death, NoneStanding, NoneStanding,
																	  Sends(ECataclysmType::Death), false, 7, 2);
		QuestsWithNoneLeft += QuestsIn(WithNoneLeft);
		MostWithOneLeft = FMath::Max(MostWithOneLeft, QuestsIn(WithOneLeft));
		WavesMakingTwo += QuestsIn(WithTwoLeft) >= 2 ? 1 : 0;
		DeathQuestsWithNoneLeft += QuestsIn(DeathWave);
		for (const FCataclysmDungeon& Dungeon : WithTwoLeft)
		{
			SiegesNotQuest += (Dungeon.Type == ECataclysmDungeonType::Quest
							   && Dungeon.SubType != ECataclysmDungeonSubType::Siege) ? 1 : 0;
		}
	}

	AddInfo(FString::Printf(TEXT("WAR WAVES: none left %d Quest; one left at most %d; two left %d waves made two; "
								 "Death with none left %d Quest"),
							QuestsWithNoneLeft, MostWithOneLeft, WavesMakingTwo, DeathQuestsWithNoneLeft));

	TestEqual(TEXT("with the day's two made, no War Quest dungeon"), QuestsWithNoneLeft, 0);
	TestTrue(TEXT("with one made, at most one in a wave"), MostWithOneLeft <= 1);
	TestEqual(TEXT("every War Quest dungeon is a Siege"), SiegesNotQuest, 0);
	TestTrue(TEXT("CONTROL: with none made, some waves make two"), WavesMakingTwo > 0);
	TestTrue(TEXT("CONTROL: Death's Quest dungeons are not capped"), DeathQuestsWithNoneLeft > 0);
	return true;
}

/**
 * A War Quest dungeon drawn onto a city that holds a Siege lands on one that holds none: every exposed city but one
 * holds a Siege, and every War Quest dungeon of 400 waves lands on the free one. CONTROL: with every exposed city
 * holding one, a War Quest dungeon lands on a besieged city, the only way a city holds two. Prints "WAR RE-DRAW".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesReDrawTest,
	"Cataclysm.WarQuestSieges.AWarQuestDungeonDrawnOntoABesiegedCityLandsOnOneWithout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesReDrawTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmSurgeScheduler* Scheduler = NewObject<UCataclysmSurgeScheduler>();
	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();

	const TArray<int32> Exposed = Map->ExposedCities();
	if (!TestTrue(TEXT("set-up: at least two exposed cities"), Exposed.Num() >= 2))
	{
		return false;
	}
	const int32 Free = Exposed[0];
	TArray<int32> AllButOne;
	AllButOne.AddZeroed(Map->Cities.Num());
	TArray<int32> All;
	All.AddZeroed(Map->Cities.Num());
	for (const int32 CityId : Exposed)
	{
		AllButOne[CityId] = CityId == Free ? 0 : 1;
		All[CityId] = 1;
	}

	const auto War = []() { return ECataclysmType::War; };
	const TArray<int32> NoCaps;
	int32 Landed = 0;
	int32 OnTheFree = 0;
	int32 FreeAlreadyTaken = 0;
	int32 ControlLanded = 0;
	int32 ControlOnABesieged = 0;
	FString Missed;
	for (int32 Seed = 1; Seed <= 400; ++Seed)
	{
		FRandomStream Draws(Seed);
		FRandomStream ControlDraws(Seed);
		// ONE LEFT TODAY, so a wave makes at most one: the free city then has room for it -- UNLESS AN EARLIER DUNGEON
		// OF THE SAME WAVE TOOK IT. An ordinary dungeon's sub-type roll counts toward a city's one Siege too, and in
		// this set-up only the free city can still take one, so an ordinary dungeon landing there first and rolling a
		// Siege leaves no free city; the War Quest dungeon then lands where it was drawn, the ruled "none free" case
		// (as `Simulation.trigger_surge` does). Window b4-w1 counted 10 of 313 off the free city with this test
		// counting them as misses; they are counted apart here, and only a miss with the free city still free fails.
		bool bFreeTaken = false;
		for (const FCataclysmDungeon& Dungeon : Scheduler->RollWave(*Map, 1, 0, Draws, NoCaps, AllButOne, War, false,
																	 Seed, 1))
		{
			if (Dungeon.Type == ECataclysmDungeonType::Quest)
			{
				++Landed;
				OnTheFree += Dungeon.CityId == Free ? 1 : 0;
				if (Dungeon.CityId != Free && bFreeTaken)
				{
					++FreeAlreadyTaken;
				}
				else if (Dungeon.CityId != Free && Missed.Len() < 1500)
				{
					Missed += FString::Printf(TEXT("seed %d: dungeon %d on city %d (exposed %d, sieges %d, free %d), sub-type %d; "),
											  Seed, Dungeon.DungeonId, Dungeon.CityId, Map->IsExposed(Dungeon.CityId) ? 1 : 0,
											  AllButOne.IsValidIndex(Dungeon.CityId) ? AllButOne[Dungeon.CityId] : -1, Free,
											  static_cast<int32>(Dungeon.SubType));
				}
			}
			bFreeTaken |= Dungeon.CityId == Free && Dungeon.SubType == ECataclysmDungeonSubType::Siege;
		}
		for (const FCataclysmDungeon& Dungeon : Scheduler->RollWave(*Map, 1, 0, ControlDraws, NoCaps, All, War, false,
																	 Seed, 1))
		{
			if (Dungeon.Type == ECataclysmDungeonType::Quest)
			{
				++ControlLanded;
				ControlOnABesieged += All.IsValidIndex(Dungeon.CityId) && All[Dungeon.CityId] > 0 ? 1 : 0;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("WAR RE-DRAW: %d landed, %d on the free city; CONTROL %d landed, %d on a besieged one"),
							Landed, OnTheFree, ControlLanded, ControlOnABesieged));

	if (!Missed.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("WAR RE-DRAW MISSED: %s"), *Missed));
	}
	TestTrue(TEXT("set-up: War Quest dungeons were made"), Landed > 0 && ControlLanded > 0);
	AddInfo(FString::Printf(TEXT("WAR RE-DRAW: %d landed after an earlier dungeon of the wave took the free city's Siege"),
							FreeAlreadyTaken));
	TestEqual(TEXT("every one that landed while the free city was free landed on it"), OnTheFree,
			  Landed - FreeAlreadyTaken);
	TestEqual(TEXT("CONTROL: with none free, each lands where it was drawn, on a besieged city"), ControlOnABesieged,
			  ControlLanded);
	return true;
}

// ---------------------------------------------------------------------------
// The bite: a quarter, and the days on its city
// ---------------------------------------------------------------------------

/**
 * Over one day a Quest Siege takes a quarter of what an ordinary Siege takes from an identical city, flat share and
 * growth alike. CONTROL: the ordinary Siege takes its whole share. Prints "QUARTER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesQuarterTest,
	"Cataclysm.WarQuestSieges.AQuestSiegeBitesAQuarterOfWhatAnOrdinarySiegeBites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesQuarterTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	TArray<int32> Outposts;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Outpost && !City.bFallen)
		{
			Outposts.Add(City.CityId);
		}
	}
	if (!TestTrue(TEXT("set-up: two Outposts"), Outposts.Num() >= 2))
	{
		return false;
	}
	FCataclysmCity* Ordinary = Run->Map->FindMutable(Outposts[0]);
	FCataclysmCity* Quest = Run->Map->FindMutable(Outposts[1]);
	Ordinary->Defence = Run->Map->EffectiveMaxDefence(*Ordinary);
	Quest->Defence = Run->Map->EffectiveMaxDefence(*Quest);
	if (!TestEqual(TEXT("set-up: the two are the same size"), Ordinary->Defence, Quest->Defence))
	{
		return false;
	}

	// SPAWNED FIVE DAYS AGO, so the growth is part of the bite and not nought.
	const int32 Spawned = Run->Day() - 5;
	Place(*Run, 501, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, ECataclysmType::War, *Ordinary,
		  10000.0f, Spawned);
	Place(*Run, 502, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Siege, ECataclysmType::War, *Quest,
		  10000.0f, Spawned);
	const float OrdinaryBefore = Ordinary->Defence;
	const float QuestBefore = Quest->Defence;

	Run->AdvanceDay();

	const float OrdinaryTook = OrdinaryBefore - Run->Map->Find(Outposts[0])->Defence;
	const float QuestTook = QuestBefore - Run->Map->Find(Outposts[1])->Defence;
	const float Whole = Run->Map->EffectiveMaxDefence(*Run->Map->Find(Outposts[0]))
						* UCataclysmEmpireRun::SiegeDefenceBitePerDay
					  + UCataclysmEmpireRun::SiegeDamageGrowthPerDay * 6;
	AddInfo(FString::Printf(TEXT("QUARTER: the ordinary Siege took %.2f (whole %.2f), the Quest Siege %.2f"),
							OrdinaryTook, Whole, QuestTook));

	TestEqual(TEXT("the Quest Siege takes a quarter"), QuestTook, OrdinaryTook * 0.25f, 0.01f);
	TestEqual(TEXT("CONTROL: the ordinary Siege takes its whole share, six days grown"), OrdinaryTook, Whole, 0.01f);
	return true;
}

/**
 * A Siege's growth counts the days on the city it stands on now: one that spawned 20 days ago and arrived today grows
 * from nought, in the day's share and in the forecast. CONTROL: the same Siege that never moved grows from its spawn.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesArrivalGrowthTest,
	"Cataclysm.WarQuestSieges.ASiegesGrowthCountsFromTheDayItArrived",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesArrivalGrowthTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	FCataclysmDungeon Dungeon;
	Dungeon.SpawnedDay = 10;
	TestEqual(TEXT("never moved: from its spawn"), Dungeon.DaysOnItsCity(30), 20);
	Dungeon.ArrivedDay = 28;
	TestEqual(TEXT("arrived on 28: two days"), Dungeon.DaysOnItsCity(30), 2);
	TestEqual(TEXT("never negative"), Dungeon.DaysOnItsCity(20), 0);

	UCataclysmEmpireRun* Moved = MakeEmptyRun();
	UCataclysmEmpireRun* Stayed = MakeEmptyRun();
	HoldTheBoard(*Moved);
	HoldTheBoard(*Stayed);
	const int32 CityId = Moved->Map->ExposedCities()[0];
	const int32 Spawned = Moved->Day() - 20;
	Place(*Moved, 601, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, ECataclysmType::War,
		  *Moved->Map->Find(CityId), 10000.0f, Spawned);
	Moved->Dungeons.Last().ArrivedDay = Moved->Day();
	Place(*Stayed, 601, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, ECataclysmType::War,
		  *Stayed->Map->Find(CityId), 10000.0f, Spawned);

	const float MovedForecast = Moved->ScheduledDefenceLossOn(CityId, 1);
	const float StayedForecast = Stayed->ScheduledDefenceLossOn(CityId, 1);
	const float Flat = Moved->Map->EffectiveMaxDefence(*Moved->Map->Find(CityId))
					   * UCataclysmEmpireRun::SiegeDefenceBitePerDay;
	TestEqual(TEXT("the forecast of one that arrived today grows one day"), MovedForecast,
			  Flat + UCataclysmEmpireRun::SiegeDamageGrowthPerDay * 1, 0.01f);
	TestEqual(TEXT("CONTROL: the one that stayed grows 21"), StayedForecast,
			  Flat + UCataclysmEmpireRun::SiegeDamageGrowthPerDay * 21, 0.01f);

	const float MovedBefore = Moved->Map->Find(CityId)->Defence;
	const float StayedBefore = Stayed->Map->Find(CityId)->Defence;
	Moved->AdvanceDay();
	Stayed->AdvanceDay();
	TestEqual(TEXT("the day's share agrees with the forecast"), MovedBefore - Moved->Map->Find(CityId)->Defence,
			  MovedForecast, 0.01f);
	TestEqual(TEXT("CONTROL: and for the one that stayed"), StayedBefore - Stayed->Map->Find(CityId)->Defence,
			  StayedForecast, 0.01f);
	return true;
}

/**
 * A Quest dungeon moved by its city's fall arrives that day: its `ArrivedDay` is the day of the fall. CONTROL: an
 * ordinary dungeon on another city keeps `INDEX_NONE`.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesFallArrivalTest,
	"Cataclysm.WarQuestSieges.AQuestDungeonMovedByAFallArrivesThatDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesFallArrivalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	const FCataclysmCity* Front = AFrontCityWithExposedNeighbours(*Run);
	if (!TestNotNull(TEXT("set-up: a front city with an exposed neighbour"), Front))
	{
		return false;
	}
	const int32 FrontId = Front->CityId;
	int32 Elsewhere = INDEX_NONE;
	for (const int32 CityId : Run->Map->ExposedCities())
	{
		if (CityId != FrontId)
		{
			Elsewhere = CityId;
			break;
		}
	}
	// DEATH'S, A HORDE: a Quest dungeon Demonic sent would be a Rift, which sends dungeons of its own.
	Place(*Run, 701, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Horde, ECataclysmType::Death,
		  *Run->Map->Find(FrontId));
	Place(*Run, 702, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, ECataclysmType::Death,
		  *Run->Map->Find(Elsewhere));

	const FCataclysmDayReport Report = FellTomorrow(*Run, FrontId, ECataclysmType::Death);
	const FCataclysmDungeon* Moved = Run->FindDungeon(701);
	const FCataclysmDungeon* Control = Run->FindDungeon(702);
	if (!TestTrue(TEXT("set-up: the city fell"), Report.Fallen.Contains(FrontId))
		|| !TestNotNull(TEXT("set-up: the Quest dungeon stands"), Moved)
		|| !TestNotNull(TEXT("set-up: and the control"), Control))
	{
		return false;
	}
	TestNotEqual(TEXT("the Quest dungeon moved"), Moved->CityId, FrontId);
	TestEqual(TEXT("and arrived on the day of the fall"), Moved->ArrivedDay, Report.Day);
	TestEqual(TEXT("CONTROL: a dungeon that did not move has no arrival"), Control->ArrivedDay, INDEX_NONE);
	return true;
}

// ---------------------------------------------------------------------------
// A fall to War starts the next one
// ---------------------------------------------------------------------------

/**
 * A city felled by a dungeon War sent records War and makes one War Quest Siege, a Quest dungeon, on an exposed
 * neighbour. CONTROL: the same fall by a dungeon Demonic sent records Demonic and makes none. Prints "FALL TO WAR".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesFallTest,
	"Cataclysm.WarQuestSieges.AFallToWarMakesAWarQuestSiegeOnANeighbour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesFallTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	UCataclysmEmpireRun* Control = MakeEmptyRun();
	HoldTheBoard(*Run);
	HoldTheBoard(*Control);
	const FCataclysmCity* Front = AFrontCityWithExposedNeighbours(*Run);
	if (!TestNotNull(TEXT("set-up: a front city with an exposed neighbour"), Front))
	{
		return false;
	}
	const int32 FrontId = Front->CityId;
	const FCataclysmDayReport Report = FellTomorrow(*Run, FrontId, ECataclysmType::War);
	const FCataclysmDayReport ControlReport = FellTomorrow(*Control, FrontId, ECataclysmType::Demonic);

	const TArray<const FCataclysmDungeon*> Made = WarQuestSieges(*Run);
	AddInfo(FString::Printf(TEXT("FALL TO WAR: fell %s, felled by %s, %d made on a fall, %d War Quest Sieges; CONTROL "
								 "felled by %s, %d made"),
							Report.Fallen.Contains(FrontId) ? TEXT("yes") : TEXT("NO"),
							*UCataclysmRoster::NameFor(Run->Map->Find(FrontId)->FellTo).ToString(),
							Report.WarSiegeOnAFall.Num(), Made.Num(),
							*UCataclysmRoster::NameFor(Control->Map->Find(FrontId)->FellTo).ToString(),
							ControlReport.WarSiegeOnAFall.Num()));

	if (!TestTrue(TEXT("set-up: both cities fell"),
				  Report.Fallen.Contains(FrontId) && ControlReport.Fallen.Contains(FrontId)))
	{
		return false;
	}
	TestEqual(TEXT("the city records War"), static_cast<int32>(Run->Map->Find(FrontId)->FellTo),
			  static_cast<int32>(ECataclysmType::War));
	TestEqual(TEXT("one War Quest Siege made on the fall"), Report.WarSiegeOnAFall.Num(), 1);
	if (TestEqual(TEXT("and it is the one War Quest Siege on the map"), Made.Num(), 1))
	{
		TestTrue(TEXT("on a neighbour of the fallen city"),
				 UCataclysmSurgeScheduler::AdjacentCities(*Run->Map->Find(FrontId)).Contains(Made[0]->CityId));
		TestTrue(TEXT("which is exposed"), Run->Map->IsExposed(Made[0]->CityId));
		TestEqual(TEXT("made today"), Made[0]->SpawnedDay, Report.Day);
		TestTrue(TEXT("and listed among the day's new dungeons"), Report.Spawned.Contains(Made[0]->DungeonId));
	}
	TestEqual(TEXT("CONTROL: a fall to Demonic records Demonic"),
			  static_cast<int32>(Control->Map->Find(FrontId)->FellTo), static_cast<int32>(ECataclysmType::Demonic));
	TestEqual(TEXT("CONTROL: and makes none"), ControlReport.WarSiegeOnAFall.Num(), 0);
	TestEqual(TEXT("CONTROL: no War Quest Siege on its map"), WarQuestSieges(*Control).Num(), 0);
	return true;
}

/**
 * A fall to War makes none when the day already has its two War Quest Sieges, or when every neighbour holds a Siege.
 * CONTROL: the same fall with neither makes one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesFallRefusedTest,
	"Cataclysm.WarQuestSieges.AFallToWarMakesNoneWhenTheDayHasItsTwoOrEveryNeighbourIsBesieged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesFallRefusedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmEmpireRun* Probe = MakeEmptyRun();
	const FCataclysmCity* Front = AFrontCityWithExposedNeighbours(*Probe);
	if (!TestNotNull(TEXT("set-up: a front city with an exposed neighbour"), Front))
	{
		return false;
	}
	const int32 FrontId = Front->CityId;
	const TArray<int32> Neighbours = UCataclysmSurgeScheduler::AdjacentCities(*Front);

	// THE DAY'S TWO: two War Quest Sieges made tomorrow, on cities that are not neighbours, so they bar nothing but the
	// count. The Pillar never holds a dungeon a wave made, so it is skipped.
	UCataclysmEmpireRun* Capped = MakeEmptyRun();
	HoldTheBoard(*Capped);
	int32 Put = 0;
	for (const FCataclysmCity& City : Capped->Map->Cities)
	{
		if (Put < 2 && City.CityId != FrontId && City.CityId != Capped->Map->PillarId && !City.bFallen
			&& !Neighbours.Contains(City.CityId))
		{
			Place(*Capped, 800 + Put, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Siege,
				  ECataclysmType::War, City, 10000.0f, Capped->Day() + 1);
			++Put;
		}
	}
	if (!TestEqual(TEXT("set-up: two War Quest Sieges made on the day of the fall"), Put, 2))
	{
		return false;
	}
	const FCataclysmDayReport CappedReport = FellTomorrow(*Capped, FrontId, ECataclysmType::War);

	// EVERY NEIGHBOUR BESIEGED, exposed or not: the fall exposes the cities it shielded.
	UCataclysmEmpireRun* Hemmed = MakeEmptyRun();
	HoldTheBoard(*Hemmed);
	int32 Next = 850;
	for (const int32 CityId : Neighbours)
	{
		if (CityId != Hemmed->Map->PillarId)
		{
			Place(*Hemmed, Next++, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, ECataclysmType::Death,
				  *Hemmed->Map->Find(CityId));
		}
	}
	const FCataclysmDayReport HemmedReport = FellTomorrow(*Hemmed, FrontId, ECataclysmType::War);

	UCataclysmEmpireRun* Free = MakeEmptyRun();
	HoldTheBoard(*Free);
	const FCataclysmDayReport FreeReport = FellTomorrow(*Free, FrontId, ECataclysmType::War);

	if (!TestTrue(TEXT("set-up: the city fell in all three"), CappedReport.Fallen.Contains(FrontId)
				  && HemmedReport.Fallen.Contains(FrontId) && FreeReport.Fallen.Contains(FrontId)))
	{
		return false;
	}
	TestEqual(TEXT("with the day's two made, none"), CappedReport.WarSiegeOnAFall.Num(), 0);
	TestEqual(TEXT("with every neighbour besieged, none"), HemmedReport.WarSiegeOnAFall.Num(), 0);
	TestEqual(TEXT("CONTROL: with neither, one"), FreeReport.WarSiegeOnAFall.Num(), 1);
	return true;
}

/** A retaken city was felled by nobody. CONTROL: before the retake it records the Cataclysm that felled it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesRetakeTest,
	"Cataclysm.WarQuestSieges.ARetakenCityWasFelledByNobody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesRetakeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	const int32 CityId = Run->Map->ExposedCities()[0];
	const FCataclysmDayReport Report = FellTomorrow(*Run, CityId, ECataclysmType::War);
	if (!TestTrue(TEXT("set-up: the city fell"), Report.Fallen.Contains(CityId)))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: fallen, it records War"), static_cast<int32>(Run->Map->Find(CityId)->FellTo),
			  static_cast<int32>(ECataclysmType::War));
	TestTrue(TEXT("set-up: it is retaken"), Run->Map->Retake(CityId));
	TestEqual(TEXT("retaken, it records nobody"), static_cast<int32>(Run->Map->Find(CityId)->FellTo),
			  static_cast<int32>(ECataclysmType::None));
	return true;
}

// ---------------------------------------------------------------------------
// The save: run record version 21
// ---------------------------------------------------------------------------

/**
 * A gathered run record carries a dungeon's `ArrivedDay` and a city's `FellTo`, and a load reads them back.
 * CONTROL: a dungeon that never moved carries `INDEX_NONE` and a standing city `None`.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesSaveTest,
	"Cataclysm.WarQuestSieges.AGatheredRunRecordCarriesArrivalAndFeller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesSaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWarQuestSiegesTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	const TArray<int32> Exposed = Run->Map->ExposedCities();
	if (!TestTrue(TEXT("set-up: two exposed cities"), Exposed.Num() >= 2))
	{
		return false;
	}
	Place(*Run, 901, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Siege, ECataclysmType::War,
		  *Run->Map->Find(Exposed[0]));
	Run->Dungeons.Last().ArrivedDay = 57;
	Place(*Run, 902, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, ECataclysmType::War,
		  *Run->Map->Find(Exposed[1]));
	Run->Map->FindMutable(Exposed[1])->FellTo = ECataclysmType::War;

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	FString Why;
	if (!TestTrue(TEXT("the dungeons are gathered"), FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why))
		|| !TestTrue(TEXT("and the cities"), FCataclysmSaveGather::CitiesFrom(*Run, *Record)))
	{
		AddError(Why);
		return false;
	}
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;

	FString Text;
	FString Error;
	if (!TestTrue(TEXT("it is written"), FCataclysmSaveStorage::ToJson(Record, Text, Error)))
	{
		AddError(Error);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (!TestNotNull(TEXT("and read back"), Read))
	{
		AddError(Message);
		return false;
	}

	const FCataclysmDungeon* Arrived = Read->Dungeons.FindByPredicate(
		[](const FCataclysmDungeon& Dungeon) { return Dungeon.DungeonId == 901; });
	const FCataclysmDungeon* Stayed = Read->Dungeons.FindByPredicate(
		[](const FCataclysmDungeon& Dungeon) { return Dungeon.DungeonId == 902; });
	const FCataclysmCity* Felled = Read->Cities.FindByPredicate(
		[&Exposed](const FCataclysmCity& City) { return City.CityId == Exposed[1]; });
	const FCataclysmCity* Standing = Read->Cities.FindByPredicate(
		[&Exposed](const FCataclysmCity& City) { return City.CityId == Exposed[0]; });
	if (!TestNotNull(TEXT("the moved dungeon is read"), Arrived) || !TestNotNull(TEXT("and the other"), Stayed)
		|| !TestNotNull(TEXT("the felled city is read"), Felled) || !TestNotNull(TEXT("and the standing one"), Standing))
	{
		return false;
	}
	TestEqual(TEXT("its day of arrival is carried"), Arrived->ArrivedDay, 57);
	TestEqual(TEXT("the city's feller is carried"), static_cast<int32>(Felled->FellTo),
			  static_cast<int32>(ECataclysmType::War));
	TestEqual(TEXT("CONTROL: a dungeon that never moved, INDEX_NONE"), Stayed->ArrivedDay, INDEX_NONE);
	TestEqual(TEXT("CONTROL: a standing city, None"), static_cast<int32>(Standing->FellTo),
			  static_cast<int32>(ECataclysmType::None));
	return true;
}

/**
 * `Run_v20.json`, written before the step, migrates to version 21 with every dungeon's `ArrivedDay` -1 and every
 * city's `FellTo` None, every other field kept. CONTROL: `Run_v22.json` is read, not migrated, and its fallen city
 * records Demonic, not the default. Prints "VERSION 21".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarQuestSiegesMigrationTest,
	"Cataclysm.WarQuestSieges.ARunFileFromVersion20GainsNoArrivalAndNoFeller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWarQuestSiegesMigrationTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString CurrentText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v20.json"), Text, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Run_v22.json"), CurrentText, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult CurrentResult = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmRunSave* Old = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	const UCataclysmRunSave* Current = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		CurrentText, UCataclysmRunSave::StaticClass(), GetTransientPackage(), CurrentResult, Message));
	if (!TestNotNull(TEXT("Run_v20.json loads"), Old) || !TestNotNull(TEXT("Run_v22.json loads"), Current))
	{
		AddError(Message);
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 20 file with what the step writes, against what the record writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion21Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion22Defaults(*Expected); // And issue #51 slice B5's step: no lair counted.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Old, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	bool bAllUnmoved = Old->Dungeons.Num() > 0;
	for (const FCataclysmDungeon& Dungeon : Old->Dungeons)
	{
		bAllUnmoved &= Dungeon.ArrivedDay == INDEX_NONE;
	}
	bool bNoFeller = Old->Cities.Num() > 0;
	for (const FCataclysmCity& City : Old->Cities)
	{
		bNoFeller &= City.FellTo == ECataclysmType::None;
	}
	const FCataclysmCity* CurrentFallen = Current->Cities.FindByPredicate(
		[](const FCataclysmCity& City) { return City.bFallen; });

	AddInfo(FString::Printf(TEXT("VERSION 21: Run_v20.json %s, unmoved %s, no feller %s; Run_v22.json %s, its fallen "
								 "city felled by %s; the rest %s"),
							FCataclysmSaveStorage::Describe(Result), bAllUnmoved ? TEXT("yes") : TEXT("NO"),
							bNoFeller ? TEXT("yes") : TEXT("NO"), FCataclysmSaveStorage::Describe(CurrentResult),
							CurrentFallen ? *UCataclysmRoster::NameFor(CurrentFallen->FellTo).ToString() : TEXT("-"),
							bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("Run_v20.json is migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("and arrives at the current version"), Old->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestTrue(TEXT("every dungeon has no arrival"), bAllUnmoved);
	TestTrue(TEXT("every city was felled by nobody"), bNoFeller);
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestEqual(TEXT("CONTROL: Run_v22.json is read, not migrated"), static_cast<int32>(CurrentResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	if (TestNotNull(TEXT("CONTROL: Run_v22.json has a fallen city"), CurrentFallen))
	{
		TestEqual(TEXT("CONTROL: and it records Demonic"), static_cast<int32>(CurrentFallen->FellTo),
				  static_cast<int32>(ECataclysmType::Demonic));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
