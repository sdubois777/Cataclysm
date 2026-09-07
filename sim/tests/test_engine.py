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
from cataclysm_sim.config import CityTier, EmpireTree, SurgeMode, TuningConfig
from cataclysm_sim.engine import DungeonType, Simulation


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

    def test_a_surge_never_brings_more_than_the_cap_however_much_is_asked(self):
        """`surge_count_max` bounds the BASE count, not only escalation growth.

        THE OWNER RULED THIS STAYS -- verbatim, "Leave it, document it", on
        2026-09-06 -- so it is a fixed rule and belongs here rather than being
        rediscovered. **It is silent**: asking for 40 dungeons a surge returns
        14 and warns about nothing, so a sweep whose count axis runs past the
        cap measures one cell repeatedly and reports it as a trend. That is
        what happened on issue #1090, where a grid of 4, 5, 10, 20, 30 and 40
        was really a grid of 4, 5, 10, 14, 14 and 14.

        Both halves are asserted: that the cap binds above 14, and that it does
        NOT bind at or below it, so a change that clamped everything to some
        other value would fail here rather than looking like this rule.
        """
        cap = TuningConfig().surge_count_max
        assert cap == 14, (
            f"surge_count_max is {cap}, not the 14 the owner ruled on "
            "2026-09-06 and that CataclysmSurge.h's MostDungeonsPerSurge "
            "carries. If it moved deliberately, sim/README.md and "
            "sim/analyse_surge_cadence.py both state 14 and need updating.")

        for asked in (1, 4, 5, 10, cap):
            cfg = dataclasses.replace(TuningConfig(), surge_dungeon_count=asked,
                          surge_mode=SurgeMode.STATIC)
            assert Simulation(cfg, seed=0).surge_count() == asked, (
                f"a count of {asked} is at or below the cap and must be "
                "delivered unchanged")

        for asked in (cap + 1, 20, 30, 40):
            cfg = dataclasses.replace(TuningConfig(), surge_dungeon_count=asked,
                          surge_mode=SurgeMode.STATIC)
            assert Simulation(cfg, seed=0).surge_count() == cap, (
                f"a count of {asked} no longer clamps to {cap}. If the cap has "
                "stopped applying to the base count, every figure measured "
                "against it -- and the warning in sim/README.md -- is stale.")

        # Raising the cap alongside the count is the only way past it, and it
        # is what sim/analyse_surge_cadence.py does for its own batches.
        lifted = dataclasses.replace(TuningConfig(), surge_dungeon_count=40,
                         surge_count_max=40, surge_mode=SurgeMode.STATIC)
        assert Simulation(lifted, seed=0).surge_count() == 40


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
class TestTheBossGrowsWithOrdinaryDungeonsCleared:
    """The Cataclysm boss dungeon gains a floor per ordinary dungeon cleared.

    THE DESIGN DOCUMENT: "Every dungeon defeated adds one floor to the
    Cataclysm boss dungeon." Nothing added any until issue #1315, so a campaign
    that cleared thirty dungeons met the same boss as one that cleared five.

    ONLY ORDINARY DUNGEONS COUNT, which the owner settled on 2026-09-06. Half
    the rule is about what does NOT count, so half these tests are about that:
    a Quest dungeon is the win condition itself and retaking a fallen city is
    recovery, so neither deepens the fight.
    """

    def _safe(self, **overrides):
        """A config where clearing a dungeon cannot kill you.

        `_finish_current` rolls for death before it counts anything, so a test
        that wants to count clears has to stop the roll landing. This zeroes the
        per-floor risk rather than choosing seeds that happen to survive.
        """
        return dataclasses.replace(TuningConfig(), per_floor_risk=0.0,
                                   boss_risk_multiplier=0.0, **overrides)

    def _clear(self, sim, dtype, city_id=None):
        """Clear one dungeon of the given kind, through the ordinary path."""
        city = (sim.empire.cities[city_id] if city_id is not None
                else sim.empire.cities[sim.empire.pillar_id])
        d = sim._make_dungeon(dtype, city)
        sim.current = d
        sim._finish_current()
        return d

    def test_the_boss_is_deeper_than_its_roll_when_dungeons_were_cleared(
            self, meet_the_unlock_requirement):
        cfg = self._safe()
        spec = cfg.spec(DungeonType.CATACLYSM, CityTier.PILLAR)
        deepest_roll = spec.floors[1]

        sim = Simulation(cfg, seed=4)
        sim.basic_cleared = 40
        meet_the_unlock_requirement(sim)
        sim._maybe_open_cataclysm()

        assert sim.cataclysm is not None
        assert sim.cataclysm.floors > deepest_roll, (
            "the boss is no deeper than the deepest it could roll, so the "
            "forty dungeons cleared added nothing")

    def test_it_is_one_floor_for_each_ordinary_dungeon(
            self, meet_the_unlock_requirement):
        """The exact figure, not merely that it grew.

        Two runs at the same seed roll the same boss, so the difference between
        them is the growth and nothing else.
        """
        cfg = self._safe()

        def depth(cleared):
            sim = Simulation(cfg, seed=11)
            sim.basic_cleared = cleared
            meet_the_unlock_requirement(sim)
            sim._maybe_open_cataclysm()
            return sim.cataclysm.floors

        assert depth(30) - depth(0) == 30
        assert depth(7) - depth(0) == 7

    def test_the_constant_is_what_decides_how_much(
            self, meet_the_unlock_requirement):
        """Turning it off reproduces the game as it was before #1315."""
        def depth(per_dungeon):
            cfg = self._safe(cataclysm_floors_per_dungeon_cleared=per_dungeon)
            sim = Simulation(cfg, seed=11)
            sim.basic_cleared = 20
            meet_the_unlock_requirement(sim)
            sim._maybe_open_cataclysm()
            return sim.cataclysm.floors

        assert depth(1) - depth(0) == 20
        assert depth(3) - depth(0) == 60

    def test_clearing_quest_and_fallen_city_dungeons_does_not_deepen_it(
            self, meet_the_unlock_requirement):
        """**The half of the rule that is about what does not count.**

        The owner ruled that a Quest dungeon is the win condition itself and
        that retaking a fallen city is recovery rather than progress, so neither
        makes the final fight harder. A test that only checked ordinary dungeons
        deepen the boss would pass on an implementation that counted everything.

        IT CLEARS THEM THROUGH `_finish_current`, the path a real campaign
        takes, rather than setting the counter by hand -- otherwise it would be
        testing the counter rather than what moves it.
        """
        cfg = self._safe()
        sim = Simulation(cfg, seed=5)

        rim = next(c for c in sim.empire.cities.values()
                   if c.tier is CityTier.OUTPOST)

        for _ in range(4):
            self._clear(sim, DungeonType.QUEST, rim.cid)
        for _ in range(3):
            self._clear(sim, DungeonType.FALLEN_CITY, rim.cid)

        # THE CONTROL. Those clears did happen and were counted as clears; it is
        # only the boss's growth they must not touch.
        assert sim.cleared == 7
        assert sim.objectives == 4

        assert sim.basic_cleared == 0, (
            "Quest or Fallen City clears moved the counter that deepens the "
            "boss; the owner ruled only ordinary dungeons count")

        meet_the_unlock_requirement(sim)
        sim._maybe_open_cataclysm()

        # AGAINST THE ROLL'S OWN RANGE, NOT AGAINST ANOTHER RUN. Two sims that
        # built different numbers of dungeons have drawn different numbers of
        # times, so their bosses roll to different depths -- 108 against 139 in
        # the first version of this test, which had nothing to do with growth.
        # The range a boss can roll to is fixed, so being inside it is a
        # statement about growth alone.
        spec = cfg.spec(DungeonType.CATACLYSM, CityTier.PILLAR)

        assert sim.cataclysm_floors_earned == 0
        assert sim.cataclysm.floors <= spec.floors[1], (
            f"the boss is {sim.cataclysm.floors} floors, past the "
            f"{spec.floors[1]} it can roll to, so clearing Quest and Fallen "
            "City dungeons deepened it")

        # AND THE SAME MEASUREMENT SHOWS ORDINARY DUNGEONS DO. Without this the
        # check above would pass on an implementation that grew nothing at all.
        grown = Simulation(cfg, seed=5)
        for _ in range(60):
            self._clear(grown, DungeonType.BASIC, rim.cid)
        meet_the_unlock_requirement(grown)
        grown._maybe_open_cataclysm()

        assert grown.cataclysm.floors > spec.floors[1]

    def test_clearing_ordinary_dungeons_through_the_same_path_does_deepen_it(self):
        """The positive half, taken the same way, so the two are comparable."""
        cfg = self._safe()
        sim = Simulation(cfg, seed=5)

        rim = next(c for c in sim.empire.cities.values()
                   if c.tier is CityTier.OUTPOST)

        for _ in range(6):
            self._clear(sim, DungeonType.BASIC, rim.cid)

        assert sim.basic_cleared == 6

    def test_the_last_stand_takes_none_of_the_growth(self):
        """The owner: "No -- the last stand replaces it".

        That fight is won 1 time in 54 by deliberate design and is built from
        its own bonuses. Adding earned growth on top would have made a chosen
        number worse by accident.
        """
        cfg = self._safe()

        def depth(cleared):
            sim = Simulation(cfg, seed=9)
            sim.basic_cleared = cleared
            sim._open_last_stand()
            return sim.last_stand.floors

        assert depth(50) == depth(0), (
            "the last stand grew with dungeons cleared; it takes its own "
            "bonuses only")

    def test_the_walk_gets_longer_with_the_extra_floors(
            self, meet_the_unlock_requirement):
        """Depth and time are the same axis at the starting rate, so a deeper
        boss is a longer commitment. A grown boss whose walk was still worked
        out from its rolled depth would be free floors."""
        cfg = self._safe()

        def walk(cleared):
            sim = Simulation(cfg, seed=11)
            sim.basic_cleared = cleared
            meet_the_unlock_requirement(sim)
            sim._maybe_open_cataclysm()
            return sim.cataclysm.run_days, sim.cataclysm.floors

        short_days, short_floors = walk(0)
        long_days, long_floors = walk(40)

        assert long_floors == short_floors + 40
        assert long_days > short_days

    def test_the_earned_boss_recomputes_its_walk_through_the_subtype_helper(
            self, meet_the_unlock_requirement):
        """It has to go through `_walk_days` rather than `run_days_for`.

        **THIS TEST USED TO BE ABOUT A COW LEVEL BOSS AND CANNOT BE ANY MORE.**
        It searched 400 seeds for an earned Cataclysm dungeon that rolled Cow
        Level and checked its doubled walk survived the growth. The owner ruled
        on 2026-09-06 that a Cataclysm dungeon may not roll Cow Level at all --
        issue #1333 -- so that search now finds nothing and the old test would
        have failed on its own control, which is the honest outcome and not the
        one to leave in place. `test_cataclysm_cannot_be_a_cow_level.py` holds
        the rule that replaced it.

        WHAT IS LEFT IS STILL WORTH CHECKING. The reason the boss goes through
        `_walk_days` is that a caller which changes a dungeon's depth after it
        is built has to work the days out again, and doing that with a bare
        `run_days_for` is what dropped the doubling in the first place. The
        helper is the guard against a future dungeon kind that CAN be a Cow
        Level and CAN grow; a boss wired straight to `run_days_for` would lose
        it again silently.
        """
        cfg = self._safe()
        sim = Simulation(cfg, seed=11)
        sim.basic_cleared = 25
        meet_the_unlock_requirement(sim)
        sim._maybe_open_cataclysm()
        boss = sim.cataclysm

        # THE CONTROL. The growth really happened, so the days below were
        # recomputed rather than left at what the roll built.
        assert boss.floors > 25
        assert boss.run_days == sim._walk_days(boss.floors, boss.subtype)

        # AND THE HELPER STILL CARRIES BOTH HALVES OF THE COW LEVEL RULE, asked
        # directly, because no Cataclysm dungeon can reach that branch any more.
        assert sim._walk_days(50, "Cow Level") == 100
        assert (sim._walk_days(50, "Cow Level")
                > sim._walk_days(50, "Timed"))

    def test_the_result_reports_the_boss_it_earned(self):
        """The report has to carry it or nothing can measure what this changed.

        A run that never opens one reports zero rather than omitting the field,
        so a mean over campaigns has to decide what to do with those rather than
        silently skipping them.
        """
        cfg = self._safe()
        result = Simulation(cfg, seed=2).run(policies.triage)

        assert result.cataclysm_floors >= 0
        assert result.cataclysm_floors_earned >= 0
        if result.cataclysm_floors == 0:
            assert result.cataclysm_floors_earned == 0
