// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmRoster.h"
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
//   - **Quest** hits its host city when its timer runs out, at an ordinary
//     dungeon's size for that city, then refreshes; clearing one is one
//     objective towards challenging the Cataclysm. A surge rolls one in place
//     of a Basic; see `RollKind` and `QuestChance`. Issue #1324 slice 3.
//     **And it may relocate**: when its timer runs out it moves to an adjacent
//     exposed city with a chance of `QuestMoveChance`, and stays when the coin
//     says no or when there is nowhere to go. `PickRelocation` chooses where
//     and `UCataclysmEmpireRun::RelocateQuestDungeon` flips the coin and moves
//     it. Issue #1324 slice 4. Issue #51 is the Hell on Earth quest mechanic
//     for the Demonic Cataclysm specifically, which is a different thing from
//     this kind. **And a fall does not take it**: since 2026-10-21 a Quest
//     dungeon standing on a city that falls moves at once, with no coin, to
//     the city `PickRefuge` answers, and `UCataclysmEmpireRun::CityFell` moves
//     it.
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
	 * A DEEPER CITY STILL TAKES A SMALLER SHARE OF ITSELF: 70 points off an
	 * Outpost's 1,000 is 7%, and 448 off a Sanctuary's 8,000 is 5.6%. Until
	 * 2026-10-20 the ordinary rows were today's divided by 0.7, 100 and 640,
	 * a tenth and 8%.
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
 * WHAT IS ON IT. The sub-type, since issue #1289; which Cataclysm sent it, since
 * issue #1357; and, since issue #41's modifier slice, which of the 117 dungeon
 * modifiers in `game/Data/DungeonModifiers.csv` it carries and how much danger
 * they add -- see `Modifiers` and `ModifierScore`.
 *
 * WHAT IS STILL NOT ON IT: where its bosses stand. `Bosses` counts them and
 * nothing places them, which is the join with issue #40.
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

	/**
	 * Which Cataclysm sent it.
	 *
	 * A PORT OF `Dungeon.source` IN `sim/cataclysm_sim/engine.py`, and the
	 * field this struct's own comment named as missing until issue #1357. Its
	 * absence was the reason two of the project owner's rulings could not be
	 * built: a Quest dungeon's spawn chance "should depend on the Cataclysm",
	 * and the Cataclysm dungeon unlocks when half the ACTIVE Cataclysms have had
	 * their objectives met. Both are rules about which Cataclysm a dungeon
	 * belongs to, and nothing could answer that.
	 *
	 * **IT IS WHAT MAKES AN OBJECTIVE COUNT FOR SOMETHING.**
	 * `UCataclysmEmpireRun::ClearDungeon` reads it to decide which Cataclysm a
	 * cleared Quest dungeon advances. A run's total of quest objectives cannot
	 * answer the unlock rule -- eight cleared for one Cataclysm while four are
	 * active finishes at most one of the two required -- so the total is kept
	 * and this is what the gate reads.
	 *
	 * `None` IS WHAT A DUNGEON NOBODY ASSIGNED CARRIES, and it is reachable: a
	 * dungeon built by hand in a test has one, and so does a Fallen City, which
	 * is made from what a city was carrying rather than sent by anybody. It
	 * counts towards no Cataclysm.
	 *
	 * WHO DRAWS IT IS THE DAY LOOP AND NOT THIS SCHEDULER. The scheduler does
	 * not know which Cataclysms are active; the run does.
	 * `UCataclysmEmpireRun::FireSurge` hands `RollWave` its `RollCataclysm`,
	 * and `RollWave` stamps each dungeon with the answer BEFORE it is made,
	 * since 2026-10-21, because a Quest dungeon's sub-type now depends on its
	 * sender. The model does the same: `Simulation.trigger_surge` sets
	 * `making_for` around `_make_dungeon` and assigns `d.source` after it.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmType Cataclysm = ECataclysmType::None;

	/** Which city it is assaulting. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 CityId = INDEX_NONE;

	/**
	 * That city's tier when the dungeon spawned, which set its depth.
	 *
	 * **IT IS NOT "THE TIER OF THE CITY THIS DUNGEON IS STANDING ON", AND A
	 * QUEST DUNGEON IS WHERE THE TWO COME APART.** It is set once, by
	 * `MakeDungeon` and `MakeFallenCityDungeon`, and neither move touches it --
	 * `RelocateQuestDungeon` on a timer, and `CityFell` when the dungeon's city
	 * falls under it, since 2026-10-21: read the host's tier off the map with
	 * `Map->Find(CityId)->Tier` instead.
	 *
	 * WHY IT MUST NOT MOVE. `BiteScale` is the only thing that reads it, and it
	 * divides the floors the dungeon was rolled with (`Floors` less
	 * `UpgradeFloors`) by the midpoint of `SpecFor(Type, CityTier)`. Both halves
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

	/**
	 * How many of those floors the host city's two floor upgrades added, or
	 * removed when it is negative.
	 *
	 * WHAT IT IS FOR. `BiteScale` sizes a dungeon's hit by `Floors` less this,
	 * which is the depth the dungeon was rolled with. So `DungeonFloorsMore`
	 * and `DungeonFloorsFewer` move the depth, the walk and the resolve timer,
	 * and neither changes what the dungeon takes from its city. The model's
	 * field is `Dungeon.tree_floors` in `sim/cataclysm_sim/engine.py`.
	 *
	 * WHAT THE UPGRADES MOVED AND NOT A SECOND DEPTH, so that nought is right
	 * for every dungeon nobody set it on: one built by hand in a test, a Fallen
	 * City, and one read from a save written before this field existed. Each
	 * of those is sized by all of its floors, as it was before.
	 *
	 * TAKEN AFTER THE CLAMP AT ONE FLOOR. `MakeDungeon` stores the floors it
	 * ended with less the floors it rolled, and not "more less fewer": an
	 * upgrade that would take 500 floors off a 12 floor dungeon moved 11.
	 *
	 * Rulings of 2026-10-13, `docs/DECISIONS.md`.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 UpgradeFloors = 0;

	/**
	 * A Hollow's natural floors when it was made, which its shrinking is
	 * measured from; nought on every other dungeon.
	 *
	 * A PORT OF `Dungeon.hollow_from`, since 2026-10-22. `MakeDungeon` sets it
	 * to the floors it rolled -- `Floors` less `UpgradeFloors` -- once the
	 * sub-type is fixed, and only for a Hollow. THE PLAIN ROLL: the model's
	 * depth is the row times the Void's 1.25 depth multiplier, which is not
	 * ported (#53), as `VoidStacksItAdds` says.
	 *
	 * NOTHING MOVES IT AFTER THAT. `Floors` comes down by one a day, and the
	 * minimum and the stacks a hit adds are read from this and from how far
	 * the natural floors have come down; see `HollowMinimum` and
	 * `HollowStacksOnto`. A Hollow built by hand with nought here never
	 * shrinks, as the model's `_hollow_shrink` skips one.
	 *
	 * SAVED WITH THE DUNGEON, AND NOTHING LOADS IT BACK, because nothing loads
	 * a run's dungeons back at all yet. Issue #753.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 HollowFrom = 0;

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
	 * The dungeon modifiers it carries, as row keys of the modifier table.
	 *
	 * ONE PER DIFFICULTY TIER, DOUBLED FOR A SACRIFICIAL DUNGEON, drawn from the
	 * modifiers of every Cataclysm the run is facing.
	 * `UCataclysmDungeonModifierRules` holds every part of that rule and
	 * `UCataclysmEmpireRun::GiveModifiers` is what calls it. Issue #41.
	 *
	 * ROW KEYS AND NOT NAMES, so that what a modifier is called and what it does
	 * stay in the table. Anything that wants to show a player the name looks it
	 * up; `UCataclysmDungeonModifierTable::FindRow` in the `Cataclysm` module is
	 * the lookup.
	 *
	 * EMPTY IS A REAL ANSWER AND A REACHABLE ONE. A dungeon built by hand in a
	 * test has none, and so does one created by a run whose modifier pool was
	 * never filled -- which is every run in a headless test, because filling it
	 * needs the DataTable.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	TArray<FName> Modifiers;

	/**
	 * How much harder those modifiers make every creature inside it.
	 *
	 * THE SUM OF THEIR DANGER SCORES, which is the design's own definition:
	 * "the sum of the weights on a dungeon is the Modifier Score in the Enemy
	 * Score formula", `docs/Cataclysm_GDD_v2.md` section VIII.
	 *
	 * STORED RATHER THAN SUMMED ON DEMAND, because the danger scores live in the
	 * table and this module cannot read it. `ACataclysmDungeonGameMode::
	 * EnterEmpireDungeon` copies this onto the game mode and
	 * `UCataclysmEnemyScore::FloorIn` reads it back, which is the same route the
	 * sub-type takes.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Cataclysm|Empire")
	float ModifierScore = 0.0f;

	/**
	 * How much of its type's damage this particular dungeon actually deals.
	 *
	 * A DUNGEON ROLLED DEEPER HITS HARDER, in proportion to how deep it was
	 * rolled against a typical one of its kind on that tier of city.
	 * `Simulation._resolve` computes it as
	 * `natural_floors / ((least + most) / 2)`, so a dungeon rolled at exactly
	 * typical depth scales by one.
	 *
	 * THE FLOORS A CITY UPGRADE ADDED OR REMOVED ARE NOT COUNTED. It reads
	 * `Floors` less `UpgradeFloors`. A dungeon made deeper by
	 * `DungeonFloorsMore` hits as hard as it would have without the upgrade,
	 * and one made shallower by `DungeonFloorsFewer` hits no less. Rulings of
	 * 2026-10-13, `docs/DECISIONS.md`.
	 *
	 * IT SCALES POINTS AND NOT A SHARE, since issue #1331. The name is the
	 * event -- a dungeon biting a city -- rather than the old fraction.
	 */
	float BiteScale() const;

	/**
	 * Whether this dungeon's timer adds Void stacks to its city instead of
	 * taking defence and people: an ordinary dungeon the Void sent.
	 *
	 * A PORT OF `Simulation._adds_void_stacks`, since 2026-10-22. The model asks
	 * whether the sender's attack pattern erases cities, and only the Void's
	 * does; the game has no patterns (#53), so this names the Void.
	 *
	 * ORDINARY ONLY. A Void Siege's daily bite stays damage, ruled by the
	 * coordinating session on 2026-10-16, and that bite is not this timer. A
	 * Void Quest dungeon is a Hollow since 2026-10-22, and its hit adds stacks
	 * by a rule of its own, `HollowStacksOnto`, in the Quest block of
	 * `UCataclysmEmpireRun::ResolveDungeon`; this still answers false for it.
	 * Until then it took the ordinary Quest hit. A Fallen City is sent by
	 * nobody.
	 */
	bool AddsVoidStacks() const;

	/**
	 * How many Void stacks this dungeon adds when its timer runs out: 1, 2 or 3,
	 * by which third of its row's floor range its natural floors fall in.
	 *
	 * A PORT OF `Simulation._void_stacks_for`, `1 + clamp(int(3 x (natural -
	 * least) / (most - least)), 0, 2)`, and 1 when the range is a single depth.
	 * Ruled by the coordinating session on 2026-10-16.
	 *
	 * THE ROW'S PLAIN RANGE, AND THE MODEL'S IS 1.25 TIMES IT. The model rolls a
	 * Void dungeon's depth from the row times the Void pattern's `floors_mult`
	 * and reads the thirds of that same scaled range. The game rolls the Void's
	 * dungeons from the plain row, because no Cataclysm's depth multiplier is
	 * ported (#53), so the thirds are of the range the depth was rolled from.
	 * Ruled by the coordinating session on 2026-10-22: the multiplier stays with
	 * issue #53.
	 *
	 * BY THE NATURAL FLOORS, `Floors` less `UpgradeFloors`, the depth
	 * `BiteScale` reads: a city's floor upgrades move no stacks, as they move no
	 * damage. A depth outside the row, which only a dungeon built by hand has,
	 * is clamped into 1 to 3.
	 */
	int32 VoidStacksItAdds() const;

	/**
	 * Whether this is a Hollow: the Void's Quest dungeon.
	 *
	 * A PORT OF `Simulation._hollow_adds_stacks`, without its switch, since
	 * 2026-10-22: a Quest dungeon whose sub-type is `Hollow`. Only the Void's
	 * Quest dungeons are, by `UCataclysmSurgeScheduler::QuestSubTypeFor`, and
	 * nothing rolls one.
	 */
	bool IsHollow() const;

	/**
	 * The fewest natural floors this Hollow shrinks to: half of `HollowFrom`,
	 * rounded up, and never below one.
	 *
	 * A PORT OF `Simulation.hollow_floor_minimum`, `max(1, ceil(hollow_from x
	 * hollow_floor_share_kept))`; the share is
	 * `UCataclysmSurgeScheduler::HollowShareKept`. A Hollow made with 20 floors
	 * stops at 10, one made with 21 at 11, and one made with 1 at 1.
	 */
	int32 HollowMinimum() const;

	/**
	 * How many more floors this Hollow will lose before it stops shrinking.
	 *
	 * ITS NATURAL FLOORS LESS ITS MINIMUM, AND NEVER SO MANY THAT IT WOULD BE
	 * LEFT WITH NONE: the model's `min(natural - minimum, floors - 1)` in
	 * `Simulation._hollow_shrink`. A city whose upgrade took floors off leaves
	 * a Hollow shallower than its natural depth, and a dungeon of no floors
	 * cannot be walked. Nought for anything that is not a Hollow, and for a
	 * Hollow with no `HollowFrom`.
	 *
	 * SO `Floors` LESS THIS IS WHERE IT STOPS, which the city screen names.
	 */
	int32 HollowFloorsLeftToLose() const;

	/**
	 * How many Void stacks this Hollow's hit adds to a city carrying
	 * `CityStacks`: 1 at its full depth, rising evenly to 3 at its minimum,
	 * and held so that a city below
	 * `UCataclysmSurgeScheduler::HollowCannotEraseBelow` stacks is left one
	 * short of erasure.
	 *
	 * A PORT OF `Simulation._hollow_stacks_for` AND
	 * `Simulation.hollow_hit_stacks`, since 2026-10-22: `1 + 2 x lost / span`,
	 * rounded half up, where `span` is `HollowFrom` less the minimum and `lost`
	 * is how far its natural floors have come down from `HollowFrom`, held
	 * between nought and `span`. 3 for a Hollow that cannot shrink at all.
	 *
	 * THE OWNER, 2026-10-16, ON THE VOID'S QUEST DUNGEONS, VERBATIM: "So, maybe
	 * Hollow works, but we shouldn't just flat out erase a city if they player
	 * can't get to it in time." So a city carrying fewer than
	 * `HollowCannotEraseBelow` stacks takes at most enough to reach one short of
	 * `UCataclysmEmpireMap::VoidStacksToErase`. A city at 8 or 9 is not held,
	 * and the hit can erase it. Never below nought.
	 *
	 * NOT `VoidStacksItAdds`, which an ordinary Void dungeon adds by the thirds
	 * of its row. A Hollow is a Quest dungeon and its hit is not sized by a row.
	 *
	 * THE NAME DOES NOT SAY "FLOORS" ON PURPOSE.
	 * `tools/tests/test_a_quest_dungeons_timer_hits_in_the_model_and_in_the_game.py`
	 * forbids that word in the Quest block of `ResolveDungeon`, which calls
	 * this, because a Quest hit is not sized by depth. A Hollow's is, by
	 * ruling, and only through here.
	 */
	int32 HollowStacksOnto(int32 CityStacks) const;

	/**
	 * Whether this dungeon is an ordinary one: the kind whose timer running out
	 * takes its OWN row's damage from its host, scaled by depth, and which then
	 * leaves the map.
	 *
	 * **SINCE 2026-10-13 A FALSE HERE NO LONGER MEANS THE CITY PAYS NOTHING.** A
	 * Quest dungeon answers false and its timer still hits its host, with the
	 * ordinary row of the host's tier: `UCataclysmEmpireRun::ResolveDungeon`
	 * does that inside the block that asks this. This used to be headed
	 * "Whether this dungeon's timer running out costs its host city anything".
	 * The paragraphs below were written then and are kept for their reasoning.
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
	 * HAPPENS. A Quest dungeon's timer is MEANT to run out, and since
	 * 2026-10-19 it comes from the depth like any dungeon's -- see
	 * `MakeDungeon`. When it does the host is hit, the timer refreshes and
	 * the dungeon may move to an adjacent city. Issue #1324 slice 4. What this
	 * also still decides is a death: dying in a dungeon resolves it only when
	 * this answers true.
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
 *   - **The EIGHT ATTACK PATTERNS, though no longer the other seven Cataclysms
 *     themselves.** Which Cataclysms a campaign faces, and which one sent a
 *     given dungeon, are ported since issue #1357: `UCataclysmRoster` draws the
 *     active set and `UCataclysmEmpireRun::TriggerSurge` stamps every dungeon in
 *     a wave with one. What is still generic is HOW they attack. The simulation
 *     gives each its own shape -- a Death wave is a swarm of shallow dungeons, a
 *     Celestial one is a handful of deep ones -- and weights how much of a wave
 *     each contributes by that shape. Everything below is still uniform pressure
 *     on the frontier weighted by tier, and the Cataclysm a dungeon carries
 *     changes nothing about how deep it is, and nothing about where it lands
 *     unless it is the Void: since 2026-10-22 a wave slot the Void sends
 *     weighs each city by its Void stacks as well, `TargetWeightForSender`.
 *     Issue #53.
 *   - **The Demonic Cataclysm's rifts, which ignore the frontier entirely.**
 *     That is the one pattern the vertical slice would actually use, and it
 *     belongs with the Hell on Earth quest mechanic, issue #51. Issue #1085
 *     records what it would mean for the lane rule.
 *   - **The 117 dungeon modifiers**, which THIS CLASS does not do and
 *     `UCataclysmEmpireRun::GiveModifiers` does. They are drawn where the
 *     Cataclysm that sent a dungeon is stamped, and for the same reason: the
 *     draw needs the run's active Cataclysms and its difficulty tier, and this
 *     scheduler knows neither. Issue #41.
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

	/**
	 * Days between surges before any escalation. `config.surge_interval_days`.
	 *
	 * 90 SINCE 2026-10-13, AND 120 UNTIL THEN. It went into the model together
	 * with `DungeonsPerSurge` below and with two rules: an ordinary dungeon
	 * leaves after it hits its city, and a Quest dungeon's timer hits its
	 * city. `docs/DECISIONS.md`, 2026-10-13.
	 */
	static constexpr float IntervalDays = 90.0f;

	/**
	 * Dungeons in every surge after the first, before any escalation.
	 * `config.surge_dungeon_count`. The first is `OpeningDungeonsPerSurge`.
	 *
	 * 8 SINCE 2026-10-20, 5 FROM 2026-10-13, AND 4 UNTIL THEN. The model went
	 * to 8 on 2026-10-16 and the game followed in its own layer. The owner,
	 * 2026-10-16, verbatim: "I want surges to summon a lot more dungeons. Or
	 * maybe the initial surge spawns more and then the first real surge is 6
	 * to 10? Idk, but 6 is boring." and "the obvious solution here is to
	 * adjust the damage dungeons do." The coordinating session chose 8, with
	 * an opening wave of 12 and every ordinary row of `SpecFor` at 0.7 of its
	 * damage, under the owner's delegation of numbers. `docs/DECISIONS.md`,
	 * 2026-10-16, has the measurement, and 2026-10-20 the game's layer.
	 *
	 * ON HERETIC THAT IS trunc(8 x 1.25) = 10, a quarter more exactly. At 5
	 * it was trunc(6.25) = 6, a fifth more. See `HereticDungeonMultiplier`.
	 */
	static constexpr int32 DungeonsPerSurge = 8;

	/**
	 * Dungeons in the first surge of a run, the one the design's "A Surge is
	 * triggered at run start" fires. `config.surge_opening_count`.
	 *
	 * 12 SINCE 2026-10-20; UNTIL THEN THE FIRST WAVE WAS `DungeonsPerSurge`
	 * LIKE EVERY OTHER. The owner's words of 2026-10-16 are quoted on
	 * `DungeonsPerSurge` above, among them "maybe the initial surge spawns
	 * more". The coordinating session chose 12 under the owner's delegation
	 * of numbers, and the model has had it since that day.
	 *
	 * READ AT `SurgeIndex` 0 AND AT NO OTHER, as `Simulation.surge_count`
	 * reads it at `surge_index` 0. The ceiling `MostDungeonsPerSurge` and the
	 * lethality mode's multiplier apply to it as to any wave, so a Heretic run
	 * opens with trunc(12 x 1.25) = 15.
	 */
	static constexpr int32 OpeningDungeonsPerSurge = 12;

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
	 *
	 * NOT EVERY FALL DOES. `IsDueOnCityFall` asks this flag and also that the
	 * last surge is at least `LeastIntervalDays` behind. Issue #2380.
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
	 * Whether a board with no dungeons on it fires a surge.
	 * `config.surge_on_empty_board`.
	 *
	 * THE PROJECT OWNER RULED IT ON 2026-09-07, verbatim: "Anytime there are no
	 * longer dungeons on the board, a surge happens." And on whether it
	 * replaces the cadence, verbatim: "it does not replace the surge cadence.
	 * It is there to ensure pressure remains." Issue #1406 built it in the
	 * simulation and issue #1414 built it here.
	 *
	 * ANY EMPTY BOARD, WHATEVER EMPTIED IT, and every kind of dungeon counts as
	 * standing: a Quest dungeon, a Fallen City and the dungeon the player is
	 * inside all keep the board from being empty. `Simulation.board_is_empty`
	 * is `not self.dungeons` and reads no kind.
	 *
	 * `LeastIntervalDays` IS THE ONLY BRAKE ON IT, and `IntervalDays` keeps
	 * running: whichever comes first fires. See `IsDueOnEmptyBoard`.
	 */
	static constexpr bool bSurgeOnEmptyBoard = true;

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

	/**
	 * How much more a wave slot the Void sends wants a city at
	 * `UCataclysmEmpireMap::VoidStacksToErase` stacks than one with none, less
	 * one. A port of the `1.0 x` in `patterns._void`.
	 *
	 * ONE, SO AT MOST TWICE: the owner's choice of 2026-10-18 among 2, 3 and 5
	 * times and the lost-defence reading the Void had before, verbatim "Stacks,
	 * up to 2x". `docs/DECISIONS.md` has the measured table.
	 * `tools/tests/test_void_stacks_in_the_model_and_in_the_game.py` compares
	 * this with the model.
	 */
	static constexpr float VoidTargetPullAtFull = 1.0f;

	/**
	 * The share of its natural floors a Hollow keeps: it shrinks to half of
	 * what it was made with, rounded up. `config.hollow_floor_share_kept`.
	 *
	 * A COPY, AND THE SIMULATION IS THE ORIGINAL, as are the two below.
	 * `tools/tests/test_the_hollow_in_the_model_and_in_the_game.py` reads all
	 * three back out of this header and compares them with the model's.
	 */
	static constexpr float HollowShareKept = 0.5f;

	/**
	 * How many floors a Hollow loses a day, until it reaches its minimum.
	 * `config.hollow_floors_lost_per_day`. See
	 * `UCataclysmEmpireRun::ShrinkHollows`.
	 */
	static constexpr int32 HollowFloorsLostPerDay = 1;

	/**
	 * Below this many Void stacks a Hollow's hit cannot erase a city: it adds
	 * at most enough to leave the city one short of
	 * `UCataclysmEmpireMap::VoidStacksToErase`.
	 * `config.hollow_hit_cannot_erase_below`. The owner's sentence is quoted at
	 * `FCataclysmDungeon::HollowStacksOnto`.
	 */
	static constexpr int32 HollowCannotEraseBelow = 8;

	/**
	 * How much a wave slot sent by this Cataclysm wants this city.
	 *
	 * `TargetWeightFor` the city's tier, for every sender but the Void. For the
	 * Void, that times `1 + VoidTargetPullAtFull x min(1, stacks / ten)`: a port
	 * of `patterns._void`, since 2026-10-22. The Void fixates on what it has
	 * already hurt, and what it hurts now is stacks, not defence.
	 *
	 * A VOID SLOT ON A CITY WITH NO STACKS WEIGHS EXACTLY WHAT ANY SLOT DOES, the
	 * same float and not merely an equal number, so a wave against an empire
	 * carrying no stacks lands where it always did whoever sends it.
	 *
	 * THE OTHER CATACLYSMS' AIMS ARE NOT PORTED, issue #53; they weigh by tier
	 * alone, as every slot did before this.
	 */
	static float TargetWeightForSender(const FCataclysmCity& City,
									   ECataclysmType Sender);

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
	 * COW LEVEL IS THE RAREST AT 7, which is what its reward deserves: the
	 * design document gives it "ridiculous amounts of loot" for double the walk.
	 * It was 4 in 100 while a third of dungeons were plain, and #1293 set it to
	 * 7 when that third was redistributed.
	 *
	 * SIEGE IS 7.5 AND NOT 15 SINCE 2026-09-06. The owner ruled on issue #1349,
	 * verbatim, "Halve the rate and cut the growth", after the dose-response
	 * curves in `sim/analyse_siege_dose.py` showed a Siege at 15 in 100 taking
	 * the earned Cataclysm dungeon from 84% of campaigns to 8%. The 7.5 it gave
	 * up went to the other sub-types, so the table still totals 100 and a Siege
	 * is still the second rarest thing a surge makes.
	 * `UCataclysmEmpireRun::SiegeDamageGrowthPerDay` carries the other half of
	 * that ruling, 10 points a day down to 2.5.
	 *
	 * AND THE COW LEVEL DID NOT RIDE UP WITH IT. Sharing the 7.5 across all six
	 * carried the Cow Level to 7.617647, above the Siege, which made the Siege
	 * the rarest sub-type in the game and reversed the order the paragraph
	 * above gives a reason for. Nobody chose that; it fell out of the
	 * arithmetic. On issue #1369 the owner delegated the answer with one
	 * constraint, verbatim -- "Your call, but the cow level should be pretty
	 * rare" -- and the Cow Level was put back to the 7 that #1293 decided. THE
	 * SIEGE'S 7.5 IS UNTOUCHED: that is the owner's own ruling.
	 *
	 * THE FIVE ARE ROUNDED TO ONE DECIMAL rather than rescaled exactly, which is
	 * the form the owner chose for this same table on 2026-09-05: "clean round
	 * numbers" over an exact proportional rescale. Siege 7.5 and Cow Level 7.0
	 * leave 85.5 for the other five, whose old shares total 78, so the scale is
	 * 85.5 / 78 = 1.096154 and Sacrificial takes the rounding remainder. The six
	 * that are not the Siege still sum to exactly 92.5.
	 *
	 * THESE ARE A COPY AND THE SIMULATION IS THE ORIGINAL, the same arrangement
	 * every other constant in this file is in.
	 * `tools/tests/test_dungeon_subtype_port.py` reads each one back out of this
	 * header and fails if either side moves.
	 */
	static constexpr float SpawnWeightTimed			= 19.7f;
	static constexpr float SpawnWeightHorde			= 19.7f;
	static constexpr float SpawnWeightSiege			=  7.5f;
	static constexpr float SpawnWeightCowLevel		=  7.0f;
	static constexpr float SpawnWeightElite			= 16.4f;
	static constexpr float SpawnWeightVolatile		= 16.4f;
	static constexpr float SpawnWeightSacrificial	= 13.3f;

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
	 * What to call a KIND of dungeon when a person has to read it.
	 *
	 * `SubTypeName` ABOVE, FOR THE OTHER HALF OF THE PAIR, and a plain switch
	 * for the same reason: the spelling shown to a person is a decision, and
	 * taking it from the identifier means renaming the identifier silently
	 * changes what is on screen. `Fallen City` is two words here and one
	 * identifier there, which is exactly that difference.
	 *
	 * `Basic` ANSWERS AN EMPTY STRING, not the word "Basic", matching `None` in
	 * `SubTypeName`. Most dungeons are ordinary and a reader wants "dungeon 3 on
	 * Outpost (1,2)" for one of those; what has to stand out is the Quest
	 * dungeon that earns an objective and the Fallen City that is a lost city.
	 * `UCataclysmEmpireRun::Describe` is the caller. Issue #1324 slice 5.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString KindName(ECataclysmDungeonType Type);

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
	 * The one sub-type a Cataclysm's Quest dungeons take, or `None` for "roll
	 * one as any dungeon does".
	 *
	 * A PORT OF `config.QUEST_SUBTYPE_BY_CATACLYSM`, FOR SIX OF ITS EIGHT. The
	 * owner, 2026-10-16, verbatim: "Each cataclysm's quest dungeons will be one
	 * specific type." Death's are Horde, Demonic's Elite, Pestilence's Timed,
	 * Famine's Sacrificial and Chaos's Volatile, since 2026-10-21, and the
	 * Void's Hollow since 2026-10-22.
	 *
	 * TWO ANSWER `None` ON PURPOSE, AND THE MODEL NAMES ONE FOR EACH OF THEM.
	 * War's are Sieges in the model and roll here, because the owner has ruled
	 * War out of the game. Celestial's are Ascendant in the model, and that
	 * sub-type is not in `ECataclysmDungeonSubType` yet. `None` itself, the
	 * Cataclysm nobody named, answers `None` too: a Quest dungeon made without
	 * a sender rolls, as the model's does.
	 * `tools/tests/test_two_cataclysms_quest_sub_types_are_in_the_model_and_not_yet_in_the_game.py`
	 * holds the two; `docs/DECISIONS.md`, 2026-10-21 and 2026-10-22, has the
	 * layers.
	 *
	 * IT DRAWS NOTHING. `MakeDungeon` still takes the sub-type draw and then
	 * discards it for a fixed Quest dungeon; see there for why.
	 */
	static ECataclysmDungeonSubType QuestSubTypeFor(ECataclysmType Cataclysm);

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
	 * THE THREE NEW KINDS ALL CARRY NO DAMAGE OF THEIR OWN, which is the design
	 * rather than a gap. A Fallen City and a Cataclysm stand on a city whose
	 * damage is already done. A Quest dungeon's rows are nought as well, and
	 * since 2026-10-13 its timer hits its host with the BASIC row of the host's
	 * tier, which `UCataclysmEmpireRun::ResolveDungeon` asks this function for.
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
	 * The chance a Quest dungeon with somewhere adjacent to go actually goes.
	 * `config.quest_move_chance`.
	 *
	 * **THE DESIGN'S "MAY" IS A DIE ROLL AND NOT ONLY THE MAP.** Slice 4 of
	 * issue #1324 read "may move to adjacent city" as satisfied by adjacency:
	 * the dungeon moved whenever an exposed neighbour existed and stayed only
	 * when hemmed in. The project owner ruled otherwise on 2026-09-06, verbatim
	 * "A chance each time", and then chose the number, verbatim: **"0.5"**.
	 *
	 * **BALANCE DID NOT CHOOSE IT.** `sim/analyse_quest_move_chance.py` measured
	 * 0, 0.25, 0.5, 0.75 and 1.0 over 10,000 campaigns in two disjoint seed
	 * blocks and every response variable was flat -- the whole ladder spans 2.4
	 * points of the earned win route in one block against a 2.7-point gap
	 * between the two blocks measuring the same thing, and paired seed by seed
	 * the largest split is p = 0.08. The number came from the ruling. The curve
	 * is recorded on issue #1324 and must not be re-derived to justify it.
	 *
	 * **HALF THE COIN IS NOT HALF THE TIMERS.** About a quarter of quest timers
	 * fire with nowhere adjacent to go whatever the coin says, so a Quest dungeon
	 * actually moves on roughly 38% of its timers. `docs/Cataclysm_GDD_v2.md`
	 * section VIII states both numbers with the conditions and the sample,
	 * because a reader given only one of them is surprised by the other. The
	 * figure recorded with the ruling was 37% and described the game before the
	 * active Cataclysm count was tied to the difficulty tier; re-measured over
	 * four disjoint blocks of 1,000 campaigns it is 38.2%, and the coin itself
	 * is unchanged at a measured take-up of 49.8%.
	 *
	 * WHERE IT IS DRAWN, AND WHY NOT HERE. `PickRelocation` answers WHERE a
	 * dungeon would go and `UCataclysmEmpireRun::RelocateQuestDungeon` decides
	 * whether it goes, in that order: the target first and the coin second. Both
	 * draws are taken whenever there is somewhere to go and neither when there is
	 * not, which is what `Simulation._resolve` does. A coin drawn first, or drawn
	 * on the empty case, would put the two streams permanently out of step.
	 *
	 * IT IS A PORT AND `config.quest_move_chance` IS THE ORIGINAL, the same
	 * arrangement every other constant in this file is in.
	 * `tools/tests/test_surge_port.py` reads it back out of this header and
	 * fails if either side moves.
	 */
	static constexpr float QuestMoveChance = 0.5f;

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
	 * @param DungeonsAbsorbed how many dungeons stood on the city when it fell
	 *        and were absorbed by it. Sets both the floor count and the boss
	 *        count. **IT CAN BE NOUGHT, AND THEN THE DUNGEON STILL HOLDS ONE
	 *        BOSS.** Since 2026-10-21 a Quest dungeon standing on a falling city
	 *        moves away and is not counted, so a city whose only dungeons were
	 *        Quest dungeons -- one whose own Quest hit felled it -- absorbs
	 *        none. This said, until then, that the count was at least one in
	 *        practice. The coordinating session ruled on 2026-10-21 that such a
	 *        city still becomes a Dungeon City with one boss.
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
	 *
	 * AT 0 THE NEXT WAVE IS THE OPENING ONE, `OpeningDungeonsPerSurge`. See
	 * `DungeonsInNextSurge`.
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

	/**
	 * The day the last surge fired, or 0 when none has.
	 *
	 * WHAT THE EMPTY-BOARD RULE AND A CITY'S FALL MEASURE THEIR LEAST INTERVAL
	 * FROM. See `IsDueOnEmptyBoard` and `IsDueOnCityFall`.
	 * `Simulation.days_since_last_surge` reads the last entry of its surge log
	 * and answers the day itself while the log is empty, so the start of the
	 * run counts as day 0; a 0 here gives the same answer.
	 *
	 * WRITTEN BY `RecordSurge` AND BY NOTHING ELSE, so it moves on exactly the
	 * days `NextSurgeDay` and `SurgesFired` move.
	 *
	 * IT IS NOT WRITTEN TO THE SAVE. `UCataclysmRunSave` holds five surge
	 * fields and this is not one of them, so a restored run would measure the
	 * interval from day 0.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	int32 LastSurgeDay = 0;

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
	 * How many days it has been since a surge last fired.
	 * `Simulation.days_since_last_surge`.
	 *
	 * @param Day the day being asked about
	 * @return `Day` itself before any surge has fired.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	float DaysSinceLastSurge(int32 Day) const;

	/**
	 * Whether a surge fires today because nothing stands on the board.
	 *
	 * A PORT OF THE THREE REFUSALS AT THE TOP OF
	 * `Simulation._maybe_surge_on_empty_board`: the rule is on, the board is
	 * empty, and the last surge is at least `LeastIntervalDays` behind.
	 *
	 * A REFUSAL IS A DELAY AND NOT A LOSS. The caller asks again every day, the
	 * board is still empty tomorrow, and this answers true on the first day the
	 * interval allows.
	 *
	 * IT DECIDES AND DOES NOT ACT, like `IsDue`. The run counts its dungeons and
	 * passes the number in, because this class does not own them.
	 *
	 * @param Day              the day being asked about
	 * @param DungeonsStanding how many dungeons of any kind are on the map
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsDueOnEmptyBoard(int32 Day, int32 DungeonsStanding) const;

	/**
	 * Whether a city falling on this day fires a surge of its own.
	 *
	 * A PORT OF THE CONDITION AT THE END OF `Simulation._fall`: the rule is on,
	 * and the last surge is at least `LeastIntervalDays` (25) behind. The model
	 * has asked both since issue #1432; the game asked only the flag until
	 * issue #2380.
	 *
	 * A REFUSAL IS A LOSS AND NOT A DELAY, which is the opposite of
	 * `IsDueOnEmptyBoard`. A fall is a moment. The caller asks once, when the
	 * city falls, and a fall refused today is not asked about tomorrow. The
	 * city still falls and still becomes a Dungeon City; only the wave is
	 * withheld.
	 *
	 * SO TWO CITIES FALLING ON ONE DAY BRING ONE WAVE. The first fall's surge
	 * is recorded by `RecordSurge`, which writes today into `LastSurgeDay`,
	 * and the second fall is then nought days behind it. A fall on a day the
	 * clock has already surged brings none for the same reason.
	 *
	 * IT DOES NOT ASK WHAT STANDS ON THE BOARD, unlike the empty-board rule.
	 *
	 * IT DECIDES AND DOES NOT ACT, like `IsDue` and `IsDueOnEmptyBoard`.
	 *
	 * @param Day the day the city fell on
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	bool IsDueOnCityFall(int32 Day) const;

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
	 * EACH SLOT'S SENDER IS DRAWN HERE, BEFORE THE SLOT IS ROLLED, since
	 * 2026-10-22, because the Void aims by its stacks: a slot the Void sends
	 * weighs each city by `TargetWeightForSender`, and the other senders by
	 * tier alone. Ruled by the coordinating session that day. One sender is
	 * drawn per city landed on, immediately before that slot's draw from
	 * `Stream`, so exactly as many are drawn as `RollWave` used to draw after
	 * the targets were picked, and in the same order. The sender comes from the
	 * run's Cataclysm stream, not from `Stream`, so asking moves no draw here.
	 *
	 * NO STACKS ANYWHERE, NO DRAW MOVES. A Void slot whose every candidate
	 * carries no stacks rolls against the very weights and total an ordinary
	 * slot does. Only when some candidate carries stacks is the slot's total
	 * worked out afresh from the candidates still holding room.
	 *
	 * @param DungeonsPerCity how many dungeons already stand on each city,
	 *                        indexed by city identifier. Empty means the caller
	 *                        does not know, and then no cap is applied --
	 *                        `UCataclysmEmpireRun` owns the dungeons and this
	 *                        class deliberately does not.
	 * @param DrawCataclysm  which Cataclysm sends each slot, asked once per city
	 *                       landed on, before that slot is rolled. EMPTY BY
	 *                       DEFAULT, and then every slot is sent by `None` and
	 *                       weighs by tier, as before this was added.
	 * @param OutSenders     when given, emptied and then filled with each landed
	 *                       slot's sender, one per entry of the answer and in
	 *                       the same order. `RollWave` stamps them.
	 * @return one city identifier per dungeon, in the order they were rolled.
	 *         Empty when nothing is exposed, which cannot happen on a built map
	 *         because the rim always is. SHORTER THAN `Count` only when every
	 *         exposed city has reached its cap.
	 */
	TArray<int32> PickTargets(
		const UCataclysmEmpireMap& Map, int32 Count, FRandomStream& Stream,
		const TArray<int32>& DungeonsPerCity = TArray<int32>(),
		const TFunction<ECataclysmType()>& DrawCataclysm =
			TFunction<ECataclysmType()>(),
		TArray<ECataclysmType>* OutSenders = nullptr) const;

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
	 * floors come from the Quest row of `SpecFor` rather than the Basic one.
	 * Its timer comes from its depth like any dungeon's, jitter and all; until
	 * 2026-10-19 it was a flat 25 days, and the owner ruled on 2026-10-16 that
	 * it follow the depth. Everything else is the same code: the city's floor
	 * upgrades still move its floors and so its timer, it still takes the
	 * sub-type draw, its walk still costs a day a floor less whatever the city
	 * bought, and it still carries the design's one boss on the final floor.
	 * Giving it its own maker would have been two copies of the shared rules.
	 *
	 * AND ONE THING MORE SINCE 2026-10-21: ITS SUB-TYPE. A Quest dungeon sent by
	 * a Cataclysm that `QuestSubTypeFor` names one for takes that sub-type
	 * rather than what it rolled -- a Famine Quest dungeon is Sacrificial, a
	 * Death one a Horde. The draw is still taken and its answer discarded, so
	 * the stream moves exactly as it did; see the implementation. A Hollow,
	 * the Void's, also records the floors it rolled in `HollowFrom`, since
	 * 2026-10-22, and takes no draw for it.
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
	 * @param Cataclysm     which Cataclysm is sending it, read only for a Quest
	 *                      dungeon's sub-type; see `QuestSubTypeFor`. IT DOES
	 *                      NOT STAMP THE DUNGEON -- `RollWave` does that -- and
	 *                      it defaults to `None`, which fixes nothing, so every
	 *                      caller that predates it makes what it always made.
	 */
	FCataclysmDungeon MakeDungeon(
		int32 DungeonId, const FCataclysmCity& City, int32 Day,
		FRandomStream& Stream, bool bSiegeAllowed = true,
		ECataclysmDungeonType Type = ECataclysmDungeonType::Basic,
		ECataclysmType Cataclysm = ECataclysmType::None) const;

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
	 * **THIS ANSWERS WHERE, NOT WHETHER.** "May move" is a die roll as well as a
	 * map: the project owner ruled on 2026-09-06, verbatim "A chance each time",
	 * and chose 0.5. The coin is `QuestMoveChance` and it is flipped by
	 * `UCataclysmEmpireRun::RelocateQuestDungeon` AFTER this has answered, so
	 * that a quest timer with somewhere to go costs the stream the same two
	 * numbers whether the dungeon moves or stays. This function is unchanged by
	 * that ruling and deliberately so -- a `PickRelocation` that sometimes
	 * answered `INDEX_NONE` for a city it had targets for would make "nowhere to
	 * go" and "decided not to" indistinguishable to every caller, and
	 * `Cataclysm.Surge.AQuestDungeonRelocatesOnlyToAnAdjacentExposedCity` checks
	 * every refusal against the map for exactly that reason.
	 *
	 * `INDEX_NONE` therefore still means one thing: nothing adjacent qualified.
	 * It happens for real -- about one quest timer in four, before the coin is
	 * even reached. Measured at 22.5% to 24.8% by block over four disjoint
	 * blocks of 1,000 campaigns of the model at the balance report's settings.
	 *
	 * **AND A DUNGEON CARRYING A SIEGE MAY NOT BE SENT TO A CITY THAT ALREADY
	 * HAS ONE.** The design's Siege row says "Max 1 per city"; `RollSubType`
	 * enforced it when a dungeon was created and nothing enforced it when one
	 * moved, so a Quest dungeon that had rolled Siege could walk onto a besieged
	 * city and the city would then take the daily bite twice. The owner ruled on
	 * 2026-09-06, verbatim "Check the limit on arrival too". Issue #1371.
	 *
	 * IT REFUSES THE DESTINATION AND NOT THE MOVE, which is the shape
	 * `RollSubType` already uses on the spawn half of the same rule: a refused
	 * Siege there is spread across the other sub-types rather than dropped, and
	 * a refused city here leaves the remaining neighbours on the table. A
	 * dungeon whose every neighbour is besieged gets `INDEX_NONE` and stays,
	 * which is what the owner named as the other outcome.
	 *
	 * IT DECIDES, IT DOES NOT ACT, like everything else on this class.
	 * `UCataclysmEmpireRun::ResolveDungeon` is what actually moves the dungeon,
	 * because this class does not own the dungeons standing on the map. That is
	 * also why the Siege count is PASSED IN rather than reached for, exactly as
	 * `RollWave` takes `SiegesPerCityNow`: the scheduler must stay able to
	 * answer against a bare map in a test.
	 *
	 * ONE DRAW WHEN THERE IS A CHOICE AND NONE WHEN THERE IS NOT, which is what
	 * `Simulation._resolve` does -- `self.rng.choice(targets)` is guarded by
	 * `if targets`. A draw taken on the empty case would put the two streams
	 * permanently out of step.
	 *
	 * @param From the city the dungeon is standing on now. Its own identifier
	 *             is never among its neighbours, so this never answers "stay"
	 *             by naming the city it is already on.
	 * @param bCarriesSiege whether the dungeon being moved has the Siege
	 *                      sub-type. False for every other dungeon, and then no
	 *                      destination is refused on account of a Siege: the cap
	 *                      is "max 1 Siege per city" and not "one dungeon per
	 *                      besieged city".
	 * @param SiegesPerCityNow how many Siege dungeons already stand on each
	 *                         city, indexed by city identifier. An empty array
	 *                         means the caller did not say, and then nothing is
	 *                         refused -- the same convention `RollWave` uses.
	 */
	static int32 PickRelocation(
		const UCataclysmEmpireMap& Map,
		const FCataclysmCity& From,
		FRandomStream& Stream,
		bool bCarriesSiege = false,
		const TArray<int32>& SiegesPerCityNow = TArray<int32>());

	/**
	 * Where a Quest dungeon standing on a city that has just fallen goes, or
	 * `INDEX_NONE` when nowhere qualifies and it stays on the fallen city.
	 *
	 * **A FALL DOES NOT TAKE A QUEST DUNGEON.** The owner, 2026-10-16,
	 * verbatim: "Quest dungeons don't go away when a city falls. They just move
	 * to an adjacent city. So eventually they should be hitting something."
	 * The simulation took it that day, in `Simulation._fall` and
	 * `Simulation._quest_refuge`; the game follows since 2026-10-21.
	 *
	 * THE RELOCATION'S OWN FILTER, ruled by the coordinating session on
	 * 2026-10-16: a city that is exposed -- so not fallen and not sealed -- and
	 * is not the Pillar, and, when the dungeon carries a Siege, not one already
	 * holding `SiegesPerCity` of them. The same three tests
	 * `PickRelocation` makes.
	 *
	 * **AN ADJACENT CITY FIRST, AND THEN THE NEAREST ONE COUNTED IN LANES.**
	 * When no neighbour qualifies it searches outward ring by ring, breadth
	 * first over every link `AdjacentCities` names -- outward, inward and the
	 * rim's perimeter -- and through any city, fallen and sealed ones
	 * included, and takes the first ring holding a city that qualifies. That
	 * is what makes this differ from `PickRelocation`, whose "nowhere adjacent"
	 * is an answer: a Quest dungeon whose city has fallen cannot stay, so it
	 * goes as far as it must.
	 *
	 * NO COIN. The move is certain, ruled on 2026-10-16; the coin is the
	 * timer's, `QuestMoveChance`, and is not flipped for a fall.
	 *
	 * ONE DRAW WHEN THERE IS A CHOICE AND NONE WHEN THERE IS NOT, as
	 * `Simulation._quest_refuge` does: its `self.rng.choice(targets)` is
	 * guarded by `if targets`. Ties within the ring are broken by that one
	 * draw. `INDEX_NONE` comes only when no exposed city but the Pillar is
	 * left, or every one left already holds a Siege and the dungeon carries
	 * one.
	 *
	 * IT DECIDES AND DOES NOT ACT, like `PickRelocation`.
	 * `UCataclysmEmpireRun::CityFell` moves the dungeon, and counts the Sieges
	 * standing and passes them in for the same reason `RelocateQuestDungeon`
	 * does.
	 *
	 * @param Fallen the city that has just fallen. It must already read as
	 *               fallen, so that the cities it shielded read as exposed.
	 * @param bCarriesSiege whether the dungeon being moved has the Siege
	 *                      sub-type.
	 * @param SiegesPerCityNow how many Siege dungeons stand on each city now,
	 *                         indexed by city identifier. Empty means the
	 *                         caller did not say, and then nothing is refused.
	 */
	static int32 PickRefuge(
		const UCataclysmEmpireMap& Map,
		const FCataclysmCity& Fallen,
		FRandomStream& Stream,
		bool bCarriesSiege = false,
		const TArray<int32>& SiegesPerCityNow = TArray<int32>());

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
	 * add. A dungeon that rolls Siege for a city that already has one has the
	 * same draw spread across the other six sub-types instead. See
	 * `SiegesPerCity`.
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
	 * @param DrawCataclysm  which Cataclysm sends each dungeon, asked ONCE PER
	 *                       CITY LANDED ON, in the order they are landed on.
	 *                       Since 2026-10-22 `PickTargets` asks it, before each
	 *                       slot is rolled, because the Void aims by its stacks;
	 *                       until then this asked it after a target not on the
	 *                       map had been skipped, which no built map has. What it
	 *                       answers is stamped on the dungeon and handed to
	 *                       `MakeDungeon`, because a Quest dungeon's sub-type
	 *                       depends on it.
	 *                       `UCataclysmEmpireRun::FireSurge` passes its own
	 *                       `RollCataclysm`; this class still does not know
	 *                       which Cataclysms a campaign faces. EMPTY BY
	 *                       DEFAULT, and then every dungeon is made and left
	 *                       carrying `None`, as before this was added.
	 */
	TArray<FCataclysmDungeon> RollWave(
		const UCataclysmEmpireMap& Map, int32 Day, int32 FirstDungeonId,
		FRandomStream& Stream,
		const TArray<int32>& DungeonsPerCity = TArray<int32>(),
		const TArray<int32>& SiegesPerCityNow = TArray<int32>(),
		const TFunction<ECataclysmType()>& DrawCataclysm =
			TFunction<ECataclysmType()>()) const;
};
