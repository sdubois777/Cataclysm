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
// above. `SpecFor` ANSWERS FOR ALL FOUR KINDS, and THREE OF THE FOUR ARE NOW
// CREATED:
//
//   - **Basic** is the common thing a surge spawns. `MakeDungeon` builds it.
//   - **Quest** refreshes instead of resolving, and clearing one is one
//     objective towards challenging the Cataclysm. A surge rolls one in place
//     of a Basic; see `RollKind` and `QuestChance`. Issue #1324 slice 3.
//     **And it relocates**: when its timer runs out it moves to an adjacent
//     exposed city, or stays put when there is not one. `PickRelocation`
//     chooses and `UCataclysmEmpireRun::ResolveDungeon` moves it. Issue #1324
//     slice 4. Issue #51 is the Hell on Earth quest mechanic for the Demonic
//     Cataclysm specifically, which is a different thing from this kind.
//   - **FallenCity** is what a city that has fallen becomes.
//     `MakeFallenCityDungeon` builds it and `UCataclysmEmpireRun::CityFell`
//     is the only caller. Issue #1324 slice 2.
//   - **Cataclysm** is the boss dungeon. NOTHING CREATES ONE; issue #1324
//     slice 6. Issue #43 moves it to the Pillar for the Last Stand and issue
//     #1315 grows it per dungeon defeated, and both assume it exists first.

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
	 * Defence POINTS taken each time this dungeon resolves undefeated, before
	 * scaling by how deep this one is.
	 *
	 * ABSOLUTE, AND NOT A SHARE OF THE HOST CITY. Issue #1331 is why. These were
	 * fractions of the city's own maximum, which meant the maximum divided out
	 * of how many resolves the city survived: a Pillar holding twenty times an
	 * Outpost's defence lasted 17 resolves against 10, and every upgrade in the
	 * game that raises a city's health was worth nothing. The project owner
	 * ruled on 2026-09-05, verbatim: "damage to cities shouldn't be a % of their
	 * hp. Instead, dungeons should have damage ranges that aren't % based, but
	 * should be flat damage numbers."
	 *
	 * `config.DungeonSpec.defense_damage` is the same number in the model, which
	 * changed first on issue #1327. Every value in `SpecFor` is the fraction it
	 * replaced multiplied by that tier's base maximum, so the arithmetic is
	 * unchanged for a city at its base size and the change of shape stands on
	 * its own.
	 *
	 * A DEEPER CITY STILL TAKES A SMALLER SHARE OF ITSELF: 100 points off an
	 * Outpost's 1,000 is a tenth, and 640 off a Sanctuary's 8,000 is 8%.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DefenceDamage = 0.0f;

	/** The same, as a number of people. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PopulationDamage = 0.0f;

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

	/**
	 * That city's tier when the dungeon spawned, which set its depth.
	 *
	 * **IT IS NOT "THE TIER OF THE CITY THIS DUNGEON IS STANDING ON", AND A
	 * QUEST DUNGEON IS WHERE THE TWO COME APART.** It is set once, by
	 * `MakeDungeon` and `MakeFallenCityDungeon`, and `RelocateQuestDungeon`
	 * deliberately leaves it alone when it moves a dungeon: read the host's tier
	 * off the map with `Map->Find(CityId)->Tier` instead.
	 *
	 * WHY IT MUST NOT MOVE. `BiteScale` is the only thing that reads it, and it
	 * divides `Floors` by the midpoint of `SpecFor(Type, CityTier)`. Both halves
	 * of that division have to name the same specification row, and `Floors`
	 * does not move. `RelocateQuestDungeon` used to assign the new host's tier
	 * here, which made a relocated dungeon read as shallower or deeper than it
	 * is; the project owner ruled on 2026-09-06, verbatim "Keeps everything, fix
	 * the size". Issue #1324.
	 */
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
	 * How many bosses it holds.
	 *
	 * ONE FOR EVERY DUNGEON BUT A FALLEN CITY, which is the design's universal
	 * rule: "Every dungeon has a boss on the final floor",
	 * `docs/Cataclysm_GDD_v2.md` section VIII.
	 *
	 * A FALLEN CITY IS THE STATED EXCEPTION AND NOT A VIOLATION OF THAT RULE.
	 * It carries one boss per dungeon that was standing on the city when it
	 * fell, which is the same count its floors are taken from, so losing a
	 * heavily besieged city is visibly worse than losing a quiet one. The final
	 * floor still carries one of them. Decided by the project owner on
	 * 2026-09-06, verbatim "One per dungeon that was standing when it fell",
	 * against the alternatives of a flat number and a share of the floors;
	 * issue #1324 records the question and the answer.
	 *
	 * WHERE THEY ACTUALLY STAND IS NOT DECIDED HERE. This is the strategy
	 * layer's count. Placing them on floors is the dungeon runtime's work,
	 * issue #41, and nothing reads this yet.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 Bosses = 1;

	/**
	 * What one floor of this dungeon costs, in days.
	 *
	 * THE WHOLE WALK DIVIDED BY THE FLOORS. A fifty floor dungeon costing two
	 * days charges a twenty-fifth of a day per floor, and
	 * `UCataclysmDayClock::SpendDays` is what turns those into whole days.
	 */
	float WalkDaysPerFloor() const;

	/**
	 * Defence points and people this dungeon takes when it resolves undefeated,
	 * copied from its spec. See `FCataclysmDungeonSpec::DefenceDamage`: they are
	 * POINTS and not shares of the city, which is issue #1331.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float DefenceDamage = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float PopulationDamage = 0.0f;

	/** Which day it arrived. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 SpawnedDay = 0;

	/**
	 * How much of its type's damage this particular dungeon actually deals.
	 *
	 * A DEEPER DUNGEON HITS HARDER, in proportion to how deep it is against a
	 * typical one of its kind on that tier of city. `Simulation._resolve`
	 * computes it as `floors / ((least + most) / 2)`, so a dungeon of exactly
	 * typical depth scales by one.
	 *
	 * IT SCALES POINTS AND NOT A SHARE, since issue #1331. The name is the
	 * event -- a dungeon biting a city -- rather than the old fraction.
	 */
	float BiteScale() const;

	/**
	 * Whether this dungeon's timer running out costs its host city anything.
	 *
	 * A PORT OF `Dungeon.resolves` IN `sim/cataclysm_sim/engine.py`, and the
	 * model's own words for why the other three answer false: "Quest dungeons
	 * refresh instead of resolving; Fallen City and Cataclysm dungeons have
	 * already done their damage."
	 *
	 * WHY THIS EXISTS RATHER THAN A CHECK THAT THE DAMAGE IS ZERO. All three
	 * kinds carry zero damage in `SpecFor` today, so reading the numbers would
	 * give the same answer -- but it would give it for the wrong reason. Issue
	 * #1327 turned city damage into flat points and asked, as this issue's own
	 * ninth design question, whether the three non-Basic kinds should deal any;
	 * if the answer is yes, a zero check silently starts letting a Quest dungeon
	 * detonate. The kind is what the design speaks about, so the kind is what
	 * this reads.
	 *
	 * IT DOES NOT MEAN THE TIMER NEVER FIRES, AND IT DOES NOT MEAN NOTHING
	 * HAPPENS. A Quest dungeon's timer is a relocation clock and is MEANT to run
	 * out -- see `QuestResolveDays`. What this answers is whether anything is
	 * taken from the CITY when it does; the dungeon itself refreshes and moves
	 * to an adjacent city. Issue #1324 slice 4.
	 */
	bool Resolves() const { return Type == ECataclysmDungeonType::Basic; }
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
	 * **EVERY DUNGEON A SURGE MAKES HAS A SUB-TYPE, AND THERE IS NO WEIGHT FOR
	 * `None` HERE ON PURPOSE.** The project owner ruled on 2026-09-05 that every
	 * dungeon should have one; before that, no sub-type at all was the commonest
	 * outcome at 34 in 100. The constant that held that 34 is gone rather than
	 * set to zero, because a zero weight reads as live code that can still be
	 * chosen. `ECataclysmDungeonSubType::None` still exists and still means
	 * something -- see `SpawnWeightFor` -- but no roll can produce it.
	 *
	 * THEY ADD UP TO 100, so each one reads as a percentage, but nothing depends
	 * on that: `RollSubType` divides by whatever the total is. Changing one
	 * weight without rebalancing the rest changes that sub-type's share and
	 * dilutes every other, which is the behaviour you want from a weight.
	 *
	 * COW LEVEL IS THE RAREST AT 7.6, which is what its reward deserves: the
	 * design document gives it "ridiculous amounts of loot" for double the walk.
	 * It was 4 in 100 while a third of dungeons were plain, so its share rose
	 * along with everything else when that third was redistributed.
	 *
	 * SIEGE IS 7.5 AND NOT 15 SINCE 2026-09-06. The owner ruled on issue #1349,
	 * verbatim, "Halve the rate and cut the growth", after the dose-response
	 * curves in `sim/analyse_siege_dose.py` showed a Siege at 15 in 100 taking
	 * the earned Cataclysm dungeon from 84% of campaigns to 8%. The 7.5 it gave
	 * up went to the other six IN PROPORTION, so the table still totals 100 and
	 * a Siege is still the third commonest thing a surge makes.
	 * `UCataclysmEmpireRun::SiegeDamageGrowthPerDay` carries the other half of
	 * that ruling, 10 points a day down to 2.5.
	 *
	 * THE SIX ARE ROUNDED TO ONE DECIMAL rather than rescaled exactly, which is
	 * the form the owner chose for this same table on 2026-09-05: "clean round
	 * numbers" over an exact proportional rescale. They sum to exactly 92.5.
	 *
	 * THESE ARE A COPY AND THE SIMULATION IS THE ORIGINAL, the same arrangement
	 * every other constant in this file is in.
	 * `tools/tests/test_dungeon_subtype_port.py` reads each one back out of this
	 * header and fails if either side moves.
	 */
	static constexpr float SpawnWeightTimed			= 19.6f;
	static constexpr float SpawnWeightHorde			= 19.6f;
	static constexpr float SpawnWeightSiege			=  7.5f;
	static constexpr float SpawnWeightCowLevel		=  7.6f;
	static constexpr float SpawnWeightElite			= 16.3f;
	static constexpr float SpawnWeightVolatile		= 16.3f;
	static constexpr float SpawnWeightSacrificial	= 13.1f;

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
	 * **A SECOND ONE IS SPREAD ACROSS THE OTHER SIX SUB-TYPES rather than being
	 * rolled again or made plain.** The project owner ruled on 2026-09-06,
	 * verbatim: "Spread it across the others". So a dungeon that would have been
	 * a Siege on a city that already has one becomes Timed, Horde, Cow Level,
	 * Elite, Volatile or Sacrificial, in proportion to their weights.
	 *
	 * AND IT STILL COSTS NO SECOND DRAW, which is what made this awkward.
	 * `RollSubType` re-reads THE DRAW IT ALREADY MADE into the weight space the
	 * other six occupy; see there for why that is exact rather than approximate.
	 * A second draw would make every later dungeon in the same wave depend on
	 * what this one first rolled.
	 *
	 * IT USED TO BECOME A DUNGEON WITH NO SUB-TYPE, until every dungeon was
	 * given one on 2026-09-05. That left this as the single way a surge could
	 * still produce a plain dungeon -- 1.6% of them, measured -- which is the
	 * hole this closes.
	 */
	static constexpr int32 SiegesPerCity = 1;

	/**
	 * The one sub-type a Cataclysm dungeon may not roll.
	 *
	 * **THE PROJECT OWNER RULED IT ON 2026-09-06, verbatim: "Last stand is a
	 * cataclysm dungeon and should not be allowed to roll as a cow level sub
	 * type."** Asked how far to take it they answered "Only the one you ruled".
	 * Issue #1333.
	 *
	 * WHY A COW LEVEL CATACLYSM WAS THE PROBLEM. Its time "is doubled and cannot
	 * be reduced", and the model's Last Stand adds floor bonuses after the
	 * dungeon is built and worked the walk out again without the sub-type, so
	 * the doubling was lost. Offered the repair, the owner removed the situation
	 * rather than fixing the symptom.
	 *
	 * **THIS IS THE ONLY ILLEGAL PAIR OF THE 28 AND NOTHING MAY BE INFERRED FROM
	 * IT.** Whether a Quest dungeon may carry a Siege that never resolves, or a
	 * Fallen City a Sacrificial, is unstated by omission rather than decided.
	 * Adding a second exclusion is a design decision for the owner, not a code
	 * one; issue #1333 raises the general question and #1342 asks whether a
	 * Fallen City should carry a sub-type at all.
	 *
	 * IT IS A COPY AND `config.SUBTYPES_FORBIDDEN_ON` IS THE ORIGINAL, the same
	 * arrangement every other constant in this file is in.
	 * `tools/tests/test_dungeon_subtype_port.py` reads it back out of this
	 * header and fails if either side moves.
	 */
	static constexpr ECataclysmDungeonSubType CataclysmForbiddenSubType =
		ECataclysmDungeonSubType::CowLevel;

	/**
	 * Which sub-type this kind of dungeon may not roll, or `None` for a kind
	 * that may roll all seven.
	 *
	 * THREE OF THE FOUR KINDS ANSWER `None`, which is the whole rule: only a
	 * Cataclysm is constrained, and only against Cow Level. See
	 * `CataclysmForbiddenSubType`.
	 *
	 * `None` IS THE RIGHT WAY TO SAY "NOTHING IS BARRED" HERE and not a missing
	 * answer, because `None` is not in the distribution at all --
	 * `TotalSpawnWeight` and `SubTypeAtPoint` already take it as the exclusion
	 * that excludes nothing, and this feeds straight into both.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static ECataclysmDungeonSubType BarredSubTypeOn(ECataclysmDungeonType Type);

	/**
	 * The total weight of every sub-type that can be rolled, leaving out up to
	 * two of them.
	 *
	 * @param Excluded which to leave out of the total. `None` is not in the
	 *                 distribution at all, so passing it excludes nothing, which
	 *                 is why it is the default.
	 * @param AlsoExcluded a second one to leave out, for the case where a Siege
	 *                 is refused on a dungeon whose kind already bars something.
	 *                 Passing the same value twice excludes it once.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float TotalSpawnWeight(
		ECataclysmDungeonSubType Excluded = ECataclysmDungeonSubType::None,
		ECataclysmDungeonSubType AlsoExcluded = ECataclysmDungeonSubType::None);

	/**
	 * The total weight of every sub-type declared before this one.
	 *
	 * WHERE A SUB-TYPE'S BAND STARTS on the weighted line `SubTypeAtPoint`
	 * walks. `RollSubType` needs it to work out where in Siege's own band a
	 * draw landed.
	 *
	 * @param Excluded a sub-type left off that line, so the bands after it start
	 *                 earlier by its weight. It matters when a dungeon's kind
	 *                 bars a sub-type declared BEFORE the one being asked about;
	 *                 with today's enum order Cow Level comes after Siege, so
	 *                 the only live exclusion changes nothing here. Passing it
	 *                 anyway is what keeps that a fact about the order rather
	 *                 than a dependency on it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static float SpawnWeightBelow(
		ECataclysmDungeonSubType SubType,
		ECataclysmDungeonSubType Excluded = ECataclysmDungeonSubType::None);

	/**
	 * Which sub-type a point on the weighted line lands on.
	 *
	 * THE LINE RUNS FROM 0 TO `TotalSpawnWeight(Excluded, AlsoExcluded)`, with
	 * each sub-type occupying a stretch as wide as its weight, in the enum's
	 * declared order. A point outside that range answers the last sub-type on
	 * the line rather than `None`: a caller asking which of these options a
	 * number picks should get one of them.
	 *
	 * @param Excluded a sub-type to leave off the line entirely, closing the gap
	 *                 rather than leaving a hole in it.
	 * @param AlsoExcluded a second one to leave off, for a refused Siege on a
	 *                 dungeon whose kind already bars something.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static ECataclysmDungeonSubType SubTypeAtPoint(
		float Point,
		ECataclysmDungeonSubType Excluded = ECataclysmDungeonSubType::None,
		ECataclysmDungeonSubType AlsoExcluded = ECataclysmDungeonSubType::None);

	/**
	 * Rolls one sub-type, weighted.
	 *
	 * ONE DRAW FROM THE STREAM, ALWAYS, whatever it returns, whether or not a
	 * Siege is allowed and whatever kind of dungeon is asking. See the note on
	 * `PickTargets` about draw counts: a roll that sometimes took two would make
	 * every later dungeon in the same wave depend on what this one rolled.
	 *
	 * IT WALKS THE ENUM IN ITS DECLARED ORDER, WHICH IS NOT THE MODEL'S ORDER.
	 * `config.SUBTYPE_SPAWN_WEIGHTS` lists them commonest first and
	 * `ECataclysmDungeonSubType` lists them in the order the design document
	 * does. The two therefore pick different sub-types from the same random
	 * fraction, and both give the same distribution. The port test compares the
	 * weights by name for that reason, and not a sequence of rolls.
	 *
	 * @param bSiegeAllowed whether this dungeon's city may take a Siege. When it
	 *                      may not and the draw lands on one, THE SAME DRAW is
	 *                      re-read into the weight space the other six occupy,
	 *                      so the refused Siege is spread across them in
	 *                      proportion to their weights. See `SiegesPerCity` for
	 *                      the ruling and the implementation for why re-reading
	 *                      is exact.
	 * @param Type what kind of dungeon is being rolled for. **A CATACLYSM MAY
	 *                      NOT BE A COW LEVEL** and everything else may be
	 *                      anything; see `BarredSubTypeOn`. The bar is applied
	 *                      by shortening the line the single draw is read
	 *                      against, so the barred sub-type's weight is spread
	 *                      over the rest in proportion and no second draw is
	 *                      taken. The default is `Basic`, which bars nothing and
	 *                      leaves every existing caller's stream untouched.
	 */
	static ECataclysmDungeonSubType RollSubType(
		FRandomStream& Stream,
		bool bSiegeAllowed = true,
		ECataclysmDungeonType Type = ECataclysmDungeonType::Basic);

	/**
	 * The floor range and the bites for one kind of dungeon on one tier of city.
	 * `config.DUNGEON_SPECS`.
	 *
	 * ALL FOUR KINDS ARE ANSWERED FOR, and the numbers are the model's rather
	 * than new ones: every row here is copied from `config.DUNGEON_SPECS`, which
	 * has carried all thirteen since long before the game had any of them.
	 * `tools/tests/test_surge_port.py` compares all thirteen.
	 *
	 * ANSWERING IS NOT BUILDING. Nothing creates a Quest, Fallen City or
	 * Cataclysm dungeon yet -- `MakeDungeon` still sets `Basic` on every dungeon
	 * a surge lands. This function says what one WOULD be, which is what the
	 * work that creates them needs first. Issue #1324 has the breakdown.
	 *
	 * THE CATACLYSM EXISTS ONLY AT THE PILLAR. Asked for on any other tier this
	 * answers a spec whose `IsBuilt` is false, because the model has no such row
	 * and raises when asked for one.
	 *
	 * THE THREE NEW KINDS ALL BITE NOTHING, which is the design rather than a
	 * gap: a Quest dungeon never resolves, and a Fallen City and a Cataclysm
	 * stand on a city whose damage is already done.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FCataclysmDungeonSpec SpecFor(ECataclysmDungeonType Type,
										 ECataclysmCityTier Tier);

	/**
	 * How long a Fallen City dungeon's timer runs. `config.DUNGEON_SPECS`.
	 *
	 * LONG ENOUGH THAT IT NEVER FIRES, which is what the model means by the
	 * `(999, 999)` on every Fallen City row: the dungeon has no consequence to
	 * apply, so a timer is meaningless for it. `UCataclysmEmpireRun::
	 * ResolveDungeon` already returns without biting when the city has fallen,
	 * so this is the second of two reasons it does nothing rather than the only
	 * one -- but without it the dungeon would appear in every day report's
	 * resolved list for the rest of the run.
	 */
	static constexpr float FallenCityResolveDays = 999.0f;

	/**
	 * How long a Quest dungeon's timer runs. `config.DUNGEON_SPECS`.
	 *
	 * IT IS A RELOCATION CLOCK AND NOT A BITE SCHEDULE, which is why it is a
	 * flat number rather than derived from the depth. `docs/Cataclysm_GDD_v2.md`
	 * section VIII: a Quest dungeon "does not resolve -- refreshes and may move
	 * to adjacent city". The model says the same with a `(25, 40)` on every
	 * Quest row of `DUNGEON_SPECS` and by reading `spec.resolve_days[0]` for
	 * every kind but `Basic` in `Simulation._make_dungeon`; all four tiers give
	 * 25, so one constant covers them and `test_surge_port.py` checks all four
	 * against it.
	 *
	 * THIS IS NOT THE "RESOLVE TIMERS SCALE WITH DEPTH" RULE BEING BROKEN.
	 * `CLAUDE.md` states that rule about a dungeon whose timer running out
	 * takes something from a city: a deeper one is worth more and slower to
	 * bite. A Quest dungeon takes nothing whenever its timer runs out, so there
	 * is no bite for its depth to be traded against. What the timer decides is
	 * how long the player has before the objective moves, and the design gives
	 * that no relationship to depth at all.
	 *
	 * AND IT IS WHAT THE MOVE HANGS ON. The timer running out puts the dungeon
	 * in the day's `FCataclysmDayReport::Resolved`, and
	 * `UCataclysmEmpireRun::ResolveDungeon` relocates it there -- to an
	 * adjacent exposed city, or nowhere when it has no such neighbour. See
	 * `PickRelocation`. Issue #1324 slice 4.
	 */
	static constexpr float QuestResolveDays = 25.0f;

	/**
	 * The chance a surge lands a Quest dungeon rather than a Basic one.
	 * `config.quest_dungeon_chance`.
	 *
	 * **THIS NUMBER IS THE MODEL'S AND IS KNOWN TO BE SUPERSEDED. READ THIS
	 * BEFORE TREATING IT AS THE DESIGN.** Asked on issue #1324 what a Quest
	 * dungeon's spawn chance should be, the project owner answered on
	 * 2026-09-06, verbatim: "It should depend on the Cataclysm". So the settled
	 * design is a RULE keyed on which Cataclysm sent the wave -- one asking for
	 * ten objectives should offer them more often than one asking for five --
	 * and not a single figure.
	 *
	 * WHY THE RULE IS NOT HERE. **The empire layer has no notion of which
	 * Cataclysm is running.** There is no Cataclysm enum in this module, no
	 * active-Cataclysm list on the scheduler, and no field on
	 * `FCataclysmDungeon` saying which one sent it; the "WHAT IS NOT PORTED"
	 * list above names the other seven Cataclysms as issue #53. A rule keyed on
	 * the Cataclysm would therefore have exactly one reachable value, which is
	 * the same argument issue #1324 itself made against writing a per-kind city
	 * damage table before anything created the kinds. The type has to exist
	 * before the rule that reads it can be verified, so this slice creates the
	 * type and issue #1357 owns the rule.
	 *
	 * SO WHY 12 IN 100 AND NOT SOMETHING ELSE. It is what the model has used
	 * for every balance figure this project holds, and keeping the two in step
	 * is what this whole class is for. It is a PORT and not a choice; nothing
	 * here picked it, and `test_surge_port.py` fails if the two drift.
	 */
	static constexpr float QuestChance = 0.12f;

	/**
	 * The dungeon a city becomes when it falls.
	 *
	 * NOT ROLLED, UNLIKE EVERY OTHER DUNGEON. A wave rolls a depth out of the
	 * spec's range; this one is determined by what the city was carrying.
	 * `docs/Cataclysm_GDD_v2.md` section VIII: "Floor count equals the number of
	 * dungeons that were in the city when it fell (minimum 20/40/60 for
	 * Outpost/Bulwark/Sanctuary)". Those three minimums are exactly the shallow
	 * end of the Fallen City rows in `SpecFor`, so the floor is read from there
	 * rather than written twice.
	 *
	 * SO THE SPEC'S DEEP END IS UNUSED FOR THIS KIND, and deliberately: a
	 * besieged city can exceed it and a quiet one never reaches the shallow end.
	 *
	 * THE MODEL ROLLS THE RANGE INSTEAD, AND IS WRONG. `Simulation._make_dungeon`
	 * draws uniformly from the spec for every kind, so how heavily a city was
	 * besieged changes nothing about the dungeon it leaves. That is issue #1341,
	 * and it is the model's defect rather than this one's -- do not "correct"
	 * this to match it. `tools/tests/test_surge_port.py` holds the divergence
	 * deliberately so that the two disagreeing does not read as drift.
	 *
	 * IT TAKES NOTHING FROM THE CITY EVER AGAIN. The city has already fallen and
	 * there is nothing left to bite.
	 *
	 * @param DungeonsAbsorbed how many dungeons stood on the city when it fell.
	 *        Sets both the floor count and the boss count. At least one in
	 *        practice, because a city falls when a dungeon standing on it
	 *        resolves, and that dungeon is absorbed with the rest.
	 */
	static FCataclysmDungeon MakeFallenCityDungeon(
		int32 DungeonId, const FCataclysmCity& City, int32 Day,
		int32 DungeonsAbsorbed);

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
	 *
	 * WHAT A QUEST DUNGEON DOES DIFFERENTLY, and it is less than it looks. Its
	 * floors come from the Quest row of `SpecFor` rather than the Basic one, and
	 * its timer is the flat `QuestResolveDays` rather than a jittered figure
	 * from its depth. Everything else is the same code: the city's floor
	 * upgrades still move it, it still rolls a sub-type, its walk still costs a
	 * day a floor less whatever the city bought, and it still carries the
	 * design's one boss on the final floor. Nothing about a Quest dungeon says
	 * otherwise, and giving it its own maker would have been two copies of the
	 * shared rules.
	 *
	 * @param bSiegeAllowed whether this city may take a Siege. Passed straight
	 *                      to `RollSubType`; see there. It defaults to allowing
	 *                      one because only `RollWave` knows what is already
	 *                      standing, and this is called on its own by tests.
	 * @param Type          which kind to build. `RollKind` is what decides it
	 *                      for a wave; it defaults to `Basic` because most
	 *                      dungeons are and because the tests that call this on
	 *                      its own predate there being a choice. **A surge
	 *                      rolls `Basic` or `Quest` and nothing else**, so
	 *                      anything else asked for here is built as a `Basic`
	 *                      rather than as a half-made Fallen City -- see the
	 *                      implementation. `MakeFallenCityDungeon` is how a
	 *                      Fallen City is built and nothing builds a Cataclysm.
	 */
	FCataclysmDungeon MakeDungeon(
		int32 DungeonId, const FCataclysmCity& City, int32 Day,
		FRandomStream& Stream, bool bSiegeAllowed = true,
		ECataclysmDungeonType Type = ECataclysmDungeonType::Basic) const;

	/**
	 * Which kind of dungeon the next one a surge lands is.
	 *
	 * A PORT OF THE TWO LINES IN `Simulation.trigger_surge` THAT READ
	 * `quest_dungeon_chance`, and it is deliberately a function of nothing but
	 * the stream so that a test can measure the share it produces without
	 * building a map.
	 *
	 * IT ANSWERS `Basic` OR `Quest` AND NOTHING ELSE. A Fallen City is not
	 * rolled -- it is what a city that fell became -- and nothing creates a
	 * Cataclysm yet; issue #1324 slice 6.
	 *
	 * ONE DRAW, ALWAYS. It takes exactly one number off the stream whichever
	 * answer it gives, so the rolls that follow it do not depend on which kind
	 * came out.
	 */
	static ECataclysmDungeonType RollKind(FRandomStream& Stream);

	/**
	 * Every city the map links one city to, whichever way the link runs.
	 *
	 * THREE KINDS OF LINK AND ALL THREE COUNT: `Outward`, `Inward` and, on the
	 * rim only, `Perimeter`. The first two are the orthogonal lanes; the third
	 * joins rim Outposts along the curved edge of the diamond, carries no lane
	 * and takes no part in exposure.
	 *
	 * **THE PERIMETER LINKS ARE INCLUDED, AND THAT IS A READING RATHER THAN A
	 * RULE.** The design never defines adjacency and `UCataclysmEmpireMap`
	 * states it twice in ways that do not agree: its class comment says
	 * "Adjacency is orthogonal ... a cell's neighbours are strictly the cells
	 * one step further out and one step further in", while
	 * `FCataclysmCity::Perimeter` says those links "exist for adjacency effects
	 * -- a passive that reads 'and its neighbours'". The first is inside the
	 * section about LANES and is answering which cities shield which, which is
	 * an exposure question; this is an adjacency question, so the second was
	 * taken. `docs/DECISIONS.md` records the choice, the disagreement and what
	 * it is worth: a Quest dungeon has somewhere to go 69.1% of the time
	 * without the perimeter and 79.5% with it, over 926 quest timers in 30
	 * simulated campaigns.
	 *
	 * IT ASKS NOTHING ABOUT THE CITIES IT NAMES. Whether they are exposed,
	 * fallen or the Pillar is `PickRelocation`'s business, so that a caller
	 * wanting plain adjacency -- a passive reading "and its neighbours" -- is
	 * not handed a list already filtered for somebody else's rule.
	 */
	static TArray<int32> AdjacentCities(const FCataclysmCity& City);

	/**
	 * Where a Quest dungeon standing on a city moves to when its timer runs
	 * out, or `INDEX_NONE` to stay where it is.
	 *
	 * `docs/Cataclysm_GDD_v2.md` section VIII: a Quest dungeon "does not
	 * resolve -- refreshes and **may move to adjacent city**". The project
	 * owner ruled on 2026-09-06, verbatim "Adjacent, and fix the simulation",
	 * that adjacency is right and that the model moving it anywhere on the map
	 * was the defect. Issue #1324 slice 4.
	 *
	 * THE SAME FILTER A SURGE USES, NARROWED TO NEIGHBOURS. A dungeon may only
	 * stand where a surge could have put one, so a sealed city is no more a
	 * relocation target than it is a spawn target and the Pillar is excluded
	 * for the same reason `UCataclysmEmpireMap::ExposedCities` excludes it. A
	 * fallen city is not exposed, so a Quest dungeon never moves onto the
	 * Fallen City dungeon standing on one.
	 *
	 * **"MAY MOVE" IS SATISFIED BY THE MAP AND NOT BY A DIE ROLL, WHICH IS A
	 * READING.** This moves the dungeon whenever an adjacent exposed city
	 * exists and answers `INDEX_NONE` when none does, which happens for real:
	 * about one quest timer in five fires with nowhere adjacent to go. The
	 * owner was offered a chance-based variant and did not take it, but neither
	 * did they say movement is certain, so this is not settled -- see
	 * `docs/DECISIONS.md`. Adding a chance later needs one constant and no new
	 * structure.
	 *
	 * IT DECIDES, IT DOES NOT ACT, like everything else on this class.
	 * `UCataclysmEmpireRun::ResolveDungeon` is what actually moves the dungeon,
	 * because this class does not own the dungeons standing on the map.
	 *
	 * ONE DRAW WHEN THERE IS A CHOICE AND NONE WHEN THERE IS NOT, which is what
	 * `Simulation._resolve` does -- `self.rng.choice(targets)` is guarded by
	 * `if targets`. A draw taken on the empty case would put the two streams
	 * permanently out of step.
	 *
	 * @param From the city the dungeon is standing on now. Its own identifier
	 *             is never among its neighbours, so this never answers "stay"
	 *             by naming the city it is already on.
	 */
	static int32 PickRelocation(const UCataclysmEmpireMap& Map,
								const FCataclysmCity& From,
								FRandomStream& Stream);

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
