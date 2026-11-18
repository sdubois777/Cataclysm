// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ACataclysmCapitalGameMode;
class ACataclysmDungeonGameMode;
class UCataclysmEmpireRun;

/**
 * Walking a dungeon from the empire screens: what the Walk button says, which dungeon it stands for, and why a press
 * is refused. Issue #2715.
 *
 * WHY IT EXISTS. Until this, the only way into an empire dungeon was typing `Cataclysm.EnterDungeon <id>` in a dungeon
 * level. The design says of the Cataclysm boss dungeon, `docs/Cataclysm_GDD_v2.md` under "How many Cataclysms must be
 * finished to unlock the Cataclysm dungeon": "The empire map shows a marker for it that the player enters from." The
 * city screen (`UCataclysmCityScreenWidget`) puts a Walk button under each dungeon line, and the empire map
 * (`UCataclysmEmpireMapWidget`) puts one under the boss dungeon's box; both call `Walk` here, so the two cannot refuse
 * differently.
 *
 * WHAT A PRESS DOES IN A DUNGEON LEVEL IS `ACataclysmDungeonGameMode::EnterEmpireDungeon`, and nothing else: it binds
 * the dungeon, refills the potions, stands the player on floor 1, stops that dungeon's timer and charges floor 1's
 * time. **NOT WHILE A WALK IS UNDER WAY**, since issue #2733 slice 3: "There is no leaving a dungeon early", and every
 * press mid-walk is refused. Until then a press under another dungeon left the walk first and ran the days it owed.
 *
 * A PLAIN STRUCT AND NOT A BLUEPRINT LIBRARY. Nothing in a Widget Blueprint calls these: the two screens build their
 * Walk buttons in C++, as they build every other button. The words are here rather than in a layout class so that the
 * two screens share one copy of them.
 *
 * AND IN THE CAPITAL, A PRESS TRAVELS, since issue #2733 slice 3: `ACataclysmCapitalGameMode::TravelToWalk` hands the
 * character over to `L_Dungeon`, which enters the dungeon once the character is loaded there. Anywhere else -- the
 * front end, the sandbox -- the press is refused and the screen says why.
 */
struct CATACLYSM_API FCataclysmWalkFromTheScreen
{
	/** What every Walk button says: "Walk". */
	static FString ButtonText();

	/**
	 * The name a dungeon's Walk button carries: "WalkDungeon12". No city's button name ("City7") and no other button
	 * on either screen can be one, so a screen can tell a Walk press from any other by the name alone.
	 */
	static FName ButtonNameFor(int32 DungeonId);

	/** The dungeon a Walk button's name stands for, or `INDEX_NONE` when the name is not a Walk button's. */
	static int32 DungeonForButton(FName Value);

	/**
	 * The refusal for a level whose game mode cannot enter a dungeon. The words `Cataclysm.EnterDungeon` prints in the
	 * same case (`CataclysmPlayerCharacter.cpp`), so the console and the screen say the same thing.
	 */
	static FString NotADungeonLevelText();

	/**
	 * The refusal for any way of leaving a walk before it ends: a Walk press mid-walk, `Cataclysm.LeaveDungeon`, and
	 * `Cataclysm.EnterDungeon` mid-walk. "You are walking dungeon 12. There is no leaving a dungeon early: clear its last
	 * floor, or die in it." One copy, so every door says the same. Issue #2733 slice 3, ruled 2026-11-17 (Q4).
	 */
	static FString NoLeavingEarlyText(int32 WalkingDungeonId);

	/**
	 * Why pressing Walk on this dungeon would be refused, or empty when it would be entered or travelled to. Changes
	 * nothing.
	 *
	 * IN THIS ORDER: no run ("No run has been started."); in a level with neither a dungeon game mode nor a capital,
	 * `NotADungeonLevelText`; a walk under way, whichever dungeon was pressed (`NoLeavingEarlyText`); the dungeon not
	 * standing on the map ("No dungeon 12 is standing on the map.", the console's words).
	 *
	 * A WALK UNDER WAY REFUSES EVERY PRESS since issue #2733 slice 3. Until then only the dungeon being walked was
	 * refused ("You are walking dungeon 12 now."), and a press under another left the walk and ran its days.
	 *
	 * @param Run      the run the screen shows
	 * @param Mode     the dungeon game mode of the level the screen is in, or null when it has none
	 * @param Capital  the capital's game mode when the screen is in the capital, or null
	 */
	static FString RefusalFor(const UCataclysmEmpireRun* Run, const ACataclysmDungeonGameMode* Mode, int32 DungeonId,
							  const ACataclysmCapitalGameMode* Capital = nullptr);

	/**
	 * Press Walk: `RefusalFor` first; then, in a dungeon level, `Mode->EnterEmpireDungeon`, and in the capital,
	 * `Capital->TravelToWalk`.
	 *
	 * @return the refusal to show, or empty when the dungeon was entered or the trip to it began. When
	 *         `EnterEmpireDungeon` itself refuses -- the game mode has no run of its own -- "Dungeon 12 could not be
	 *         entered.", the console's words; when the trip could not begin, the capital's words for why. Never an
	 *         empty string for a press that did nothing.
	 */
	static FString Walk(const UCataclysmEmpireRun* Run, ACataclysmDungeonGameMode* Mode, int32 DungeonId,
						ACataclysmCapitalGameMode* Capital = nullptr);
};
