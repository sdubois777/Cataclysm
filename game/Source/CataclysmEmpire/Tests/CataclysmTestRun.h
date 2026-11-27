// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"

/**
 * How a test begins an empire run so that the Cataclysms it faces are named by the test, not drawn from whatever the
 * game counts as built that day. Issue #2928.
 *
 * WHY. A run's Cataclysms are drawn once, when it begins (`UCataclysmEmpireRun::BeginForCharacter`, through
 * `UCataclysmRoster::ActiveFor`), from a shuffle that puts the built Cataclysms first (`UCataclysmRoster::Built`).
 * When War left the built list on 2026-11-26, nine tests whose fixed seeds had faced War began facing Demonic or Death
 * and failed on things unrelated to what they measure; the survey for #2928 found at least 531 tests exposed to the
 * same draw. Nothing after the run begins reads the built list, so holding a named list while the run begins fixes the
 * draw for good.
 *
 * TWO WAYS, AND A TEST CHOOSES ONE BY NAME:
 * - `FBuiltAsWritten`: the built list on 2026-11-26, Demonic and Death (`BuiltOn20261126`), so a seed draws what it
 *   drew when the test was written. Every shared helper and direct `Begin` in the tests holds one, declared just before
 *   the call. It does not replace a list an outer test already holds (`FCataclysmBuiltCataclysmsForTests::IsHeld`), so
 *   the nine tests that hold the list they were written against, with War, keep it.
 * - `BeginFacing`: exactly the named Cataclysms, in the named order. For a test that needs one Cataclysm in particular.
 *
 * A NEW BUILT CATACLYSM DOES NOT MOVE ANY OF THESE TESTS. A test that should follow the game's own list holds nothing
 * and says so in its comment, as `CataclysmCataclysmDrawTests.cpp` and `CataclysmRosterTests.cpp` do, since the draw is
 * their subject.
 */
namespace CataclysmTestRun
{
	/** The Cataclysms the game counted as built on 2026-11-26, the day these tests were made to name them. */
	inline const TArray<ECataclysmType>& BuiltOn20261126()
	{
		static const TArray<ECataclysmType> Built = {ECataclysmType::Demonic, ECataclysmType::Death};
		return Built;
	}

	/**
	 * Holds `BuiltOn20261126` as the built list until it goes out of scope, unless a test already holds one, which it
	 * then keeps. Declare it on the line before a run begins.
	 */
	struct FBuiltAsWritten
	{
		FBuiltAsWritten()
			: Hold(FCataclysmBuiltCataclysmsForTests::IsHeld() ? UCataclysmRoster::Built() : BuiltOn20261126())
		{
		}

	private:
		FCataclysmBuiltCataclysmsForTests Hold;
	};

	/**
	 * Begins `Run` facing exactly `Facing`, in that order, whatever the built list. The tier is the number named, so the
	 * draw takes them all; the order is then set as named, since the draw shuffles it.
	 */
	inline void BeginFacing(UCataclysmEmpireRun& Run, const TArray<ECataclysmType>& Facing, int32 Seed,
							ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static, int32 LethalityRung = 0)
	{
		check(Facing.Num() > 0);
		{
			const FCataclysmBuiltCataclysmsForTests OnlyThese(Facing);
			Run.Begin(Seed, Mode, LethalityRung, /*InDifficultyTier=*/Facing.Num());
		}
		Run.ActiveCataclysms = Facing;
	}
}
