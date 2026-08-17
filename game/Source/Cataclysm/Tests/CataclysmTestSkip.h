// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cataclysm.h"
#include "Misc/AutomationTest.h"

/**
 * Saying, once and in a findable way, that a test skipped part of its work.
 *
 * WHY THIS EXISTS. Fifteen automation tests take a shorter path when the Paragon
 * art packs are absent: they check what can be checked without a skeletal mesh
 * and return early. Each said so by calling `AddInfo` with its own wording, and
 * `CLAUDE.md` told the reader to confirm which path a test took by grepping
 * `game/Saved/Logs/Cataclysm.log` for that wording. That has two faults and
 * neither is the one issue #467 suspected.
 *
 * FIFTEEN DIFFERENT WORDINGS IS NOT A THING YOU CAN GREP FOR. "Paragon Rampage
 * pack is not installed", "The Paragon art is absent", "No skeleton with a
 * weapon_r bone", "SKIPPED: the Paragon Grux pack is not present" -- a reader
 * has to know all of them, and a new one added tomorrow is invisible to anyone
 * who learned the old list. Every message now carries `CATACLYSM_SKIPPED_HALF`,
 * which is one token and cannot be mistaken for prose.
 *
 * AND THE RUN'S SUMMARY NEVER MENTIONED IT. `python tools/unreal_build.py tests`
 * printed "22 tests performed, 22 succeeded, 0 failed" whether or not half of
 * them checked anything. It now counts these lines and names the tests, so a
 * skipped half is reported rather than waiting to be searched for.
 *
 * WHAT ISSUE #467 SUSPECTED, AND WHY IT WAS WRONG. It proposed that the
 * automation controller drops Info events for a test that also produced Warning
 * events. A probe run on 2026-08-17 emitted an Info beside an `AddWarning` and
 * beside a `UE_LOG` warning, and both Infos reached the log intact. So Info is
 * not filtered, and the missing message seen on 2026-08-09 has some other
 * explanation that could not be reproduced: with the Paragon packs present on
 * this machine, none of these skip paths runs at all.
 *
 * TWO ROUTES, ON PURPOSE. `AddInfo` puts the line inside the automation
 * controller's event block for this test, which is where a reader of the block
 * looks. `UE_LOG` puts it in the log file directly, under the `LogCataclysm`
 * category, which does not depend on how the automation framework formats or
 * filters events. The second route is what made the first fault survivable had
 * issue #467's theory been right, and it costs one line.
 */
namespace CataclysmTestSkip
{
	/**
	 * The one token every skipped half carries.
	 *
	 * DELIBERATELY NOT A WORD OR A PHRASE. Prose gets reworded; this is a name.
	 * `tools/unreal_build.py` searches for exactly this, and
	 * `tools/tests/test_unreal_build.py` fails if the two ever disagree.
	 */
	inline const TCHAR* Marker = TEXT("CATACLYSM_SKIPPED_HALF");

	/**
	 * Report that this test could not check part of what it is named for.
	 *
	 * @param Test    the test reporting it, so the line names itself and a
	 *                reader of the log does not have to work out which block a
	 *                bare message fell in
	 * @param Reason  what could not be checked and why, in the test's own words.
	 *                Say what was NOT checked rather than only what was missing:
	 *                "the launch height is not checked; the arc above is" tells a
	 *                reader what the run is worth, and "no skeleton" does not.
	 */
	inline void ReportSkippedHalf(FAutomationTestBase& Test, const FString& Reason)
	{
		const FString Line = FString::Printf(TEXT("%s %s -- %s"),
			Marker, *Test.GetBeautifiedTestName(), *Reason);

		Test.AddInfo(Line);
		UE_LOG(LogCataclysm, Display, TEXT("%s"), *Line);
	}
}
