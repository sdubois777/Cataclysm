"""The claims `sim/analyse_explorer_rate.py` makes about how large the Explorer
branch's walk-time percentage should be.

WHY THIS EXISTS. Issue #1383. The project owner ruled on 2026-09-06 that four
Explorer nodes change from "-1 day per point" to a percentage of the dungeon's
run time, and ruled nothing about how large the percentage should be. That
script picks it, and the pick rests on four claims that are structural rather
than statistical:

1. **The lower bound is real arithmetic and not a preference.** Below it the
   shallowest ordinary dungeon rounds back down to the one-day minimum, which is
   the collapse the whole change exists to remove, reached by another route.
2. **The upper bound is the owner's own worked example of "the whole stack".**
   A 51-floor dungeon with the tree, `Opportunist`, `The Delver` and one
   Explorer city upgrade has to come to three days or fewer.
3. **The chosen per-point values land inside that window and are what the
   simulation models.** Three records carry them -- the node graph, the preset
   and the script -- and a wrong one is invisible.
4. **The script's own walk-day arithmetic agrees with the shipped engine.** It
   recomputes rather than running a `Simulation`, so it could drift and print a
   table describing nothing.

WHAT IS NOT CHECKED HERE. No campaign figure. The script runs at one campaign a
block by default, where every measured number is noise, and pinning noise is
checking the sampler. `tools/tests/test_explorer_walk_time_shape_ruling.py`
checks the design document and the decision log; this checks the reasoning.
"""

from __future__ import annotations

import contextlib
import io
import math
import pathlib
import runpy

import pytest

from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED
from cataclysm_sim.engine import Simulation

SIM_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = SIM_ROOT / "analyse_explorer_rate.py"


@pytest.fixture(scope="module")
def report():
    """Run the script once at its smoke-test size and hand back its globals.

    The trial count is pinned here rather than left to the environment so that a
    shell with `CATACLYSM_RATE_TRIALS` set -- which is how the figures for issue
    #1383 were taken -- cannot turn this file into a twenty minute test.
    """
    import os
    previous = os.environ.get("CATACLYSM_RATE_TRIALS")
    os.environ["CATACLYSM_RATE_TRIALS"] = "1"
    try:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            namespace = runpy.run_path(str(SCRIPT))
    finally:
        if previous is None:
            os.environ.pop("CATACLYSM_RATE_TRIALS", None)
        else:
            os.environ["CATACLYSM_RATE_TRIALS"] = previous
    return out.getvalue(), namespace


# --------------------------------------------------------------------------
# Claim 1: the lower bound
# --------------------------------------------------------------------------

def test_the_lower_bound_is_where_the_shallowest_dungeon_leaves_the_floor(report):
    """Recomputed here from the definition rather than read off the script.

    The bound is the smallest multiplier at which no depth in the ordinary
    8-to-40 floor range walks in one day. A multiplier `m` puts an `f`-floor
    dungeon at `ceil(f * m)`, so the binding depth is the shallowest one and the
    bound is just above `1 / 8`.
    """
    _printed, namespace = report
    lower, _upper = namespace["exact_bounds"]()
    ordinary = namespace["ORDINARY"]

    assert lower == pytest.approx(1.0 / ordinary[0], abs=0.001), (
        f"the script puts the lower bound at x{lower:.4f}; the shallowest "
        f"ordinary dungeon is {ordinary[0]} floors, so it should sit just above "
        f"x{1.0 / ordinary[0]:.4f}")

    # THE CONTROL. Just under the bound the collapse is back, and just over it
    # the range is clear. A bound that is not a boundary is not a bound.
    assert namespace["gradient"](lower)[1] == 0.0
    assert namespace["gradient"](lower - 0.001)[1] > 0.0, (
        "one step below the reported lower bound nothing lands on the one-day "
        "minimum, so the reported bound is not where the behaviour changes")


def test_below_the_lower_bound_the_gradient_really_does_collapse(report):
    """The property the bound protects, checked over the whole range below it
    rather than at the one point the bound sits on."""
    _printed, namespace = report
    lower, _upper = namespace["exact_bounds"]()
    previous = 101.0
    for thousandths in range(20, int(lower * 1000) + 1):
        share = namespace["gradient"](thousandths / 1000.0)[1]
        assert share <= previous + 1e-9, (
            f"a multiplier of {thousandths / 1000.0:.3f} puts MORE of the range "
            "on the one-day minimum than a smaller one did; a slower player "
            "cannot have more dungeons pinned to the minimum than a faster one")
        previous = share
    assert previous > 0.0, (
        "nothing below the lower bound lands on the one-day minimum, so the "
        "bound is protecting nothing")
    assert namespace["gradient"](0.02)[1] == 100.0, (
        "at x0.02 every dungeon in the ordinary range should be on the one-day "
        "minimum, which is the collapse the whole change exists to remove")


# --------------------------------------------------------------------------
# Claim 2: the upper bound
# --------------------------------------------------------------------------

def test_the_upper_bound_is_the_owners_worked_example_of_the_whole_stack(report):
    """`docs/DECISIONS.md`, 2026-09-06: "a 50 floor dungeon that only takes you
    a couple days" describes the whole stack, and the ruling's own arithmetic
    landed on three days and accepted it."""
    _printed, namespace = report
    _lower, upper = namespace["exact_bounds"]()
    walk_days = namespace["walk_days"]
    proposed = namespace["proposed_tree"]
    _label, flat, halve = namespace["stack_levels"]()[1]

    assert walk_days(proposed(upper), namespace["STACK_FLOORS"], flat, halve) == 3
    assert walk_days(proposed(upper + 0.001), namespace["STACK_FLOORS"],
                     flat, halve) == 4, (
        "one step above the reported upper bound the whole stack still reaches "
        "three days, so the reported bound is not where the behaviour changes")


def test_the_stack_the_bound_uses_is_the_one_the_ruling_named(report):
    """The bound is only as good as what "the whole stack" is taken to mean, so
    the flat days in it are checked against their sources.

    `Opportunist` 5 and `The Delver` 5 come from the design document;
    the Explorer city upgrade comes from `game/Data/CityUpgrades.csv`. The
    middle level uses that upgrade's FIRST value, which is what the owner's
    worked example used.
    """
    _printed, namespace = report
    first, full = namespace["city_upgrade_days"]()
    assert (first, full) == (4.0, 12.0)

    levels = namespace["stack_levels"]()
    assert levels[0][1] == 0.0, "the first level is the tree on its own"
    assert levels[1][1] == 5.0 + 5.0 + first
    assert levels[2][1] == 5.0 + 5.0 + full
    assert levels[1][2] is True, "Tactical Entry halves anything over 50 floors"


# --------------------------------------------------------------------------
# Claim 3: the chosen values
# --------------------------------------------------------------------------

def test_the_chosen_values_are_inside_the_window(report):
    _printed, namespace = report
    lower, upper = namespace["exact_bounds"]()
    chosen = namespace["total_multiplier"]()
    assert lower <= chosen <= upper


def test_the_chosen_values_are_what_the_preset_models(report):
    """Three records carry the same number and this is the one that reads all
    three: the per-point values here, the node descriptions in the design
    document, and `TREE_EXPLORER_AS_DESIGNED.run_days_mult`.

    The graph half is checked in
    `tools/tests/test_explorer_walk_time_shape_ruling.py`; this checks the
    script against the preset.
    """
    _printed, namespace = report
    assert namespace["total_multiplier"]() == pytest.approx(
        TREE_EXPLORER_AS_DESIGNED.run_days_mult, abs=1e-12)


def test_the_keystone_is_worth_what_it_was_worth_before(report):
    """**THE PRINCIPLE THE KEYSTONE'S VALUE COMES FROM, checked rather than
    stated.** `Fleet Footed` removes 5 days today where a basic point removes 1,
    so it is worth exactly 5 basic points. The 2026-09-06 ruling changed the
    shape and not the weights, so -12% is whatever preserves that ratio under a
    multiplicative design.
    """
    _printed, namespace = report
    per_point = namespace["PER_POINT"]
    keystone = namespace["KEYSTONE_PERCENT"]
    worth = math.log(1.0 - keystone) / math.log(1.0 - per_point)
    assert worth == pytest.approx(namespace["KEYSTONE_WORTH_BASIC_POINTS"],
                                  abs=0.1), (
        f"the keystone is worth {worth:.2f} basic points at -{keystone:.0%} "
        f"against -{per_point:.1%} per point, and it was worth exactly "
        f"{namespace['KEYSTONE_WORTH_BASIC_POINTS']} under the fixed-day "
        "design. Re-sizing it goes past what was ruled.")


def test_only_two_half_percent_values_reach_the_window(report):
    """**THE REASON THE CHOICE IS BETWEEN TWO NUMBERS AND NOT A CONTINUUM.**

    Tying the keystone to the basic nodes makes the total `(1-p)^60`, so half a
    percent on p moves it by about a third. If a future change widens the
    window, this test says so rather than leaving the recommendation looking
    more forced than it is.
    """
    _printed, namespace = report
    inside = namespace["REPORT"]["values"]["inside"]
    assert [round(row[0], 4) for row in inside] == [0.025, 0.03]
    assert namespace["PER_POINT"] == inside[0][0], (
        "the recommendation is the weakest of the values in the window, "
        "because every axis outside the window favours the weakest reduction "
        "that satisfies both bounds")


# --------------------------------------------------------------------------
# Claim 4: the script agrees with the engine
# --------------------------------------------------------------------------

def test_the_scripts_walk_days_match_the_shipped_engine(report):
    """Section 1 recomputes walk days instead of running campaigns, so it can
    drift from `Simulation.run_days_for` and print a table describing nothing.

    Checked over the whole ordinary range and past it, at three multipliers,
    with no extra flat days -- because `run_days_for` knows nothing about city
    upgrades or capstone options, which is exactly why the script has its own
    `extra_flat` argument.
    """
    _printed, namespace = report
    base = namespace["BASE"]
    for mult in (0.15, namespace["total_multiplier"](), 0.30):
        tree = namespace["proposed_tree"](mult)
        sim = Simulation(base.with_tree(tree), seed=0)
        for floors in (1, 8, 20, 40, 51, 80, 125, 400):
            assert namespace["walk_days"](tree, floors) == sim.run_days_for(floors), (
                f"at x{mult:.4f} the script walks {floors} floors in "
                f"{namespace['walk_days'](tree, floors)} days and the engine in "
                f"{sim.run_days_for(floors)}")


def test_the_flat_days_are_subtracted_before_the_multiplier(report):
    """**THE ORDER IS PART OF THE DESIGN AND NOT AN IMPLEMENTATION DETAIL.**

    `Simulation.run_days_for` computes `(floors - flat) * mult`, which is the
    order Diablo 3's cooldown formula uses: a flat subtraction first, then a
    multiplicative term, then a floor. The other order would make every flat day
    in the stack worth `1 / mult` times more, and the whole-stack arithmetic the
    upper bound rests on would be wrong by a factor of about five.
    """
    _printed, namespace = report
    tree = namespace["proposed_tree"](0.20)
    walk_days = namespace["walk_days"]

    # 100 floors, tree removes 10 flat, extra_flat 10 more.
    assert walk_days(tree, 100, 10.0) == math.ceil((100 - 20) * 0.20)
    assert walk_days(tree, 100, 10.0) != math.ceil(100 * 0.20 - 20)


# --------------------------------------------------------------------------
# Issue #1390: the Speed Runner cap
# --------------------------------------------------------------------------

def test_speed_runner_is_unbounded_without_a_cap(report):
    """The finding issue #1390 opened, recomputed rather than quoted.

    It pays for "days under default run time", and the branch's own depth nodes
    raise the default, so the payout rises without limit as dungeons get deeper.
    """
    _printed, namespace = report
    tree = namespace["proposed_tree"](namespace["total_multiplier"]())
    payouts = [namespace["speed_runner"](f, tree) for f in (8, 20, 40, 125, 400)]
    assert payouts == sorted(payouts), "the payout should rise with depth"
    assert payouts[-1] > 30.0, (
        f"a 400-floor dungeon pays {payouts[-1]:.0%} and issue #1390 is about "
        "the payout having no ceiling at all")


def test_the_cap_actually_bounds_it(report):
    """**THE CONTROL FOR THE TEST ABOVE.** A cap that is never reached is not a
    cap, and one applied to the wrong quantity would still let depth through."""
    _printed, namespace = report
    tree = namespace["proposed_tree"](namespace["total_multiplier"]())
    cap = namespace["SPEED_RUNNER_CAP"]
    capped = [namespace["speed_runner"](f, tree, cap=cap)
              for f in (8, 20, 40, 125, 400)]
    assert set(capped) == {cap}, (
        f"with a cap of {cap:.0%} the payouts are {capped}; every depth "
        "measured here removes far more than enough days to reach it")


def test_the_cap_is_sized_against_the_trees_own_loot_quantity_nodes(report):
    """The cap is a judgement, and the yardsticks it was sized against are read
    out of the design document so the judgement can be re-checked.

    `Bounty` is the largest unconditional Loot Quantity node in the Explorer
    branch and `The Hoarder` is the largest single source anywhere in the tree.
    """
    _printed, namespace = report
    nodes = namespace["NODES"]
    for name, points, _per_point in namespace["QUANTITY_YARDSTICKS"]:
        assert name in nodes
        assert nodes[name].get("maxPoints", 1) == points
    biggest = max(points * per_point
                  for _n, points, per_point in namespace["QUANTITY_YARDSTICKS"])
    assert namespace["SPEED_RUNNER_CAP"] == biggest, (
        f"Speed Runner's cap is {namespace['SPEED_RUNNER_CAP']:.0%} and the "
        f"largest Loot Quantity effect it was sized against is {biggest:.0%}. "
        "If they have come apart, one of the two moved and the reason is in "
        "docs/DECISIONS.md.")


def test_one_day_specialist_stops_firing_on_an_ordinary_dungeon(report):
    """What the shape change does to the third loot node, which is the reason
    issue #1390 belongs in the same change as #1383.

    Under the fixed-day design the keystone doubled Explorer loot on every
    ordinary dungeon, because every ordinary dungeon was already at the one-day
    minimum. Under a percentage a maxed branch never reaches it, because the
    branch's own depth nodes make every dungeon deeper than the depth at which
    one day is still possible.
    """
    _printed, namespace = report
    tree = namespace["proposed_tree"](namespace["total_multiplier"]())
    walk_days, base = namespace["walk_days"], namespace["BASE"]
    active = namespace["ACTIVE_TYPES"]

    shallowest = namespace["ORDINARY"][0]
    with_tree = max(1, int(round(shallowest + tree.floors_added(active))))
    for _label, flat, _halve in namespace["stack_levels"]():
        assert walk_days(tree, with_tree, flat) > base.run_days_min, (
            f"the shallowest ordinary dungeon is {with_tree} floors once the "
            f"branch's depth nodes are counted, and it still reaches the "
            f"one-day minimum with {flat:g} flat days of stack on top")

    # THE CONTROL, and without it the test above proves only that the number is
    # large. Under the flat design it DID reach one day, on the same dungeon.
    flat_design = namespace["EmpireTree"](name="flat 70", run_days_flat=70.0)
    assert walk_days(flat_design, with_tree) == base.run_days_min
