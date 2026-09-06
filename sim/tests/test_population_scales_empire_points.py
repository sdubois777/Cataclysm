"""The population an empire keeps alive scales what a defeated dungeon is worth.

WHY THIS FILE EXISTS. The project owner ruled it on 2026-09-06, issue #1348:

    points = base_points[dungeon_type] x (living_population / total_maximum)

evaluated at the instant each dungeon is defeated, linear, with no floor and no
cap, on every dungeon type. Before that ruling the award was flat per type and
nothing scaled it. The rule had been stated in `docs/Cataclysm_GDD_v2.md` and
implemented nowhere, which is the easiest kind of rule to lose.

WHAT EACH TEST HAS TO AVOID, AND IT IS THE REASON THESE ARE ARITHMETIC AND NOT
CAMPAIGN COMPARISONS. A test whose expected value is computed the way the code
computes it cannot fail. `population_frac()` is the thing under test, so no test
here may call it to build its own expectation. Every expected number below is
either a literal or is built from `max_population`, which the multiplier's
numerator never touches. Issue #1327 is the cautionary tale: a behaviour test
comparing forty campaigns at two settings passed on a change that did nothing,
because jitter put the comparison the right way round.

THE COUPLING THAT MUST NOT BE LOST. The absence of a floor is only safe because
the multiplier is sampled per defeat rather than once at the end of a run.
Population starts at maximum and cities fall late, so the per-defeat sample is
far kinder: measured over 500 campaigns with no defensive tree and a policy that
abandons cities deliberately, the worst run still kept 51% of the flat award,
against 0.25 for the end-of-run form. `TestItIsSampledWhenTheDungeonIsDefeated`
is what stops that timing being changed quietly, because moving the award to the
end of the run would remove a floor nobody would notice was there.

WHAT IS DELIBERATELY NOT TESTED HERE. That the GAME implements any of this. The
C++ awards no empire points yet; this is the model only. The design document and
the decisions log are checked by
`tools/tests/test_cataclysm_order_and_population_experience_are_stated.py`.
"""

from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_NONE, DungeonType, EmpireTree, TuningConfig,
)
from cataclysm_sim.engine import Simulation


def sim_for(tree: EmpireTree = TREE_NONE, tier: int = 1,
            seed: int = 0) -> Simulation:
    sim = Simulation(replace(TuningConfig(), tier=tier).with_tree(tree),
                     seed=seed)
    # The clear must not be able to roll a death. Death has nothing to do with
    # the award and an unlucky roll would return before it, so the failure
    # would name the wrong thing.
    sim.death_chance = lambda d: 0.0        # type: ignore[method-assign]
    return sim


def set_population_fraction(sim: Simulation, frac: float) -> None:
    """Put EVERY city at the same share of its own maximum.

    Doing it per city is what makes the empire-wide fraction exactly `frac`
    without computing a ratio: sum(frac * max_i) / sum(max_i) is frac for any
    set of maxima. So the test's expectation is a literal, not a repeat of the
    code's own arithmetic.
    """
    for city in sim.empire.cities.values():
        city.population = city.max_population * frac
    sim.empire._invalidate()


def a_result():
    """One real `RunResult`, to be modified with `dataclasses.replace`.

    Building one by hand would mean listing every field, and the list would go
    stale the next time one is added.
    """
    return Simulation(replace(TuningConfig(), tier=1).with_tree(TREE_NONE),
                      seed=0).run(policies.triage)


def clear_one(sim: Simulation, dtype: DungeonType) -> float:
    """Defeat one dungeon of that type and return the points it awarded.

    The Cataclysm boss only ever sits on the Pillar -- it is the only
    (type, tier) pair `TuningConfig.DUNGEON_SPECS` defines for it -- so the
    host city is chosen by type rather than taken as the first one going.
    """
    if dtype is DungeonType.CATACLYSM:
        city = sim.empire.cities[sim.empire.pillar_id]
    else:
        city = next(c for c in sim.empire.cities.values()
                    if c.alive and c.cid != sim.empire.pillar_id)
    d = sim._make_dungeon(dtype, city)
    sim.current = d
    before = sim.empire_points
    sim._finish_current()
    return sim.empire_points - before


class TestTheAwardScalesWithLivingPopulation:
    """The ruling's formula, checked as arithmetic."""

    @pytest.mark.parametrize("frac", [1.0, 0.75, 0.5, 0.25, 0.1])
    def test_a_basic_dungeon_pays_its_base_times_the_fraction(self, frac):
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]
        set_population_fraction(sim, frac)

        awarded = clear_one(sim, DungeonType.BASIC)

        assert awarded == pytest.approx(base * frac), (
            f"defeating a Basic dungeon at {frac:.0%} population awarded "
            f"{awarded} empire points, not {base * frac}. The owner ruled on "
            "2026-09-06 that the award is the base points times the living "
            "population over the total maximum. Issue #1348.")

    @pytest.mark.parametrize("dtype", list(DungeonType))
    def test_every_dungeon_type_is_scaled(self, dtype):
        """"Every dungeon type", with no exceptions, is part of the ruling. A
        Cataclysm is worth 25 against a Basic's 1 and is cleared last when the
        population is lowest, so it is the type the multiplier bites hardest
        and the most tempting one to except."""
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[dtype]
        set_population_fraction(sim, 0.5)

        awarded = clear_one(sim, dtype)

        assert awarded == pytest.approx(base * 0.5), (
            f"defeating a {dtype.name} dungeon at half population awarded "
            f"{awarded}, not {base * 0.5}. The owner ruled that the population "
            "multiplier applies to every dungeon type, with no exceptions. "
            "Issue #1348.")

    def test_a_full_empire_pays_the_full_base(self):
        """The multiplier must be 1.0 at full population and not merely close
        to it. A fraction that never quite reaches 1 would tax every run."""
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]

        awarded = clear_one(sim, DungeonType.BASIC)

        assert awarded == pytest.approx(base), (
            f"an untouched empire awarded {awarded} for a Basic dungeon rather "
            f"than the full {base}. At full population the multiplier is 1.0. "
            "Issue #1348.")

    def test_it_is_linear_rather_than_curved(self):
        """The ruling says linear, no curve. Halving the population must halve
        the award exactly, at every level -- which a square, a square root or
        any soft curve would fail."""
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]

        for frac in (0.8, 0.4, 0.2):
            set_population_fraction(sim, frac)
            full = clear_one(sim, DungeonType.BASIC)
            set_population_fraction(sim, frac / 2.0)
            half = clear_one(sim, DungeonType.BASIC)

            assert half == pytest.approx(full / 2.0), (
                f"at {frac:.0%} population a Basic dungeon paid {full} and at "
                f"{frac / 2:.0%} it paid {half}, which is not half. The owner "
                "ruled the shape is linear with no curve. Issue #1348.")
            assert full == pytest.approx(base * frac)


class TestThereIsNoFloorAndNoCap:
    """"No floor and no cap", ruled 2026-09-06.

    The floor is the one most likely to be added later in good faith, because
    a run that earns nothing looks broken. It is not: the per-defeat timing
    means a real campaign never reaches this state, and the measured worst of
    500 campaigns kept 51% of the flat award. Every floor above zero hands the
    strategy of abandoning the empire its advantage back.
    """

    def test_a_dead_empire_awards_exactly_nothing(self):
        sim = sim_for()
        set_population_fraction(sim, 0.0)

        awarded = clear_one(sim, DungeonType.CATACLYSM)

        assert awarded == 0.0, (
            f"an empire with no living population awarded {awarded} empire "
            "points for a Cataclysm. The owner ruled there is no floor, so a "
            "wiped-out empire pays nothing. A floor added here would be paid "
            "for out of the reason the rule exists. Issue #1348.")

    def test_a_tiny_surviving_population_still_pays_proportionally(self):
        """A floor would show up as a lower bound the award never goes under.
        1% of the base is far below any floor anyone would choose."""
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]
        set_population_fraction(sim, 0.01)

        awarded = clear_one(sim, DungeonType.BASIC)

        assert awarded == pytest.approx(base * 0.01), (
            f"at 1% population a Basic dungeon paid {awarded} rather than "
            f"{base * 0.01}. Anything larger means a floor has been added, "
            "which the owner ruled against. Issue #1348.")

    def test_the_multiplier_is_not_capped_below_one(self):
        """"No cap" cuts both ways. The failure worth guarding is a clamp that
        keeps a full empire below its full award."""
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.CATACLYSM]

        awarded = clear_one(sim, DungeonType.CATACLYSM)

        assert awarded == pytest.approx(base), (
            f"a Cataclysm at full population paid {awarded} rather than the "
            f"full {base}. Issue #1348.")


class TestTheDenominatorCountsEveryCityFallenOrNot:
    """The answer to the question the issue thought was open, and it was not:
    the game had already decided it explicitly.

    `UCataclysmEmpireMap::TotalMaxPopulation` in
    `game/Source/CataclysmEmpire/Empire/CataclysmEmpireMap.cpp` carries the
    reason in a comment -- "EVERY CITY, FALLEN OR NOT. This is what the empire
    would hold intact ... it must not shrink as cities are lost." This is the
    guard most likely to be broken by a later refactor, because a denominator
    that follows the living cities looks tidier and reports a destroyed empire
    as perfectly intact.
    """

    def test_losing_a_city_lowers_the_award_by_that_citys_whole_share(self):
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]
        total_max = sum(c.max_population for c in sim.empire.cities.values())

        victim = next(c for c in sim.empire.cities.values()
                      if c.cid != sim.empire.pillar_id and not c.doomed)
        expected = 1.0 - victim.max_population / total_max
        sim._fall(victim)

        awarded = clear_one(sim, DungeonType.BASIC)

        assert awarded == pytest.approx(base * expected), (
            f"after one city fell, a Basic dungeon paid {awarded} rather than "
            f"{base * expected}. A fallen city must leave the numerator and "
            "stay in the denominator, so the award falls by that city's whole "
            "share of the empire's maximum. Issue #1348.")

    def test_the_denominator_does_not_shrink_when_a_city_falls(self):
        """Stated directly, because it is the half a refactor would break
        while leaving the numerator correct."""
        sim = sim_for()
        before = sim.empire.max_population()

        victim = next(c for c in sim.empire.cities.values()
                      if c.cid != sim.empire.pillar_id)
        sim._fall(victim)

        assert sim.empire.max_population() == before, (
            "the empire's maximum population changed when a city fell. It is "
            "what the empire would hold intact and must not shrink as cities "
            "are lost, or a wiped-out empire would report as fully populated. "
            "Issue #1348.")

    def test_an_erased_city_is_still_counted_in_the_denominator(self):
        """The Void erases a city rather than merely felling it. `erased` is a
        flag and the city stays in the map, so nothing special is needed -- but
        an "erased cities are gone, so drop them" change would be an easy one
        to make and would rebase the fraction."""
        sim = sim_for()
        before = sim.empire.max_population()

        victim = next(c for c in sim.empire.cities.values()
                      if c.cid != sim.empire.pillar_id)
        victim.doomed = True
        sim._fall(victim)
        assert victim.erased, "the test did not reach the erasure path"

        assert sim.empire.max_population() == before, (
            "an erased city left the empire's maximum population. Erasure is "
            "the Void taking a city permanently; it still counts toward what "
            "the empire would hold intact. Issue #1348.")

    def test_a_fallen_city_contributes_nothing_to_the_numerator(self):
        """Not "whatever population survived it" -- one of the readings the
        design document listed as open before the ruling."""
        sim = sim_for()
        victim = next(c for c in sim.empire.cities.values()
                      if c.cid != sim.empire.pillar_id)
        victim.population = victim.max_population      # full, then felled
        living_before = sim.empire.total_population()

        sim._fall(victim)

        assert sim.empire.total_population() == pytest.approx(
            living_before - victim.max_population), (
            "a fallen city still contributed population to the empire's living "
            "total. A fallen city contributes nothing to the numerator. "
            "Issue #1348.")


class TestItIsSampledWhenTheDungeonIsDefeated:
    """THE COUPLING. "Evaluated at the instant each dungeon is defeated", not
    once at the end of a run -- and the no-floor decision depends on it.

    If the award were computed at the end of a run, every clear in a campaign
    would be paid at the final population, the worst case would fall from 0.51
    to 0.25 and a floor would be needed. These tests fail if the timing moves,
    which is the only warning anyone would get that the floor had gone with it.
    """

    def test_two_clears_at_different_populations_pay_different_amounts(self):
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]

        first = clear_one(sim, DungeonType.BASIC)
        set_population_fraction(sim, 0.25)
        second = clear_one(sim, DungeonType.BASIC)

        assert first == pytest.approx(base), (
            f"the first clear, at full population, paid {first} rather than "
            f"{base}.")
        assert second == pytest.approx(base * 0.25), (
            f"the second clear, after the population fell to a quarter, paid "
            f"{second} rather than {base * 0.25}. Each award is fixed at the "
            "instant its dungeon is defeated. Issue #1348.")

    def test_a_later_collapse_does_not_reduce_what_was_already_paid(self):
        """The end-of-run form's signature. Under it, the empire collapsing
        after a clear would retroactively devalue that clear."""
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]

        clear_one(sim, DungeonType.BASIC)
        banked = sim.empire_points
        assert banked == pytest.approx(base)

        set_population_fraction(sim, 0.0)

        assert sim.empire_points == pytest.approx(banked), (
            f"the empire points already earned changed from {banked} to "
            f"{sim.empire_points} when the population collapsed afterwards. "
            "Points are awarded and fixed at each defeat; a later loss cannot "
            "reach back. Issue #1348.")

    def test_a_real_campaign_keeps_far_more_than_its_final_population(self):
        """The measured consequence of the timing, on a whole campaign rather
        than a constructed empire: because cities fall late, the multiplier a
        run actually experiences is much higher than the population it ends
        with. That gap IS the floor, and it is why no explicit one was added.
        """
        r = Simulation(replace(TuningConfig(), tier=1).with_tree(TREE_NONE),
                       seed=7).run(policies.triage)

        assert r.dungeons_cleared > 0, "the campaign cleared nothing to measure"
        assert r.empire_points_multiplier > r.final_population_frac, (
            f"the multiplier this run experienced ({r.empire_points_multiplier:.3f}) "
            f"was not above the population it ended with "
            f"({r.final_population_frac:.3f}). Awarding per defeat is what "
            "makes it higher; if these are equal the award is being computed "
            "at the end of the run. Issue #1348.")


class TestTheRunRecordsTheMultiplierItExperienced:
    """Without this a scaled award over many clears and a flat award over few
    are indistinguishable in a sweep's output, which is the failure issue #1327
    documents. Both totals come from the same campaign, so comparing them needs
    no second batch.
    """

    def test_the_flat_total_is_the_award_before_the_multiplier(self):
        sim = sim_for()
        base = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]
        set_population_fraction(sim, 0.5)

        clear_one(sim, DungeonType.BASIC)

        assert sim.empire_points_flat == pytest.approx(base), (
            f"the unscaled total recorded {sim.empire_points_flat} rather than "
            f"{base}. It must record what the clear would have paid with no "
            "population multiplier at all. Issue #1348.")
        assert sim.empire_points == pytest.approx(base * 0.5)

    def test_the_multiplier_is_weighted_by_what_each_clear_was_worth(self):
        """A plain mean of the per-clear fractions would flatter a run, because
        the cheap clears come early when the population is high and the
        25-point Cataclysm comes last when it is low."""
        sim = sim_for()
        basic = sim.cfg.empire_points_per_dungeon[DungeonType.BASIC]
        boss = sim.cfg.empire_points_per_dungeon[DungeonType.CATACLYSM]

        clear_one(sim, DungeonType.BASIC)              # at 1.0
        set_population_fraction(sim, 0.2)
        clear_one(sim, DungeonType.CATACLYSM)          # at 0.2

        scaled = basic * 1.0 + boss * 0.2
        flat = basic + boss
        assert sim.empire_points == pytest.approx(scaled)
        assert sim.empire_points_flat == pytest.approx(flat)

        # The two totals above are what the property divides, so read the
        # property off a result carrying them and compare it with the number
        # this test worked out by hand.
        r = replace(a_result(), empire_points=sim.empire_points,
                    empire_points_flat=sim.empire_points_flat)

        # Value-weighted this is 0.2308, because the 25-point clear happened at
        # 0.2. The unweighted mean of 1.0 and 0.2 would be 0.6, and that gap is
        # what makes the distinction worth a test rather than a comment.
        assert r.empire_points_multiplier == pytest.approx(0.230769, abs=1e-5), (
            f"the run reported a multiplier of {r.empire_points_multiplier} "
            "after a 1-point clear at full population and a 25-point clear at "
            "a fifth of it. Weighted by what each clear was worth it is "
            "0.2308; 0.6 would mean the clears were being averaged equally. "
            "Issue #1348.")

    def test_a_run_that_cleared_nothing_reports_a_multiplier_of_one(self):
        """Not zero, and not a division by zero: no award was scaled, so no
        multiplier was experienced."""
        r = replace(a_result(), empire_points=0.0, empire_points_flat=0.0)

        assert r.empire_points_multiplier == 1.0, (
            f"a run that cleared nothing reported a multiplier of "
            f"{r.empire_points_multiplier}. No award was scaled, so the "
            "multiplier it experienced is 1.0 rather than 0.0. Issue #1348.")

    def test_the_campaign_result_carries_both_totals(self):
        r = Simulation(replace(TuningConfig(), tier=1).with_tree(TREE_NONE),
                       seed=3).run(policies.triage)

        assert r.empire_points_flat >= r.empire_points, (
            "the scaled empire points exceeded the flat ones. The multiplier "
            "cannot be above 1.0, because the living population cannot exceed "
            "the maximum. Issue #1348.")
        assert r.empire_points_multiplier == pytest.approx(
            r.empire_points / r.empire_points_flat)
