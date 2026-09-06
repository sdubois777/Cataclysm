// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CataclysmDayClock.generated.h"

/**
 * One dungeon's timer, as the day clock sees it.
 *
 * WHAT IT IS NOT: a dungeon. There is no city, no Cataclysm, no modifier list
 * and no consequence here. A dungeon in the design has all of those and building
 * them needs an empire graph, which is issue #42 and does not exist. This is the
 * part of a dungeon that a clock has to know about: how deep it is, how long
 * until it resolves undefeated, and which one it is.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmDungeonTimer
{
	GENERATED_BODY()

	/**
	 * Which dungeon this is the timer for.
	 *
	 * SUPPLIED RATHER THAN GENERATED, because the thing that creates dungeons is
	 * the surge system and it does not exist. The clock only ever compares these
	 * for equality, so any distinct numbers will do.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 DungeonId = INDEX_NONE;

	/**
	 * How many floors deep it is.
	 *
	 * IT DECIDES WHAT THE DUNGEON IS WORTH AND WHEN IT BITES. The resolve timer
	 * is set from this, so a deeper dungeon is worth more and is slower to bite.
	 *
	 * IT IS NOT THE WALK COST, THOUGH IT STARTS EQUAL TO IT. One floor costs one
	 * day as a starting rate, and `FCataclysmDungeon::WalkDays` is where a real
	 * dungeon's shortened walk is held, so a city upgrade can make the walk
	 * shorter while this stays where it is.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 Floors = 1;

	/** Days until it resolves undefeated. Counts down, one a day. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DaysUntilResolve = 0.0f;

	/** What the timer is set back to when it resolves. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ResolveDays = 0.0f;

	/**
	 * How many times it has resolved undefeated.
	 *
	 * A DUNGEON THAT RESOLVES DOES NOT GO AWAY, so this is not always 0 or 1. It
	 * is what lets an ignored city actually die: the same dungeon bites it again
	 * every time its timer runs out.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 TimesResolved = 0;
};

/**
 * The empire's clock: what a day costs, and which timers it moves.
 *
 * A PORT OF `sim/cataclysm_sim/engine.py` AND `config.py`, and the first thing
 * in the `CataclysmEmpire` module, which until now held a build file, an
 * `IMPLEMENT_MODULE` and nothing else. Its build file already said what this
 * layer is: "plain arithmetic on plain structs", which "should stay testable
 * without the combat, rendering or input systems". Nothing here touches a world,
 * an actor or a pawn.
 *
 * THE CONSTANTS ARE A COPY AND THE MODEL IS THE ORIGINAL.
 * `tools/tests/test_day_clock_port.py` reads every one of them back out of this
 * header and compares it against `sim/cataclysm_sim/config.py`. That guard
 * exists because the same arrangement has silently drifted twice for the power
 * model, which is what `CLAUDE.md` warns about at length. Change a number in the
 * simulation and this fails until it is copied across, and the other way round.
 *
 * WHAT IT DOES NOT DO, and these are named so the scope is not argued later:
 *
 *   - **It does not act on a resolve.** It says which dungeons ran out of time
 *     today. Taking the bite out of a city's defence and population needs an
 *     empire graph, which is issue #42.
 *   - **It does not create dungeons.** Surges do that and there are none.
 *   - **It knows nothing of the 117 dungeon modifiers**, which change how hard
 *     a dungeon is to fight rather than how long it takes.
 *   - **It does not carry the three sub-types that change the time model.**
 *     Timed and Siege both need city state. Cow Level doubles the run length and
 *     is the only one that could be here today.
 *   - **Nothing writes it to a save.** `UCataclysmRunSave` carries an `int32 Day`
 *     that nothing computes, and joining the two would make the `Cataclysm`
 *     module depend on this one. That dependency is allowed and is not needed
 *     yet.
 */
UCLASS(BlueprintType)
class CATACLYSMEMPIRE_API UCataclysmDayClock : public UObject
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// What a floor costs
	// ----------------------------------------------------------------------

	/**
	 * How many days one dungeon floor costs. `config.days_per_floor`.
	 *
	 * ONE, AS A STARTING RATE. It is what a floor costs before a player has
	 * invested in anything, and it is the figure every other number here is
	 * derived from.
	 *
	 * IT IS NOT AN INVARIANT, AND READING IT AS ONE HAS ALREADY COST THIS
	 * PROJECT A MERGED PULL REQUEST. City upgrades and the empire upgrade tree
	 * lower the days a dungeon takes to walk **while its floor count stays where
	 * it is**, which is the point of those upgrades rather than a loophole in
	 * them. A fifty floor dungeon an invested player runs in two days is still
	 * fifty floors deep and still worth what that is worth.
	 *
	 * WHAT THIS CONSTANT DOES AND DOES NOT DECIDE. `RunDaysFor` below answers
	 * what a depth costs at this rate and nothing lowers it here, because the
	 * upgrades live on a city. `FCataclysmDungeon::WalkDays` is where a real
	 * dungeon's shortened walk is held and `WalkDaysPerFloor` is the rate it
	 * actually charges. `PartialDay` further down exists precisely because that
	 * rate can be a fraction of a day.
	 *
	 * DEPTH AND REWARD ARE THE SAME AXIS. DEPTH AND TIME ARE NOT, once a player
	 * has invested in separating them. `CLAUDE.md` states this at length and
	 * `docs/DECISIONS.md` records the 2026-09-05 correction that established it.
	 *
	 * `sim/README.md` lists this among the rules the simulation fixes rather
	 * than sweeps, which is a fact about the model and not about the design: the
	 * model has no upgrades, so there is nothing there to lower the rate.
	 */
	static constexpr float DaysPerFloor = 1.0f;

	/** The shortest and longest a dungeon run may be, in days. */
	static constexpr int32 LeastRunDays = 1;
	static constexpr int32 MostRunDays = 400;

	/**
	 * How long a dungeon of a given depth takes to walk, in whole days.
	 *
	 * ROUNDED UP AND THEN CLAMPED, in that order, the way
	 * `Simulation.run_days_for` does it. A dungeon that took no days would be
	 * free, and the empire layer's whole tension is that nothing is.
	 *
	 * THE TREE'S REDUCTIONS ARE NOT PORTED. The model applies a flat reduction
	 * and a multiplier from the empire upgrade tree before clamping, and there is
	 * no upgrade tree here -- that is issue #38. With the tree's defaults, which
	 * are no reduction and a multiplier of one, the model's answer is this one.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 RunDaysFor(int32 Floors);

	// ----------------------------------------------------------------------
	// What a dungeon costs if it is ignored
	// ----------------------------------------------------------------------

	/**
	 * The days a dungeon of no depth would take to resolve.
	 * `config.resolve_base_days`.
	 */
	static constexpr float ResolveBaseDays = 10.0f;

	/**
	 * How many more days of timer each floor of depth buys.
	 * `config.resolve_floor_ratio`.
	 *
	 * **THE SINGLE MOST IMPORTANT NUMBER IN THE STRATEGY LAYER**, in the
	 * simulation's own words. At 1.0 every dungeon is exactly barely savable and
	 * nothing else can be. Above roughly 1.5 the player can save any one dungeon
	 * comfortably but still cannot save all of them, which is where triage lives.
	 */
	static constexpr float ResolveFloorRatio = 1.6f;

	/**
	 * How long a dungeon of a given depth takes to resolve undefeated, in days.
	 *
	 * `ResolveBaseDays + Floors * ResolveFloorRatio`.
	 *
	 * IT SCALES WITH DEPTH, AND WITH DEPTH ONLY. A flat timer cannot work at the
	 * starting rate of one day a floor: a 40-floor dungeon with a 30-day timer
	 * is unsavable however well the player plays. A shortened walk does not move
	 * this, which is what lets an invested player beat the timer.
	 *
	 * THE MODEL'S PER-DUNGEON JITTER IS NOT PORTED. `config.resolve_jitter`
	 * varies a real dungeon's timer by plus or minus 15%, rolled once when the
	 * dungeon is made. Nothing makes dungeons here, so there is nothing to roll
	 * it for; this is the figure the jitter is applied to.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float ResolveDaysFor(int32 Floors);

	// ----------------------------------------------------------------------
	// Two rules the clock obeys
	// ----------------------------------------------------------------------

	/**
	 * Whether the dungeon the player is standing in keeps counting down.
	 * `config.timer_ticks_while_running`.
	 *
	 * FALSE, AND THAT IS A DESIGN DECISION WITH REAL WEIGHT. Its residents are
	 * busy fighting the player rather than marching on the city, so entering a
	 * dungeon is a guaranteed save rather than a gamble. What it costs is pure
	 * opportunity: thirty days in here is thirty days every other timer advances
	 * without you.
	 */
	static constexpr bool bTimerTicksWhileRunning = false;

	/**
	 * Whether a dungeon that resolves undefeated stays on the map.
	 * `config.dungeon_persists_after_resolve`.
	 *
	 * TRUE. It is what lets an ignored city actually die: the same dungeon bites
	 * it again every time the refreshed timer runs out, rather than one bite and
	 * gone.
	 */
	static constexpr bool bDungeonPersistsAfterResolve = true;

	// ----------------------------------------------------------------------
	// What dying costs
	// ----------------------------------------------------------------------

	/**
	 * The days a death costs, one per lethality mode.
	 * `config.LETHALITY_RULES[...].death_day_cost`.
	 *
	 * HARDCORE IS NOT PERMADEATH IN THIS DESIGN, which is worth repeating here
	 * because the word means permadeath in most of this genre. Nothing destroys a
	 * character; dying costs days and some of what the character is wearing.
	 * `ECataclysmLethality` in the `Cataclysm` module carries the same three
	 * modes and the same note.
	 */
	static constexpr int32 DeathDayCostStandard = 5;
	static constexpr int32 DeathDayCostHardcore = 10;
	static constexpr int32 DeathDayCostHeretic = 15;

	/**
	 * What a death costs in the given mode.
	 *
	 * IT TAKES A NUMBER AND NOT `ECataclysmLethality`, AND THAT IS THE MODULE
	 * BOUNDARY RATHER THAN A SHORTCUT. That enum lives in the `Cataclysm` module
	 * and this layer must not depend on it -- its build file says the dependency
	 * runs one way. The numbering is safe to rely on because it is *persisted*: a
	 * character record stores the value, so renumbering it would change what
	 * every existing save says its mode is, and the enum says so itself.
	 * `tools/tests/test_day_clock_port.py` reads both and fails if they part.
	 *
	 * @param LethalityRung 0 Standard, 1 Hardcore, 2 Heretic. Anything else
	 *                      answers Standard, because a mode nobody chose should
	 *                      cost what the gentlest one costs rather than the
	 *                      harshest.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static int32 DeathDayCostFor(int32 LethalityRung);

	// ----------------------------------------------------------------------
	// The clock itself
	// ----------------------------------------------------------------------

	/** Which day the run is on. Counted from 0, before any day has passed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 Day = 0;

	/**
	 * Time spent that has not yet added up to a whole day.
	 *
	 * WHY A DAY CAN BE SPENT IN PIECES. A floor costs one day by default and
	 * upgrades can lower that rate, so a fifty floor dungeon a player has
	 * invested in may cost two days rather than fifty. Walking one of its floors
	 * then costs a twenty-fifth of a day, and there has to be somewhere to keep
	 * the part of a day that has been spent but has not yet turned into one.
	 *
	 * ALWAYS BETWEEN 0 AND 1. `SpendDays` advances a whole day for each whole
	 * day that accumulates, so this never reaches one.
	 *
	 * THE DAY ITSELF IS STILL A WHOLE NUMBER. Nothing else in the empire layer
	 * has to know about fractions: surges, timers and city falls all happen on
	 * `Day`, exactly as before.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PartialDay = 0.0f;

	/**
	 * Which dungeon the player is standing in, or `INDEX_NONE`.
	 *
	 * IT IS THE ONE TIMER THAT DOES NOT MOVE. See `bTimerTicksWhileRunning`.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 CurrentDungeonId = INDEX_NONE;

	/** Every dungeon the clock is counting down, in the order they were added. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FCataclysmDungeonTimer> Timers;

	/**
	 * Starts counting down a dungeon of the given depth.
	 *
	 * @param DungeonId which dungeon. Must not already be counting down.
	 * @param Floors    how deep. Below 1 is read as 1; there is no floorless
	 *                  dungeon.
	 * @return whether it was added
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool AddDungeon(int32 DungeonId, int32 Floors);

	/**
	 * Replaces a dungeon's timer with a figure the caller worked out.
	 *
	 * WHY A CALLER WOULD. `ResolveDaysFor` gives the figure a depth deserves and
	 * nothing else. The model varies a real dungeon's timer by plus or minus 15%,
	 * rolled once when the dungeon is made, and that roll needs a source of
	 * chance -- which a clock has no business owning. So whoever makes the
	 * dungeon rolls it and says so here.
	 *
	 * IT SETS BOTH THE TIMER AND WHAT IT REFILLS TO, because a dungeon that
	 * resolved and then refilled to a different figure than it started with would
	 * be two dungeons wearing one number.
	 *
	 * @param Days how long from now. Below zero is refused; a dungeon that
	 *             resolves before it exists is not a shorter timer, it is a
	 *             mistake.
	 * @return whether the clock knew that dungeon
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool SetResolveDays(int32 DungeonId, float Days);

	/** The timer for a dungeon, or null if the clock does not know it. */
	const FCataclysmDungeonTimer* FindTimer(int32 DungeonId) const;

	/** How many days are left on a dungeon's timer. -1 if it is not known. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float DaysUntilResolveFor(int32 DungeonId) const;

	/** The player walks into a dungeon, which stops its timer. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void EnterDungeon(int32 DungeonId);

	/** The player leaves, and every timer moves again. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void LeaveDungeon();

	/**
	 * A day passes.
	 *
	 * Every timer moves down by one except the one the player is standing in.
	 * Any that reach zero resolve: they are counted, their timer is set back to
	 * full, and they stay on the clock.
	 *
	 * @return the dungeons that resolved today, in the order they were added.
	 *         Empty on most days, which is the point of a timer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	TArray<int32> AdvanceDay();

	/**
	 * Several days pass at once.
	 *
	 * WHAT IT IS FOR IS NOT CONVENIENCE. Almost nothing in this game costs one
	 * day: walking a 40-floor dungeon costs 40, dying costs 5 to 15, and an item
	 * at the forge costs its own. Every one of those is a run of days during
	 * which timers keep moving and dungeons keep resolving, so a caller that
	 * added 40 to the day would skip 40 days of resolves.
	 *
	 * @param Days how many. Zero or fewer passes no time at all.
	 * @return every resolve over those days, oldest first. A dungeon that
	 *         resolved twice appears twice.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	TArray<int32> AdvanceDays(int32 Days);

	/**
	 * Adds part of a day to the carry, and says whether a whole day came out.
	 *
	 * WHY THIS EXISTS. A floor costs one day by default, and a city upgrade can
	 * lower that rate while the floor count stays where it is -- a fifty floor
	 * dungeon a player has invested in may cost two days rather than fifty. A
	 * floor of that dungeon then costs a twenty-fifth of a day, and something has
	 * to hold the part of a day spent so far.
	 *
	 * IT DOES NOT ADVANCE THE DAY, AND THAT IS DELIBERATE. `AdvanceDay` here
	 * moves this clock's timers and nothing else; the day loop that fires surges,
	 * repairs cities and resolves dungeons is `UCataclysmEmpireRun::AdvanceDay`,
	 * and only the run can drive it. Advancing from inside this class would move
	 * the timers while skipping everything else a day does.
	 * `UCataclysmEmpireRun::SpendFloorTime` is the caller.
	 *
	 * @param Days how much time to add. Zero or less adds nothing, which is what
	 *             a caller with no rate should pass rather than a negative number
	 *             that would wind the clock back.
	 * @return whether a whole day came out of the carry, which the caller should
	 *         then spend. Call again to find out whether another did.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	bool TakeAWholeDay(float Days);
};
