// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmExperience.h"

/**
 * Tests for the character experience curve.
 *
 * REGISTERED UNDER `Cataclysm.Experience`, which is not this file's name. A run
 * narrowed with the wrong prefix reports "0 tests performed" and reads exactly
 * like a passing guard, so the prefix is stated here for whoever narrows to it.
 *
 * WHAT THESE CHECK AND WHAT `tools/tests/test_experience_curve_port.py` CHECKS.
 * That file reads the two constants out of the header and compares them with
 * `sim/analyse_experience_curve.py`. These run the code. A test against a
 * constant cannot notice that the code ignores it, and a test that reads a
 * constant out of source cannot notice the constant is wrong, so both halves
 * are needed and neither is redundant.
 *
 * EVERY EXPECTED FIGURE BELOW IS COMPUTED FROM `SecondLevelCost` AND
 * `GrowthPerLevel` RATHER THAN TYPED, except the seven quoted from the design
 * document in the first test. Typing the other ninety-two would make this file
 * a second copy of the curve, which is the failure the port test exists to
 * prevent.
 */

namespace CataclysmExperienceTest
{
	/** The curve worked out here, independently of the port's own table. */
	int64 ExpectedCost(int32 Level)
	{
		if (Level < 2 || Level > UCataclysmExperience::MaxLevel)
		{
			return 0;
		}
		const double Raw = static_cast<double>(UCataclysmExperience::SecondLevelCost)
			* FMath::Pow(1.0 + UCataclysmExperience::GrowthPerLevel,
						 static_cast<double>(Level - 2));
		return static_cast<int64>(FMath::FloorToDouble(Raw + 0.5));
	}
}

// ---------------------------------------------------------------------------
// The curve itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceMatchesTheDesignDocument,
	"Cataclysm.Experience.TheCurveMatchesTheDesignDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceMatchesTheDesignDocument::RunTest(const FString&)
{
	// DELIBERATELY DOES NOT USE THE HELPER that recomputes the curve, unlike
	// every other test here. This one compares the port against the design
	// document, so recomputing would compare the port against itself.

	// THE ONLY TYPED NUMBERS IN THIS FILE, and they are typed on purpose: they
	// are what `docs/Cataclysm_GDD_v2.md` section XII prints in its cost table,
	// so this is the one test that would notice the port and the document
	// drifting apart while remaining internally consistent.
	struct FStated { int32 Level; int64 Cost; int64 Cumulative; };
	const FStated Stated[] = {
		{   2,       230000,          230000 },
		{  10,       432062,         2896231 },
		{  25,      1409142,        15788927 },
		{  50,     10107328,       130562548 },
		{  75,     72496639,       953797118 },
		{  90,    236443176,      3117091665 },
		{ 100,    519995268,      6858596102 },
	};

	for (const FStated& Row : Stated)
	{
		TestEqual(FString::Printf(TEXT("level %d costs"), Row.Level),
			UCataclysmExperience::CostOfLevel(Row.Level), Row.Cost);
		TestEqual(FString::Printf(TEXT("reaching level %d costs in total"), Row.Level),
			UCataclysmExperience::TotalToReach(Row.Level), Row.Cumulative);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceEveryLevelFollowsTheRate,
	"Cataclysm.Experience.EveryLevelCostsTheRateMoreThanTheOneBelow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceEveryLevelFollowsTheRate::RunTest(const FString&)
{
	using namespace CataclysmExperienceTest;

	// ALL NINETY-NINE, not a sample. The table is built in a loop and an
	// off-by-one in its exponent would agree at level 2 and diverge everywhere
	// else, which a sample of five could easily miss.
	for (int32 Level = 2; Level <= UCataclysmExperience::MaxLevel; ++Level)
	{
		TestEqual(FString::Printf(TEXT("level %d costs"), Level),
			UCataclysmExperience::CostOfLevel(Level), ExpectedCost(Level));
	}

	// AND IT RISES EVERY STEP. A rounding that went the wrong way, or a rate
	// read as a divisor, would show here even if the table matched itself.
	for (int32 Level = 3; Level <= UCataclysmExperience::MaxLevel; ++Level)
	{
		TestTrue(FString::Printf(TEXT("level %d costs more than level %d"),
				 Level, Level - 1),
			UCataclysmExperience::CostOfLevel(Level) > UCataclysmExperience::CostOfLevel(Level - 1));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceOutsideTheRangeCostsNothing,
	"Cataclysm.Experience.ALevelOutsideTheRangeCostsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceOutsideTheRangeCostsNothing::RunTest(const FString&)
{
	// A character starts at level 1, so reaching it costs nothing, and there is
	// nothing above the maximum. Both answers are zero rather than an error,
	// which is what lets Grant and TotalToReach stay free of boundary cases.
	TestEqual(TEXT("level 1 costs nothing"), UCataclysmExperience::CostOfLevel(1), (int64)0);
	TestEqual(TEXT("level 0 costs nothing"), UCataclysmExperience::CostOfLevel(0), (int64)0);
	TestEqual(TEXT("a negative level costs nothing"),
		UCataclysmExperience::CostOfLevel(-5), (int64)0);
	TestEqual(TEXT("above the maximum costs nothing"),
		UCataclysmExperience::CostOfLevel(UCataclysmExperience::MaxLevel + 1), (int64)0);

	TestEqual(TEXT("reaching level 1 costs nothing"),
		UCataclysmExperience::TotalToReach(1), (int64)0);
	TestEqual(TEXT("reaching level 2 costs one level"),
		UCataclysmExperience::TotalToReach(2), UCataclysmExperience::CostOfLevel(2));
	TestEqual(TEXT("asking above the maximum gives the whole climb"),
		UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel + 50),
		UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceTotalIsSummedNotClosedForm,
	"Cataclysm.Experience.TheTotalIsSummedFromWholeLevelCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceTotalIsSummedNotClosedForm::RunTest(const FString&)
{
	using namespace CataclysmExperienceTest;

	// THE DISTINCTION THAT IS EASY TO COLLAPSE. A player pays each level's whole
	// cost, so the climb is the sum of 99 roundings. The closed-form geometric
	// sum, rounded once, is 5 lower. Five is nothing as a quantity and
	// everything to a save record that stores progress into a level.
	int64 Summed = 0;
	for (int32 Level = 2; Level <= UCataclysmExperience::MaxLevel; ++Level)
	{
		Summed += ExpectedCost(Level);
	}

	const double ClosedForm = static_cast<double>(UCataclysmExperience::SecondLevelCost)
		* (FMath::Pow(1.0 + UCataclysmExperience::GrowthPerLevel,
					  static_cast<double>(UCataclysmExperience::MaxLevel - 1)) - 1.0)
		/ UCataclysmExperience::GrowthPerLevel;
	const int64 RoundedClosedForm =
		static_cast<int64>(FMath::FloorToDouble(ClosedForm + 0.5));

	TestEqual(TEXT("the whole climb is the sum of the level costs"),
		UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel), Summed);
	TestTrue(TEXT("and that is NOT the rounded closed-form sum, which is why "
				  "TotalToReach adds them up"),
		Summed != RoundedClosedForm);
	TestTrue(TEXT("the two differ by less than a hundred, so one of them is not "
				  "simply wrong"),
		FMath::Abs(Summed - RoundedClosedForm) < 100);

	return true;
}

// ---------------------------------------------------------------------------
// Granting experience
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceGrantRaisesALevel,
	"Cataclysm.Experience.GrantingEnoughRaisesALevelAndCarriesTheRemainder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceGrantRaisesALevel::RunTest(const FString&)
{
	int32 Level = 1;
	int64 Progress = 0;

	// One short of level 2 leaves the character at level 1 with all of it banked.
	const int64 ToSecond = UCataclysmExperience::CostOfLevel(2);
	TestEqual(TEXT("one short of the next level gains nothing"),
		UCataclysmExperience::Grant(ToSecond - 1, Level, Progress), 0);
	TestEqual(TEXT("and stays at level 1"), Level, 1);
	TestEqual(TEXT("with everything banked"), Progress, ToSecond - 1);

	// The last unit tips it over, and nothing is left behind.
	TestEqual(TEXT("the last unit gains the level"),
		UCataclysmExperience::Grant(1, Level, Progress), 1);
	TestEqual(TEXT("now level 2"), Level, 2);
	TestEqual(TEXT("with nothing left over"), Progress, (int64)0);

	// A remainder carries into the new level rather than being discarded.
	const int64 ToThird = UCataclysmExperience::CostOfLevel(3);
	TestEqual(TEXT("overshooting gains one level"),
		UCataclysmExperience::Grant(ToThird + 7, Level, Progress), 1);
	TestEqual(TEXT("now level 3"), Level, 3);
	TestEqual(TEXT("and the overshoot carried"), Progress, (int64)7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceOneGrantCanCrossManyLevels,
	"Cataclysm.Experience.OneGrantCanCrossSeveralLevelsAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceOneGrantCanCrossManyLevels::RunTest(const FString&)
{
	// A REAL CASE AND NOT A CONTRIVED ONE. Early levels cost a fraction of a
	// dungeon, so one boss can pay for several of them, and a loop that assumed
	// one level per grant would silently drop the rest.
	int32 Level = 1;
	int64 Progress = 0;

	const int64 ToLevelTen = UCataclysmExperience::TotalToReach(10);
	const int32 Gained = UCataclysmExperience::Grant(ToLevelTen, Level, Progress);

	TestEqual(TEXT("nine levels gained in one grant"), Gained, 9);
	TestEqual(TEXT("landing exactly on level 10"), Level, 10);
	TestEqual(TEXT("with nothing left over"), Progress, (int64)0);

	// The count returned is what a caller awarding a point per level needs, and
	// it cannot be worked out afterwards, so it has to be right.
	TestEqual(TEXT("the levels gained is the difference in level"),
		Gained, Level - 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceStopsAtTheMaximum,
	"Cataclysm.Experience.TheMaximumLevelAbsorbsAnyFurtherGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceStopsAtTheMaximum::RunTest(const FString&)
{
	int32 Level = 1;
	int64 Progress = 0;

	// Paying for the whole climb in one go lands exactly on the maximum.
	const int32 Gained = UCataclysmExperience::Grant(
		UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel), Level, Progress);
	TestEqual(TEXT("ninety-nine levels gained"), Gained, 99);
	TestEqual(TEXT("at the maximum level"), Level, UCataclysmExperience::MaxLevel);
	TestEqual(TEXT("with nothing banked"), Progress, (int64)0);

	// AND NOTHING ACCUMULATES ABOVE IT. A number that only ever grows and can
	// never be spent eventually overflows and does nothing useful first.
	TestEqual(TEXT("a further grant gains no level"),
		UCataclysmExperience::Grant(999999999, Level, Progress), 0);
	TestEqual(TEXT("still at the maximum"), Level, UCataclysmExperience::MaxLevel);
	TestEqual(TEXT("and still banks nothing"), Progress, (int64)0);

	TestTrue(TEXT("the maximum level is recognised as such"),
		UCataclysmExperience::IsMaxLevel(UCataclysmExperience::MaxLevel));
	TestTrue(TEXT("and so is anything above it"),
		UCataclysmExperience::IsMaxLevel(UCataclysmExperience::MaxLevel + 1));
	TestFalse(TEXT("but level 99 is not"),
		UCataclysmExperience::IsMaxLevel(UCataclysmExperience::MaxLevel - 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceIgnoresNothingAndLessThanNothing,
	"Cataclysm.Experience.GrantingZeroOrLessChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceIgnoresNothingAndLessThanNothing::RunTest(const FString&)
{
	// A kill that is worth nothing is a real case: Enemy Score is not yet
	// ported, so a caller may well have zero to hand over. It must not be able
	// to take a level away.
	int32 Level = 40;
	int64 Progress = 12345;

	TestEqual(TEXT("granting nothing gains nothing"),
		UCataclysmExperience::Grant(0, Level, Progress), 0);
	TestEqual(TEXT("the level is untouched"), Level, 40);
	TestEqual(TEXT("and so is the progress"), Progress, (int64)12345);

	TestEqual(TEXT("granting a negative gains nothing"),
		UCataclysmExperience::Grant(-5000000, Level, Progress), 0);
	TestEqual(TEXT("the level is still untouched"), Level, 40);
	TestEqual(TEXT("and cannot be spent backwards"), Progress, (int64)12345);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceRepairsANonsenseSaveRecord,
	"Cataclysm.Experience.ANonsenseLevelFromASaveIsClampedRatherThanLooped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceRepairsANonsenseSaveRecord::RunTest(const FString&)
{
	// THIS IS REACHED FROM A SAVE RECORD, which holds whatever was last written
	// to it including whatever a future migration leaves behind. Level 0 would
	// make the loop charge the cost of level 1, which is nothing, and gain a
	// level every iteration for ever.
	int32 Level = 0;
	int64 Progress = 0;
	UCataclysmExperience::Grant(UCataclysmExperience::CostOfLevel(2), Level, Progress);
	TestEqual(TEXT("a level of zero is treated as level 1, then earns level 2"),
		Level, 2);

	Level = -70;
	Progress = 0;
	UCataclysmExperience::Grant(1, Level, Progress);
	TestEqual(TEXT("a negative level is clamped to 1"), Level, 1);

	Level = 4000;
	Progress = 0;
	UCataclysmExperience::Grant(1, Level, Progress);
	TestEqual(TEXT("a level above the maximum is clamped to it"),
		Level, UCataclysmExperience::MaxLevel);

	Level = 10;
	Progress = -900;
	UCataclysmExperience::Grant(1, Level, Progress);
	TestEqual(TEXT("negative progress is treated as none"), Progress, (int64)1);

	return true;
}

// ---------------------------------------------------------------------------
// Reading a level back out of a total
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceLevelForTotalRoundTrips,
	"Cataclysm.Experience.TheLevelForATotalAgreesWithWhatItCostToReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceLevelForTotalRoundTrips::RunTest(const FString&)
{
	TestEqual(TEXT("nothing earned is level 1"),
		UCataclysmExperience::LevelForTotal(0), 1);
	TestEqual(TEXT("a negative total is level 1"),
		UCataclysmExperience::LevelForTotal(-1), 1);

	// EVERY LEVEL, BOTH SIDES OF ITS BOUNDARY. Exactly the cost of a level is
	// that level; one short of it is the level below. An off-by-one here would
	// misreport a character's level on every screen that showed it.
	for (int32 Level = 2; Level <= UCataclysmExperience::MaxLevel; ++Level)
	{
		const int64 Exactly = UCataclysmExperience::TotalToReach(Level);
		TestEqual(FString::Printf(TEXT("exactly the cost of level %d is level %d"),
				 Level, Level),
			UCataclysmExperience::LevelForTotal(Exactly), Level);
		TestEqual(FString::Printf(TEXT("one short of level %d is level %d"),
				 Level, Level - 1),
			UCataclysmExperience::LevelForTotal(Exactly - 1), Level - 1);
	}

	TestEqual(TEXT("more than the whole climb is still the maximum"),
		UCataclysmExperience::LevelForTotal(
			UCataclysmExperience::TotalToReach(UCataclysmExperience::MaxLevel) * 10),
		UCataclysmExperience::MaxLevel);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
