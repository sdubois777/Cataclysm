// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Engine/World.h"
#include "Framework/Application/NavigationConfig.h"
#include "Framework/Application/SlateApplication.h"
#include "Interface/CataclysmFloorMap.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the map Tab shows. Issue #2805, from the owner's playtest of 2026-11-21: "Tab brings up a map overlay so
 * you can make sure you aren't running in circles".
 *
 * WHAT NO TEST HERE CAN SEE: the map on the screen. `AHUD::DrawHUD` does not run under -nullrhi (issue #650), so these
 * hold the statics `ACataclysmHUD::DrawFloorMap` draws from, and the seen-cell record on the floor.
 */

namespace CataclysmFloorMapTest
{
	using Map = UCataclysmFloorMap;

	/** A floor of this size, every cell walkable. */
	static FCataclysmFloorPlan OpenRoom(int32 Width, int32 Height)
	{
		FCataclysmFloorPlan Plan;
		Plan.Reset(Width, Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Plan.Carve(FIntPoint(X, Y));
			}
		}
		return Plan;
	}

	static int32 CountSeen(const FCataclysmFloorPlan& Plan, const TBitArray<>& Seen)
	{
		int32 Count = 0;
		for (int32 Y = 0; Y < Plan.Height; ++Y)
		{
			for (int32 X = 0; X < Plan.Width; ++X)
			{
				Count += Map::IsSeen(Plan, Seen, FIntPoint(X, Y)) ? 1 : 0;
			}
		}
		return Count;
	}

	/** Two points agree, axis by axis. The automation base has no overload for a 2D vector. */
	static void ExpectPoint(FAutomationTestBase& Test, const FString& What, const FVector2D& Actual,
							const FVector2D& Expected, double Tolerance = 0.001)
	{
		Test.TestEqual(What + TEXT(", across"), Actual.X, Expected.X, Tolerance);
		Test.TestEqual(What + TEXT(", down"), Actual.Y, Expected.Y, Tolerance);
	}

	/** The 1080p layout every placement test reads. */
	static FCataclysmFloorMapLayout At1080()
	{
		return Map::LayoutFor(FVector2D(1920.0, 1080.0));
	}
}

// --------------------------------------------------------------------------
// What has been seen
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapRevealsWithinThreeCellsNotThroughWalls,
	"Cataclysm.FloorMap.TheCellsWithinThreeAreSeenAndAWallHidesWhatIsBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapRevealsWithinThreeCellsNotThroughWalls::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	TestEqual(TEXT("the reveal reaches three cells"), Map::RevealRadiusCells, 3);

	// THE CONTROL: an open room. From its middle, everything within three cells is seen and nothing further.
	FCataclysmFloorPlan Open = OpenRoom(11, 11);
	TBitArray<> Seen;
	const FIntPoint Middle(5, 5);
	const int32 Revealed = Map::RevealAround(Open, Seen, Middle, Map::RevealRadiusCells);
	TestTrue(TEXT("three cells up is seen"), Map::IsSeen(Open, Seen, FIntPoint(8, 5)));
	TestTrue(TEXT("three cells right is seen"), Map::IsSeen(Open, Seen, FIntPoint(5, 8)));
	TestTrue(TEXT("two by two diagonally, 2.83 cells, is seen"), Map::IsSeen(Open, Seen, FIntPoint(7, 7)));
	TestFalse(TEXT("four cells up is not"), Map::IsSeen(Open, Seen, FIntPoint(9, 5)));
	TestFalse(TEXT("three by three diagonally, 4.24 cells, is not"), Map::IsSeen(Open, Seen, FIntPoint(8, 8)));
	TestEqual(TEXT("the count returned is the cells now seen"), Revealed, CountSeen(Open, Seen));

	// A WALL ACROSS THE LINE: the same room with cell (7, 5) solid. The wall itself is seen; (8, 5) behind it is not.
	FCataclysmFloorPlan Walled = OpenRoom(11, 11);
	Walled.Fill(FIntPoint(7, 5));
	TBitArray<> SeenWalled;
	Map::RevealAround(Walled, SeenWalled, Middle, Map::RevealRadiusCells);
	TestTrue(TEXT("the wall's own cell is seen"), Map::IsSeen(Walled, SeenWalled, FIntPoint(7, 5)));
	TestFalse(TEXT("**the cell behind the wall is not**"), Map::IsSeen(Walled, SeenWalled, FIntPoint(8, 5)));
	TestTrue(TEXT("and it was, in the room without the wall"), Map::IsSeen(Open, Seen, FIntPoint(8, 5)));
	TestTrue(TEXT("a line that misses the wall still sees"), Map::IsSeen(Walled, SeenWalled, FIntPoint(8, 4)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapRevealsOnlyFromTheFloor,
	"Cataclysm.FloorMap.NothingIsSeenFromOffTheFloorAndOnlyNewCellsAreCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapRevealsOnlyFromTheFloor::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	FCataclysmFloorPlan Room = OpenRoom(9, 9);
	TBitArray<> Seen;
	TestEqual(TEXT("from off the grid, nothing"), Map::RevealAround(Room, Seen, FIntPoint(-5, 2), 3), 0);
	Room.Fill(FIntPoint(4, 4));
	TestEqual(TEXT("from a solid cell, nothing"), Map::RevealAround(Room, Seen, FIntPoint(4, 4), 3), 0);
	TestEqual(TEXT("so nothing is seen"), CountSeen(Room, Seen), 0);

	const int32 First = Map::RevealAround(Room, Seen, FIntPoint(2, 2), 3);
	TestTrue(TEXT("from a walkable cell, something"), First > 0);
	TestEqual(TEXT("**the same place again sees nothing new**"), Map::RevealAround(Room, Seen, FIntPoint(2, 2), 3), 0);
	TestTrue(TEXT("a step away sees some more"), Map::RevealAround(Room, Seen, FIntPoint(2, 5), 3) > 0);
	TestFalse(TEXT("and a cell off the grid is never seen"), Map::IsSeen(Room, Seen, FIntPoint(9, 9)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapRunsCoverTheSeenFloor,
	"Cataclysm.FloorMap.TheRunsCoverExactlyTheSeenWalkableCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapRunsCoverTheSeenFloor::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	// A ROOM WITH A PILLAR, seen from one side: the runs must skip the pillar and everything unseen.
	FCataclysmFloorPlan Room = OpenRoom(9, 9);
	Room.Fill(FIntPoint(4, 4));
	TBitArray<> Seen;
	Map::RevealAround(Room, Seen, FIntPoint(4, 2), 3);

	int32 Covered = 0;
	for (const FCataclysmFloorMapRun& Run : Map::RunsOf(Room, Seen))
	{
		TestTrue(TEXT("a run runs forwards"), Run.FirstY <= Run.LastY);
		for (int32 Y = Run.FirstY; Y <= Run.LastY; ++Y)
		{
			const FIntPoint Cell(Run.X, Y);
			if (!Room.IsFloor(Cell) || !Map::IsSeen(Room, Seen, Cell))
			{
				AddError(FString::Printf(TEXT("a run covers (%d, %d), which is unseen or not walkable"), Cell.X, Y));
			}
			++Covered;
		}
	}
	int32 SeenFloor = 0;
	for (int32 Y = 0; Y < Room.Height; ++Y)
	{
		for (int32 X = 0; X < Room.Width; ++X)
		{
			SeenFloor += (Room.IsFloor(FIntPoint(X, Y)) && Map::IsSeen(Room, Seen, FIntPoint(X, Y))) ? 1 : 0;
		}
	}
	TestTrue(TEXT("something was seen"), SeenFloor > 0);
	TestEqual(TEXT("**the runs cover every seen walkable cell once**"), Covered, SeenFloor);
	TestEqual(TEXT("and nothing seen gives no runs"), Map::RunsOf(Room, TBitArray<>()).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapANewFloorStartsUnseen,
	"Cataclysm.FloorMap.ANewFloorStartsWithNothingSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapANewFloorStartsUnseen::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmDungeonFloor* Floor = World->SpawnActor<ACataclysmDungeonFloor>();
	if (!TestNotNull(TEXT("a floor"), Floor) || !TestTrue(TEXT("built"), Floor->Build(OpenRoom(9, 9))))
	{
		return false;
	}
	const int32 FirstBuild = Floor->BuildGeneration();
	TestEqual(TEXT("a built floor has nothing seen"), CountSeen(Floor->GetPlan(), Floor->SeenCells()), 0);

	const int32 ChangesBefore = Floor->SeenChangeCount();
	TestTrue(TEXT("standing on it sees something"), Floor->RevealAround(FIntPoint(4, 4)) > 0);
	TestTrue(TEXT("and that counts as a change"), Floor->SeenChangeCount() > ChangesBefore);
	const int32 ChangesAfter = Floor->SeenChangeCount();
	Floor->RevealAround(FIntPoint(4, 4));
	TestEqual(TEXT("standing there again changes nothing"), Floor->SeenChangeCount(), ChangesAfter);

	// THE NEXT FLOOR: the same actor rebuilt, as going down the stairs does.
	TestTrue(TEXT("built again"), Floor->Build(OpenRoom(12, 10)));
	TestTrue(TEXT("a new build is a new generation"), Floor->BuildGeneration() > FirstBuild);
	TestEqual(TEXT("**the new floor has nothing seen**"), CountSeen(Floor->GetPlan(), Floor->SeenCells()), 0);
	TestEqual(TEXT("and its record is its own size"), Floor->SeenCells().Num(), 12 * 10);
	return true;
}

// --------------------------------------------------------------------------
// Where it is drawn
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapLayoutIsTheJudgedSize,
	"Cataclysm.FloorMap.TheMapIsSeventyPercentOfTheShorterSideAtTwentyFourPixelsACell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapLayoutIsTheJudgedSize::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	const FCataclysmFloorMapLayout Hd = Map::LayoutFor(FVector2D(1920.0, 1080.0));
	ExpectPoint(*this, TEXT("centred"), Hd.Centre, FVector2D(960.0, 540.0));
	TestEqual(TEXT("a 756 pixel square at 1080"), Hd.HalfSide * 2.0f, 756.0f, 0.01f);
	TestEqual(TEXT("24 pixels a cell at 1080"), Hd.PxPerCell, 24.0f, 0.001f);
	TestTrue(TEXT("**more detail than fitting a 48 cell floor, about 16 a cell**"),
		Hd.PxPerCell > Hd.HalfSide * 2.0f / 48.0f);

	const FCataclysmFloorMapLayout Uhd = Map::LayoutFor(FVector2D(3840.0, 2160.0));
	TestEqual(TEXT("it scales with the screen's height"), Uhd.PxPerCell, 48.0f, 0.001f);
	const FCataclysmFloorMapLayout Tall = Map::LayoutFor(FVector2D(1080.0, 1920.0));
	TestEqual(TEXT("and the square is of the shorter side"), Tall.HalfSide * 2.0f, 756.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapPlayerIsAtTheCentre,
	"Cataclysm.FloorMap.ThePlayerIsAtTheCentreWithPlusXUpAndPlusYRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapPlayerIsAtTheCentre::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	const FCataclysmFloorMapLayout Layout = At1080();
	const FVector2D Player(10.4, 20.7);
	ExpectPoint(*this, TEXT("**the player's own position is the centre**"), Map::ScreenOf(Player, Player, Layout),
		Layout.Centre);
	const FVector2D Ahead = Map::ScreenOf(Player + FVector2D(1.0, 0.0), Player, Layout);
	ExpectPoint(*this, TEXT("one cell along world +X is one cell up"), Ahead, Layout.Centre - FVector2D(0.0, 24.0));
	const FVector2D Right = Map::ScreenOf(Player + FVector2D(0.0, 1.0), Player, Layout);
	ExpectPoint(*this, TEXT("one cell along world +Y is one cell right"), Right, Layout.Centre + FVector2D(24.0, 0.0));

	// THE PLAYER'S MARKER POINTS THE WAY THEY FACE: facing world +X, the tip is above the centre.
	FVector2D Corners[3];
	Map::FacingTriangle(Layout.Centre, 0.0f, 12.0f, Corners);
	TestTrue(TEXT("facing +X, the tip is above"), Corners[0].Y < Layout.Centre.Y);
	TestEqual(TEXT("and straight above"), Corners[0].X, Layout.Centre.X, 0.001);
	Map::FacingTriangle(Layout.Centre, 90.0f, 12.0f, Corners);
	TestTrue(TEXT("facing +Y, the tip is to the right"), Corners[0].X > Layout.Centre.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapClipsToTheSquare,
	"Cataclysm.FloorMap.ACellBeyondTheMapsEdgeIsClippedAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapClipsToTheSquare::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	const FCataclysmFloorMapLayout Layout = At1080();
	const FVector2D Player(10.0, 10.0);

	// THE CONTROL: the player's own cell is inside, and is not changed.
	FBox2D Own = Map::BoxOf(FCataclysmFloorMapRun{10, 10, 10}, Player, Layout);
	const FBox2D OwnBefore = Own;
	TestTrue(TEXT("the player's cell is drawn"), Map::ClipToArea(Own, Layout));
	ExpectPoint(*this, TEXT("as it was"), Own.Min, OwnBefore.Min);
	TestEqual(TEXT("its whole width, one cell"), Own.Max.X - Own.Min.X, 24.0, 0.001);

	// 20 CELLS AHEAD IS 480 PIXELS UP, past the 378 pixel half side: nothing to draw.
	FBox2D Far = Map::BoxOf(FCataclysmFloorMapRun{30, 10, 10}, Player, Layout);
	TestFalse(TEXT("**a cell beyond the edge is clipped away**"), Map::ClipToArea(Far, Layout));

	// A RUN ACROSS THE EDGE is cut at it.
	FBox2D Across = Map::BoxOf(FCataclysmFloorMapRun{10, 0, 40}, Player, Layout);
	TestTrue(TEXT("a run across the edge is drawn"), Map::ClipToArea(Across, Layout));
	TestEqual(TEXT("up to the right edge"), Across.Max.X, Layout.Area().Max.X, 0.001);
	TestEqual(TEXT("and from the left edge"), Across.Min.X, Layout.Area().Min.X, 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapExitMarkerOnTheEdge,
	"Cataclysm.FloorMap.TheStairsBeyondTheEdgeAreMarkedOnTheEdgeInTheirDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapExitMarkerOnTheEdge::RunTest(const FString&)
{
	using namespace CataclysmFloorMapTest;

	const FCataclysmFloorMapLayout Layout = At1080();
	const FBox2D Area = Layout.Area();
	const FVector2D Player(20.0, 20.0);
	bool bOnTheEdge = true;

	// THE CONTROL: stairs two cells ahead are drawn where they are.
	const FVector2D Near = Map::MarkerAt(Player + FVector2D(2.0, 0.0), Player, Layout, bOnTheEdge);
	TestFalse(TEXT("stairs inside the map are not moved"), bOnTheEdge);
	ExpectPoint(*this, TEXT("they are where they are"), Near, Map::ScreenOf(Player + FVector2D(2.0, 0.0), Player, Layout));

	struct FCase
	{
		const TCHAR* What;
		FVector2D Offset;
		FVector2D Expected;
	};
	const FCase Cases[] = {
		{TEXT("far ahead (+X): the top edge, straight above"), FVector2D(40.0, 0.0),
			FVector2D(Layout.Centre.X, Area.Min.Y)},
		{TEXT("far behind (-X): the bottom edge"), FVector2D(-40.0, 0.0), FVector2D(Layout.Centre.X, Area.Max.Y)},
		{TEXT("far to +Y: the right edge"), FVector2D(0.0, 40.0), FVector2D(Area.Max.X, Layout.Centre.Y)},
		{TEXT("far to -Y: the left edge"), FVector2D(0.0, -40.0), FVector2D(Area.Min.X, Layout.Centre.Y)},
		{TEXT("far along both: the top right corner"), FVector2D(40.0, 40.0), FVector2D(Area.Max.X, Area.Min.Y)},
	};
	for (const FCase& Case : Cases)
	{
		const FVector2D At = Map::MarkerAt(Player + Case.Offset, Player, Layout, bOnTheEdge);
		TestTrue(FString::Printf(TEXT("%s, and it is pulled in"), Case.What), bOnTheEdge);
		ExpectPoint(*this, FString::Printf(TEXT("**%s**"), Case.What), At, Case.Expected, 0.01);
	}

	// IN ITS DIRECTION, not only on the edge: a point 40 ahead and 10 right meets the top edge a quarter of the way.
	const FVector2D Slanted = Map::MarkerAt(Player + FVector2D(40.0, 10.0), Player, Layout, bOnTheEdge);
	TestEqual(TEXT("a slanted direction meets the top edge"), Slanted.Y, Area.Min.Y, 0.01);
	TestEqual(TEXT("a quarter of the half side to the right"), Slanted.X, Layout.Centre.X + Layout.HalfSide * 0.25,
		0.01);
	return true;
}

// --------------------------------------------------------------------------
// When it is drawn, and what it costs
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapWhenDrawn,
	"Cataclysm.FloorMap.ItIsDrawnWhenShownOnAFloorAndNotUnderAFullScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapWhenDrawn::RunTest(const FString&)
{
	using Map = UCataclysmFloorMap;
	TestTrue(TEXT("shown, on a floor, nothing open: drawn"), Map::ShouldDraw(true, true, false));
	TestFalse(TEXT("not shown: not drawn"), Map::ShouldDraw(false, true, false));
	TestFalse(TEXT("no floor: not drawn"), Map::ShouldDraw(true, false, false));
	TestFalse(TEXT("**a full screen open: not drawn**"), Map::ShouldDraw(true, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapRunCountOnGeneratedFloors,
	"Cataclysm.FloorMap.TheRunsOnAWhollySeenFloorAreMeasuredForEveryLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapRunCountOnGeneratedFloors::RunTest(const FString&)
{
	using Map = UCataclysmFloorMap;

	// THE COST PER FRAME IS ONE RECTANGLE A RUN, so the most runs a floor can have is its worst case: every cell seen.
	// MEASURED, NOT ESTIMATED: twenty seeds of each layout. The bound asserted is the arithmetic one -- a run every
	// other cell, half a 48 by 48 floor -- and the measured figure is what the window reports.
	int32 Most = 0;
	for (uint8 Index = 0; Index < static_cast<uint8>(ECataclysmFloorLayout::Count); ++Index)
	{
		const ECataclysmFloorLayout Layout = static_cast<ECataclysmFloorLayout>(Index);
		int32 MostForLayout = 0;
		for (int32 Seed = 1; Seed <= 20; ++Seed)
		{
			FCataclysmFloorRequest Request;
			Request.DungeonSeed = Seed;
			Request.FloorNumber = 1;
			Request.Layout = Layout;
			const FCataclysmFloorPlan Plan = FCataclysmFloorGenerator::Generate(Request);
			TBitArray<> All(true, Plan.Width * Plan.Height);
			MostForLayout = FMath::Max(MostForLayout, Map::RunsOf(Plan, All).Num());
		}
		AddInfo(FString::Printf(TEXT("layout %d: at most %d runs on a wholly seen floor, over 20 seeds"),
			static_cast<int32>(Index), MostForLayout));
		Most = FMath::Max(Most, MostForLayout);
	}
	TestTrue(TEXT("a run was measured"), Most > 0);
	TestTrue(FString::Printf(TEXT("**at most %d rectangles a frame, under half a 48 by 48 floor**"), Most),
		Most <= 48 * 48 / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFloorMapSlateAndTab,
	"Cataclysm.FloorMap.WhatSlateDoesWithTabIsRecorded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMapSlateAndTab::RunTest(const FString&)
{
	// WHAT THIS CAN AND CANNOT SAY. Slate's navigation turns Tab into "next" focus when a widget has keyboard focus,
	// and a focused widget may take the key before Enhanced Input sees it. This records what the navigation
	// configuration does with Tab. Whether a press reaches the map's action while a screen has focus is not something
	// a headless test can drive; it is checked in play. The map is not drawn under a full screen anyway
	// (`ShouldDraw`), so the case is a press the map would ignore.
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialised in this run, so what it does with Tab could not be read."));
		return true;
	}
	const TSharedRef<FNavigationConfig> Config = FSlateApplication::Get().GetNavigationConfig();
	const FKeyEvent Tab(EKeys::Tab, FModifierKeysState(), 0, false, 0, 0);
	const EUINavigation Direction = Config->GetNavigationDirectionFromKey(Tab);
	AddInfo(FString::Printf(TEXT("Slate reads Tab as navigation %d (Next is %d, Invalid is %d)."),
		static_cast<int32>(Direction), static_cast<int32>(EUINavigation::Next), static_cast<int32>(EUINavigation::Invalid)));
	TestTrue(TEXT("Slate's answer is one of the two expected"),
		Direction == EUINavigation::Next || Direction == EUINavigation::Invalid);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
