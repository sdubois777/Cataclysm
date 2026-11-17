// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A percentage roll drawn from 0 up to but not 100. Issue #2201.
 *
 * WHY THIS EXISTS. A chance in this game is a number from 0 to 100, and a roll
 * succeeds when it is below the chance. The roll used to be
 * `FMath::FRandRange(0.0f, 100.0f)`, which is `0 + 100 * FMath::FRand()`, and in
 * Unreal Engine 5.8 `FRand()` is `(Rand() & RandMax) / (float)RandMax`: it
 * returns exactly 1.0 as one of its values (`GenericPlatformMath.h`, "between 0
 * and 1, inclusive"). So the roll could be exactly 100, and a chance of 100
 * failed on that one roll. On Windows `RAND_MAX` is `0x7fff`, so that is one roll
 * in 32,768. That rate is arithmetic, not a measurement.
 *
 * WHAT THIS DOES ABOUT IT. `Draw` makes the same single call to the engine's
 * generator and hands the fraction to `FromFraction`, which gives every fraction
 * below 1 exactly what `FRandRange(0, 100)` gave it, and gives the fraction 1 the
 * largest float below 100. So, with every comparison left as it was:
 *
 *   - `Roll < Chance` succeeds on every roll at a chance of 100, and never at a
 *     chance of 0, because no roll is below 0;
 *   - `Roll >= Chance`, the form that skips, never skips at a chance of 100 and
 *     always skips at a chance of 0;
 *   - a chance below 100 is rolled exactly as before on every draw but the one
 *     of exactly 1.0. That draw failed every chance before; now it succeeds
 *     only at a chance above 99.99999, which no designed chance is.
 *
 * NOT A GUARD AND NOT A NEW COMPARISON. The rolls that already compare a chance
 * of 100 before rolling (`Chance >= 100 ||`) keep `FRandRange` and their guard.
 * This is for the rolls that compare strictly and had no guard. A roll pinned by
 * a console variable for a test does not come through here, so a pin of 100
 * still means "never" wherever it did.
 *
 * WHY HERE. Every roll that uses it is in the `Cataclysm` module, and this is a
 * header with nothing in it but arithmetic on the engine's own generator, so no
 * module gains a dependency and no source file has to be built for it.
 */
namespace CataclysmPercentDraw
{
	/**
	 * The largest float below 100, which the fraction 1.0 draws. 99.99999 rounds
	 * to it; the float above it is 100 itself.
	 */
	constexpr float HighestDraw = 99.99999f;
	static_assert(HighestDraw < 100.0f, "A draw must stay below 100, or a chance of 100 can fail its roll.");
	static_assert(HighestDraw > 99.9999f, "The fraction 1.0 must draw the top of the range, not somewhere below it.");

	/**
	 * The percentage a fraction of the engine's generator draws.
	 *
	 * SEPARATE FROM `Draw` so that a test can hand it exactly 1.0, the case that
	 * cannot be drawn on purpose.
	 *
	 * @param Fraction  what `FMath::FRand()` returned, from 0 to 1 inclusive.
	 * @return `Fraction * 100`, which is what `FRandRange(0, 100)` returned, but
	 *         never more than `HighestDraw`.
	 */
	inline float FromFraction(float Fraction)
	{
		return FMath::Min(Fraction * 100.0f, HighestDraw);
	}

	/** A percentage roll, from 0 up to but not 100. One call to `FMath::FRand()`. */
	inline float Draw()
	{
		return FromFraction(FMath::FRand());
	}
}
