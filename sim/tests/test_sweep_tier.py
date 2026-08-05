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
import pathlib
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


class TestTheOrderingIsReportedHonestly:
    def test_two_rates_a_sample_cannot_separate_are_shown_tied(self):
        tolerance = experiments.win_rate_noise(150)
        order = experiments.rank_by_win({"a": 39.0, "b": 38.0, "c": 20.0},
                                        tolerance)
        assert order == [("a", "b"), ("c",)]

    def test_the_best_group_comes_first(self):
        order = experiments.rank_by_win({"low": 1.0, "high": 90.0}, tolerance=0.0)
        assert order[0] == ("high",)

    def test_everything_tied_collapses_to_one_group(self):
        """What tier 8 does: the win rate goes to zero and stops discriminating."""
        order = experiments.rank_by_win({"a": 0.0, "b": 0.0, "c": 0.0},
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
        noisy = experiments.rank_by_win({"a": 39.0, "b": 38.0}, tolerance=0.0)
        assert noisy == [("a",), ("b",)]


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
        reasonably assume it worked. The report says outright that it did not
        and that the issue is still open."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1, 8), trials=2)
        printed = capsys.readouterr().out
        assert "IT DOES NOT" in printed, (
            "the preset section prints objectives cleared without saying it "
            "was measured as a replacement for win rate and failed. Issue #294 "
            "is still open and the report should not imply otherwise.")
        assert "Issue #294 stays open" in printed

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
            ["a", "b"], {"a": 21.0, "b": 3.0}, 2500)
        assert flagged == []
        assert capsys.readouterr().out == "", (
            "the sweep warns about unresolved campaigns for a table where "
            "every cell resolved. Tier 1 stalemate rates run to about 20% and "
            "are normal.")

    def test_it_names_every_preset_whose_campaigns_mostly_had_no_result(self,
                                                                       capsys):
        flagged = experiments.warn_about_unresolved_campaigns(
            ["fine", "truncated", "also truncated"],
            {"fine": 12.0, "truncated": 100.0, "also truncated": 64.0}, 2500)
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
        experiments.warn_about_unresolved_campaigns(["x"], {"x": 100.0}, 2500)
        printed = capsys.readouterr().out
        assert "not" in printed and "comparable" in printed, (
            "the warning names the unresolved presets without saying their "
            "win and loss rates cannot be compared with the others. Issue "
            "#293.")

    def test_it_records_that_raising_the_day_cap_is_not_a_clean_control(self,
                                                                       capsys):
        """The measurement issue #293 asked for was run on 2026-08-05 and its
        result is that the experiment cannot answer the question. Dungeon power
        is keyed to ELAPSED DAYS at 22% per 100 days, so a run given more days
        is given a harder game, and the day cap is not an independent variable.

        Without this sentence the obvious next step -- raise max_days -- looks
        like a fix. It is not, and it costs about eight times the sweep's
        runtime to find that out again."""
        experiments.warn_about_unresolved_campaigns(["x"], {"x": 100.0}, 2500)
        printed = capsys.readouterr().out
        assert "not an" in printed and "independent variable" in printed, (
            "the warning does not record that raising the day cap changes the "
            "difficulty as well as the length of the run. Issue #293.")
        assert "22% per 100 days" in printed, (
            "the warning states that dungeon power is keyed to elapsed days "
            "without the rate. The rate is what decides whether the confound "
            "matters, and it is 0.22 per 100 days in config.py.")

    def test_the_stated_rate_matches_the_configured_one(self):
        """The warning quotes 22% per 100 days. If the constant changes, the
        sentence becomes false, and a false explanation is worse than none."""
        assert TuningConfig().dungeon_power_escalation_per_100_days == 0.22, (
            "dungeon_power_escalation_per_100_days is no longer 0.22, so the "
            "unresolved-campaign warning in experiments.py quotes the wrong "
            "rate. Update the printed sentence and this test together.")

    def test_the_threshold_is_a_majority_of_campaigns(self):
        """Below half, the resolved campaigns are still the larger sample and
        the rates mean something. At or above half they do not."""
        assert experiments.UNRESOLVED_WARNING_PERCENT == 50.0

    def test_a_cell_exactly_at_the_threshold_is_flagged(self, capsys):
        """Half the campaigns having no result is already too many, and an
        off-by-one here would silently drop the borderline cases the warning
        exists for."""
        assert experiments.warn_about_unresolved_campaigns(
            ["x"], {"x": experiments.UNRESOLVED_WARNING_PERCENT}, 2500) == ["x"]
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
        table and stop meaning anything."""
        base = replace(TuningConfig(), tier=experiments.SWEEP_TIER)
        experiments.exp_presets(base, tiers=(1,), trials=4)
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
