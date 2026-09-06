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


def until_it_falls(tree: EmpireTree, tier: CityTier = CityTier.SANCTUARY):
    """Resolve one typical Basic dungeon on a city until it falls.

    DRIVES `Simulation._resolve` ITSELF, and that is not fussiness. The version
    of this file written before issue #1327 copied `max_defense * bite * scale *
    mult` into the test body. When the model stopped using that expression the
    copy went on returning 13 and the test went on passing, measuring a formula
    nothing ran any more.

    Returns `(resolves, defence pool, share of the population destroyed)`.
    """
    cfg = replace(TuningConfig(), tier=1).with_tree(tree)
    sim = Simulation(cfg, seed=0)
    city = next(c for c in sim.empire.cities.values() if c.tier is tier)
    d = sim._make_dungeon(DungeonType.BASIC, city)

    # PINNED TO THE MIDPOINT so the depth scale is exactly 1.0. Otherwise the
    # count measures which depth the seed happened to roll.
    lo, hi = cfg.spec(DungeonType.BASIC, tier).floors
    d.floors = (lo + hi) // 2

    started_with = city.population
    resolves = 0
    while not city.fallen and resolves < 10_000:
        sim._resolve(d)
        resolves += 1
    return resolves, city.max_defense, 1.0 - city.population / started_with


class TestTheLeverChangesHowLongACityLasts:
    """WHAT #1319 ASKED FOR AND #1327 UNBLOCKED. These are the tests that
    failed until the damage stopped being a fraction of the pool it came out
    of, and they are the reason the fix cannot be mistaken for a no-op."""

    def test_a_city_with_no_tree_falls_in_thirteen(self):
        """The control. This number is unchanged by issue #1327, and it is what
        makes the rows below a comparison rather than a rescale."""
        assert until_it_falls(EmpireTree(name="none"))[0] == 13

    @pytest.mark.parametrize("mult, expected", [(2.0, 26), (4.0, 51),
                                                (5.9, 75), (100.0, 1270)])
    def test_raising_city_health_raises_how_long_a_city_lasts(
            self, mult, expected):
        """PROPORTIONAL, WHICH IS THE WHOLE POINT. A 5.9x Sanctuary takes 75
        resolves against 13. Before #1327 every one of these was 13."""
        resolves, pool, _ = until_it_falls(
            EmpireTree(name="t", city_health_mult=mult))
        assert resolves == expected
        assert pool == pytest.approx(8_000 * mult)
        assert resolves > 13, (
            "city health bought nothing, so the damage is a fraction of the "
            "pool again and issue #1327 has regressed")


class TestTheTwoLeversAreStillNotOneLever:
    """Issue #1288 refused to fold this into `city_damage_mult`. THE REASON IT
    GAVE HAS BEEN OVERTAKEN AND THE CONCLUSION HAS NOT.

    Under the fraction, halving the damage and doubling the pool differed
    because doubling the pool doubled the bite too. Under flat damage they do
    NOT differ that way: both exactly double how long the city stands, and the
    first test below says so rather than hiding it.

    They part company on two other things, and both are situations rather than
    numbers -- which is the test this project applies to any two levers that
    look alike.
    """

    def test_for_how_long_a_city_stands_they_are_now_interchangeable(self):
        """SAID PLAINLY BECAUSE IT IS THE UNCOMFORTABLE HALF. Anyone re-reading
        #1288 should find this rather than discover it."""
        halved = until_it_falls(EmpireTree(name="d", city_damage_mult=0.5))
        doubled = until_it_falls(EmpireTree(name="h", city_health_mult=2.0))
        assert halved[0] == doubled[0] == 26

    def test_but_only_damage_reduction_saves_the_PEOPLE(self):
        """THE SITUATION EACH ONE OWNS. `city_health_mult` scales defence and
        deliberately not population, so a city with the health nodes and no
        damage reduction stands twice as long and is emptied of people doing
        it. Halving the damage halves what the population loses too.

        This is new with flat damage. While the damage was a fraction, both
        pools drained at the same relative rate whatever the pool sizes.
        """
        _, _, halved_lost = until_it_falls(
            EmpireTree(name="d", city_damage_mult=0.5))
        _, _, doubled_lost = until_it_falls(
            EmpireTree(name="h", city_health_mult=2.0))
        assert halved_lost == pytest.approx(0.512, abs=0.01)
        assert doubled_lost == pytest.approx(1.0), (
            "a city that survives twice as long takes twice as many hits on a "
            "population pool the health nodes do not raise")

    def test_and_only_city_health_helps_a_city_that_already_fell(self):
        """`_retake` restores a fraction of `max_defense`, so a larger pool
        means a larger restore. Damage reduction does nothing for a city that
        has already fallen."""
        base = TuningConfig().TIER_STATS[CityTier.SANCTUARY].max_defense
        back = {}
        for label, tree in (("damage", EmpireTree(name="d", city_damage_mult=0.5)),
                            ("health", EmpireTree(name="h", city_health_mult=2.0))):
            cfg = replace(TuningConfig(), tier=1).with_tree(tree)
            sim = Simulation(cfg, seed=0)
            city = next(c for c in sim.empire.cities.values()
                        if c.tier is CityTier.SANCTUARY)
            city.defense = 0.0
            city.fallen = True
            sim._retake(city)
            back[label] = city.defense
        assert back["damage"] == pytest.approx(base * 0.5)
        assert back["health"] == pytest.approx(base * 2.0 * 0.5)
        assert back["health"] == pytest.approx(2.0 * back["damage"])


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


class TestTheDamageDoesNotGrowWithThePool:
    """THE MECHANISM THAT MAKES THE LEVER WORK, asserted directly rather than
    through a campaign. Issue #1327.

    A campaign outcome cannot settle this at any sample this suite can afford.
    The class these replaced learned that the expensive way: a behaviour test
    written specifically to catch a field nothing reads compared 40 campaigns
    at 1x against 6x, asserted the sturdier empire lost fewer cities, and
    PASSED on a change that provably did nothing, because floating-point jitter
    at two magnitudes ordered the comparison by chance.
    """

    def test_the_spec_holds_points_and_not_a_fraction(self):
        """A fraction would be a number below one. If these ever become
        fractions again, everything else in this file goes quiet rather than
        failing loudly, so the shape is asserted on its own."""
        cfg = TuningConfig()
        for tier in CityTier:
            spec = cfg.spec(DungeonType.BASIC, tier)
            assert spec.defense_damage > 1.0, (
                f"{tier.value} takes {spec.defense_damage} defence damage, "
                "which is a fraction rather than a number of points")
            assert spec.population_damage > 1.0

    def one_resolve(self, mult: float) -> float:
        """Defence points a single typical resolve removes from a Bulwark.

        A BULWARK BECAUSE ITS FLOOR RANGE HAS A WHOLE MIDPOINT. 15 to 25 gives
        a typical depth of exactly 20, so the depth scale is exactly 1.0 and
        the number below is the spec's own. A Sanctuary's 25 to 40 has a
        midpoint of 32.5, which no integer floor count can sit on.
        """
        cfg = replace(TuningConfig(), tier=1).with_tree(
            EmpireTree(name="t", city_health_mult=mult))
        sim = Simulation(cfg, seed=0)
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.BULWARK)
        d = sim._make_dungeon(DungeonType.BASIC, city)
        lo, hi = cfg.spec(DungeonType.BASIC, CityTier.BULWARK).floors
        d.floors = (lo + hi) // 2
        assert d.floors * 2 == lo + hi, "the Bulwark midpoint stopped being whole"

        before = city.defense
        sim._resolve(d)
        return before - city.defense

    @pytest.mark.parametrize("mult", [1.0, 2.0, 5.9, 100.0])
    def test_raising_the_pool_does_not_raise_the_damage(self, mult):
        """The exact thing that was wrong. One resolve against a city with a
        hundred times the defence removes the same number of points as one
        against an untouched city."""
        assert self.one_resolve(mult) == pytest.approx(
            TuningConfig().spec(DungeonType.BASIC,
                                CityTier.BULWARK).defense_damage)
        assert self.one_resolve(mult) == pytest.approx(self.one_resolve(1.0))

    def test_a_campaign_notices(self):
        """The behaviour check that the earlier version of this file could not
        make. Doubling city health changes campaigns now; it did not before."""
        cfg = replace(TuningConfig(), tier=1)
        base = [Simulation(cfg.with_tree(EmpireTree(name="base")),
                           seed=i).run(policies.triage).cities_lost
                for i in range(40)]
        raised = [Simulation(cfg.with_tree(
            EmpireTree(name="raised", city_health_mult=6.0)),
            seed=i).run(policies.triage).cities_lost
            for i in range(40)]
        assert sum(raised) < sum(base), (
            f"six times the city health lost {sum(raised)} cities against "
            f"{sum(base)} without it, so the lever is not reaching the day loop")
