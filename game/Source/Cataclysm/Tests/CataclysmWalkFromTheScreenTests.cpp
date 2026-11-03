// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Interface/CataclysmCityScreenWidget.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmEmpireMapWidget.h"
#include "Interface/CataclysmWalkFromTheScreen.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Walking a dungeon from the empire screens. Issue #2715, 2026-11-03.
 *
 * THE DESIGN, `docs/Cataclysm_GDD_v2.md` under "How many Cataclysms must be finished to unlock the Cataclysm dungeon":
 * "The empire map shows a marker for it that the player enters from." Until this the only way into an empire dungeon
 * was typing `Cataclysm.EnterDungeon <id>` in a dungeon level. Now each dungeon line on a city's screen has a Walk
 * button under it, and the Cataclysm boss dungeon's marker has a Walk box under it; a press calls
 * `ACataclysmDungeonGameMode::EnterEmpireDungeon` through `FCataclysmWalkFromTheScreen::Walk`, or shows why not.
 *
 * `Cataclysm.WalkFromTheScreen.` IS A NEW GROUP and selects these and nothing else.
 *
 * WHAT NO TEST HERE CAN DO IS CLICK. The automation command passes `-nullrhi`, so nothing is drawn and a headless
 * widget's `BindWidget` labels and panels are null: no Walk button is made and no label can be read. What is tested is
 * what the buttons are made from (`UCataclysmCityScreenLayout::DungeonIdsFor`, the Walk box's place and name), and the
 * handler each button calls, reached by the name the button carries -- the same lookup a real press goes through.
 * Closing the empire screens needs a player controller, which a headless test has none of, so that half is not run.
 *
 * THE RUN IS HANDED OVER, to the screen and to the game mode both, through their test seams: a world built by
 * `UWorld::CreateWorld` has no game instance to ask. In a running game both read the game instance's run.
 */
namespace CataclysmWalkFromTheScreenTest
{
	/** The city screen tests' run: Static, seed 4242, day 0, nothing standing anywhere yet. */
	UCataclysmEmpireRun* MakeWalkRun()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 4242, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		return Run;
	}

	/**
	 * A dungeon put on a city by hand with a chosen identifier and timer, as the city screen tests place theirs.
	 * `WalkDays` is left at nought, so a floor costs `UCataclysmDayClock::DaysPerFloor`, the starting day.
	 */
	void PlaceWalkDungeon(UCataclysmEmpireRun& Run, int32 CityId, int32 DungeonId, int32 Floors, float ResolveDays)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Floors = Floors;
		Dungeon.ResolveDays = ResolveDays;

		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/**
	 * `Cataclysm.BossOnTheMap.`'s run with its boss open and nothing defeated before or after: tier 1, seed 11, four
	 * days, then its one Cataclysm's quest dungeons built by hand on the first city and cleared through `ClearDungeon`.
	 */
	UCataclysmEmpireRun* MakeWalkRunWithItsBossOpen()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 11, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* InDifficultyTier */ 1);
		Run->AdvanceDays(4);

		const ECataclysmType Cataclysm = Run->ActiveCataclysms[0];
		const int32 Needed = UCataclysmRoster::QuestObjectivesFor(Cataclysm);
		for (int32 Index = 0; Index < Needed; ++Index)
		{
			FCataclysmDungeon Quest;
			Quest.DungeonId = Run->NextDungeonId++;
			Quest.Type = ECataclysmDungeonType::Quest;
			Quest.Cataclysm = Cataclysm;
			Quest.CityId = Run->Map->Cities[0].CityId;

			Run->Dungeons.Add(Quest);
			Run->ClearDungeon(Quest.DungeonId);
		}
		return Run;
	}

	/** A test world holding a dungeon game mode bound to nothing, as `Cataclysm.DungeonMode.`'s are made. */
	struct FWalkWorld
	{
		UWorld* World = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
	};

	FWalkWorld MakeWalkWorld()
	{
		FWalkWorld Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (Out.World != nullptr)
		{
			Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();
		}
		return Out;
	}
}

// ---------------------------------------------------------------------------
// What the buttons are made from
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWalkFromTheScreenCityButtonsTest,
	"Cataclysm.WalkFromTheScreen.TheCityScreenOffersOneWalkForEachDungeonLineInTheOrderItListsThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three dungeons placed out of order on city 0 of a fresh run (700: 30 floors, 40 days; 701: 12 and 9; 702: 20 and 25).
 * `DungeonIdsFor` lists 701, 702, 700, soonest to bite first, one per line of `DungeonLinesFor` and the line at the same
 * index describes the dungeon at that index. A city with nothing standing on it offers no Walk (CONTROL). Every
 * button says "Walk", and its name stands for its dungeon and for nothing else on either screen. Prints "WALK CITY".
 */
bool FCataclysmWalkFromTheScreenCityButtonsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWalkFromTheScreenTest;

	UCataclysmEmpireRun* Run = MakeWalkRun();
	const int32 CityId = Run->Map->Cities[0].CityId;

	// CONTROL: A FRESH RUN STANDS NOTHING ON ANY CITY, so there is nothing to walk and no button.
	if (!TestEqual(TEXT("set-up: a fresh run has nothing standing on city 0"), Run->DungeonsOn(CityId).Num(), 0))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: a city with nothing standing on it offers no Walk"),
			  UCataclysmCityScreenLayout::DungeonIdsFor(Run, CityId).Num(), 0);
	TestEqual(TEXT("and no run offers none"), UCataclysmCityScreenLayout::DungeonIdsFor(nullptr, CityId).Num(), 0);

	// PLACED OUT OF ORDER ON PURPOSE, as `Cataclysm.CityScreen.DungeonsAreListedSoonestToBiteFirst` places them.
	PlaceWalkDungeon(*Run, CityId, /* DungeonId */ 700, /* Floors */ 30, /* ResolveDays */ 40.0f);
	PlaceWalkDungeon(*Run, CityId, /* DungeonId */ 701, /* Floors */ 12, /* ResolveDays */ 9.0f);
	PlaceWalkDungeon(*Run, CityId, /* DungeonId */ 702, /* Floors */ 20, /* ResolveDays */ 25.0f);

	const TArray<int32> Ids = UCataclysmCityScreenLayout::DungeonIdsFor(Run, CityId);
	const TArray<FString> Lines = UCataclysmCityScreenLayout::DungeonLinesFor(Run, CityId);
	AddInfo(FString::Printf(TEXT("WALK CITY: %d ids, %d lines"), Ids.Num(), Lines.Num()));

	if (!TestEqual(TEXT("one Walk for each of the three dungeons"), Ids.Num(), 3)
		|| !TestEqual(TEXT("and one line for each, so one Walk under each line"), Lines.Num(), Ids.Num()))
	{
		return false;
	}

	TestEqual(TEXT("the first Walk is the dungeon due in nine days"), Ids[0], 701);
	TestEqual(TEXT("the second the one due in 25"), Ids[1], 702);
	TestEqual(TEXT("the third the one due in 40"), Ids[2], 700);

	// THE LINE ABOVE EACH WALK DESCRIBES THE DUNGEON IT WALKS, checked by the floors, which is not what is sorted on.
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		const FCataclysmDungeon* Dungeon = Run->FindDungeon(Ids[Index]);
		TestTrue(FString::Printf(TEXT("line %d describes the dungeon its Walk enters"), Index),
				 Dungeon != nullptr && Lines[Index].Contains(FString::Printf(TEXT("%d floors"), Dungeon->Floors)));
	}

	// WHAT THE BUTTON SAYS AND WHICH DUNGEON ITS NAME STANDS FOR.
	TestEqual(TEXT("every Walk button says Walk"), FCataclysmWalkFromTheScreen::ButtonText(), FString(TEXT("Walk")));
	TestEqual(TEXT("a Walk button's name stands for its dungeon"),
			  FCataclysmWalkFromTheScreen::DungeonForButton(FCataclysmWalkFromTheScreen::ButtonNameFor(701)), 701);
	TestEqual(TEXT("and dungeon 0's for dungeon 0"),
			  FCataclysmWalkFromTheScreen::DungeonForButton(FCataclysmWalkFromTheScreen::ButtonNameFor(0)), 0);
	TestEqual(TEXT("a city's box name is no Walk"), FCataclysmWalkFromTheScreen::DungeonForButton(FName(TEXT("City7"))),
			  static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("nor is the boss marker's"),
			  FCataclysmWalkFromTheScreen::DungeonForButton(FName(TEXT("CataclysmBossDungeon"))),
			  static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("nor the prefix with no number"),
			  FCataclysmWalkFromTheScreen::DungeonForButton(FName(TEXT("WalkDungeon"))), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("nor no name"), FCataclysmWalkFromTheScreen::DungeonForButton(NAME_None),
			  static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWalkFromTheScreenBossBoxTest,
	"Cataclysm.WalkFromTheScreen.TheBossMarkerHasAWalkBoxUnderItThatCoversNoCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Walk box is one city wide and one tall, under the boss marker with a gap of four, and on every panel the
 * marker's test uses it is inside the panel, clear of the marker and clear of every city (100 placements). Its name is
 * the boss's Walk name while the boss stands, and no name while none does (CONTROL). Prints "WALK BOX".
 */
bool FCataclysmWalkFromTheScreenBossBoxTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWalkFromTheScreenTest;

	// AT A SCALE OF ONE: 172 by 48, its left edge on the marker's (24 in), its top 4 under the marker's bottom (24 + 48),
	// so its centre is 24 + 86 across and 24 + 48 + 4 + 24 down.
	const FVector2D AtOne = UCataclysmEmpireMapLayout::CataclysmDungeonWalkPosition(1.0f);
	const FVector2D SizeAtOne = UCataclysmEmpireMapLayout::CataclysmDungeonWalkSize(1.0f);
	TestEqual(TEXT("at a scale of one the Walk box is one city wide"), SizeAtOne.X, 172.0, 0.01);
	TestEqual(TEXT("and one tall"), SizeAtOne.Y, 48.0, 0.01);
	TestEqual(TEXT("its centre is a margin and half its width from the left"), AtOne.X, 110.0, 0.01);
	TestEqual(TEXT("and a margin, the marker, the gap and half its height from the top"), AtOne.Y, 100.0, 0.01);

	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();

	// THE MARKER TEST'S FOUR PANELS: the diamond with its margins exactly, two the city overlap test uses, and the one
	// the project owner's window gave on 2026-08-31.
	const FVector2D Panels[] = {
		UCataclysmEmpireMapLayout::DiamondSize()
			+ FVector2D(2.0 * UCataclysmEmpireMapLayout::FitMarginPx, 2.0 * UCataclysmEmpireMapLayout::FitMarginPx),
		FVector2D(1600.0, 900.0),
		FVector2D(700.0, 1200.0),
		FVector2D(1852.0, 528.0),
	};

	int32 Checked = 0;
	for (const FVector2D& Panel : Panels)
	{
		const float Scale = UCataclysmEmpireMapLayout::ScaleToFit(Panel);
		const FVector2D Box = UCataclysmEmpireMapLayout::CitySize(Scale);
		const FVector2D Walk = UCataclysmEmpireMapLayout::CataclysmDungeonWalkSize(Scale);
		const FVector2D At = UCataclysmEmpireMapLayout::CataclysmDungeonWalkPosition(Scale);
		const FVector2D Marker = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(Scale);
		const FVector2D MarkerAt = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(Scale);

		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the Walk box is inside the panel"), Panel.X, Panel.Y),
				 At.X - Walk.X * 0.5 >= -0.01 && At.Y - Walk.Y * 0.5 >= -0.01
					 && At.X + Walk.X * 0.5 <= Panel.X + 0.01 && At.Y + Walk.Y * 0.5 <= Panel.Y + 0.01);
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the Walk box is under the marker, not on it"), Panel.X,
								 Panel.Y),
				 At.Y - Walk.Y * 0.5 >= MarkerAt.Y + Marker.Y * 0.5 - 0.01);

		// TWO RECTANGLES PLACED BY THEIR CENTRES MISS EACH OTHER when they are clear on either axis by half of both.
		for (const FCataclysmCity& City : Map->Cities)
		{
			const FVector2D Gap =
				(UCataclysmEmpireMapLayout::PositionFor(City.R, City.C, Scale, Panel) - At).GetAbs();
			TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the Walk box covers no part of %s"), Panel.X,
									 Panel.Y, *City.Name),
					 Gap.X >= (Box.X + Walk.X) * 0.5 - 0.01 || Gap.Y >= (Box.Y + Walk.Y) * 0.5 - 0.01);
			++Checked;
		}
	}
	AddInfo(FString::Printf(TEXT("WALK BOX: %d city placements checked against the Walk box"), Checked));
	TestEqual(TEXT("twenty-five cities on each of four panels"), Checked, 100);

	// ITS NAME IS THE STANDING BOSS'S WALK, so a press walks the dungeon standing now.
	UCataclysmEmpireRun* BossRun = MakeWalkRunWithItsBossOpen();
	const FCataclysmDungeon* Boss = BossRun->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: finishing the run's one Cataclysm opens the boss"), Boss))
	{
		return false;
	}
	TestEqual(TEXT("while the boss stands the Walk box carries its Walk name"),
			  UCataclysmEmpireMapLayout::CataclysmDungeonWalkButtonName(BossRun),
			  FCataclysmWalkFromTheScreen::ButtonNameFor(Boss->DungeonId));
	TestEqual(TEXT("CONTROL: a run with no boss standing gives it no name"),
			  UCataclysmEmpireMapLayout::CataclysmDungeonWalkButtonName(MakeWalkRun()), FName(NAME_None));
	TestEqual(TEXT("and nor does no run"), UCataclysmEmpireMapLayout::CataclysmDungeonWalkButtonName(nullptr),
			  FName(NAME_None));
	return true;
}

// ---------------------------------------------------------------------------
// Pressing Walk
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWalkFromTheScreenCityPressTest,
	"Cataclysm.WalkFromTheScreen.PressingWalkOnACityScreenInADungeonLevelEntersThatDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A fresh run (day 0, nothing owed) with two dungeons placed by hand on city 0 at the starting day a floor: 701, 12
 * floors, and 702, 20. The screen's Walk under 701 enters it: the game mode walks 701, 12 floors deep, on floor 1,
 * owing the one day arriving costs, on day 0 still, with no refusal. Then Walk under 702 while 701 is being walked
 * leaves 701 -- its one owed day runs, so day 1 -- and enters 702, owing one day; 701 stays on the map. Prints
 * "WALK PRESS".
 */
bool FCataclysmWalkFromTheScreenCityPressTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWalkFromTheScreenTest;

	FWalkWorld Scene = MakeWalkWorld();
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Scene.Mode))
	{
		return false;
	}

	UCataclysmEmpireRun* Run = MakeWalkRun();
	const int32 CityId = Run->Map->Cities[0].CityId;
	PlaceWalkDungeon(*Run, CityId, /* DungeonId */ 701, /* Floors */ 12, /* ResolveDays */ 9.0f);
	PlaceWalkDungeon(*Run, CityId, /* DungeonId */ 702, /* Floors */ 20, /* ResolveDays */ 25.0f);
	Scene.Mode->SetEmpireRunForTests(Run);

	if (!TestEqual(TEXT("set-up: the game mode walks nothing yet"), Scene.Mode->EmpireDungeonId,
				   static_cast<int32>(INDEX_NONE))
		|| !TestEqual(TEXT("set-up: a fresh run is on day 0"), Run->Day(), 0)
		|| !TestEqual(TEXT("set-up: and owes nothing"), Run->DaysOwed(), 0)
		|| !TestEqual(TEXT("set-up: and has awarded no empire points"), Run->LastEmpirePointsAwarded, 0.0))
	{
		return false;
	}

	UCataclysmCityScreenWidget* Screen = NewObject<UCataclysmCityScreenWidget>();
	Screen->SetRunForTests(Run);
	Screen->SetDungeonModeForTests(Scene.Mode);
	Screen->SetCity(CityId);

	Screen->ClickWalkForTests(701);
	AddInfo(FString::Printf(TEXT("WALK PRESS: after 701, walking %d, floor %d, %d floors, day %d, owing %d, refusal \"%s\""),
							Scene.Mode->EmpireDungeonId, Scene.Mode->FloorNumber, Scene.Mode->TotalFloors, Run->Day(),
							Run->DaysOwed(), *Screen->LastRefusal()));

	TestEqual(TEXT("pressing Walk under a dungeon's line enters that dungeon"), Scene.Mode->EmpireDungeonId, 701);
	TestEqual(TEXT("with no refusal shown"), Screen->LastRefusal(), FString());
	TestEqual(TEXT("as deep as that dungeon is"), Scene.Mode->TotalFloors, 12);
	TestEqual(TEXT("with the player on its floor 1"), Scene.Mode->FloorNumber, 1);
	TestEqual(TEXT("owing the one day arriving costs"), Run->DaysOwed(), 1);
	TestEqual(TEXT("and running none of it while inside"), Run->Day(), 0);

	// AND WALK UNDER ANOTHER DUNGEON, MID-WALK, LEAVES THE FIRST, as `Cataclysm.EnterDungeon` typed mid-walk does.
	Screen->ClickWalkForTests(702);
	AddInfo(FString::Printf(TEXT("WALK PRESS: after 702, walking %d, floor %d, %d floors, day %d, owing %d, refusal \"%s\""),
							Scene.Mode->EmpireDungeonId, Scene.Mode->FloorNumber, Scene.Mode->TotalFloors, Run->Day(),
							Run->DaysOwed(), *Screen->LastRefusal()));

	TestEqual(TEXT("Walk under a second dungeon mid-walk enters the second"), Scene.Mode->EmpireDungeonId, 702);
	TestEqual(TEXT("with no refusal shown"), Screen->LastRefusal(), FString());
	TestEqual(TEXT("as deep as it is"), Scene.Mode->TotalFloors, 20);
	TestEqual(TEXT("leaving the first ran the one day it owed"), Run->Day(), 1);
	TestEqual(TEXT("and the second owes the day arriving costs"), Run->DaysOwed(), 1);
	TestNotNull(TEXT("and the first, left uncleared, is still on the map"), Run->FindDungeon(701));

	// AND NO EMPIRE POINTS, since issue #52: only `ClearDungeon` awards them, and leaving a walk clears nothing.
	TestEqual(TEXT("and leaving it uncleared awarded no empire points"), Run->LastEmpirePointsAwarded, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWalkFromTheScreenBossPressTest,
	"Cataclysm.WalkFromTheScreen.PressingWalkUnderTheBossMarkerEntersTheBossDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Cataclysm.BossOnTheMap.`'s run with its boss open. The map's Walk box, pressed through the name it carries, enters
 * the boss dungeon: the game mode walks it, as deep as it is, with no refusal, and it still stands. Prints "WALK BOSS".
 */
bool FCataclysmWalkFromTheScreenBossPressTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWalkFromTheScreenTest;

	FWalkWorld Scene = MakeWalkWorld();
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Scene.Mode))
	{
		return false;
	}

	UCataclysmEmpireRun* Run = MakeWalkRunWithItsBossOpen();
	const FCataclysmDungeon* Boss = Run->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: finishing the run's one Cataclysm opens the boss"), Boss))
	{
		return false;
	}
	const int32 BossId = Boss->DungeonId;
	const int32 BossFloors = Boss->Floors;
	const double AwardBefore = Run->LastEmpirePointsAwarded;
	Scene.Mode->SetEmpireRunForTests(Run);

	UCataclysmEmpireMapWidget* Map = NewObject<UCataclysmEmpireMapWidget>();
	Map->SetRunForTests(Run);
	Map->SetDungeonModeForTests(Scene.Mode);
	Map->Refresh();

	Map->ClickCataclysmDungeonWalkForTests();
	AddInfo(FString::Printf(TEXT("WALK BOSS: boss %d of %d floors; walking %d, %d floors, refusal \"%s\"; last award %.3f, "
								 "%.3f before"),
							BossId, BossFloors, Scene.Mode->EmpireDungeonId, Scene.Mode->TotalFloors,
							*Map->LastWalkRefusal(), Run->LastEmpirePointsAwarded, AwardBefore));

	TestEqual(TEXT("pressing Walk under the boss marker enters the boss dungeon"), Scene.Mode->EmpireDungeonId, BossId);
	TestEqual(TEXT("as deep as the boss dungeon is"), Scene.Mode->TotalFloors, BossFloors);
	TestEqual(TEXT("with no refusal written under the map"), Map->LastWalkRefusal(), FString());
	TestNotNull(TEXT("and the boss dungeon still stands while it is walked"), Run->FindCataclysmDungeon());

	// AND NO EMPIRE POINTS, since issue #52: entering clears nothing, so the last award is still the last quest
	// dungeon's, which opened the boss.
	TestEqual(TEXT("and entering it awarded no empire points"), Run->LastEmpirePointsAwarded, AwardBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWalkFromTheScreenRefusalTest,
	"Cataclysm.WalkFromTheScreen.ARefusedWalkIsShownAndEntersNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every way a press is refused shows its reason and enters nothing. On the map with no dungeon game mode: "This level
 * is not a dungeon...". On the city screen, with a game mode that has no run of its own, `EnterEmpireDungeon`'s false
 * is shown as "Dungeon 700 could not be entered."; with the run, a dungeon not standing is "No dungeon 999 is standing
 * on the map.". CONTROL: the same press on 700 then enters it. A floor down, owing two days on day 0, Walk on 700 again
 * is refused, "You are walking dungeon 700 now.", and the player stays on floor 2 with both days owed and none run.
 * Prints "WALK REFUSED".
 */
bool FCataclysmWalkFromTheScreenRefusalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWalkFromTheScreenTest;

	// ---- the map, in a level with no dungeon game mode ---------------------
	UCataclysmEmpireRun* BossRun = MakeWalkRunWithItsBossOpen();
	if (!TestNotNull(TEXT("set-up: the boss is open"), BossRun->FindCataclysmDungeon()))
	{
		return false;
	}

	UCataclysmEmpireMapWidget* Map = NewObject<UCataclysmEmpireMapWidget>();
	Map->SetRunForTests(BossRun);
	Map->Refresh();
	Map->ClickCataclysmDungeonWalkForTests();
	AddInfo(FString::Printf(TEXT("WALK REFUSED: map \"%s\""), *Map->LastWalkRefusal()));

	TestEqual(TEXT("in a level that is not a dungeon the boss's Walk is refused, and the line under the map says why"),
			  Map->LastWalkRefusal(), FCataclysmWalkFromTheScreen::NotADungeonLevelText());
	TestNotNull(TEXT("and the boss dungeon still stands"), BossRun->FindCataclysmDungeon());
	TestEqual(TEXT("with no run a press is refused for that"),
			  FCataclysmWalkFromTheScreen::RefusalFor(nullptr, nullptr, 700), FString(TEXT("No run has been started.")));

	// ---- the city screen, in a dungeon level --------------------------------
	FWalkWorld Scene = MakeWalkWorld();
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Scene.Mode))
	{
		return false;
	}

	UCataclysmEmpireRun* Run = MakeWalkRun();
	const int32 CityId = Run->Map->Cities[0].CityId;
	PlaceWalkDungeon(*Run, CityId, /* DungeonId */ 700, /* Floors */ 12, /* ResolveDays */ 9.0f);

	UCataclysmCityScreenWidget* Screen = NewObject<UCataclysmCityScreenWidget>();
	Screen->SetRunForTests(Run);
	Screen->SetDungeonModeForTests(Scene.Mode);
	Screen->SetCity(CityId);

	// THE GAME MODE'S OWN REFUSAL. Given no run, `EnterEmpireDungeon` answers false, and the screen shows that.
	Screen->ClickWalkForTests(700);
	AddInfo(FString::Printf(TEXT("WALK REFUSED: no run on the game mode \"%s\""), *Screen->LastRefusal()));
	TestEqual(TEXT("a game mode that cannot enter it refuses, and the screen shows that refusal"), Screen->LastRefusal(),
			  FString(TEXT("Dungeon 700 could not be entered.")));
	TestEqual(TEXT("and nothing is walked"), Scene.Mode->EmpireDungeonId, static_cast<int32>(INDEX_NONE));

	Scene.Mode->SetEmpireRunForTests(Run);

	// A DUNGEON NOT STANDING ON THE MAP.
	Screen->ClickWalkForTests(999);
	AddInfo(FString::Printf(TEXT("WALK REFUSED: not standing \"%s\""), *Screen->LastRefusal()));
	TestEqual(TEXT("a dungeon not on the map is refused, and the screen says so"), Screen->LastRefusal(),
			  FString(TEXT("No dungeon 999 is standing on the map.")));
	TestEqual(TEXT("and still nothing is walked"), Scene.Mode->EmpireDungeonId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and nothing is owed"), Run->DaysOwed(), 0);

	// CONTROL: THE SAME PRESS ON A STANDING DUNGEON ENTERS IT, so the refusals above are the screen refusing and not
	// a press that could never work.
	Screen->ClickWalkForTests(700);
	if (!TestEqual(TEXT("CONTROL: Walk on a standing dungeon enters it"), Scene.Mode->EmpireDungeonId, 700)
		|| !TestEqual(TEXT("CONTROL: and shows no refusal"), Screen->LastRefusal(), FString()))
	{
		return false;
	}

	// THE DUNGEON BEING WALKED, ONE FLOOR DOWN. Arriving owed a day and the descent another; neither has run.
	if (!TestTrue(TEXT("set-up: the stairs take the player down a floor"), Scene.Mode->GoDownOneFloor())
		|| !TestEqual(TEXT("set-up: on floor 2"), Scene.Mode->FloorNumber, 2)
		|| !TestEqual(TEXT("set-up: owing two days"), Run->DaysOwed(), 2)
		|| !TestEqual(TEXT("set-up: on day 0"), Run->Day(), 0))
	{
		return false;
	}

	Screen->ClickWalkForTests(700);
	AddInfo(FString::Printf(TEXT("WALK REFUSED: walking it \"%s\", floor %d, day %d, owing %d"), *Screen->LastRefusal(),
							Scene.Mode->FloorNumber, Run->Day(), Run->DaysOwed()));
	TestEqual(TEXT("pressing Walk on the dungeon being walked is refused, and says why"), Screen->LastRefusal(),
			  FString(TEXT("You are walking dungeon 700 now.")));
	TestEqual(TEXT("and the player is still on floor 2"), Scene.Mode->FloorNumber, 2);
	TestEqual(TEXT("and still owes the walk's two days"), Run->DaysOwed(), 2);
	TestEqual(TEXT("and none of them has run"), Run->Day(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
