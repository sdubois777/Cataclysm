"""Tests for the empire-layer day loop.

These are fast guards, not a balance check. Balance lives in experiments.py,
which runs about 25,000 campaigns and takes roughly 18 minutes -- far too slow
for continuous integration. What is checked here is that the loop runs, that it
is deterministic, and that the design rules the README calls fixed are actually
enforced by the code.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import EmpireTree, SurgeMode, TuningConfig
from cataclysm_sim.engine import Simulation


def run(cfg=None, policy=None, seed=0):
    return Simulation(cfg or TuningConfig(), seed=seed).run(policy or policies.triage)


class TestRunsAtAll:
    @pytest.mark.parametrize("name", list(policies.ALL))
    def test_every_policy_completes_a_campaign(self, name):
        result = run(policy=policies.ALL[name], seed=3)
        assert result.survived_days > 0

    def test_result_exposes_the_fields_experiments_reports_on(self):
        result = run()
        names = {f.name for f in dataclasses.fields(result)}
        required = {"survived_days", "won", "lost", "cities_lost", "objectives",
                    "floors_cleared", "power", "crafts", "craft_days", "deaths",
                    "surges", "final_surge_gap", "final_surge_count",
                    "idle_days", "free_days"}
        assert required <= names

    def test_outcome_is_exactly_one_of_won_lost_or_stalemate(self):
        for seed in range(12):
            result = run(seed=seed)
            assert not (result.won and result.lost)


class TestDeterminism:
    """Every sweep in experiments.py assumes a seed reproduces a campaign."""

    def test_same_seed_gives_the_same_result(self):
        a, b = run(seed=42), run(seed=42)
        assert dataclasses.asdict(a) == dataclasses.asdict(b)

    def test_different_seeds_give_different_campaigns(self):
        results = {run(seed=s).survived_days for s in range(15)}
        assert len(results) > 1


class TestFixedDesignRules:
    """Rules sim/README.md states are fixed by design rather than swept."""

    def test_one_floor_costs_one_day_by_default(self):
        sim = Simulation(TuningConfig(), seed=0)
        for floors in (1, 15, 30, 60, 113):
            assert sim.run_days_for(floors) == floors

    def test_run_days_never_drop_below_one(self):
        cfg = TuningConfig().with_tree(EmpireTree(name="huge", run_days_flat=500))
        sim = Simulation(cfg, seed=0)
        for floors in (1, 15, 60):
            assert sim.run_days_for(floors) >= 1

    def test_flat_day_reduction_shortens_runs(self):
        plain = Simulation(TuningConfig(), seed=0).run_days_for(30)
        cfg = TuningConfig().with_tree(EmpireTree(name="fast", run_days_flat=10))
        reduced = Simulation(cfg, seed=0).run_days_for(30)
        assert reduced < plain

    def test_more_floors_cost_more_days(self):
        sim = Simulation(TuningConfig(), seed=0)
        days = [sim.run_days_for(f) for f in (10, 30, 60, 120)]
        assert days == sorted(days)
        assert days[0] < days[-1]

    def test_floor_delta_changes_how_deep_dungeons_are(self):
        """floor_delta changes a dungeon's depth, not the floors-to-days formula.

        run_days_for() takes an already-effective floor count, so the tree's
        floor_delta has to show up in the depth of spawned dungeons. Measured as
        floors per cleared dungeon: campaign totals do not work here, because
        deeper dungeons end campaigns sooner and so lower the total.
        """
        def mean_depth(delta):
            cfg = TuningConfig().with_tree(EmpireTree(name=f"d{delta}",
                                                      floor_delta=delta))
            runs = [Simulation(cfg, seed=s).run(policies.triage) for s in range(8)]
            floors = sum(r.floors_cleared for r in runs)
            cleared = sum(r.dungeons_cleared for r in runs)
            return floors / max(1, cleared)

        assert mean_depth(-25) < mean_depth(0) < mean_depth(30)


class TestAccounting:
    """Totals the report divides by must stay internally consistent."""

    @pytest.mark.parametrize("seed", range(6))
    def test_free_days_never_exceed_days_survived(self, seed):
        result = run(seed=seed)
        assert 0 <= result.free_days <= result.survived_days

    @pytest.mark.parametrize("seed", range(6))
    def test_idle_days_never_exceed_free_days(self, seed):
        result = run(seed=seed)
        assert 0 <= result.idle_days <= result.free_days

    @pytest.mark.parametrize("seed", range(6))
    def test_counters_are_never_negative(self, seed):
        result = run(seed=seed)
        for field in ("cities_lost", "floors_cleared", "crafts", "craft_days",
                      "deaths", "surges", "objectives"):
            assert getattr(result, field) >= 0, field

    def test_never_craft_policy_spends_no_days_at_the_forge(self):
        for seed in range(6):
            result = run(policy=policies.never_craft, seed=seed)
            assert result.crafts == 0
            assert result.craft_days == 0


class TestSurgeModes:
    @pytest.mark.parametrize("mode", list(SurgeMode))
    def test_every_surge_mode_runs(self, mode):
        cfg = dataclasses.replace(TuningConfig(), surge_mode=mode)
        assert run(cfg=cfg, seed=5).survived_days > 0

    def test_more_cataclysms_is_not_easier(self):
        """Stacking active Cataclysms should not raise the win rate."""
        def win_rate(count):
            cfg = dataclasses.replace(TuningConfig(), active_cataclysms=count)
            wins = sum(1 for s in range(40)
                       if Simulation(cfg, seed=s).run(policies.triage).won)
            return wins / 40

        assert win_rate(8) <= win_rate(1) + 0.05
