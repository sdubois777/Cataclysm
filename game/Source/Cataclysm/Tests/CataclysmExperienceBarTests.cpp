// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmExperience.h"
#include "Interface/CataclysmExperienceBar.h"

/**
 * The heads-up display's level and experience bar, as plain numbers: what it is filled to and what it says. Issue
 * #2744. Registered under `Cataclysm.ExperienceBar.`, which no other group's name begins with. The bar reaching the
 * screen cannot be watched by a test (`-nullrhi`); a kill moving it is `Cataclysm.CharacterLevel.
 * AKillThatCrossesALevelRaisesTheNumberTheBarAndMaximumHealth`.
 *
 * EVERY COST IS ASKED OF `UCataclysmExperience`, not written here, so a retuned curve moves these with it. The words
 * test's literals are the words themselves; the one that matters most is "99.9%", not "100.0%", one short of a level.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceBarFillTest,
	"Cataclysm.ExperienceBar.TheFillIsTheExperienceIntoTheLevelOverWhatTheNextLevelCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceBarFillTest::RunTest(const FString&)
{
	using Bar = UCataclysmExperienceBar;
	const int64 ToSecond = UCataclysmExperience::CostOfLevel(2);
	const int64 ToEighth = UCataclysmExperience::CostOfLevel(8);

	AddInfo(FString::Printf(TEXT("EXPERIENCE BAR FILL: level 1 at 0 %.6f, half %.6f, one short %.6f; level 7 at a "
								 "quarter %.6f; level 100 %.6f; level 0 %.6f; past the cost %.6f; below nought %.6f"),
							Bar::FractionFor(1, 0), Bar::FractionFor(1, ToSecond / 2), Bar::FractionFor(1, ToSecond - 1),
							Bar::FractionFor(7, ToEighth / 4), Bar::FractionFor(UCataclysmExperience::MaxLevel, 0),
							Bar::FractionFor(0, ToSecond / 2), Bar::FractionFor(1, ToSecond * 3),
							Bar::FractionFor(1, -5)));

	TestTrue(TEXT("set-up: level 2 costs something"), ToSecond > 0);
	TestEqual(TEXT("a new character's bar is empty"), Bar::FractionFor(1, 0), 0.0f);
	TestEqual(TEXT("half of level 2's cost fills half the bar"), Bar::FractionFor(1, ToSecond / 2),
			  static_cast<float>(static_cast<double>(ToSecond / 2) / static_cast<double>(ToSecond)));
	const float OneShort = Bar::FractionFor(1, ToSecond - 1);
	TestTrue(FString::Printf(TEXT("one short of level 2 is nearly full and not full: %.7f"), OneShort),
			 OneShort > 0.99f && OneShort < 1.0f);
	TestEqual(TEXT("a level part-way, level 7 a quarter of the way to 8, against level 8's cost"),
			  Bar::FractionFor(7, ToEighth / 4),
			  static_cast<float>(static_cast<double>(ToEighth / 4) / static_cast<double>(ToEighth)));
	TestEqual(TEXT("at the maximum level the bar is full"), Bar::FractionFor(UCataclysmExperience::MaxLevel, 0), 1.0f);
	TestEqual(TEXT("and above it, full"), Bar::FractionFor(UCataclysmExperience::MaxLevel + 5, 0), 1.0f);
	TestEqual(TEXT("a level below 1 is read as level 1"), Bar::FractionFor(0, ToSecond / 2),
			  Bar::FractionFor(1, ToSecond / 2));
	TestEqual(TEXT("more than the cost is held at full"), Bar::FractionFor(1, ToSecond * 3), 1.0f);
	TestEqual(TEXT("less than nothing is held at empty"), Bar::FractionFor(1, -5), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExperienceBarWordsTest,
	"Cataclysm.ExperienceBar.TheWordsSayTheLevelAndTheShareEarnedRoundedDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExperienceBarWordsTest::RunTest(const FString&)
{
	using Bar = UCataclysmExperienceBar;
	const int64 ToSecond = UCataclysmExperience::CostOfLevel(2);
	const int64 ToEighth = UCataclysmExperience::CostOfLevel(8);

	// 45.2% OF LEVEL 8'S COST, ROUNDED UP TO A WHOLE POINT: at least 452 thousandths of it and, since the cost is far
	// above a thousand, short of 453.
	const int64 FortyFivePointTwo = (452 * ToEighth + 999) / 1000;

	AddInfo(FString::Printf(TEXT("EXPERIENCE BAR WORDS: '%s' | '%s' | '%s' | '%s' | '%s' | '%s'"), *Bar::TextFor(1, 0),
							*Bar::TextFor(1, ToSecond / 2), *Bar::TextFor(1, ToSecond - 1),
							*Bar::TextFor(7, FortyFivePointTwo), *Bar::TextFor(UCataclysmExperience::MaxLevel, 0),
							*Bar::TextFor(UCataclysmExperience::MaxLevel + 20, 0)));

	TestEqual(TEXT("a new character"), Bar::TextFor(1, 0), FString(TEXT("Level 1: 0.0%")));
	TestEqual(TEXT("half way to level 2"), Bar::TextFor(1, ToSecond / 2), FString(TEXT("Level 1: 50.0%")));
	TestEqual(TEXT("ONE SHORT OF LEVEL 2 READS 99.9%, NOT 100.0%: rounded down, so it never claims a level not reached"),
			  Bar::TextFor(1, ToSecond - 1), FString(TEXT("Level 1: 99.9%")));
	TestEqual(TEXT("a level part-way, in tenths"), Bar::TextFor(7, FortyFivePointTwo), FString(TEXT("Level 7: 45.2%")));
	TestEqual(TEXT("the maximum level says so"), Bar::TextFor(UCataclysmExperience::MaxLevel, 0),
			  FString(TEXT("Level 100: maximum")));
	TestEqual(TEXT("a level above the maximum is the maximum"), Bar::TextFor(UCataclysmExperience::MaxLevel + 20, 0),
			  FString(TEXT("Level 100: maximum")));
	TestEqual(TEXT("less than nothing reads nothing"), Bar::TextFor(1, -5), FString(TEXT("Level 1: 0.0%")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
