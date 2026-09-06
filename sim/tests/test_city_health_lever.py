"""The empire tree can raise how much damage a city absorbs, not only reduce it.

WHY THIS FILE EXISTS. Issue #1319. The Architect branch has seven nodes that raise
a city's health or defence — worth 5.90x for a Sanctuary next to the Pillar — and
the model had a field for none of them. `TIER_STATS.max_defense` was fixed per
tier and no tree field touched it, so the simulation measured only half of what
the defensive branch does. The project owner ruled on 2026-09-05, verbatim: "Add a
city-health lever to the model".

WHY IT IS A SEPARATE LEVER RATHER THAN A BIGGER `city_damage_mult`. Issue #1288
refused that fold, and this file is where the reason is checked rather than
asserted. Halving the damage and doubling the health agree on the first bite
against a full-health city and diverge immediately after, in two ways the model
already depends on:

  * the bite is `max_defense * bite * mult`, so raising `max_defense` raises the
    absolute size of every bite as well as the pool it comes from;
  * `_retake` restores a fraction of `max_defense`, which damage reduction does
    nothing for.

`TestTheTwoLeversAreNotInterchangeable` demonstrates both. If they were
interchangeable the fold would have been correct and this lever unnecessary.

WHAT IS NOT CHECKED HERE. Any balance figure. No test in this file asserts a win
rate, because the value of the preset moves whenever the model beneath it changes
and a test pinning it would fail on every legitimate change. The measurement lives
in the pull request and in `docs/DECISIONS.md`.
"""

from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_ARCHITECT_AS_DESIGNED, TREE_NONE, CityTier, DungeonType, EmpireTree,
    TuningConfig,
)
from cataclysm_sim.engine import Simulation
from cataclysm_sim.world import build_empire


def empire(tree: EmpireTree, **kwargs):
    return build_empire(replace(TuningConfig(), **kwargs).with_tree(tree))


def a_city(tree: EmpireTree, tier: CityTier):
    return next(c for c in empire(tree).cities.values() if c.tier is tier)


class TestTheLeverScalesWhatItSays:

    def test_no_tree_leaves_every_tier_at_its_base(self):
        cfg = TuningConfig()
        for tier in CityTier:
            assert a_city(TREE_NONE, tier).max_defense == pytest.approx(
                cfg.TIER_STATS[tier].max_defense)

    @pytest.mark.parametrize("mult", [0.5, 1.0, 2.0, 5.9, 10.0])
    @pytest.mark.parametrize("tier", list(CityTier))
    def test_it_multiplies_every_tier_by_what_it_says(self, mult, tier):
        tree = EmpireTree(name="t", city_health_mult=mult)
        assert a_city(tree, tier).max_defense == pytest.approx(
            TuningConfig().TIER_STATS[tier].max_defense * mult)

    def test_it_does_not_touch_population(self):
        """Four of the seven nodes say defence or health and not population, and
        `Imperial Decree` explicitly trades -10% population for its +20% health.
        Scaling population would invent an effect the tree does not have."""
        tree = EmpireTree(name="t", city_health_mult=5.9)
        for tier in CityTier:
            assert a_city(tree, tier).max_population == pytest.approx(
                TuningConfig().TIER_STATS[tier].max_population)

    def test_a_city_starts_the_run_at_its_raised_maximum(self):
        """`City.reset` fills defence to `max_defense`, so the raised pool has to
        be there before the reset rather than applied to the bite later."""
        city = a_city(EmpireTree(name="t", city_health_mult=4.0),
                      CityTier.SANCTUARY)
        assert city.defense == pytest.approx(city.max_defense)
        assert city.defense_frac == pytest.approx(1.0)


class TestTheTwoLeversAreNotInterchangeable:
    """The reason issue #1288 refused to fold this into `city_damage_mult`.

    If halving the damage and doubling the health were the same thing, the fold
    would have been right. These say where they part company.
    """

    def bite(self, tree: EmpireTree, times: int) -> float:
        """Defence left on a Sanctuary after `times` identical bites."""
        cfg = replace(TuningConfig(), tier=1).with_tree(tree)
        sim = Simulation(cfg, seed=0)
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.SANCTUARY)
        for _ in range(times):
            city.defense -= (city.max_defense * 0.08 * 1.0
                             * cfg.tree.city_damage_mult)
        return city.defense

    def test_they_do_not_even_agree_on_the_first_bite(self):
        """WRITTEN THE OTHER WAY ROUND FIRST, and the failure is the finding.

        I asserted the two levers leave the same share of the pool standing after
        one bite, on the reasoning that halving the damage and doubling the pool
        are the same thing at full health. They are not. Halving the multiplier
        halves the share removed; doubling `max_defense` doubles the pool AND the
        bite, so the share removed is unchanged. Issue #1327.
        """
        halved = EmpireTree(name="halved", city_damage_mult=0.5)
        doubled = EmpireTree(name="doubled", city_health_mult=2.0)
        left_halved = (self.bite(halved, 1)
                       / a_city(halved, CityTier.SANCTUARY).max_defense)
        left_doubled = (self.bite(doubled, 1)
                        / a_city(doubled, CityTier.SANCTUARY).max_defense)
        assert left_halved == pytest.approx(0.96), "8% bite, halved"
        assert left_doubled == pytest.approx(0.92), "8% bite, pool doubled"
        assert left_halved != pytest.approx(left_doubled)

    def test_they_disagree_on_how_much_damage_that_bite_did(self):
        """The absolute bite is a fraction of `max_defense`. Doubling the pool
        doubles the bite; halving the multiplier does not."""
        halved = self.bite(EmpireTree(name="halved", city_damage_mult=0.5), 1)
        doubled = self.bite(EmpireTree(name="doubled", city_health_mult=2.0), 1)
        base = TuningConfig().TIER_STATS[CityTier.SANCTUARY].max_defense
        assert base - halved == pytest.approx(base * 0.08 * 0.5)
        assert (base * 2.0) - doubled == pytest.approx(base * 2.0 * 0.08)
        assert (base - halved) != pytest.approx((base * 2.0) - doubled)

    def test_retaking_a_city_restores_a_fraction_of_the_raised_pool(self):
        """`_retake` gives back half of `max_defense`. A larger pool means a
        larger restore; damage reduction does nothing for a fallen city."""
        cfg = replace(TuningConfig(), tier=1).with_tree(
            EmpireTree(name="t", city_health_mult=3.0))
        sim = Simulation(cfg, seed=0)
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.SANCTUARY)
        base = TuningConfig().TIER_STATS[CityTier.SANCTUARY].max_defense
        city.defense = 0.0
        city.fallen = True
        sim._retake(city)
        assert city.defense == pytest.approx(base * 3.0 * 0.5)
        assert city.defense > base, (
            "a retaken city with the health nodes comes back stronger than an "
            "untouched city without them, which is the point of the lever")


class TestTheArchitectPresetCarriesIt:

    def test_the_preset_raises_city_health(self):
        assert TREE_ARCHITECT_AS_DESIGNED.city_health_mult > 1.0, (
            "the Architect preset no longer models its city-health nodes, so "
            "the sweep measures half of what that branch does. Issue #1319.")

    def test_the_value_is_the_six_nodes_that_apply_to_a_sanctuary(self):
        """1 + 1.5 + 1.2 + 1.0 + 0.5 + 0.5 + 0.2, summed as increases.

        NOT 6.54. That figure came from matching node descriptions for a
        percentage without reading which tier each applies to, and included
        `Fortified Gates`, which grants +64% but only "for Outposts". The preset's
        scenario is a Sanctuary.
        """
        assert TREE_ARCHITECT_AS_DESIGNED.city_health_mult == pytest.approx(5.90)

    def test_no_other_preset_claims_city_health(self):
        """Only the Architect branch has these nodes. A preset that gained the
        field by accident would quietly become far harder to kill."""
        from cataclysm_sim.config import TREE_PRESETS
        for tree in TREE_PRESETS:
            if tree is TREE_ARCHITECT_AS_DESIGNED:
                continue
            assert tree.city_health_mult == 1.0, (
                f"{tree.name} raises city health and has no node that grants it")

    def test_describe_mentions_it(self):
        """`describe` is what a report prints for a preset. A lever it does not
        mention is one a reader cannot see was set."""
        assert "city hp" in TREE_ARCHITECT_AS_DESIGNED.describe()


class TestWhetherItChangesAnything:
    """THE LEVER IS CURRENTLY INERT AND THESE SAY SO. Issue #1327.

    A dungeon bite is a fraction of the city's own `max_defense`, so raising
    that raises the bite by the same factor and the share removed per bite is
    unchanged. The lever built here is what issue #1319 specified and it
    cannot work until the project owner settles what a bite is a fraction of.

    THESE TESTS ARE EXPECTED TO FAIL WHEN THAT LANDS, and that is the point of
    them: they pin the current behaviour so the change that fixes it cannot be
    mistaken for a change that did nothing.
    """

    def bites_to_fall(self, mult: float) -> int:
        """How many identical bites a Sanctuary survives. THE MECHANISM, and it
        is arithmetic rather than a statistic."""
        cfg = replace(TuningConfig(), tier=1).with_tree(
            EmpireTree(name="t", city_health_mult=mult))
        city = a_city(cfg.tree, CityTier.SANCTUARY)
        bite = city.max_defense * 0.08 * 1.0 * cfg.tree.city_damage_mult
        left, count = city.max_defense, 0
        while left > 0:
            left -= bite
            count += 1
        return count

    @pytest.mark.parametrize("mult", [1.0, 2.0, 5.9, 100.0])
    def test_city_health_does_not_change_how_many_bites_a_city_survives(
            self, mult):
        """Issue #1327, measured. Thirteen at every multiplier, including a
        hundredfold one."""
        assert self.bites_to_fall(mult) == self.bites_to_fall(1.0) == 13

    def test_a_campaign_comparison_is_too_noisy_to_prove_this_either_way(self):
        """WHY THERE IS NO CAMPAIGN TEST HERE, and it is not an omission.

        This class replaced one that compared cities lost over 40 campaigns at
        1x against 6x and asserted the sturdier empire lost fewer. IT PASSED, on
        a change proved above to do nothing, because floating-point jitter at two
        magnitudes ordered the comparison by chance. Its docstring said it
        existed to catch a field that nothing reads.

        Over 300 campaigns the outcome is bit-identical at 2x and moves by less
        than noise at 100x. A campaign outcome cannot resolve this at any sample
        this suite can afford, so the mechanism is asserted instead.
        """
        cfg = replace(TuningConfig(), tier=1)
        inert = EmpireTree(name="inert", city_health_mult=2.0)
        base = [Simulation(cfg.with_tree(EmpireTree(name="base")),
                           seed=i).run(policies.triage).cities_lost
                for i in range(40)]
        raised = [Simulation(cfg.with_tree(inert),
                             seed=i).run(policies.triage).cities_lost
                  for i in range(40)]
        assert base == raised, (
            "doubling city health changed a campaign, so either issue #1327 has "
            "been fixed -- in which case this class needs rewriting -- or the "
            "bite is no longer a fraction of max_defense.")

    def test_a_dungeon_bite_still_scales_with_the_raised_pool(self):
        """The bite is a fraction of `max_defense`, so it grows with the lever.
        A test that only counted cities lost would pass if the pool were raised
        and the bite left at its old absolute size, which is the fold this
        deliberately is not."""
        cfg = replace(TuningConfig(), tier=1).with_tree(
            EmpireTree(name="t", city_health_mult=5.0))
        sim = Simulation(cfg, seed=0)
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.SANCTUARY)
        spec = cfg.spec(DungeonType.BASIC, CityTier.SANCTUARY)
        before = city.defense
        city.defense -= city.max_defense * spec.defense_bite
        base = TuningConfig().TIER_STATS[CityTier.SANCTUARY].max_defense
        assert before - city.defense == pytest.approx(
            base * 5.0 * spec.defense_bite)
