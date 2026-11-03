// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/TextBlock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmEmpireStatusBar.h"
#include "Tests/CataclysmTestSkip.h"

/**
 * The empire map shows the Cataclysm boss dungeon. Issue #1324 slice 6c, 2026-11-03.
 *
 * THE OWNER'S DECISION, 2026-11-02, verbatim: "On no city (Recommended)". Once a run has earned the boss dungeon it
 * sits on no city, and the empire map shows a Cataclysm boss dungeon marker the player enters from. Slice 6b opened
 * it (`Cataclysm.EarnedBoss.`); this slice shows it: the progress line counts the Cataclysms finished against how many
 * open it, a box of its own in the map's top left corner says how deep it is, the opening is the screen's latest
 * event while nothing later has happened, and the status bar counts it as a dungeon standing.
 *
 * `Cataclysm.BossOnTheMap.` IS A NEW GROUP and selects these and nothing else.
 *
 * HEADLESS, LIKE `Cataclysm.EmpireScreen.`: a headless widget's labels are null, so every word is read from
 * `UCataclysmEmpireMapLayout`, which the widget writes from. No world and no actor; the quest dungeons are built by
 * hand and cleared through `ClearDungeon`, the live path, as `Cataclysm.EarnedBoss.` does.
 */
namespace CataclysmBossOnTheMapTest
{
	UCataclysmEmpireRun* MakeTierRun(int32 Seed, int32 DifficultyTier)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, DifficultyTier);
		return Run;
	}

	/** One dungeon of this kind, built by hand on the map's first city and cleared through `ClearDungeon`. */
	void ClearOneByHand(UCataclysmEmpireRun& Run, ECataclysmDungeonType Kind, ECataclysmType Sender)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId++;
		Dungeon.Type = Kind;
		Dungeon.Cataclysm = Sender;
		Dungeon.CityId = Run.Map->Cities[0].CityId;

		Run.Dungeons.Add(Dungeon);
		Run.ClearDungeon(Dungeon.DungeonId);
	}

	/** Clears exactly as many quest dungeons as this Cataclysm asks for. */
	void FinishOne(UCataclysmEmpireRun& Run, ECataclysmType Cataclysm)
	{
		const int32 Needed = UCataclysmRoster::QuestObjectivesFor(Cataclysm);
		for (int32 Index = 0; Index < Needed; ++Index)
		{
			ClearOneByHand(Run, ECataclysmDungeonType::Quest, Cataclysm);
		}
	}

	/** A tier 1 run on day 4 that has defeated this many ordinary dungeons and then finished its one Cataclysm,
	 *  which opens the boss, and then this many more. */
	UCataclysmEmpireRun* RunWithItsBossOpen(int32 OrdinaryBefore, int32 OrdinaryAfter)
	{
		UCataclysmEmpireRun* Run = MakeTierRun(/* Seed */ 11, /* DifficultyTier */ 1);
		Run->AdvanceDays(4);
		for (int32 Index = 0; Index < OrdinaryBefore; ++Index)
		{
			ClearOneByHand(*Run, ECataclysmDungeonType::Basic, ECataclysmType::None);
		}
		FinishOne(*Run, Run->ActiveCataclysms[0]);
		for (int32 Index = 0; Index < OrdinaryAfter; ++Index)
		{
			ClearOneByHand(*Run, ECataclysmDungeonType::Basic, ECataclysmType::None);
		}
		return Run;
	}
}

// ---------------------------------------------------------------------------
// The progress line
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossOnTheMapProgressTest,
	"Cataclysm.BossOnTheMap.TheProgressLineCountsTheCataclysmsFinishedAndSaysWhenTheBossOpens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * TIER 5, AN ODD COUNT: five Cataclysms, three to finish -- the count that tells half rounded up from half rounded
 * down. The line counts each one finished, says the boss opens when three are, says it is open on the clear that
 * finishes the third, and says it has been cleared once it is. Nothing about the boss shows before it opens. Then
 * tier 1, the singular, and tier 3 with its two met by hand and no clear yet, which a version 3 run record can hold.
 * Prints "BOSS PROGRESS".
 */
bool FCataclysmBossOnTheMapProgressTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossOnTheMapTest;

	UCataclysmEmpireRun* Run = MakeTierRun(/* Seed */ 5, /* DifficultyTier */ 5);
	if (!TestEqual(TEXT("set-up: a tier 5 run faces five Cataclysms"), Run->ActiveCataclysms.Num(), 5)
		|| !TestEqual(TEXT("set-up: and needs three of them finished"), Run->CataclysmDungeonRequirement(), 3)
		|| !TestEqual(TEXT("set-up: a fresh run has cleared nothing"), Run->DungeonsCleared, 0)
		|| !TestEqual(TEXT("set-up: and earned nothing"), Run->QuestObjectives, 0))
	{
		return false;
	}

	TestEqual(TEXT("a fresh tier 5 run has finished none of five, and three open the boss"),
			  UCataclysmEmpireMapLayout::ProgressLine(Run),
			  FString(TEXT("No dungeons cleared yet, no quest objectives, 0 of 5 Cataclysms finished; the boss "
						   "dungeon opens when 3 are finished")));

	// ONE FINISHED. Every Cataclysm asks for five quest dungeons or more, so both counts before it are plural.
	const int32 FirstNeeds = UCataclysmRoster::QuestObjectivesFor(Run->ActiveCataclysms[0]);
	FinishOne(*Run, Run->ActiveCataclysms[0]);
	TestEqual(TEXT("one of five finished"), UCataclysmEmpireMapLayout::ProgressLine(Run),
			  FString::Printf(TEXT("%d dungeons cleared, %d quest objectives, 1 of 5 Cataclysms finished; the boss "
								   "dungeon opens when 3 are finished"), FirstNeeds, FirstNeeds));

	FinishOne(*Run, Run->ActiveCataclysms[1]);
	TestTrue(TEXT("two of five finished"), UCataclysmEmpireMapLayout::ProgressLine(Run).EndsWith(
		TEXT(", 2 of 5 Cataclysms finished; the boss dungeon opens when 3 are finished")));

	// NOTHING ABOUT THE BOSS SHOWS BEFORE IT OPENS: no box, no brief, no opening, and the latest event names none.
	if (!TestFalse(TEXT("set-up: two of three finished, the boss has not opened"), Run->bCataclysmDungeonOpened))
	{
		return false;
	}
	TestEqual(TEXT("before it opens the marker has no label, so no box is shown"),
			  UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(Run), FString());
	TestEqual(TEXT("and no brief"), UCataclysmEmpireMapLayout::CataclysmDungeonBrief(Run), FString());
	TestEqual(TEXT("and no opening line"), UCataclysmEmpireMapLayout::CataclysmDungeonOpenedLine(Run), FString());
	TestFalse(TEXT("and the latest event names no boss"),
			  UCataclysmEmpireMapLayout::LatestEventLine(Run).Contains(TEXT("boss")));

	// THE THIRD OPENS IT.
	FinishOne(*Run, Run->ActiveCataclysms[2]);
	const FCataclysmDungeon* Boss = Run->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: the clear that finishes the third opens the boss"), Boss))
	{
		return false;
	}
	const int32 BossId = Boss->DungeonId;
	const FString Open = UCataclysmEmpireMapLayout::ProgressLine(Run);
	AddInfo(FString::Printf(TEXT("BOSS PROGRESS: tier 5 open: \"%s\""), *Open));
	TestTrue(TEXT("on the clear that finishes the third the line says the boss is open"),
			 Open.EndsWith(TEXT(", 3 of 5 Cataclysms finished; the Cataclysm boss dungeon is open")));

	// AND CLEARED. `ClearDungeon` is the only thing that takes it off the board.
	Run->ClearDungeon(BossId);
	if (!TestNull(TEXT("set-up: the boss has left the board"), Run->FindCataclysmDungeon()))
	{
		return false;
	}
	TestTrue(TEXT("once it has been cleared the line says so"), UCataclysmEmpireMapLayout::ProgressLine(Run).EndsWith(
		TEXT(", 3 of 5 Cataclysms finished; the Cataclysm boss dungeon has been cleared")));
	TestEqual(TEXT("and the box is hidden again"), UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(Run),
			  FString());

	// TIER 1: ONE CATACLYSM, SINGULAR.
	UCataclysmEmpireRun* Single = MakeTierRun(/* Seed */ 7, /* DifficultyTier */ 1);
	TestEqual(TEXT("a tier 1 run faces one Cataclysm, singular, and one opens the boss"),
			  UCataclysmEmpireMapLayout::ProgressLine(Single),
			  FString(TEXT("No dungeons cleared yet, no quest objectives, 0 of 1 Cataclysm finished; the boss "
						   "dungeon opens when 1 is finished")));

	// TIER 3, ITS TWO MET BY HAND AND NOT BY A CLEAR: the state a version 3 run record migrates into, since
	// `Migrate_3_to_4` writes the boss unopened. The next quest clear opens it, and the line says so before it does.
	UCataclysmEmpireRun* Met = MakeTierRun(/* Seed */ 3, /* DifficultyTier */ 3);
	if (!TestEqual(TEXT("set-up: a tier 3 run faces three"), Met->ActiveCataclysms.Num(), 3)
		|| !TestEqual(TEXT("set-up: and needs two"), Met->CataclysmDungeonRequirement(), 2))
	{
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const ECataclysmType Cataclysm = Met->ActiveCataclysms[Index];
		Met->QuestObjectivesByCataclysm.FindOrAdd(Cataclysm) = UCataclysmRoster::QuestObjectivesFor(Cataclysm);
	}
	if (!TestEqual(TEXT("set-up: two of three are finished"), Met->CataclysmsComplete(), 2)
		|| !TestFalse(TEXT("set-up: and the boss has not opened"), Met->bCataclysmDungeonOpened))
	{
		return false;
	}
	TestTrue(TEXT("met and not opened, the line says the next quest clear opens it"),
			 UCataclysmEmpireMapLayout::ProgressLine(Met).EndsWith(
				 TEXT(", 2 of 3 Cataclysms finished; the boss dungeon opens on the next quest dungeon cleared")));

	ClearOneByHand(*Met, ECataclysmDungeonType::Quest, Met->ActiveCataclysms[2]);
	TestTrue(TEXT("and the next quest clear does open it"), UCataclysmEmpireMapLayout::ProgressLine(Met).EndsWith(
		TEXT(", 2 of 3 Cataclysms finished; the Cataclysm boss dungeon is open")));
	return true;
}

// ---------------------------------------------------------------------------
// The marker
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossOnTheMapMarkerTextTest,
	"Cataclysm.BossOnTheMap.TheMarkerSaysHowDeepItIsWhoSentItAndThatItNeverResolves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three tier 1 runs at seed 11 open the boss after three ordinary dungeons defeated and one more after it opened
 * (four added), after one before it (one added), and with none (as it rolled). The box says the depth; the brief
 * names the Cataclysm that sent it and its sub-type, the floors and the floors the ordinary dungeons added, singular
 * and plural, and that it stands on no city and never resolves. Prints "BOSS MARKER".
 */
bool FCataclysmBossOnTheMapMarkerTextTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossOnTheMapTest;

	TestEqual(TEXT("no run has no marker"), UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(nullptr),
			  FString());
	TestEqual(TEXT("and no brief"), UCataclysmEmpireMapLayout::CataclysmDungeonBrief(nullptr), FString());

	const FString Rule = TEXT(". Every ordinary dungeon defeated adds a floor. It stands on no city and never "
							  "resolves.");

	// FOUR ADDED: three before it opened and one after.
	UCataclysmEmpireRun* Four = RunWithItsBossOpen(/* OrdinaryBefore */ 3, /* OrdinaryAfter */ 1);
	const FCataclysmDungeon* Boss = Four->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: the run opened its boss"), Boss)
		|| !TestEqual(TEXT("set-up: four ordinary dungeons were defeated"), Four->BasicDungeonsCleared, 4)
		|| !TestEqual(TEXT("set-up: so it is four floors deeper than its roll"), Boss->Floors,
					  Four->CataclysmDungeonBaseFloors + 4))
	{
		return false;
	}

	const FString Label = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(Four);
	const FString Brief = UCataclysmEmpireMapLayout::CataclysmDungeonBrief(Four);
	const FString Sender = UCataclysmRoster::NameFor(Boss->Cataclysm).ToString();
	const FString SubType = UCataclysmSurgeScheduler::SubTypeName(Boss->SubType);
	AddInfo(FString::Printf(TEXT("BOSS MARKER: \"%s\" / \"%s\""), *Label, *Brief));

	TestEqual(TEXT("the box says how deep it is"), Label,
			  FString::Printf(TEXT("Cataclysm boss: %d floors"), Boss->Floors));
	TestTrue(TEXT("the brief names the Cataclysm that sent it, which is the run's one"),
			 Boss->Cataclysm == Four->ActiveCataclysms[0]
				 && Brief.StartsWith(FString::Printf(TEXT("The Cataclysm boss dungeon (%s"), *Sender)));
	TestTrue(TEXT("and its sub-type, when it has one"),
			 SubType.IsEmpty() || Brief.Contains(FString::Printf(TEXT(", %s): "), *SubType)));
	TestTrue(TEXT("and says four of its floors were added by ordinary dungeons defeated"),
			 Brief.Contains(FString::Printf(TEXT(": %d floors, 4 of them added by ordinary dungeons defeated. "),
											Boss->Floors)));
	TestTrue(TEXT("and that it stands on no city and never resolves"), Brief.EndsWith(Rule));

	// ONE ADDED, SINGULAR.
	UCataclysmEmpireRun* One = RunWithItsBossOpen(/* OrdinaryBefore */ 1, /* OrdinaryAfter */ 0);
	const FCataclysmDungeon* OneBoss = One->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: the second run opened its boss"), OneBoss))
	{
		return false;
	}
	TestTrue(TEXT("one floor added is singular"), UCataclysmEmpireMapLayout::CataclysmDungeonBrief(One).Contains(
		FString::Printf(TEXT(": %d floors, 1 of them added by an ordinary dungeon defeated. "), OneBoss->Floors)));

	// NONE ADDED.
	UCataclysmEmpireRun* NoneAdded = RunWithItsBossOpen(/* OrdinaryBefore */ 0, /* OrdinaryAfter */ 0);
	const FCataclysmDungeon* NoneBoss = NoneAdded->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: the third run opened its boss"), NoneBoss)
		|| !TestEqual(TEXT("set-up: as deep as its roll"), NoneBoss->Floors, NoneAdded->CataclysmDungeonBaseFloors))
	{
		return false;
	}
	TestTrue(TEXT("none added reads as it rolled"), UCataclysmEmpireMapLayout::CataclysmDungeonBrief(NoneAdded).Contains(
		FString::Printf(TEXT(": %d floors, as it rolled. "), NoneBoss->Floors)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossOnTheMapMarkerPlaceTest,
	"Cataclysm.BossOnTheMap.TheMarkerSitsInTheEmptyCornerAndItsLabelFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The box is two cities wide and one tall, a margin in from the top and the left, and on every panel the city overlap
 * test uses -- and the panel the project owner's window gave on 2026-08-31 -- it is inside the panel and covers no
 * city. Its widest likely label, at a four-figure depth, measures narrower than it at full size, as
 * `Cataclysm.EmpireScreen.EveryCityLabelMeasuresNarrowerThanItsBox` measures every city's; that half is skipped, and
 * says so, when there is no Slate renderer. Prints "BOSS BOX".
 */
bool FCataclysmBossOnTheMapMarkerPlaceTest::RunTest(const FString& Parameters)
{
	// AT A SCALE OF ONE: 24 in from each edge, 344 by 48, so its centre is 24 + 172 across and 24 + 24 down.
	const FVector2D AtOne = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(1.0f);
	const FVector2D SizeAtOne = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(1.0f);
	TestEqual(TEXT("at a scale of one it is two cities wide"), SizeAtOne.X, 344.0, 0.01);
	TestEqual(TEXT("and one tall"), SizeAtOne.Y, 48.0, 0.01);
	TestEqual(TEXT("its centre is a margin and half its width from the left"), AtOne.X, 196.0, 0.01);
	TestEqual(TEXT("and a margin and half its height from the top"), AtOne.Y, 48.0, 0.01);

	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();

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
		const FVector2D Marker = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(Scale);
		const FVector2D At = UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(Scale);

		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the box is inside the panel"), Panel.X, Panel.Y),
				 At.X - Marker.X * 0.5 >= -0.01 && At.Y - Marker.Y * 0.5 >= -0.01
					 && At.X + Marker.X * 0.5 <= Panel.X + 0.01 && At.Y + Marker.Y * 0.5 <= Panel.Y + 0.01);

		// TWO RECTANGLES PLACED BY THEIR CENTRES MISS EACH OTHER when they are clear on either axis by half of both.
		for (const FCataclysmCity& City : Map->Cities)
		{
			const FVector2D Gap =
				(UCataclysmEmpireMapLayout::PositionFor(City.R, City.C, Scale, Panel) - At).GetAbs();
			TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the box covers no part of %s"), Panel.X, Panel.Y,
									 *City.Name),
					 Gap.X >= (Box.X + Marker.X) * 0.5 - 0.01 || Gap.Y >= (Box.Y + Marker.Y) * 0.5 - 0.01);
			++Checked;
		}
	}
	AddInfo(FString::Printf(TEXT("BOSS BOX: %d city placements checked against the box"), Checked));
	TestEqual(TEXT("twenty-five cities on each of four panels"), Checked, 100);

	// ---- the label fits ---------------------------------------------------
	//
	// THE SAME ARITHMETIC AS THE CITY LABELS' TEST: the designed 16 points of `WBP_ChoiceButton`'s text block, the
	// button's padding allowed for as 16 pixels, at full size, which a real window draws at.
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("no Slate renderer, so no font can be measured; where the boss dungeon's box sits is still checked, "
				 "but whether its label FITS is not checked at all"));
		return true;
	}

	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const UTextBlock* Probe = NewObject<UTextBlock>();
	if (!TestNotNull(TEXT("a text block was created to read its default font"), Probe))
	{
		return false;
	}
	FSlateFontInfo Font = Probe->GetFont();
	Font.Size = 16;
	const float PaddingAllowancePx = 16.0f;

	// A FOUR-FIGURE DEPTH, of eights, the widest digit in most faces: a roll of 150 at most and a floor for every
	// ordinary dungeon defeated, which no run reaches a thousand of.
	const FString Widest = TEXT("Cataclysm boss: 8888 floors");
	const float Needed = static_cast<float>(Measure->Measure(Widest, Font).X) + PaddingAllowancePx;
	AddInfo(FString::Printf(TEXT("BOSS BOX: \"%s\" needs %.0f pixels of %.0f"), *Widest, Needed,
							UCataclysmEmpireMapLayout::CataclysmDungeonMarkerWidthPx));
	TestTrue(FString::Printf(TEXT("at full size, \"%s\" needs %.0f pixels and the box is %.0f wide"), *Widest, Needed,
							 UCataclysmEmpireMapLayout::CataclysmDungeonMarkerWidthPx),
			 Needed <= UCataclysmEmpireMapLayout::CataclysmDungeonMarkerWidthPx);
	return true;
}

// ---------------------------------------------------------------------------
// The latest event
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossOnTheMapLatestEventTest,
	"Cataclysm.BossOnTheMap.TheOpeningIsTheLatestEventUntilSomethingLaterHappens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A tier 1 run on day 4 opens its boss. The screen's latest event is the opening while the last hit and the last
 * fall's move are earlier or on the same day, and a death is earlier; a later hit replaces it, as does a later move;
 * a death on its own day is shown instead. The opening stays the latest event after the boss has been cleared. The
 * fields are set by hand, as `Cataclysm.DeathCost.TheEmpireScreenShowsTheDeathWhileItIsTheLatestAndTheHitOnceOneIsLater`
 * sets them, because what is tested is the choosing. Prints "BOSS EVENT".
 */
bool FCataclysmBossOnTheMapLatestEventTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossOnTheMapTest;

	UCataclysmEmpireRun* Run = MakeTierRun(/* Seed */ 11, /* DifficultyTier */ 1);
	Run->AdvanceDays(4);
	if (!TestEqual(TEXT("set-up: the run is on day 4"), Run->Day(), 4))
	{
		return false;
	}

	// WHATEVER THE FOUR DAYS LANDED IS PUT ASIDE, and the hit set to day 3.
	Run->LastMoveDay = 0;
	Run->LastDeathDay = 0;
	Run->LastHitDay = 3;
	Run->LastHitCityId = 0;
	Run->bLastHitWasAQuestTimer = false;
	TestEqual(TEXT("CONTROL: before the boss opens the screen shows the hit"),
			  UCataclysmEmpireMapLayout::LatestEventLine(Run), UCataclysmEmpireMapLayout::LastHitLine(Run));

	FinishOne(*Run, Run->ActiveCataclysms[0]);
	const FCataclysmDungeon* Boss = Run->FindCataclysmDungeon();
	if (!TestNotNull(TEXT("set-up: finishing the run's one Cataclysm opens the boss"), Boss)
		|| !TestEqual(TEXT("set-up: on day 4"), Run->CataclysmDungeonOpenedDay, 4))
	{
		return false;
	}
	const int32 BossId = Boss->DungeonId;

	const FString Opened = UCataclysmEmpireMapLayout::CataclysmDungeonOpenedLine(Run);
	AddInfo(FString::Printf(TEXT("BOSS EVENT: \"%s\""), *Opened));
	TestEqual(TEXT("the opening's line"), Opened, FString(TEXT("Day 4: the Cataclysm boss dungeon opened.")));
	TestEqual(TEXT("an opening later than the last hit is shown"), UCataclysmEmpireMapLayout::LatestEventLine(Run),
			  Opened);

	Run->LastHitDay = 4;
	TestEqual(TEXT("on the same day as the last hit the opening is shown"),
			  UCataclysmEmpireMapLayout::LatestEventLine(Run), Opened);

	Run->LastHitDay = 5;
	TestTrue(TEXT("a later hit replaces the opening"),
			 UCataclysmEmpireMapLayout::LatestEventLine(Run).StartsWith(TEXT("Day 5: a dungeon hit ")));

	Run->LastHitDay = 3;
	Run->LastMoveDay = 4;
	Run->LastMoveFromCityId = 0;
	Run->LastMoveToCityId = 1;
	TestEqual(TEXT("on the same day as a fall's move the opening is shown"),
			  UCataclysmEmpireMapLayout::LatestEventLine(Run), Opened);

	Run->LastMoveDay = 6;
	TestTrue(TEXT("a later fall's move replaces the opening"),
			 UCataclysmEmpireMapLayout::LatestEventLine(Run).StartsWith(TEXT("Day 6: a Quest dungeon moved")));

	Run->LastMoveDay = 0;
	Run->NoteDeath(2, TEXT("the dungeon"), 1, 5, TArray<int32>());
	TestEqual(TEXT("an earlier death does not replace the opening"), UCataclysmEmpireMapLayout::LatestEventLine(Run),
			  Opened);

	Run->NoteDeath(4, TEXT("the dungeon"), 0, 5, TArray<int32>());
	const FString Death = UCataclysmEmpireMapLayout::LastDeathLine(Run);
	TestTrue(TEXT("set-up: the death has a line"), Death.StartsWith(TEXT("Day 4: you died in the dungeon.")));
	TestEqual(TEXT("a death on the opening's own day is shown"), UCataclysmEmpireMapLayout::LatestEventLine(Run),
			  Death);

	// AND IT STAYS THE RUN'S OPENING ONCE THE BOSS HAS BEEN CLEARED.
	Run->LastDeathDay = 0;
	Run->ClearDungeon(BossId);
	TestNull(TEXT("set-up: the boss has left the board"), Run->FindCataclysmDungeon());
	TestEqual(TEXT("the opening is still the latest event after it has been cleared"),
			  UCataclysmEmpireMapLayout::LatestEventLine(Run), Opened);
	return true;
}

// ---------------------------------------------------------------------------
// The status bar
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossOnTheMapStatusBarTest,
	"Cataclysm.BossOnTheMap.TheStatusBarCountsTheBossAsStandingAndPutsNoCityAtRisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The heads-up status bar (#49) counts the boss among the dungeons standing, because it stands, and puts no city at
 * risk for it, because it bites none. A board holding the boss alone reads "1 dungeon standing" and "0 cities at
 * risk". Prints "BOSS BAR".
 */
bool FCataclysmBossOnTheMapStatusBarTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossOnTheMapTest;

	UCataclysmEmpireRun* Run = MakeTierRun(/* Seed */ 11, /* DifficultyTier */ 1);
	Run->AdvanceDays(4);

	// HAND CLEARS ADD AND REMOVE THEIR OWN DUNGEON, so the board before the last quest clear is the board the four
	// days left.
	const FCataclysmEmpireStatusFacts Before = UCataclysmEmpireStatusBar::FactsOf(Run);
	FinishOne(*Run, Run->ActiveCataclysms[0]);
	const FCataclysmEmpireStatusFacts After = UCataclysmEmpireStatusBar::FactsOf(Run);
	if (!TestNotNull(TEXT("set-up: the boss opened"), Run->FindCataclysmDungeon())
		|| !TestTrue(TEXT("set-up: a run is bound"), After.bRunBound))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("BOSS BAR: before \"%s\", after \"%s\""), *UCataclysmEmpireStatusBar::LineFor(Before),
							*UCataclysmEmpireStatusBar::LineFor(After)));
	TestEqual(TEXT("the boss is one dungeon more standing"), After.DungeonsStanding, Before.DungeonsStanding + 1);
	TestEqual(TEXT("and puts no city more at risk"), After.CitiesAtRisk, Before.CitiesAtRisk);

	// EVERY OTHER DUNGEON CLEARED, so the board holds the boss alone.
	TArray<int32> Others;
	for (const FCataclysmDungeon& Standing : Run->Dungeons)
	{
		if (Standing.Type != ECataclysmDungeonType::Cataclysm)
		{
			Others.Add(Standing.DungeonId);
		}
	}
	for (const int32 DungeonId : Others)
	{
		Run->ClearDungeon(DungeonId);
	}
	if (!TestEqual(TEXT("set-up: the boss stands alone"), Run->DungeonCount(), 1))
	{
		return false;
	}

	const FString Alone = UCataclysmEmpireStatusBar::LineFor(UCataclysmEmpireStatusBar::FactsOf(Run));
	TestTrue(TEXT("the boss alone is one dungeon standing, singular"), Alone.StartsWith(TEXT("1 dungeon standing | ")));
	TestTrue(TEXT("and no city at risk"), Alone.EndsWith(TEXT(" | 0 cities at risk")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
