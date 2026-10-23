// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmPercentDraw.h"

/**
 * Tests for the percentage roll drawn below 100. Issue #2201.
 *
 * WHAT IS COVERED. `CataclysmPercentDraw::FromFraction` given the two ends of
 * what `FMath::FRand()` returns, 1.0 and 0.0, each compared the way the seven
 * rolls that use it compare; that a fraction below 1 draws what
 * `FRandRange(0, 100)` drew; and that real draws stay from 0 up to but not 100.
 *
 * WHAT CANNOT BE. A real draw of exactly 1.0 cannot be asked for, which is why
 * the mapping is a function of its own and the 1.0 case is handed to it. That
 * each of the seven rolls calls `Draw` is read in the source, not tested here.
 */
namespace CataclysmPercentDrawTest
{
	/**
	 * How evasion, the critical strike roll, the block roll, the cooldown skip and
	 * bleed on a critical strike compare: the roll succeeds below the chance.
	 */
	static bool Succeeds(float Roll, float Chance)
	{
		return Roll < Chance;
	}

	/** How the two contagion rolls compare: the roll skips at or above the chance. */
	static bool Skips(float Roll, float Chance)
	{
		return Roll >= Chance;
	}

	/**
	 * What `FMath::FRandRange(0.0f, 100.0f)` returned for the fraction `FRand()`
	 * gave it, in the engine's own form: `InMin + (InMax - InMin) * FRand()`
	 * (`UnrealMathUtility.h`, Unreal Engine 5.8).
	 */
	static float OldRange(float Fraction)
	{
		return 0.0f + (100.0f - 0.0f) * Fraction;
	}
}

/**
 * The fraction 1.0, which `FRand()` returns one time in 32,768 on Windows, draws
 * below 100, so a chance of 100 succeeds against it and the contagion's skip does
 * not skip. The control first: the old range gives that fraction exactly 100,
 * which fails the same comparison.
 *
 * AND A FRACTION BELOW 1 DRAWS WHAT IT DREW BEFORE, so no chance below 100 is
 * rolled differently except against the one draw that moved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPercentDrawFractionOfOneTest,
	"Cataclysm.PercentDraw.AFractionOfOneDrawsBelowOneHundredSoAChanceOfOneHundredSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPercentDrawFractionOfOneTest::RunTest(const FString&)
{
	using namespace CataclysmPercentDrawTest;

	// THE CONTROL: the fault, in the old form.
	const float Old = OldRange(1.0f);
	TestEqual(TEXT("control: the old range given a fraction of exactly 1 is 100"), Old, 100.0f);
	TestFalse(TEXT("control: and a chance of 100 fails against it"), Succeeds(Old, 100.0f));
	TestTrue(TEXT("control: and a contagion chance of 100 skips on it"), Skips(Old, 100.0f));

	const float Drawn = CataclysmPercentDraw::FromFraction(1.0f);
	TestTrue(*FString::Printf(TEXT("a fraction of exactly 1 draws below 100: %.6f"), Drawn), Drawn < 100.0f);
	TestTrue(*FString::Printf(TEXT("and at the top of the range, above 99.9999: %.6f"), Drawn), Drawn > 99.9999f);
	TestTrue(TEXT("a chance of 100 succeeds against it"), Succeeds(Drawn, 100.0f));
	TestFalse(TEXT("a contagion chance of 100 does not skip on it"), Skips(Drawn, 100.0f));

	// THE FRACTION ONE STEP BELOW 1 ON WINDOWS, 32766 / 32767, and one in the middle.
	const float JustBelowOne = 32766.0f / 32767.0f;
	TestEqual(TEXT("a fraction just below 1 draws what the old range drew"),
		CataclysmPercentDraw::FromFraction(JustBelowOne), OldRange(JustBelowOne));
	TestEqual(TEXT("a fraction of one half draws what the old range drew"),
		CataclysmPercentDraw::FromFraction(0.5f), OldRange(0.5f));
	return true;
}

/**
 * The fraction 0.0 draws 0, and a chance of 0 does not succeed against it, so the
 * other end of the range stays right with the comparisons unchanged. The control
 * first: a chance just above nought does succeed against the same draw, so the
 * comparison is one that can succeed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPercentDrawFractionOfZeroTest,
	"Cataclysm.PercentDraw.AFractionOfZeroDrawsZeroSoAChanceOfZeroNeverSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPercentDrawFractionOfZeroTest::RunTest(const FString&)
{
	using namespace CataclysmPercentDrawTest;

	const float Drawn = CataclysmPercentDraw::FromFraction(0.0f);
	TestTrue(TEXT("control: a chance of 0.001 succeeds against the draw of a fraction of 0"),
		Succeeds(Drawn, 0.001f));

	TestEqual(TEXT("a fraction of 0 draws 0"), Drawn, 0.0f);
	TestFalse(TEXT("a chance of 0 does not succeed against it"), Succeeds(Drawn, 0.0f));
	TestTrue(TEXT("a contagion chance of 0 skips on it"), Skips(Drawn, 0.0f));
	return true;
}

/**
 * Ten thousand real draws all fall from 0 up to but not 100. The control first:
 * the draws spread over more than half the range, so the generator was asked and
 * did not hand back one value.
 *
 * NOT A PROOF THAT 100 CANNOT COME UP. A draw of exactly 1.0 comes about once in
 * 32,768, so ten thousand draws meet it in about one run in four; the first test
 * is the one that hands it over on purpose. This one shows the real path, one
 * call to the engine's generator through the mapping, stays in range.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPercentDrawRealDrawsTest,
	"Cataclysm.PercentDraw.TenThousandRealDrawsAllFallFromZeroUpToButNotOneHundred",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPercentDrawRealDrawsTest::RunTest(const FString&)
{
	int32 BelowZero = 0;
	int32 AtOrAboveOneHundred = 0;
	float Lowest = 100.0f;
	float Highest = 0.0f;
	for (int32 Index = 0; Index < 10000; ++Index)
	{
		const float Roll = CataclysmPercentDraw::Draw();
		BelowZero += Roll < 0.0f ? 1 : 0;
		AtOrAboveOneHundred += Roll >= 100.0f ? 1 : 0;
		Lowest = FMath::Min(Lowest, Roll);
		Highest = FMath::Max(Highest, Roll);
	}

	TestTrue(*FString::Printf(TEXT("control: the draws spread from %.3f to %.3f"), Lowest, Highest),
		Highest - Lowest > 50.0f);
	TestEqual(TEXT("no draw is below 0"), BelowZero, 0);
	TestEqual(TEXT("no draw is 100 or more"), AtOrAboveOneHundred, 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
