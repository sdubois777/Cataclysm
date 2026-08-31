// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmEmpireMapWidget.h"
#include "Player/CataclysmGameInstance.h"

/**
 * Tests for the empire overview, issue #1087.
 *
 * WHAT THEY CANNOT CHECK, AND IT IS THE THING THAT MATTERS MOST. The automation
 * test command in `tools/unreal_build.py` passes `-nullrhi`, so nothing is drawn
 * and no test here can say whether the screen is legible, whether the diamond
 * reads as a map, or whether a city's box is under the cursor. **Somebody has to
 * look.** These cover the arithmetic underneath: where a city lands, how big it
 * is drawn, what its box says, and which run the screen is showing.
 *
 * WHICH IS WHY MOST OF THEM ARE ABOUT `UCataclysmEmpireMapLayout` RATHER THAN
 * THE WIDGET. A widget in a headless test has no Widget Blueprint, so every one
 * of its `BindWidget` properties is null and it draws nothing at all --
 * `UCataclysmPassiveTreeWidget` is in exactly the same position. Everything that
 * can be decided without a screen is in the layout class so that it can be
 * covered.
 */

namespace CataclysmEmpireScreenTest
{
	UCataclysmEmpireMap* MakeMap()
	{
		UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
		Map->Build();
		return Map;
	}

	/** A panel exactly big enough for the diamond at a scale of one. */
	FVector2D SnugPanel()
	{
		return UCataclysmEmpireMapLayout::DiamondSize()
			+ FVector2D(2.0 * UCataclysmEmpireMapLayout::FitMarginPx,
						2.0 * UCataclysmEmpireMapLayout::FitMarginPx);
	}
}

// ---------------------------------------------------------------------------
// The shape of the drawing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireLayoutShapeTest,
	"Cataclysm.EmpireScreen.TheDiamondIsSevenCitiesAcross",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireLayoutShapeTest::RunTest(const FString& Parameters)
{
	// SEVEN, AND DERIVED FROM THE MAP'S OWN RADIUS. The lattice runs from -3 to
	// 3 on each axis.
	TestEqual(TEXT("the lattice is seven cities across"),
			  UCataclysmEmpireMapLayout::CellsAcross(), 7);
	TestEqual(TEXT("which is what the map's radius gives"),
			  UCataclysmEmpireMapLayout::CellsAcross(),
			  2 * UCataclysmEmpireMap::Radius + 1);

	// HAND-WORKED. Six cells between the outermost centres, plus half a city
	// hanging off each end: 6 x 140 + 128 across, and 6 x 92 + 56 down.
	const FVector2D Diamond = UCataclysmEmpireMapLayout::DiamondSize();

	TestEqual(TEXT("the diamond is 968 pixels across"), Diamond.X, 968.0, 0.01);
	TestEqual(TEXT("and 608 down"), Diamond.Y, 608.0, 0.01);

	// WIDER THAN IT IS TALL, because a city is. A city's box holds its tier and
	// how much defence is left, which is a wide shape.
	TestTrue(TEXT("the diamond is wider than it is tall"), Diamond.X > Diamond.Y);
	TestTrue(TEXT("and a city is drawn wider than it is tall"),
			 UCataclysmEmpireMapLayout::CityWidthPx
			 > UCataclysmEmpireMapLayout::CityHeightPx);

	// A CITY IS SMALLER THAN ITS CELL, so two neighbours have clear space
	// between them. A diamond of touching boxes reads as a grid.
	TestTrue(TEXT("a city is narrower than the space between two of them"),
			 UCataclysmEmpireMapLayout::CityWidthPx
			 < UCataclysmEmpireMapLayout::CellWidthPx);
	TestTrue(TEXT("and shorter than the space between two rows"),
			 UCataclysmEmpireMapLayout::CityHeightPx
			 < UCataclysmEmpireMapLayout::CellHeightPx);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireLayoutFitTest,
	"Cataclysm.EmpireScreen.TheWholeDiamondFitsWhateverPanelItIsGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireLayoutFitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireScreenTest;

	// A PANEL EXACTLY BIG ENOUGH IS A SCALE OF ONE.
	TestEqual(TEXT("a panel exactly big enough draws at full size"),
			  UCataclysmEmpireMapLayout::ScaleToFit(SnugPanel()), 1.0f, 0.001f);

	// HALF THE ROOM IS HALF THE SIZE. Hand-worked: the diamond is 968 by 608 and
	// the margins take 48 off each axis, so a panel of 532 by 352 leaves 484 by
	// 304, which is exactly half of each.
	TestEqual(TEXT("half the room draws at half the size"),
			  UCataclysmEmpireMapLayout::ScaleToFit(FVector2D(532.0, 352.0)),
			  0.5f, 0.001f);

	// A HUGE PANEL DOES NOT DRAW A HUGE DIAMOND. The sizes are what a city is
	// meant to be read at; drawing one larger gains nothing because the words
	// inside it are already fully visible.
	TestEqual(TEXT("a panel with room to spare still draws at full size"),
			  UCataclysmEmpireMapLayout::ScaleToFit(FVector2D(4000.0, 3000.0)),
			  UCataclysmEmpireMapLayout::LargestScale, 0.001f);

	// AND THE SMALLER OF THE TWO AXES WINS, so the diamond fits both ways.
	// Wide and short: the height is what binds.
	const FVector2D WideAndShort(4000.0, 352.0);
	TestEqual(TEXT("a wide, short panel is bound by its height"),
			  UCataclysmEmpireMapLayout::ScaleToFit(WideAndShort), 0.5f, 0.001f);

	const FVector2D TallAndNarrow(532.0, 3000.0);
	TestEqual(TEXT("a tall, narrow panel is bound by its width"),
			  UCataclysmEmpireMapLayout::ScaleToFit(TallAndNarrow), 0.5f, 0.001f);

	// A PANEL SMALLER THAN ITS OWN MARGINS ANSWERS THE FLOOR, not zero and not a
	// negative. It happens while a screen is being laid out and before Slate has
	// given it a size.
	TestEqual(TEXT("a panel of no size answers the smallest scale"),
			  UCataclysmEmpireMapLayout::ScaleToFit(FVector2D::ZeroVector),
			  UCataclysmEmpireMapLayout::SmallestScale, 0.001f);
	TestEqual(TEXT("and so does a nonsensical negative one"),
			  UCataclysmEmpireMapLayout::ScaleToFit(FVector2D(-100.0, -100.0)),
			  UCataclysmEmpireMapLayout::SmallestScale, 0.001f);

	TestTrue(TEXT("the smallest scale is above zero, so 25 cities can never "
				  "land on one point"),
			 UCataclysmEmpireMapLayout::SmallestScale > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireLayoutPositionTest,
	"Cataclysm.EmpireScreen.ThePillarSitsInTheMiddleAndTheRimAtThePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireLayoutPositionTest::RunTest(const FString& Parameters)
{
	const FVector2D Panel(1200.0, 800.0);
	const FVector2D Centre = Panel * 0.5;

	// THE PILLAR IS AT (0,0) AND LANDS IN THE MIDDLE OF THE PANEL, whatever the
	// scale. It is the thing the whole map is about.
	for (const float Scale : { 0.25f, 0.5f, 1.0f })
	{
		TestEqual(FString::Printf(
			TEXT("at a scale of %.2f the Pillar is in the middle"), Scale),
			UCataclysmEmpireMapLayout::PositionFor(0, 0, Scale, Panel), Centre);
	}

	// THE ROW GROWS DOWNWARDS AND THE COLUMN RIGHTWARDS, which is what turns the
	// taxicab ball into a diamond rather than a square.
	const FVector2D Top = UCataclysmEmpireMapLayout::PositionFor(-3, 0, 1.0f, Panel);
	const FVector2D Bottom = UCataclysmEmpireMapLayout::PositionFor(3, 0, 1.0f, Panel);
	const FVector2D Left = UCataclysmEmpireMapLayout::PositionFor(0, -3, 1.0f, Panel);
	const FVector2D Right = UCataclysmEmpireMapLayout::PositionFor(0, 3, 1.0f, Panel);

	TestTrue(TEXT("the Outpost at row -3 is above the Pillar"), Top.Y < Centre.Y);
	TestTrue(TEXT("and directly above it"),
			 FMath::IsNearlyEqual(Top.X, Centre.X, 0.01));
	TestTrue(TEXT("the Outpost at row 3 is below"), Bottom.Y > Centre.Y);
	TestTrue(TEXT("the Outpost at column -3 is to the left"), Left.X < Centre.X);
	TestTrue(TEXT("and level with the Pillar"),
			 FMath::IsNearlyEqual(Left.Y, Centre.Y, 0.01));
	TestTrue(TEXT("and the one at column 3 to the right"), Right.X > Centre.X);

	// THE FOUR POINTS ARE THE SAME DISTANCE OUT AS EACH OTHER, which is what
	// makes it a diamond rather than a kite.
	TestEqual(TEXT("the top and bottom points are equally far out"),
			  Centre.Y - Top.Y, Bottom.Y - Centre.Y, 0.01);
	TestEqual(TEXT("and so are the left and right"),
			  Centre.X - Left.X, Right.X - Centre.X, 0.01);

	// HAND-WORKED: three cells at 140 across and 92 down.
	TestEqual(TEXT("the rightmost Outpost is three cells out"),
			  Right.X - Centre.X,
			  3.0 * UCataclysmEmpireMapLayout::CellWidthPx, 0.01);
	TestEqual(TEXT("and the topmost three rows up"),
			  Centre.Y - Top.Y,
			  3.0 * UCataclysmEmpireMapLayout::CellHeightPx, 0.01);

	// HALVING THE SCALE HALVES THE DISTANCE FROM THE MIDDLE, so zooming out
	// really does show a smaller map rather than the same one cropped.
	const FVector2D HalfRight =
		UCataclysmEmpireMapLayout::PositionFor(0, 3, 0.5f, Panel);
	TestEqual(TEXT("at half the scale a city is half as far from the middle"),
			  HalfRight.X - Centre.X, (Right.X - Centre.X) * 0.5, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireLayoutNoOverlapTest,
	"Cataclysm.EmpireScreen.EveryCityFitsThePanelAndNoTwoOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireLayoutNoOverlapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireScreenTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// THREE PANELS, INCLUDING ONE THAT IS THE WRONG SHAPE. A layout that only
	// worked on the panel it was designed against would come apart the first
	// time somebody resized the window.
	const FVector2D Panels[] = {
		SnugPanel(),
		FVector2D(1600.0, 900.0),
		FVector2D(700.0, 1200.0),
	};

	for (const FVector2D& Panel : Panels)
	{
		const float Scale = UCataclysmEmpireMapLayout::ScaleToFit(Panel);
		const FVector2D Box = UCataclysmEmpireMapLayout::CitySize(Scale);

		TArray<FVector2D> Placed;

		for (const FCataclysmCity& City : Map->Cities)
		{
			const FVector2D At = UCataclysmEmpireMapLayout::PositionFor(
				City.R, City.C, Scale, Panel);

			// A CITY IS PLACED BY ITS CENTRE, so half its box hangs off each
			// side of the point it was given.
			const FVector2D Least = At - Box * 0.5;
			const FVector2D Most = At + Box * 0.5;

			TestTrue(FString::Printf(
				TEXT("on a %.0f by %.0f panel, %s is inside the left and top "
					 "edges"), Panel.X, Panel.Y, *City.Name),
				Least.X >= -0.01 && Least.Y >= -0.01);
			TestTrue(FString::Printf(
				TEXT("on a %.0f by %.0f panel, %s is inside the right and bottom "
					 "edges"), Panel.X, Panel.Y, *City.Name),
				Most.X <= Panel.X + 0.01 && Most.Y <= Panel.Y + 0.01);

			Placed.Add(At);
		}

		if (!TestEqual(TEXT("all twenty-five were placed"), Placed.Num(), 25))
		{
			return false;
		}

		// NO TWO BOXES TOUCH. Two rectangles miss each other when they are clear
		// on either axis. A map where two cities overlapped would be unclickable
		// and unreadable in exactly the places that matter most.
		for (int32 A = 0; A < Placed.Num(); ++A)
		{
			for (int32 B = A + 1; B < Placed.Num(); ++B)
			{
				const FVector2D Gap = (Placed[A] - Placed[B]).GetAbs();

				TestTrue(FString::Printf(
					TEXT("on a %.0f by %.0f panel, %s and %s do not overlap"),
					Panel.X, Panel.Y, *Map->Cities[A].Name, *Map->Cities[B].Name),
					Gap.X >= Box.X - 0.01 || Gap.Y >= Box.Y - 0.01);
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// What a city says
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireLayoutMarkTest,
	"Cataclysm.EmpireScreen.ACityReadsAsSealedExposedOrFallen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireLayoutMarkTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireScreenTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// THREE STATES AND NOT TWO. "Cannot be attacked yet" is the empire's whole
	// structure, and a player who cannot see which cities are exposed cannot
	// make the only decision the strategy layer asks of them.
	//
	// AT THE START, THE TWELVE RIM OUTPOSTS ARE EXPOSED AND NOTHING ELSE IS.
	int32 Exposed = 0;
	int32 Sealed = 0;

	for (const FCataclysmCity& City : Map->Cities)
	{
		const ECataclysmCityMark Mark =
			UCataclysmEmpireMapLayout::MarkFor(Map, City.CityId);

		TestFalse(FString::Printf(TEXT("%s has not fallen"), *City.Name),
				  Mark == ECataclysmCityMark::Fallen);

		if (Mark == ECataclysmCityMark::Exposed)
		{
			++Exposed;
			TestEqual(FString::Printf(TEXT("%s is on the rim"), *City.Name),
					  City.Ring(), UCataclysmEmpireMap::Radius);
		}
		else
		{
			++Sealed;
		}
	}

	TestEqual(TEXT("twelve cities read as exposed"), Exposed, 12);
	TestEqual(TEXT("and thirteen as sealed"), Sealed, 13);

	// A CITY THAT FALLS READS AS FALLEN, and the one behind it becomes exposed.
	Map->Fall(0);

	TestTrue(TEXT("the fallen Outpost reads as fallen"),
			 UCataclysmEmpireMapLayout::MarkFor(Map, 0)
			 == ECataclysmCityMark::Fallen);
	TestTrue(TEXT("the Bulwark behind it now reads as exposed"),
			 UCataclysmEmpireMapLayout::MarkFor(Map, 2)
			 == ECataclysmCityMark::Exposed);
	TestTrue(TEXT("and a Bulwark elsewhere still reads as sealed"),
			 UCataclysmEmpireMapLayout::MarkFor(Map, 5)
			 == ECataclysmCityMark::Sealed);

	// THE SCREEN AGREES WITH THE GAME. `IsExposed` is what decides where a surge
	// can land, so a screen that decided differently would be showing the player
	// a rule the game does not obey.
	for (const FCataclysmCity& City : Map->Cities)
	{
		const bool bScreenSaysExposed =
			UCataclysmEmpireMapLayout::MarkFor(Map, City.CityId)
			== ECataclysmCityMark::Exposed;

		TestEqual(FString::Printf(
			TEXT("%s reads as exposed exactly when the map says it is"),
			*City.Name),
			bScreenSaysExposed, Map->IsExposed(City.CityId));
	}

	// A MAP THAT IS NOT THERE ANSWERS SEALED rather than reading through null.
	TestTrue(TEXT("no map reads as sealed"),
			 UCataclysmEmpireMapLayout::MarkFor(nullptr, 0)
			 == ECataclysmCityMark::Sealed);
	TestTrue(TEXT("and so does a city that does not exist"),
			 UCataclysmEmpireMapLayout::MarkFor(Map, 999)
			 == ECataclysmCityMark::Sealed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireLayoutLabelTest,
	"Cataclysm.EmpireScreen.ACitysBoxSaysItsTierAndWhatIsLeftOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireLayoutLabelTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireScreenTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// AN INTACT CITY IS AT ITS FULL SHARE.
	TestEqual(TEXT("an untouched Outpost"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 0),
			  FString(TEXT("Outpost 100%")));
	TestEqual(TEXT("an untouched Bulwark"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 2),
			  FString(TEXT("Bulwark 100%")));
	TestEqual(TEXT("an untouched Sanctuary"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 6),
			  FString(TEXT("Sanctuary 100%")));
	TestEqual(TEXT("and the Pillar"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, Map->PillarId),
			  FString(TEXT("Pillar 100%")));

	// THE SHARE AND NOT THE NUMBER. A Sanctuary has 8,000 defence and an Outpost
	// 1,000, so the raw figures cannot be compared by eye and what a player has
	// to decide is which city is closest to falling. A quarter off each reads
	// the same on both.
	Map->Bite(0, 0.25f, 0.0f);
	Map->Bite(6, 0.25f, 0.0f);

	TestEqual(TEXT("an Outpost a quarter down"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 0),
			  FString(TEXT("Outpost 75%")));
	TestEqual(TEXT("and a Sanctuary a quarter down read the same"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 6),
			  FString(TEXT("Sanctuary 75%")));

	// A CITY WITH ALMOST NOTHING LEFT NEVER READS AS NOTHING. A player told a
	// city is at 0% would stop defending it, and a city at 0.4% is still
	// standing and still savable.
	Map->Bite(1, 0.996f, 0.0f);

	TestEqual(TEXT("a city with a fraction of a percent left reads as 1%"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 1),
			  FString(TEXT("Outpost 1%")));
	TestFalse(TEXT("and it has not fallen"), Map->Find(1)->bFallen);

	// AND A FALLEN CITY SAYS SO RATHER THAN SAYING 0%.
	Map->Fall(3);
	TestEqual(TEXT("a fallen Outpost"),
			  UCataclysmEmpireMapLayout::CityLabel(Map, 3),
			  FString(TEXT("Outpost lost")));

	TestEqual(TEXT("no map gives no label"),
			  UCataclysmEmpireMapLayout::CityLabel(nullptr, 0), FString());

	return true;
}

// ---------------------------------------------------------------------------
// Where a run lives
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireGameInstanceTest,
	"Cataclysm.EmpireScreen.TheGameInstanceHoldsOneRunAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireGameInstanceTest::RunTest(const FString& Parameters)
{
	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();

	// NULL RATHER THAN AN EMPTY RUN, so "no run has been started" and "a run in
	// which nothing has happened yet" are different states. They are: the second
	// has a surge due.
	TestNull(TEXT("a fresh game instance holds no run"), Instance->EmpireRun.Get());

	UCataclysmEmpireRun* First = Instance->BeginEmpireRun(7);

	if (!TestNotNull(TEXT("a run can be started"), First))
	{
		return false;
	}

	TestTrue(TEXT("and it is the one the instance holds"),
			 Instance->EmpireRun.Get() == First);
	TestEqual(TEXT("on day 0"), First->Day(), 0);
	TestEqual(TEXT("with an intact empire"), First->Map->FallenCityCount(), 0);

	First->AdvanceDays(300);
	const FString AfterThreeHundred = First->Map->Render();

	// THE SAME SEED GIVES THE SAME EMPIRE, which is what makes a bug worth
	// reporting: a person can say which seed it happened on.
	UCataclysmEmpireRun* Same = Instance->BeginEmpireRun(7);
	Same->AdvanceDays(300);

	TestEqual(TEXT("the same seed gives the same empire three hundred days in"),
			  Same->Map->Render(), AfterThreeHundred);

	// AND STARTING ONE THROWS THE LAST AWAY. A second run begins on day 0
	// rather than continuing where the first had reached.
	UCataclysmEmpireRun* Other = Instance->BeginEmpireRun(8);
	TestEqual(TEXT("a run started after another begins on day 0"),
			  Other->Day(), 0);
	TestEqual(TEXT("with an intact empire"),
			  Other->Map->FallenCityCount(), 0);

	Other->AdvanceDays(300);
	TestNotEqual(TEXT("and a different seed gives a different empire"),
				 Other->Map->Render(), AfterThreeHundred);

	// THE LETHALITY MODE AND THE ESCALATION REACH THE SCHEDULE.
	UCataclysmEmpireRun* Heretic = Instance->BeginEmpireRun(
		1, ECataclysmSurgeMode::Accelerating, /*Heretic*/ 2);

	TestTrue(TEXT("an accelerating run escalates"),
			 Heretic->Surges->Mode == ECataclysmSurgeMode::Accelerating);
	TestEqual(TEXT("and a Heretic run brings five dungeons rather than four"),
			  Heretic->Surges->DungeonsInNextSurge(), 5);

	// `GetOrBeginEmpireRun` KEEPS WHAT IS THERE. It is what a console command
	// asking to see the empire calls, and it must not restart a campaign.
	TestTrue(TEXT("asking for the run again gives the same one"),
			 Instance->GetOrBeginEmpireRun() == Heretic);

	// AND STARTS ONE WHEN THERE IS NONE.
	UCataclysmGameInstance* Empty = NewObject<UCataclysmGameInstance>();
	TestNotNull(TEXT("asking an empty instance starts a run"),
				Empty->GetOrBeginEmpireRun());
	TestNotNull(TEXT("which it then holds"), Empty->EmpireRun.Get());

	// NOTHING TO ASK ANSWERS NOTHING, rather than reading through null.
	TestNull(TEXT("no world context gives no run"),
			 UCataclysmGameInstance::EmpireRunFor(nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireWidgetRunTest,
	"Cataclysm.EmpireScreen.TheScreenShowsTheRunItIsGivenAndSurvivesHavingNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireWidgetRunTest::RunTest(const FString& Parameters)
{
	// THE WIDGET WITH NO BLUEPRINT AT ALL, which is every widget in a headless
	// test: every `BindWidget` property is null and it draws nothing. What can
	// be checked is which run it decided to show and that it does not fall over
	// without one. `UCataclysmPassiveTreeWidget` is in the same position.
	UCataclysmEmpireMapWidget* Screen = NewObject<UCataclysmEmpireMapWidget>();
	if (!TestNotNull(TEXT("the screen was created"), Screen))
	{
		return false;
	}

	TestNull(TEXT("with no run to show"), Screen->ShownRun());

	// REFRESHING WITH NOTHING TO SHOW MUST NOT FALL OVER. It is the state the
	// screen is in the moment it is created, before anything has been handed to
	// it.
	Screen->Refresh();
	TestEqual(TEXT("and no city is drawn"), Screen->CityCount(), 0);

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(3);

	Screen->SetRunForTests(Run);
	TestTrue(TEXT("the run it is given is the run it shows"),
			 Screen->ShownRun() == Run);

	Screen->SetPanelSizeForTests(FVector2D(1600.0, 900.0));
	Screen->Refresh();

	// STILL NO CITIES, AND THAT IS THE HEADLESS CASE RATHER THAN A FAULT. The
	// boxes go on `MapCanvas`, which is a `BindWidget` and is null without a
	// Widget Blueprint. Where they WOULD go is `UCataclysmEmpireMapLayout`, and
	// that is covered above.
	TestEqual(TEXT("no boxes are made without a Widget Blueprint"),
			  Screen->CityCount(), 0);
	TestEqual(TEXT("and the day it drew for is the run's"),
			  Screen->ShownDay(), Run->Day());

	Run->AdvanceDays(150);
	Screen->Refresh();
	TestEqual(TEXT("after a hundred and fifty days it draws for day 150"),
			  Screen->ShownDay(), 150);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
