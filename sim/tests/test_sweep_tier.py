"""The tuning sweep must say which difficulty tier its results are from.

WHY THIS FILE EXISTS. Issue #281. `sim/experiments.py` ran every one of its nine
sections at difficulty tier 1 and no other, because `TuningConfig.tier` defaults
to 1 and no section set it. The game has eight tiers. Nothing in the report said
which one it covered, so every tuning conclusion this project holds -- including
issues #4 and #5, the two open findings -- was a tier 1 result that read as a
general one.

That was found the hard way. The sweep was re-run on 2026-08-05 to get a baseline
after the player power anchors were reset, and every number came back identical
to the digit. The reason was that the anchor change moved tiers 2 to 7 and left
tier 1 alone, so a sweep that never leaves tier 1 could not have moved.

WHAT IS CHECKED HERE. That the tier is named in the output, that no section can
quietly go back to relying on the default, and that the preset comparison covers
more than one tier and reports ties honestly. None of it runs the sweep: that is
about eighteen minutes. The preset section is exercised at a small sample size,
which checks its shape and not its numbers.
"""

from __future__ import annotations

import ast
import contextlib
import io
import math
import pathlib
import random
import sys
from dataclasses import replace

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import experiments  # noqa: E402
from cataclysm_sim import scoring  # noqa: E402
from cataclysm_sim.config import TuningConfig  # noqa: E402

SOURCE = pathlib.Path(experiments.__file__).read_text(encoding="utf-8")

#: Parsed ONCE. The check below matches nodes by identity, and two calls to
#: ast.parse over the same text produce two sets of objects that share no
#: identities, so re-parsing makes every node look unwrapped.
TREE = ast.parse(SOURCE)


def function_node(name: str) -> ast.FunctionDef:
    """One top-level function of experiments.py, by name.

    Source checks used to slice from `def <name>` to the END of the file, so
    anything defined below could satisfy them. Issue #1297 moved section 7's
    per-tier body into `preset_tables` and added `compare_surge_sizes` after it,
    which made that looseness visible.
    """
    for node in ast.walk(TREE):
        if isinstance(node, ast.FunctionDef) and node.name == name:
            return node
    raise AssertionError(
        f"experiments.py has no function called {name!r} any more. If it was "
        "renamed, follow it here rather than deleting the check.")


def function_source(name: str) -> str:
    """The text of one function, and nothing after it."""
    node = function_node(name)
    return "\n".join(SOURCE.splitlines()[node.lineno - 1:node.end_lineno])


def unwrapped(text: str) -> str:
    """Collapse whitespace, so a match survives the line the sentence wraps on."""
    return " ".join(text.split())


class TestTheTierIsStated:
    def test_the_sweep_tier_is_a_real_tier(self):
        assert experiments.SWEEP_TIER in range(1, 9)
        assert scoring.tier_bounds(experiments.SWEEP_TIER)[1] > 0

    def test_the_header_names_the_tier(self):
        """The bug was not a wrong number, it was an unstated one."""
        header = unwrapped("\n".join(experiments.header_lines()))
        assert f"DIFFICULTY TIER {experiments.SWEEP_TIER} of 8" in header

    def test_the_header_names_the_power_ceiling_it_implies(self):
        """Checked on the tier line, not anywhere in the header.

        A looser check passes for the wrong reason: the power key lines lower
        down compute their own ceiling and print it too, so searching the whole
        header finds the right number even when the tier line holds a wrong one.
        Found by breaking the ceiling to a hard-coded 385 and watching nothing
        fail.
        """
        ceiling = scoring.tier_bounds(experiments.SWEEP_TIER)[1]
        assert self._tier_line() == (
            f"DIFFICULTY TIER {experiments.SWEEP_TIER} of 8, player power "
            f"ceiling {ceiling:,.0f}. Every section below runs at")

    def _tier_line(self) -> str:
        lines = [line for line in experiments.header_lines()
                 if line.startswith("DIFFICULTY TIER")]
        assert len(lines) == 1, (
            f"expected exactly one DIFFICULTY TIER line, found {len(lines)}")
        return lines[0]

    def test_the_header_says_section_seven_is_the_exception(self):
        header = unwrapped("\n".join(experiments.header_lines()))
        assert ("except section 7, which also runs at tier "
                f"{experiments.PRESET_TIERS[-1]}") in header

    def test_the_header_ceiling_moves_with_the_tier(self, monkeypatch):
        """A hard-coded ceiling would pass every test above. This one fails.

        Issue #8 is the same mistake made once already, with a power range that
        was worked out by hand and never re-derived.
        """
        monkeypatch.setattr(experiments, "SWEEP_TIER", 8)
        line = self._tier_line()
        assert "DIFFICULTY TIER 8 of 8" in line
        assert f"{scoring.tier_bounds(8)[1]:,.0f}" in line
        assert f"{scoring.tier_bounds(1)[1]:,.0f}" not in line


class TestNoSectionRelivesTheBug:
    """Every TuningConfig built in the sweep must set the tier explicitly.

    Checked against the SOURCE, because the failure being guarded against is a
    section that silently takes the default. Running the sweep would find it, and
    the sweep is eighteen minutes, so nobody would.
    """

    def _config_calls(self) -> list[ast.Call]:
        return [node for node in ast.walk(TREE)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "TuningConfig"]

    def test_the_sweep_builds_a_config_at_all(self):
        """Guards the two tests below from passing on an empty list."""
        assert self._config_calls(), (
            "sim/experiments.py no longer constructs TuningConfig anywhere, so "
            "the checks below cannot see anything. Rewrite them against however "
            "it builds a configuration now.")

    def test_every_config_is_wrapped_in_a_replace_that_sets_the_tier(self):
        wrapped: set[int] = set()
        for node in ast.walk(TREE):
            if not (isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Name)
                    and node.func.id == "replace"):
                continue
            if not any(keyword.arg == "tier" for keyword in node.keywords):
                continue
            for inner in ast.walk(node):
                if isinstance(inner, ast.Call):
                    wrapped.add(id(inner))

        bare = [node for node in self._config_calls() if id(node) not in wrapped]
        assert not bare, (
            "sim/experiments.py builds TuningConfig without setting the tier, at "
            f"line(s) {', '.join(str(n.lineno) for n in bare)}. That is issue "
            "#281: the sweep then runs at whatever config.py defaults to and the "
            "report does not say which tier its numbers are from. Wrap it in "
            "replace(..., tier=SWEEP_TIER).")

    def test_the_explicit_tier_is_the_one_the_default_used_to_give(self):
        """Naming the tier changed no section's behaviour, and this proves it.

        Delete this test if SWEEP_TIER is ever deliberately moved off the
        default. It exists to show the change that introduced SWEEP_TIER was not
        also a silent re-tuning, not to stop the tier ever changing.
        """
        assert replace(TuningConfig(), tier=experiments.SWEEP_TIER) == TuningConfig()


#: Where the preset ordering starts and stops in `exp_presets` output.
#: FOUR blocks below it print one `tier N:` line per tier -- the tolerance
#: comparison, the same ordering under the cap alone, the win-rate second
#: opinion, and the objectives-cleared block -- so matching on that prefix alone
#: picks up all of them. Issues #294 and #328.
_ORDER_HEADING = "PRESET ORDER BY WIN RATE MINUS LOSS RATE"
_ORDER_ENDS_AT = "HOW MUCH THE PER-PAIR TOLERANCE BUYS"

#: The block that reprints the ordering under the worst-case cap, which is what
#: the report gave before issue #328. Kept sliceable so a test can compare the
#: two orderings the report itself prints.
_CAP_ORDER_HEADING = "THE SAME ORDER UNDER THE CAP ALONE"
_CAP_ORDER_ENDS_AT = "PRESET ORDER BY WIN RATE ALONE"

#: The same, for the win-rate ordering kept as a second opinion. Issue #294.
_WIN_ORDER_HEADING = "PRESET ORDER BY WIN RATE ALONE"
_WIN_ORDER_ENDS_AT = "QUEST OBJECTIVES CLEARED"


def _lines_between(printed: str, start: str, end: str) -> list[str]:
    block = printed[printed.index(start):printed.index(end)]
    return [line for line in block.splitlines()
            if line.strip().startswith("tier ") and ":" in line]


def _ordering_lines(printed: str) -> list[str]:
    """The `tier N: a > b = c` lines of the preset ranking, and only those."""
    return _lines_between(printed, _ORDER_HEADING, _ORDER_ENDS_AT)


def _win_ordering_lines(printed: str) -> list[str]:
    """The same for the win-rate ordering printed as a second opinion."""
    return _lines_between(printed, _WIN_ORDER_HEADING, _WIN_ORDER_ENDS_AT)


def _cap_ordering_lines(printed: str) -> list[str]:
    """The same for the ordering under the worst-case cap. Issue #328."""
    return _lines_between(printed, _CAP_ORDER_HEADING, _CAP_ORDER_ENDS_AT)


class TestTheOrderingIsReportedHonestly:
    def test_two_rates_a_sample_cannot_separate_are_shown_tied(self):
        tolerance = experiments.win_rate_noise(150)
        order = experiments.rank_by_score({"a": 39.0, "b": 38.0, "c": 20.0},
                                        tolerance)
        assert order == [("a", "b"), ("c",)]

    def test_the_best_group_comes_first(self):
        order = experiments.rank_by_score({"low": 1.0, "high": 90.0}, tolerance=0.0)
        assert order[0] == ("high",)

    def test_everything_tied_collapses_to_one_group(self):
        """What tier 8 does: the win rate goes to zero and stops discriminating."""
        order = experiments.rank_by_score({"a": 0.0, "b": 0.0, "c": 0.0},
                                        tolerance=1.0)
        assert order == [("a", "b", "c")]

    def test_the_noise_floor_falls_as_the_sample_grows(self):
        assert experiments.win_rate_noise(600) < experiments.win_rate_noise(150)

    def test_the_noise_floor_at_the_sample_size_actually_used(self):
        """150 campaigns per cell. Two rates closer than this mean nothing."""
        assert experiments.win_rate_noise(150) == pytest.approx(5.77, abs=0.05)

    def test_a_zero_tolerance_would_report_noise_as_an_ordering(self):
        """The reason the tolerance exists, stated as a test.

        One percentage point apart at 150 campaigns is inside the sampling
        error, so calling one better than the other is reporting the sort.
        """
        noisy = experiments.rank_by_score({"a": 39.0, "b": 38.0}, tolerance=0.0)
        assert noisy == [("a",), ("b",)]


class TestAPresetWithNoResultIsNotRanked:
    """Issue #294. A campaign that runs out of days is neither won nor lost, so
    it has NO outcome. A preset whose campaigns mostly did that has no win rate
    to rank, and 0% win with 0% loss is the absence of a measurement rather
    than a poor one.

    Ranked anyway it comes out well, because every proposed metric reads the
    empty cell as a good score. The measured tier 8 table is the case: the
    Architect preset is 0% win, 0% loss, 100% no result, and win minus loss
    puts it FIRST, ahead of the only preset in the table that wins anything.

    So `rank_by_score` takes the names to leave out, and `exp_presets` passes it
    what `warn_about_unresolved_campaigns` found.
    """

    def test_an_excluded_preset_appears_nowhere_in_the_order(self):
        order = experiments.rank_by_score(
            {"good": 40.0, "gone": 0.0, "poor": 2.0}, tolerance=5.8,
            exclude=["gone"])
        assert order == [("good",), ("poor",)]
        assert all("gone" not in group for group in order)

    def test_excluding_nothing_leaves_the_ranking_alone(self):
        """The default has to be the old behaviour, or every other caller of
        rank_by_score changes meaning."""
        wins = {"a": 39.0, "b": 38.0, "c": 20.0}
        assert (experiments.rank_by_score(wins, 5.8)
                == experiments.rank_by_score(wins, 5.8, exclude=[]))

    def test_the_measured_tier_8_table_no_longer_ranks_the_empty_cell(self):
        """The real numbers from the 2026-08-05 run, 150 campaigns per cell.

        Architect maxed is the 0/0/100 cell. Under win rate alone it lands in
        the floor tie; under win minus loss it would lead the table. Either way
        it is being scored on a campaign that never finished, and now it is not
        in the order at all.
        """
        tier_8 = {
            "No tree": 2.0,
            "Explorer maxed (as designed)": 1.0,
            "Explorer via floors (-25 floors)": 10.0,
            "Explorer via floors (+30 floors)": 0.0,
            "Architect maxed (as designed)": 0.0,
            "Proposed budget (x0.85 time, x0.55 dmg)": 1.0,
        }
        order = experiments.rank_by_score(
            tier_8, experiments.win_rate_noise(150),
            exclude=["Architect maxed (as designed)"])
        ranked = [name for group in order for name in group]
        assert "Architect maxed (as designed)" not in ranked
        assert set(ranked) == set(tier_8) - {"Architect maxed (as designed)"}

    def test_the_name_is_dropped_before_grouping_not_after(self):
        """This is the reason the exclusion is a parameter rather than a filter
        the caller applies to the finished groups.

        Each preset is compared with the FIRST member of the group being built,
        not with its immediate neighbour. So which presets are present decides
        what the rest are measured against. With a tolerance of 5, 10-6-2 is
        (10, 6) then (2), because 2 is 8 below the 10 that opened the group.
        Take the 10 away and 6 and 2 are 4 apart, which is one group.

        Deleting rows from the finished groups would have given (6), (2) --
        the grouping of a set that was never ranked.
        """
        wins = {"a": 10.0, "b": 6.0, "c": 2.0}
        assert experiments.rank_by_score(wins, 5.0) == [("a", "b"), ("c",)]
        assert experiments.rank_by_score(wins, 5.0, exclude=["a"]) == [("b", "c")]

    def test_excluding_everything_gives_an_empty_ranking(self):
        """Not a single empty group, and not a crash. `exp_presets` reads this
        to decide whether it has any ranking to report at all."""
        assert experiments.rank_by_score({"a": 1.0, "b": 2.0}, 5.0,
                                       exclude=["a", "b"]) == []


class TestTheMarginNoiseFloor:
    """Issue #294. Section 7 ranks on win rate minus loss rate, and that metric
    needs its own tolerance.

    THE MISTAKE THIS PREVENTS. `win_rate_noise` treats a rate as a binomial
    proportion and doubles its standard error for a difference of two
    independent rates. Win and loss are not independent: they come from the same
    campaign, which scores +1 won, -1 lost, 0 no result. So a margin is the
    sample mean of ONE score, and using the win rate's tolerance on it reports a
    figure half the size of the right one -- which would call gaps significant
    that are not.

    THE DERIVATION, restated so a reader of this file can check the tests
    against it rather than against `margin_noise`'s own docstring. With p the
    win probability and q the loss probability,

        E[S] = p - q, E[S^2] = p + q, Var(S) = (p + q) - (p - q)^2

    The worst case is Var(S) = 1 at p = q = 0.5, which makes the bound exactly
    twice the win rate's.
    """

    def test_the_bound_is_exactly_twice_the_win_rate_bound(self):
        """The headline of the derivation. The win rate's worst case is
        sqrt(0.25) and the margin's is sqrt(1), and the two differ by nothing
        else."""
        for trials in (10, 60, 150, 600):
            assert experiments.margin_noise(trials) == pytest.approx(
                2.0 * experiments.win_rate_noise(trials))

    def test_the_bound_at_the_sample_sizes_actually_used(self):
        """150 campaigns per cell in the full sweep, 60 in the runs section 7
        is measured at directly."""
        assert experiments.margin_noise(150) == pytest.approx(11.55, abs=0.05)
        assert experiments.margin_noise(60) == pytest.approx(18.26, abs=0.05)

    def test_the_bound_falls_as_the_sample_grows(self):
        assert experiments.margin_noise(600) < experiments.margin_noise(150)

    def test_one_campaign_varies_most_on_a_coin_flip(self):
        """Var(S) = 1 at p = q = 0.5, and that is the maximum over every
        (p, q) with p + q <= 1. Checked over a grid rather than asserted,
        because the whole bound rests on it."""
        assert experiments.margin_variance(50.0, 50.0) == pytest.approx(1.0)
        for win in range(0, 101, 5):
            for loss in range(0, 101 - win, 5):
                assert experiments.margin_variance(win, loss) <= 1.0 + 1e-9

    def test_a_cell_that_never_resolves_has_no_spread(self):
        """0% win and 0% loss is the absence of a result. Its margin is 0 with
        no variance at all, which is exactly why it must not be ranked --
        `rank_by_score`'s `exclude` is what keeps it out."""
        assert experiments.margin_variance(0.0, 0.0) == pytest.approx(0.0)

    def test_a_cell_that_always_loses_has_no_spread_either(self):
        """Every campaign scores -1, so the margin is -100 with no variance.
        This is the case the worst-case bound is most conservative about, and
        it is the common case at tier 8."""
        assert experiments.margin_variance(0.0, 100.0) == pytest.approx(0.0)
        assert experiments.margin_variance(100.0, 0.0) == pytest.approx(0.0)

    def test_the_tolerance_for_a_pair_never_exceeds_the_bound(self):
        """What makes the bound safe to use as one tolerance for a whole
        table."""
        for win in range(0, 101, 10):
            for loss in range(0, 101 - win, 10):
                observed = experiments.margin_noise_between(
                    win, loss, 50.0, 50.0, 150)
                assert observed <= experiments.margin_noise(150) + 1e-9

    def test_the_tolerance_for_two_losing_cells_is_far_below_the_bound(self):
        """The measured tier 8 case. Both cells lose nearly every campaign, so
        the pair is separable by a gap the bound would call noise."""
        observed = experiments.margin_noise_between(2.0, 98.0, 0.0, 97.0, 60)
        assert observed < experiments.margin_noise(60) / 2.0

    def test_the_win_rate_tolerance_would_be_half_the_right_one(self):
        """The mistake stated as a test, so that swapping the two back gets
        caught. Issue #294 named this as the thing to settle before the metric
        could be adopted."""
        assert experiments.win_rate_noise(150) < experiments.margin_noise(150)
        assert (experiments.win_rate_noise(150)
                == pytest.approx(experiments.margin_noise(150) / 2.0))


class TestTheTolerancePerPair:
    """Issue #328. The grouping no longer uses one worst-case figure for the
    whole table. It uses the tolerance each PAIR of cells needs, computed from
    the rates those two cells observed after smoothing.

    WHAT THE BOUND COST. It assumes a cell that wins half its campaigns and
    loses the other half, where one campaign's variance is 1. Above difficulty
    tier 5 almost every campaign loses, the variance is near zero, and the bound
    was about eight times wider than the closest measured pair needed.

    WHY IT COULD NOT SIMPLY BE SWAPPED. A cell that observed 0 wins in 60
    campaigns has an estimated variance of exactly zero, so its tolerance
    against another such cell is exactly zero and ANY gap gets called a
    difference. `smoothed_rates` pulls the rates off the boundary first, and
    `sim/analyse_margin_tolerance.py` is the measurement that chose by how much:
    two cells with IDENTICAL true rates are separated 51% of the time unsmoothed
    against a target near 32%.
    """

    def test_the_smoothing_constant_is_the_documented_one(self):
        """Both derivations give 0.5: Agresti-Coull's z^2/2 at z = 1, which is
        the one standard error this tolerance covers, and the Jeffreys prior
        for a multinomial. Pinned because the whole calibration rests on it."""
        assert experiments.MARGIN_SMOOTHING == 0.5

    def test_smoothing_moves_a_cell_that_won_nothing_off_the_boundary(self):
        """THE FAILURE THE SMOOTHING EXISTS FOR. 0% win and 100% loss is the
        common cell at tier 8."""
        win, loss = experiments.smoothed_rates(0.0, 100.0, 60)
        assert win > 0.0
        assert loss < 100.0

    def test_smoothing_never_moves_a_rate_past_the_middle(self):
        """It is a weighted average of the observed rate and one third, so it
        always moves toward the centre and never overshoots it. Checked because
        a smoothing that could cross would turn a losing cell into a winning
        one."""
        for win in range(0, 101, 10):
            for loss in range(0, 101 - win, 10):
                smooth_win, smooth_loss = experiments.smoothed_rates(
                    float(win), float(loss), 60)
                for before, after in ((win, smooth_win), (loss, smooth_loss)):
                    lo, hi = sorted((float(before), 100.0 / 3.0))
                    assert lo - 1e-9 <= after <= hi + 1e-9

    def test_no_smoothing_is_the_identity(self):
        """So that the constant is the only thing that changes behaviour, and a
        measurement can compare against the unsmoothed case."""
        assert experiments.smoothed_rates(0.0, 100.0, 60, 0.0) == (0.0, 100.0)
        assert experiments.smoothed_rates(12.0, 44.0, 60, 0.0) == pytest.approx(
            (12.0, 44.0))

    def test_two_cells_that_lost_everything_no_longer_get_a_zero_tolerance(
            self):
        """THE BUG THE BOUND EXISTED TO AVOID, and the reason issue #294 kept
        the bound rather than swapping in the observed figure. Without
        smoothing this is exactly 0.0, so a gap of any size at all would be
        reported as a real difference."""
        unsmoothed = experiments.margin_variance(0.0, 100.0)
        assert unsmoothed == 0.0, (
            "the unsmoothed variance of an all-losing cell is no longer zero, "
            "so the trap this smoothing was added for has gone away and the "
            "constant should be re-derived rather than kept.")
        assert experiments.margin_noise_between(
            0.0, 100.0, 0.0, 100.0, 60) > 0.0

    def test_the_smoothed_pair_tolerance_never_exceeds_the_bound(self):
        """What makes `margin_noise` safe as the cap. Smoothed rates are still
        rates, and `margin_variance` is at most 1 for any rates at all, so this
        holds by construction -- checked over a grid because it is the property
        `rank_by_score` relies on when it takes the smaller of the two."""
        for trials in (60, 150):
            for win in range(0, 101, 10):
                for loss in range(0, 101 - win, 10):
                    observed = experiments.margin_noise_between(
                        float(win), float(loss), 50.0, 50.0, trials)
                    assert observed <= experiments.margin_noise(trials) + 1e-9

    def test_the_high_tier_pair_tolerance_is_a_fraction_of_the_cap(self):
        """THE POINT OF THE CHANGE, as a number. These are the rates of the
        closest pair at difficulty tiers 6 and 7, measured 2026-08-06 at 60
        campaigns per cell: the Explorer maxed preset against Explorer via
        floors (+30 floors). Smoothed it gets 4.28 points against a cap of
        18.26, so the cap was 4.3 times wider than that pair needed."""
        observed = experiments.margin_noise_between(0.0, 100.0, 0.0, 96.7, 60)
        assert observed < experiments.margin_noise(60) / 3.0, (
            f"the per-pair tolerance for two losing cells is now "
            f"{observed:.2f} against a cap of {experiments.margin_noise(60):.2f}. "
            f"Issue #328 was opened because the cap was many times wider than "
            f"these cells need; if that ratio has collapsed, the change is "
            f"buying nothing and should be reconsidered.")

    def test_that_pair_is_still_reported_tied_and_that_is_correct(self):
        """SAID PLAINLY BECAUSE IT IS THE OPPOSITE OF WHAT THE ISSUE EXPECTED.

        Issue #328 said that pair is 3.3 points apart and "needs 2.3 points to
        be called apart". The 2.3 is the UNSMOOTHED estimate, which is the one
        the same issue says cannot be used, because a cell that won nothing
        gets a variance of exactly zero. Smoothed it is 4.28, and 3.3 does not
        clear it.

        So the fix does NOT separate the pair it was opened about. What it
        separates is the wider pairs, which moved the reported order at tiers
        5, 6 and 7. If a future change makes this pair separate, check what
        moved: it probably means the smoothing was reduced.
        """
        gap = 3.3
        smoothed = experiments.margin_noise_between(0.0, 100.0, 0.0, 96.7, 60)
        assert gap <= smoothed, (
            f"the closest pair at tiers 6 and 7 now separates: a gap of {gap} "
            f"against a tolerance of {smoothed:.2f}. Issue #328's own figure "
            f"of 2.3 was unsmoothed. Check MARGIN_SMOOTHING before treating "
            f"this as a gain.")
        unsmoothed = 100.0 * math.sqrt(
            (experiments.margin_variance(0.0, 100.0)
             + experiments.margin_variance(0.0, 96.7)) / 60.0)
        assert unsmoothed == pytest.approx(2.3, abs=0.05), (
            f"the unsmoothed tolerance for that pair is {unsmoothed:.2f}, not "
            f"the 2.3 issue #328 quotes, so these are no longer the rates the "
            f"issue was measured on and the paragraph above needs rewriting.")
        assert gap > unsmoothed

    def test_the_pair_tolerance_factory_agrees_with_the_function(self):
        wins = {"a": 4.0, "b": 0.0}
        losses = {"a": 90.0, "b": 97.0}
        tolerance = experiments.pair_tolerance_from(wins, losses, 60)
        assert tolerance("a", "b") == experiments.margin_noise_between(
            4.0, 90.0, 0.0, 97.0, 60)

    def test_a_scalar_tolerance_groups_exactly_as_it_did_before(self):
        """`rank_by_score` now checks a candidate against EVERY member of the
        group rather than only the one that opened it. With one tolerance the
        two rules are the same rule, because the list is sorted by score so the
        opener is the furthest member. Checked against a direct copy of the old
        rule over random inputs, because "the same rule" is an argument and
        this is the evidence."""
        rng = random.Random(20260806)

        def old_rule(scores, tolerance):
            groups = []
            for name in sorted(scores, key=lambda n: (-scores[n], n)):
                if groups and abs(groups[-1][0] - scores[name]) <= tolerance:
                    groups[-1][1].append(name)
                else:
                    groups.append([scores[name], [name]])
            return [tuple(names) for _, names in groups]

        for _ in range(400):
            scores = {f"p{i}": round(rng.uniform(-100.0, 100.0), 1)
                      for i in range(rng.randint(2, 8))}
            tolerance = round(rng.uniform(0.0, 40.0), 1)
            assert (experiments.rank_by_score(scores, tolerance)
                    == old_rule(scores, tolerance))

    def test_the_pair_tolerance_is_used_when_it_is_given(self):
        """A gap of 4 with a cap of 10 is a tie; the same gap with a pair
        tolerance of 1 is not."""
        scores = {"a": 10.0, "b": 6.0}
        assert experiments.rank_by_score(scores, 10.0) == [("a", "b")]
        assert experiments.rank_by_score(
            scores, 10.0, pair_tolerance=lambda x, y: 1.0) == [("a",), ("b",)]

    def test_the_cap_stops_a_pair_tolerance_looser_than_the_worst_case(self):
        """The guard, proved by feeding it the input it guards against. A pair
        tolerance of a million must not merge presets the worst case says are
        different."""
        scores = {"a": 90.0, "b": 10.0}
        assert experiments.rank_by_score(
            scores, 5.0, pair_tolerance=lambda x, y: 1e6) == [("a",), ("b",)]

    def test_a_preset_must_clear_every_member_of_a_group_not_just_the_opener(
            self):
        """Why the rule changed. `c` is within the pair tolerance of the group
        opener `a` but not of `b`, which is already in the group. Grouping it
        with both would claim a tie between `b` and `c` that the tolerance for
        that pair rejects."""
        scores = {"a": 10.0, "b": 7.0, "c": 4.0}

        def tolerance(x, y):
            return 1.0 if {x, y} == {"b", "c"} else 100.0

        assert experiments.rank_by_score(
            scores, 100.0, pair_tolerance=tolerance) == [("a", "b"), ("c",)]

    def test_the_result_is_not_always_a_refinement_of_the_caps_grouping(self):
        """SAID OUT LOUD SO NOBODY ASSUMES IT. Issue #328 expected a tighter
        tolerance to split groups and never merge them. It does not follow:
        splitting a group changes which preset opens the next one, so a pair
        can be compared that the cap never compared.

        Here the cap gives (a, b) then (c). The pair tolerance splits a from b,
        which leaves b opening a group, and c then joins b -- a grouping the
        cap's does not contain and which does not contain the cap's.
        """
        scores = {"a": 10.0, "b": 6.0, "c": 2.0}

        def tolerance(x, y):
            return 3.0 if {x, y} == {"a", "b"} else 5.0

        under_cap = experiments.rank_by_score(scores, 5.0)
        under_pair = experiments.rank_by_score(
            scores, 5.0, pair_tolerance=tolerance)
        assert under_cap == [("a", "b"), ("c",)]
        assert under_pair == [("a",), ("b", "c")]
        assert not set(under_pair) <= set(under_cap)
        assert not set(under_cap) <= set(under_pair)


@pytest.fixture(scope="module")
def printed():
    """One two-tier run of the preset section at two campaigns per cell.

    Module-scoped because the section is the slowest thing this file touches
    and every test below reads the same output.
    """
    base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
    return out.getvalue()


class TestTheReportShowsWhatThePerPairToleranceChanged:
    """Issue #328. The report prints both orderings, so a reader can see what
    the change did without running the previous version."""

    def test_it_prints_the_ordering_under_the_cap_as_well(self, printed):
        assert _CAP_ORDER_HEADING in printed
        assert len(_cap_ordering_lines(printed)) == 2

    def test_it_says_whether_the_change_moved_anything(self, printed):
        assert ("The per-pair tolerance CHANGES the reported order" in printed
                or "changes no reported order at any tier" in printed), (
            "the report no longer states whether the per-pair tolerance moved "
            "any ordering. That sentence is what connects issue #328 to issues "
            "#4 and #5, which rest on the ordering.")

    def test_it_says_the_tolerance_is_computed_per_pair(self, printed):
        collapsed = unwrapped(printed)
        assert "THE TOLERANCE IS COMPUTED PER PAIR" in collapsed
        assert "Issue #328" in collapsed
        assert "MARGIN_SMOOTHING" in collapsed, (
            "the report does not name the smoothing constant, so a reader "
            "cannot find why a cell that won nothing has a tolerance at all.")

    def test_the_closest_pair_block_shows_both_verdicts(self, printed):
        """Gap, the tolerance that pair got, and what each of the two figures
        calls it. Without both verdicts the block is two numbers with no
        statement of which one decided."""
        block = printed[printed.index(_ORDER_ENDS_AT):]
        for tier in (1, 8):
            line = next((ln for ln in block.splitlines()
                         if ln.strip().startswith(f"tier {tier}:")), None)
            assert line is not None
            assert ("fewer than two presets ranked" in line
                    or ("closest gap" in line and "cap" in line
                        and ("APART" in line or "tied" in line)))


class TestTheClosestPairIsIdentified:
    """`closest_ranked_pair` finds the gap that decides whether the tolerance
    mattered. Every wider gap survives a tolerance this one survives."""

    def test_it_finds_the_smallest_gap(self):
        assert experiments.closest_ranked_pair(
            {"a": 10.0, "b": 9.0, "c": 2.0}) == ("b", "a")

    def test_it_ignores_excluded_names(self):
        """A preset with no result must not be able to look like the closest
        pair, for the same reason it is not ranked."""
        assert experiments.closest_ranked_pair(
            {"a": 10.0, "gone": 9.5, "c": 2.0}, exclude=["gone"]) == ("c", "a")

    def test_fewer_than_two_names_has_no_pair(self):
        assert experiments.closest_ranked_pair({"a": 1.0}) is None
        assert experiments.closest_ranked_pair({}) is None
        assert experiments.closest_ranked_pair(
            {"a": 1.0, "b": 2.0}, exclude=["a", "b"]) is None


class TestTheOrderingUsesWinMinusLoss:
    """Issue #294. Win rate collapses towards zero above tier 3 and orders
    nothing; win minus loss keeps varying because the loss rate does."""

    #: The measured tier 8 cell, 60 campaigns each, 2026-08-05, with the
    #: Architect preset already excluded because its campaigns had no result.
    TIER_8 = {
        "Explorer via floors (-25 floors)": (8.0, 67.0),
        "Proposed budget (x0.85 time, x0.55 dmg)": (3.0, 72.0),
        "Explorer maxed (as designed)": (2.0, 98.0),
        "No tree": (0.0, 97.0),
        "Explorer via floors (+30 floors)": (0.0, 100.0),
    }

    def test_win_rate_alone_puts_the_measured_tier_8_table_in_one_group(self):
        """WHAT THE ISSUE WAS ABOUT. Five presets inside 8 points, and 60
        campaigns per cell cannot separate anything closer than 9.1."""
        wins = {name: win for name, (win, _) in self.TIER_8.items()}
        order = experiments.rank_by_score(wins, experiments.win_rate_noise(60))
        assert len(order) == 1, (
            f"win rate now separates the measured tier 8 table into "
            f"{len(order)} groups. It did not when issue #294 was closed, and "
            f"the reason the metric changed was that it could not.")

    def test_win_minus_loss_separates_the_same_table(self):
        """THE ANSWER. The same five cells, the same 60 campaigns, ranked on
        the margin with the margin's own tolerance."""
        margins = {name: win - loss
                   for name, (win, loss) in self.TIER_8.items()}
        order = experiments.rank_by_score(margins,
                                          experiments.margin_noise(60))
        assert len(order) > 1, (
            "win minus loss no longer separates the measured tier 8 table, so "
            "it is no better than the win rate it replaced. Issue #294.")
        assert order[0] == ("Explorer via floors (-25 floors)",
                            "Proposed budget (x0.85 time, x0.55 dmg)"), (
            f"the two presets that win anything at tier 8 are no longer the "
            f"leading group. Got {order[0]}.")
        assert "No tree" in order[-1]

    def test_the_spread_it_gives_is_wider_than_the_win_rate_spread(self):
        """41 points against 8 on the measured table. This is what "still
        varies there" means, stated as a number."""
        wins = [win for win, _ in self.TIER_8.values()]
        margins = [win - loss for win, loss in self.TIER_8.values()]
        assert max(margins) - min(margins) > max(wins) - min(wins)

    def test_the_table_prints_the_margin_column(self, capsys):
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert "w-l" in printed, (
            "the preset table no longer prints the win-minus-loss column, so "
            "the column the ordering is built on is not visible. Issue #294.")

    def test_the_ordering_is_built_with_the_per_pair_tolerance(self):
        """Issue #328. A behaviour check cannot see this: when the two
        tolerances happen to give the same grouping -- which they do at the
        two-campaign sample size these tests run at -- a report built without
        the per-pair tolerance is identical to one built with it. So the call
        itself is checked.

        Read out of the syntax tree rather than by matching text, because the
        argument sits on its own line and any reformatting would silently stop
        a string match from checking anything.
        """
        presets = next(node for node in ast.walk(TREE)
                       if isinstance(node, ast.FunctionDef)
                       and node.name == "preset_tables")
        ranked_margins = [
            node for node in ast.walk(presets)
            if isinstance(node, ast.Call)
            and getattr(node.func, "id", None) == "rank_by_score"
            and node.args
            and isinstance(node.args[0], ast.Subscript)
            and getattr(node.args[0].value, "id", None) == "margins"]
        assert ranked_margins, (
            "preset_tables no longer ranks the margins with rank_by_score. "
            "Issue #294. The per-tier ordering moved out of exp_presets into "
            "preset_tables under issue #1297, when the section began running "
            "the whole block once per surge size.")
        with_pair = [call for call in ranked_margins
                     if any(kw.arg == "pair_tolerance" for kw in call.keywords)]
        assert with_pair, (
            "preset_tables ranks the margins without passing pair_tolerance, "
            "so "
            "the ordering is back on the worst-case cap and issue #328 has "
            "been undone. The cap is about four times wider than the high "
            "difficulty tiers need.")
        assert len(with_pair) < len(ranked_margins), (
            "every rank_by_score call on the margins now passes a pair "
            "tolerance, so the report no longer computes the ordering under "
            "the cap alone and cannot show what the change did.")

    def test_the_ordering_is_built_from_the_margins(self):
        """A source check, because the printed order alone cannot tell which
        dict it came from when the two metrics happen to agree.

        BOUNDED TO ONE FUNCTION. It used to slice from `def exp_presets` to the
        end of the file, so anything below could have satisfied it. The per-tier
        ordering moved into `preset_tables` under issue #1297.
        """
        body = function_source("preset_tables")
        assert "rank_by_score(margins[tier], tolerance" in body, (
            "preset_tables no longer ranks on the margins. Issue #294.")
        assert "tolerance = margin_noise(trials)" in body, (
            "preset_tables no longer uses the margin's own tolerance. Using "
            "win_rate_noise on a margin reports half the right figure. "
            "Issue #294.")

    def test_the_surge_size_comparison_uses_the_per_pair_tolerance_too(self):
        """Issue #1297 added a SECOND place that orders the presets, comparing
        one surge size's ordering against another's. It is held to the same rule
        as the per-tier one: the worst-case cap is about four times wider than
        the high difficulty tiers need. Issue #328."""
        ranked = [node for node in ast.walk(function_node("compare_surge_sizes"))
                  if isinstance(node, ast.Call)
                  and getattr(node.func, "id", None) == "rank_by_score"]
        assert ranked, (
            "compare_surge_sizes no longer orders the presets, so it cannot "
            "say whether the ordering survives a change of surge size.")
        assert all(any(kw.arg == "pair_tolerance" for kw in call.keywords)
                   for call in ranked), (
            "compare_surge_sizes orders the presets under the worst-case cap "
            "alone. Issue #328.")

    def test_the_win_rate_ordering_is_kept_as_a_second_opinion(self, capsys):
        """Not deleted. It is the metric the issue was opened about, and
        keeping it printed is what makes the collapse visible in the report
        rather than only in the issue."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert _WIN_ORDER_HEADING in printed
        assert len(_win_ordering_lines(printed)) == 2

    def test_the_report_says_the_tolerance_is_not_the_win_rates(self, capsys):
        """The one thing a reader of the output could get wrong, given the two
        orderings now sit next to each other with different tolerances."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = unwrapped(capsys.readouterr().out)
        assert "ITS TOLERANCE IS NOT THE WIN RATE'S" in printed
        assert "+1 won, -1 lost, 0 no result" in printed
        assert "exactly twice the win rate's" in printed

    def test_the_report_shows_how_conservative_the_cap_is(self, capsys):
        """The cap assumes a cell that wins half its campaigns and loses the
        rest. At tier 8 nothing does, so it is far wider than the one that pair
        needs, and the report says so per tier.

        WHAT THIS USED TO ASSERT. Until issue #328 the block was headed HOW
        CONSERVATIVE THAT TOLERANCE IS, because the worst-case figure was the
        tolerance the grouping used. It is now the cap, and the tolerance is
        computed per pair, so the heading names what the per-pair figure buys.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert _ORDER_ENDS_AT in printed
        block = printed[printed.index(_ORDER_ENDS_AT):]
        for tier in (1, 8):
            assert (f"tier {tier}: closest gap" in block
                    or f"tier {tier}: fewer than two presets ranked" in block)

class TestThePresetSectionCoversBothEnds:
    def test_it_runs_at_more_than_one_tier(self):
        assert len(experiments.PRESET_TIERS) > 1
        assert all(t in range(1, 9) for t in experiments.PRESET_TIERS)

    def test_it_covers_both_ends_of_the_curve(self):
        """Tier width multiplies every weighted term, so the ends are the test."""
        assert min(experiments.PRESET_TIERS) == 1
        assert max(experiments.PRESET_TIERS) == 8

    def test_it_returns_a_win_rate_for_every_preset_at_every_tier(self):
        """Small sample: this checks the shape, not the numbers."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        wins = experiments.exp_presets(base, tiers=(1, 8), trials=2)
        assert set(wins) == {1, 8}
        for tier in (1, 8):
            assert set(wins[tier]) == {tree.name for tree in experiments.PRESETS}
            assert all(0.0 <= rate <= 100.0 for rate in wins[tier].values())

    def test_the_table_reports_quest_objectives_cleared(self, capsys):
        """Issue #294. Win rate collapses towards zero above tier 3 and stops
        ranking the presets. Objectives cleared is the same axis measured before
        it saturates -- a win requires all of them -- and `summarise` already
        computed it while the table did not print it.

        It is printed whether or not it turned out to separate them, because it
        is the column that explains the collapse: at tier 8 the campaigns are
        not failing at the final fight, they are failing to clear the quest
        objectives at all.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert f"obj/{base.quest_objectives_required}" in printed, (
            "the preset table no longer reports quest objectives cleared. "
            "Issue #294 added it because win rate stops separating the presets "
            "above tier 3.")

    def test_the_objectives_column_is_labelled_from_the_config(self, capsys):
        """The heading says how many objectives a win needs. Reading it from the
        config rather than writing 8 into a format string means changing
        `quest_objectives_required` cannot leave the table lying about what the
        number is out of."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER,
                       quest_objectives_required=5)
        experiments.exp_presets(base, tiers=(1,), trials=2)
        printed = capsys.readouterr().out
        assert "obj/5" in printed, (
            "the preset table's objectives heading does not follow "
            "quest_objectives_required. It is hard-coded, so changing how many "
            "objectives a win needs would leave the column labelled wrongly.")
        assert "obj/8" not in printed

    def test_it_records_that_objectives_did_not_solve_the_problem(self, capsys):
        """CLAUDE.md: say what did not work, plainly and first. Objectives
        cleared was measured on 2026-08-05 across all eight tiers and does NOT
        separate the presets -- it saturates at the low tiers, where five of six
        sit within 0.1 of each other, and collapses at the high tiers exactly as
        win rate does.

        A reader who sees a new column added for issue #294 would otherwise
        reasonably assume it worked. The report says outright that it did not,
        and names the metric that was chosen instead.

        WHAT THIS USED TO ASSERT. That the report says "Issue #294 stays open".
        It no longer does: the issue chose win rate minus loss rate and is
        closed. The column stays because it explains the collapse -- at tier 8
        campaigns are not losing the final fight, they are clearing 1.1 of the 8
        objectives needed to reach it -- and this test now checks that the
        report says which metric replaced it rather than leaving the reader to
        assume this column is the answer.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = unwrapped(capsys.readouterr().out)
        assert "IT DOES NOT" in printed, (
            "the preset section prints objectives cleared without saying it "
            "was measured as a replacement for win rate and failed. A reader "
            "would take a column added for issue #294 as the answer to it.")
        assert "Win minus loss is the metric issue #294 settled on" in printed, (
            "the preset section says objectives cleared failed without naming "
            "what replaced it. The ordering above it is on win minus loss and "
            "the report has to connect the two.")
        assert "Issue #294 stays open" not in printed, (
            "the preset section still says issue #294 is open. It was closed "
            "when win rate minus loss rate was adopted as the ranking metric.")

    def test_the_objectives_spread_is_reported_per_tier(self, capsys):
        """The spread is what decides whether a metric separates anything. One
        number per tier, so a later reader can see at a glance whether the
        2026-08-05 finding still holds after a tuning change."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert "spread over" in printed, (
            "the preset section no longer reports the spread of objectives "
            "cleared per tier, which is the measurement issue #294 turns on.")

    def test_the_table_header_states_the_day_cap(self, capsys):
        """Issue #293. A campaign that runs out of days has no result, so the
        day budget is part of what the table measured and belongs beside the
        power ceiling rather than only in the config file."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1,), trials=2)
        printed = capsys.readouterr().out
        assert f"day cap {base.max_days:,}" in printed, (
            "the preset table no longer states the day cap it ran under. A "
            "campaign that hits the cap has no result, so the cap is part of "
            "what the table measured. Issue #293.")


class TestTheSweepFlagsCampaignsWithNoResult:
    """Issue #293. `engine.Simulation.run` is

        while self.day < self.cfg.max_days and not self.lost and not self.won:

    with no other exit, so a campaign that is neither won nor lost ran out of
    days and has NO result. The `stale%` column reads like a third outcome
    beside winning and losing and is not one.

    At difficulty tier 8 the Architect preset scores 0% win, 0% loss and 100%
    no result, and every ranking metric proposed on issue #294 reads that as a
    good score -- win minus loss ranks it FIRST, ahead of the only preset that
    wins anything. So the sweep has to say which cells are unresolved.

    These run the warning function directly rather than through a sweep,
    because producing a 100%-unresolved cell honestly costs about 150 seconds
    and a test that waits that long will not be run.
    """

    def test_it_says_nothing_when_every_cell_resolved(self, capsys):
        """A warning that always fires is not a warning. Ordinary tier 1 cells
        have stalemate rates around 20% and must not trip it."""
        flagged = experiments.warn_about_unresolved_campaigns(
            ["a", "b"], {"a": 21.0, "b": 3.0}, 2500, 0.10)
        assert flagged == []
        assert capsys.readouterr().out == "", (
            "the sweep warns about unresolved campaigns for a table where "
            "every cell resolved. Tier 1 stalemate rates run to about 20% and "
            "are normal.")

    def test_it_names_every_preset_whose_campaigns_mostly_had_no_result(self,
                                                                       capsys):
        flagged = experiments.warn_about_unresolved_campaigns(
            ["fine", "truncated", "also truncated"],
            {"fine": 12.0, "truncated": 100.0, "also truncated": 64.0},
            2500, 0.10)
        assert flagged == ["truncated", "also truncated"]
        printed = capsys.readouterr().out
        assert "truncated: 100% of campaigns ran out of days" in printed
        assert "also truncated: 64% of campaigns ran out of days" in printed
        assert "fine" not in printed, (
            "the warning names a preset whose campaigns did resolve.")

    def test_it_says_why_the_numbers_are_not_comparable(self, capsys):
        """Naming the cell is not enough. A reader looking at 0% win and 0%
        loss needs to be told those are not two measurements of a preset, they
        are the absence of any measurement."""
        experiments.warn_about_unresolved_campaigns(
            ["x"], {"x": 100.0}, 2500, 0.10)
        printed = capsys.readouterr().out
        assert "not" in printed and "comparable" in printed, (
            "the warning names the unresolved presets without saying their "
            "win and loss rates cannot be compared with the others. Issue "
            "#293.")

    def test_it_records_that_raising_the_day_cap_is_not_a_clean_control(self,
                                                                       capsys):
        """The measurement issue #293 asked for was run on 2026-08-05 and its
        result is that the experiment cannot answer the question. Dungeon power
        is keyed to ELAPSED DAYS, so a run given more days is given a harder
        game, and the day cap is not an independent variable.

        Without this sentence the obvious next step -- raise max_days -- looks
        like a fix. It is not, and it costs about eight times the sweep's
        runtime to find that out again."""
        experiments.warn_about_unresolved_campaigns(
            ["x"], {"x": 100.0}, 2500, 0.10)
        printed = capsys.readouterr().out
        assert "not an" in printed and "independent variable" in printed, (
            "the warning does not record that raising the day cap changes the "
            "difficulty as well as the length of the run. Issue #293.")
        assert "10% per 100 days" in printed, (
            "the warning states that dungeon power is keyed to elapsed days "
            "without the rate. The rate is what decides whether the confound "
            "matters, and it must be the rate the caller ran at. Issue #1290.")

    def test_the_printed_rate_is_the_one_the_caller_ran_at(self, capsys):
        """Whatever rate is handed in is the rate printed, and the multiplier
        printed beside it is that rate carried to the day cap.

        REPLACES a test that asserted `TuningConfig`'s default was still 0.22 so
        that a hard-coded sentence stayed true. That guard could only catch the
        default moving. It could not catch what actually happened -- the default
        staying where it was while the section ran at a different rate chosen by
        `exp_forge` -- so the warning printed 22% and 6.5x beside a table
        produced at 10% and 3.5x. Issue #1290."""
        for rate, stated, multiplier in (
                (0.00, "0% per 100 days", "1.0x"),
                (0.10, "10% per 100 days", "3.5x"),
                (0.22, "22% per 100 days", "6.5x"),
                (0.50, "50% per 100 days", "13.5x"),
        ):
            experiments.warn_about_unresolved_campaigns(
                ["x"], {"x": 100.0}, 2500, rate)
            printed = capsys.readouterr().out
            assert stated in printed, (
                f"the caller ran at {rate} per 100 days and the warning did "
                f"not say so. Issue #1290.")
            assert multiplier in printed, (
                f"{rate} per 100 days is {multiplier} base power at a 2,500 "
                f"day cap, and the warning did not say so. That multiplier is "
                f"the number showing the confound is large. Issue #1290.")

    def test_section_seven_need_not_run_at_the_config_default(self):
        """Why the rate has to come from the caller rather than from config.py.

        `exp_forge` sweeps `FORGE_ESCALATION_RATES` and hands section 7 whichever
        cell wins, so the default is only the starting point. On 2026-09-05 the
        sweep chose 0.10 against a default of 0.22. Issue #1290."""
        default = TuningConfig().dungeon_power_escalation_per_100_days
        assert default in experiments.FORGE_ESCALATION_RATES, (
            "the forge sweep no longer includes the config default, so the "
            "two can no longer even be compared.")
        assert len(set(experiments.FORGE_ESCALATION_RATES)) > 1, (
            "the forge sweep has only one escalation rate, so section 7 always "
            "runs at it and the printed rate could safely be written out. If "
            "that is now true, this test and the caller argument can go.")

    def test_the_threshold_is_a_majority_of_campaigns(self):
        """Below half, the resolved campaigns are still the larger sample and
        the rates mean something. At or above half they do not."""
        assert experiments.UNRESOLVED_WARNING_PERCENT == 50.0

    def test_a_cell_exactly_at_the_threshold_is_flagged(self, capsys):
        """Half the campaigns having no result is already too many, and an
        off-by-one here would silently drop the borderline cases the warning
        exists for."""
        assert experiments.warn_about_unresolved_campaigns(
            ["x"], {"x": experiments.UNRESOLVED_WARNING_PERCENT},
            2500, 0.10) == ["x"]
        capsys.readouterr()

    def test_the_warning_reaches_the_printed_table(self, capsys):
        """Every test above calls the warning directly, which proves what it
        says and not that anything calls it. This runs the real preset section
        with a day cap of 40, so no campaign can finish and every cell is
        unresolved, and checks the warning comes out.

        40 days rather than the real 2,500 because a cell that is genuinely
        100% unresolved at the real cap costs about 150 seconds to produce.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER, max_days=40)
        experiments.exp_presets(base, tiers=(1,), trials=2)
        printed = capsys.readouterr().out
        assert "NO RESULT for most campaigns" in printed, (
            "exp_presets does not warn about unresolved campaigns even when "
            "every campaign in the table ran out of days. The warning function "
            "exists but nothing calls it. Issue #293.")
        assert "day cap 40" in printed

    def test_no_warning_appears_when_the_cells_resolve(self, capsys):
        """The other half. At the real day cap and tier 1 the presets resolve,
        so the warning must stay silent -- otherwise it would appear on every
        table and stop meaning anything.

        WHY `trials` IS 25 AND NOT 4. It was 4, and issue #1338 made it fail:
        one preset had 2 campaigns of 4 with no result, which is exactly the
        50% threshold. FOUR CAMPAIGNS CANNOT DISTINGUISH THE TWO CASES THIS
        TEST EXISTS TO SEPARATE. Measured at 300 campaigns per cell, tier 1,
        `triage`, surge size 4, the true unresolved rate across the six presets
        runs 2.0% to 26.0% -- the highest of them less than half the threshold.
        At 4 campaigns a cell at 26% trips the threshold better than one time
        in four, so the old sample passed by luck both before this change and
        after the sub-type work that raised the rates in the first place.

        The rate did rise with #1338, and the rise is real rather than noise:
        the same measurement under the old fixed Demonic draw runs 0.0% to
        24.7%. Drawing the Cataclysm per character brings in Celestial, which
        sends few and deep dungeons, so more campaigns reach the day cap having
        neither won nor lost. It is nowhere near the threshold either way.

        25 campaigns costs about 9 seconds against about 1.5. That buys a test
        that measures something: at a true rate of 26% a cell needs 13 of 25 to
        trip, which is 2.7 standard errors out.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1,), trials=25)
        printed = capsys.readouterr().out
        assert "NO RESULT for most campaigns" not in printed, (
            "the unresolved-campaign warning fires on an ordinary tier 1 "
            "table, where the campaigns do resolve. A warning that always "
            "fires is not a warning.")

    def test_the_campaign_loop_has_no_exit_other_than_the_day_cap(self):
        """The whole warning rests on this. If the day loop ever gains another
        way out, a campaign could end with no result for a different reason and
        the warning would be attributing it wrongly."""
        import inspect

        from cataclysm_sim import engine

        source = inspect.getsource(engine.Simulation.run)
        body = source[source.index("while"):]
        assert "break" not in body, (
            "engine.Simulation.run now has a break in its day loop, so a "
            "campaign can end without winning, losing, or running out of "
            "days. The unresolved-campaign warning in experiments.py says "
            "otherwise. Issue #293.")
        assert "self.day < self.cfg.max_days" in body, (
            "engine.Simulation.run no longer bounds its day loop by "
            "cfg.max_days. Issue #293.")

    def test_the_preset_order_leaves_the_unresolved_cells_out(self, capsys):
        """Issue #294. The warning names the cells; this checks the ranking
        then acts on it.

        A day cap of 40 so that no campaign can finish and every cell is
        unresolved, which is the strongest form: not one preset should appear
        in an ordering line, and the section should say it has no ranking
        rather than printing six presets tied at zero.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER, max_days=40)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert "LEFT OUT OF THE ORDER" in printed, (
            "the preset section ranks presets whose campaigns ran out of days "
            "without saying it left any out. Issue #294.")
        assert "NO PRESET RANKED" in printed
        assert "NO RANKING AT ALL" in printed, (
            "every preset was unresolved and the section did not say so. An "
            "empty ordering at every tier compares equal to an empty ordering "
            "at every other tier, so without this it would report that the "
            "ordering is the same at every tier. Issue #294.")
        assert "the ordering is THE SAME" not in printed.replace(
            "The ordering is THE SAME", "the ordering is THE SAME")

    def test_no_preset_name_reaches_an_ordering_line_when_none_resolved(self,
                                                                       capsys):
        """The previous test checks the words. This checks the data: with every
        cell unresolved, no preset may appear on a `tier N:` line."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER, max_days=40)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        ordering_lines = _ordering_lines(capsys.readouterr().out)
        assert ordering_lines, "the section printed no ordering lines at all."
        for line in ordering_lines:
            for tree in experiments.PRESETS:
                assert tree.name not in line, (
                    f"{tree.name!r} is ranked on {line.strip()!r} even though "
                    "its campaigns ran out of days and it has no result. "
                    "Issue #294.")

    def test_a_preset_unresolved_at_one_tier_is_left_out_at_every_tier(self):
        """Why the exclusion is not per tier.

        The point of printing an order per tier is to compare them, and two
        orders over different sets of presets are not comparable. Grouping is
        chained, so an order over five presets is not the six-preset order with
        a row deleted. `exp_presets` therefore builds one exclusion set across
        all the tiers it compares, and this checks the source says so rather
        than filtering per tier.
        """
        body = function_source("preset_tables")
        assert "no_result_at" in body
        assert "exclude=no_result_at" in body, (
            "exp_presets no longer passes its unresolved presets to "
            "rank_by_score. Issue #294.")
        assert body.count("no_result_at.setdefault") == 1, (
            "the exclusion set is no longer accumulated across every tier, so "
            "the orderings can cover different presets and stop being "
            "comparable. Issue #294.")

    def test_nothing_is_left_out_when_every_cell_resolves(self, capsys):
        """The other half. At the real day cap the low tiers resolve, so no
        preset may be dropped -- otherwise the section would quietly rank fewer
        presets than the table shows on every run.

        **SIXTEEN CAMPAIGNS PER CELL, AND THE COUNT IS THE FRAGILE PART.** The
        campaigns are seeded from 0 upwards so this is reproducible rather than
        flaky, but the threshold is 50% and more than one cell sits close to
        it, so a small sample can land on the wrong side of a line the true
        rate is nowhere near.

        Four was too few: at tier 2, Explorer via floors (-25 floors) left two
        of four unresolved, which is exactly 50%.

        **Eight was enough until two separate changes on 2026-09-06 each pushed
        a different cell past it**, which is why the count was raised once and
        this docstring names two causes:

          * issue #1315 gave the Cataclysm boss a floor for every ordinary
            dungeon cleared, which lengthens campaigns. Over 60 campaigns the
            "Proposed budget (x0.85 time, x0.55 dmg)" cell at tier 2 went from
            **30% to 37%** of campaigns running out of days;
          * issue #1327 moved the Architect preset, whose tier 2 cell genuinely
            sits at about **35%** -- measured over 60 campaigns, and true on
            `development` as well as here.

        Neither rate is over the threshold. At eight campaigns the estimate
        landed on 4 of 8 and the check is `>=`, so the test failed on its
        sample size rather than on what it names.

        **AND SIXTEEN LASTED ONE DAY.** Issue #1324's move chance -- the owner's
        "a chance each time", built at 0.5 -- adds a draw to every quest timer,
        so every later draw in a campaign shifts. The
        "Proposed budget (x0.85 time, x0.55 dmg)" cell at **tier 1** went from 6
        of 16 to exactly 8 of 16, and the check is `>=`.

        **THE UNDERLYING RATE DID NOT MOVE, AND IT WAS MEASURED BEFORE THE COUNT
        WAS RAISED**, which is what the paragraph below used to demand and now
        records:

        | campaigns | on `development` | with the move chance |
        | --: | --: | --: |
        | 8 | 50.0% | 62.5% |
        | 16 | 37.5% | **50.0%** |
        | 32 | 34.4% | 34.4% |
        | 60 | 31.7% | 31.7% |
        | 120 | 34.2% | 35.0% |

        Identical at 32 and 60 and within a point at 120. **Thirty-two is where
        the estimate meets the 120-campaign figure**, which is the reason for
        that number rather than the next one up: at 24 the same cell reads 41.7%
        and is still sampling rather than measuring. It costs about 38 seconds
        against 13 for sixteen, measured on 2026-09-06.

        **IF IT FAILS AGAIN, MEASURE THE UNDERLYING RATE OVER 60 CAMPAIGNS
        BEFORE RAISING THIS NUMBER**: a genuine move past 50% is a finding about
        the game, and raising the count would hide it. **And consider not
        raising it at all.** This is the third raise, each after a change that
        had nothing to do with what the test names, and the reason is structural
        rather than bad luck -- a printed artefact gated on a noisy rate crossing
        a fixed threshold, with the nearest cell's true rate at about 34%. Issue
        [#1385](https://github.com/sdubois777/Cataclysm/issues/1385) carries
        that, with three cheaper shapes for the check.

        THE UNDERLYING RATES ARE NOT A TEST PROBLEM. A maxed Architect empire
        reaches the day cap without winning or losing in 13% of campaigns at
        difficulty tier 1, 35% at tier 2 and 97% at tier 4. That is issue #1336
        and is not caused by this file.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 2), trials=32)
        printed = capsys.readouterr().out
        assert "LEFT OUT OF THE ORDER" not in printed, (
            "the preset section drops presets from the ranking on an ordinary "
            "table where every campaign resolved.")
        assert "NO PRESET RANKED" not in printed
        ranked = [line for line in _ordering_lines(printed)
                  if line.strip().startswith("tier 1:")]
        assert len(ranked) == 1
        for tree in experiments.PRESETS:
            assert tree.name in ranked[0], (
                f"{tree.name!r} is missing from the tier 1 ordering even "
                "though its campaigns resolved.")

    def test_the_tier_actually_changes_the_run(self, capsys):
        """Otherwise the second table would be a copy of the first.

        The power ceiling printed above each table comes from the tier, so the
        two headings differ. Checked because passing `tiers` and then forgetting
        to apply it is the obvious way to write this wrong.
        """
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert f"TIER 1 -- player power ceiling {scoring.tier_bounds(1)[1]:,.0f}" in printed
        assert f"TIER 8 -- player power ceiling {scoring.tier_bounds(8)[1]:,.0f}" in printed
        assert scoring.tier_bounds(1)[1] != scoring.tier_bounds(8)[1]
