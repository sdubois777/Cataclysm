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
 *   PLAY LOOP: level 2 reached, from level 1: 3120 experience into it, 245740 more to level 3
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

	/**
	 * The gold a walk picked up and the balance it left, said at the walk's end after the clear's or the death's line:
	 * "dungeon 3's walk picked up 1,234 gold; 5,678 gold held". Issue #2742, slice 1: what a walk earns, measured, so
	 * slice 2's city upgrade price can be checked against it (the coordinating session's G4 ruling).
	 *
	 * @param Held  the balance after the walk, or below zero when there is none to name -- no character playing, or an
	 *              account record that would not read -- and the line says so.
	 */
	static FString GoldLine(int32 DungeonId, int64 PickedUp, int64 Held);

	/**
	 * `GoldLine` for the character playing in a world: the game instance's `GoldPickedUpThisWalk`, and the balance of
	 * its partition's account. A world with no game instance of this project's picked up nothing and holds no balance.
	 */
	static FString GoldLineIn(const UObject* WorldContext, int32 DungeonId);

	/**
	 * A level gained, from a kill or `Cataclysm.GrantExperience`: the level reached, the level it came from, the
	 * experience into the new level and what the next one still costs. At the maximum level, that it is the maximum.
	 * Said by `ACataclysmPlayerState::GrantExperience`. Issue #2744.
	 */
	static FString LevelGainedLine(int32 From, int32 To, int64 ExperienceIntoLevel);

	/**
	 * A city upgrade bought on the city screen, what it cost and the balance it left: "bought 'Increase max defense
	 * by 20%' on Outpost (-3,0) for 10,000 gold; 2,345 gold held". Issue #2742's second slice: the owner's check that
	 * a purchase took the price it showed.
	 *
	 * @param Held  the balance after the purchase, or below zero when there is none to name, and the line says so.
	 */
	static FString UpgradeLine(const FString& Upgrade, const UCataclysmEmpireRun* Run, int32 CityId, int64 Price,
							   int64 Held);

	/**
	 * A point bought in the empire upgrade tree: "bought a point in Structural Integrity (Architect): 3 of 10; 9.5
	 * empire points banked". Issue #2741 slice 2; said by `UCataclysmGameInstance::BuyEmpireTreePoint`, after the point
	 * is in the tree and paid for, so the figures are the ones after it. The bank is rounded down to a tenth, as the
	 * empire map shows it (`UCataclysmEmpireMapLayout::EmpirePointsText`).
	 */
	static FString TreePointLine(const FString& Node, const FString& Branch, int32 Points, int32 MaxPoints,
								 double Banked);

	/**
	 * One of a centre capstone's options chosen: "chose The Delver for Foundations of the Empire". Issue #2741 slice 2;
	 * said by `UCataclysmGameInstance::ChooseEmpireTreeOption`, after the choice is made.
	 */
	static FString TreeOptionLine(const FString& Option, const FString& Capstone);

	/**
	 * The empire upgrade tree respecced: "respecced the empire upgrade tree: 26 points back, 26.5 points banked; it cost
	 * 5 days, day 12 to day 17". Issue #2741 slice 2; said by `UCataclysmGameInstance::RespecEmpireTree`, after
	 * the points are back and the days have passed.
	 */
	static FString TreeRespecLine(int32 PointsBack, double Banked, int32 Days, int32 DayFrom, int32 DayTo);
};
