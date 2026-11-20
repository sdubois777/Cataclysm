"""Do a floor's creatures draw their rarity and their modifiers independently? Issue #2749, the placement seeding slice.

THE FAULT. The dungeon game mode seeds a placed creature's rarity stream `FloorSeed ^ (0x72720000 + Index)`
(`ACataclysmDungeonGameMode::RarityStreamForPlacement`) and its modifier stream `FloorSeed ^ (0x6D6D0000 + Index)`
(`ModifierSeedForPlacement`; a dark floor's raise uses 0x6D640000). Both salts have nought in their low 16 bits, so a
creature's two seeds differ by a constant on a floor, and Unreal's `FRandomStream` is a linear congruential generator
(`Seed = Seed * 196314165 + 907633515`, a fraction from the top 23 bits), so its first modifier fraction is its rarity
fraction plus a constant. Every Elite's rarity fraction lies in one narrow range, so every Elite on a floor draws its
first modifier from a band of a few adjacent rows of the sorted pool. The project owner's floor 1 on 2026-11-19 had 48
Elites carrying four modifiers, Thorns of Glass on 15 of them. The coordinating session ruled it a fault, not a design.

WHAT THIS REPLAYS. The engine's `FRandomStream` exactly (`RandomStream.h`, Unreal Engine 5.8: single precision, the
fraction `(Seed >> 9) / 2^23` after the seed moves), `UCataclysmEnemyRarity::RollRarityStep` over
`game/Data/EnemyRarities.csv` and the first draw of `UCataclysmEnemyModifiers::Draw` over the pool `PoolFor` builds from
`game/Data/EnemyModifiers.csv` (the creature's type and Generic, sorted ignoring case), for many floors, with:

- THE GAME'S SEEDS, as above; and
- THE PLANNED SEEDS, `planned_seed(floor seed, salt, index)`: the three multiplied by three odd constants and added, then
  `FCataclysmFloorGenerator::SeedForFloor`'s finaliser, so neighbouring indices and different salts give unrelated
  seeds. A JUDGEMENT of this slice; the C++ port must match it, which a later test checks.

WHAT IT MEASURES, among a floor's Elites: how many distinct first modifiers they carry; the share carrying the floor's
commonest; whether every one lies in a narrow cyclic band of the sorted pool; and how often two placements next to each
other in the floor's list are both Elite. Over every floor, each modifier's share, which should be the pool's uniform
share with either seeding.

    python analyse_placement_seeds.py                                   # a smoke run: 120 floors of each type
    CATACLYSM_PLACEMENT_FLOORS=2000 python analyse_placement_seeds.py   # the measurement

The smoke run's figures are not the measurement; 2,000 floors is, and its output is quoted in the entry.
"""
from __future__ import annotations

import collections
import csv
import os
import pathlib
import statistics
import struct

REPO = pathlib.Path(__file__).resolve().parents[1]
DATA = REPO / "game" / "Data"

M32 = 0xFFFFFFFF

#: `ACataclysmDungeonGameMode`'s three placement salts.
RARITY_FROM_THE_FLOOR_SALT = 0x72720000
MODIFIERS_FROM_THE_FLOOR_SALT = 0x6D6D0000
DARK_FLOOR_MODIFIERS_SALT = 0x6D640000

#: The rarity step whose creatures draw exactly one modifier (`UCataclysmEnemyModifiers::CountForRarityStep`).
ELITE_STEP = 1

#: Placements a floor's list holds in this replay: the planning measurement's figure, more than a floor puts down, so a
#: floor holds enough Elites to measure.
PLACEMENTS = 250

#: The width of the band the game's seeds confine a floor's Elites to, in rows of the sorted pool: the measurement's
#: "about 3.6 adjacent rows", rounded up, plus one for a band that straddles two rows' edges.
BAND_ROWS = 5

THORNS = "Generic_Thorns_of_Glass"


def f32(value: float) -> float:
    """`value` rounded to single precision, as the engine's float arithmetic rounds it."""
    return struct.unpack("f", struct.pack("f", value))[0]


class Stream:
    """`FRandomStream`, as Unreal Engine 5.8's `RandomStream.h` writes it."""

    def __init__(self, seed: int):
        self.seed = seed & M32  # `Seed = uint32(InSeed)`

    def fraction(self) -> float:
        """`GetFraction`: the seed moves first, then the top 23 bits make the fraction, exactly representable."""
        self.seed = (self.seed * 196314165 + 907633515) & M32
        return (self.seed >> 9) / float(1 << 23)

    def rand_range(self, low: int, high: int) -> int:
        """`RandRange`: `Min + TruncToInt(GetFraction() * float(Range))`, the product in single precision."""
        span = high - low + 1
        return low + (int(f32(self.fraction() * f32(span))) if span > 0 else 0)

    def frand_range(self, low: float, high: float) -> float:
        """`FRandRange`, in double precision, as its `FVector::FReal` result is."""
        return low + (high - low) * self.fraction()


def seed_for_floor(seed: int, number: int) -> int:
    """`FCataclysmFloorGenerator::SeedForFloor`."""
    mixed = ((seed & M32) * 0x9E3779B1 + (number & M32) * 0x85EBCA6B) & M32
    return finalise(mixed)


def finalise(mixed: int) -> int:
    """The avalanche finaliser `SeedForFloor` ends with, to a non-negative int32."""
    mixed ^= mixed >> 16
    mixed = (mixed * 0x7FEB352D) & M32
    mixed ^= mixed >> 15
    mixed = (mixed * 0x846CA68B) & M32
    mixed ^= mixed >> 16
    return mixed & 0x7FFFFFFF


def planned_seed(seed: int, salt: int, index: int) -> int:
    """THE PLANNED MIX (a judgement): the floor seed, the salt and the index, each times its own odd constant, summed,
    then the finaliser; never nought, which a caller reads as the clock."""
    mixed = ((seed & M32) * 0x9E3779B1 + (salt & M32) * 0x85EBCA6B + (index & M32) * 0xC2B2AE35) & M32
    return finalise(mixed) or 1


def game_rarity_seed(floor_seed: int, index: int) -> int:
    """`RarityStreamForPlacement`'s seed."""
    return floor_seed ^ (RARITY_FROM_THE_FLOOR_SALT + index)


def game_modifier_seed(floor_seed: int, index: int, salt: int = MODIFIERS_FROM_THE_FLOOR_SALT) -> int:
    """`ModifierSeedForPlacement`: never nought."""
    return (floor_seed ^ (salt + index)) or 1


def load_rarities() -> list[tuple[int, float]]:
    """`SpawnableSteps` with their weights: every step whose spawn weight is above nought, in step order."""
    with (DATA / "EnemyRarities.csv").open(encoding="utf-8-sig", newline="") as handle:
        rows = [(int(r["Step"]), f32(float(r["SpawnWeight"]))) for r in csv.DictReader(handle)]
    return sorted((step, weight) for step, weight in rows if weight > 0.0)


def load_pools() -> dict[str, list[str]]:
    """`PoolFor` for every type but Generic: its rows and Generic's, sorted ignoring case (`FName::LexicalLess`)."""
    with (DATA / "EnemyModifiers.csv").open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    types = sorted({r["CataclysmType"] for r in rows} - {"Generic"})
    return {kind: sorted((r["Name"] for r in rows if r["CataclysmType"].lower() in (kind.lower(), "generic")),
                         key=str.lower)
            for kind in types}


def roll_rarity(stream: Stream, steps: list[tuple[int, float]]) -> int:
    """`RollRarityStep`: a single-precision draw over the total, walked against a single-precision running sum."""
    total = 0.0
    for _, weight in steps:
        total = f32(total + weight)
    drawn = f32(stream.frand_range(0.0, total))
    running = 0.0
    for step, weight in steps:
        running = f32(running + weight)
        if drawn <= running:
            return step
    return steps[-1][0]


def first_modifier(stream: Stream, pool: list[str]) -> int:
    """The index in `pool` of `Draw`'s first pick: `RandRange(0, Pool.Num() - 1)`."""
    return stream.rand_range(0, len(pool) - 1)


def floor(floor_seed: int, pool: list[str], steps, planned: bool, placements: int = PLACEMENTS):
    """Every placement's rarity step, and the pool index of each Elite's first modifier, in placement order."""
    rungs, elites = [], []
    for index in range(placements):
        if planned:
            rarity = Stream(planned_seed(floor_seed, RARITY_FROM_THE_FLOOR_SALT, index))
            modifier = Stream(planned_seed(floor_seed, MODIFIERS_FROM_THE_FLOOR_SALT, index))
        else:
            rarity = Stream(game_rarity_seed(floor_seed, index))
            modifier = Stream(game_modifier_seed(floor_seed, index))
        step = roll_rarity(rarity, steps)
        rungs.append(step)
        if step == ELITE_STEP:
            elites.append(first_modifier(modifier, pool))
    return rungs, elites


def narrowest_cyclic_band(indices: list[int], size: int) -> int:
    """The fewest adjacent rows, wrapping round the pool's end, that hold every index."""
    present = sorted(set(indices))
    if len(present) <= 1:
        return len(present)
    gaps = [(present[(i + 1) % len(present)] - present[i]) % size for i in range(len(present))]
    return size - max(gaps) + 1


def measure(floor_seeds: list[int], pool: list[str], steps, planned: bool) -> dict:
    distinct, commonest, bands, in_band, adjacent = [], [], [], 0, []
    overall = collections.Counter()
    for seed in floor_seeds:
        rungs, elites = floor(seed, pool, steps, planned)
        # EACH PLACEMENT AND THE NEXT: the second list is one shorter, so the pairs stop at the last placement.
        pairs = sum(1 for a, b in zip(rungs, rungs[1:], strict=False) if a == ELITE_STEP and b == ELITE_STEP)
        adjacent.append(pairs / (len(rungs) - 1))
        if len(elites) < 10:
            continue
        counts = collections.Counter(elites)
        overall.update(counts)
        distinct.append(len(counts))
        commonest.append(counts.most_common(1)[0][1] / len(elites))
        band = narrowest_cyclic_band(elites, len(pool))
        bands.append(band)
        in_band += band <= BAND_ROWS
    total = sum(overall.values())
    return {
        "floors": len(distinct),
        "distinct_median": statistics.median(distinct), "distinct_least": min(distinct),
        "distinct_most": max(distinct),
        "commonest_median": statistics.median(commonest),
        "band_median": statistics.median(bands), "floors_in_a_band": in_band,
        "adjacent_elites": statistics.mean(adjacent),
        "shares": {pool[i]: n / total for i, n in overall.items()},
        "uniform": 1.0 / len(pool),
    }


def floor_seeds(count: int) -> list[int]:
    """Floor seeds as the generator makes them, `SeedForFloor(dungeon seed, floor number)`, eight floors a dungeon."""
    return [seed_for_floor(dungeon, number) for dungeon in range(1, count // 8 + 2) for number in range(1, 9)][:count]


#: Floors replayed for each type: 120 by default, a smoke run; set CATACLYSM_PLACEMENT_FLOORS=2000 for the measurement.
FLOORS = int(os.environ.get("CATACLYSM_PLACEMENT_FLOORS", "120"))


def main() -> dict:
    steps = load_rarities()
    pools = load_pools()
    seeds = floor_seeds(FLOORS)
    if FLOORS < 2000:
        print(f"SMOKE RUN: {FLOORS} floors a type; the measurement is 2,000 (set CATACLYSM_PLACEMENT_FLOORS).")
    print(f"rarity steps that spawn, with weights: {steps}; Elite is step {ELITE_STEP}")
    print(f"the Demonic pool in the order the draw indexes it (a band is a run of these): {pools['Demonic']}")
    print(f"{len(seeds)} floors of {PLACEMENTS} placements a type; a band is {BAND_ROWS} adjacent rows or fewer")
    results = {}
    for kind, pool in pools.items():
        for planned in (False, True):
            got = measure(seeds, pool, steps, planned)
            results[(kind, planned)] = got
            label = "PLANNED" if planned else "GAME   "
            print(f"{kind:<10} {label} pool {len(pool):>2}: distinct a floor median {got['distinct_median']:>4} "
                  f"(least {got['distinct_least']:>2}, most {got['distinct_most']:>2}); commonest median "
                  f"{got['commonest_median']:.0%}; band median {got['band_median']:>4} rows, floors in a band "
                  f"{got['floors_in_a_band']:>4} of {got['floors']}; neighbours both Elite "
                  f"{got['adjacent_elites']:.1%}; most common share over all floors "
                  f"{max(got['shares'].values()):.1%} (uniform {got['uniform']:.1%})")
    demonic_game = results[("Demonic", False)]
    print(f"Thorns of Glass among Demonic Elites over all floors: game {demonic_game['shares'].get(THORNS, 0):.1%}, "
          f"planned {results[('Demonic', True)]['shares'].get(THORNS, 0):.1%}, uniform {demonic_game['uniform']:.1%}")
    return results


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, as the other analysis scripts are:
# `sim/tests/test_analysis_scripts.py` runs them through `runpy.run_path`, which does not set `__name__`.
RESULTS = main()
