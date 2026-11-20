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

RE-MEASURED SINCE DEMONIC FOLLOWS THE FRONT LINE (2026-11-19, the next slice of
issue #2749), at the same 300 seeds and 20 deepest. Until then a Demonic first
surge put some dungeons on Bulwarks and Sanctuaries, which the game never does,
and every figure above was measured that way. Now the first surge makes 3,171
Outpost ordinary dungeons; full clears are 359,699, 503,020 and 540,646 at 5, 7
and 8 floors, and the levels above move by at most a point of their shares. The
scale table keeps its shape: 0.05 passes all 3,171 at a quarter's, a half's
and every kill, 0.055 at a quarter's still leaves 56 under 10% health (3,115),
and 0.06 at a half's still leaves 63 (3,108), so 0.05 is still the largest step
of 0.01 that passes. SECTION 4 MOVED: the 20 deepest ordinary dungeons of the
first three surges were 33 to 39 floors, all of them Demonic dungeons behind the
line; they are now 16, and the Quest ones 45 to 48 against 46 to 49. At 0.05 a
level 20 to 40 Ravager ends them with 80% to 93% of its health, against 79% to
93%; at 0.20 a level 20 one now wins all 20 ordinary and all 20 Quest ones,
against 17 of 20 of each. `.claude/windows/demonic/opening_before.out` and
`opening_after.out`, in the worktree that made the change, are both runs.

THE RING SLICE, sections 5 and 6. The project owner ruled on 2026-11-19 that tier
1 creature damage rises by city ring: Outposts stay at 0.05, Bulwark and
Sanctuary dungeons hit harder. Section 5 runs tier 1 campaigns under every
policy, with Demonic following the front line as the game's does (through a
helper in the pack test when this was measured; the simulation's own rule since
the next slice of issue #2749, which gives the same clears), logs every dungeon
cleared and the level the
character held when it entered, and walks each again on its ring at each scale.
Measured 2026-11-19 on seeds 0 to 59 (17,925 clears) in 426 seconds with the
first sections at full size; the largest step of 0.01 at which none of a ring's
clears is lost and none ends under 10% health:

    ring       clears   level at entry, median   largest passing   median health left there
    Outpost    14,642   17 to 24 by type          0.05 (only one walked)   82%
    Bulwark     2,039   28 to 32                  0.11              68%
    Sanctuary   1,025   30 to 35                  0.13              64%
    Pillar        219   24                        0.10              50%

The coordinating session ruled those four the same day. On seeds 60 to 119, never
used to choose them, every one passes again. Section 6 is the measurement tiers 2
to 8 were left flat by ring on: a campaign gains a median 26 levels at tier 1,
14 at tier 2 and 11 at tier 3, and the starting class's health at the median
Sanctuary entry over the median Outpost entry is 1.55, 1.30 and 1.14 times.

RUNNING IT. The default is a smoke run, 20 seeds and the 3 deepest dungeons, 3
campaign seeds a policy and 2 a tier for section 6, which proves the code path and
nothing else, and the output says so. For the figures above:

    CATACLYSM_OPENING_SEEDS=300 CATACLYSM_OPENING_DEEPEST=20 CATACLYSM_OPENING_CAMPAIGNS=60 \\
        CATACLYSM_OPENING_TIER_GAIN=40 python -u sim/analyse_opening.py

and for the check on seeds the ring figures were not chosen on:

    CATACLYSM_OPENING_CAMPAIGNS=60 CATACLYSM_OPENING_CAMPAIGNS_FROM=60 python -u sim/analyse_opening.py
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

#: How many seeds of each policy's tier 1 campaign section 5 runs, and the first. 60 from 0 is the measurement the ring
#: figures were fitted on and 60 from 60 the check on seeds it never saw; the default is a smoke run.
CAMPAIGN_SEEDS = int(os.environ.get("CATACLYSM_OPENING_CAMPAIGNS", "3"))
CAMPAIGN_FROM = int(os.environ.get("CATACLYSM_OPENING_CAMPAIGNS_FROM", "0"))
#: How many seeds of each policy's campaign section 6 runs at each of tiers 1 to 3. 40 is the measurement.
TIER_GAIN_SEEDS = int(os.environ.get("CATACLYSM_OPENING_TIER_GAIN", "2"))
#: The tier 1 damage scales section 5 walks each ring's clears at. Every ring is walked at 0.05, the scale every ring
#: had before the ring slice, and the inner rings at 0.20, tier 1's before issue #2749; the steps of 0.01 around each
#: ruled figure find the largest that passes.
RING_SCALES = {
    "Outpost": (0.05,),
    "Bulwark": (0.05, 0.10, 0.11, 0.12, 0.15, 0.20),
    "Sanctuary": (0.05, 0.10, 0.12, 0.13, 0.14, 0.15, 0.20),
    "Pillar": (0.05, 0.08, 0.10, 0.11, 0.12, 0.13, 0.15, 0.20),
}


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
    print()

    results["rings"] = section_five()
    results["tier_gain"] = section_six()
    results["skill_levels"] = section_seven()
    print(f"Done in {time.time() - started:.0f} seconds.")
    return results


def _pct(values: list, share: float):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def section_five() -> dict:
    """The ring figures: every dungeon tier 1 campaigns clear, by the ring its depth was rolled on, walked again from
    the level the character held when it entered."""
    seeds = range(CAMPAIGN_FROM, CAMPAIGN_FROM + CAMPAIGN_SEEDS)
    print("5. THE RING FIGURES: WHAT TIER 1 CAMPAIGNS CLEAR ON EACH RING, AND AT WHAT LEVEL")
    print()
    if CAMPAIGN_SEEDS < 60:
        print(f"  SMOKE RUN: {CAMPAIGN_SEEDS} seeds a policy. The figures in docs/DECISIONS.md are 60 from seed 0, and 60")
        print("  from seed 60 for the check; set CATACLYSM_OPENING_CAMPAIGNS and CATACLYSM_OPENING_CAMPAIGNS_FROM.")
        print()
    clears = PACK.campaign_clears(MODEL, seeds)
    out: dict = {"clears": len(clears)}
    print(f"  {len(clears)} clears in the tier 1 campaigns of seeds {seeds.start} to {seeds.stop - 1} under each of the")
    print("  simulation's policies, the character banking half of each floor's experience, with Demonic following the")
    print("  front line as the game's does (the simulation's own rule since issue #2749; the fit used a helper for it).")
    print()
    print("  ring       | type        | clears | level at entry: 10th, median, 90th, lowest | floors, median")
    print("  " + "-" * 92)
    levels: dict = {}
    for ring in RING_SCALES:
        for kind in ("Basic", "Quest", "Fallen City", "Cataclysm"):
            these = [c for c in clears if c.ring == ring and c.dungeon_type == kind]
            if not these:
                continue
            entry = [PACK.level_for(c.banked, MODEL) for c in these]
            levels[(ring, kind)] = entry
            print(f"  {ring:<10} | {kind:<11} | {len(these):>6} | {_pct(entry, 0.1):>4} {statistics.median(entry):>7}"
                  f" {_pct(entry, 0.9):>5} {min(entry):>6}   | {statistics.median(c.floors for c in these):>5}")
    out["levels"] = levels
    moved = collections.Counter((c.ring, c.standing_on, c.dungeon_type) for c in clears
                                if c.standing_on is not None and c.standing_on != c.ring)
    out["moved"] = sum(moved.values())
    print()
    print(f"  {out['moved']} of the {len(clears)} stood on a ring other than the one their depth was rolled on when")
    print("  cleared; each keeps its own ring's figure. (rolled on, standing on, type): "
          + ", ".join(f"{k}: {v}" for k, v in moved.most_common()))
    print()
    print("  Each ring's clears at each scale: lost, won with under 10% health left, and the health left (10th")
    print("  percentile and median). The rule, the one that chose the Outpost's 0.05: the largest step of 0.01 at")
    print("  which none is lost and none is under 10%.")
    print()
    sweep: dict = {}
    for ring, scales in RING_SCALES.items():
        these = [c for c in clears if c.ring == ring]
        if not these:
            continue
        shipped = es.damage_scale(1, ring)
        print(f"  {ring}: {len(these)} clears, {min(c.floors for c in these)} to {max(c.floors for c in these)} floors")
        passing = []
        for scale in scales:
            walked = [PACK.walk_clear(c, model=MODEL, lines=LINES, constants=CONSTANTS, slots=SLOTS, weapon=WEAPON,
                                      first_tier_scale=scale) for c in these]
            lost = sum(1 for w in walked if not w["won"])
            low = sum(1 for w in walked if w["won"] and w["left"] < PACK.LEAST_HEALTH_LEFT)
            lefts = [w["left"] for w in walked]
            sweep[(ring, scale)] = (lost, low, len(walked), statistics.median(lefts))
            if lost == low == 0:
                passing.append(scale)
            mark = "  <- shipped on this ring" if abs(scale - shipped) < 1e-9 else ""
            print(f"    {scale:.3f} | lost {lost:>5} | under 10% {low:>5} | left {_pct(lefts, 0.1):>4.0%} "
                  f"{statistics.median(lefts):>4.0%}{mark}")
        print(f"    largest passing here: {max(passing) if passing else '-'}; shipped {shipped}")
        print()
    out["sweep"] = sweep
    print("  The pack target only, as in section 3: ten Common Imps, no Elites, bosses or modifier effects, and the")
    print("  gear-level-0 Greataxe. The rule is bound by the weakest entries, not the median.")
    print()
    print()
    return out


def section_six() -> dict:
    """The levels a character gains inside one campaign at tiers 1, 2 and 3, each entered at the median level the tier
    before ended at: the measurement tiers 2 to 8 were left flat by ring on."""
    print("6. LEVELS GAINED INSIDE A CAMPAIGN, TIERS 1 TO 3")
    print()
    if TIER_GAIN_SEEDS < 40:
        print(f"  SMOKE RUN: {TIER_GAIN_SEEDS} seeds a policy a tier. The figures in docs/DECISIONS.md are 40; set")
        print("  CATACLYSM_OPENING_TIER_GAIN.")
        print()
    cumulative = [0, 0]
    for level in range(2, MODEL["MAX_LEVEL"] + 1):
        cumulative.append(cumulative[-1] + MODEL["whole_level_cost"](level))
    out: dict = {}
    start = 1
    for tier in (1, 2, 3):
        clears = PACK.campaign_clears(MODEL, range(TIER_GAIN_SEEDS), tier=tier,
                                      banked_at_start=float(cumulative[start]))
        ends = {(c.policy, c.seed): c.banked_after for c in clears}
        finals = [PACK.level_for(total, MODEL) for total in ends.values()]
        entry = {ring: [PACK.level_for(c.banked, MODEL) for c in clears if c.ring == ring] for ring in RING_SCALES}
        health = {ring: PACK.stat(LINES, CONSTANTS, CLASS, "max_health", int(statistics.median(levels)))
                  for ring, levels in entry.items() if levels}
        out[tier] = {"start": start, "end_median": statistics.median(finals),
                     "entry_median": {ring: statistics.median(v) for ring, v in entry.items() if v}}
        print(f"  tier {tier}: entered at level {start}; ends at level {statistics.median(finals)} (median), levels "
              f"gained {statistics.median(finals) - start}")
        for ring, levels in entry.items():
            if levels:
                print(f"    {ring:<10} entered at level {statistics.median(levels)} (median), starting class health "
                      f"{health[ring]:.0f}")
        if "Outpost" in health and "Sanctuary" in health:
            print(f"    Sanctuary over Outpost, in health: {health['Sanctuary'] / health['Outpost']:.2f} times")
        start = int(statistics.median(finals))
    print()
    print("  Only campaigns that cleared something count; each tier is entered at the median end of the one before.")
    print()
    return out


def section_seven() -> dict:
    """Skill levels over a first Outpost dungeon of 5 to 8 floors, issue #2764: the pack fight with each rotation slot
    counted, its share of the damage paid in experience at half each floor's kills and split by the health each slot
    removed, its level from `cataclysm_sim.skill_levels`, and its blows multiplied by that level's factor on the next
    floor. The basic attack does not level. This is the measurement the plan quoted: the skill levels reached, and what
    they do to a fight whose creatures' health rises about four times over the floors."""
    from cataclysm_sim import enemy_stats as es
    from cataclysm_sim import skill_levels as sl

    print("7. SKILL LEVELS OVER A FIRST DUNGEON OF 5 TO 8 FLOORS (issue #2764)")
    print()
    total = MODEL["whole_total_to_reach"]
    dungeons = [d for d in PACK.first_surge_dungeons(range(SEEDS)) if 5 <= d.floors <= 8]
    ends = collections.Counter()
    shares = collections.defaultdict(list)
    by_floor = collections.defaultdict(lambda: collections.defaultdict(list))
    for dungeon in dungeons:
        banked = 0.0
        earned: dict = {}
        for floor in range(1, dungeon.floors + 1):
            level = PACK.level_for(banked, MODEL)
            skill_level = {slot: sl.level_for(earned.get(slot, 0.0), level, total) for slot in PACK.ROTATION}
            factors = {slot: sl.damage_factor(skill_level[slot]) for slot in PACK.ROTATION}
            imp = es.strength_on_floor("Common", 1, "Basic", dungeon.subtype, dungeon.floors, floor,
                                       dungeon.modifier_score, kind="Imp")
            dealt: dict = {}
            fought = PACK.fight(CLASS, level=level, lines=LINES, slots=SLOTS, weapon=WEAPON, slot_factor=factors,
                                dealt=dealt, constants=dict(CONSTANTS, imp_health=str(imp.health),
                                                            imp_damage=str(imp.damage_per_hit)))
            whole = sum(dealt.values())
            for slot, amount in dealt.items():
                shares[slot].append(amount / whole if whole else 0.0)
            pay = PACK.KILL_SHARE * PACK.floor_experience(MODEL, dungeon.subtype, dungeon.floors, floor,
                                                          dungeon.modifier_score)
            for slot, gain in sl.split_a_kill(pay, {s: dealt.get(s, 0.0) for s in PACK.ROTATION}, whole).items():
                earned[slot] = earned.get(slot, 0.0) + gain
            row = by_floor[floor]
            row["level"].append(level)
            row["heavy"].append(skill_level["Heavy"])
            row["factor"].append(factors["Heavy"])
            row["seconds"].append(fought["seconds"])
            row["imp"].append(imp.health)
            banked += pay
        end_level = PACK.level_for(banked, MODEL)
        ends[(end_level, sl.level_for(earned.get("Heavy", 0.0), end_level, total))] += 1

    print(f"  {len(dungeons)} first-surge Outpost dungeons of 5 to 8 floors, seeds 0 to {SEEDS - 1}.")
    print("  damage share by slot, median: " + ", ".join(
        f"{slot} {statistics.median(values):.0%}" for slot, values in sorted(shares.items())))
    print("  at the end, (character level, Heavy skill level): " + ", ".join(
        f"{key}: {count}" for key, count in sorted(ends.items())))
    print()
    print("  floor | Imp health | character level | Heavy level | Heavy factor | pack fight, seconds (medians)")
    print("  " + "-" * 88)
    for floor in sorted(by_floor):
        row = by_floor[floor]
        print(f"  {floor:>5} | {statistics.median(row['imp']):>10.1f} | {statistics.median(row['level']):>15} | "
              f"{statistics.median(row['heavy']):>11} | {statistics.median(row['factor']):>12.3f} | "
              f"{statistics.median(row['seconds']):.1f}")
    print()
    print("  The basic attack does not level. No gear but the gear-level-0 Greataxe, as in the pack test.")
    print()
    return {"dungeons": len(dungeons), "ends": ends}


def _spread(counter: collections.Counter) -> str:
    total = sum(counter.values())
    return ", ".join(f"{level} ({count / total:.0%})" for level, count in sorted(counter.items()))


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, as the other analysis scripts are:
# `sim/tests/test_analysis_scripts.py` runs them through `runpy.run_path`, which does not set `__name__`.
RESULTS = main()
