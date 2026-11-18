// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCataclysmEmpireRun;
struct FCataclysmDayReport;

/**
 * THE PLAY LOOP'S LOG: one plain line at each step of the main loop, so that one press of Play leaves the whole loop in
 * the log, in order. Issue #2733 slice 4. The project owner's checklist, `docs/Play_Loop_Checklist.md`, quotes these
 * lines; the Output Log's filter "PLAY LOOP" shows them and nothing else.
 *
 *   PLAY LOOP: the front end is open: 1 saved character to continue, or New character to make one
 *   PLAY LOOP: new character 'Ash' (Greatsword, War) begun: opening the capital
 *   PLAY LOOP: in the capital on day 0, tier 1: 12 dungeons standing, 0 cities fallen; level 1, 0 experience into it
 *   PLAY LOOP: standing: dungeon 3, the Horde dungeon on Outpost (-3,0): 12 floors, 9 days until it bites, ...
 *   PLAY LOOP: Walk pressed on dungeon 3, the Horde dungeon on Outpost (-3,0): travelling to the dungeon level
 *   PLAY LOOP: entered dungeon 3, floor 1 of 12; the walk owes 1 day
 *   PLAY LOOP: dungeon 3: down to floor 2 of 12; the walk owes 2 days
 *   PLAY LOOP: cleared dungeon 3 on day 12: +1.0 empire points; level 4: back to the capital
 *   PLAY LOOP: in the capital on day 12, ...; while away, days 0 to 12: dungeons resolved: 5, ...
 *
 * EACH LINE IS BUILT HERE FROM WHAT IT IS GIVEN, AND SAID BY `Say`, so a test can read every line's words without a
 * world, and the place that says one passes only figures the game already holds. Nothing here changes any state.
 *
 * DISPLAY, NOT LOG: a Display line reaches the Output Log and `Saved/Logs` at the editor's default verbosity.
 */
struct CATACLYSM_API FCataclysmPlayLoopLog
{
	/** What every line begins with, which the Output Log's filter finds. */
	static const TCHAR* const Prefix;

	/** Logs `Prefix` and `Line` at Display on LogCataclysm. */
	static void Say(const FString& Line);

	/** The front end, opened: how many saved characters it lists. */
	static FString FrontEndLine(int32 SavedCharacters);

	/** Begin pressed on the new character screen: its name, weapon type and damage type. */
	static FString NewCharacterLine(const FString& Name, FName WeaponType, FName DamageType);

	/** A saved character's row pressed: its name, and whether it resumes a walk in the dungeon level. */
	static FString ContinueLine(const FString& Name, bool bResumesAWalk);

	/**
	 * A dungeon standing on the run, as the line names it: "dungeon 3, the Horde dungeon on Outpost (-3,0): 12 floors,
	 * 9 days until it bites, modifiers Brand of the Aggressor, Hard Mode". The floors and the days are the city
	 * screen's own words; the kind and the modifiers are what no screen shows today. Empty for a dungeon not on the run.
	 */
	static FString DungeonLine(const UCataclysmEmpireRun* Run, int32 DungeonId);

	/** One "standing: <DungeonLine>" per dungeon on the run, in identifier order. */
	static TArray<FString> StandingLines(const UCataclysmEmpireRun* Run);

	/**
	 * THE NAMES OF THE DUNGEONS STANDING, by identifier, read before a walk's days run: what `AwayAccount` names a
	 * dungeon those days resolved by, because a resolved dungeon is no longer on the run to be asked.
	 */
	static TMap<int32, FString> NamesStanding(const UCataclysmEmpireRun* Run);

	/**
	 * WHAT THE DAYS AWAY DID, from the days' own reports and not from the summary the map reads
	 * (`UCataclysmEmpireRun::NoteDaysAway`), so the log is a second account to check the map against: "while away,
	 * days 0 to 12: dungeons resolved: 5 (the dungeon on Outpost (-2,-1)); cities hit: Outpost (-2,-1) lost 750 defence
	 * and 1200 people; cities fallen: none; 1 surge brought 8 dungeons". "No day passed" when none did.
	 *
	 * @param NamesBefore `NamesStanding` read before the days ran; a dungeon the days both brought and resolved is
	 *                    named by its identifier alone.
	 */
	static FString AwayAccount(int32 FromDay, int32 ToDay, const TArray<FCataclysmDayReport>& Days,
							   const TMap<int32, FString>& NamesBefore, const UCataclysmEmpireRun* Run);

	/** The capital, its map opened: the day, the board and the character, and the days away when a walk just ended. */
	static FString CapitalLine(const UCataclysmEmpireRun* Run, int32 Level, int64 ExperienceIntoLevel,
							   const FString& Away);

	/** Walk pressed in the capital: the dungeon, named as `DungeonLine` names it. */
	static FString WalkLine(const UCataclysmEmpireRun* Run, int32 DungeonId);

	/** A dungeon entered: its floor 1 of all of them, and the days the walk owes. */
	static FString EnteredLine(int32 DungeonId, int32 Floors, int32 DaysOwed);

	/** The stairs taken to the next floor. */
	static FString FloorLine(int32 DungeonId, int32 Floor, int32 Floors, int32 DaysOwed);

	/** The last floor's stairs, which clear the dungeon. */
	static FString ClearLine(int32 DungeonId, int32 DayOfTheClear, double PointsAwarded, int32 Level);

	/** A death that ended a walk. */
	static FString DeathLine(int32 DungeonId, int32 Floor, int32 Floors, int32 DaysTheDeathCost);
};
