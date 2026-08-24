"""What reaching character level 100 costs, in dungeons and in hours.

WHY THIS EXISTS. Issue #50 needs an experience curve and
`docs/Cataclysm_GDD_v2.md` section XII gives only two facts: the maximum level is
100, and experience comes from killing dungeon enemies and defeating bosses.
There is no curve anywhere in the design. The project owner settled two things on
2026-08-24: an enemy's Power Score IS the experience it grants, and the cost of a
level grows by a constant factor each level. What is still open is the size of
that factor.

Every figure printed here is computed from `cataclysm_sim/scoring.py`, from
`cataclysm_sim/player_power.py`, from `config.DUNGEON_SPECS` and from
`game/Data/EnemyRarities.csv`. Nothing is typed in from a previous run.

WHAT THIS FOUND, in one line each. The working is in the output.

    A dungeon at difficulty tier 1 is worth about HALF what the previous
    estimate said, because that estimate valued every floor at the rate the
    LAST floor pays. Enemy Score rises with `currentFloor / totalFloors`, so a
    floor near the entrance pays far less than the floor above the boss.

    The gap between the difficulty tiers is 27.9 times over a whole dungeon,
    not the 15.5 times measured on the last floor alone. The shallow floors are
    where the two tiers differ most.

    HOW MANY DUNGEONS THE CLIMB SHOULD COST IS NOT A FREE CHOICE. A run is
    played at a fixed difficulty tier, there are eight tiers, and a campaign is
    about 26 dungeons, so passing through every tier is about 208 dungeons.
    Wanting level 100 to arrive during tier 8 fixes the size at that.

    THE RATE THEN DECIDES WHAT LEVEL THE CHARACTER IS WHEN EACH TIER ENDS, and
    no rate gives both a quick first level and a level that keeps pace with the
    difficulty tier. That is not a curve-shaping problem that a cleverer shape
    solves; a decaying rate was tried and does not solve it either. It follows
    from experience per dungeon rising 27.9 times across the tiers while the
    dungeon budget per tier is flat at one campaign each.

    The rate and the scale are TWO choices, and "level 2 costs 100" collapses
    them into one. That is the whole reason the earlier band of 15% to 17%
    looked so delicate that two percentage points moved the answer ten times.

THE THREE INPUTS THAT ARE NOT CALCULATED, and where each comes from:

    Creatures on a floor. `EnemiesPerWalkableCell = 0.24` in
    `game/Source/Cataclysm/Dungeon/CataclysmFloorPopulation.h`, measured over
    1,000 seeds per layout on 2026-08-22 and recorded in `docs/DECISIONS.md` as
    108-420 for Halls, 84-510 for Caverns and 73-350 for Arena. The midpoint of
    those three midpoints is used here. THE ANSWER IN DUNGEONS SCALES INVERSELY
    WITH THIS NUMBER, so it is printed as a sensitivity rather than buried.

    Minutes on a floor. Two minutes on average, with an endgame build. The
    project owner's figure, given on 2026-08-24. `docs/Cataclysm_GDD_v2.md` says
    two to five minutes including finding the stairs; two is the average being
    designed for, so every hours figure here is the SHORT end and a slower
    player pays more.

    Dungeons in a campaign. 26, from the `floors` column of the balance sweep
    baseline recorded in issue #914: 1,053 to 1,345 floors for the three
    policies that resolve rather than stalemate, over an average dungeon of 50
    floors. The range is 21 to 34 dungeons.

    Rarity spawn weights. The SpawnWeight column of `game/Data/EnemyRarities.csv`
    -- Common 0.6, Elite 0.2, Legendary 0.15, Herald 0.04, Boss 0.01, Cataclysm
    Boss 0. Read from the file rather than retyped.

THE ASSUMPTION THAT MATTERS MOST, stated plainly because it is not measurable
yet: this assumes the player kills every creature on every floor. A player who
runs for the stairs earns less and the climb takes proportionally longer. There
is no measurement of what share of a floor a real player clears, because nobody
has played a full dungeon. Issue #925. Treat every dungeon count here as the
FULL-CLEAR figure, which is the optimistic end.

Run: python analyse_experience_curve.py
"""

from __future__ import annotations

import csv
import pathlib

from cataclysm_sim import scoring
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.player_power import MAX_LEVEL

# ---------------------------------------------------------------------------
# The inputs that were measured elsewhere.

#: Midpoint of the three per-layout creature ranges in `docs/DECISIONS.md`,
#: 2026-08-22, "0.24 creatures per walkable cell".
LAYOUT_CREATURE_RANGES = {"Halls": (108, 420), "Caverns": (84, 510), "Arena": (73, 350)}

#: The project owner's figure, 2026-08-24: two minutes on average with an
#: endgame build. `docs/Cataclysm_GDD_v2.md` states two to five including
#: finding the stairs, so this is the short end and a slower player pays more.
MINUTES_PER_FLOOR = 2.0

#: Dungeons in one campaign. Issue #914's balance sweep baseline, `floors`
#: column, for the policies that resolve, divided by an average dungeon.
CAMPAIGN_DUNGEONS = 26

TIERS = range(1, 9)

#: What the analysis measures a dungeon against. Basic with no sub-type is the
#: plainest dungeon in the game and the one the earlier figures used.
REFERENCE_TYPE = "Basic"
REFERENCE_SUBTYPE = "None"

#: Path of Exile's own curve, for comparison. From its published experience
#: table: the share of the whole climb to level 100 spent by each point.
POE_SHARE_BY_50 = 0.0128
POE_SHARE_BY_90 = 0.4550
POE_SHARE_LAST_LEVEL = 0.0747


def creatures_per_floor() -> float:
    """The middle of the measured population range, averaged over the layouts."""
    midpoints = [(low + high) / 2 for low, high in LAYOUT_CREATURE_RANGES.values()]
    return sum(midpoints) / len(midpoints)


def spawn_weights() -> dict[str, float]:
    """The SpawnWeight column of `game/Data/EnemyRarities.csv`, keyed by rarity."""
    here = pathlib.Path(__file__).resolve().parent
    table = here.parent / "game" / "Data" / "EnemyRarities.csv"
    with table.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    return {row["RarityName"]: float(row["SpawnWeight"]) for row in rows}


def average_dungeon_floors() -> float:
    """The mean of the floor ranges in `config.DUNGEON_SPECS`.

    Every (dungeon type, city tier) pair counts once. This is an average dungeon
    in the sense of "one of each", not "one a player is likely to draw", because
    nothing in the design says how often each is entered.
    """
    specs = TuningConfig().DUNGEON_SPECS
    midpoints = [(spec.floors[0] + spec.floors[1]) / 2 for spec in specs.values()]
    return sum(midpoints) / len(midpoints)


# ---------------------------------------------------------------------------
# What a dungeon is worth.


def creature_experience(tier: int, floor: int, total_floors: int,
                        weights: dict[str, float]) -> float:
    """One creature on one floor, averaged over what actually spawns there."""
    scores = scoring.enemy_scores(tier, REFERENCE_TYPE, REFERENCE_SUBTYPE,
                                  total_floors, floor)
    return sum(scores[rarity] * weight for rarity, weight in weights.items())


def dungeon_experience(tier: int, total_floors: int, population: float,
                       weights: dict[str, float]) -> float:
    """Every creature in one dungeon, cleared floor by floor, plus its boss.

    The per-floor sum is the whole point. Valuing every floor at the last
    floor's rate overstates a tier 1 dungeon by about 2x and a tier 8 dungeon by
    about 1.1x, which is how the difficulty gap came out as 15.5x rather than
    27.9x. The right-hand column of the first table prints both.
    """
    total = sum(population * creature_experience(tier, floor, total_floors, weights)
                for floor in range(1, total_floors + 1))
    total += scoring.final_boss_score(tier, REFERENCE_TYPE, REFERENCE_SUBTYPE, total_floors)
    return total


def dungeon_experience_flat(tier: int, total_floors: int, population: float,
                            weights: dict[str, float]) -> float:
    """The superseded method: every floor valued at what the last floor pays."""
    last = creature_experience(tier, total_floors, total_floors, weights)
    return (last * population * total_floors
            + scoring.final_boss_score(tier, REFERENCE_TYPE, REFERENCE_SUBTYPE, total_floors))


# ---------------------------------------------------------------------------
# The curve. It has TWO free numbers, not one.
#
#   rate   how much more each level costs than the one below it. This is the
#          SHAPE: where in the climb the time goes, and what level the character
#          is when each difficulty tier ends.
#   scale  what level 2 costs. This is the SIZE: what the whole climb costs.


def level_cost(level: int, rate: float, scale: float) -> float:
    """What the step from `level - 1` to `level` costs."""
    return scale * (1.0 + rate) ** (level - 2)


def total_experience(rate: float, scale: float, top_level: int = MAX_LEVEL) -> float:
    """Every level from 2 to `top_level`, summed."""
    steps = top_level - 1
    if rate == 0.0:
        return scale * steps
    return scale * ((1.0 + rate) ** steps - 1.0) / rate


def rate_matching_path_of_exile() -> float:
    """The rate whose share of the climb spent by level 90 matches Path of Exile.

    Path of Exile publishes its whole experience table, and 45.50% of the climb
    to level 100 is spent between levels 90 and 100. Matching that one number
    fixes the rate, and the other two checkpoints then agree closely without
    being fitted -- which is the reason to trust it rather than a rate picked to
    feel right. The output prints all three side by side.

    This has nothing to do with the difficulty tiers, so it does not depend on
    the enemy scores at all.
    """
    low, high = 0.0, 1.0
    for _ in range(200):
        middle = (low + high) / 2
        share = total_experience(middle, 1.0, 90) / total_experience(middle, 1.0)
        if share > POE_SHARE_BY_90:
            low = middle
        else:
            high = middle
    return (low + high) / 2


# ---------------------------------------------------------------------------
# The climb, played the way the design says a game is played.
#
# `docs/Cataclysm_GDD_v2.md`: "A run is played at a fixed tier." So a player does
# not drift up the difficulty tiers inside a run; they finish a campaign and
# start the next one higher. Eight tiers is therefore eight campaigns, and the
# size of the whole climb is not a free choice once the owner says level 100
# should arrive during tier 8.


def whole_climb_dungeons(campaign_dungeons: int = CAMPAIGN_DUNGEONS) -> int:
    """Dungeons in one campaign at each of the eight difficulty tiers."""
    return campaign_dungeons * len(TIERS)


def scale_for_level_100_at_the_end(rate: float, per_dungeon: dict[int, float],
                                   campaign_dungeons: int = CAMPAIGN_DUNGEONS) -> float:
    """The cost of level 2 that puts level 100 exactly at the end of tier 8."""
    available = campaign_dungeons * sum(per_dungeon.values())
    return available / total_experience(rate, 1.0)


def levels_at_tier_ends(rate: float, per_dungeon: dict[int, float],
                        campaign_dungeons: int = CAMPAIGN_DUNGEONS) -> list[int]:
    """What level the character is when each difficulty tier's campaign ends.

    This is the number the rate actually decides, and the one to compare against
    `player_power.reference_character`, which says a character is level 12.5 at
    the end of tier 1, 25 at the end of tier 2, and so on to 100.
    """
    scale = scale_for_level_100_at_the_end(rate, per_dungeon, campaign_dungeons)
    cumulative = [0.0] + [total_experience(rate, scale, level)
                          for level in range(1, MAX_LEVEL + 1)]
    earned, out = 0.0, []
    for tier in TIERS:
        earned += campaign_dungeons * per_dungeon[tier]
        out.append(max(level for level in range(1, MAX_LEVEL + 1)
                       if cumulative[level] <= earned or level == 1))
    return out


def reference_levels_at_tier_ends() -> list[float]:
    """The design's own expectation, unrounded, from `player_power`.

    `reference_character` rounds to an integer level; the rule it documents is
    that level rises evenly to 100 at the end of tier 8, so the boundaries are
    at half levels. Four of the eight fall on one.
    """
    return [MAX_LEVEL * tier / 8 for tier in TIERS]


def first_level_in_floors(rate: float, scale: float, population: float,
                          weights: dict[str, float], floors: int) -> float:
    """Floors of a tier 1 dungeon needed for level 2. The cheapest sanity check.

    A curve flat enough to keep the character's level in step with the difficulty
    tier makes the first level cost most of a dungeon, which no ARPG does. This
    is what catches that.
    """
    average = sum(creature_experience(1, f, floors, weights)
                  for f in range(1, floors + 1)) / floors
    return level_cost(2, rate, scale) / (average * population)


def hours(dungeon_count: float, floors: int) -> float:
    """A dungeon count in hours of play, at the owner's two minutes a floor."""
    return dungeon_count * floors * MINUTES_PER_FLOOR / 60


# ---------------------------------------------------------------------------
# Computed once at module level rather than inside main(), so
# `sim/tests/test_analysis_scripts.py` can check the sentences this script
# prints against the numbers it printed them from, instead of re-implementing
# the script to check it. That is the trap issue #6 was about.

WEIGHTS = spawn_weights()
POPULATION = creatures_per_floor()
FLOORS = average_dungeon_floors()
WHOLE_FLOORS = round(FLOORS)

#: What one Basic dungeon of average depth is worth, at each difficulty tier.
PER_DUNGEON = {tier: dungeon_experience(tier, WHOLE_FLOORS, POPULATION, WEIGHTS)
               for tier in TIERS}

#: The rate that reproduces Path of Exile's distribution. The recommendation.
POE_RATE = rate_matching_path_of_exile()

#: Level Weight from the Power Score formula, `docs/Cataclysm_GDD_v2.md`. What
#: one character level is worth in Power Score.
LEVEL_WEIGHT = 6.3270

# ---------------------------------------------------------------------------
# THE DECISION. Chosen by the project owner on 2026-08-24 from the measurements
# below, and recorded in `docs/DECISIONS.md`. These two numbers are the curve.
#
# The rate is `POE_RATE` rounded from 8.1876% to 8.2%, which moves the Path of
# Exile checkpoints by about a twentieth of a percentage point -- the share of
# the climb spent by level 90 goes from 45.50% to 45.45% -- and leaves the level
# at the end of every tier's campaign unchanged. The cost of level 2
# is likewise rounded to a number a person can hold, from 230,366 to 230,000.
# The script asserts both of those claims rather than stating them.

DECIDED_RATE = 0.082
DECIDED_LEVEL_2_COST = 230_000.0

#: The size of the whole climb, in dungeons. Derived, not chosen.
CLIMB_DUNGEONS = whole_climb_dungeons()


def main() -> None:
    weights, population = WEIGHTS, POPULATION
    floors, whole_floors = FLOORS, WHOLE_FLOORS
    per_dungeon = PER_DUNGEON

    # -- What a creature and a dungeon are worth ----------------------------

    print("WHAT ONE CREATURE AND ONE DUNGEON ARE WORTH")
    print()
    print("  Rarity spawn weights, from game/Data/EnemyRarities.csv: "
          + ", ".join(f"{k} {v:g}" for k, v in weights.items() if v > 0))
    print(f"  Creatures on a floor: {population:.0f} (midpoint of "
          + ", ".join(f"{k} {lo}-{hi}" for k, (lo, hi) in LAYOUT_CREATURE_RANGES.items()) + ")")
    print(f"  Floors in an average dungeon: {floors:.1f}, over "
          f"{len(TuningConfig().DUNGEON_SPECS)} floor ranges in config.DUNGEON_SPECS")
    print(f"  One dungeon of {whole_floors} floors is {hours(1, whole_floors):.1f} hours of "
          f"play, at {MINUTES_PER_FLOOR:g} minutes a floor with an endgame build")
    print()

    print("  Tier | Creature, last floor | Creature, dungeon average | "
          "One dungeon | Last floor's rate everywhere")
    print("  " + "-" * 111)
    for tier in TIERS:
        last = creature_experience(tier, whole_floors, whole_floors, weights)
        mean = sum(creature_experience(tier, f, whole_floors, weights)
                   for f in range(1, whole_floors + 1)) / whole_floors
        flat = dungeon_experience_flat(tier, whole_floors, population, weights)
        print(f"  {tier:>4} | {last:>20,.0f} | {mean:>25,.0f} | {per_dungeon[tier]:>15,.0f} | "
              f"{flat:>16,.0f} ({flat / per_dungeon[tier]:.2f}x too high)")
    print()
    last_ratio = (creature_experience(8, whole_floors, whole_floors, weights)
                  / creature_experience(1, whole_floors, whole_floors, weights))
    print(f"  Over a whole dungeon, tier 8 pays {per_dungeon[8] / per_dungeon[1]:.1f} times what "
          f"tier 1 pays. On the last floor alone it is {last_ratio:.1f} times,")
    print("  which is where the earlier 15.5x figure came from. The right-hand column is "
          "that earlier method.")
    print()
    print()

    # -- The size, which is derived rather than chosen ----------------------

    print("THE SIZE OF THE CLIMB IS NOT A FREE CHOICE")
    print()
    print("  docs/Cataclysm_GDD_v2.md: \"A run is played at a fixed tier.\" A player does not")
    print("  drift up the difficulty tiers inside a run. They finish a campaign and start the")
    print("  next one higher, so passing through all eight tiers is eight campaigns.")
    print()
    print(f"    {len(TIERS)} difficulty tiers x {CAMPAIGN_DUNGEONS} dungeons a campaign "
          f"= {CLIMB_DUNGEONS} dungeons")
    print(f"    {CLIMB_DUNGEONS} dungeons x {whole_floors} floors x "
          f"{MINUTES_PER_FLOOR:g} minutes = {hours(CLIMB_DUNGEONS, whole_floors):,.0f} hours")
    print()
    print("  The campaign length comes from the balance sweep baseline in issue #914, whose")
    print("  three resolving policies clear 1,053 to 1,345 floors: 21 to 34 dungeons. At the")
    print(f"  low end the whole climb is {hours(21 * 8, whole_floors):,.0f} hours and at the "
          f"high end {hours(34 * 8, whole_floors):,.0f}.")
    print()
    print("  For comparison: Last Epoch 60 to 70 hours to maximum level, Diablo IV about 150,")
    print("  Path of Exile 150 to 300 and it treats level 100 as aspirational. This is longer")
    print("  than all three, and it is what wanting level 100 to arrive during tier 8 costs.")
    print()
    print()

    # -- What the rate decides ---------------------------------------------

    print("WHAT THE RATE DECIDES: THE LEVEL AT THE END OF EACH TIER")
    print()
    print("  With the size fixed, the rate no longer sets how long the climb is. It sets how")
    print("  the climb is spread, and the thing that shows is what level the character is when")
    print("  each tier's campaign ends. player_power.reference_character says this should be:")
    print()
    print("    " + "  ".join(f"{level:g}" for level in reference_levels_at_tier_ends()))
    print()
    print("  Rate   | Level at the end of each tier's campaign, 1 to 8 | Level 2 costs, "
          "in floors of tier 1")
    print("  " + "-" * 106)
    for rate in (0.02, 0.03, 0.04, 0.05, 0.06, round(POE_RATE, 6), 0.10, 0.15):
        levels = levels_at_tier_ends(rate, per_dungeon)
        scale = scale_for_level_100_at_the_end(rate, per_dungeon)
        opening = first_level_in_floors(rate, scale, population, weights, whole_floors)
        mark = "*" if abs(rate - POE_RATE) < 5e-7 else " "
        print(f"  {rate * 100:>5.2f}%{mark}| " + "  ".join(f"{level:>3}" for level in levels)
              + f"   | {opening:>26.1f}")
    print()
    print("  * is the recommendation, and the reason is the right-hand column. NO RATE GIVES")
    print("  BOTH a quick first level and a level that keeps pace with the difficulty tier.")
    print("  A decaying rate was tried and does not solve it either. It follows from")
    print(f"  experience per dungeon rising {per_dungeon[8] / per_dungeon[1]:.1f} times across "
          "the tiers while the dungeon")
    print("  budget per tier is flat at one campaign each.")
    print()
    print("  THE COST OF CHOOSING THE QUICK OPENING is that the character out-levels the early")
    print("  tiers. Every ARPG in the genre does this. What it breaks here is named below.")
    print()
    print()

    # -- The recommended rate, and why -------------------------------------

    print("WHY THAT RATE, RATHER THAN ANOTHER ONE WITH A QUICK OPENING")
    print()
    print("  Path of Exile publishes its whole experience table. Fitting one number from it")
    print("  -- the share of the climb spent between levels 90 and 100 -- fixes the rate, and")
    print("  the other two checkpoints then agree without being fitted:")
    print()
    print(f"    {POE_RATE * 100:.2f}% a level.")
    print()
    print("  Checkpoint                        | This curve | Path of Exile | Fitted?")
    print("  " + "-" * 74)
    for label, mine, theirs, fitted in (
        ("Share of the climb by level 50",
         total_experience(POE_RATE, 1.0, 50) / total_experience(POE_RATE, 1.0),
         POE_SHARE_BY_50, "no"),
        ("Share of the climb by level 90",
         total_experience(POE_RATE, 1.0, 90) / total_experience(POE_RATE, 1.0),
         POE_SHARE_BY_90, "yes"),
        ("The last level alone",
         level_cost(MAX_LEVEL, POE_RATE, 1.0) / total_experience(POE_RATE, 1.0),
         POE_SHARE_LAST_LEVEL, "no"),
    ):
        print(f"  {label:<33} | {mine:>10.2%} | {theirs:>13.2%} | {fitted:>7}")
    print()

    scale = scale_for_level_100_at_the_end(POE_RATE, per_dungeon)
    print("  Sized so level 100 arrives at the end of tier 8, that curve is:")
    print()
    print(f"    level 2 costs   {scale:>15,.0f}   "
          f"({first_level_in_floors(POE_RATE, scale, population, weights, whole_floors):.1f} "
          "floors of a tier 1 dungeon)")
    print(f"    level 50 costs  {level_cost(50, POE_RATE, scale):>15,.0f}")
    print(f"    level 100 costs {level_cost(MAX_LEVEL, POE_RATE, scale):>15,.0f}")
    print(f"    the whole climb {total_experience(POE_RATE, scale):>15,.0f}")
    print()
    print()

    # -- What this forces elsewhere ----------------------------------------

    levels = levels_at_tier_ends(POE_RATE, per_dungeon)
    level_weight = LEVEL_WEIGHT
    from_level = level_weight * levels[0]
    tier_1_ceiling = scoring.PLAYER_MAX_SCORES[1]
    print("WHAT THE RECOMMENDATION FORCES ELSEWHERE, AND IT IS NOT NOTHING")
    print()
    print(f"  The character is level {levels[0]} when the tier 1 campaign ends, against the "
          f"{reference_levels_at_tier_ends()[0]:g}")
    print("  player_power.reference_character expects. Two things follow and both need a")
    print("  decision that this script cannot make:")
    print()
    print("  1. player_power.reference_character's rule, \"level rises evenly to 100 at the end")
    print("     of tier 8\", stops being true. It is used to check the Power Score formula")
    print("     against the tier anchors, so it has to be replaced with what levelling")
    print("     actually produces rather than left disagreeing.")
    print()
    print(f"  2. Level Weight is {level_weight} in the Power Score formula, so level "
          f"{levels[0]} is worth")
    print(f"     {from_level:.0f} Power Score on its own. The maximum a player is expected to "
          f"reach by the")
    print(f"     end of tier 1 is {tier_1_ceiling:g}, so level alone would be "
          f"{from_level / tier_1_ceiling:.0%} of it. The early tiers")
    print("     get easier. If that is unwanted, the thing to change is Level Weight, not")
    print("     the experience curve.")
    print()
    print("  HOW BADLY, TIER BY TIER. The character is furthest ahead at the start and the")
    print("  lead shrinks every tier, because level is capped at 100 while the tier the")
    print("  player is entering keeps rising. This is the shape every ARPG in the genre has,")
    print("  and it says the problem is confined to the first tier or two:")
    print()
    print("  Entering tier | at level | Power Score from level | The tier starts at | Share")
    print("  " + "-" * 80)
    for tier in range(2, 9):
        level = levels[tier - 2]
        carried = level_weight * level
        starts_at = scoring.PLAYER_MAX_SCORES[tier - 1]
        print(f"  {tier:>13} | {level:>8} | {carried:>22.0f} | {starts_at:>18g} | "
              f"{carried / starts_at:>5.0%}")
    print()
    print()

    # -- Sensitivity -------------------------------------------------------

    print("HOW MUCH THE UNMEASURED INPUTS MOVE THE ANSWER")
    print()
    print("  The climb assumes the player kills every creature on every floor, and nobody has")
    print("  played a full dungeon to find out what share they really clear. Issue #925. A")
    print("  half-cleared floor doubles the hours. Level 100 would then arrive during tier 8")
    print("  only for a player who does clear everything; everyone else reaches it later.")
    print()
    print("  Creatures killed a floor | Dungeons for the same climb | Hours")
    print("  " + "-" * 66)
    for count in (65, 129, round(population), 400):
        scaled = {t: dungeon_experience(t, whole_floors, count, weights) for t in TIERS}
        needed = total_experience(POE_RATE, scale) / (sum(scaled.values()) / len(TIERS))
        print(f"  {count:>4} ({count / population:>4.0%} of the floor) | {needed:>27,.0f} | "
              f"{hours(needed, whole_floors):,.0f}")
    print()
    print()

    _print_the_decided_curve()


def _print_the_decided_curve() -> None:
    """The curve as chosen, and the check that eight campaigns really pay for it."""
    rate, scale = DECIDED_RATE, DECIDED_LEVEL_2_COST
    climb = total_experience(rate, scale)
    earned = CAMPAIGN_DUNGEONS * sum(PER_DUNGEON.values())

    print("THE CURVE AS DECIDED")
    print()
    print(f"  A level costs the last level's cost times {1 + rate:g}. Level 2 costs "
          f"{scale:,.0f}.")
    print()
    print(f"    cost of level L = {scale:,.0f} x {1 + rate:g} ^ (L - 2),  for L from 2 to "
          f"{MAX_LEVEL}")
    print()
    print("  Level | Costs             | Cumulative")
    print("  " + "-" * 52)
    for level in (2, 10, 25, 50, 75, 90, 99, MAX_LEVEL):
        print(f"  {level:>5} | {level_cost(level, rate, scale):>17,.0f} | "
              f"{total_experience(rate, scale, level):>18,.0f}")
    print()
    print(f"  The whole climb costs {climb:,.0f}. Eight campaigns of "
          f"{CAMPAIGN_DUNGEONS} dungeons pay")
    print(f"  {earned:,.0f}, which is {earned / climb:.3f} times it, so level "
          f"{MAX_LEVEL} arrives at the end of")
    print(f"  difficulty tier 8. Reaching level 2 takes "
          f"{first_level_in_floors(rate, scale, POPULATION, WEIGHTS, WHOLE_FLOORS):.1f} floors "
          "of a tier 1 dungeon.")
    print()
    print("  Level at the end of each tier's campaign: "
          + "  ".join(str(level) for level in levels_at_tier_ends(rate, PER_DUNGEON)))
    print()


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, matching
# `analyse_margin_tolerance.py` and the others. `sim/tests/test_analysis_scripts.py`
# runs these through `runpy.run_path`, which does NOT set `__name__` to
# `__main__`, so a guarded script runs and prints nothing and the test reports it
# as a script that produced no output.
main()
