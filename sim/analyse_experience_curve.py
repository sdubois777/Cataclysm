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

    The rate and the scale are TWO choices, and "level 2 costs 100" collapses
    them into one. That is the whole reason the earlier band of 15% to 17%
    looked so delicate that two percentage points moved the answer ten times.
    Separate them and the rate stops being a scale knob.

    A rate of 8.19% reproduces Path of Exile's own published distribution of
    the climb across the levels. One number was fitted, the share spent between
    levels 90 and 100; the other two checkpoints then agree to within a
    percentage point without being fitted. 15% does not, and neither does the
    rate that keeps the pace even across the difficulty tiers, which is 3.70%
    and makes reaching level 2 take 35 floors.

    One dungeon is 1.7 to 4.2 hours of play at the design's own two to five
    minutes a floor, so twenty dungeons is already 33 to 83 hours -- about what
    Last Epoch costs to reach its maximum level. Fifty dungeons is 83 to 208
    hours, which brackets the 150 Diablo IV states, and is 1.9 campaigns.

THE THREE INPUTS THAT ARE NOT CALCULATED, and where each comes from:

    Creatures on a floor. `EnemiesPerWalkableCell = 0.24` in
    `game/Source/Cataclysm/Dungeon/CataclysmFloorPopulation.h`, measured over
    1,000 seeds per layout on 2026-08-22 and recorded in `docs/DECISIONS.md` as
    108-420 for Halls, 84-510 for Caverns and 73-350 for Arena. The midpoint of
    those three midpoints is used here. THE ANSWER IN DUNGEONS SCALES INVERSELY
    WITH THIS NUMBER, so it is printed as a sensitivity rather than buried.

    Minutes on a floor. `docs/Cataclysm_GDD_v2.md`: "A floor should take an
    efficient player between two and five minutes, including finding the
    stairs." That sentence is the project owner's own, from a comment on issue
    #34. It is what makes an answer in hours possible at all.

    Rarity spawn weights. The SpawnWeight column of `game/Data/EnemyRarities.csv`
    -- Common 0.6, Elite 0.2, Legendary 0.15, Herald 0.04, Boss 0.01, Cataclysm
    Boss 0. Read from the file rather than retyped.

THE ASSUMPTION THAT MATTERS MOST, stated plainly because it is not measurable
yet: this assumes the player kills every creature on every floor. A player who
runs for the stairs earns less and the climb takes proportionally longer. There
is no measurement of what share of a floor a real player clears, because nobody
has played a full dungeon. Treat every dungeon count here as the FULL-CLEAR
figure, which is the optimistic end.

Run: python analyse_experience_curve.py
"""

from __future__ import annotations

import csv
import math
import pathlib

from cataclysm_sim import scoring
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.player_power import MAX_LEVEL

# ---------------------------------------------------------------------------
# The inputs that were measured elsewhere.

#: Midpoint of the three per-layout creature ranges in `docs/DECISIONS.md`,
#: 2026-08-22, "0.24 creatures per walkable cell".
LAYOUT_CREATURE_RANGES = {"Halls": (108, 420), "Caverns": (84, 510), "Arena": (73, 350)}

#: `docs/Cataclysm_GDD_v2.md`, "What a Floor Is".
MINUTES_PER_FLOOR = (2.0, 5.0)

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
#          SHAPE: where in the climb the time goes.
#   scale  what level 2 costs. This is the SIZE: what the whole climb costs.
#
# Fixing "level 2 costs 100" and then choosing the rate ties the two together,
# which is why the earlier band of 15% to 17% looked so delicate. Two percentage
# points moved the total by ten times because the scale was pinned.


def level_cost(level: int, rate: float, scale: float) -> float:
    """What the step from `level - 1` to `level` costs."""
    return scale * (1.0 + rate) ** (level - 2)


def total_experience(rate: float, scale: float, top_level: int = MAX_LEVEL) -> float:
    """Every level from 2 to `top_level`, summed."""
    steps = top_level - 1
    if rate == 0.0:
        return scale * steps
    return scale * ((1.0 + rate) ** steps - 1.0) / rate


# ---------------------------------------------------------------------------
# A climb that moves up the difficulty tiers.


def tier_for_level(level: int) -> int:
    """The difficulty tier a character of this level is expected to be running.

    THIS IS NOT INVENTED HERE. `player_power.reference_character` says what a
    player looks like at the end of each difficulty tier, and its rule for level
    is "level rises evenly to 100 at the end of tier 8" -- level 12.5 at the end
    of tier 1, 25 at the end of tier 2, and so on. This is that rule read
    backwards.

    `docs/Cataclysm_GDD_v2.md` says "There is no hard gate on difficulty. A
    player may enter any dungeon at any time", so nothing FORCES it. It is what a
    player who keeps pace does, and the design already commits to it elsewhere.
    """
    return max(1, min(8, math.ceil(level * 8 / MAX_LEVEL)))


def dungeons_by_tier(rate: float, scale: float,
                     per_dungeon: dict[int, float]) -> dict[int, float]:
    """Dungeons spent inside each difficulty tier's band of levels."""
    out = {tier: 0.0 for tier in TIERS}
    for level in range(2, MAX_LEVEL + 1):
        tier = tier_for_level(level)
        out[tier] += level_cost(level, rate, scale) / per_dungeon[tier]
    return out


def dungeons_at_fixed_tier(rate: float, scale: float,
                           per_dungeon: dict[int, float], tier: int) -> float:
    """The whole climb run at one difficulty tier and never moving off it."""
    return total_experience(rate, scale) / per_dungeon[tier]


def rate_for_even_pace(per_dungeon: dict[int, float]) -> float:
    """The rate at which every difficulty tier costs the same number of dungeons.

    Bisection on the last tier's band against the first tier's. Raising the rate
    loads more of the climb onto the high levels, which are the ones run at the
    high tiers, so the ratio rises with the rate and bisection is sound.
    """
    low, high = 0.0, 1.0
    for _ in range(200):
        middle = (low + high) / 2
        spent = dungeons_by_tier(middle, 1.0, per_dungeon)
        if spent[8] < spent[1]:
            low = middle
        else:
            high = middle
    return (low + high) / 2


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


def scale_for_climbing_dungeons(rate: float, per_dungeon: dict[int, float],
                                target: float) -> float:
    """The cost of level 2 that makes a tier-climbing player run `target` dungeons."""
    return target / sum(dungeons_by_tier(rate, 1.0, per_dungeon).values())


def first_level_in_floors(rate: float, scale: float, population: float,
                          weights: dict[str, float], floors: int) -> float:
    """Floors of a tier 1 dungeon needed for level 2. The cheapest sanity check.

    A curve flat enough to keep the pace even across the difficulty tiers makes
    the first level cost most of a dungeon, which no ARPG does. This is what
    catches that.
    """
    average = sum(creature_experience(1, f, floors, weights)
                  for f in range(1, floors + 1)) / floors
    return level_cost(2, rate, scale) / (average * population)


# ---------------------------------------------------------------------------


def hours(dungeon_count: float, floors: int) -> tuple[float, float]:
    """A dungeon count in hours of play, at the design's two to five a floor."""
    low, high = MINUTES_PER_FLOOR
    return (dungeon_count * floors * low / 60, dungeon_count * floors * high / 60)


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

#: The rate at which every difficulty tier costs the same number of dungeons.
EVEN_RATE = rate_for_even_pace(PER_DUNGEON)

#: The rate that reproduces Path of Exile's distribution. The recommendation.
POE_RATE = rate_matching_path_of_exile()


def main() -> None:
    weights, population = WEIGHTS, POPULATION
    floors, whole_floors = FLOORS, WHOLE_FLOORS

    # -- What a creature and a dungeon are worth ----------------------------

    print("WHAT ONE CREATURE AND ONE DUNGEON ARE WORTH")
    print()
    print("  Rarity spawn weights, from game/Data/EnemyRarities.csv: "
          + ", ".join(f"{k} {v:g}" for k, v in weights.items() if v > 0))
    print(f"  Creatures on a floor: {population:.0f} (midpoint of "
          + ", ".join(f"{k} {lo}-{hi}" for k, (lo, hi) in LAYOUT_CREATURE_RANGES.items()) + ")")
    print(f"  Floors in an average dungeon: {floors:.1f}, over "
          f"{len(TuningConfig().DUNGEON_SPECS)} floor ranges in config.DUNGEON_SPECS")
    one_low, one_high = hours(1, whole_floors)
    print(f"  One dungeon of {whole_floors} floors is {one_low:.1f} to {one_high:.1f} hours of "
          f"play, at {MINUTES_PER_FLOOR[0]:g} to {MINUTES_PER_FLOOR[1]:g} minutes a floor")
    print()

    per_dungeon = PER_DUNGEON
    print("  Tier | Creature, last floor | Creature, dungeon average | "
          "One dungeon | Last floor's rate everywhere")
    print("  " + "-" * 111)
    for tier in TIERS:
        last = creature_experience(tier, whole_floors, whole_floors, weights)
        mean = sum(creature_experience(tier, f, whole_floors, weights)
                   for f in range(1, whole_floors + 1)) / whole_floors
        value = dungeon_experience(tier, whole_floors, population, weights)
        flat = dungeon_experience_flat(tier, whole_floors, population, weights)
        print(f"  {tier:>4} | {last:>20,.0f} | {mean:>25,.0f} | {value:>15,.0f} | "
              f"{flat:>16,.0f} ({flat / value:.2f}x too high)")
    print()
    last_ratio = (creature_experience(8, whole_floors, whole_floors, weights)
                  / creature_experience(1, whole_floors, whole_floors, weights))
    print(f"  Over a whole dungeon, tier 8 pays {per_dungeon[8] / per_dungeon[1]:.1f} times what "
          f"tier 1 pays. On the last floor alone it is {last_ratio:.1f} times,")
    print("  which is where the earlier 15.5x figure came from. The right-hand column is "
          "that earlier method.")
    print()
    print()

    # -- The constant-rate proposal with the scale pinned -------------------

    print("THE CONSTANT-RATE PROPOSAL, WITH LEVEL 2 PINNED AT 100")
    print()
    print("  The shape is already agreed. Pinning level 2 at 100 is what makes the rate "
          "look delicate:")
    print("  it chooses the size and the shape with one number.")
    print()
    print("  Rate | Total experience | Tier 1 dungeons | Tier 4 | Tier 8 | "
          "Moving up the tiers | Hours at tier 1")
    print("  " + "-" * 110)
    for rate in (0.10, 0.12, 0.14, 0.15, 0.16, 0.17, 0.18, 0.20, 0.25):
        total = total_experience(rate, 100.0)
        at1 = dungeons_at_fixed_tier(rate, 100.0, per_dungeon, 1)
        at4 = dungeons_at_fixed_tier(rate, 100.0, per_dungeon, 4)
        at8 = dungeons_at_fixed_tier(rate, 100.0, per_dungeon, 8)
        moving = sum(dungeons_by_tier(rate, 100.0, per_dungeon).values())
        low_hours, high_hours = hours(at1, whole_floors)
        print(f"  {rate * 100:>3.0f}% | {total:>16.3g} | {at1:>15,.0f} | {at4:>6,.0f} | "
              f"{at8:>6,.0f} | {moving:>19,.1f} | {low_hours:>7,.0f} to {high_hours:,.0f}")
    print()
    doubling = total_experience(1.0, 100.0)
    print(f"  The doubling first proposed is 100% a level. Its total is {doubling:.2g}, or "
          f"{doubling / per_dungeon[1]:.1g} tier 1")
    print("  dungeons. No scale rescues that, which is settled and is not revisited here.")
    print()
    at_15 = dungeons_at_fixed_tier(0.15, 100.0, per_dungeon, 1)
    moving_15 = sum(dungeons_by_tier(0.15, 100.0, per_dungeon).values())
    print("  READ THE LAST TWO COLUMNS TOGETHER. At 15%, a player who never leaves tier 1 "
          f"runs {at_15:,.0f}")
    print(f"  dungeons and one who moves up the tiers as the design expects runs "
          f"{moving_15:,.1f}. That is not")
    print("  one pace with a spread. It is two different games.")
    print()
    print()

    # -- The rate that holds the pace even ---------------------------------

    even = EVEN_RATE
    print("THE RATE THAT HOLDS THE PACE EVEN ACROSS THE DIFFICULTY TIERS")
    print()
    print("  A dungeon pays more at every tier up. If a level's cost grows faster than "
          "that, the")
    print("  climb collapses onto the top tiers; slower, and it all happens at tier 1. One "
          "rate makes")
    print("  every tier cost the same number of dungeons:")
    print()
    print(f"    {even * 100:.2f}% a level.")
    print()
    print("  Rate  | Share of the climb spent in tier 1 | in tier 4 | in tier 8 | "
          "Dearest tier over cheapest")
    print("  " + "-" * 104)
    for rate in sorted({0.02, 0.03, round(even, 6), 0.05, 0.08, 0.10, 0.15}):
        spent = dungeons_by_tier(rate, 1.0, per_dungeon)
        whole = sum(spent.values())
        spread = max(spent.values()) / min(spent.values())
        mark = "*" if abs(rate - even) < 5e-7 else " "
        print(f"  {rate * 100:>5.2f}%{mark}| {spent[1] / whole:>34.1%} | "
              f"{spent[4] / whole:>9.1%} | {spent[8] / whole:>9.1%} | {spread:>25,.0f}x")
    print()
    print("  * is the even rate. Above it the last tier dominates, below it the first "
          "tier does.")
    print()
    even_scale = scale_for_climbing_dungeons(even, per_dungeon, 50)
    print(f"  AN EVEN PACE IS NOT FREE, AND THIS IS WHY IT IS NOT THE RECOMMENDATION. A rate "
          f"of {even * 100:.2f}%")
    print("  is nearly flat, so the first level costs almost as much as the last. Sized so a")
    print("  tier-climbing player runs 50 dungeons, reaching level 2 takes "
          f"{first_level_in_floors(even, even_scale, population, weights, whole_floors):.0f} "
          "floors of a tier 1")
    print("  dungeon. No ARPG opens that way.")
    print()
    print()

    # -- The rate Path of Exile's own table implies -------------------------

    poe = POE_RATE
    print("THE RATE THAT REPRODUCES PATH OF EXILE'S OWN DISTRIBUTION")
    print()
    print("  Path of Exile publishes its whole experience table. Fitting one number from it "
          "-- the")
    print("  share of the climb spent between levels 90 and 100 -- fixes the rate, and the "
          "other")
    print("  two checkpoints then agree without being fitted:")
    print()
    print(f"    {poe * 100:.2f}% a level.")
    print()
    print("  Checkpoint                        | This curve | Path of Exile | Fitted?")
    print("  " + "-" * 74)
    for label, mine, theirs, fitted in (
        ("Share of the climb by level 50",
         total_experience(poe, 1.0, 50) / total_experience(poe, 1.0), POE_SHARE_BY_50, "no"),
        ("Share of the climb by level 90",
         total_experience(poe, 1.0, 90) / total_experience(poe, 1.0), POE_SHARE_BY_90, "yes"),
        ("The last level alone",
         level_cost(MAX_LEVEL, poe, 1.0) / total_experience(poe, 1.0),
         POE_SHARE_LAST_LEVEL, "no"),
    ):
        print(f"  {label:<33} | {mine:>10.2%} | {theirs:>13.2%} | {fitted:>7}")
    print()
    spent = dungeons_by_tier(poe, 1.0, per_dungeon)
    whole = sum(spent.values())
    print(f"  It spends {spent[1] / whole:.1%} of the climb in difficulty tier 1 and "
          f"{spent[8] / whole:.1%} in tier 8, which is the")
    print("  same top-heavy shape Path of Exile has and the opposite of an even pace. That "
          "is the")
    print("  trade being made: a fast opening in exchange for a long last ten levels.")
    print()
    print()

    # -- Working backwards from a chosen size ------------------------------

    print("WORKING BACKWARDS: PICK THE SIZE, THE COST OF LEVEL 2 FOLLOWS")
    print()
    print(f"  Shape held at {poe * 100:.2f}%. Choose how many dungeons the whole climb should "
          "cost a player who")
    print("  moves up the difficulty tiers as the design expects them to. A campaign is about")
    print("  27 dungeons, so the right-hand column is how many campaigns a character spans.")
    print()
    print("  Dungeons | Hours         | Level 2 costs | Level 2 in floors | Level 100 costs "
          "| Never leaves tier 1 | Campaigns")
    print("  " + "-" * 122)
    for target in (20, 30, 40, 50, 60, 75, 100, 150):
        scale = scale_for_climbing_dungeons(poe, per_dungeon, target)
        low_hours, high_hours = hours(target, whole_floors)
        opening = first_level_in_floors(poe, scale, population, weights, whole_floors)
        stuck = total_experience(poe, scale) / per_dungeon[1]
        print(f"  {target:>8} | {low_hours:>4,.0f} to {high_hours:<7,.0f} | {scale:>13,.0f} | "
              f"{opening:>17.1f} | {level_cost(MAX_LEVEL, poe, scale):>16,.0f} | "
              f"{stuck:>19,.0f} | {target / 27:>9.1f}")
    print()
    print("  Time to maximum level in the genre, for reading the hours column: Last Epoch 60")
    print("  to 70 hours, Diablo IV about 150, Path of Exile 150 to 300 and it treats level")
    print("  100 as aspirational rather than expected.")
    print()
    print("  The 'never leaves tier 1' column is the player who refuses to raise difficulty.")
    print("  It is meant to be unreachable. Every ARPG in the genre works this way: maximum")
    print("  level is not obtainable on the lowest difficulty in any sane time.")
    print()
    print()

    # -- Sensitivity -------------------------------------------------------

    print("HOW MUCH THE UNMEASURED INPUTS MOVE THE ANSWER")
    print()
    print("  Every dungeon count above scales inversely with how many creatures a floor "
          "holds and")
    print("  with what share of them a player actually kills, and neither has been "
          "measured in play.")
    print(f"  At {poe * 100:.2f}%, with the size set so a tier-climbing player runs "
          "50 dungeons:")
    print()
    scale = scale_for_climbing_dungeons(poe, per_dungeon, 50)
    print("  Creatures killed a floor | Dungeons to level 100 | Hours")
    print("  " + "-" * 62)
    for count in (65, 129, round(population), 400):
        scaled = {t: dungeon_experience(t, whole_floors, count, weights) for t in TIERS}
        run = sum(dungeons_by_tier(poe, scale, scaled).values())
        low_hours, high_hours = hours(run, whole_floors)
        print(f"  {count:>4} ({count / population:>4.0%} of the floor) | {run:>21,.0f} | "
              f"{low_hours:,.0f} to {high_hours:,.0f}")
    print()


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, matching
# `analyse_margin_tolerance.py` and the others. `sim/tests/test_analysis_scripts.py`
# runs these through `runpy.run_path`, which does NOT set `__name__` to
# `__main__`, so a guarded script runs and prints nothing and the test reports it
# as a script that produced no output.
main()
