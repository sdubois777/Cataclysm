// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "UObject/Object.h"
#include "CataclysmSurge.generated.h"

/**
 * How the Cataclysm escalates surge over surge.
 *
 * AN OPEN TUNING QUESTION RATHER THAN A SETTLED DESIGN, and the reason this is
 * an enum at all. The design document says a surge "recurs after a fixed number
 * of days or when a city falls" and stops there. `sim/experiments.py` sweeps all
 * four of these against each other; until that sweep has an answer the game has
 * to be able to run any of them.
 */
UENUM(BlueprintType)
enum class ECataclysmSurgeMode : uint8
{
	/** Fixed gap, fixed count. Nothing escalates. */
	Static			= 0	UMETA(DisplayName = "Static"),

	/** The gap shrinks each surge; the count is fixed. */
	Accelerating	= 1	UMETA(DisplayName = "Accelerating"),

	/** The count grows each surge; the gap is fixed. */
	Swelling		= 2	UMETA(DisplayName = "Swelling"),

	/** Both. */
	Both			= 3	UMETA(DisplayName = "Both"),
};

// `ECataclysmDungeonType` IS IN `Empire/CataclysmDungeonKind.h`, included
// above, and only one of its four kinds is built:
//
//   - **Basic** is what a surge spawns, and the only one `SpecFor` answers for.
//   - **Quest** relocates instead of resolving. The Cataclysm quest mechanics
//     are issue #51.
//   - **FallenCity** is what a city that has fallen becomes. It needs a dungeon
//     runtime, issue #41.
//   - **Cataclysm** is the boss dungeon. Issue #43.

/**
 * How deep a dungeon of one kind on one tier of city is, and what it takes when
 * it resolves undefeated. `config.DungeonSpec`.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmDungeonSpec
{
	GENERATED_BODY()

	/** The shallowest such dungeon. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 LeastFloors = 1;

	/** The deepest. Inclusive: a dungeon may be exactly this deep. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 MostFloors = 1;

	/**
	 * Share of the host city's MAXIMUM defence taken each time this dungeon
	 * resolves undefeated, before scaling by how deep this one is.
	 *
	 * OF THE MAXIMUM AND NOT OF WHAT IS LEFT. That is what makes an ignored city
	 * die on a schedule rather than approach zero for ever.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DefenceBite = 0.0f;

	/** The same, for population. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PopulationBite = 0.0f;

	/** Whether this spec describes a dungeon that exists in the game yet. */
	bool IsBuilt() const { return MostFloors > 1; }
};

/**
 * One dungeon a surge put on the map.
 *
 * WHAT IT IS AND WHAT `FCataclysmDungeonTimer` IS. The timer, in
 * `CataclysmDayClock.h`, holds only what a clock has to know: which dungeon,
 * how deep, how long left. This is the dungeon itself -- which city it sits on,
 * what it will take when it resolves, when it arrived. The day clock's own
 * comment asked for exactly this split and said the identifier would be
 * "SUPPLIED RATHER THAN GENERATED, because the thing that creates dungeons is
 * the surge system and it does not exist". This is that thing.
 *
 * WHAT IS NOT ON IT YET: the 117 dungeon modifiers in
 * `game/Data/DungeonModifiers.csv`, and which Cataclysm sent it. Both are issue
 * #41 and issue #53. The sub-type IS on it now; see `SubType` below.
 */
USTRUCT(BlueprintType)
struct CATACLYSMEMPIRE_API FCataclysmDungeon
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 DungeonId = INDEX_NONE;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmDungeonType Type = ECataclysmDungeonType::Basic;

	/**
	 * What this dungeon does differently, rolled when it spawned.
	 *
	 * `None` IS THE COMMON ANSWER and a real one: it carries a third of the
	 * spawn weight, more than any single sub-type. See
	 * `UCataclysmSurgeScheduler::RollSubType`.
	 *
	 * ONE OF THE SEVEN CHANGES THE CLOCK AND THE REST DO NOT. Cow Level doubles
	 * `WalkDays` and the doubling cannot be reduced, which is the only sub-type
	 * rule the empire layer can carry out today. The other six describe what
	 * happens inside a dungeon -- waves, per-floor bosses, a time limit, changing
	 * modifiers -- or need systems that are not built. `UCataclysmEmpireRun` acts
	 * on none of them.
	 *
	 * WHAT DOES READ IT. `ACataclysmDungeonGameMode::EnterEmpireDungeon` copies
	 * it onto the game mode, so `UCataclysmEnemyScore` scores the creatures on a
	 * floor with the sub-type weight the design gives it. Before this field
	 * existed that weight was always the one for `None`.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmDungeonSubType SubType = ECataclysmDungeonSubType::None;

	/** Which city it is assaulting. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 CityId = INDEX_NONE;

	/** That city's tier when the dungeon spawned, which set its depth. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmCityTier CityTier = ECataclysmCityTier::Outpost;

	/**
	 * How many floors deep it is.
	 *
	 * IT DECIDES WHAT THE DUNGEON IS WORTH AND WHEN IT BITES. Its resolve timer
	 * is set from this, so a deeper dungeon is worth more and is slower to bite.
	 *
	 * IT IS NOT THE WALK COST, THOUGH IT STARTS EQUAL TO IT. One floor costs one
	 * day as a starting rate and `WalkDays` below is where that lands, so a city
	 * upgrade can make the walk shorter while this stays where it is.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 Floors = 1;

	/** Days from spawning until it resolves undefeated, jitter included. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ResolveDays = 0.0f;

	/**
	 * How many days walking the whole dungeon costs.
	 *
	 * SEPARATE FROM THE FLOOR COUNT, AND THAT IS THE WHOLE POINT. One floor
	 * costs one day to begin with, so this starts equal to `Floors`. City
	 * upgrades and the empire tree lower it **while the floor count stays where
	 * it is**, which is what lets a player who has invested make a fifty floor
	 * dungeon cost two days rather than fifty. The dungeon is still fifty floors
	 * deep, still worth what fifty floors are worth, and still bites on the same
	 * schedule; only the time to walk it moved.
	 *
	 * ZERO MEANS NOBODY SET IT, and then a floor costs the ordinary one day. A
	 * dungeon built by hand in a test, or one from before this field existed,
	 * behaves exactly as it did.
	 *
	 * NEVER BELOW ONE once it is set, which is what "to a minimum of 1" in the
	 * City Upgrades sheet says. A dungeon that cost no time at all would be free,
	 * and the empire layer's whole tension is that nothing is.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float WalkDays = 0.0f;

	/**
	 * What one floor of this dungeon costs, in days.
	 *
	 * THE WHOLE WALK DIVIDED BY THE FLOORS. A fifty floor dungeon costing two
	 * days charges a twenty-fifth of a day per floor, and
	 * `UCataclysmDayClock::SpendDays` is what turns those into whole days.
	 */
	float WalkDaysPerFloor() const;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DefenceBite = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PopulationBite = 0.0f;

	/** Which day it arrived. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 SpawnedDay = 0;

	/**
	 * How much of its type's bite this particular dungeon actually takes.
	 *
	 * A DEEPER DUNGEON HITS HARDER, in proportion to how deep it is against a
	 * typical one of its kind on that tier of city. `Simulation._resolve`
	 * computes it as `floors / ((least + most) / 2)`, so a dungeon of exactly
	 * typical depth scales by one.
	 */
	float BiteScale() const;
};

/**
 * When the next wave comes, how big it is, and which cities it lands on.
 *
 * A PORT OF `Simulation.surge_count`, `surge_gap` and `trigger_surge` in
 * `sim/cataclysm_sim/engine.py`, and the third thing in the `CataclysmEmpire`
 * module. Like the two before it, nothing here touches a world, an actor or a
 * pawn.
 *
 * WHAT A SURGE IS. The design document: "A Surge is triggered at run start and
 * recurs after a fixed number of days or when a city falls. During a Surge, the
 * Cataclysm releases a wave of dungeons that assault random player cities.
 * Players must prioritize which dungeons to tackle before they resolve and
 * which cities to sacrifice."
 *
 * IT DECIDES, IT DOES NOT ACT. It answers when the next surge is due, how many
 * dungeons it brings, which cities they land on and what each one is. Adding
 * them to a clock, taking their bite out of a city and noticing a city fall is
 * the empire's day loop, issue #1084.
 *
 * THE CONSTANTS ARE A COPY AND THE SIMULATION IS THE ORIGINAL.
 * `tools/tests/test_surge_port.py` compares every one of them against
 * `sim/cataclysm_sim/config.py`, the same arrangement `UCataclysmDayClock` and
 * `UCataclysmEmpireMap` are in.
 *
 * WHAT IS NOT PORTED, named so the scope is not argued later:
 *
 *   - **The other seven Cataclysms.** The simulation splits a wave between
 *     however many are active, each attacking in its own way -- a Death wave is
 *     a swarm of shallow dungeons, a Celestial one is a handful of deep ones.
 *     This is the generic pattern, which is uniform pressure on the frontier
 *     weighted by tier. Issue #53.
 *   - **The Demonic Cataclysm's rifts, which ignore the frontier entirely.**
 *     That is the one pattern the vertical slice would actually use, and it
 *     belongs with the Hell on Earth quest mechanic, issue #51. Issue #1085
 *     records what it would mean for the lane rule.
 *   - **Quest dungeons.** 12% of a wave in the simulation, and they relocate
 *     rather than resolving. Issue #51.
 *   - **The 117 dungeon modifiers.** Issue #41.
 *   - **What six of the seven sub-types do.** One of them is here: Cow Level
 *     doubles the walk. The other six describe what happens inside a dungeon,
 *     or need systems that are not built. Issue #41.
 *   - **The empire upgrade tree**, which adds days to the gap. Issue #38.
 */
UCLASS(BlueprintType)
class CATACLYSMEMPIRE_API UCataclysmSurgeScheduler : public UObject
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// Cadence
	// ----------------------------------------------------------------------

	/** Days between surges before any escalation. `config.surge_interval_days`. */
	static constexpr float IntervalDays = 120.0f;

	/** Dungeons in a surge before any escalation. `config.surge_dungeon_count`. */
	static constexpr int32 DungeonsPerSurge = 4;

	/**
	 * What an accelerating surge multiplies the gap by each time.
	 * `config.surge_interval_decay`.
	 */
	static constexpr float IntervalDecay = 0.88f;

	/**
	 * The shortest the gap ever gets. `config.surge_interval_min`.
	 *
	 * WITHOUT A FLOOR AN ACCELERATING RUN ENDS IN A SURGE EVERY DAY, which is
	 * not difficulty but arithmetic running away.
	 */
	static constexpr float LeastIntervalDays = 25.0f;

	/** Dungeons a swelling surge adds each time. `config.surge_count_growth`. */
	static constexpr float CountGrowthPerSurge = 0.5f;

	/** The most a surge ever brings, before the lethality mode. `config.surge_count_max`. */
	static constexpr int32 MostDungeonsPerSurge = 14;

	/**
	 * Whether a city falling fires a surge of its own.
	 * `config.surge_on_city_fall`. The design document says it does.
	 */
	static constexpr bool bSurgeOnCityFall = true;

	/**
	 * Whether a surge fired by a city falling also advances the escalation
	 * counter. `config.city_fall_advances_escalation`.
	 *
	 * TRUE, AND IT IS WHAT MAKES A BAD RUN GET WORSE. Losing a city does not
	 * only cost that city: it permanently speeds the rest of the run up.
	 */
	static constexpr bool bCityFallAdvancesEscalation = true;

	/**
	 * What Heretic multiplies a wave by. "Surges spawn 25% more dungeons."
	 *
	 * APPLIED AFTER THE CAP AND NOT BEFORE, and the order is the whole point.
	 * The design states the Heretic rule without qualification, while
	 * `MostDungeonsPerSurge` bounds how far the Cataclysm's own escalation runs,
	 * which is a different thing. Multiplying first would make Heretic identical
	 * to Standard at every surge that reaches the cap -- which is exactly where
	 * the extra dungeons would hurt most. Issue #289 in the simulation.
	 */
	static constexpr float HereticDungeonMultiplier = 1.25f;

	// ----------------------------------------------------------------------
	// Where a wave lands
	// ----------------------------------------------------------------------

	/**
	 * How much a wave prefers each tier. `config.SURGE_TARGET_WEIGHT`.
	 *
	 * THE PILLAR IS WEIGHTED AT NOTHING, and that is a rule rather than a
	 * preference: the Pillar is only ever attacked in the Last Stand, when the
	 * Cataclysm comes to the player. A wave never lands there.
	 */
	static constexpr float OutpostTargetWeight = 5.0f;
	static constexpr float BulwarkTargetWeight = 3.0f;
	static constexpr float SanctuaryTargetWeight = 1.5f;
	static constexpr float PillarTargetWeight = 0.0f;

	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float TargetWeightFor(ECataclysmCityTier Tier);

	// ----------------------------------------------------------------------
	// What a dungeon is
	// ----------------------------------------------------------------------

	/**
	 * How far a dungeon's resolve timer varies from the figure the depth gives.
	 * `config.resolve_jitter`. Rolled once, when the dungeon is made.
	 *
	 * PLUS OR MINUS 15%, SO TWO DUNGEONS OF THE SAME DEPTH ARE NOT THE SAME
	 * PROBLEM. Without it every dungeon of a given depth would come due on
	 * exactly the same day as every other, and triage would be arithmetic rather
	 * than a judgement. `UCataclysmDayClock::ResolveDaysFor` is the figure this
	 * is applied to; its own comment says the jitter was left for whoever built
	 * the thing that makes dungeons.
	 */
	static constexpr float ResolveJitter = 0.15f;

	// ----------------------------------------------------------------------
	// What a dungeon does differently
	// ----------------------------------------------------------------------

	/**
	 * How often each sub-type is rolled. `config.SUBTYPE_SPAWN_WEIGHTS`.
	 *
	 * THEY ADD UP TO 100, so each one reads as a percentage, but nothing depends
	 * on that: `RollSubType` divides by whatever the total is. Changing one
	 * weight without rebalancing the rest changes that sub-type's share and
	 * dilutes every other, which is the behaviour you want from a weight.
	 *
	 * NO SUB-TYPE AT ALL IS THE COMMONEST OUTCOME, at 34 against the largest
	 * single sub-type's 12. A dungeon that does something unusual should be worth
	 * noticing, and it is not if most of them do.
	 *
	 * COW LEVEL IS THE RAREST AT 4, which is what its reward deserves: the design
	 * document gives it "ridiculous amounts of loot" for double the walk.
	 *
	 * THESE ARE A COPY AND THE SIMULATION IS THE ORIGINAL, the same arrangement
	 * every other constant in this file is in.
	 * `tools/tests/test_dungeon_subtype_port.py` reads all eight back out of this
	 * header and fails if either side moves.
	 */
	static constexpr float SpawnWeightNone			= 34.0f;
	static constexpr float SpawnWeightTimed			= 12.0f;
	static constexpr float SpawnWeightHorde			= 12.0f;
	static constexpr float SpawnWeightSiege			= 10.0f;
	static constexpr float SpawnWeightCowLevel		= 4.0f;
	static constexpr float SpawnWeightElite			= 10.0f;
	static constexpr float SpawnWeightVolatile		= 10.0f;
	static constexpr float SpawnWeightSacrificial	= 8.0f;

	/**
	 * How often the given sub-type is rolled.
	 *
	 * @return its weight, or 0 for a value that is not one of the eight. A
	 *         weight of zero means never chosen, which is the right answer for a
	 *         sub-type this build does not know about.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float SpawnWeightFor(ECataclysmDungeonSubType SubType);

	/**
	 * What to call a sub-type when a person has to read it.
	 *
	 * WHY THIS EXISTS RATHER THAN ENUM REFLECTION. `UCataclysmEmpireMap::TierName`
	 * beside it is a plain switch for the same reason: the spelling shown to a
	 * person is a decision, and taking it from the identifier means renaming the
	 * identifier silently changes what is on screen.
	 *
	 * `None` ANSWERS AN EMPTY STRING, not the word "None". A caller printing a
	 * dungeon wants "dungeon 3 on Outpost (1,2)" for an ordinary one, not
	 * "dungeon 3 (None) on ...", and most dungeons are ordinary.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString SubTypeName(ECataclysmDungeonSubType SubType);

	/**
	 * What Cow Level multiplies the walk by.
	 *
	 * THE DESIGN DOCUMENT: "Time to complete is doubled and cannot be reduced."
	 * Both halves are rules. `MakeDungeon` applies this to the depth's own walk
	 * cost rather than to the reduced one, so a city that bought
	 * `DungeonWalkDaysFewer` gets no discount here -- which is what "cannot be
	 * reduced" says, and is why the multiplication is not simply applied last.
	 */
	static constexpr float CowLevelWalkMultiplier = 2.0f;

	/**
	 * How many Siege dungeons one city may hold at once.
	 *
	 * ONE, WHICH THE DESIGN DOCUMENT STATES: "Max 1 per city." A Siege takes a
	 * share of its host every day it stands rather than only when its timer runs
	 * out, so two of them on one city would take that share twice a day and no
	 * city could survive the pair.
	 *
	 * A SECOND ONE BECOMES A DUNGEON WITH NO SUB-TYPE rather than being rolled
	 * again. Rolling again would take a second draw from the stream, and every
	 * later dungeon in the same wave would then depend on what this one first
	 * rolled -- see `RollSubType`. Plain is also the honest answer: the design
	 * says a city may not have two Sieges, not that it should get something else
	 * instead.
	 */
	static constexpr int32 SiegesPerCity = 1;

	/**
	 * Rolls one sub-type, weighted.
	 *
	 * ONE DRAW FROM THE STREAM, ALWAYS, whatever it returns. See the note on
	 * `PickTargets` about draw counts: a roll that sometimes took two would make
	 * every later dungeon in the same wave depend on what this one rolled.
	 *
	 * IT WALKS THE ENUM IN ITS DECLARED ORDER, WHICH IS NOT THE MODEL'S ORDER.
	 * `config.SUBTYPE_SPAWN_WEIGHTS` lists them commonest first and
	 * `ECataclysmDungeonSubType` lists them in the order the design document
	 * does. The two therefore pick different sub-types from the same random
	 * fraction, and both give the same distribution. The port test compares the
	 * weights by name for that reason, and not a sequence of rolls.
	 */
	static ECataclysmDungeonSubType RollSubType(FRandomStream& Stream);

	/**
	 * The floor range and the bites for one kind of dungeon on one tier of city.
	 * `config.DUNGEON_SPECS`.
	 *
	 * ONLY `Basic` IS ANSWERED FOR. The other three kinds have no runtime in the
	 * game and the specs they would need are numbers nothing would read; see
	 * `ECataclysmDungeonType` for which issue each belongs to. Anything else
	 * answers a spec whose `IsBuilt` is false rather than a plausible-looking
	 * guess.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FCataclysmDungeonSpec SpecFor(ECataclysmDungeonType Type,
										 ECataclysmCityTier Tier);

	// ----------------------------------------------------------------------
	// The schedule
	// ----------------------------------------------------------------------

	/** Which escalation the run is being played under. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Empire")
	ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static;

	/**
	 * Which lethality mode the run is being played in: 0 Standard, 1 Hardcore,
	 * 2 Heretic.
	 *
	 * A NUMBER AND NOT `ECataclysmLethality`, for the module boundary reason
	 * `UCataclysmDayClock::DeathDayCostFor` gives: that enum lives in the
	 * `Cataclysm` module and this layer must not depend on it.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Empire")
	int32 LethalityRung = 0;

	/**
	 * How many surges have fired, which is what every escalation counts.
	 *
	 * NOT THE SAME AS HOW MANY WAVES HAVE LANDED, when a fall-triggered surge
	 * does not advance it. Today it always does; see
	 * `bCityFallAdvancesEscalation`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 SurgeIndex = 0;

	/** How many waves have landed, escalating or not. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 SurgesFired = 0;

	/**
	 * The day the next surge is due.
	 *
	 * ZERO TO START WITH, BECAUSE A SURGE FIRES AT RUN START. The design
	 * document says a surge "is triggered at run start", so day 0 is due.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float NextSurgeDay = 0.0f;

	/** What a wave is multiplied by in this lethality mode. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float DungeonMultiplier() const;

	/** How many dungeons the next surge brings. Never fewer than one. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	int32 DungeonsInNextSurge() const;

	/** How many days there will be between this surge and the one after it. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float GapAfterThisSurge() const;

	/** Whether a surge is due on this day. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsDue(int32 Day) const;

	/** How many days until the next surge. Zero once it is due. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float DaysUntilNextSurge(int32 Day) const;

	/**
	 * Records that a surge fired today, and sets when the next one is due.
	 *
	 * @param Day           which day it fired on
	 * @param bFromCityFall whether a city falling caused it rather than the
	 *                      clock. See `bCityFallAdvancesEscalation`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	void RecordSurge(int32 Day, bool bFromCityFall = false);

	// ----------------------------------------------------------------------
	// The wave
	// ----------------------------------------------------------------------

	/**
	 * Which cities a wave of this size lands on.
	 *
	 * ONLY THE EXPOSED FRONTIER CAN BE ATTACKED, and never the Pillar. A city
	 * behind a standing one is sealed until that city falls.
	 *
	 * WITH REPLACEMENT, so two dungeons of one wave may land on the same city.
	 * That is what the simulation does and it is what lets a wave concentrate:
	 * a city can hold several dungeons, and one that does is in real trouble.
	 *
	 * A CITY THAT HAS BOUGHT THE DUNGEON CAP STOPS BEING A TARGET once it holds
	 * that many, and the wave lands somewhere else rather than getting smaller.
	 * "There can be no more than 15 dungeons on this city" says nothing about
	 * where the dungeon goes instead; making it vanish would let a capped city
	 * absorb wave slots harmlessly, which is a far stronger upgrade than the
	 * sentence describes.
	 *
	 * THE CAP IS ENFORCED WHILE THE WAVE IS BUILT, not only when the candidates
	 * are chosen, because the roll is with replacement: a city one short of its
	 * cap could otherwise be chosen twice in one wave and end up over it.
	 *
	 * THE NUMBER OF RANDOM DRAWS IS ONE PER DUNGEON WHATEVER THE CAPS ARE, so a
	 * run in which nothing bought a cap rolls exactly what it rolled before this
	 * existed. Anything that took an extra draw would shift every later roll and
	 * change runs that have nothing to do with the upgrade.
	 *
	 * @param DungeonsPerCity how many dungeons already stand on each city,
	 *                        indexed by city identifier. Empty means the caller
	 *                        does not know, and then no cap is applied --
	 *                        `UCataclysmEmpireRun` owns the dungeons and this
	 *                        class deliberately does not.
	 * @return one city identifier per dungeon, in the order they were rolled.
	 *         Empty when nothing is exposed, which cannot happen on a built map
	 *         because the rim always is. SHORTER THAN `Count` only when every
	 *         exposed city has reached its cap.
	 */
	TArray<int32> PickTargets(
		const UCataclysmEmpireMap& Map, int32 Count, FRandomStream& Stream,
		const TArray<int32>& DungeonsPerCity = TArray<int32>()) const;

	/**
	 * How many more dungeons a city will accept before its cap is reached.
	 *
	 * A LARGE NUMBER WHEN IT HAS NO CAP, rather than a flag, so the caller can
	 * count down without asking twice whether a cap exists.
	 */
	static int32 RoomLeftOn(const FCataclysmCity& City,
							const TArray<int32>& DungeonsPerCity);

	/**
	 * One dungeon, rolled for a city.
	 *
	 * THE CITY'S OWN UPGRADES SHAPE IT. Two of them change the floor count, and
	 * the timer follows from the floor count rather than being adjusted
	 * separately: a deeper dungeon is worth more and slower to bite, and a
	 * shallower one is poorer and bites sooner. Depth and reward are the same
	 * axis and that trade must not be worked around.
	 *
	 * DEPTH AND TIME ARE A DIFFERENT MATTER. A third upgrade lowers `WalkDays`
	 * alone, leaving the floor count, the reward and the timer where they are.
	 * A fifty floor dungeon that costs two days to walk is still fifty floors
	 * deep and still bites on the same schedule.
	 */
	FCataclysmDungeon MakeDungeon(int32 DungeonId, const FCataclysmCity& City,
								  int32 Day, FRandomStream& Stream) const;

	/**
	 * The whole wave: picks the targets and rolls a dungeon for each.
	 *
	 * IT DOES NOT RECORD THE SURGE. `RecordSurge` is separate so a caller can
	 * see what a wave would be without moving the schedule on, which is what the
	 * tests do.
	 *
	 * IT ENFORCES THE ONE-SIEGE-PER-CITY RULE, and it is the only place that
	 * can. `MakeDungeon` is deliberately ignorant of what already stands on a
	 * city, and it is also called on its own by tests; the wave is the smallest
	 * thing that can see both what was already there and what it is about to
	 * add. A dungeon that rolls Siege for a city that already has one is made
	 * plain instead. See `SiegesPerCity`.
	 *
	 * @param FirstDungeonId what to number the first dungeon; the rest follow
	 *                       from it.
	 * @param DungeonsPerCity passed through to `PickTargets`; see there.
	 * @param SiegesPerCityNow how many Siege dungeons already stand on each
	 *                       city, indexed by city identifier. Empty means the
	 *                       caller does not know, and then no Siege is refused
	 *                       -- `UCataclysmEmpireRun` owns the dungeons and this
	 *                       class deliberately does not. Sieges this wave itself
	 *                       creates are counted whether or not it is supplied,
	 *                       so one wave can never land two on one city.
	 */
	TArray<FCataclysmDungeon> RollWave(
		const UCataclysmEmpireMap& Map, int32 Day, int32 FirstDungeonId,
		FRandomStream& Stream,
		const TArray<int32>& DungeonsPerCity = TArray<int32>(),
		const TArray<int32>& SiegesPerCityNow = TArray<int32>()) const;
};
