"""How large the Explorer branch's walk-time percentage should be. Issue #1383.

WHY THIS EXISTS, AND WHAT IT IS NOT. The project owner ruled the SHAPE on
2026-09-06, verbatim: **"Change to a percentage"**. Four Explorer nodes --
`Temporal Mastery`, `Overclock`, `Pacing` and the `Fleet Footed` keystone --
stop removing a fixed number of days and start removing a percentage of the
dungeon's run time, combined multiplicatively. **The owner did NOT rule the
per-point percentages**, and the analysis that produced the ruling said in as
many words that its own figures were illustrations. This file picks them.

`sim/analyse_explorer_shape.py` answers "which of the three terms in
`run_days_for` should the branch load". This one answers "how much". They are
different questions and the second could not be asked until the first was
answered.

WHAT DECIDES THE ANSWER. Two constraints pull in opposite directions and the
window between them is narrow.

  * **A lower bound, from the gradient.** Walk time is a whole number of days,
    so a reduction that is too steep pushes the shallow end of the ordinary
    dungeon range back onto the one-day minimum -- the collapse the shape change
    exists to remove, reached by a different route. Section 1 measures where
    that starts.

  * **An upper bound, from the owner's second ruling.** `docs/DECISIONS.md`
    records the 2026-09-05 sentence "you could have a 50 floor dungeon that only
    takes you a couple days in world time to beat", and the 2026-09-06 ruling
    that it describes **the whole stack** -- empire tree plus city upgrades plus
    the situational nodes -- rather than the tree alone. A percentage too weak
    cannot reach a couple of days even with everything stacked. Section 1
    measures that too, at three readings of how much "the whole stack" is.

**EVERY OTHER AXIS FAVOURS THE WEAKEST REDUCTION THE WINDOW ALLOWS**, which is
what makes the window worth computing rather than the midpoint worth taking.
More walk lengths survive, the player has fewer empty days, the empire is
marginally less safe, and the genre's own clear-speed investments are far weaker
than either end of this window. Section 2 states the recommendation that follows.

WHAT IS HELD FIXED. Difficulty tier 1, the `triage` policy, **static surges
every 120 days for 5 dungeons**, resolve floor ratio 2.0, escalation 0.10 per
100 days, craft 12 days for +4% of tier width. Those are
`experiments.exp_calibrate`'s choices and what the balance report runs at; the
bare `TuningConfig` default of 4 dungeons a surge is a value calibration
rejects, so a figure here and a figure at 4 are not comparable.

**THE SURGE CADENCE IS THE HALF THIS FILE DOES NOT MOVE**, and the owner has
directed that it rise. Every campaign figure below therefore describes a player
on the cadence the owner called "incredibly low", and the rate chosen here has
to be re-measured once the cadence moves.

HOW UNCERTAINTY IS REPORTED. Eight disjoint seed blocks a row, with the standard
error over the individual campaigns printed beside the standard deviation of the
eight block means, so the two can be read against each other. Issue #1379
records why a gap between two blocks is not a noise floor.
"""

from __future__ import annotations

import json
import math
import os
import pathlib
import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_EXPLORER_AS_DESIGNED,
    TREE_NONE,
    EmpireTree,
    SurgeMode,
    TuningConfig,
)
from cataclysm_sim.engine import Simulation

SIM_ROOT = pathlib.Path(__file__).resolve().parent
TREE_JSON = SIM_ROOT.parent / "docs" / "Empire_Development_Tree_Final.json"
CITY_UPGRADES = SIM_ROOT.parent / "game" / "Data" / "CityUpgrades.csv"

#: Campaigns per seed block, eight blocks a row. `sim/tests/test_analysis_
#: scripts.py` executes this file, so the default has to be a smoke test. The
#: figures put to the owner were taken with `CATACLYSM_RATE_TRIALS=500`, which
#: is 4,000 campaigns a row. An environment variable rather than a command line
#: flag because the test executes this file with `runpy.run_path`, which leaves
#: `sys.argv` pointing at pytest's arguments.
TRIALS = int(os.environ.get("CATACLYSM_RATE_TRIALS", "1"))

BLOCK_COUNT = 8
BLOCKS = tuple((chr(ord("a") + i), i * TRIALS) for i in range(BLOCK_COUNT))

#: Difficulty tier 1 is one active Cataclysm type. Named because three nodes in
#: the graph pay per active type, so every total below is a per-tier figure.
ACTIVE_TYPES = 1

#: The four nodes the ruling changes, and their point counts. Read back off the
#: design document by `assert_the_document_matches` so a rename or a re-pointing
#: breaks this file rather than leaving it quietly wrong.
RULED_NODES = (("Temporal Mastery", 25), ("Overclock", 20), ("Pacing", 10))
KEYSTONE = ("Fleet Footed", 1)

#: **THE RECOMMENDATION.** Per point on the three basic nodes, and the single
#: keystone. Section 2 shows the search these came out of.
PER_POINT = 0.025
KEYSTONE_PERCENT = 0.12

#: What `Fleet Footed` is worth today, in basic points: it removes 5 days where
#: a basic point removes 1. **THE RULING CHANGED THE SHAPE AND NOT THE
#: WEIGHTS**, so the keystone is translated to keep that ratio rather than
#: re-sized, which is why `KEYSTONE_PERCENT` is not a round number. Under a
#: multiplicative design "worth r basic points" is `1 - (1 - p) ** r`.
KEYSTONE_WORTH_BASIC_POINTS = 5

#: The ordinary Basic dungeon range: 8 to 40 floors, the three city sizes a
#: surge targets. The gradient constraint is stated over this range.
ORDINARY = range(8, 41)

#: Depths quoted in the tables, chosen to span the range plus the earned
#: Cataclysm dungeon at 125 floors.
QUOTED_DEPTHS = (11, 20, 32, 50, 125)

#: The dungeon the owner's sentence is about: "a 50 floor dungeon". 51 rather
#: than 50 because `Tactical Entry` halves the walk only ABOVE 50 floors, and a
#: reading that excluded it would leave out the one node in the stack that is
#: already multiplicative.
STACK_FLOORS = 51


# =========================================================================
# The design document, read rather than trusted
# =========================================================================

def load_nodes() -> dict[str, dict]:
    """Every node and capstone option in the graph, by name."""
    with open(TREE_JSON, encoding="utf-8") as handle:
        graph = json.load(handle)
    out: dict[str, dict] = {}
    for node in graph["nodes"]:
        out[node["data"]["name"]] = node["data"]
        for option in node["data"].get("options") or []:
            out[option["name"]] = option
    return out


def city_upgrade_days() -> tuple[float, float]:
    """The Explorer city upgrade's first and fully upgraded values, in days.

    Read out of `game/Data/CityUpgrades.csv` rather than typed, because the
    owner's own worked example used the FIRST value and a fully invested player
    has the third. The difference decides how weak the percentage may be, so it
    is not a detail worth restating from memory.
    """
    wanted = "Explorer_Dungeons_here_take_4_less_days_to_beat"
    with open(CITY_UPGRADES, encoding="utf-8") as handle:
        header = next(handle).rstrip("\n").split(",")
        for line in handle:
            if not line.startswith(wanted + ","):
                continue
            # The Effect column is quoted and contains a comma, so split on the
            # quotes rather than on every comma.
            before, _quoted, after = line.split('"')
            cells = before.split(",")[:-1] + ["effect"] + after.strip().split(",")[1:]
            row = dict(zip(header, cells, strict=True))
            return float(row["Tier1Value"]), float(row["Tier3Value"])
    raise AssertionError(
        f"{wanted!r} is no longer a row in {CITY_UPGRADES.name}. The upper "
        "bound on the percentage comes from what that upgrade removes.")


def assert_the_document_matches(nodes: dict[str, dict]) -> None:
    """The four ruled nodes exist with the point counts this file multiplies.

    Without this the search in section 2 is arithmetic over numbers nobody
    checked, which is the defect issue #1288 found in the Architect branch's
    modelled multiplier.
    """
    for name, points in RULED_NODES + (KEYSTONE,):
        assert name in nodes, (
            f"{name!r} is no longer a node in {TREE_JSON.name}. The 2026-09-06 "
            "ruling names it as one of the four that become a percentage.")
        actual = nodes[name].get("maxPoints", 1)
        assert actual == points, (
            f"{name!r} has {actual} points in {TREE_JSON.name} and this file "
            f"multiplies by {points}. The branch's total percentage is built "
            "from those point counts, so it moves when they do.")


NODES = load_nodes()
assert_the_document_matches(NODES)
BASIC_POINTS = sum(points for _name, points in RULED_NODES)
CITY_FIRST, CITY_FULL = city_upgrade_days()


def total_multiplier(per_point: float = PER_POINT,
                     keystone: float = KEYSTONE_PERCENT) -> float:
    """What the four nodes multiply a dungeon's walk by, at full investment.

    COMBINED MULTIPLICATIVELY, which is what the owner ruled. `55` points at
    -2.5% is `0.975 ** 55` and not `1 - 55 * 0.025`; the second is negative.
    """
    return (1.0 - per_point) ** BASIC_POINTS * (1.0 - keystone)


# =========================================================================
# 1. The window
# =========================================================================

def base_config() -> TuningConfig:
    """The settings every row runs under. See the module docstring."""
    return replace(
        TuningConfig(),
        tier=1,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=120.0,
        surge_dungeon_count=5,
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
    )


BASE = base_config()


def proposed_tree(mult: float) -> EmpireTree:
    """`TREE_EXPLORER_AS_DESIGNED` with the four ruled nodes as a percentage.

    **BUILT BY `replace` FROM THE SHIPPED PRESET, NOT TYPED OUT.** Everything the
    ruling leaves alone -- `Sovereign's Haste` as a per-type flat subtraction,
    the branch's five depth nodes -- has to be carried across unchanged, and a
    hand-written preset here would silently drop whichever of them changed next.
    """
    return replace(TREE_EXPLORER_AS_DESIGNED,
                   name=f"Explorer maxed, x{mult:.3f}",
                   run_days_flat=0.0, run_days_mult=mult)


def walk_days(tree: EmpireTree, floors: int, extra_flat: float = 0.0,
              halve: bool = False) -> int:
    """What the engine's own formula returns, at difficulty tier 1.

    `extra_flat` is anything outside the empire tree -- a city upgrade, a
    capstone option -- and is subtracted with the tree's own flat days, BEFORE
    the multiplier, because that is the order `Simulation.run_days_for` uses and
    the order Diablo 3's cooldown formula uses. `halve` is `Tactical Entry`.

    `test_the_window_matches_the_engine` in `sim/tests/test_explorer_rate.py`
    checks this against `Simulation.run_days_for` rather than trusting it.
    """
    removed = tree.days_removed(ACTIVE_TYPES) + extra_flat
    value = (floors * BASE.days_per_floor - removed) * tree.run_days_mult
    if halve and floors > 50:
        value /= 2.0
    value = max(BASE.run_days_min, min(BASE.run_days_max, value))
    return int(math.ceil(value))


def bare(mult: float) -> EmpireTree:
    """A tree that is nothing but the multiplier, for the gradient constraint.

    THE GRADIENT IS A PROPERTY OF THE PERCENTAGE ALONE and the depth nodes would
    hide it: they push every dungeon deeper, which moves the whole range off the
    one-day minimum for free and would make any percentage look acceptable.
    """
    return EmpireTree(name=f"x{mult:.3f}", run_days_mult=mult)


def gradient(mult: float) -> tuple[int, float]:
    """Distinct walk lengths across the ordinary range, and the share of it
    sitting on the one-day minimum where floor count buys nothing."""
    days = [walk_days(bare(mult), f) for f in ORDINARY]
    on_floor = 100.0 * sum(1 for d in days if d <= BASE.run_days_min) / len(days)
    return len(set(days)), on_floor


#: The three readings of "the whole stack", in flat days removed on top of the
#: tree's own `Sovereign's Haste`. **THE MIDDLE ONE IS THE OWNER'S OWN WORKED
#: EXAMPLE** and is the one the upper bound is taken from.
def stack_levels() -> tuple[tuple[str, float, bool], ...]:
    return (
        ("tree alone", 0.0, False),
        (f"+ Opportunist 5, The Delver 5, city upgrade {CITY_FIRST:g}",
         5.0 + 5.0 + CITY_FIRST, True),
        (f"+ the same with the city upgrade fully bought, {CITY_FULL:g}",
         5.0 + 5.0 + CITY_FULL, True),
    )


CANDIDATE_RATES = (0.100, 0.120, 0.130, 0.150, 0.161, 0.199,
                   total_multiplier(), 0.250, 0.300, 0.400)


def exact_bounds() -> tuple[float, float]:
    """The two bounds, scanned on a fine grid rather than read off the table.

    **NOT TAKEN FROM `CANDIDATE_RATES`**, which is a display list: the first row
    in it that satisfies a constraint is not the place the constraint starts,
    and an earlier version of this file reported x0.150 as the lower bound where
    the real one is a little above x0.125. A bound quoted from a coarse table is
    a bound with an unstated rounding in it.
    """
    lower = min(m / 10000.0 for m in range(500, 5000)
                if gradient(m / 10000.0)[1] == 0.0)
    upper = max(m / 10000.0 for m in range(500, 5000)
                if walk_days(proposed_tree(m / 10000.0), STACK_FLOORS,
                             stack_levels()[1][1], True) <= 3)
    return lower, upper


def section_one() -> dict:
    print("-" * 100)
    print("1. THE WINDOW. What the percentage may be, before any campaign is "
          "run.")
    print("-" * 100)
    print(f"   The gradient constraint is measured on the percentage ALONE, "
          f"over the {ORDINARY[0]}-{ORDINARY[-1]} floor")
    print("   ordinary Basic range. The 'whole stack' columns are walk days on "
          f"a {STACK_FLOORS}-floor dungeon with")
    print("   the maxed tree at difficulty tier 1, Tactical Entry halving it, "
          "and the flat days named below.")
    print()
    levels = stack_levels()
    print(f"{'rate':>8} {'8f':>4} {'40f':>4} {'answers':>8} {'on floor':>9}"
          f"   | {'tree alone':>10} {'+ owner example':>16} "
          f"{'+ upgrade maxed':>16}")
    window = {}
    for mult in CANDIDATE_RATES:
        answers, on_floor = gradient(mult)
        tree = proposed_tree(mult)
        stack = [walk_days(tree, STACK_FLOORS, flat, halve)
                 for _label, flat, halve in levels]
        window[round(mult, 4)] = {
            "answers": answers, "on_floor": on_floor, "stack": stack,
            "shallow": walk_days(bare(mult), ORDINARY[0]),
            "deep": walk_days(bare(mult), ORDINARY[-1])}
        print(f"{mult:>8.3f} {window[round(mult, 4)]['shallow']:>4} "
              f"{window[round(mult, 4)]['deep']:>4} {answers:>8} "
              f"{on_floor:>8.0f}%   | {stack[0]:>10} {stack[1]:>16} "
              f"{stack[2]:>16}")
    print()
    for label, flat, halve in levels:
        print(f"   '{label}': {flat:g} flat days on top of the tree's own "
              f"Sovereign's Haste{', halved by Tactical Entry' if halve else ''}")
    print()

    lower, upper = exact_bounds()
    print(f"  THE LOWER BOUND IS x{lower:.4f}. Below it part of the "
          f"{ORDINARY[0]}-{ORDINARY[-1]} floor range sits on the one-day")
    print("  minimum, which is the collapse the shape change exists to remove, "
          "reached by a different")
    print("  route. Walk time is a whole number of days, so a gradient needs "
          "somewhere to go: the bound is")
    print(f"  where the shallowest ordinary dungeon, {ORDINARY[0]} floors, "
          f"stops rounding up past one day.")
    print()
    print(f"  THE UPPER BOUND IS x{upper:.4f}, from the owner's own worked "
          f"example of the whole stack. Above")
    print(f"  it a {STACK_FLOORS}-floor dungeon with the tree, Opportunist, "
          f"The Delver and one Explorer city")
    print("  upgrade takes four days or more, and the 2026-09-06 ruling reads "
          "'a couple of days' as")
    print("  covering that stack. The ruling's own arithmetic landed on three "
          "days and accepted it.")
    print()
    print("  EVERY OTHER AXIS FAVOURS THE WEAK END OF THAT WINDOW: more walk "
          "lengths survive, the player")
    print("  has fewer empty days, and the empire is marginally less safe. "
          "Section 3 measures the last two.")
    print()
    return {"window": window, "lower": lower, "upper": upper,
            "levels": levels}


# =========================================================================
# 2. The per-point values
# =========================================================================

def section_two(bounds: dict) -> dict:
    print("-" * 100)
    print("2. THE PER-POINT VALUES. What the four nodes have to say to reach "
          "that window.")
    print("-" * 100)
    lower, upper = bounds["lower"], bounds["upper"]
    print(f"   {BASIC_POINTS} points across Temporal Mastery, Overclock and "
          f"Pacing at -p% each, and the Fleet Footed")
    print("   keystone at -k%, combined multiplicatively: (1-p)^"
          f"{BASIC_POINTS} x (1-k). Only whole and half percents")
    print("   are searched, because a node description is read by a player.")
    print()
    print("   THE KEYSTONE IS TIED TO THE THREE BASIC NODES RATHER THAN "
          "SEARCHED SEPARATELY. Fleet Footed")
    print(f"   removes 5 days today where a basic point removes 1, so it is "
          f"worth exactly "
          f"{KEYSTONE_WORTH_BASIC_POINTS} basic")
    print("   points. The ruling changed the shape and not the weights, so k "
          "is whatever preserves that:")
    print(f"   k = 1 - (1-p)^{KEYSTONE_WORTH_BASIC_POINTS}, rounded to a whole "
          "percent. That leaves p as the only free value.")
    print()
    print(f"{'p':>6} {'k':>6} {'total':>8} {'40f walk':>9} {'answers':>8}"
          f"   in window?")
    inside = []
    for tenths in range(10, 61):          # 1.0% to 6.0% in steps of 0.1
        per_point = tenths / 1000.0
        if round(per_point * 1000) % 5:   # whole and half percents only
            continue
        keystone = round(1.0 - (1.0 - per_point) ** KEYSTONE_WORTH_BASIC_POINTS,
                         2)
        total = total_multiplier(per_point, keystone)
        ok = lower <= total <= upper
        if ok:
            inside.append((per_point, keystone, total))
        print(f"{per_point * 100:>5.1f}% {keystone * 100:>5.0f}% "
              f"x{total:>7.4f} {walk_days(bare(total), ORDINARY[-1]):>8}d "
              f"{gradient(total)[0]:>8}   {'yes' if ok else 'no'}")
    print()
    chosen = total_multiplier()
    assert lower <= chosen <= upper, (
        f"the recommended per-point values give x{chosen:.4f}, outside the "
        f"window x{lower:.4f} to x{upper:.4f} that section 1 computed. One of "
        "the two has moved and they are not independent.")
    assert inside, "no whole-or-half-percent value of p lands inside the window"
    weakest = max(inside, key=lambda row: row[2])
    print(f"  ONLY {len(inside)} HALF-PERCENT VALUE(S) OF p LAND INSIDE THE "
          f"WINDOW AT ALL, because tying the keystone")
    print(f"  to p makes the total (1-p)^"
          f"{BASIC_POINTS + KEYSTONE_WORTH_BASIC_POINTS} and half a percent on "
          f"p is a large step on that. The weakest")
    print(f"  is -{weakest[0] * 100:g}% per point with a "
          f"-{weakest[1] * 100:g}% keystone, x{weakest[2]:.4f}.")
    print()
    print("  THE RECOMMENDATION, and it is the weakest of them, because every "
          "axis outside the window")
    print("  favours the weakest reduction that satisfies both bounds:")
    print(f"    Temporal Mastery, Overclock and Pacing   "
          f"-{PER_POINT * 100:g}% of run time per point")
    print(f"    Fleet Footed (keystone)                  "
          f"-{KEYSTONE_PERCENT * 100:g}% of run time")
    print(f"    combined                                 x{chosen:.4f}, "
          f"a reduction of {(1 - chosen) * 100:.1f}%")
    print()
    worth = math.log(1 - KEYSTONE_PERCENT) / math.log(1 - PER_POINT)
    print(f"  The keystone is worth {worth:.2f} basic points at that rate, "
          f"against exactly {KEYSTONE_WORTH_BASIC_POINTS} under the")
    print("  fixed-day design it replaces, so its weight relative to the "
          "branch is carried across.")
    print()
    print(f"  THE MARGIN IS NOT SYMMETRIC AND THAT IS DELIBERATE. x{chosen:.4f} "
          f"sits {100 * (upper - chosen) / upper:.0f}% below the upper bound and")
    print(f"  {100 * (chosen - lower) / lower:.0f}% above the lower one. The "
          f"upper bound is the owner's 'couple of days' reading and the")
    print("  ruling's own arithmetic landed on three days and accepted it; "
          "sitting near it is the point,")
    print("  not an oversight.")
    for per_point, _keystone, total in inside:
        if abs(per_point - PER_POINT) < 1e-9:
            continue
        print(f"  THE OTHER OPTION IN THE WINDOW, -{per_point * 100:g}% per "
              f"point, gives x{total:.4f}: "
              f"{100 * (total - lower) / lower:.0f}% above the")
        print(f"  lower bound and {100 * (upper - total) / upper:.0f}% below "
              f"the upper one, so nearer the middle. It is rejected on")
        print("  section 3's campaign measurement and not on where it sits in "
              "the window.")
    print()
    return {"chosen": chosen, "inside": inside, "weakest": weakest,
            "keystone_worth": worth}


# =========================================================================
# 3. What it does to a campaign
# =========================================================================

class _Recording(Simulation):
    """A campaign that keeps every dungeon it built. Changes no behaviour."""

    def __init__(self, cfg: TuningConfig, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        self.made: list[tuple[int, int]] = []

    def _make_dungeon(self, *args, **kwargs):
        d = super()._make_dungeon(*args, **kwargs)
        self.made.append((d.floors, d.run_days))
        return d


def empty_board_counter(policy):
    """Wrap a policy so free days facing an empty board are counted.

    **THIS IS THE OWNER'S COMPLAINT AND `RunResult.idle_days` IS NOT IT.**
    `idle_days` counts every free day the policy declined, including days with
    dungeons standing that the player judged not worth entering. Draws no random
    numbers, so an instrumented batch runs the same campaigns a bare one would.
    """
    def counted(sim, dungeons):
        if not dungeons:
            sim.empty_days = getattr(sim, "empty_days", 0) + 1
        return policy(sim, dungeons)
    return counted


def measure(tree: EmpireTree, seed0: int, trials: int = TRIALS) -> dict:
    """One block: `trials` campaigns from `seed0` upward under one tree."""
    cfg = BASE.with_tree(tree)
    policy = empty_board_counter(policies.ALL["triage"])
    results, empties, made = [], [], []
    for i in range(trials):
        sim = _Recording(cfg, seed=seed0 + i)
        results.append(sim.run(policy))
        empties.append(getattr(sim, "empty_days", 0))
        made.extend(sim.made)

    days = [max(1, r.survived_days) for r in results]
    at_floor = sum(1 for _f, d in made if d <= cfg.run_days_min)
    return {
        "n": trials,
        "_won": [1.0 if r.won else 0.0 for r in results],
        "_ls": [1.0 if r.last_stand else 0.0 for r in results],
        "_cities": [float(r.cities_lost) for r in results],
        "_days": [float(d) for d in days],
        "_empty": [100.0 * e / d for e, d in zip(empties, days, strict=True)],
        "_cleared": [float(r.dungeons_cleared) for r in results],
        "_walk": [float(d) for _f, d in made],
        "at_floor%": 100.0 * at_floor / max(1, len(made)),
        "dungeons": len(made),
    }


SAMPLES = (("won%", "_won", 100.0), ("LS%", "_ls", 100.0),
           ("cities", "_cities", 1.0), ("days", "_days", 1.0),
           ("empty%", "_empty", 1.0), ("cleared", "_cleared", 1.0),
           ("walk", "_walk", 1.0))


def pool(cells: list[dict]) -> dict:
    """Several blocks read as one sample, with standard errors over campaigns."""
    out = {"n": sum(c["n"] for c in cells),
           "at_floor%": sum(c["at_floor%"] * c["dungeons"] for c in cells)
                        / max(1, sum(c["dungeons"] for c in cells)),
           "dungeons": sum(c["dungeons"] for c in cells)}
    for key, sample, scale in SAMPLES:
        values = [v * scale for c in cells for v in c[sample]]
        out[key] = statistics.fmean(values)
        out[key + "_se"] = (statistics.stdev(values) / math.sqrt(len(values))
                            if len(values) > 1 else 0.0)
    return out


#: The rows measured, in the order they are printed. **THE UNTREED CONTROL AND
#: THE SHIPPED PRESET ARE BOTH HERE ON PURPOSE**: a rate has to be read against
#: what it replaces and against what it costs a player who has no tree at all.
def candidates() -> tuple[tuple[str, EmpireTree], ...]:
    return (
        ("no tree", TREE_NONE),
        ("shipped: flat", TREE_EXPLORER_AS_DESIGNED),
        ("x0.150", proposed_tree(0.150)),
        ("x0.161 (-3%/pt)", proposed_tree(total_multiplier(0.03, 0.14))),
        (f"x{total_multiplier():.3f} PROPOSED", proposed_tree(total_multiplier())),
        ("x0.250", proposed_tree(0.250)),
        ("x0.300", proposed_tree(0.300)),
    )


HEADER = (f"{'candidate':>18} {'blk':>4} {'won%':>6} {'LS%':>6} {'cities':>7} "
          f"{'days':>6} {'empty%':>7} {'cleared':>8} {'walk':>6} "
          f"{'at floor%':>10}")


def section_three() -> dict:
    print("-" * 100)
    print("3. WHAT EACH RATE DOES TO A CAMPAIGN.")
    print("-" * 100)
    print(f"tier {BASE.tier}, policy triage, "
          f"{BASE.surge_mode.name.lower()} surges every "
          f"{BASE.surge_interval_days:g} days for {BASE.surge_dungeon_count} "
          f"dungeons, {TRIALS} campaigns x {BLOCK_COUNT} blocks")
    print("'walk' is the mean run days of every dungeon built; 'at floor%' is "
          "the share of them on the")
    print("one-day minimum. 'cities' is of 25.")
    print()
    print(HEADER)
    out = {}
    for label, tree in candidates():
        cells = [measure(tree, seed0) for _name, seed0 in BLOCKS]
        pooled = pool(cells)
        pooled["_blocks"] = [
            {key: statistics.fmean([v * scale for v in c[sample]])
             for key, sample, scale in SAMPLES} for c in cells]
        out[label] = pooled
        print(f"{label:>18} {'a-h':>4} {pooled['won%']:>6.1f} "
              f"{pooled['LS%']:>6.1f} {pooled['cities']:>7.2f} "
              f"{pooled['days']:>6.0f} {pooled['empty%']:>7.1f} "
              f"{pooled['cleared']:>8.1f} {pooled['walk']:>6.1f} "
              f"{pooled['at_floor%']:>10.1f}")
    print()
    print("  Pooled over all eight blocks, with the standard error over the "
          "campaigns and, after the")
    print("  slash, the standard deviation of the eight block means. Issue "
          "#1379: a gap between two")
    print("  blocks is one difference and not a spread, so both are printed.")
    print()
    print(f"{'candidate':>18} {'cities lost of 25':>26} "
          f"{'empty board days%':>26} {'won%':>22}")
    for label, pooled in out.items():
        cells = []
        for key in ("cities", "empty%", "won%"):
            sd8 = (statistics.stdev([b[key] for b in pooled["_blocks"]])
                   if len(pooled["_blocks"]) > 1 else 0.0)
            cells.append(f"{pooled[key]:>10.2f} +-{pooled[key + '_se']:<5.2f} "
                         f"/{sd8:<5.2f}")
        print(f"{label:>18} {cells[0]:>26} {cells[1]:>26} {cells[2]:>22}")
    print()
    return out


# =========================================================================
# 4. What it costs the uninvested player, and what the loot nodes are worth
# =========================================================================

#: The three Explorer nodes that pay the player for having removed days. Change
#: the shape of the reduction and all three change what they are worth, which is
#: why issue #1390 is part of this change rather than a separate one.
REWARD_NODES = (
    ("Speed Runner", 10, "+5% bonus Loot Quantity per 2 days under default "
     "run time per point (cap 100%)."),
    ("Efficiency Premium", 1, "+5% Loot Quantity for every Day removed from "
     "default run time (cap 50%)."),
    ("One-Day Specialist", 1, "If run time reduced to 1-Day minimum, all "
     "Explorer Loot modifiers doubled for that dungeon."),
)

#: The largest single Loot Quantity effect anywhere else in the design document,
#: which is what issue #1390's proposed cap is sized against. Both are read back
#: out of the graph below rather than trusted.
QUANTITY_YARDSTICKS = (("Bounty", 15, 0.05), ("The Hoarder", 1, 1.00))

#: The cap this change gives Speed Runner. See section 4's own output for how it
#: was sized and `docs/DECISIONS.md` for the sources behind it.
SPEED_RUNNER_CAP = 1.00


def speed_runner(base_floors: int, tree: EmpireTree, extra_flat: float = 0.0,
                 cap: float | None = None) -> float:
    """What Speed Runner pays on a dungeon of this DEFAULT depth, as a fraction.

    "Days under default run time" is the dungeon's own depth in days minus what
    it actually costs to walk, so the tree's depth nodes raise BOTH -- which is
    why the payout barely moves when the reduction's shape changes and grows
    without bound with depth. That is issue #1390's finding, recomputed here
    rather than quoted.
    """
    floors = max(1, int(round(base_floors + tree.floors_added(ACTIVE_TYPES))))
    default = floors * BASE.days_per_floor
    actual = walk_days(tree, floors, extra_flat)
    under = max(0.0, default - actual)
    paid = under / 2.0 * 0.05 * 10          # +5% per 2 days per point, 10 points
    return paid if cap is None else min(paid, cap)


def section_four(bounds: dict) -> dict:
    print("-" * 100)
    print("4. THE THREE NODES THAT PAY FOR REMOVED DAYS -- issue #1390.")
    print("-" * 100)
    for name, points, text in REWARD_NODES:
        assert name in NODES, f"{name!r} is no longer in {TREE_JSON.name}"
        actual = (NODES[name].get("description") or "").strip()
        assert actual == text, (
            f"{name!r} now reads {actual!r} in {TREE_JSON.name} and this file "
            f"expects {text!r}. What it is worth is computed from that text.")
        print(f"  {name:<20} {points:>3} pt  {text}")
    print()
    shipped, proposed = TREE_EXPLORER_AS_DESIGNED, proposed_tree(total_multiplier())
    print("  WHAT SPEED RUNNER PAYS, UNCAPPED, at difficulty tier 1 with the "
          "maxed branch's depth nodes:")
    print(f"{'default depth':>14} {'floors with tree':>17} "
          f"{'shipped flat':>14} {'proposed rate':>15}")
    payouts = {}
    for base_floors in (8, 20, 40, 125):
        with_tree = max(1, int(round(
            base_floors + shipped.floors_added(ACTIVE_TYPES))))
        a = speed_runner(base_floors, shipped)
        b = speed_runner(base_floors, proposed)
        payouts[base_floors] = (a, b)
        print(f"{base_floors:>13}f {with_tree:>16}f "
              f"{a * 100:>13.0f}% {b * 100:>14.0f}%")
    print()
    print("  **THE SHAPE CHANGE DOES NOT REPAIR IT**, which is what issue "
          "#1390 says and this confirms:")
    print(f"  a 40-floor dungeon pays {payouts[40][0] * 100:.0f}% under the "
          f"shipped flat subtraction and {payouts[40][1] * 100:.0f}% under the")
    print("  proposed rate. Most of the floor count is still 'under default "
          "run time' whatever shape the")
    print("  reduction takes, because the branch's own depth nodes raise the "
          "default as well.")
    print()
    print("  WHAT ELSE IN THE DESIGN DOCUMENT PAYS LOOT QUANTITY, which is "
          "what the cap is sized against:")
    for name, points, per_point in QUANTITY_YARDSTICKS:
        assert name in NODES, f"{name!r} is no longer in {TREE_JSON.name}"
        total = points * per_point
        print(f"    {name:<20} {points:>3} pt   +{total * 100:>4.0f}%")
    biggest = max(points * per_point for _n, points, per_point in QUANTITY_YARDSTICKS)
    print(f"  The largest is +{biggest * 100:.0f}%. Uncapped Speed Runner is "
          f"{payouts[40][1] / biggest:.0f} times that on one 40-floor dungeon.")
    print()
    print(f"  WITH A CAP OF +{SPEED_RUNNER_CAP * 100:.0f}%, every row above "
          f"pays the cap and the node stops scaling with depth:")
    for base_floors in (8, 20, 40, 125):
        capped = speed_runner(base_floors, proposed, cap=SPEED_RUNNER_CAP)
        print(f"    {base_floors:>4}f default -> +{capped * 100:.0f}%")
    print()
    print("  EFFICIENCY PREMIUM IS UNAFFECTED IN PRACTICE. Its 50% cap is "
          "reached at 10 days removed, and")
    ep_reached = [f for f in (8, 20, 40, 125)
                  if speed_runner(f, proposed) > 0.0]
    print(f"  every default depth measured here removes more than that: "
          f"{', '.join(f'{f}f' for f in ep_reached)}.")
    print()
    print("  ONE-DAY SPECIALIST IS THE ONE THE SHAPE CHANGE REALLY MOVES.")
    reach = {}
    for label, flat, _halve in bounds["levels"]:
        tree = proposed
        deepest = max((f for f in range(1, 200)
                       if walk_days(tree, f, flat) <= BASE.run_days_min),
                      default=0)
        reach[label] = deepest
        print(f"    {label:<52} reaches one day up to {deepest:>3}f")
    print("  Those are depths AFTER the branch's depth nodes have added their "
          "floors, so a maxed branch")
    print(f"  never reaches them: it adds "
          f"+{proposed.floors_added(ACTIVE_TYPES):g} floors at tier 1 and the "
          "shallowest dungeon in the game is 8.")
    print("  Under the shipped flat subtraction it fires on every ordinary "
          "dungeon. **That is the keystone")
    print("  getting something to pay for**, which the 2026-09-06 ruling says "
          "in as many words -- but it")
    print("  now needs The Last Stand or a shallow build rather than the tree "
          "alone, and nothing has")
    print("  ruled on whether that is far enough.")
    print()
    return {"payouts": payouts, "reach": reach, "biggest": biggest}


def section_five(campaigns: dict) -> None:
    print("-" * 100)
    print("5. WHAT IT COSTS THE UNINVESTED PLAYER.")
    print("-" * 100)
    none = campaigns["no tree"]
    print("  NOTHING, AND THE ROW ABOVE IS THE MEASUREMENT RATHER THAN THE "
          "ARGUMENT. A player with no")
    print("  tree has `run_days_flat` 0 and `run_days_mult` 1.00 whatever the "
          "branch's nodes say, so")
    print("  changing what those nodes do cannot move their walk times. The "
          "untreed row is run in the")
    print("  same batch as every candidate for exactly that reason:")
    print(f"    cities lost of 25   {none['cities']:.2f} +-{none['cities_se']:.2f}")
    print(f"    won                 {none['won%']:.1f}%")
    print(f"    reached Last Stand  {none['LS%']:.1f}%")
    print(f"    empty board days    {none['empty%']:.1f}%")
    print(f"    mean walk days      {none['walk']:.1f}")
    print()
    print("  Investment still has to be worth something, and it plainly is: "
          "every candidate takes")
    print("  cities lost far below the untreed row. The balance target the "
          "owner set is about the")
    print("  invested player, and its qualifier -- 'or at least to some "
          "degree' -- is what stops this")
    print("  licensing a change that only works for one of them.")
    print()


def main() -> dict:
    print("=" * 100)
    print("HOW LARGE THE EXPLORER BRANCH'S WALK-TIME PERCENTAGE SHOULD BE "
          "-- issue #1383")
    print("=" * 100)
    print()
    bounds = section_one()
    values = section_two(bounds)
    campaigns = section_three()
    loot = section_four(bounds)
    section_five(campaigns)
    return {"bounds": bounds, "values": values, "campaigns": campaigns,
            "loot": loot}


REPORT = main()
