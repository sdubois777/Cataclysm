// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Player/CataclysmPlayLoopLog.h"

/**
 * THE PLAY LOOP'S LINES, built from what they are given (`FCataclysmPlayLoopLog`). Issue #2733 slice 4. The two tests
 * that need a world -- a walk logged step by step, and a death -- are in `CataclysmSaveLoadTests.cpp`, beside the
 * travel tests whose helpers they use.
 */

namespace CataclysmPlayLoopTextTest
{
	/** A run with its opening wave standing, as a new character's run begins. Seed 4242, as the city screen's tests. */
	UCataclysmEmpireRun* AnOpenedRun()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 4242, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Run->FireOpeningSurge();
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopChoicesTest,
	"Cataclysm.PlayLoop.TheFrontEndStartAndContinueLinesSayWhatWasChosen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopChoicesTest::RunTest(const FString&)
{
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("the prefix the Output Log's filter finds"), FString(Log::Prefix), FString(TEXT("PLAY LOOP: ")));
	TestEqual(TEXT("no character"), Log::FrontEndLine(0),
			  FString(TEXT("the front end is open: no saved character to continue, or New character to make one")));
	TestEqual(TEXT("one"), Log::FrontEndLine(1),
			  FString(TEXT("the front end is open: 1 saved character to continue, or New character to make one")));
	TestEqual(TEXT("three"), Log::FrontEndLine(3),
			  FString(TEXT("the front end is open: 3 saved characters to continue, or New character to make one")));
	TestEqual(TEXT("Begin"), Log::NewCharacterLine(TEXT("Ash"), FName(TEXT("Greatsword")), FName(TEXT("War"))),
			  FString(TEXT("new character 'Ash' (Greatsword, War) begun: opening the capital")));
	TestEqual(TEXT("Continue to the capital"), Log::ContinueLine(TEXT("Ash"), false),
			  FString(TEXT("continuing 'Ash': opening the capital")));
	TestEqual(TEXT("Continue to a walk saved in a dungeon"), Log::ContinueLine(TEXT("Ash"), true),
			  FString(TEXT("continuing 'Ash': opening the dungeon level, to resume the walk it was saved in")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopWalkTextTest,
	"Cataclysm.PlayLoop.TheWalksLinesSayItsFloorsItsDaysAndItsEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopWalkTextTest::RunTest(const FString&)
{
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("entered, one day"), Log::EnteredLine(3, 12, 1),
			  FString(TEXT("entered dungeon 3, floor 1 of 12; the walk owes 1 day")));
	TestEqual(TEXT("entered, a Cow Level's two"), Log::EnteredLine(3, 12, 2),
			  FString(TEXT("entered dungeon 3, floor 1 of 12; the walk owes 2 days")));
	TestEqual(TEXT("a floor"), Log::FloorLine(3, 2, 12, 2),
			  FString(TEXT("dungeon 3: down to floor 2 of 12; the walk owes 2 days")));
	TestEqual(TEXT("the clear"), Log::ClearLine(3, 12, 1.0, 4),
			  FString(TEXT("cleared dungeon 3 on day 12: +1.0 empire points; level 4: back to the capital")));
	TestEqual(TEXT("a death"), Log::DeathLine(3, 2, 12, 5),
			  FString(TEXT("died in dungeon 3 on floor 2 of 12: the death cost 5 days: back to the capital")));
	TestEqual(TEXT("Walk on a dungeon not on the run names it by its number"), Log::WalkLine(nullptr, 9),
			  FString(TEXT("Walk pressed: dungeon 9; travelling to the dungeon level")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopStandingTest,
	"Cataclysm.PlayLoop.EachDungeonStandingIsListedByItsNameFloorsTimerAndModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopStandingTest::RunTest(const FString&)
{
	using Log = FCataclysmPlayLoopLog;
	using Effects = UCataclysmDungeonModifierEffects;

	UCataclysmEmpireRun* Run = CataclysmPlayLoopTextTest::AnOpenedRun();
	if (!TestTrue(TEXT("set-up: the opening wave stands"), Run && Run->Clock && Run->DungeonCount() > 1))
	{
		return false;
	}
	// TWO MODIFIERS ON THE FIRST DUNGEON AND NONE ON THE SECOND, set by hand, so both readings are seen whatever the
	// seed rolled.
	const FName First(Effects::PactOfTemptationKey);
	const FName Second(Effects::VoidParasiteKey);
	Run->Dungeons[0].Modifiers = {First, Second};
	Run->Dungeons[1].Modifiers.Reset();

	const TArray<FString> Lines = Log::StandingLines(Run);
	AddInfo(FString::Printf(TEXT("PLAY LOOP STANDING: %s"), *FString::Join(Lines, TEXT(" | "))));
	TestEqual(TEXT("one line per dungeon standing"), Lines.Num(), Run->DungeonCount());

	int32 Wrong = 0;
	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		const FString Expected = FString::Printf(TEXT("standing: dungeon %d, %s: %d floors, %.0f days until it bites, "),
												 Dungeon.DungeonId, *Run->NameOfDungeon(Dungeon.DungeonId),
												 Dungeon.Floors, Run->Clock->DaysUntilResolveFor(Dungeon.DungeonId));
		const bool bFound = Lines.ContainsByPredicate([&Expected](const FString& Line) { return Line.StartsWith(Expected); });
		Wrong += bFound ? 0 : 1;
		if (!bFound)
		{
			AddInfo(FString::Printf(TEXT("no line begins \"%s\""), *Expected));
		}
	}
	TestEqual(TEXT("each names its dungeon, its kind and city, its floors and the days until it bites"), Wrong, 0);
	TestTrue(TEXT("the first carries its two modifiers by name"),
			 Log::DungeonLine(Run, Run->Dungeons[0].DungeonId).EndsWith(FString::Printf(
				 TEXT(", modifiers %s, %s"), *UCataclysmDungeonModifierTable::NameOf(First),
				 *UCataclysmDungeonModifierTable::NameOf(Second))));
	TestTrue(TEXT("the second says it carries none"),
			 Log::DungeonLine(Run, Run->Dungeons[1].DungeonId).EndsWith(TEXT(", no modifiers")));
	TestEqual(TEXT("a dungeon not on the run has no line"), Log::DungeonLine(Run, 999999), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopAwayTest,
	"Cataclysm.PlayLoop.TheDaysAwayNameWhatResolvedWhatEachCityLostWhatFellAndTheSurges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopAwayTest::RunTest(const FString&)
{
	using Log = FCataclysmPlayLoopLog;

	UCataclysmEmpireRun* Run = CataclysmPlayLoopTextTest::AnOpenedRun();
	if (!TestTrue(TEXT("set-up: the opening wave stands"), Run && Run->Clock && Run->Map && Run->DungeonCount() > 0))
	{
		return false;
	}

	// AS FAR AS THE SOONEST TIMER, so at least one dungeon resolves: the days a walk could have taken.
	float Soonest = TNumericLimits<float>::Max();
	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		if (Dungeon.Resolves())
		{
			Soonest = FMath::Min(Soonest, Run->Clock->DaysUntilResolveFor(Dungeon.DungeonId));
		}
	}
	if (!TestTrue(TEXT("set-up: a dungeon that resolves stands"), Soonest < 1000.0f))
	{
		return false;
	}
	const int32 Days = FMath::CeilToInt(Soonest);
	const int32 FromDay = Run->Day();
	const TMap<int32, FString> Before = Log::NamesStanding(Run);
	const TArray<FCataclysmDayReport> Reports = Run->AdvanceDays(Days);
	const FString Away = Log::AwayAccount(FromDay, Run->Day(), Reports, Before, Run);
	AddInfo(FString::Printf(TEXT("PLAY LOOP AWAY: %d days: %s"), Days, *Away));

	// WHAT THE REPORTS SAY, WORKED OUT HERE, and each looked for in the account.
	TArray<int32> Resolved;
	TMap<int32, TPair<float, float>> Lost;
	TArray<int32> Fallen;
	int32 Surges = 0;
	int32 SurgeDungeons = 0;
	for (const FCataclysmDayReport& Day : Reports)
	{
		for (const int32 Id : Day.Resolved)
		{
			Resolved.AddUnique(Id);
		}
		for (const FCataclysmCityHit& Hit : Day.Hits)
		{
			TPair<float, float>& Sum = Lost.FindOrAdd(Hit.CityId);
			Sum.Key += Hit.DefenceLost;
			Sum.Value += Hit.PopulationLost;
		}
		for (const int32 CityId : Day.Fallen)
		{
			Fallen.AddUnique(CityId);
		}
		// EACH SURGE, NOT EACH DAY THAT HAD ONE: since issue #2865 every city that falls fires its own, so a day can
		// hold several. A day with one counts one either way, so this reads the same as before on such a day.
		Surges += Day.Surges;
		TestTrue(FString::Printf(TEXT("day %d: a day that surged counts at least one surge, and one that did not none"),
								 Day.Day),
				 Day.bSurged ? Day.Surges >= 1 : Day.Surges == 0);
		SurgeDungeons += Day.SurgeDungeons;
	}
	TestTrue(TEXT("CONTROL: the days resolved a dungeon"), Resolved.Num() > 0);
	TestTrue(TEXT("CONTROL: and hit a city"), Lost.Num() > 0);
	TestTrue(TEXT("it says which days"),
			 Away.StartsWith(FString::Printf(TEXT("while away, days %d to %d: dungeons resolved: "), FromDay, Run->Day())));
	int32 Missing = 0;
	for (const int32 Id : Resolved)
	{
		const FString* Name = Before.Find(Id);
		Missing += Name && Away.Contains(FString::Printf(TEXT("%d (%s)"), Id, **Name)) ? 0 : 1;
	}
	TestEqual(TEXT("each dungeon resolved, by the name it had before the days ran"), Missing, 0);
	Missing = 0;
	for (const TPair<int32, TPair<float, float>>& City : Lost)
	{
		const FCataclysmCity* Hit = Run->Map->Find(City.Key);
		Missing += Hit && Away.Contains(FString::Printf(TEXT("%s lost %.0f defence and %.0f people"), *Hit->Name,
														City.Value.Key, City.Value.Value))
			? 0 : 1;
	}
	TestEqual(TEXT("each city hit, with all it lost added up"), Missing, 0);
	TArray<FString> FallenNames;
	for (const int32 CityId : Fallen)
	{
		const FCataclysmCity* City = Run->Map->Find(CityId);
		FallenNames.Add(City ? City->Name : FString::Printf(TEXT("city %d"), CityId));
	}
	TestTrue(TEXT("each city fallen, or none"),
			 Away.Contains(FString::Printf(TEXT("; cities fallen: %s; "),
										   FallenNames.IsEmpty() ? TEXT("none") : *FString::Join(FallenNames, TEXT(", ")))));
	const FString SurgeText = Surges == 0
		? FString(TEXT("no surge"))
		: FString::Printf(TEXT("%d surge%s brought %d dungeon%s"), Surges, Surges == 1 ? TEXT("") : TEXT("s"),
						  SurgeDungeons, SurgeDungeons == 1 ? TEXT("") : TEXT("s"));
	TestTrue(TEXT("and the surges"), Away.EndsWith(SurgeText));
	TestEqual(TEXT("no day passed"), Log::AwayAccount(4, 4, {}, {}, Run), FString(TEXT("while away: no day passed")));
	return true;
}

/**
 * Days spent in the capital are told in the walk's words after their own lead, issue #2857: a forge craft's "at the
 * forge", the respec's "for the respec", the screen's "Meanwhile"; a walk's are still "while away" when no lead is given.
 * `DaysDidSomething` is true for each thing the account names and false for days that did none of them, which is when a
 * screen adds nothing. Prints "CAPITAL DAYS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopCapitalDaysTextTest,
	"Cataclysm.PlayLoop.DaysSpentInTheCapitalAreToldInTheWalksWordsAfterTheirOwnLead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopCapitalDaysTextTest::RunTest(const FString&)
{
	using Log = FCataclysmPlayLoopLog;

	UCataclysmEmpireRun* Run = CataclysmPlayLoopTextTest::AnOpenedRun();
	if (!TestTrue(TEXT("set-up: the opening wave stands"), Run && Run->Map && Run->Map->Cities.Num() > 0))
	{
		return false;
	}

	TArray<FCataclysmDayReport> Quiet;
	Quiet.AddDefaulted();
	Quiet[0].Day = 5;
	const FString AtTheForge = Log::AwayAccount(4, 5, Quiet, {}, Run, TEXT("at the forge"));
	AddInfo(FString::Printf(TEXT("CAPITAL DAYS: \"%s\""), *AtTheForge));

	TestEqual(TEXT("a forge craft's quiet day"), AtTheForge,
			  FString(TEXT("at the forge, days 4 to 5: dungeons resolved: none; cities hit: none; cities fallen: none; no "
						   "surge")));
	TestEqual(TEXT("the respec's lead"), Log::AwayAccount(4, 5, Quiet, {}, Run, TEXT("for the respec")),
			  FString(TEXT("for the respec, days 4 to 5: dungeons resolved: none; cities hit: none; cities fallen: none; "
						   "no surge")));
	TestEqual(TEXT("no day passed, after a lead"), Log::AwayAccount(4, 4, {}, {}, Run, TEXT("at the forge")),
			  FString(TEXT("at the forge: no day passed")));
	TestTrue(TEXT("CONTROL: with no lead given, a walk's words"),
			 Log::AwayAccount(4, 5, Quiet, {}, Run).StartsWith(TEXT("while away, days 4 to 5: ")));

	// EACH THING THE ACCOUNT NAMES MAKES THE DAYS COUNT; NONE OF THEM, NOT.
	TestFalse(TEXT("a day that did nothing did nothing"), Log::DaysDidSomething(Quiet));
	TestFalse(TEXT("nor did no day"), Log::DaysDidSomething(TArray<FCataclysmDayReport>()));
	const int32 CityId = Run->Map->Cities[0].CityId;
	const auto OneDayWith = [&Quiet](const auto& Change)
	{
		TArray<FCataclysmDayReport> Days = Quiet;
		Change(Days[0]);
		return Days;
	};
	TestTrue(TEXT("a dungeon resolved"),
			 Log::DaysDidSomething(OneDayWith([](FCataclysmDayReport& Day) { Day.Resolved.Add(3); })));
	TestTrue(TEXT("a city hit"), Log::DaysDidSomething(OneDayWith([CityId](FCataclysmDayReport& Day)
	{
		FCataclysmCityHit& Hit = Day.Hits.AddDefaulted_GetRef();
		Hit.CityId = CityId;
	})));
	TestTrue(TEXT("a city fallen"),
			 Log::DaysDidSomething(OneDayWith([CityId](FCataclysmDayReport& Day) { Day.Fallen.Add(CityId); })));
	// A SURGE AS `FireSurge` WRITES IT: the flag and the count, which `AwayAccount` reads since issue #2865.
	const TArray<FCataclysmDayReport> SurgeDay = OneDayWith([](FCataclysmDayReport& Day)
	{
		Day.bSurged = true;
		Day.Surges = 1;
		Day.SurgeDungeons = 6;
	});
	TestTrue(TEXT("a surge"), Log::DaysDidSomething(SurgeDay));
	TestTrue(TEXT("and the account names it"),
			 Log::AwayAccount(4, 5, SurgeDay, {}, Run, TEXT("at the forge")).EndsWith(TEXT("1 surge brought 6 dungeons")));
	TestTrue(TEXT("a city upgrade finished"),
			 Log::DaysDidSomething(OneDayWith([CityId](FCataclysmDayReport& Day) { Day.UpgradesFinishedOn.Add(CityId); })));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopCapitalTextTest,
	"Cataclysm.PlayLoop.TheCapitalLineSaysTheBoardTheCharacterAndTheDaysAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopCapitalTextTest::RunTest(const FString&)
{
	using Log = FCataclysmPlayLoopLog;

	UCataclysmEmpireRun* Run = CataclysmPlayLoopTextTest::AnOpenedRun();
	if (!TestTrue(TEXT("set-up: the opening wave stands"), Run && Run->Map))
	{
		return false;
	}
	const FString Board = FString::Printf(TEXT("in the capital on day %d, tier %d: %d dungeons standing, %d cities fallen"),
										  Run->Day(), Run->DifficultyTier, Run->DungeonCount(),
										  Run->Map->FallenCityCount());
	TestEqual(TEXT("the board and the character"), Log::CapitalLine(Run, 3, 120, FString()),
			  Board + TEXT("; level 3, 120 experience into it"));
	TestEqual(TEXT("and the days away after a walk"), Log::CapitalLine(Run, 3, 120, TEXT("while away: no day passed")),
			  Board + TEXT("; level 3, 120 experience into it; while away: no day passed"));
	TestEqual(TEXT("with no run"), Log::CapitalLine(nullptr, 1, 0, FString()),
			  FString(TEXT("in the capital with no empire run; level 1, 0 experience into it")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopLevelGainedTextTest,
	"Cataclysm.PlayLoop.TheLevelGainedLineSaysTheLevelReachedAndWhatTheNextOneStillCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopLevelGainedTextTest::RunTest(const FString&)
{
	// Issue #2744. The figures are today's curve: level 3 costs 49,400 and level 5 costs 136,120 since issue #2749's
	// opening (`tools/tests/test_experience_curve_port.py` holds the curve to the simulation's), so a retuned curve
	// fails this test and says which line's figures moved, rather than leaving the checklist quoting a stale example.
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("one level, with the cost of level 3 less what is in hand"), Log::LevelGainedLine(1, 2, 3120),
			  FString(TEXT("level 2 reached, from level 1: 3120 experience into it, 46280 more to level 3")));
	TestEqual(TEXT("several levels in one grant name where it started"), Log::LevelGainedLine(1, 4, 10),
			  FString(TEXT("level 4 reached, from level 1: 10 experience into it, 136110 more to level 5")));
	TestEqual(TEXT("the maximum level has no next level"), Log::LevelGainedLine(99, 100, 0),
			  FString(TEXT("level 100 reached, from level 99: the maximum level")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopBuildTextTest,
	"Cataclysm.PlayLoop.TheBuildLinesSayWhatChangedAndWhatIsLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopBuildTextTest::RunTest(const FString&)
{
	// Issue #2763: one line for each change the player makes to the build, said beside the write of the character
	// record. The names are real rows of `game/Data/PassiveNodes.csv` and `WeaponSkills.csv`, but nothing here reads
	// a table: these are the words for the figures given, and the places that say them pass the figures.
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("a passive point: the node, its tree, its points and the character's"),
			  Log::PassivePointLine(TEXT("Fervour"), TEXT("Masochist"), 1, 1, 1, 2),
			  FString(TEXT("spent a passive point in Fervour (Masochist): 1 of 1; 1 of 2 passive points spent")));
	TestEqual(TEXT("a capstone option: the option, the capstone and the points spent with it"),
			  Log::PassiveOptionLine(TEXT("Reprisal Wave"), TEXT("The First Vow"), 26, 33),
			  FString(TEXT("took the option Reprisal Wave at the capstone The First Vow; 26 of 33 passive points spent")));
	TestEqual(TEXT("the passive tree reset, several points back"), Log::PassiveResetLine(3, 3),
			  FString(TEXT("reset the passive tree: 3 points back; 3 to spend")));
	TestEqual(TEXT("and one point back is one point"), Log::PassiveResetLine(1, 2),
			  FString(TEXT("reset the passive tree: 1 point back; 2 to spend")));
	TestEqual(TEXT("attribute points: how many, on what, what it now holds and what is left"),
			  Log::AttributePointLine(1, TEXT("Ferocity"), 2, 0),
			  FString(TEXT("spent 1 attribute point on Ferocity: 2 in it; 0 unspent")));
	TestEqual(TEXT("and several at once, as the console spends them"), Log::AttributePointLine(3, TEXT("Mind"), 3, 2),
			  FString(TEXT("spent 3 attribute points on Mind: 3 in it; 2 unspent")));
	TestEqual(TEXT("the attribute points reset"), Log::AttributeResetLine(2, 2),
			  FString(TEXT("reset the attribute points: 2 points back; 2 unspent")));
	TestEqual(TEXT("a skill put on a key, by its name"), Log::SkillChosenLine(TEXT("Pyroclasm"), TEXT("Heavy")),
			  FString(TEXT("put Pyroclasm in the Heavy slot")));
	TestEqual(TEXT("a key's choice forgotten"), Log::SkillForgottenLine(TEXT("Heavy")),
			  FString(TEXT("forgot the Heavy slot's skill choice: it shows its default again")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopForgeTextTest,
	"Cataclysm.PlayLoop.TheForgesLinesSayWhatWasRaisedAndWhatWasCombined",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopForgeTextTest::RunTest(const FString&)
{
	// The forge, the owner's ruling of 2026-11-19. `docs/Play_Loop_Checklist.md` step 16 quotes both lines.
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("an upgrade of one day"), Log::ForgeUpgradeLine(TEXT("Everyday Torc of Warding"), 0, 1, 14, 510, 52, 77,
																   1, 25, 26),
			  FString(TEXT("upgraded Everyday Torc of Warding from +0 to +1 with two +1 stones, 14 left, and 510 gold; "
						   "residue 52 to 77; it cost 1 day, day 25 to day 26")));
	TestEqual(TEXT("an upgrade of four days"), Log::ForgeUpgradeLine(TEXT("Treads"), 2, 3, 0, 3000, 150, 175, 4, 30, 34),
			  FString(TEXT("upgraded Treads from +2 to +3 with two +3 stones, 0 left, and 3,000 gold; residue 150 to 175; "
						   "it cost 4 days, day 30 to day 34")));
	TestEqual(TEXT("a combine"), Log::ForgeCombineLine(1, 2, 12, 20, 50),
			  FString(TEXT("combined two +1 upgrade stones into one +2 for 50 gold: 12 +1 and 20 +2 held")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopGearTextTest,
	"Cataclysm.PlayLoop.ThePickedUpAndWoreLinesNameThePieceAndTheHitBeforeAndAfter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopGearTextTest::RunTest(const FString&)
{
	// The coordinating session's ruling of 2026-11-19. The figures are the owner's case: a Greataxe +0 hits for 57.6,
	// 63.4 on a Demonic creature's health with Slashing's 10%, and 97.2 with a T1 Torc of +53.3% against Demonic
	// enemies (`Cataclysm.ItemComparison.ANecklaceAgainstTheRunsFamilyRaisesItsLineAndNotThePlainHit` works them out).
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("a piece picked up, by the name the floor showed"), Log::PickedUpLine(TEXT("Quality Torc of Warding"), 0),
			  FString(TEXT("picked up Quality Torc of Warding, +0")));
	TestEqual(TEXT("and its gear level"), Log::PickedUpLine(TEXT("Legendary Greataxe"), 4),
			  FString(TEXT("picked up Legendary Greataxe, +4")));

	Log::FHits Before;
	Before.Plain = 57.6f;
	Before.Families = {FName(TEXT("Demonic"))};
	Before.OnFamilies = {63.36f};
	Log::FHits After = Before;
	After.OnFamilies = {97.152f};
	TestEqual(TEXT("a piece worn, with both hits before and after"),
			  Log::WoreLine(TEXT("Everyday Torc"), TEXT("Necklace"), Before, After),
			  FString(TEXT("wore Everyday Torc in Necklace: basic attack hit 57.6 to 57.6; on a Demonic creature's "
						   "health 63.4 to 97.2")));

	// WITH NO RUN THERE IS NO FAMILY, and the line says only the plain hit.
	Log::FHits NoRun;
	NoRun.Plain = 57.6f;
	Log::FHits Sword;
	Sword.Plain = 16.0f;
	TestEqual(TEXT("with no run, the plain hit alone"), Log::WoreLine(TEXT("Sword"), TEXT("Weapon 1"), NoRun, Sword),
			  FString(TEXT("wore Sword in Weapon 1: basic attack hit 57.6 to 16")));

	// A FAMILY ONLY ONE SIDE PRICED IS LEFT OUT, rather than set against nothing.
	Log::FHits Other = After;
	Other.Families = {FName(TEXT("Death"))};
	TestEqual(TEXT("a family the other side did not price is left out"),
			  Log::WoreLine(TEXT("Everyday Torc"), TEXT("Necklace"), Before, Other),
			  FString(TEXT("wore Everyday Torc in Necklace: basic attack hit 57.6 to 57.6")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayLoopSkillLevelTextTest,
	"Cataclysm.PlayLoop.TheSkillLevelLineSaysTheSkillAndBothLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayLoopSkillLevelTextTest::RunTest(const FString&)
{
	// Issue #2764: one line for each skill level gained, from a kill's share or from a character level that let a
	// skill's kept experience through. The skill by its name, the level reached, the level it came from.
	using Log = FCataclysmPlayLoopLog;
	TestEqual(TEXT("one level"), Log::SkillLevelLine(TEXT("Molten Cleave"), 2, 3),
			  FString(TEXT("skill Molten Cleave reached level 3, from level 2")));
	TestEqual(TEXT("several at once name where it started"), Log::SkillLevelLine(TEXT("Pyroclasm"), 1, 4),
			  FString(TEXT("skill Pyroclasm reached level 4, from level 1")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
