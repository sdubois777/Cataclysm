// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmSurge.h"
#include "Math/RandomStream.h"
#include "UObject/Object.h"
#include "CataclysmEmpireRun.generated.h"

/**
 * What one day did to the empire.
 *
 * WHY A REPORT AND NOT A SET OF DELEGATES. Everything that reads this is above
 * the empire layer -- a screen, a save, a console command -- and this module
 * must not know about any of them. A caller that advances several days at once
 * gets one of these per day, in order, so nothing is collapsed into a summary
 * that loses which day a city was lost on.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmDayReport
{
	GENERATED_BODY()

	/** Which day this is a report about. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 Day = 0;

	/** Whether a surge fired today, from the clock or from a city falling. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bSurged = false;

	/** The dungeons that arrived today. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Spawned;

	/**
	 * The dungeons whose timers ran out today.
	 *
	 * A DUNGEON THAT RESOLVES DOES NOT GO AWAY. Its timer is set back to full
	 * and it bites the same city again the next time it runs out, which is what
	 * lets an ignored city actually die.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Resolved;

	/**
	 * The cities a Siege took its daily share of today.
	 *
	 * A SIEGE BITES EVERY DAY AND NOT ONLY WHEN ITS TIMER RUNS OUT, which is
	 * what makes it different from every other dungeon. A city appears here once
	 * per Siege standing on it, and the design allows only one, so at most once.
	 *
	 * IT IS NOT THE SAME LIST AS `Resolved`. A Siege that also ran out of time
	 * today bites twice: once here for the day, and once there for the timer.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Besieged;

	/** The cities that fell today. Usually empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Fallen;

	/**
	 * The dungeons that were on a city that fell today and are gone with it.
	 *
	 * THEY ARE ABSORBED RATHER THAN CLEARED. In the design they become part of
	 * the Dungeon City the fallen city turns into, and its floor count is how
	 * many were standing there when it fell. Nothing builds that Dungeon City
	 * yet, so today they are simply removed and the count is recorded here for
	 * whoever does. Issue #41.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Absorbed;

	/**
	 * Whether the Cataclysm can now reach the Pillar.
	 *
	 * TRUE ON EVERY DAY AFTER IT HAPPENS, not only the day it happened, because
	 * it is a state and not an event. The day it began is the first report with
	 * both this and a Sanctuary in `Fallen`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bPillarExposed = false;
};

/**
 * The empire's day loop: the map, the clock and the surge schedule, joined.
 *
 * WHY THIS EXISTS. The three pieces below were each built with a note saying
 * that joining them was somebody else's work, and until this nothing did. The
 * clock reported a dungeon resolving to a caller that did not exist; the map's
 * cities could be bitten by a caller that did not exist; the surge scheduler
 * could roll a wave that nobody put anywhere. Issue #1084.
 *
 * WHAT IT OWNS:
 *
 *   - `UCataclysmEmpireMap` -- 25 cities, the lanes between them, and the loss
 *     condition.
 *   - `UCataclysmDayClock` -- what a day costs and which timers it moves.
 *   - `UCataclysmSurgeScheduler` -- when the next wave comes and where it lands.
 *   - The dungeons standing on the map, which nothing else holds.
 *
 * WHAT ONE DAY DOES, in this order, which is `Simulation.step`'s order in
 * `sim/cataclysm_sim/engine.py`:
 *
 *   1. The day advances.
 *   2. If a surge is due, a wave lands: dungeons are rolled onto exposed cities
 *      and their timers start.
 *   3. Every city repairs whatever its upgrades repair on a schedule. Before
 *      the day's assaults, not after -- see `AdvanceDay` for why.
 *   4. Every timer moves down one day. A dungeon that arrived today loses a day
 *      immediately, which is what the model does.
 *   5. Every Siege takes its daily share of the city it stands on, whether or
 *      not anything resolved. It is the one dungeon that costs something every
 *      day rather than only when its timer runs out.
 *   6. Any timer that reached zero resolves: its dungeon takes a share of its
 *      host city's defence and population, and its own timer is set back to
 *      full.
 *   7. A city whose defence reached zero falls. Every dungeon standing on it is
 *      absorbed, and its fall fires a surge of its own.
 *   8. If a Sanctuary has fallen, the Cataclysm can reach the Pillar.
 *
 * STEPS 3 AND 5 ARE NOT IN `Simulation.step`. The model has neither city
 * upgrades nor sub-type behaviour, so its order is the order of what both have
 * in common rather than of everything here.
 *
 * THE RUN IS DETERMINISTIC FROM ITS SEED. Everything random goes through one
 * `FRandomStream`, so the same seed gives the same run: the same waves on the
 * same cities with the same depths, and therefore the same cities lost on the
 * same days.
 *
 * WHAT IT DELIBERATELY DOES NOT DO, named so the scope is not argued later:
 *
 *   - **The player.** The model's day loop also asks a policy which dungeon to
 *     walk into, spends days at the forge, and kills the player. None of that
 *     belongs here: in the game the player is a character in a world, and what
 *     this owns is the empire's side of the clock. `EnterDungeon` and
 *     `LeaveDungeon` on the clock are how a player is represented, and nothing
 *     calls them yet.
 *   - **Clearing a dungeon.** Nothing here removes a dungeon because the player
 *     beat it. `ClearDungeon` exists for whoever joins a finished dungeon run to
 *     this, and no dungeon run reaches it today.
 *   - **The Dungeon City.** A fallen city should become a retakeable dungeon
 *     with the design's 20, 40 and 60 floor minimums. Issue #41.
 *   - **The Last Stand.** `bPillarExposed` is the condition; the Cataclysm boss
 *     dungeon moving to the Pillar and absorbing everything still standing is
 *     issue #43.
 *   - **Saving any of it.** `UCataclysmRunSave` carries an `int32 Day` that
 *     nothing computes. Joining that record to this is separate work.
 */
UCLASS(BlueprintType)
class CATACLYSMEMPIRE_API UCataclysmEmpireRun : public UObject
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// What a Siege costs its host, every day
	// ----------------------------------------------------------------------

	/**
	 * The share of a city's maximum defence a Siege takes each day it stands.
	 *
	 * ONE PER CENT, WHICH THE DESIGN DOCUMENT STATES: "Deals 1% damage to city
	 * defenses and population per day while active." A hundred days would empty
	 * an untouched city on its own, and a Siege's timer runs out several times
	 * inside that, so the two together are what make it the dungeon a player
	 * cannot postpone.
	 *
	 * NOT PORTED FROM THE SIMULATION, BECAUSE THE SIMULATION DOES NOT MODEL IT.
	 * `sim/cataclysm_sim/engine.py` gives a sub-type no behaviour beyond Cow
	 * Level's doubled walk and Sacrificial's doubled modifiers. The design
	 * document is the only source for this number and
	 * `docs/DECISIONS.md` records that it is.
	 */
	static constexpr float SiegeDefenceBitePerDay = 0.01f;

	/** The same share of the city's maximum population. See above. */
	static constexpr float SiegePopulationBitePerDay = 0.01f;

	/**
	 * How much more damage a Siege deals for each day it has stood.
	 *
	 * THE DESIGN DOCUMENT SAYS "Increases in power by 10 points per day", and
	 * the project owner settled on 2026-09-05 what its power is: "That's in
	 * regards to the damage it does to the city/population". So this is ten
	 * points of city defence and ten points of city population, added to the
	 * day's damage for every day the Siege has already stood.
	 *
	 * POINTS AND NOT A SHARE, WHICH IS WHY SMALL CITIES SUFFER MOST. Ten points
	 * is one per cent of an Outpost's thousand defence and half a per cent of a
	 * Pillar's twenty thousand, so the growth bites hardest where the empire is
	 * thinnest. An unattended Siege empties an Outpost's defence in 14 days, a
	 * Bulwark's in 23, a Sanctuary's in 34 and the Pillar's in 47. Without the
	 * growth every city would take exactly 100 days whatever its size, because
	 * the flat part is a share of that city's own maximum.
	 *
	 * IT COUNTS FROM THE DAY THE SIEGE ARRIVED, so its first day deals the flat
	 * share alone and each later day adds another ten points.
	 * `FCataclysmDungeon::SpawnedDay` is the day it arrived.
	 *
	 * IT IS ADDED TO BOTH THE DEFENCE AND THE POPULATION DAMAGE, which is the
	 * plain reading of a sentence whose subject is the damage it does and whose
	 * previous sentence names both. That reading is recorded in
	 * `docs/DECISIONS.md` as a reading rather than a ruling, because the owner's
	 * answer settled what "power" means and not which of the two it grows.
	 */
	static constexpr float SiegeDamageGrowthPerDay = 10.0f;

	/**
	 * Whether a Siege stands on that city.
	 *
	 * WHAT IT IS FOR. A besieged city cannot buy an upgrade. See
	 * `ECataclysmCityUpgradeResult::CityIsBesieged`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsBesieged(int32 CityId) const;

	/** The 25 cities and their lanes. Null until `Begin` runs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TObjectPtr<UCataclysmEmpireMap> Map;

	/** What a day costs, and every dungeon's timer. Null until `Begin` runs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TObjectPtr<UCataclysmDayClock> Clock;

	/** When the next wave comes. Null until `Begin` runs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TObjectPtr<UCataclysmSurgeScheduler> Surges;

	/**
	 * Every dungeon standing on the map, in the order they arrived.
	 *
	 * THE CLOCK HOLDS A TIMER FOR EACH OF THESE AND NOTHING ELSE ABOUT IT. The
	 * two lists are kept in step by this object and by nothing else; see
	 * `FCataclysmDungeon` for why the split exists.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FCataclysmDungeon> Dungeons;

	/** The one source of chance in the run. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FRandomStream Stream;

	/** What the next dungeon will be numbered. Only ever counts up. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 NextDungeonId = 0;

	/**
	 * Starts a run: an intact empire, a clock at day 0, and a surge due
	 * immediately.
	 *
	 * A SECOND CALL IS A FRESH RUN rather than a continuation, so a caller that
	 * wants to keep a run has to keep this object.
	 *
	 * @param InSeed        what to seed the run's chance with. The same seed
	 *                      gives the same run.
	 * @param Mode          how surges escalate. An open tuning question; see
	 *                      `ECataclysmSurgeMode`.
	 * @param LethalityRung 0 Standard, 1 Hardcore, 2 Heretic.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void Begin(int32 InSeed = 0,
			   ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static,
			   int32 LethalityRung = 0);

	/** Which day the run is on. Counted from 0, before any day has passed. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 Day() const;

	/** Whether the Cataclysm can reach the Pillar. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsLost() const;

	/**
	 * One day passes.
	 *
	 * IT KEEPS GOING AFTER THE RUN IS LOST, and that is deliberate rather than
	 * an oversight: losing the path to the Pillar opens the Last Stand rather
	 * than ending the run, and until that exists the honest behaviour is to let
	 * the empire keep crumbling and let the caller decide what to do about it.
	 * `IsLost` and `FCataclysmDayReport::bPillarExposed` are how a caller
	 * notices.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	FCataclysmDayReport AdvanceDay();

	/**
	 * Several days pass, one at a time.
	 *
	 * @param Days how many. Zero or fewer passes no time at all.
	 * @return one report per day, oldest first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	TArray<FCataclysmDayReport> AdvanceDays(int32 Days);

	/**
	 * One floor's worth of time passes, which is usually less than a day.
	 *
	 * WHY A FLOOR IS NOT ALWAYS A DAY. One floor costs one day to begin with,
	 * and a city upgrade lowers that rate for the dungeons that city receives
	 * **without lowering their floor count**. A fifty floor dungeon can be made
	 * to cost two days rather than fifty: still fifty floors deep, still worth
	 * what that is worth, still biting on the same schedule, and a quarter of a
	 * month quicker to walk. `FCataclysmDungeon::WalkDaysPerFloor` is the rate.
	 *
	 * IT SPENDS WHOLE DAYS THROUGH `AdvanceDay` AND NOTHING ELSE, so a day that
	 * accumulates out of twenty-five floors fires its surge, repairs its cities
	 * and resolves its dungeons exactly as a day spent in one go would. The part
	 * of a day left over is kept on `UCataclysmDayClock::PartialDay`.
	 *
	 * @param Days the floor's cost. Zero or less passes no time.
	 * @return one report per whole day that passed, in order. **Usually empty**,
	 *         which is the difference between this and `AdvanceDay`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	TArray<FCataclysmDayReport> SpendFloorTime(float Days);

	// ----------------------------------------------------------------------
	// The dungeons standing on the map
	// ----------------------------------------------------------------------

	/** One dungeon by its identifier, or null. */
	const FCataclysmDungeon* FindDungeon(int32 DungeonId) const;

	/** Every dungeon standing on one city. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	TArray<int32> DungeonsOn(int32 CityId) const;

	/** How many dungeons are on the map. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 DungeonCount() const { return Dungeons.Num(); }

	/**
	 * The PLAYER cleared a dungeon, and it leaves the map.
	 *
	 * NO DUNGEON RUN CALLS THIS YET. It is what a finished dungeon run will
	 * call, and it is here because the alternative -- a caller reaching into
	 * `Dungeons` and the clock's `Timers` separately -- is how the two lists
	 * come apart.
	 *
	 * IT IS NOT WHAT A CITY FALLING USES. A city that falls absorbs the dungeons
	 * standing on it, and that goes through `RemoveDungeon` below instead. The
	 * two are separate because clearing a dungeon can now restore a city's
	 * defence, and a city that has just fallen must not be healed by the
	 * dungeons that killed it.
	 *
	 * @return whether it was there to clear.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool ClearDungeon(int32 DungeonId);

	// ----------------------------------------------------------------------
	// City upgrades
	// ----------------------------------------------------------------------

	/**
	 * Spends one of a city's upgrade slots.
	 *
	 * WHY BUYING IS HERE AND THE STATE IS ON THE MAP. Three of the ten built
	 * effects need something the map does not own: the dungeons standing on a
	 * city, and the current day. This has both.
	 *
	 * WHAT IT REFUSES, each with its own reason so a screen can say which:
	 * a run that has not begun, no such city, a fallen city, no slots left, an
	 * upgrade the city already has, and an upgrade whose effect is not built.
	 * See `ECataclysmCityUpgradeResult`.
	 *
	 * IT IS FREE AND IMMEDIATE. The design says a city upgrade costs gold and
	 * takes days to build and neither number is written anywhere, so both are
	 * zero. `UCataclysmCityUpgradeRules::GoldCost` and `BuildDays` are where
	 * they will go, and this refuses outright if either is raised without the
	 * payment and the timer being built. Issue #1264.
	 *
	 * @param Upgrade what to buy. `UCataclysmCityUpgradeMapping::Make` in the
	 *                `Cataclysm` module builds one from a row of
	 *                `game/Data/CityUpgrades.csv`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	ECataclysmCityUpgradeResult BuyCityUpgrade(
		int32 CityId, const FCataclysmCityUpgrade& Upgrade);

	/**
	 * What `BuyCityUpgrade` would answer, without buying anything.
	 *
	 * WHY IT IS SEPARATE. A screen has to grey out the upgrades a city cannot
	 * buy and say why, and the only way for it to be right is to ask the same
	 * question the purchase asks. A screen that decided for itself would be a
	 * second opinion about a question already answered here, and the two would
	 * drift the first time a rule changed.
	 *
	 * `BuyCityUpgrade` CALLS THIS AND REFUSES ON ANYTHING BUT `Bought`, so there
	 * is one list of checks rather than two.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	ECataclysmCityUpgradeResult WouldBuyCityUpgrade(
		int32 CityId, const FCataclysmCityUpgrade& Upgrade) const;

	// ----------------------------------------------------------------------
	// Seeing it
	// ----------------------------------------------------------------------

	/**
	 * The state of the run in a few lines: the day, the map, how far the
	 * Cataclysm has to go, and what is standing where.
	 *
	 * WHAT IT IS FOR IS EVIDENCE AND A CONSOLE COMMAND, not the interface.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	FString Describe() const;

private:
	/**
	 * Rolls a wave onto the map and starts its timers.
	 *
	 * @param Today         the day it lands on
	 * @param bFromCityFall whether a city falling caused it
	 * @param OutReport     the day's report, to record what arrived
	 */
	void FireSurge(int32 Today, bool bFromCityFall, FCataclysmDayReport& OutReport);

	/**
	 * A dungeon's timer ran out: it takes its share of its host city, and the
	 * city may fall.
	 */
	void ResolveDungeon(int32 DungeonId, FCataclysmDayReport& OutReport);

	/**
	 * Every Siege takes its daily share of the city it stands on.
	 *
	 * THE ONE DUNGEON THAT COSTS SOMETHING EVERY DAY. The design document:
	 * "Deals 1% damage to city defenses and population per day while active."
	 * Every other dungeon is free until its timer runs out, which is what makes
	 * a Siege the one you cannot leave for later.
	 *
	 * A FRACTION OF THE CITY'S MAXIMUM, not of what it has left, and that is
	 * what the sentence has to mean. A percentage of the remainder never reaches
	 * zero, so a city could be besieged for ever and never fall; a percentage of
	 * the maximum takes a hundred days to empty an untouched city, which is a
	 * real threat on the empire's timescale. Every other city damage in this
	 * layer is a fraction of the maximum for the same reason.
	 *
	 * IT GOES THROUGH `UCataclysmEmpireMap::Bite`, so a city that bought damage
	 * resistance resists this too. The upgrade says it resists damage and does
	 * not name a source.
	 */
	void ApplySiegeDamage(FCataclysmDayReport& OutReport);

	/** A city fell: its dungeons are absorbed and its fall fires a surge. */
	void CityFell(int32 CityId, FCataclysmDayReport& OutReport);

	/**
	 * Takes a dungeon off the map and off the clock, and nothing else.
	 *
	 * THE HALF OF `ClearDungeon` THAT IS ONLY BOOKKEEPING. A city absorbing its
	 * dungeons as it falls uses this, so it does not trigger what a player
	 * clearing a dungeon triggers.
	 */
	bool RemoveDungeon(int32 DungeonId);

	/**
	 * Fires the two "every N days" city upgrades that are due today.
	 *
	 * EACH UPGRADE CARRIES ITS OWN NEXT TRIGGER DAY rather than the day being
	 * divided by the interval, because an upgrade bought on day 37 should first
	 * fire a full interval later and not on whichever day happens to divide
	 * evenly.
	 */
	void RunCityUpgradeIntervals(int32 Today);
};
