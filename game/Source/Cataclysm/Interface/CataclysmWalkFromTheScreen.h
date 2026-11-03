// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
 * WHAT A PRESS DOES IS `ACataclysmDungeonGameMode::EnterEmpireDungeon`, and nothing else: it binds the dungeon, refills
 * the potions, stands the player on floor 1, stops that dungeon's timer and charges floor 1's time. A walk already
 * under way is left first, which runs the days it owes -- the same as typing the command mid-walk.
 *
 * A PLAIN STRUCT AND NOT A BLUEPRINT LIBRARY. Nothing in a Widget Blueprint calls these: the two screens build their
 * Walk buttons in C++, as they build every other button. The words are here rather than in a layout class so that the
 * two screens share one copy of them.
 *
 * NO TRAVEL. A press works only where `EnterEmpireDungeon` can be called, which is a level whose game mode is
 * `ACataclysmDungeonGameMode`. Anywhere else -- the front end, the sandbox -- the press is refused and the screen says
 * why; getting from somewhere else into a dungeon level is issue #48, the capital hub, and is not invented here.
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
	 * Why pressing Walk on this dungeon would be refused, or empty when it would be entered. Changes nothing.
	 *
	 * IN THIS ORDER: no run ("No run has been started."); no dungeon game mode (`NotADungeonLevelText`); the dungeon
	 * not standing on the map ("No dungeon 12 is standing on the map.", the console's words); the player already
	 * walking that very dungeon ("You are walking dungeon 12 now."). The last is this screen's own refusal, which the
	 * console does not make: entering the dungeon being walked would leave it, run the days the walk owes, and start it
	 * again on floor 1, which no button should do to a player in one click.
	 *
	 * @param Run   the run the screen shows
	 * @param Mode  the dungeon game mode of the level the screen is in, or null when it has none
	 */
	static FString RefusalFor(const UCataclysmEmpireRun* Run, const ACataclysmDungeonGameMode* Mode, int32 DungeonId);

	/**
	 * Press Walk: `RefusalFor` first, and then `Mode->EnterEmpireDungeon`.
	 *
	 * @return the refusal to show, or empty when the dungeon was entered. When `EnterEmpireDungeon` itself refuses --
	 *         the game mode has no run of its own, or the days a walk being left ran took the dungeon off the map --
	 *         "Dungeon 12 could not be entered.", the console's words, and never an empty string: a press that did
	 *         nothing must say so.
	 */
	static FString Walk(const UCataclysmEmpireRun* Run, ACataclysmDungeonGameMode* Mode, int32 DungeonId);
};
