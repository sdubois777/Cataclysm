"""Section 7 must say whether a preset beats taking no tree at all.

WHY THIS FILE EXISTS. Issue #5 reported that maxing the Architect quadrant of the
empire tree "produces the same win rate as allocating no passive points at all",
quoting 52% against 52% out of `experiments.exp_presets` at 150 campaigns per
cell. Both numbers were right and the conclusion was not.

`experiments.win_rate_noise(150)` is 5.77 percentage points. A nil gap at that
sample size is not a measurement of equality; it is the sample failing to resolve
whatever the gap is. Re-run at 1,000 campaigns per cell over two disjoint blocks
of seeds, difficulty tier 1, the `triage` policy, the same calibrated config
section 7 receives:

    preset                          seeds 0-999   seeds 1000-1999   both
    No tree                                45.8              47.5   46.6
    Architect maxed (as designed)          55.0              57.5   56.2

The branch is 9.6 points ahead, replicated, against a 1.58 point tolerance at
2,000 campaigns.

THE FIGURE HAS MOVED THREE TIMES AND THE CONCLUSION HAS NOT. Issue #1282 put the
Corrupted Stalker into the pool the dungeon modifier draw reads, re-rolling every
campaign and making the gap 7.1. Issue #1303 took it back out on the owner's
ruling and it returned to 10.4, exactly, block by block. Issue #1288 then
corrected `city_damage_mult` from an untraceable 0.023 to 0.0766, the product of
the eleven damage-reduction nodes, and it became 9.6. Every one of those is far
outside the 1.58 point tolerance, which is why the branch beating no tree has
survived three changes to the model underneath it.

The report could not have shown that. Its orderings do carry a tolerance, but
`rank_by_score` is given an `exclude` set holding every preset that failed to
resolve at ANY tier, so that each tier's ordering covers the same presets -- issue
#294. The Architect preset has no result in 94% of its tier 8 campaigns, so it is
left out at tier 8 AND at tier 1, where it resolved in 91% of them. It appears in
no ordering at any tier, while its win rate is still printed in the table next to
the no-tree row, where a reader compares the two.

WHAT IS CHECKED HERE. That the per-pair win-rate tolerance is sound and is
bounded by the worst-case one; that `compare_against_no_tree` covers every preset
including those the orderings leave out; that it refuses to compare a preset whose
campaigns had no result; and that the section prints the comparison and the
warning that a nil gap is not a nil difference. None of it runs the sweep, which
is twenty minutes. The section is exercised at a sample size that checks its shape
and not its numbers.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import sys
from dataclasses import replace

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import experiments  # noqa: E402
from cataclysm_sim.config import TREE_NONE, TuningConfig  # noqa: E402


def run_presets(**kwargs) -> str:
    """Section 7's output, captured. Small samples: shape, not numbers."""
    base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        experiments.exp_presets(base, **kwargs)
    return buffer.getvalue()


class TestTheWinRatePairTolerance:
    """`win_rate_noise_between`, the per-pair version of `win_rate_noise`."""

    @pytest.mark.parametrize("trials", [25, 60, 150, 400, 1000])
    def test_it_equals_the_worst_case_bound_when_both_cells_win_half(
            self, trials):
        """The bound's worst case is p = 0.5 for both cells, so the two must
        agree there. Smoothing pulls a rate toward 1/3, so 50/50 win/loss with
        nothing unresolved is left almost exactly where it is."""
        pair = experiments.win_rate_noise_between(50.0, 50.0, 50.0, 50.0,
                                                  trials)
        assert pair == pytest.approx(experiments.win_rate_noise(trials),
                                     rel=0.02)

    @pytest.mark.parametrize("win_a,loss_a,win_b,loss_b", [
        (0.0, 100.0, 0.0, 100.0),
        (2.0, 95.0, 1.0, 96.0),
        (52.0, 42.0, 52.0, 39.0),
        (99.0, 1.0, 98.0, 2.0),
        (0.0, 0.0, 0.0, 0.0),
    ])
    @pytest.mark.parametrize("trials", [60, 150, 1000])
    def test_it_never_exceeds_the_worst_case_bound(
            self, win_a, loss_a, win_b, loss_b, trials):
        """It can report a real difference as a tie; it must not report noise
        as a difference by being looser than the bound."""
        assert (experiments.win_rate_noise_between(
            win_a, loss_a, win_b, loss_b, trials)
            <= experiments.win_rate_noise(trials) + 1e-9)

    def test_a_cell_that_won_nothing_is_not_given_a_tolerance_of_zero(self):
        """Without smoothing, p = 0 gives p(1 - p) = 0 and any non-zero gap is
        called significant. That is the defect issue #328 fixed for the margin
        path, and this shares its smoothing."""
        assert experiments.win_rate_noise_between(
            0.0, 100.0, 0.0, 100.0, 150) > 0.5

    def test_it_shrinks_as_the_sample_grows(self):
        wide = experiments.win_rate_noise_between(50.0, 40.0, 55.0, 35.0, 150)
        narrow = experiments.win_rate_noise_between(50.0, 40.0, 55.0, 35.0,
                                                    1500)
        assert narrow < wide / 2.0


class TestCompareAgainstNoTree:
    """The comparison itself, on rates handed in rather than simulated."""

    #: The tier 1 row of section 7 as it stood when issue #5 was written, and
    #: reproduced unchanged on 2026-09-05.
    WINS = {
        "No tree": 52.0,
        "Architect maxed (as designed)": 52.0,
        "Explorer maxed (as designed)": 60.0,
        "Explorer via floors (+30 floors)": 3.0,
    }
    LOSSES = {
        "No tree": 42.0,
        "Architect maxed (as designed)": 39.0,
        "Explorer maxed (as designed)": 33.0,
        "Explorer via floors (+30 floors)": 97.0,
    }
    #: Written out rather than derived in the class body, where a comprehension
    #: cannot see its sibling attributes.
    STALE = {
        "No tree": 6.0,
        "Architect maxed (as designed)": 9.0,
        "Explorer maxed (as designed)": 7.0,
        "Explorer via floors (+30 floors)": 0.0,
    }

    def test_the_three_rates_of_each_row_add_up(self):
        """A campaign is won, lost, or has no result. If these three tables
        disagree the fixture is not a table section 7 could have printed."""
        for name in self.WINS:
            assert (self.WINS[name] + self.LOSSES[name]
                    + self.STALE[name]) == pytest.approx(100.0), name

    def compare(self, trials=150):
        return experiments.compare_against_no_tree(
            self.WINS, self.LOSSES, self.STALE, trials)

    def test_the_baseline_row_is_not_compared_with_itself(self):
        assert experiments.BASELINE_PRESET not in self.compare()

    def test_the_baseline_name_is_the_no_tree_preset(self):
        """Written out, this would survive a rename of the preset and point at
        a row that is no longer in the table."""
        assert experiments.BASELINE_PRESET == TREE_NONE.name

    def test_every_other_preset_is_covered(self):
        got = self.compare()
        assert set(got) == set(self.WINS) - {experiments.BASELINE_PRESET}

    def test_the_gap_issue_5_read_as_zero_is_reported_as_unresolvable(self):
        """52% against 52% at 150 campaigns. This is the reading the issue made,
        and the verdict has to say the sample cannot settle it."""
        gap, tolerance, verdict = self.compare()[
            "Architect maxed (as designed)"]
        assert gap == 0.0
        assert tolerance > 5.0
        assert verdict == "cannot be told apart"

    def test_a_gap_either_side_of_the_tolerance_reads_differently(self):
        """What 150 campaigns can and cannot do, stated as a test.

        NOT that the true 9.6 point gap is unresolvable at 150 campaigns -- it
        is resolvable, at about 5.7 points for this pair. What happened in issue
        #5 is that the section OBSERVED a nil gap when the truth is 9.6, which
        is an unlucky draw of roughly 1.7 standard errors, and the issue read
        that observation as equality. A nil observation carries no information
        about a 9.6 truth, which is why the verdict has to say so rather than
        leave two equal-looking numbers side by side.
        """
        name = "Architect maxed (as designed)"
        tolerance = experiments.win_rate_noise_between(
            self.WINS[name], self.LOSSES[name],
            self.WINS["No tree"], self.LOSSES["No tree"], 150)
        assert 4.0 < tolerance < 8.0, tolerance

        under = dict(self.WINS)
        under[name] = self.WINS["No tree"] + tolerance - 0.5
        assert experiments.compare_against_no_tree(
            under, self.LOSSES, self.STALE, 150)[name][2] == \
            "cannot be told apart"

        over = dict(self.WINS)
        over[name] = self.WINS["No tree"] + tolerance + 0.5
        assert experiments.compare_against_no_tree(
            over, self.LOSSES, self.STALE, 150)[name][2] == "BETTER"

    def test_a_nil_gap_stays_unresolvable_however_large_the_sample(self):
        """A genuinely equal pair is never called different, at any sample size.
        The tolerance shrinks but the gap it is compared against is zero."""
        for trials in (150, 2000, 50_000):
            assert experiments.compare_against_no_tree(
                self.WINS, self.LOSSES, self.STALE,
                trials)["Architect maxed (as designed)"][2] == \
                "cannot be told apart"

    def test_a_clearly_better_preset_reads_better(self):
        assert self.compare()["Explorer maxed (as designed)"][2] == "BETTER"

    def test_a_clearly_worse_preset_reads_worse(self):
        assert self.compare()["Explorer via floors (+30 floors)"][2] == "WORSE"

    def test_a_preset_whose_campaigns_had_no_result_is_not_given_a_gap(self):
        """0% win and 0% loss is the absence of a result, not a poor one. Issue
        #293. Comparing its win rate to anything is comparing nothing."""
        wins = dict(self.WINS)
        wins["Architect maxed (as designed)"] = 0.0
        losses = dict(self.LOSSES)
        losses["Architect maxed (as designed)"] = 0.0
        stale = dict(self.STALE)
        stale["Architect maxed (as designed)"] = 100.0
        got = experiments.compare_against_no_tree(wins, losses, stale, 150)
        assert got["Architect maxed (as designed)"] == (0.0, 0.0, "no result")

    def test_the_no_result_cutoff_is_the_one_the_warning_uses(self):
        """One threshold, so the table cannot warn about a preset and then rank
        it, or rank one it warned about."""
        stale = dict(self.STALE)
        name = "Architect maxed (as designed)"
        stale[name] = experiments.UNRESOLVED_WARNING_PERCENT
        assert experiments.compare_against_no_tree(
            self.WINS, self.LOSSES, stale, 150)[name][2] == "no result"
        stale[name] = experiments.UNRESOLVED_WARNING_PERCENT - 0.1
        assert experiments.compare_against_no_tree(
            self.WINS, self.LOSSES, stale, 150)[name][2] != "no result"


class TestSectionSevenPrintsIt:
    """The output, at a sample size that checks shape and not numbers."""

    def test_the_comparison_is_printed_for_each_tier_and_surge_size(self):
        """Issue #1297 gave the section a second surge size, so the comparison
        is printed once per tier per surge size. Derived rather than written
        out: a fixed 2 would have quietly stopped covering half the tables."""
        out = run_presets(tiers=(1, 8), trials=2)
        blocks = out.count("DUNGEONS PER SURGE")
        assert blocks == 2, f"expected two surge blocks, found {blocks}"
        assert out.count("AGAINST THE 'No tree' ROW") == 2 * blocks

    def test_it_warns_that_a_nil_gap_is_not_a_nil_difference(self):
        """The sentence that would have stopped issue #5 being written."""
        out = run_presets(tiers=(1,), trials=2)
        assert "'CANNOT BE TOLD APART' IS NOT 'NO DIFFERENCE'" in out
        assert "cannot" in out and "resolve the gap" in out

    def test_it_names_the_sample_size_and_what_that_sample_can_resolve(self):
        """Both facts, not the sentence they sit in. Rewrapping the paragraph
        moves them between lines and changes the capital at its start."""
        out = run_presets(tiers=(1,), trials=8)
        assert "8 campaigns per cell" in out
        assert f"{experiments.win_rate_noise(8):.1f} points" in out

    def test_it_covers_the_presets_the_orderings_leave_out(self):
        """The Architect preset is excluded from every ordering because of its
        tier 8 behaviour, so this is the only place it is compared at all.
        Issue #5."""
        out = run_presets(tiers=(1, 8), trials=2)
        head = out.index("AGAINST THE 'No tree' ROW")
        tail = out.index("PRESET ORDER BY WIN RATE MINUS LOSS RATE")
        for tree in experiments.PRESETS:
            if tree.name == experiments.BASELINE_PRESET:
                continue
            assert tree.name in out[head:tail], (
                f"{tree.name} is not compared against the no-tree row")

    def test_the_baseline_row_is_not_listed_as_beating_itself(self):
        out = run_presets(tiers=(1,), trials=4)
        block = out[out.index("AGAINST THE 'No tree' ROW"):]
        block = block[:block.index("'CANNOT BE TOLD APART'")]
        listed = [line for line in block.splitlines()
                  if line.startswith("    " + experiments.BASELINE_PRESET)]
        assert listed == []
