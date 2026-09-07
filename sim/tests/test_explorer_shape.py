"""The claims `sim/analyse_explorer_shape.py` makes about the Explorer branch.

WHY THIS EXISTS. Issue #1383 asked what shape the Explorer branch's walk-time
reduction should have, and the answer rests on four claims that are structural
rather than statistical. A campaign batch cannot check any of them, and all four
are the kind of claim that goes quietly wrong when the design document moves:

1. The Explorer branch's own unconditional flat reduction is **60 days**, not the
   70 that `TREE_EXPLORER_AS_DESIGNED.run_days_flat` models. The extra 10 is
   Opportunist, which is conditional, plus The Delver, which is a central
   capstone option and not an Explorer node at all. Issue #1288 found exactly
   this class of error in the Architect branch, where one factor matched no node.
2. A flat subtraction cannot compress the gradient. It stretches it while the
   amount is below the shallowest dungeon and destroys it above, with nothing in
   between. **That is the whole argument against every flat candidate**, so it is
   checked as an exhaustive property rather than at the two or three amounts the
   report happens to print.
3. A rate multiplier preserves the gradient, which is why it is the recommended
   shape.
4. The script's shape table agrees with the shipped `Simulation.run_days_for`.
   Section 2 of the report recomputes walk days rather than running campaigns, so
   it could drift from the engine and print a table that describes nothing.

`sim/tests/test_analysis_scripts.py` checks that the script runs and prints
something; the conclusions live here, which is the arrangement
`sim/tests/test_lethality_modes.py` already uses for
`sim/analyse_lethality_modes.py`.

WHAT IS NOT CHECKED HERE. No campaign figure. Every number the report's sections
3 to 5 print is a measurement, and this file runs the script at its smoke-test
size of one campaign a block, where those figures are noise and pinning them
would be checking the noise. The structural claims above do not depend on the
sample size at all.
"""

from __future__ import annotations

import contextlib
import io
import json
import pathlib
import runpy

import pytest

from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED, TuningConfig
from cataclysm_sim.engine import Simulation

SIM_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = SIM_ROOT / "analyse_explorer_shape.py"


@pytest.fixture(scope="module")
def report(monkeypatch_session=None):
    """Run the script once at its smoke-test size and hand back its globals.

    The trial count is pinned here rather than left to the environment so that a
    shell with `CATACLYSM_EXPLORER_TRIALS` set -- which is how the figures for
    issue #1383 were taken -- cannot turn this file into a fifty minute test.
    """
    import os
    previous = os.environ.get("CATACLYSM_EXPLORER_TRIALS")
    os.environ["CATACLYSM_EXPLORER_TRIALS"] = "1"
    try:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            namespace = runpy.run_path(str(SCRIPT))
    finally:
        if previous is None:
            os.environ.pop("CATACLYSM_EXPLORER_TRIALS", None)
        else:
            os.environ["CATACLYSM_EXPLORER_TRIALS"] = previous
    return out.getvalue(), namespace


# --------------------------------------------------------------------------
# Claim 1: what the branch actually gives
# --------------------------------------------------------------------------

def test_the_explorer_branchs_unconditional_flat_total_is_sixty(report):
    """Read off the design document, not off the config.

    Four nodes, all in the Explorer branch, all unconditional: Temporal Mastery
    25, Overclock 20, Pacing 10, Fleet Footed 5.
    """
    _printed, namespace = report
    total = sum(days for _n, branch, _p, days, note in namespace["WALK_TIME_NODES"]
                if days is not None and not note and branch == "Explorer")
    assert total == 60.0
    assert namespace["RESULT"]["facts"]["unconditional"] == 60.0


def test_the_model_now_removes_exactly_what_the_branch_does(report):
    """The gap is the finding, so it is asserted as a gap and not as two totals.

    If somebody corrects `TREE_EXPLORER_AS_DESIGNED` this test fails, which is
    the point: the report's whole first section would then be stale.
    """
    _printed, namespace = report
    facts = namespace["RESULT"]["facts"]
    assert facts["modelled"] == TREE_EXPLORER_AS_DESIGNED.run_days_flat
    assert facts["modelled"] - facts["unconditional"] == 0.0, (
        "the model and the Explorer branch disagree about how many flat days "
        "the branch removes. They differed by 10 until issue #1386 -- the "
        "model counted Opportunist, which is conditional, and The Delver, "
        "which is a capstone option in no branch. If the branch's own total "
        "moved, follow it in TREE_EXPLORER_AS_DESIGNED rather than here.")


def test_the_two_extra_terms_are_conditional_or_outside_the_branch(report):
    """Opportunist carries a condition in its own text; The Delver is a capstone
    option, which is one of three mutually exclusive choices and not a node in
    any branch."""
    _printed, namespace = report
    nodes = namespace["load_tree"]()
    _node, opportunist = namespace["named"](nodes, "Opportunist")
    assert "no other active dungeons" in opportunist["description"]

    delver_node, delver = namespace["named"](nodes, "The Delver")
    assert namespace["branch_of"](delver_node) == "CENTRAL"
    assert delver_node["data"]["isDecision"] is True
    assert delver["name"] != delver_node["data"]["name"], (
        "The Delver should be an option inside a capstone, not the capstone")
    siblings = [o["name"] for o in delver_node["data"]["options"]]
    assert len(siblings) == 3 and "The Delver" in siblings


def test_the_added_and_the_net_floor_totals_are_ten_apart(report):
    """**The two floor totals this file and the model deliberately differ on.**

    The rows here labelled `+50f` sum the four nodes that ADD floors and leave
    `Exclusionary Mapping` untaken. `TREE_EXPLORER_AS_DESIGNED` carries the NET
    total of all five, because a maxed branch has every node in it. Ten floors
    apart, on purpose, and this asserts the gap rather than either figure alone
    so that a change to one without the other fails.

    Until issue #1386 the model carried 0 here, crediting the branch with none
    of its depth nodes at all.
    """
    _printed, namespace = report
    added = sum(floors for _n, _p, floors, _note in namespace["FLOOR_NODES"]
                if floors > 0)
    net = sum(floors for _n, _p, floors, _note in namespace["FLOOR_NODES"])

    assert added == 50.0
    assert net == 40.0
    assert added - net == 10.0, (
        "the added and net floor totals no longer differ by Exclusionary "
        "Mapping's 10, so either that node changed or another floor-removing "
        "node was added. Both figures below depend on which is which.")
    assert TREE_EXPLORER_AS_DESIGNED.floor_delta == net, (
        f"TREE_EXPLORER_AS_DESIGNED.floor_delta is "
        f"{TREE_EXPLORER_AS_DESIGNED.floor_delta:+g} and the branch's net is "
        f"{net:+g}. tools/tests/test_the_explorer_preset_matches_the_tree.py "
        "derives that from the graph; follow it there first.")


def test_a_renamed_node_breaks_the_script_rather_than_the_total(report):
    """THE GUARD PROOF FOR SECTION 1. `named` raising is what stops this script
    doing what issue #1288 found the Architect branch's comment had done: keep
    stating a total after the node behind it stopped existing."""
    _printed, namespace = report
    nodes = namespace["load_tree"]()
    with pytest.raises(AssertionError, match="Overclock"):
        namespace["named"](nodes, "Overclock ")


def test_a_node_whose_points_moved_stops_the_script(report):
    """THE GUARD PROOF FOR THE POINT COUNTS, done on a copy of the loaded nodes
    rather than by editing the design document.

    Editing `docs/Empire_Development_Tree_Final.json` on disk does make this
    check fire -- measured with `tools/prove_guard.py`, moving Overclock from 20
    points to 21 -- but it fires while the script is being imported, so every
    test in this file reports as an ERROR at fixture setup and `prove_guard`
    calls that run no measurement rather than a named failure. Mutating a copy
    produces the same condition with a test name attached to it.
    """
    _printed, namespace = report
    moved = json.loads(json.dumps(namespace["load_tree"]()))
    target = next(n for n in moved if n["data"].get("name") == "Overclock")
    target["data"]["maxPoints"] = 21
    with pytest.raises(AssertionError, match="Overclock has 21 points"):
        namespace["assert_document_matches"](moved)


def test_the_document_sweep_notices_a_time_node_nobody_classified(report):
    """THE GUARD PROOF FOR THE SWEEP, and the one that matters.

    Checking that every node you wrote down is present cannot catch a node you
    never wrote down. This adds one, and the sweep must refuse to pass.
    """
    _printed, namespace = report
    nodes = namespace["load_tree"]()
    namespace["assert_document_matches"](nodes)   # the control: it passes as-is

    invented = json.loads(json.dumps(nodes[0]))
    invented["data"] = {"kind": "basic", "name": "Chronal Shortcut",
                        "description": "-2 days from dungeon run time per point.",
                        "maxPoints": 10}
    invented["position"] = {"x": 200.0, "y": 200.0}
    with pytest.raises(AssertionError, match="Chronal Shortcut"):
        namespace["assert_document_matches"](nodes + [invented])


# --------------------------------------------------------------------------
# Claims 2 and 3: what each shape does to the gradient
# --------------------------------------------------------------------------

def on_floor(namespace, shape, depths=None) -> float:
    """The script's own helper, so a test cannot check a private reimplementation
    of the figure the report prints."""
    return namespace["on_the_floor"](shape, depths)


def test_the_untreed_gradient_is_thirty_three_distinct_walks(report):
    """The control every other row is read against."""
    _printed, namespace = report
    ratio, answers = namespace["gradient"](namespace["Shape"]("none"))
    assert (ratio, answers) == (5.0, 33)
    assert on_floor(namespace, namespace["Shape"]("none")) == 0.0


def test_a_surge_reaches_deeper_than_the_basic_dungeon_range(report):
    """The correction to issue #1383's table, derived from the config.

    A surge rolls a Quest 12% of the time and a Quest at a Sanctuary is 30-50
    floors, and every surge dungeon is scaled by the active Cataclysm's
    `floors_mult`, which runs 0.55 to 1.60. So the reachable range is 4-80
    floors, not 8-40, and a flat 70 leaves a gradient on the deepest of them.
    """
    from cataclysm_sim.patterns import PATTERNS
    _printed, namespace = report
    wide = namespace["surge_reachable"]()
    mults = [p.floors_mult for p in PATTERNS.values()]
    assert (min(mults), max(mults)) == (0.55, 1.60)
    assert (wide[0], wide[-1]) == (4, 80)

    shipped = namespace["Shape"]("probe", flat=TREE_EXPLORER_AS_DESIGNED.run_days_flat)
    assert on_floor(namespace, shipped) == 100.0
    assert on_floor(namespace, shipped, wide) < 100.0, (
        "the shipped flat 70 does put every BASIC surge dungeon on the one-day "
        "floor, but not every dungeon a surge can reach; issue #1383's table "
        "leaves out Quest dungeons and the Cataclysm depth multiplier")
    _ratio, answers = namespace["gradient"](shipped, wide)
    assert answers > 1


def test_a_flat_subtraction_flattens_everything_shallower_than_it_removes(report):
    """EXHAUSTIVE OVER EVERY WHOLE-DAY FLAT AMOUNT UP TO THE SHIPPED 70.

    A first version of this test asserted something stronger and false -- that a
    flat subtraction never produces an intermediate gradient -- and it failed at
    a flat 36, which walks 40 floors in 4 days and leaves 4 distinct walk
    lengths. What is actually true, and what the report now says, is that a flat
    subtraction of f days puts every dungeon of f+1 floors or fewer on the same
    one-day minimum, so the share of the range it erases rises with the amount
    it removes and never falls.
    """
    _printed, namespace = report
    shape_of = namespace["Shape"]
    previous = -1.0
    for amount in range(0, 71):
        shape = shape_of(f"flat {amount}", flat=float(amount))
        share = on_floor(namespace, shape)
        expected = 100.0 * sum(1 for f in namespace["ORDINARY"]
                               if f - amount <= 1) / 33
        assert share == pytest.approx(expected), f"flat {amount}"
        assert share >= previous, (
            f"a flat {amount} erases less of the range than a flat "
            f"{amount - 1} did; the report says the share never falls")
        previous = share
    assert on_floor(namespace, shape_of("flat 39", flat=39.0)) == 100.0


def test_at_the_same_speed_a_flat_erases_more_of_the_range_than_a_rate(report):
    """THE COMPARISON THE RECOMMENDATION RESTS ON, checked at every whole-day
    target from 20 days down to 1.

    Each pair walks a 40-floor dungeon in the same number of days: a flat of
    40-d against a rate of d/40. The flat must put at least as much of the
    8-to-40 range on the one-day floor, and strictly more at every target of 2
    days or more. At a target of 1 day the two are equal, because a whole-day
    walk has nowhere left to put a gradient -- which is the limit the report
    states on how fast any shape can make an invested player.
    """
    _printed, namespace = report
    shape_of = namespace["Shape"]
    for target in range(1, 21):
        by_flat = on_floor(namespace, shape_of("f", flat=40.0 - target))
        by_rate = on_floor(namespace, shape_of("r", mult=target / 40.0))
        if target == 1:
            assert by_flat == by_rate == 100.0
        else:
            assert by_flat > by_rate, (
                f"at a 40-floor walk of {target} days the flat "
                f"{40 - target} erases {by_flat:.0f}% of the range and the "
                f"rate {target / 40.0:.3f} erases {by_rate:.0f}%")


def test_raising_the_floor_leaves_one_walk_length_at_any_height(report):
    """A higher floor moves the collapse; it does not remove it. Checked at
    every floor height from 1 to 40, because "raise the floor" is the candidate
    a reader is most likely to reach for after seeing the one-day figure."""
    _printed, namespace = report
    shape_of, gradient = namespace["Shape"], namespace["gradient"]
    flat = TREE_EXPLORER_AS_DESIGNED.run_days_flat
    for minimum in range(1, 41):
        _ratio, answers = gradient(
            shape_of("probe", flat=flat, minimum=minimum))
        assert answers == 1, (
            f"a floor of {minimum} days under a flat {flat:g} left {answers} "
            f"walk lengths; the report says it always leaves exactly one")


def test_a_rate_keeps_floor_count_worth_something(report):
    """The recommended shape, checked as a property over the plausible range.

    Every multiplier from 0.05 to 0.50 must leave the 8-to-40-floor range with
    more than one walk length, and must leave the deepest ordinary dungeon
    costing strictly more than the shallowest.
    """
    _printed, namespace = report
    shape_of, gradient = namespace["Shape"], namespace["gradient"]
    for step in range(1, 11):
        mult = step * 0.05
        ratio, answers = gradient(shape_of(f"x{mult:.2f}", mult=mult))
        assert answers > 1 and ratio > 1.0, (
            f"a multiplier of {mult:.2f} left {answers} walk lengths at a "
            f"ratio of {ratio}")


def test_a_rate_is_faster_than_no_tree_at_every_ordinary_depth(report):
    """Investment has to be worth something. A multiplier that made a dungeon
    slower would be a worse deal than not investing at all."""
    _printed, namespace = report
    shape_of, base = namespace["Shape"], namespace["BASE"]
    untreed, rate = shape_of("none"), shape_of("x0.15", mult=0.15)
    for floors in namespace["ORDINARY"]:
        assert rate.days(floors, base) < untreed.days(floors, base)


# --------------------------------------------------------------------------
# Claim 4: the table describes the engine
# --------------------------------------------------------------------------

def test_the_shape_table_matches_the_engine(report):
    """Section 2 recomputes walk days instead of running campaigns, so it can
    drift from `Simulation.run_days_for` and print a table about nothing.

    Every shape at every quoted depth, against a real `Simulation` built from
    that shape's own config.
    """
    _printed, namespace = report
    base = namespace["BASE"]
    for shape in namespace["SHAPES"]:
        sim = Simulation(shape.config(base), seed=0)
        for floors in namespace["QUOTED_DEPTHS"]:
            adjusted = max(1, int(round(floors + shape.floor_delta)))
            assert shape.days(floors, base) == sim.run_days_for(adjusted), (
                f"{shape.label} at {floors} floors")


def test_every_candidate_is_expressible_in_fields_the_engine_already_has(report):
    """The report's opening claim, and the reason no engine method is
    overridden: each candidate is a `TuningConfig` and an `EmpireTree`, so none
    of them needs a new mechanism to be tried."""
    _printed, namespace = report
    base = namespace["BASE"]
    for shape in namespace["SHAPES"]:
        cfg = shape.config(base)
        assert isinstance(cfg, TuningConfig)
        assert cfg.tree.run_days_flat == shape.flat
        assert cfg.tree.run_days_mult == shape.mult
        assert cfg.run_days_min == shape.minimum


def test_raising_the_run_day_floor_is_the_one_candidate_that_taxes_no_tree(report):
    """`run_days_min` lives on `TuningConfig`, not on `EmpireTree`, so raising
    it slows the untreed player down as well. Every other candidate leaves them
    exactly where they are. Issue #1383 item 4."""
    _printed, namespace = report
    base = namespace["BASE"]
    untreed_today = Simulation(base.with_tree(base.tree), seed=0)
    assert untreed_today.run_days_for(8) == 8

    raised = namespace["Shape"]("probe", minimum=10)
    assert Simulation(raised.config(base), seed=0).run_days_for(8) == 10

    for shape in namespace["SHAPES"]:
        if shape.minimum != base.run_days_min:
            continue
        assert shape.config(base).run_days_min == base.run_days_min


def test_a_cow_level_keeps_its_doubled_walk_under_every_candidate(report):
    """No candidate overrides an engine method, so every one of them inherits
    the rule in `_walk_days` that a Cow Level's time "is doubled and cannot be
    reduced". Checked rather than assumed, because a candidate that reached the
    walk time by overriding `run_days_for` on a subclass would silently apply
    itself to the one sub-type the design exempts."""
    _printed, namespace = report
    base = namespace["BASE"]
    for shape in namespace["SHAPES"]:
        sim = Simulation(shape.config(base), seed=0)
        adjusted = max(1, int(round(30 + shape.floor_delta)))
        assert sim._walk_days(adjusted, "Cow Level") == adjusted * 2
        assert sim._walk_days(adjusted, "None") == shape.days(30, base)
