// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmDungeonModifier.h"

/**
 * What a dungeon is, as much of it as building one of its floors needs.
 *
 * WHY THIS EXISTS. Nothing that generated a floor had ever asked what dungeon
 * the floor belonged to. `FCataclysmFloorRequest` carries a seed, a floor number
 * and a layout; `FCataclysmFloorPopulator::Populate` takes a plan and a density
 * multiplier. Neither can name the dungeon, so no rule that depends on the
 * dungeon could be written anywhere in the floor pipeline, and none was: seven
 * dungeon sub-types were rolled onto every dungeon a surge made and not one of
 * them changed a floor. Issue #41.
 *
 * IT IS THE INPUT SIDE OF THE SEAM AND `FCataclysmFloorBrief` IS THE OUTPUT.
 * `FCataclysmDungeonFloorRules::BriefFor` is the only thing that turns one into
 * the other, so every rule about what a dungeon does to its floors is in one
 * place and each is a function that can be called with a struct built by hand.
 *
 * A COPY RATHER THAN A POINTER TO `FCataclysmDungeon`. The dungeon record lives
 * in `CataclysmEmpire` and carries a city, a timer, resolution damage and a
 * spawn day, none of which a floor has any business reading. This carries the
 * eight fields that decide what a floor holds and nothing else, which is the
 * same reason `FCataclysmDungeonModifier` copies three columns of the modifier
 * table instead of taking the row.
 */
struct CATACLYSM_API FCataclysmDungeonIdentity
{
	/** The dungeon's seed. Every floor of one dungeon is derived from it. */
	int32 DungeonSeed = 0;

	/** How many floors deep it is. The last one is its bottom. */
	int32 TotalFloors = 1;

	/**
	 * What it does differently.
	 *
	 * EVERY DUNGEON A SURGE MAKES HAS ONE. The project owner ruled on
	 * 2026-09-05 that no dungeon is plain, and `UCataclysmSurgeScheduler`'s
	 * spawn weights carry no entry for `None`. `None` is still reachable and
	 * still means "an ordinary floor": it is what a dungeon built by hand in a
	 * test carries, and what pressing Play in `L_Dungeon` gives you.
	 */
	ECataclysmDungeonSubType SubType = ECataclysmDungeonSubType::None;

	/** Which family carves an ordinary floor of it. */
	ECataclysmFloorLayout Layout = ECataclysmFloorLayout::Halls;

	/** The run's difficulty tier, 1 to 8. It sets how many modifiers a floor carries. */
	int32 DifficultyTier = 1;

	/**
	 * The modifiers the dungeon itself drew, as row keys of the modifier table.
	 *
	 * DRAWN ONCE FOR THE WHOLE DUNGEON BY `UCataclysmEmpireRun::GiveModifiers`,
	 * which is where they still belong. What this seam adds is that a floor's
	 * modifiers need not be these: see `FCataclysmFloorBrief::Modifiers`.
	 */
	TArray<FName> Modifiers;

	/** The sum of those modifiers' danger scores. */
	float ModifierScore = 0.0f;

	/**
	 * Every modifier a floor of this dungeon may draw, already narrowed to the
	 * Cataclysms the run is facing.
	 *
	 * NEEDED ONLY BY THE RULES THAT RE-DRAW. A dungeon whose floors all carry
	 * the dungeon's own modifiers never reads it, so leaving it empty is a real
	 * answer and not missing data -- it is what a headless test that never
	 * loaded the modifier DataTable has, and every rule here still works.
	 * `UCataclysmDungeonModifierRules::PoolFor` is what narrows it.
	 */
	TArray<FCataclysmDungeonModifier> ModifierPool;
};

/**
 * What one floor of one dungeon is, decided before anything is built.
 *
 * WHAT IT IS FOR. It is the one thing the floor pipeline reads that knows which
 * dungeon it is building for. `FCataclysmFloorGenerator::Generate` takes its
 * layout, `FCataclysmFloorPopulator::Populate` takes the rest, and
 * `UCataclysmEnemyScore` reads its modifier score off the game mode. A rule that
 * cannot be said in these fields is a rule this seam cannot carry, which is a
 * useful thing to be able to say plainly.
 *
 * A DEFAULT-CONSTRUCTED ONE IS AN ORDINARY FLOOR: Halls, no modifiers, no boss,
 * not a wave. That is what every caller that does not name a dungeon gets, and
 * it is the behaviour the floor pipeline had before this existed.
 */
struct CATACLYSM_API FCataclysmFloorBrief
{
	/** Which floor of the dungeon this describes, counted from 1. */
	int32 FloorNumber = 1;

	/** Which family carves it. */
	ECataclysmFloorLayout Layout = ECataclysmFloorLayout::Halls;

	/**
	 * The modifiers in force on THIS floor, as row keys.
	 *
	 * **THIS IS THE FIELD THAT MADE THE SEAM NECESSARY.** A dungeon's modifiers
	 * were drawn once and stored on the dungeon, so "Dungeon modifiers change
	 * every floor" -- the whole of what the design gives the Volatile sub-type
	 * -- could not be said at all. Making them a per-floor quantity is what
	 * lets it be said, and it is why the rules take a floor number.
	 *
	 * USUALLY THE DUNGEON'S OWN, and that is not a special case being handled:
	 * an ordinary dungeon's floors all carry the same list, which is exactly
	 * what a per-floor quantity that never changes looks like.
	 */
	TArray<FName> Modifiers;

	/** The sum of those modifiers' danger scores, for the enemy score model. */
	float ModifierScore = 0.0f;

	/**
	 * Whether a boss stands at this floor's exit.
	 *
	 * TRUE ON THE LAST FLOOR OF EVERY DUNGEON, which is the design's universal
	 * rule -- "Every dungeon has a boss on the final floor",
	 * `docs/Cataclysm_GDD_v2.md` section VIII -- and true on EVERY floor of an
	 * Elite dungeon, which is the whole of what the design gives that sub-type.
	 *
	 * AT THE EXIT AND NOT SOMEWHERE ON THE FLOOR. "Every floor ends with a boss
	 * fight" names where it stands as well as that it is there: the end of a
	 * floor is the way down off it.
	 */
	bool bBossAtTheExit = false;

	/**
	 * Whether this floor's creatures are one wave rather than several
	 * encounters spread over it.
	 *
	 * THE POPULATION PASS ALREADY NAMED BOTH OPTIONS. Its
	 * `LeastCellsBetweenPacks` exists so "a floor reads as a series of
	 * encounters rather than as one crowd that arrives together" -- its own
	 * words. A wave is the other one of those two, so this is the case that
	 * rule is turned off for, and the creatures are gathered around one point
	 * instead of scattered.
	 */
	bool bOneWave = false;
};

/**
 * What a dungeon does to the floors it generates.
 *
 * **THIS IS THE SEAM, AND IT HAS FOUR USERS.** Three dungeon sub-types and one
 * of the 117 dungeon modifiers. Every one of them is a rule of the form "this
 * dungeon's floors are not like an ordinary dungeon's", and before this existed
 * there was nowhere to write one down. Issue #41.
 *
 * | User | What the design says | Which field carries it |
 * | :-- | :-- | :-- |
 * | Volatile sub-type | "Dungeon modifiers change every floor." | `Modifiers`, `ModifierScore` |
 * | Elite sub-type | "Every floor ends with a boss fight." | `bBossAtTheExit` |
 * | Horde sub-type | "Number of floors equals number of enemy waves." | `Layout`, `bOneWave` |
 * | Unstable Dimensions modifier | "Every time you clear a floor ... a new, random modifier ... on the next floor." | `Modifiers`, `ModifierScore` |
 *
 * TWO USERS SHARE ONE FIELD AND DO DIFFERENT THINGS WITH IT, which is the point
 * of the last row being here. Volatile replaces a floor's modifier list; the
 * Unstable Dimensions modifier adds one to it. Both are per-floor modifier
 * rules and neither could be written before.
 *
 * A PLAIN CLASS OF STATICS OVER PLAIN STRUCTS, the shape
 * `FCataclysmFloorGenerator` and `FCataclysmFloorPopulator` next door already
 * use, and for the reason they give: the automation tests run with `-nullrhi`,
 * so a rule that can be called with a struct built by hand can be swept over a
 * thousand floors and a rule that needs a world cannot. Nothing here spawns
 * anything, reads a DataTable or touches a `UObject`.
 *
 * **IT IS IN THIS MODULE AND NOT IN `CataclysmEmpire`, WHICH IS THE OPPOSITE
 * ARRANGEMENT FROM `UCataclysmDungeonModifierRules`, AND FOR THE OPPOSITE
 * REASON.** Those rules had to be readable BY the empire layer, so they went
 * there and the DataTable row was left behind. These rules have to name
 * `ECataclysmFloorLayout`, which is a fact about how a floor is carved and is
 * declared in `Dungeon/CataclysmFloorPlan.h` here. The dependency runs one way
 * -- `Cataclysm` may use `CataclysmEmpire` and not the reverse,
 * `CataclysmEmpire.Build.cs` says so -- so a rule that names both a sub-type
 * and a layout can only live on this side of the line.
 */
class CATACLYSM_API FCataclysmDungeonFloorRules
{
public:
	// ----------------------------------------------------------------------
	// The whole answer
	// ----------------------------------------------------------------------

	/**
	 * What floor `FloorNumber` of this dungeon is.
	 *
	 * @param Dungeon     what the dungeon is. A default-constructed one gives an
	 *                    ordinary floor
	 * @param FloorNumber counted from 1. Below 1 is clamped, the way
	 *                    `ACataclysmDungeonGameMode::GoToFloor` clamps it
	 */
	static FCataclysmFloorBrief BriefFor(const FCataclysmDungeonIdentity& Dungeon,
										 int32 FloorNumber);

	// ----------------------------------------------------------------------
	// The rules it is made of, each callable on its own so each can be tested
	// ----------------------------------------------------------------------

	/**
	 * Which family carves this floor.
	 *
	 * **HORDE IS ALWAYS ONE OPEN SPACE.** `ECataclysmFloorLayout::Arena`'s own
	 * comment has said "What a Horde floor is, and what a boss floor wants"
	 * since the layout families were built, and nothing had ever made it true.
	 * A wave of creatures in a warren of rooms is not a wave; it is the ordinary
	 * floor with the rooms full.
	 *
	 * EVERY OTHER SUB-TYPE TAKES THE DUNGEON'S OWN LAYOUT, unchanged.
	 */
	static ECataclysmFloorLayout LayoutFor(const FCataclysmDungeonIdentity& Dungeon,
										   int32 FloorNumber);

	/**
	 * Whether a boss stands at this floor's exit.
	 *
	 * TWO RULES, AND THE SECOND DOES NOT REPLACE THE FIRST. Every dungeon has a
	 * boss on its final floor; an Elite dungeon has one on every floor,
	 * including the final one. So an Elite dungeon of eight floors holds eight
	 * bosses and an ordinary one holds one.
	 *
	 * A DUNGEON WITH NO BOTTOM HAS NO LAST FLOOR AND SO HAS NO BOSS, which is
	 * reachable and is not a fault: pressing Play in `L_Dungeon` descends for
	 * ever, and `ACataclysmDungeonGameMode::IsOnTheLastFloor` says the same
	 * thing about the same case. An Elite dungeon still has one on every floor,
	 * because that rule does not depend on where the bottom is.
	 */
	static bool BossAtTheExit(const FCataclysmDungeonIdentity& Dungeon,
							  int32 FloorNumber);

	/**
	 * Whether this floor's creatures arrive as one wave.
	 *
	 * ONE FLOOR IS ONE WAVE, WHICH IS WHAT THE DESIGN SENTENCE COUNTS.
	 * "Number of floors equals number of enemy waves" is an equation between
	 * two counts, and the only reading of it that keeps a Horde dungeon worth
	 * what its depth is worth is one wave per floor. Collapsing twenty floors
	 * into one arena of twenty waves would make the dungeon one floor deep,
	 * and `CLAUDE.md` is explicit that depth and reward are the same axis: a
	 * dungeon made shallower is made poorer, and it would have been made
	 * nineteen twentieths poorer.
	 */
	static bool OneWave(const FCataclysmDungeonIdentity& Dungeon,
						int32 FloorNumber);

	/**
	 * Which modifiers are in force on this floor, and what they are worth.
	 *
	 * THREE RULES IN ORDER, AND THEY COMPOSE RATHER THAN EXCLUDE EACH OTHER:
	 *
	 *   1. **An ordinary dungeon's floor carries the dungeon's own modifiers.**
	 *      Every floor carries the same list, which is the behaviour there was
	 *      before this function existed.
	 *   2. **A Volatile dungeon re-draws them for every floor.** The design
	 *      gives that sub-type one sentence -- "Dungeon modifiers change every
	 *      floor" -- and this is it. The count is the same count the dungeon
	 *      itself drew, `UCataclysmDungeonModifierRules::CountFor`, so a
	 *      Volatile dungeon is not carrying more modifiers than a plain one at
	 *      the same tier; it is carrying different ones on every floor.
	 *   3. **A floor carrying Unstable Dimensions draws one more of its own.**
	 *      `Chaos_Unstable_Dimensions` in `game/Data/DungeonModifiers.csv`:
	 *      "Every time you clear a floor, the very fabric of the dungeon warps.
	 *      A new 'reality' is imposed, granting a new, random modifier to all
	 *      enemies on the next floor." Applied after rule 2, so a Volatile
	 *      dungeon that re-draws Unstable Dimensions onto a floor gets the extra
	 *      on that floor and one that does not, does not.
	 *
	 * **THE DRAW IS SEEDED FROM THE FLOOR AND NOT FROM THE RUN.** Its stream
	 * comes from the dungeon's seed and the floor number, so the same floor of
	 * the same dungeon always carries the same modifiers -- a player who leaves
	 * and returns finds what they left -- and nothing here advances
	 * `UCataclysmEmpireRun::ModifierStream`. Taking these draws from that stream
	 * would shift every later draw the run makes, which the modifier slice's own
	 * `TheDrawDoesNotShiftAnythingElseTheRunRolls` was written to prevent.
	 *
	 * **AN EMPTY POOL LEAVES THE DUNGEON'S OWN MODIFIERS ALONE.** A run whose
	 * modifier pool was never filled -- every headless test, because filling it
	 * needs the DataTable -- gets a Volatile dungeon whose floors all carry the
	 * dungeon's list. That is the honest answer: there is nothing to re-draw
	 * from, so nothing changes.
	 *
	 * @param Dungeon        what the dungeon is
	 * @param FloorNumber    counted from 1
	 * @param OutModifiers   the row keys in force on this floor
	 * @param OutScore       the sum of their danger scores
	 */
	static void ModifiersFor(const FCataclysmDungeonIdentity& Dungeon,
							 int32 FloorNumber,
							 TArray<FName>& OutModifiers,
							 float& OutScore);

	// ----------------------------------------------------------------------

	/**
	 * The row key of the dungeon modifier that adds one more modifier per floor.
	 *
	 * A ROW KEY AND NOT A NEW ENUM, because the 117 modifiers are data and one
	 * of them behaving differently does not make it a different kind of thing.
	 * `Cataclysm.FloorBrief.TheModifierThatChangesAFloorIsARealRowOfTheTable` is
	 * what fails if this key stops naming a row of
	 * `game/Data/DungeonModifiers.csv`.
	 */
	static const TCHAR* UnstableDimensionsKey;

	/**
	 * Mixed into a floor's seed so a per-floor modifier draw is not taken from
	 * the numbers the carve or the creatures used.
	 *
	 * THE SAME DEVICE `FCataclysmFloorPopulator::PopulationSalt` USES and for
	 * the same stated reason: reusing the floor's seed directly would still be
	 * deterministic, and would also mean the first modifier drawn was decided by
	 * a number that already decided the size of a room.
	 */
	static constexpr int32 ModifierSalt = 0x6D6F64;
};
