// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCataclysmEmpireRun;

/**
 * BRIBERY, the player's action on a dungeon's timer: "Once per Surge, spend a large Gold fee to reset a single dungeon
 * resolution timer to its maximum value." Issue #2741 slice 9, the coordinating session's ruling of 2026-11-21 and V12
 * to V14 of 2026-11-22.
 *
 * WHAT IT DOES. Pick a dungeon, see the fee, pay; its timer returns to the value it started at. Once per surge for the
 * whole run (V12). Any dungeon whose timer resolves but the Cataclysm boss's, a Scout Parties bonus dungeon included
 * (V13). 20,000 tier 1 gold (`UCataclysmEmpireTree::BribeFeeAtTierOne`), scaled to the run's tier as a gold pile is,
 * paid in full from the balance with no credit, as Economic Bailout's fee is (V9, V13). Every refusal says why.
 *
 * WHERE. A Bribe button beside each dungeon's Walk button on the city screen, showing the fee, and the console command
 * `Cataclysm.BribeDungeon <dungeon id>` (V14). The run answers the refusals that are its own
 * (`UCataclysmEmpireRun::RefusalForBribing`) and resets the timer (`BribeDungeon`); the node held and the gold are this
 * module's, so they are asked here.
 */
struct CATACLYSM_API FCataclysmDungeonBribe
{
	/** The fee at this run's difficulty tier: 20,000 tier 1 gold scaled as a gold pile is (R10). 0 with no run. */
	static int64 FeeIn(const UCataclysmEmpireRun* Run);

	/**
	 * Why `Bribe` would refuse, or empty when it would not: Bribery not held, any refusal of the run's, no gold balance,
	 * or a balance short of the fee ("Costs 20,000 gold; 5,000 more needed.").
	 */
	static FString RefusalFor(const UObject* WorldContext, const UCataclysmEmpireRun* Run, int32 DungeonId);

	/**
	 * Pays the fee and resets the timer, or does nothing and answers why. The gold is taken before the run resets the
	 * timer and given back if the run refuses, as a city upgrade's price is. Raises the saves a gold spend raises, and
	 * says what it did in the play-loop log.
	 *
	 * @return empty when the dungeon was bribed, else the refusal
	 */
	static FString Bribe(const UObject* WorldContext, UCataclysmEmpireRun* Run, int32 DungeonId);

	/** The Bribe button's words for a dungeon, with the fee: "Bribe: reset its timer for 20,000 gold". */
	static FString ButtonText(const UCataclysmEmpireRun* Run);

	/** The name a Bribe button carries for a dungeon, and the dungeon a pressed button's name stands for. */
	static FName ButtonNameFor(int32 DungeonId);
	static int32 DungeonForButton(FName Value);
};
