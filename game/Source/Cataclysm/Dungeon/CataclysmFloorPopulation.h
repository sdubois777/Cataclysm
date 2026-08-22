// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dungeon/CataclysmFloorPlan.h"

/**
 * Which designed creature stands on a floor.
 *
 * SIX OF THE SEVEN THAT ARE BUILT. The Gatekeeper is missing on purpose: it is
 * the boss, the design places one at the end of a dungeon rather than around a
 * floor, and there is no dungeon object holding a floor count for "the end" to
 * mean anything yet. That is issue #41's side of the join. Adding it here would
 * put a boss on every floor of every dungeon.
 *
 * NOT REFLECTED, unlike `ECataclysmFloorLayout` next door. That one is a
 * `UENUM` because `ACataclysmDungeonGameMode` carries a `UPROPERTY` of it, and
 * Unreal's header tool refuses an unreflected type there. Nothing holds one of
 * these in a `UPROPERTY`, so it stays a plain enum, which is what
 * `FCataclysmFloorPlan`'s own comment asks for: add reflection when something
 * actually needs it.
 */
enum class ECataclysmDungeonCreature : uint8
{
	Imp = 0,
	Hellhound,
	Brute,
	AbyssalWarden,
	CorruptedSentinel,
	Succubus,

	/** Not a creature. How many there are, for a test that must cover each. */
	Count
};

/** The creature's name, for a log line and a test failure that says which. */
CATACLYSM_API const TCHAR* CataclysmDungeonCreatureName(ECataclysmDungeonCreature Creature);

/** One creature, and the cell of the floor plan it stands on. */
struct CATACLYSM_API FCataclysmEnemyPlacement
{
	/** Which cell. Always a walkable one that can be reached from the entrance. */
	FIntPoint Cell = FIntPoint(-1, -1);

	/** Which creature stands there. */
	ECataclysmDungeonCreature Creature = ECataclysmDungeonCreature::Imp;

	/**
	 * Which group it belongs to, counted from 0.
	 *
	 * KEPT RATHER THAN DISCARDED because "the floor holds 60 creatures" and "the
	 * floor holds 18 groups of creatures" are different facts and a test needs
	 * both. Without it, ten Imps standing together and ten Imps scattered over a
	 * floor are the same list.
	 */
	int32 Pack = INDEX_NONE;
};

/** Every creature one floor holds, and where each stands. */
struct CATACLYSM_API FCataclysmFloorPopulation
{
	/** Every creature, in the order they were placed. */
	TArray<FCataclysmEnemyPlacement> Enemies;

	/** How many groups they stand in. */
	int32 PackCount = 0;

	/**
	 * The middle of each group, indexed by `FCataclysmEnemyPlacement::Pack`.
	 *
	 * KEPT SO THE SPACING RULE CAN BE TESTED RATHER THAN ASSERTED. The guarantee
	 * `LeastCellsBetweenPacks` gives is about these cells and not about every
	 * creature: two groups four cells apart can each spread three cells, so
	 * members of neighbouring groups may stand side by side. That is a fact about
	 * the rule and it is measured rather than claimed away.
	 *
	 * A CREATURE IS ALWAYS STANDING ON THE MIDDLE. It is the nearest cell to
	 * itself, so it is the first one the group fills.
	 */
	TArray<FIntPoint> PackSites;

	/**
	 * How many creatures the density asked for, before placement.
	 *
	 * RECORDED RATHER THAN HIDDEN, because placement can fall short: the last
	 * group drawn may want ten cells and find six. A test that could not see the
	 * difference between "the density asked for 60" and "60 were placed" could
	 * not tell a floor that is genuinely full from one where placement quietly
	 * gave up.
	 */
	int32 Wanted = 0;

	/** How many of one kind of creature stand on the floor. */
	int32 HowMany(ECataclysmDungeonCreature Creature) const;

	/** How many different kinds of creature stand on the floor. */
	int32 KindsPresent() const;
};

/**
 * Decides which creature stands on which cell of a floor.
 *
 * WHY IT IS A SEPARATE PASS FROM SPAWNING, and separate from the geometry. It is
 * the same order `FCataclysmFloorPlan` is built in and for the same stated
 * reason: **the automation tests run with `-nullrhi`**. A list of cells and
 * creature names can be swept over a thousand seeds in a headless test. Sixty
 * spawned characters with ability systems and animation cannot. Every property
 * worth having -- nothing inside rock, nothing waiting at the entrance, the same
 * seed places the same creatures, a bigger floor holds more of them -- is a
 * property of this list, and pinning them here costs a test each.
 *
 * DETERMINISTIC FROM THE FLOOR'S OWN SEED, which the plan already carries. So a
 * dungeon holds the same creatures when the player leaves and returns, and a
 * floor that goes wrong is reproducible from its seed alone -- the same two
 * requirements issue #40 puts on the layout.
 *
 * WHAT IT DOES NOT DO. It spawns nothing, sets no health, and draws no rarity.
 * `ACataclysmDungeonGameMode::PopulateFloor` turns this list into characters and
 * is where a rarity is rolled, because `UCataclysmEnemyRarity` needs an actor to
 * seed its own draw from.
 */
class CATACLYSM_API FCataclysmFloorPopulator
{
public:
	// ----------------------------------------------------------------------
	// How many
	// ----------------------------------------------------------------------

	/**
	 * How many creatures a floor holds per walkable cell.
	 *
	 * A DENSITY RATHER THAN A COUNT, AND THE GENRE SETTLES THAT MUCH. Diablo II
	 * gives every area a `MonDen` column, described by the Phrozen Keep's column
	 * reference as "a chance in 100000ths that a monster pack will spawn on a
	 * tile" -- a rate per unit of area, not a number per level. The reason to
	 * copy it here is concrete: floor size is rolled per floor between 32 and 48
	 * cells on each axis, so a fixed count would make a small floor crowded and a
	 * large one empty, with nothing saying so.
	 *
	 * THE NUMBER CAME FROM PLAYING IT. It was 0.08 until 2026-08-21, chosen
	 * before anybody had walked a floor. The project owner played one and said
	 * "mob density is way too low", and separately that a floor had "a ton of
	 * empty space" which they then explained was mostly the same complaint:
	 * the size and the layout of a floor were fine, there were simply not many
	 * creatures in a lot of it. They chose three times the old figure. Issue
	 * #809 has the report and #810 the explanation.
	 *
	 * WHAT HELD THE OLD FIGURE DOWN IS PART ANSWERED AND PART STILL OPEN. It
	 * was set below the genre figure because nothing here had measured what
	 * sixty characters with ability systems cost per frame, and an automation
	 * test running with `-nullrhi` still cannot measure it. The project owner
	 * played a floor at 0.08 and said it worked fine, so frame rate was not
	 * what made that floor read badly. **NOBODY HAS PLAYED 0.24 YET**, so the
	 * frame cost of three times as many creatures is unmeasured and #547 is
	 * still the missing written performance budget. `Cataclysm.DungeonEnemyScale`
	 * is how it gets walked back without a rebuild if it turns out to cost too
	 * much.
	 *
	 * WHAT IT PUTS ON A FLOOR, MEASURED over 1,000 seeds of each layout by
	 * `Cataclysm.DungeonEnemies.MeasureWhatAFloorIsPopulatedWith`:
	 *
	 *     Halls     108 to 420 creatures in  20 to 135 groups
	 *     Caverns    84 to 510 creatures in  15 to 155 groups
	 *     Arena      73 to 350 creatures in  16 to 102 groups
	 *
	 * At 0.08 the same sweep gave a third of those counts. For comparison,
	 * Diablo IV's own before-and-after for the Nostrava Deepwood dungeon,
	 * published when patch 1.1.1 raised monster density in answer to players
	 * saying there was too little, moved that dungeon from 120 creatures to
	 * 181. Sources are recorded on issue #40.
	 *
	 * THE TIGHTEST FLOORS NO LONGER QUITE REACH IT, which is new at this
	 * density and is a fact about the geometry rather than a fault. See
	 * `LeastCellsBetweenPacks` below.
	 *
	 * `Cataclysm.DungeonEnemyScale` still multiplies this without a rebuild,
	 * so 0.5 walks the old density and 2 walks twice the new one.
	 */
	static constexpr float EnemiesPerWalkableCell = 0.24f;

	// ----------------------------------------------------------------------
	// Where
	// ----------------------------------------------------------------------

	/**
	 * How far from the entrance, in cells walked, the nearest creature may be.
	 *
	 * EIGHT CELLS IS 32 METRES, and it is set from the creatures rather than
	 * chosen. The furthest any of them notices a target is 15 metres
	 * (`ACataclysmEnemyCharacter::NoticeRadiusCm`, 1,500 cm), and the six placed
	 * here reach 14 metres at most. Twice that means a player who has just
	 * arrived is not already being walked at, and can look at the room before
	 * anything happens.
	 *
	 * MEASURED BY WALKING, NOT BY STRAIGHT LINE. Two cells either side of a wall
	 * are two metres apart and a long way to walk. `CataclysmFloorDistancesFrom`
	 * already answers the walking question and is what decides this.
	 */
	static constexpr int32 LeastCellsFromEntrance = 8;

	/**
	 * How far apart, in cells walked, two groups of creatures must stand.
	 *
	 * FOUR CELLS IS 16 METRES, which is further than any of these creatures can
	 * see. So walking into one group does not, by itself, pull the next one --
	 * a floor reads as a series of encounters rather than as one crowd that
	 * arrives together.
	 *
	 * IT ALSO BOUNDS HOW MANY GROUPS FIT, which is why it is not larger, and at
	 * the density above that bound is now close enough to matter. Each accepted
	 * group claims the cells within this distance of it, so no two groups stand
	 * closer than four cells and the floor can only hold so many of them.
	 *
	 * MEASURED RATHER THAN ESTIMATED, over 1,000 seeds of each layout by
	 * `Cataclysm.DungeonEnemies.MeasureWhatAFloorIsPopulatedWith`: a floor holds
	 * 20 to 135 groups on Halls, 15 to 155 on Caverns and 16 to 102 on Arena.
	 * The comment here used to guess "at most about 25 groups on 800 walkable
	 * cells", which was four times too low because it assumed the claimed
	 * regions do not overlap, and neighbouring groups' claims overlap heavily.
	 *
	 * THE BOUND IS REACHED ON THE TIGHTEST FLOORS. Ten floors in the 360-floor
	 * sweep no longer hold quite the number the density asks for; the worst of
	 * those held 0.850 of it, and over the wider 1,000-seed measurement the
	 * worst was 0.837. That is measured and asserted in
	 * `Cataclysm.DungeonEnemies.ABiggerFloorHoldsMoreCreatures`, which is where
	 * to look first if this constant or the density above is changed again.
	 */
	static constexpr int32 LeastCellsBetweenPacks = 4;

	/**
	 * How far from its own group's middle, in cells walked, a creature may stand.
	 *
	 * THREE CELLS IS 12 METRES. A group of ten Imps needs ten walkable cells and
	 * there are at most 25 within three steps, so a group in a room stands
	 * together while one in a corridor spreads along it. Fewer cells than the
	 * group wants means a smaller group rather than a creature placed in rock --
	 * `FCataclysmFloorPopulation::Wanted` is what makes that visible.
	 */
	static constexpr int32 MostCellsFromPackSite = 3;

	/**
	 * One group in this many is joined by a Succubus.
	 *
	 * A SUCCUBUS IS NOT A GROUP OF ITS OWN, AND THAT IS THE DESIGN RATHER THAN A
	 * TIDINESS CHOICE. Its aura Dominion reaches 8 metres and makes every other
	 * creature inside that radius stronger; it is the only thing in the game that
	 * does. One standing on its own in a room buffs nothing and is a 150-health
	 * creature by itself. So it is placed beside a group that already exists.
	 *
	 * ONE IN FOUR IS A JUDGEMENT. The design says nothing about how often the
	 * creature that strengthens others should appear. Roughly 18 groups on a
	 * typical floor gives four or five of them, so killing the right one first is
	 * a decision the player makes several times a floor rather than once.
	 */
	static constexpr int32 SuccubusEscortsOnePackIn = 4;

	// ----------------------------------------------------------------------
	// What
	// ----------------------------------------------------------------------

	/** One kind of group: which creature, how many of it, and how likely. */
	struct FPackKind
	{
		ECataclysmDungeonCreature Creature = ECataclysmDungeonCreature::Imp;
		int32 Count = 1;
		float Weight = 1.0f;
	};

	/**
	 * Every kind of group a floor draws from.
	 *
	 * GROUPS RATHER THAN CREATURES, WHICH IS BOTH THE GENRE AND THIS PROJECT'S
	 * OWN DESIGN. Diablo II's density column spawns "a monster pack" on a tile
	 * rather than a monster, and `docs/Cataclysm_GDD_v2.md` says of the Imp that
	 * "a single Common enemy is not the threat. A pack is", with a pack being
	 * ten: one Imp takes 48 seconds to kill a geared character and ten take 4.9.
	 * A floor that drew ten separate Imps and scattered them would be a floor of
	 * ten harmless creatures.
	 *
	 * THE COUNTS COME FROM THE CREATURES. Ten Imps because the document names ten
	 * as the pack. One Abyssal Warden because it is the hardest thing in the
	 * roster, at 873 health against an Imp's 87. One Corrupted Sentinel because
	 * it cannot move at all and a second beside it adds a second immobile gun
	 * rather than a second problem. Three Hellhounds and two Brutes are
	 * judgements between those two ends and are the numbers most likely to move
	 * once somebody plays a floor.
	 *
	 * EQUAL WEIGHTS, AND THAT IS A STARTING POINT RATHER THAN A RESULT. Every
	 * kind of encounter is equally likely because nothing in the design yet says
	 * one should be rarer than another. It is not the same as every creature
	 * being equally likely: a group is ten Imps or one Warden, so Imps come out
	 * as roughly three creatures in five and the Warden as one in seventeen.
	 *
	 * WHAT IS DELIBERATELY NOT DECIDED HERE: which creature belongs to which
	 * Cataclysm. All seven designed creatures are Demonic, there is one
	 * Cataclysm's worth of roster, and per-Cataclysm theming is still open on
	 * issue #40.
	 */
	static const TArray<FPackKind>& PackKinds();

	// ----------------------------------------------------------------------

	/**
	 * Mixed into the floor's seed so the creatures are not drawn from the same
	 * numbers the carve used.
	 *
	 * NOT A CORRECTNESS REQUIREMENT AND WORTH SAYING SO. Reusing the floor's seed
	 * directly would still be deterministic, which is the property that matters.
	 * It would also mean the first number this pass draws is a number the carve
	 * already used, and a room's size and the group standing in it would be
	 * related for no reason anybody chose.
	 */
	static constexpr int32 PopulationSalt = 0x706F70;

	/**
	 * Decides what stands where on a floor.
	 *
	 * @param Plan  a built floor. An unbuilt one gives an empty population.
	 * @param Scale multiplies the density. 0 empties the floor, which is what
	 *              `Cataclysm.DungeonEnemyScale 0` is for. Negative is clamped
	 *              to 0, because a negative count is not a floor with anything
	 *              on it.
	 */
	static FCataclysmFloorPopulation Populate(const FCataclysmFloorPlan& Plan,
											  float Scale = 1.0f);
};
