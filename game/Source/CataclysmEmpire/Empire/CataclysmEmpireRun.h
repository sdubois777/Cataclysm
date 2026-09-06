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
	 *
	 * **A DUNGEON IN THIS LIST DID NOT NECESSARILY BITE ANYTHING.** It is the
	 * list of timers that ran out, and since issue #1324 slice 3 not every
	 * timer running out costs the host city something.
	 * `FCataclysmDungeon::Resolves` is what says which do: a Quest dungeon
	 * refreshes instead of detonating, and a Fallen City and a Cataclysm stand
	 * on a city whose damage is already done. A caller counting how often the
	 * empire was hurt must filter on that rather than on the length of this
	 * list. Nothing does yet -- a run-wide count of dungeons that detonated is
	 * slice 5 of that issue.
	 *
	 * A QUEST DUNGEON'S TIMER IS A RELOCATION CLOCK AND APPEARS HERE ON PURPOSE.
	 * The design has it "refresh and may move to adjacent city", and this is the
	 * event the move hangs on. `Relocated` below is which ones actually moved.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Resolved;

	/**
	 * The dungeons that moved to another city today.
	 *
	 * A SUBSET OF `Resolved` AND NEVER MORE THAN THAT. Only a Quest dungeon
	 * moves, and only when its timer runs out; `docs/Cataclysm_GDD_v2.md`
	 * section VIII says it "does not resolve -- refreshes and may move to
	 * adjacent city". Issue #1324 slice 4.
	 *
	 * **IT IS SHORTER THAN THE QUEST DUNGEONS IN `Resolved`, AND THAT IS THE
	 * RULE RATHER THAN A LOSS.** A Quest dungeon whose neighbours are all
	 * sealed or fallen has nowhere adjacent to go and stays where it stands,
	 * which is where the design's "MAY move" comes from. About one quest timer
	 * in five fires with no target. See
	 * `UCataclysmSurgeScheduler::PickRelocation`.
	 *
	 * WHERE EACH ONE WENT IS READ OFF THE DUNGEON. `FCataclysmDungeon::CityId`
	 * is already the new city by the time this report is returned, so recording
	 * the destination here would be a second copy of it that could disagree.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<int32> Relocated;

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
	 * many were standing there when it fell. `CityFell` builds that dungeon
	 * through `AddFallenCityDungeon`, since issue #1324 slice 2, so these are
	 * removed from the map and counted into the one that replaces them. This
	 * comment said nothing built it and that stopped being true at `c7e11f7`.
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
 *   - **Choosing for the player.** The model's day loop also asks a policy which
 *     dungeon to walk into, spends days at the forge, and kills the player. None
 *     of that belongs here: in the game the player is a character in a world,
 *     and what this owns is the empire's side of the clock.
 *   - **The win condition, though no longer the requirement it reads.** Since
 *     issue #1357 this DOES answer whether the player has earned the right to
 *     face the Cataclysm -- `IsCataclysmDungeonUnlocked`, half the active
 *     Cataclysms finished, rounded up. What it still does not do is ACT on the
 *     answer: nothing here creates a Cataclysm boss dungeon, opens an enemy
 *     capital or sets a won state. That is slice 6 of issue #1324, and its other
 *     two blockers are untouched -- the enemy capital does not exist and neither
 *     does the loss condition, and both are design gaps as well as missing code.
 *   - **The Last Stand.** `bPillarExposed` is the condition; the Cataclysm boss
 *     dungeon moving to the Pillar and absorbing everything still standing is
 *     issue #43.
 *   - **Saving any of it.** `UCataclysmRunSave` carries an `int32 Day` that
 *     nothing computes. Joining that record to this is separate work.
 *
 * THREE THINGS THIS LIST CLAIMED WERE MISSING WERE ALREADY BUILT, and each had
 * been for at least a slice before anybody re-read the list. They are recorded
 * here rather than quietly deleted, because a comment that says a feature is
 * absent is read as evidence that it is:
 *
 *   - It said nothing called `EnterDungeon` or `LeaveDungeon` on the clock.
 *     `ACataclysmDungeonGameMode` calls both.
 *   - It said no dungeon run reaches `ClearDungeon`. Walking to the bottom floor
 *     of an empire dungeon calls it, through
 *     `ACataclysmDungeonGameMode::ClearEmpireDungeon`, and that is the live path
 *     the counters below are raised on.
 *   - It said a fallen city becoming a retakeable dungeon was still issue #41.
 *     `AddFallenCityDungeon`, in this class, has done it since `c7e11f7`.
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
	 * THIS WAS WRITTEN FIRST AND THE MODEL FOLLOWED IT, which is the reverse of
	 * this project's usual direction. The design document was the only source for
	 * the number when this was built, because the simulation gave a sub-type no
	 * behaviour at all. That changed on 2026-09-06: the project owner ruled that
	 * the Siege be added to the model, and `sim/cataclysm_sim/engine.py` now
	 * carries `_apply_siege_damage` with `TuningConfig.siege_defence_bite_per_day`
	 * beside it. Issue #1329. The two must now be kept in step like every other
	 * ported number.
	 *
	 * IT IS THE LAST SHARE-OF-THE-MAXIMUM IN THE GAME AND THAT IS ON PURPOSE.
	 * Issue #1331 turned every dungeon resolve into flat points; the owner ruled
	 * on 2026-09-05, verbatim, "Keep it as a deliberate exception
	 * (Recommended)". See `ApplySiegeDamage`.
	 */
	static constexpr float SiegeDefenceBitePerDay = 0.01f;

	/** The same share of the city's maximum population. See above. */
	static constexpr float SiegePopulationBitePerDay = 0.01f;

	/**
	 * How much more damage a Siege deals for each day it has stood.
	 *
	 * THE DESIGN DOCUMENT SAYS "Increases in power by 2.5 points per day", and
	 * the project owner settled on 2026-09-05 what its power is: "That's in
	 * regards to the damage it does to the city/population". So this is 2.5
	 * points of city defence and 2.5 points of city population, added to the
	 * day's damage for every day the Siege has already stood.
	 *
	 * IT WAS 10 UNTIL 2026-09-06, AND THE OWNER CUT IT ON ISSUE #1349, verbatim
	 * "Halve the rate and cut the growth". The dose-response curves in
	 * `sim/analyse_siege_dose.py` showed a Siege at the old numbers taking the
	 * earned Cataclysm dungeon -- the route the design treats as the ordinary
	 * way to win -- from 84% of campaigns to 8%. `SpawnWeightSiege` in
	 * `CataclysmSurge.h` carries the other half of the ruling, 15 down to 7.5.
	 * The 1% share above was left exactly where the owner put it.
	 *
	 * POINTS AND NOT A SHARE, WHICH IS WHY SMALL CITIES SUFFER MOST. Two and a
	 * half points is a quarter of a per cent of an Outpost's thousand defence
	 * and an eightieth of that of a Pillar's twenty thousand, so the growth
	 * bites hardest where the empire is thinnest. An unattended Siege empties an
	 * Outpost's defence in 25 days, a Bulwark's in 39, a Sanctuary's in 55 and
	 * the Pillar's in 70. Without the growth every city would take exactly 100
	 * days whatever its size, because the flat part is a share of that city's
	 * own maximum.
	 *
	 * THOSE FOUR DAY-COUNTS ARE DERIVED AND NOT CHOSEN. A city falls on the
	 * first day D where `D * 0.01 * MaxDefence + 2.5 * D * (D - 1) / 2` reaches
	 * its maximum defence, which for an Outpost is `1.25*D*D + 8.75*D >= 1000`.
	 * `sim/tests/test_siege_subtype.py::TestItMatchesTheGamesOwnStatedFigures`
	 * runs the model's day loop against all four and fails by name if this
	 * constant and this comment part company.
	 *
	 * IT COUNTS FROM THE DAY THE SIEGE ARRIVED, so its first day deals the flat
	 * share alone and each later day adds another 2.5 points.
	 * `FCataclysmDungeon::SpawnedDay` is the day it arrived.
	 *
	 * IT IS ADDED TO BOTH THE DEFENCE AND THE POPULATION DAMAGE, which is the
	 * plain reading of a sentence whose subject is the damage it does and whose
	 * previous sentence names both. That reading is recorded in
	 * `docs/DECISIONS.md` as a reading rather than a ruling, because the owner's
	 * answer settled what "power" means and not which of the two it grows.
	 */
	static constexpr float SiegeDamageGrowthPerDay = 2.5f;

	/**
	 * Whether a list of dungeons and a list of timers describe the same board.
	 *
	 * THE ONE DEFINITION OF "THESE AGREE", AND IT IS STATIC ON PURPOSE. The two
	 * lists are kept in step by this class and by nothing else, so a save that
	 * wrote them and a load that read them back would each be a second writer of
	 * that relationship. Both sides call this rather than each deciding for
	 * itself what agreement means, because two definitions drift.
	 *
	 * WHAT DISAGREEMENT COSTS, WHICH IS WHY IT IS WORTH A CHECK AT ALL. A
	 * dungeon with no timer never resolves and sits on its city for ever. A
	 * timer with no dungeon runs out and bites a city on behalf of a dungeon
	 * that is not there. Neither announces itself.
	 *
	 * IT IS CALLED ON THE WAY OUT, BY THE SAVE. Refusing to write a pair that
	 * disagrees means a corrupt pair cannot reach a file at all, so whoever
	 * builds the restore inherits a guarantee rather than a hope, and the
	 * load-side check becomes a second line rather than the only one. It also
	 * names the run that produced the fault, where a check on the way in could
	 * only say the file was bad.
	 *
	 * @param OutWhy  set to what is wrong, for a log or a test message. Untouched
	 *                when they agree.
	 * @return whether every dungeon has exactly one timer and every timer has a
	 *         dungeon
	 */
	static bool DungeonsAgreeWithTimers(
		const TArray<FCataclysmDungeon>& Dungeons,
		const TArray<FCataclysmDungeonTimer>& Timers,
		FString& OutWhy);

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

	/**
	 * The source of chance for everything the run does.
	 *
	 * IT IS NO LONGER THE ONLY ONE. `CataclysmStream` below draws which
	 * Cataclysm sends each dungeon, deliberately away from this one; its comment
	 * says why.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FRandomStream Stream;

	/**
	 * The chance that decides which Cataclysm sends a dungeon, and nothing else.
	 *
	 * **SEPARATE FROM `Stream` SO THAT ADDING CATACLYSM IDENTITY DID NOT RE-ROLL
	 * EVERY RUN IN THE PROJECT.** Drawing this from `Stream` would consume one
	 * number per dungeon and shift every later draw, so the same seed would land
	 * different waves on different cities at different depths than it did before
	 * issue #1357 -- and every fixed-seed test measuring something else would
	 * have started failing for a reason that had nothing to do with what it
	 * measures. `cataclysm_order_for` in `sim/cataclysm_sim/engine.py` keeps its
	 * own generator for exactly this reason and states it.
	 *
	 * **IT WILL HAVE TO MOVE INTO `Stream` WHEN THE ATTACK PATTERNS ARE
	 * PORTED**, and this is where that is recorded. Today the draw is uniform
	 * and independent of everything else a wave rolls, so where it comes from
	 * cannot affect anything but itself. Issue #53 gives each Cataclysm its own
	 * pattern, at which point which one sends a dungeon decides where the
	 * dungeon lands and how deep it is -- and then it is part of the wave's own
	 * chance and belongs in the same stream. `Simulation.trigger_surge` already
	 * draws it from the main stream for that reason.
	 *
	 * STILL DETERMINISTIC FROM THE RUN'S SEED, so the same seed still gives the
	 * same run in every respect. `Begin` initialises both.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FRandomStream CataclysmStream;

	/**
	 * Which Cataclysms this run faces.
	 *
	 * HOW MANY comes from the difficulty tier and WHICH ONES from the seed. The
	 * design says a character faces one Cataclysm at first and gains one per
	 * boss defeated until it faces all eight; the project owner ruled on
	 * 2026-09-06 that the ORDER they are added in belongs to the character, so
	 * replaying at the same seed meets the same ones. `UCataclysmRoster::
	 * ActiveFor` draws it and its comment carries the argument.
	 *
	 * IT IS THE DENOMINATOR OF THE UNLOCK RULE. `CataclysmDungeonRequirement`
	 * is half of this list's length, rounded up.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<ECataclysmType> ActiveCataclysms;

	/** What the next dungeon will be numbered. Only ever counts up. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 NextDungeonId = 0;

	// ----------------------------------------------------------------------
	// What the run has come to -- issue #1324 slice 5
	// ----------------------------------------------------------------------

	/**
	 * How many dungeons of any kind the player has beaten this run.
	 *
	 * A PORT OF `Simulation.cleared` in `sim/cataclysm_sim/engine.py`, which
	 * counts every kind the same way. `ClearDungeon` is the only thing that
	 * raises it, so a dungeon a falling city absorbed is not counted: nobody
	 * walked it. `RemoveDungeon` is that path and it deliberately counts
	 * nothing.
	 *
	 * IT IS NOT THE NUMBER THE BOSS DUNGEON GROWS BY. That is
	 * `BasicDungeonsCleared` below, and reading this one instead would let
	 * clearing quest dungeons deepen the fight they exist to unlock. The design
	 * document is explicit about the difference; see there.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 DungeonsCleared = 0;

	/**
	 * How many ORDINARY dungeons the player has beaten this run.
	 *
	 * "ORDINARY" IS THE DESIGN'S WORD FOR `ECataclysmDungeonType::Basic`.
	 * `docs/Cataclysm_GDD_v2.md` section VIII: "Every **ordinary** dungeon
	 * defeated adds one floor to the Cataclysm boss dungeon. Quest dungeons and
	 * retaken Dungeon Cities do not: a Quest dungeon is the win condition itself,
	 * and retaking your own city is recovery rather than progress." Settled with
	 * the project owner on 2026-09-06.
	 *
	 * SO THIS IS A SEPARATE COUNTER AND NOT A CONVENIENCE. `DungeonsCleared`
	 * above is the wrong number for the boss's depth, and the model says so in
	 * as many words beside `Simulation.basic_cleared`, which this ports.
	 *
	 * NOTHING READS IT YET. The Cataclysm boss dungeon does not exist -- that is
	 * slice 6 of issue #1324 -- and growing it by one floor per ordinary dungeon
	 * is [#1315](https://github.com/sdubois777/Cataclysm/issues/1315). This is
	 * the number both will need, counted from the day the player starts rather
	 * than reconstructed afterwards from a board that no longer holds what was
	 * cleared.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 BasicDungeonsCleared = 0;

	/**
	 * How many quest objectives the player has earned this run.
	 *
	 * ONE CLEARED QUEST DUNGEON IS ONE OBJECTIVE, which the project owner ruled
	 * on 2026-09-06, verbatim "Yes -- one dungeon, one objective".
	 * `docs/Cataclysm_GDD_v2.md` section XI states it: the seals, seeds,
	 * essences, cores and rituals the eight Cataclysms name are flavour for one
	 * mechanic. A port of `Simulation.objectives`.
	 *
	 * **IT IS THE RUN'S TOTAL AND IT IS NOT WHAT THE UNLOCK RULE READS.** That
	 * is `QuestObjectivesByCataclysm` below, and the two are kept apart on
	 * purpose. A total cannot express the owner's rule: a player facing four
	 * Cataclysms who clears eight quest dungeons all belonging to one of them
	 * has eight objectives and has finished at most one Cataclysm, where the
	 * rule asks for two. This is what the empire screen shows and what a
	 * player's sense of progress reads; the map below is what the gate reads.
	 *
	 * IT IS STILL THE SUM OF THE MAP, and the two are raised together in
	 * `ClearDungeon`. A dungeon that names no Cataclysm -- one built by hand in
	 * a test -- raises this and nothing in the map, which is the one case where
	 * they come apart and is why the sum is not simply computed.
	 *
	 * Issue #1324 slice 5, and issue #1357 for the split.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 QuestObjectives = 0;

	/**
	 * Quest objectives earned FOR EACH CATACLYSM, which is what the unlock rule
	 * reads.
	 *
	 * A PORT OF `Simulation.objectives_by_type`. Every active Cataclysm is a key
	 * from the moment `Begin` runs, so one that has never sent a quest dungeon
	 * reads as 0 rather than as absent, and a caller iterating this sees the
	 * whole campaign rather than only the parts of it that have happened.
	 *
	 * IT IS NOT SAVED, and neither is `QuestObjectives`. What the player has
	 * achieved this run is on the list `UCataclysmRunSave`'s own comment names as
	 * deliberately absent; issue
	 * [#1374](https://github.com/sdubois777/Cataclysm/issues/1374) owns writing
	 * it. This joins that list rather than starting a new problem.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TMap<ECataclysmType, int32> QuestObjectivesByCataclysm;

	/**
	 * How many times a dungeon's timer ran out and the empire actually paid.
	 *
	 * WHAT `FCataclysmDayReport::Resolved` CANNOT ANSWER, which is why this
	 * exists. That list is every timer that ran out, and since slice 3 of issue
	 * #1324 not every timer running out costs a city anything: a Quest dungeon
	 * refreshes and moves, and a Fallen City and a Cataclysm stand on a city
	 * whose damage is already done. A caller counting how often the empire was
	 * hurt had no number to read and the report's own comment said so.
	 *
	 * **IT IS DELIBERATELY NOT A PORT OF `Simulation.resolved`, WHICH COUNTS
	 * SOMETHING LOOSER.** The model raises its tally before asking whether the
	 * dungeon detonates, so a Fallen City dungeon's timer running out counts
	 * there and not here. `RunResult.dungeons_resolved` is documented in the
	 * model as "times a dungeon detonated undefeated", which stopped being true
	 * of it when slice 2 gave the model a kind that does not detonate. Copying
	 * that arithmetic would have carried the defect across rather than the
	 * meaning, so this counts the bite and the difference is recorded in
	 * `docs/DECISIONS.md` and in
	 * [#1373](https://github.com/sdubois777/Cataclysm/issues/1373).
	 *
	 * A CITY THAT HAS ALREADY FALLEN IS NOT A BITE EITHER. `ResolveDungeon`
	 * returns before touching anything when its host is gone, and this is raised
	 * where the damage is dealt rather than at the top of that function, so the
	 * two cannot part company.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 DungeonsDetonated = 0;

	/**
	 * Starts a run: an intact empire, a clock at day 0, and a surge due
	 * immediately.
	 *
	 * A SECOND CALL IS A FRESH RUN rather than a continuation, so a caller that
	 * wants to keep a run has to keep this object.
	 *
	 * @param InSeed         what to seed the run's chance with. The same seed
	 *                       gives the same run, AND the same set of Cataclysms:
	 *                       see `ActiveCataclysms`.
	 * @param Mode           how surges escalate. An open tuning question; see
	 *                       `ECataclysmSurgeMode`.
	 * @param LethalityRung  0 Standard, 1 Hardcore, 2 Heretic.
	 * @param DifficultyTier which tier this run is played at, 1 to 8. **IT IS
	 *                       HOW MANY CATACLYSMS ARE ACTIVE** and that is the
	 *                       only thing this class reads it for. It is passed in
	 *                       rather than read off the game mode because
	 *                       `ACataclysmGameMode` is in the `Cataclysm` module
	 *                       and this one must not depend on it; the caller that
	 *                       has both is the one that joins them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void Begin(int32 InSeed = 0,
			   ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static,
			   int32 LethalityRung = 0,
			   int32 DifficultyTier = 1);

	/** Which day the run is on. Counted from 0, before any day has passed. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 Day() const;

	/** Whether the Cataclysm can reach the Pillar. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsLost() const;

	// ----------------------------------------------------------------------
	// The Cataclysm dungeon's unlock -- issue #1357
	// ----------------------------------------------------------------------

	/**
	 * How many quest objectives this Cataclysm has earned so far.
	 *
	 * ZERO FOR ONE THAT IS NOT ACTIVE, and zero for `None`. Reading a Cataclysm
	 * the run does not face is a caller's mistake and the honest answer is that
	 * the player has done nothing towards it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 QuestObjectivesFor(ECataclysmType Cataclysm) const;

	/**
	 * Whether this Cataclysm's own objective count has been met.
	 *
	 * THE COUNTS DIFFER: Death asks for 5 quest dungeons and Demonic for 10. See
	 * `UCataclysmRoster::QuestObjectivesFor`.
	 *
	 * FALSE FOR A CATACLYSM THE RUN DOES NOT FACE. `None` asks for 0, which
	 * would otherwise read as complete the moment the run began; the active list
	 * is what this is asked about and `None` is never in it, but a caller can
	 * ask anything, so this refuses rather than relying on that.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsCataclysmComplete(ECataclysmType Cataclysm) const;

	/** How many of the active Cataclysms have had their own count met. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 CataclysmsComplete() const;

	/**
	 * How many have to be, for the Cataclysm dungeon to unlock.
	 *
	 * HALF OF `ActiveCataclysms`, ROUNDED UP. The project owner's ruling of
	 * 2026-09-06; `UCataclysmRoster::CataclysmsRequiredFor` carries the verbatim
	 * words, the worked table and the reason the odd counts are the only ones
	 * that prove which rounding was built.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 CataclysmDungeonRequirement() const;

	/**
	 * Whether the player has earned the right to face the Cataclysm.
	 *
	 * **IT ANSWERS AND IT DOES NOT ACT.** Nothing here creates a Cataclysm
	 * dungeon, opens an enemy capital or sets a won state; that is slice 6 of
	 * issue [#1324](https://github.com/sdubois777/Cataclysm/issues/1324) and it
	 * has two blockers left that this does not touch -- the enemy capital does
	 * not exist, and neither does the loss condition. Both are design gaps as
	 * well as missing code. What issue #1357 removes is the third: the module
	 * could not tell which Cataclysm a quest dungeon belonged to, so the
	 * requirement could not be checked at all.
	 *
	 * IT CAN GO FROM TRUE BACK TO FALSE ONLY IF THE ACTIVE COUNT RISES, and it
	 * cannot: `ActiveCataclysms` is set once, by `Begin`, from a difficulty tier
	 * a run is played at from beginning to end. The owner's ruling did not say
	 * whether an unlocked Cataclysm dungeon should re-lock if a ninth Cataclysm
	 * could arrive mid-run, and it does not have to, because it cannot.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsCataclysmDungeonUnlocked() const;

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
	 * A DUNGEON RUN DOES CALL THIS. `ACataclysmDungeonGameMode::GoDownOneFloor`
	 * reaches the bottom floor of an empire dungeon and calls
	 * `ClearEmpireDungeon`, which calls this. It is also here because the
	 * alternative -- a caller reaching into `Dungeons` and the clock's `Timers`
	 * separately -- is how the two lists come apart.
	 *
	 * IT IS NOT WHAT A CITY FALLING USES. A city that falls absorbs the dungeons
	 * standing on it, and that goes through `RemoveDungeon` below instead. The
	 * two are separate because clearing a dungeon can now restore a city's
	 * defence, and a city that has just fallen must not be healed by the
	 * dungeons that killed it.
	 *
	 * **AND IT IS THE ONLY THING THAT COUNTS PROGRESS**, which is the second
	 * reason that split matters. `DungeonsCleared`, `BasicDungeonsCleared` and
	 * `QuestObjectives` are all raised here and nowhere else, so a dungeon a
	 * falling city swallowed earns the player nothing -- nobody walked it. Issue
	 * #1324 slice 5.
	 *
	 * NOTHING IS COUNTED FOR A DUNGEON THAT WAS NOT THERE. The counters are
	 * raised after the removal succeeds, so clearing the same identifier twice,
	 * or one that never existed, adds nothing.
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
	 * Which active Cataclysm sent one dungeon of a wave.
	 *
	 * UNIFORM OVER THE ACTIVE SET, which is a statement about what is NOT built
	 * rather than a design decision. `Simulation.trigger_surge` weights the draw
	 * by each Cataclysm's `count_mult`, so a Death wave really is a swarm and a
	 * Celestial one really is a handful -- and this module has no patterns at
	 * all. Weighting by something that does not exist would be inventing it.
	 * Issue #53 ports them, and then this is where the weights go.
	 *
	 * PER DUNGEON AND NOT PER WAVE, which is also the model's shape: a single
	 * surge is a wave from every active Cataclysm at once rather than from one
	 * of them in turn, which is what makes three active Cataclysms three
	 * different problems instead of one problem three times over.
	 *
	 * `None` WHEN NOTHING IS ACTIVE, which `Begin` makes impossible -- the
	 * active count is clamped to at least one -- but a run whose `Begin` never
	 * ran has an empty list and reaches nothing else here anyway.
	 */
	ECataclysmType RollCataclysm();

	/**
	 * A dungeon's timer ran out: if it is a kind that detonates it takes a
	 * NUMBER OF POINTS off its host city, and the city may fall.
	 *
	 * POINTS AND NOT A SHARE. Issue #1331 and `UCataclysmEmpireMap::Damage`.
	 *
	 * NOT EVERY KIND DETONATES. `FCataclysmDungeon::Resolves` is the rule and
	 * this leaves the city untouched when it answers false -- a Quest dungeon
	 * refreshes instead, which is issue #1324 slice 3. The clock has already put
	 * the timer back to full by the time this is called, so refreshing is doing
	 * nothing; what is not nothing is the move, which is
	 * `RelocateQuestDungeon` below.
	 */
	void ResolveDungeon(int32 DungeonId, FCataclysmDayReport& OutReport);

	/**
	 * A Quest dungeon whose timer ran out picks up and moves to an adjacent
	 * city, or stays where it is when it has no adjacent city to move to.
	 *
	 * `docs/Cataclysm_GDD_v2.md` section VIII: a Quest dungeon "does not
	 * resolve -- refreshes and **may move to adjacent city**". The project
	 * owner ruled on 2026-09-06, verbatim "Adjacent, and fix the simulation".
	 * Issue #1324 slice 4.
	 *
	 * `UCataclysmSurgeScheduler::PickRelocation` DECIDES AND THIS ACTS, which
	 * is the division every other rule in this module keeps: the scheduler owns
	 * what a dungeon is and where it may be, this owns the dungeons actually
	 * standing on the map.
	 *
	 * **IT IS THE ONLY THING IN THIS CLASS THAT MOVES A DUNGEON**, and it is
	 * separate from `ResolveDungeon` for that reason rather than for length.
	 * `ResolveDungeon` holds its dungeon by const pointer, so a reader can see
	 * at a glance that the biting path cannot move anything, and this is where
	 * to look when a dungeon has changed city.
	 *
	 * IT MOVES ONLY A QUEST DUNGEON. A Fallen City and a Cataclysm also refuse
	 * to resolve and also reach here; a Fallen City *is* its city and cannot
	 * leave it, and the Cataclysm boss dungeon's one move is the Last Stand,
	 * issue #43.
	 */
	void RelocateQuestDungeon(int32 DungeonId, FCataclysmDayReport& OutReport);

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
	 * real threat on the empire's timescale.
	 *
	 * AND IT IS NOW THE ONLY CITY DAMAGE IN THIS LAYER THAT IS A SHARE. It used
	 * to be one of many. Issue #1331 turned every dungeon resolve into points,
	 * because a share of the maximum divided out of how long a city survived and
	 * made every city-health upgrade worthless; the project owner ruled on
	 * 2026-09-05, verbatim, "Keep it as a deliberate exception (Recommended)"
	 * for the Siege, on the grounds that a siege does not care how thick your
	 * walls are. So this is the one threat city-health investment does not
	 * protect against, and the percentage in the implementation is the ruling
	 * rather than an oversight.
	 *
	 * IT GOES THROUGH `UCataclysmEmpireMap::Damage`, so a city that bought
	 * damage resistance resists this too. The upgrade says it resists damage and
	 * does not name a source. A siege ignores how much a city can ABSORB and is
	 * still blunted by what REDUCES damage; the owner confirmed that separately
	 * on 2026-09-06, verbatim: "Yes -- damage reduction still applies
	 * (Recommended)".
	 */
	void ApplySiegeDamage(FCataclysmDayReport& OutReport);

	/**
	 * A city fell: its dungeons are absorbed into the one it becomes, and its
	 * fall fires a surge.
	 */
	void CityFell(int32 CityId, FCataclysmDayReport& OutReport);

	/**
	 * The city that just fell becomes a dungeon standing on itself.
	 *
	 * IT IS THE ONLY DUNGEON NOT PUT THERE BY A SURGE, and the only one whose
	 * depth is decided rather than rolled. See
	 * `UCataclysmSurgeScheduler::MakeFallenCityDungeon`.
	 *
	 * AN ERASED CITY GETS NONE. The Void erases rather than takes, and an erased
	 * city cannot be retaken, so a dungeon on one would be a reward the map
	 * refuses to pay.
	 *
	 * @param DungeonsAbsorbed how many stood on it when it fell.
	 */
	void AddFallenCityDungeon(int32 CityId, int32 DungeonsAbsorbed,
							  FCataclysmDayReport& OutReport);

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
