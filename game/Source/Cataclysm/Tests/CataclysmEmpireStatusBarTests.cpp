// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "Interface/CataclysmCreaturePanel.h"
#include "Interface/CataclysmEmpireStatusBar.h"
#include "Player/CataclysmGameInstance.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the empire status bar on the dungeon screen, issue #49: "Empire status bar -- active dungeon count, next
 * surge countdown, cities at risk".
 *
 * WHAT THEY CANNOT CHECK is whether the line can be read: the automation command passes `-nullrhi`, so
 * `ACataclysmHUD::DrawHUD` never runs and nothing is drawn. **Somebody has to look.** What they cover is everything
 * `UCataclysmEmpireStatusBar` decides: the words, the three figures gathered from a real run, that nothing is said
 * with no run bound, and where the line may go.
 *
 * THE RULINGS THEY HOLD, by the coordinating session on 2026-11-02 (`docs/DECISIONS.md`): (a) a city at risk is a
 * distinct standing city with a dungeon on it; (b) the countdown counts from `Day() + DaysOwed()`; (c) no run bound,
 * nothing said.
 */

namespace CataclysmEmpireStatusBarTest
{
	/** Facts as a bound run would give them, written by hand so a test of the words needs no campaign. */
	FCataclysmEmpireStatusFacts BoundFacts(int32 Dungeons, int32 DaysToSurge, int32 Owed, int32 Cities)
	{
		FCataclysmEmpireStatusFacts Facts;
		Facts.bRunBound = true;
		Facts.DungeonsStanding = Dungeons;
		Facts.DaysToNextSurge = DaysToSurge;
		Facts.DaysOwed = Owed;
		Facts.CitiesAtRisk = Cities;
		return Facts;
	}

	/** A run begun on seed 7 and advanced one day, so the opening wave has landed and the next surge is scheduled. */
	UCataclysmEmpireRun* BegunOnDayOne(UObject* Outer)
	{
		UCataclysmEmpireRun* Run = Outer != nullptr ? NewObject<UCataclysmEmpireRun>(Outer)
													: NewObject<UCataclysmEmpireRun>();
		Run->Begin(7);
		Run->AdvanceDay();
		return Run;
	}

	/** The test's own count of ruling (a), written apart from the code it checks. */
	int32 StandingCitiesCarryingADungeon(const UCataclysmEmpireRun& Run)
	{
		TSet<int32> Cities;
		for (const FCataclysmDungeon& Dungeon : Run.Dungeons)
		{
			const FCataclysmCity* City = Run.Map->Find(Dungeon.CityId);
			if (City != nullptr && !City->bFallen)
			{
				Cities.Add(Dungeon.CityId);
			}
		}
		return Cities.Num();
	}

	/** Puts one dungeon on a city, on the map's list only: nothing in these tests advances a day afterwards. */
	void StandADungeonOn(UCataclysmEmpireRun& Run, int32 DungeonId, int32 CityId)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Floors = 12;
		Dungeon.SpawnedDay = Run.Day();
		Run.Dungeons.Add(Dungeon);
	}
}

// ---------------------------------------------------------------------------
// The words
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarWordsTest,
	"Cataclysm.EmpireStatusBar.TheLineSaysEachFigureWithItsOwnPlural",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarWordsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	TestEqual(TEXT("three dungeons, four days and two cities, each plural"),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(3, 4, 0, 2)),
			  FString(TEXT("3 dungeons standing | surge in 4 days | 2 cities at risk")));

	TestEqual(TEXT("one dungeon, one day and one city, each singular"),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(1, 1, 0, 1)),
			  FString(TEXT("1 dungeon standing | surge tomorrow | 1 city at risk")));

	// NOUGHT IS PLURAL, as English has it: "0 cities at risk", not "0 city".
	TestEqual(TEXT("none standing and none at risk are said as nought, in the plural"),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(0, 25, 0, 0)),
			  FString(TEXT("0 dungeons standing | surge in 25 days | 0 cities at risk")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarZeroDaysTest,
	"Cataclysm.EmpireStatusBar.ASurgeTheDaysOwedReachLandsWhenThePlayerLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarZeroDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	// 0 DAYS WITH DAYS OWED: the owed days reach the surge, and they run when the player leaves. Saying "today" here
	// would be false, since nothing resolves while the player is inside (the owner's decision of 2026-10-24).
	TestEqual(TEXT("a surge the days owed reach lands when the player leaves"),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(2, 0, 3, 1)),
			  FString(TEXT("2 dungeons standing | surge when you leave | 1 city at risk")));

	// 0 DAYS WITH NOTHING OWED: a surge already due, which `AdvanceDay` fires as the next day begins.
	TestEqual(TEXT("with nothing owed a surge already due lands with the next day"),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(2, 0, 0, 1)),
			  FString(TEXT("2 dungeons standing | surge tomorrow | 1 city at risk")));

	TestEqual(TEXT("and a surge one day off is tomorrow whether or not days are owed"),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(2, 1, 3, 1)),
			  FString(TEXT("2 dungeons standing | surge tomorrow | 1 city at risk")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarSameFiguresTest,
	"Cataclysm.EmpireStatusBar.TheFiguresAreTheSameOnlyWhenEveryOneIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarSameFiguresTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	// THE HUD KEEPS ITS LINE WHILE THE FIGURES ARE THE SAME, so a figure the comparison skipped would leave a stale
	// number on screen. Each field is changed alone below, and each change must be seen.
	const FCataclysmEmpireStatusFacts Drawn = BoundFacts(3, 4, 0, 2);

	TestTrue(TEXT("CONTROL: the same figures are the same"), Drawn.HasTheSameFiguresAs(BoundFacts(3, 4, 0, 2)));

	FCataclysmEmpireStatusFacts Unbound = Drawn;
	Unbound.bRunBound = false;
	TestFalse(TEXT("a run no longer bound is a change"), Drawn.HasTheSameFiguresAs(Unbound));
	TestFalse(TEXT("another dungeon standing is a change"), Drawn.HasTheSameFiguresAs(BoundFacts(4, 4, 0, 2)));
	TestFalse(TEXT("a day closer to the surge is a change"), Drawn.HasTheSameFiguresAs(BoundFacts(3, 3, 0, 2)));
	TestFalse(TEXT("a day owed is a change"), Drawn.HasTheSameFiguresAs(BoundFacts(3, 4, 1, 2)));
	TestFalse(TEXT("another city at risk is a change"), Drawn.HasTheSameFiguresAs(BoundFacts(3, 4, 0, 3)));

	return true;
}

// ---------------------------------------------------------------------------
// No run bound: nothing said, ruling (c)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarNoRunTest,
	"Cataclysm.EmpireStatusBar.NothingIsSaidWithNoRunBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarNoRunTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	TestFalse(TEXT("no run is not bound"), UCataclysmEmpireStatusBar::FactsOf(nullptr).bRunBound);
	TestEqual(TEXT("no run says nothing"),
			  UCataclysmEmpireStatusBar::LineFor(UCataclysmEmpireStatusBar::FactsOf(nullptr)), FString());

	// A RUN THAT HAS NOT BEGUN has no map, clock or scheduler, and is the same case.
	UCataclysmEmpireRun* NotStarted = NewObject<UCataclysmEmpireRun>();
	TestEqual(TEXT("a run that has not begun says nothing"),
			  UCataclysmEmpireStatusBar::LineFor(UCataclysmEmpireStatusBar::FactsOf(NotStarted)), FString());

	// THE FLAG DECIDES, NOT THE FIGURES: facts carrying numbers but no run still say nothing.
	FCataclysmEmpireStatusFacts Unbound = BoundFacts(3, 4, 0, 2);
	Unbound.bRunBound = false;
	TestEqual(TEXT("figures with no run bound say nothing"), UCataclysmEmpireStatusBar::LineFor(Unbound), FString());

	TestEqual(TEXT("no world says nothing"), UCataclysmEmpireStatusBar::LineForWorld(nullptr), FString());

	// THE CONTROL: the same calls on a begun run say something, so the empties above are not a function that always
	// answers empty.
	UCataclysmEmpireRun* Begun = BegunOnDayOne(nullptr);
	TestFalse(TEXT("CONTROL: a begun run says something"),
			  UCataclysmEmpireStatusBar::LineFor(UCataclysmEmpireStatusBar::FactsOf(Begun)).IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Cities at risk, ruling (a)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarCitiesTest,
	"Cataclysm.EmpireStatusBar.CitiesAtRiskAreDistinctStandingCitiesNotDungeons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarCitiesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	// A REAL RUN, WITH THE OPENING WAVE TAKEN OFF both lists, so the only dungeons standing are the ones put down here.
	UCataclysmEmpireRun* Run = BegunOnDayOne(nullptr);
	Run->Dungeons.Empty();
	Run->Clock->Timers.Empty();

	// THREE STANDING OUTPOSTS, found rather than assumed.
	TArray<int32> Outposts;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (Outposts.Num() < 3 && City.Tier == ECataclysmCityTier::Outpost && !City.bFallen)
		{
			Outposts.Add(City.CityId);
		}
	}
	if (!TestEqual(TEXT("set-up: the map has three Outposts standing"), Outposts.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("CONTROL: an empty board puts no city at risk"),
			  UCataclysmEmpireStatusBar::CitiesAtRiskIn(Run), 0);

	StandADungeonOn(*Run, 9801, Outposts[0]);
	StandADungeonOn(*Run, 9802, Outposts[0]);
	StandADungeonOn(*Run, 9803, Outposts[1]);

	TestEqual(TEXT("two dungeons on one city and one on another are two cities at risk, not three"),
			  UCataclysmEmpireStatusBar::CitiesAtRiskIn(Run), 2);

	StandADungeonOn(*Run, 9804, Outposts[2]);

	TestEqual(TEXT("CONTROL: a third standing city with a dungeon on it is at risk"),
			  UCataclysmEmpireStatusBar::CitiesAtRiskIn(Run), 3);

	// THE THIRD CITY FALLS, AND ITS DUNGEON STAYS: the map's fall does not touch the run's list, as a Fallen City
	// dungeon stands on a fallen city in play.
	if (!TestTrue(TEXT("set-up: the third city falls"), Run->Map->Fall(Outposts[2])))
	{
		return false;
	}

	TestEqual(TEXT("a fallen city with a dungeon still on it is not at risk"),
			  UCataclysmEmpireStatusBar::CitiesAtRiskIn(Run), 2);
	TestEqual(TEXT("and the dungeon on it still stands"), Run->DungeonCount(), 4);

	return true;
}

// ---------------------------------------------------------------------------
// The countdown, ruling (b)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarCountdownTest,
	"Cataclysm.EmpireStatusBar.TheCountdownCountsFromTheDayTheWalkHasReached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarCountdownTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	UCataclysmEmpireRun* Run = BegunOnDayOne(nullptr);
	if (!TestEqual(TEXT("set-up: the run is on day 1"), Run->Day(), 1))
	{
		return false;
	}

	// THE NEXT SURGE PUT HALF A DAY PAST DAY 11, so the rounding is seen as well: it lands on day 12.
	Run->Surges->NextSurgeDay = 11.5f;

	TestEqual(TEXT("CONTROL: with nothing owed, a surge ten and a half days off is eleven days off"),
			  UCataclysmEmpireStatusBar::DaysToNextSurgeIn(Run), 11);

	// THREE DAYS OWED: the walk has reached day 4, and the surge is 7.5 days past that.
	Run->OwedDays = 3;

	TestEqual(TEXT("three days owed count from the day the walk has reached: eight days off"),
			  UCataclysmEmpireStatusBar::DaysToNextSurgeIn(Run), 8);
	TestTrue(TEXT("and the line counts the same way"),
			 UCataclysmEmpireStatusBar::LineFor(UCataclysmEmpireStatusBar::FactsOf(Run))
				 .Contains(TEXT("surge in 8 days")));

	// ELEVEN DAYS OWED: the walk has reached day 12, past the surge's day, so it lands when the owed days run.
	Run->OwedDays = 11;

	TestEqual(TEXT("days owed past the surge's day leave nothing to count"),
			  UCataclysmEmpireStatusBar::DaysToNextSurgeIn(Run), 0);
	TestTrue(TEXT("and the line says the surge lands when the player leaves"),
			 UCataclysmEmpireStatusBar::LineFor(UCataclysmEmpireStatusBar::FactsOf(Run))
				 .Contains(TEXT("surge when you leave")));

	return true;
}

// ---------------------------------------------------------------------------
// Gathered from the run the world holds
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarWorldTest,
	"Cataclysm.EmpireStatusBar.TheBarGathersItsFiguresFromTheRunTheWorldHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarWorldTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireStatusBarTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}

	// A GAME INSTANCE ON THE WORLD, which a test world lacks; `CataclysmEmpireRunTierTests.cpp` attaches one the same
	// way. Holding no run yet: Play in `L_Dungeon` with no campaign.
	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	World->SetGameInstance(Instance);

	TestEqual(TEXT("CONTROL: a game instance holding no run gives the bar nothing to say"),
			  UCataclysmEmpireStatusBar::LineForWorld(World), FString());

	// A REAL RUN, ITS OPENING WAVE STANDING, and one more dungeon on the first wave dungeon's city so the dungeon count
	// and the city count are bound to differ.
	UCataclysmEmpireRun* Run = BegunOnDayOne(Instance);
	Instance->EmpireRun = Run;

	if (!TestTrue(TEXT("set-up: the opening wave left dungeons standing"), Run->Dungeons.Num() > 0))
	{
		World->DestroyWorld(false);
		return false;
	}

	FCataclysmDungeon Extra = Run->Dungeons[0];
	Extra.DungeonId = 9901;
	Run->Dungeons.Add(Extra);
	Run->Clock->AddDungeon(Extra.DungeonId, Extra.Floors);

	const int32 Standing = Run->Dungeons.Num();
	const int32 Cities = StandingCitiesCarryingADungeon(*Run);
	const int32 Days = FMath::CeilToInt(Run->Surges->DaysUntilNextSurge(Run->Day()));

	// THE SET-UP LINES BELOW ARE READ FROM THE RUN, NOT ASSUMED, and the code says why each holds on day 1 whatever the
	// seed: the opening wave lands on day 1 (`NextSurgeDay` starts at 0, and `AdvanceDay` asks `IsDue` of day 1) and puts
	// the next surge `IntervalDays`, 90, after it; no timer can run out on the day it starts (`ResolveDaysFor` gives at
	// least 11.6 days, less 15% jitter); and the one bite on day 1 is a Siege's 1% of a full city. Recorded in
	// `docs/DECISIONS.md`. The extra dungeon makes "more dungeons than cities" true by construction.

	if (!TestTrue(TEXT("set-up: the world's game instance gives this run"),
				  UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ false) == Run)
		|| !TestEqual(TEXT("set-up: no city has fallen on day 1"), Run->Map->FallenCityCount(), 0)
		|| !TestTrue(TEXT("set-up: more dungeons stand than cities carry them, so the two counts differ"),
					 Cities < Standing)
		|| !TestTrue(TEXT("set-up: the next surge is more than three days off"),
					 Run->Surges->DaysUntilNextSurge(Run->Day()) > 3.0f))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FCataclysmEmpireStatusFacts Gathered = UCataclysmEmpireStatusBar::FactsForWorld(World);

	TestTrue(TEXT("the bar finds the run"), Gathered.bRunBound);
	TestEqual(TEXT("the bar counts every dungeon standing"), Gathered.DungeonsStanding, Standing);
	TestEqual(TEXT("and the distinct standing cities they stand on"), Gathered.CitiesAtRisk, Cities);
	TestEqual(TEXT("and the days to the next surge, rounded up"), Gathered.DaysToNextSurge, Days);
	TestEqual(TEXT("the world's line says those three figures"),
			  UCataclysmEmpireStatusBar::LineForWorld(World),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(Standing, Days, 0, Cities)));

	// A WALK THAT OWES THREE DAYS: the countdown moves three days closer while the run's day stands still.
	Run->OwedDays = 3;
	const int32 DaysFromTheWalk = FMath::CeilToInt(Run->Surges->DaysUntilNextSurge(Run->Day() + 3));

	TestEqual(TEXT("with three days owed the countdown counts from the walk's day"),
			  UCataclysmEmpireStatusBar::FactsForWorld(World).DaysToNextSurge, DaysFromTheWalk);
	TestEqual(TEXT("and the world's line says so"),
			  UCataclysmEmpireStatusBar::LineForWorld(World),
			  UCataclysmEmpireStatusBar::LineFor(BoundFacts(Standing, DaysFromTheWalk, 3, Cities)));

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Where it goes
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireStatusBarRoomTest,
	"Cataclysm.EmpireStatusBar.TheLineShrinksToStayClearOfTheCreaturePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireStatusBarRoomTest::RunTest(const FString& Parameters)
{
	// HAND-WORKED AT 1920 WIDE. The creature panel is at least 28% of the screen and centred, so its left edge is at
	// 1920 x 0.72 / 2 = 691.2 at the narrowest; the line starts at 28 and stops 16 short of that: 647.2.
	TestEqual(TEXT("at 1920 wide the line has 647.2 pixels beside the creature panel"),
			  UCataclysmEmpireStatusBar::RoomBesideThePanel(1920.0f), 647.2f, 0.01f);

	TestEqual(TEXT("a line that fits is drawn at its own size"),
			  UCataclysmEmpireStatusBar::ScaleToFit(400.0f, 1920.0f), UCataclysmEmpireStatusBar::BaseScale, 0.0001f);

	TestEqual(TEXT("a line with no width is drawn at its own size"),
			  UCataclysmEmpireStatusBar::ScaleToFit(0.0f, 1920.0f), UCataclysmEmpireStatusBar::BaseScale, 0.0001f);

	// A LINE 1000 PIXELS WIDE AT ITS OWN SIZE IS SHRUNK, and where it then ends is worked out from the scale.
	const float Shrunk = UCataclysmEmpireStatusBar::ScaleToFit(1000.0f, 1920.0f);
	TestTrue(TEXT("a line too wide for the room is shrunk"), Shrunk < UCataclysmEmpireStatusBar::BaseScale);

	const float RightEdge =
		UCataclysmEmpireStatusBar::LeftPx + 1000.0f * Shrunk / UCataclysmEmpireStatusBar::BaseScale;
	const float PanelLeft = 1920.0f * (1.0f - UCataclysmCreaturePanel::MinimumWidthShare) * 0.5f;
	TestTrue(TEXT("until it ends clear of the creature panel at its narrowest"),
			 RightEdge + UCataclysmEmpireStatusBar::GapToPanelPx <= PanelLeft + 0.01f);

	TestEqual(TEXT("and a line far too wide for a narrow screen stops at the smallest scale"),
			  UCataclysmEmpireStatusBar::ScaleToFit(5000.0f, 1024.0f), UCataclysmEmpireStatusBar::SmallestScale,
			  0.0001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
