"""The opening for a character that starts at level 1 -- issue #2749, slice 4.

WHY THIS EXISTS. A new character starts at level 1 since issue #2744. The project
owner played it on 2026-11-18 and died 11 to 17 seconds into floor 1 of tier 1,
and ruled to "make the first levels much cheaper, so a new character gains several
levels in its first dungeon as in Path of Exile's first areas. Then re-set tier 1
enemy damage so a character levelling at that pace survives." This file is the
measurement both halves were decided on, and the measurement the coordinating
session added. It writes no constant.

THE METHOD. The dungeons are the Outpost ordinary dungeons the simulation's first
surge makes (`Simulation.trigger_surge` on day 0), one wave a seed: 3 to 16
floors, every sub-type a surge rolls, modifier scores 5 to 50. A floor pays its
creatures' design scores, which ARE their experience, at the rarity spawn weights
and the population `analyse_experience_curve.py` uses (257.5 a floor), its boss on
the last floor. The fight is the pack test's own, run from
`tools/tests/test_the_starting_character_survives_a_pack.py` so the measurement
and the guard cannot part company: 10 Common Imps as the game builds them on that
floor, against the starting Ravager with its gear-level-0 Greataxe and the
Slashing 10%, paying a skill's mana at its level. A walk fights each floor at the
level held when the floor began, then banks a share of the floor's experience.
LOADING THAT FILE NEEDS `pytest` (`sim/requirements-dev.txt`), which this script
does not use otherwise; the simulation itself still has no dependency.

WHAT IT MEASURED, 2026-11-19, at 300 seeds (3,057 dungeons) and the 20 deepest, on
development b67599db5 with this slice, in 58 seconds. Full clear of a first-surge
dungeon, median: 353,256 at 5 floors, 499,119 at 7, 530,341 at 8. The level
reached at the end:

    floors  share  curve alone          with the opening
       5     25%   1                    3
       5     50%   1                    4 (95%), 3 (5%)
       5    100%   2                    5
       7     25%   1                    3
       7     50%   2 (62%), 1 (38%)     4 (95%), 5 (5%)
       7    100%   3 (51%), 2 (49%)     6 (51%), 5 (49%)
       8     50%   2 (96%), 1 (4%)      4 (59%), 5 (41%)
       8    100%   3 (84%), 2 (16%)     6 (82%), 5 (18%)

The tier 1 damage scale, levelling at half each floor's kills, 3,057 dungeons --
every floor won, and won with at least 10% health left on the last:

    0.20:  0 and 0             0.08: 1,539 and 775        0.07: 2,726 and 2,355
    0.06:  3,057 and 2,994 (the 63 short are shallow Elite and Volatile ones)
    0.055: 3,057 and 3,057; at a quarter's kills, 3,057 and 3,001
    0.05:  3,057 and 3,057 at a quarter's, a half's and every kill

0.05 is the largest step of 0.01 that passes. The coordinating session ruled it,
and the opening of 40 times Path of Exile's costs, on 2026-11-19; both are
judgements, and so is the half share. `docs/DECISIONS.md` has the rest.

THE ADDED MEASUREMENT, ruled "report it, and I decide": 0.20 was fitted to a level
20 Ravager, and tier 1 runs to about level 42, so 0.05 also makes the middle and
end of tier 1 four times gentler. Section 4 fights the level 20 Ravager's own
reference dungeon at both scales, and level 20, 30 and 40 Ravagers through the
deepest dungeons the first three surges make.

RUNNING IT. The default is a smoke run, 20 seeds and the 3 deepest dungeons, which
proves the code path and nothing else, and the output says so. For the figures
above:

    CATACLYSM_OPENING_SEEDS=300 CATACLYSM_OPENING_DEEPEST=20 python -u sim/analyse_opening.py
"""

from __future__ import annotations

import collections
import importlib.util
import os
import pathlib
import statistics
import time

from cataclysm_sim import enemy_stats as es
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import Simulation

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PACK_TEST = REPO_ROOT / "tools" / "tests" / "test_the_starting_character_survives_a_pack.py"

#: How many seeds' first surges are walked. 300 is the measurement; the default is a smoke run.
SEEDS = int(os.environ.get("CATACLYSM_OPENING_SEEDS", "20"))
#: How many of the deepest ordinary and Quest dungeons section 4 walks. 20 is the measurement.
DEEPEST = int(os.environ.get("CATACLYSM_OPENING_DEEPEST", "3"))
SMOKE = SEEDS < 300

#: The tier 1 damage scales compared. 0.20 is tier 1's figure before issue #2749; 0.05 is the ruled one.
CANDIDATE_SCALES = (0.20, 0.10, 0.08, 0.07, 0.06, 0.055, 0.05, 0.04)
#: The shares of each floor's creatures killed. A half is the one the ruling was made at.
SHARES = (0.25, 0.5, 1.0)
#: The scale that ships, read from the simulation, which `tools/tests/test_content_relief_port.py` holds to the game's.
SHIPPED_SCALE = es.DAMAGE_SCALE_AT_FIRST_TIER
#: What tier 1 was before issue #2749.
FORMER_SCALE = 0.20
#: The levels section 4 fights at.
FIXED_LEVELS = (20, 30, 40)


def _load_pack():
    spec = importlib.util.spec_from_file_location("starting_character_pack", PACK_TEST)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACK = _load_pack()
CONSTANTS = PACK.read_constants()
LINES = PACK.read_class_lines()
SLOTS = PACK.read_slots()
WEAPON = PACK.read_weapon(CONSTANTS)
MODEL = PACK.experience_model()
CLASS = CONSTANTS["starting_class"]


def level_for(total: float, cost) -> int:
    """The level a total reaches from level 1, `cost` giving each level's price."""
    level, paid = 1, 0
    while level < MODEL["MAX_LEVEL"] and paid + cost(level + 1) <= total:
        paid += cost(level + 1)
        level += 1
    return level


def full_clear(dungeon) -> float:
    return sum(PACK.floor_experience(MODEL, dungeon.subtype, dungeon.floors, floor, dungeon.modifier_score)
               for floor in range(1, dungeon.floors + 1))


def fight_at(level: int, dungeon_type: str, subtype: str, floors: int, floor: int, modifier_score: float,
             scale: float, slots=None) -> dict:
    """The pack on one floor at a fixed level and a given tier 1 damage scale."""
    imp = es.strength_on_floor("Common", 1, dungeon_type, subtype, floors, floor, modifier_score, kind="Imp")
    damage = imp.damage_per_hit / es.damage_scale(1) * scale
    return PACK.fight(CLASS, level=level, lines=LINES, slots=SLOTS if slots is None else slots, weapon=WEAPON,
                      constants=dict(CONSTANTS, imp_health=str(imp.health), imp_damage=str(damage)))


def walk_at_fixed_level(level: int, dungeon_type: str, subtype: str, floors: int, modifier_score: float,
                        scale: float, slots=None) -> tuple[bool, int | None, float]:
    """Every floor at one level: (won every floor, the first floor lost, health share left on the last)."""
    left = 0.0
    for floor in range(1, floors + 1):
        result = fight_at(level, dungeon_type, subtype, floors, floor, modifier_score, scale, slots)
        if result["won"] is not True:
            return False, floor, 0.0
        left = result["health_left"] / result["max_health"]
    return True, None, left


def slots_charged_at_level_100(level: int) -> dict:
    """The slots pre-divided so the fight, which charges mana at `level`, charges the level 100 figures: how the pack
    test charged every level until issue #2749, and how 0.20 was fitted."""
    shared = CONSTANTS["shared_line"]
    reference = PACK.stat(LINES, CONSTANTS, shared, "max_mana", int(CONSTANTS["mana_cost_reference_level"]))
    share = PACK.stat(LINES, CONSTANTS, shared, "max_mana", level) / reference
    return {name: dict(spec, mana=spec["mana"] / share, mana_on_hit=spec["mana_on_hit"] / share)
            for name, spec in SLOTS.items()}


def first_three_surges(seeds) -> list:
    """Every dungeon the first three surges make, called back to back on day 0 -- the frontier does not move between
    them, which is an approximation -- as (floors, type, sub-type, modifier score)."""
    out = []
    for seed in seeds:
        sim = Simulation(TuningConfig(), seed)
        for _ in range(3):
            sim.trigger_surge()
        out += [(d.floors, d.dtype.name, d.subtype, d.modifier_score) for d in sim.dungeons.values()]
    return out


def main() -> dict:
    started = time.time()
    results: dict = {}
    if SMOKE:
        print(f"SMOKE RUN: {SEEDS} seeds and the {DEEPEST} deepest dungeons. The figures in the docstring are 300 "
              "seeds and 20; set CATACLYSM_OPENING_SEEDS and CATACLYSM_OPENING_DEEPEST.")
        print()

    dungeons = PACK.first_surge_dungeons(range(SEEDS))
    results["dungeons"] = len(dungeons)
    print("1. THE FIRST SURGE'S OUTPOST ORDINARY DUNGEONS")
    print()
    print(f"  {len(dungeons)} dungeons from seeds 0 to {SEEDS - 1}.")
    print("  floors:     " + ", ".join(f"{k}: {v}" for k, v in sorted(collections.Counter(
        d.floors for d in dungeons).items())))
    print("  sub-types:  " + ", ".join(f"{k}: {v}" for k, v in sorted(collections.Counter(
        d.subtype for d in dungeons).items())))
    mods = [d.modifier_score for d in dungeons]
    print(f"  modifier score: median {statistics.median(mods):g}, least {min(mods):g}, most {max(mods):g}")
    print()
    print()

    print("2. THE EXPERIENCE A FIRST DUNGEON PAYS, AND THE LEVEL IT REACHES")
    print()
    curve_alone, opening = MODEL["curve_whole_level_cost"], MODEL["whole_level_cost"]
    print("  floors | full clear, median | share | level, curve alone | level, with the opening")
    print("  " + "-" * 86)
    levels: dict = {}
    for floors in (5, 7, 8):
        these = [d for d in dungeons if d.floors == floors]
        if not these:
            continue
        cleared = [full_clear(d) for d in these]
        for share in SHARES:
            before = collections.Counter(level_for(share * c, curve_alone) for c in cleared)
            after = collections.Counter(level_for(share * c, opening) for c in cleared)
            levels[(floors, share)] = after
            print(f"  {floors:>6} | {statistics.median(cleared):>18,.0f} | {share:>5.0%} | "
                  f"{_spread(before):>18} | {_spread(after)}")
    results["levels"] = levels
    print()
    print("  The share of creatures a player kills is not measured (issue #925). The opening was fitted at a half.")
    print()
    print()

    print("3. TIER 1'S DAMAGE SCALE: A NEW CHARACTER LEVELLING THROUGH EVERY ONE OF THOSE DUNGEONS")
    print()
    print("  scale | share | every floor won | and 10% health left on the last")
    print("  " + "-" * 66)
    sweep: dict = {}
    for scale in CANDIDATE_SCALES:
        for share in SHARES:
            walked = [PACK.walk(d, model=MODEL, lines=LINES, constants=CONSTANTS, slots=SLOTS, weapon=WEAPON,
                                kill_share=share, first_tier_scale=scale) for d in dungeons]
            won = sum(1 for w in walked if w["won"])
            healthy = sum(1 for w in walked if w["won"] and w["left"] >= PACK.LEAST_HEALTH_LEFT)
            sweep[(scale, share)] = (won, healthy, len(walked))
            mark = "  <- shipped" if scale == SHIPPED_SCALE and share == PACK.KILL_SHARE else ""
            print(f"  {scale:>5.3f} | {share:>5.0%} | {won:>6} of {len(walked):<6} | {healthy:>6} of {len(walked)}"
                  + mark)
    results["sweep"] = sweep
    print()
    print()

    print("4. THE ADDED MEASUREMENT: OLDER CHARACTERS IN TIER 1, AT 0.20 AND AT THE SHIPPED SCALE")
    print()
    print("  The fight 0.20 was fitted to: a level 20 Ravager, a 12-floor Basic dungeon with no sub-type, health left on")
    print("  floor 12.")
    reference: dict = {}
    for label, slots in (("mana at level 100's figures, as it was fitted", slots_charged_at_level_100(20)),
                         ("mana at the level", None)):
        for scale in (FORMER_SCALE, SHIPPED_SCALE):
            won, _, left = walk_at_fixed_level(20, "Basic", "None", 12, 0.0, scale, slots)
            reference[(label, scale)] = (won, left)
            print(f"    {label:<48} {scale:.2f}: every floor won {won}, health left {left:.1%}")
    results["reference"] = reference
    print()
    deep = first_three_surges(range(SEEDS))
    deep.sort(reverse=True)
    ordinary = [d for d in deep if d[1] == "BASIC"][:DEEPEST]
    quest = [d for d in deep if d[1] == "QUEST"][:DEEPEST]
    print(f"  The first three surges of seeds 0 to {SEEDS - 1}: {len(deep)} dungeons. The {DEEPEST} deepest ordinary "
          f"ones are {ordinary[-1][0]} to {ordinary[0][0]} floors; Quest, {quest[-1][0]} to {quest[0][0]}.")
    print()
    print("  kind     | level | scale | every floor won | health left on the last floor, median and least")
    print("  " + "-" * 92)
    deep_results: dict = {}
    for kind, pool in (("ordinary", ordinary), ("Quest", quest)):
        for level in FIXED_LEVELS:
            for scale in (FORMER_SCALE, SHIPPED_SCALE):
                walked = [walk_at_fixed_level(level, "Basic" if k == "BASIC" else "Quest", sub, floors, mod, scale)
                          for floors, k, sub, mod in pool]
                lefts = sorted(w[2] for w in walked if w[0])
                deep_results[(kind, level, scale)] = (sum(1 for w in walked if w[0]), len(walked), lefts)
                spread = f"{statistics.median(lefts):.0%}, {lefts[0]:.0%}" if lefts else "-"
                print(f"  {kind:<8} | {level:>5} | {scale:.2f} | {len(lefts):>6} of {len(walked):<6} | {spread}")
    results["deep"] = deep_results
    print()
    print("  The pack target only: no Elites, Legendaries, bosses, other archetypes or modifier effects, and no gear")
    print("  beyond the gear-level-0 Greataxe, so a real character of those levels does better at both scales.")
    print()
    print(f"Done in {time.time() - started:.0f} seconds.")
    return results


def _spread(counter: collections.Counter) -> str:
    total = sum(counter.values())
    return ", ".join(f"{level} ({count / total:.0%})" for level, count in sorted(counter.items()))


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, as the other analysis scripts are:
# `sim/tests/test_analysis_scripts.py` runs them through `runpy.run_path`, which does not set `__name__`.
RESULTS = main()
