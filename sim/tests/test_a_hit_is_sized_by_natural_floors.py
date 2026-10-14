"""A dungeon's hit is sized by its natural floors, not by floors a tree adds.

THE RULE. On 2026-10-13 the owner accepted the coordinating session's
recommendation, in that session's words: a dungeon's hit is sized by its
NATURAL floors, not the floors the Explorer tree adds. Until then
`Simulation._resolve` scaled a hit by `floors / typical`, and `floors` included
what the empire tree added, so the Explorer preset's +40 floors at difficulty
tier 1 made every ordinary dungeon on an Outpost hit about 4.5 times as hard
(51.5 floors over 11.5 against 11.5 over 11.5).

WHAT NATURAL FLOORS ARE. The depth rolled from the dungeon's row, times the
Cataclysm pattern's `floors_mult`, before the tree's floors. The pattern's
multiplier is part of what the dungeon is; that is a labelled judgement.
`Dungeon.tree_floors` holds what the tree added and `Dungeon.natural_floors` is
`floors - tree_floors`.

A TREE THAT REMOVES FLOORS DOES NOT SHRINK THE HIT EITHER. That follows from
the same sentence and the ruling did not spell it out: `Exclusionary Mapping`
and any preset with a negative `floor_delta` make a dungeon shorter to walk and
poorer, and no longer make it hit less.

THE TOTAL FLOORS STILL DRIVE EVERYTHING ELSE: the walk, the resolve timer, the
power, the death chance and the reward.

`TuningConfig.hit_scales_with_natural_floors` False is the model as it was.

EVERY CASE HAS A CONTROL: the twin without the tree, or the same board with the
setting on the old scale.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim.config import (
    TREE_EXPLORER_AS_DESIGNED,
    CityTier,
    DungeonType,
    EmpireTree,
    TuningConfig,
)
from cataclysm_sim.engine import Dungeon, Simulation

SEED = 5

NATURAL = True
TOTAL = False

#: Trees that move floors and nothing else, so no other lever acts on a hit.
DEEPER = EmpireTree(name="forty more floors", floor_delta=20.0,
                    floor_delta_per_type=20.0)
SHALLOWER = EmpireTree(name="five fewer floors", floor_delta=-5.0)
NO_TREE = EmpireTree(name="no tree")


def simulation(scale: bool, tree: EmpireTree = NO_TREE,
               **overrides) -> Simulation:
    cfg = dataclasses.replace(
        TuningConfig(), hit_scales_with_natural_floors=scale,
        surge_on_empty_board=False, **overrides).with_tree(tree)
    sim = Simulation(cfg, seed=SEED)
    sim.next_surge_day = float("inf")
    return sim


def an_outpost(sim: Simulation):
    for city in sim.empire.cities.values():
        if city.tier is CityTier.OUTPOST and not city.fallen:
            return city
    raise AssertionError("no standing Outpost")


def one_hit(scale: bool, tree: EmpireTree,
            dtype: DungeonType = DungeonType.BASIC, **overrides):
    """One dungeon made on the same Outpost from the same seed, resolved once.

    Returns the dungeon and the defence it took.
    """
    sim = simulation(scale, tree, **overrides)
    city = an_outpost(sim)
    d = sim._make_dungeon(dtype, city)
    before = city.defense
    sim._resolve(d)
    return d, before - city.defense


def nobody_decides(sim, dungeons):
    return None


class TestTheDefault:
    def test_a_hit_is_sized_by_natural_floors_unless_somebody_says_total(self):
        assert TuningConfig().hit_scales_with_natural_floors is True


class TestWhatNaturalFloorsAre:
    def test_the_tree_adds_to_the_total_and_not_to_the_natural(self):
        plain, _ = one_hit(NATURAL, NO_TREE)
        deeper, _ = one_hit(NATURAL, DEEPER)

        lo, hi = TuningConfig().spec(DungeonType.BASIC,
                                     CityTier.OUTPOST).floors
        assert lo <= plain.floors <= hi
        assert plain.tree_floors == 0
        assert plain.natural_floors == plain.floors

        assert deeper.natural_floors == plain.natural_floors, (
            "the same seed rolled a different natural depth with a tree that "
            "only adds floors")
        assert deeper.tree_floors == 40
        assert deeper.floors == plain.floors + 40

    def test_a_tree_that_removes_floors_leaves_the_natural_alone(self):
        plain, _ = one_hit(NATURAL, NO_TREE)
        shallower, _ = one_hit(NATURAL, SHALLOWER)

        assert shallower.floors == plain.floors - 5
        assert shallower.tree_floors == -5
        assert shallower.natural_floors == plain.natural_floors

    def test_the_cataclysm_patterns_multiplier_is_part_of_the_natural(self):
        """A labelled judgement: the pattern is what the dungeon is."""
        sims = [simulation(NATURAL, DEEPER) for _ in range(2)]
        once = sims[0]._make_dungeon(DungeonType.BASIC, an_outpost(sims[0]))
        doubled = sims[1]._make_dungeon(
            DungeonType.BASIC, an_outpost(sims[1]), floors_mult=2.0)

        assert doubled.natural_floors == 2 * once.natural_floors
        assert doubled.tree_floors == once.tree_floors == 40

    def test_a_dungeon_built_by_hand_has_no_tree_floors(self):
        fields = {f.name: f for f in dataclasses.fields(Dungeon)}
        assert fields["tree_floors"].default == 0

    def test_floors_set_by_hand_move_the_natural_with_them(self):
        """`natural_floors` is `floors - tree_floors`, so a test or a later
        rule that changes a dungeon's depth after it was made changes what it
        hits with, and the tree's part stays out."""
        sim = simulation(NATURAL, DEEPER)
        d = sim._make_dungeon(DungeonType.BASIC, an_outpost(sim))
        d.floors += 7
        assert d.natural_floors == d.floors - 40


class TestTheHit:
    def test_a_dungeon_with_tree_added_floors_hits_as_its_twin_without(self):
        plain, taken_plain = one_hit(NATURAL, NO_TREE)
        deeper, taken_deeper = one_hit(NATURAL, DEEPER)

        assert deeper.floors > plain.floors
        assert taken_plain > 0
        assert taken_deeper == pytest.approx(taken_plain), (
            f"a dungeon of {deeper.floors} floors, {deeper.tree_floors} of "
            f"them the tree's, took {taken_deeper} defence and its twin of "
            f"{plain.floors} floors took {taken_plain}")

    def test_on_the_old_scale_the_tree_added_floors_made_it_hit_harder(self):
        """**THE CONTROL.** The model as it was before 2026-10-13."""
        plain, taken_plain = one_hit(TOTAL, NO_TREE)
        deeper, taken_deeper = one_hit(TOTAL, DEEPER)

        assert taken_deeper == pytest.approx(
            taken_plain * deeper.floors / plain.floors)
        assert taken_deeper > 2 * taken_plain

    def test_a_dungeon_with_floors_removed_hits_as_its_twin_too(self):
        """The consequence the ruling's words imply and did not spell out."""
        plain, taken_plain = one_hit(NATURAL, NO_TREE)
        shallower, taken_shallower = one_hit(NATURAL, SHALLOWER)

        assert shallower.floors < plain.floors
        assert taken_shallower == pytest.approx(taken_plain)

    def test_on_the_old_scale_removing_floors_shrank_the_hit(self):
        """**THE CONTROL.**"""
        plain, taken_plain = one_hit(TOTAL, NO_TREE)
        shallower, taken_shallower = one_hit(TOTAL, SHALLOWER)

        assert taken_shallower == pytest.approx(
            taken_plain * shallower.floors / plain.floors)
        assert taken_shallower < taken_plain

    def test_a_deeper_natural_dungeon_still_hits_harder(self):
        """The scale is still there: it reads the natural depth."""
        taken = []
        for floors in (8, 15):
            sim = simulation(NATURAL, DEEPER)
            city = an_outpost(sim)
            d = sim._make_dungeon(DungeonType.BASIC, city)
            d.floors = floors + d.tree_floors
            before = city.defense
            sim._resolve(d)
            taken.append(before - city.defense)

        assert taken[1] == pytest.approx(taken[0] * 15 / 8)

    def test_the_other_reading_of_a_quest_hit_uses_natural_floors_too(self):
        _, plain = one_hit(NATURAL, NO_TREE, DungeonType.QUEST,
                           quest_hit_scales_with_depth=True)
        _, deeper = one_hit(NATURAL, DEEPER, DungeonType.QUEST,
                            quest_hit_scales_with_depth=True)
        _, old = one_hit(TOTAL, DEEPER, DungeonType.QUEST,
                         quest_hit_scales_with_depth=True)

        assert deeper == pytest.approx(plain)
        assert old > deeper


class TestTheTotalFloorsStillDriveTheRest:
    def twins(self):
        out = []
        for tree in (NO_TREE, DEEPER):
            sim = simulation(NATURAL, tree)
            d = sim._make_dungeon(DungeonType.BASIC, an_outpost(sim))
            out.append((sim, d))
        return out

    def test_the_walk_is_the_total_floors(self):
        (plain_sim, plain), (deeper_sim, deeper) = self.twins()

        assert plain.subtype == deeper.subtype
        assert deeper.run_days == deeper_sim._walk_days(deeper.floors,
                                                         deeper.subtype)
        assert deeper.run_days > plain.run_days

    def test_the_timer_is_rolled_from_the_total_floors(self):
        (_, plain), (_, deeper) = self.twins()

        ratio = TuningConfig().resolve_floor_ratio
        assert ratio > 0
        assert deeper.resolve_max > plain.resolve_max, (
            "forty more floors did not lengthen the resolve timer. The timer "
            "still scales with depth, and depth is the total floors")

    def test_the_power_and_the_death_chance_read_the_total_floors(self):
        (plain_sim, plain), (deeper_sim, deeper) = self.twins()

        assert deeper_sim.dungeon_power(deeper) != plain_sim.dungeon_power(plain)
        same_depth = dataclasses.replace(deeper, floors=plain.floors)
        assert (deeper_sim.dungeon_power(same_depth)
                == pytest.approx(plain_sim.dungeon_power(plain)))


class TestOverAWholeCampaign:
    def campaign(self, scale: bool, tree: EmpireTree):
        cfg = dataclasses.replace(
            TuningConfig(), hit_scales_with_natural_floors=scale,
            max_days=600).with_tree(tree)
        sim = Simulation(cfg, seed=SEED)
        result = sim.run(nobody_decides)
        health = {cid: (c.defense, c.population, c.fallen)
                  for cid, c in sim.empire.cities.items()}
        return result, health, sim.rng.getstate()

    def test_with_no_tree_nothing_changes_bit_for_bit(self):
        assert self.campaign(NATURAL, NO_TREE) == self.campaign(TOTAL, NO_TREE)

    def test_with_the_explorer_preset_the_two_scales_part(self):
        """**THE CONTROL**: the setting does reach a campaign."""
        natural = self.campaign(NATURAL, TREE_EXPLORER_AS_DESIGNED)
        total = self.campaign(TOTAL, TREE_EXPLORER_AS_DESIGNED)

        assert natural[1] != total[1]

    def test_with_the_explorer_preset_fewer_cities_are_lost(self):
        """Nobody enters anything, 600 days, one seed: a comparison of two
        runs and not a measured rate."""
        natural, _, _ = self.campaign(NATURAL, TREE_EXPLORER_AS_DESIGNED)
        total, _, _ = self.campaign(TOTAL, TREE_EXPLORER_AS_DESIGNED)

        assert natural.cities_lost < total.cities_lost
