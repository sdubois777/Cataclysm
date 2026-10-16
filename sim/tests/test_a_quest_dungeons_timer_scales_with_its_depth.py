"""A Quest dungeon's timer is rolled from its own depth, as an ordinary dungeon's is.

THE RULE. The owner decided on 2026-10-16 that a Quest dungeon's timer scales
with its depth like other dungeons; the flat 25 days is retired. The formula is
the ordinary one on the Quest dungeon's own floors, with the same variation and
the empire tree's resolve bonus, rolled once when the dungeon is made: a
labelled judgement of the coordinating session under that decision.

A Quest dungeon's floor ranges are deeper than an ordinary one's on the same
tier (an Outpost's 20 to 30 against 8 to 15), so its timer is longer.

EVERY CASE HAS A CONTROL: an ordinary dungeon under the same rule, a shallower
Quest dungeon, or the same seed without the tree.
"""
from __future__ import annotations

import dataclasses
import pathlib

from cataclysm_sim.config import (
    CityTier,
    DungeonSpec,
    DungeonType,
    EmpireTree,
    TuningConfig,
)
from cataclysm_sim.engine import Simulation

NO_TREE = EmpireTree(name="no tree")
TEN_DAYS = EmpireTree(name="ten days longer", resolve_bonus_days=10.0)
SEEDS = range(40)


def simulation(seed: int, tree: EmpireTree = NO_TREE) -> Simulation:
    cfg = dataclasses.replace(
        TuningConfig(), surge_on_empty_board=False).with_tree(tree)
    sim = Simulation(cfg, seed=seed)
    sim.next_surge_day = float("inf")
    return sim


def a_city(sim: Simulation, tier: CityTier):
    for city in sim.empire.cities.values():
        if city.tier is tier and not city.fallen:
            return city
    raise AssertionError(f"no standing {tier.value}")


def bounds(cfg: TuningConfig, floors: int) -> tuple[int, int]:
    """The whole days the formula can give a dungeon of `floors` floors."""
    base = cfg.resolve_base_days + floors * cfg.resolve_floor_ratio
    bonus = cfg.tree.resolve_bonus_days
    return (int(base * (1.0 - cfg.resolve_jitter) + bonus),
            int(base * (1.0 + cfg.resolve_jitter) + bonus))


def test_a_quest_timer_is_the_ordinary_formula_on_its_own_depth() -> None:
    """On every tier a Quest dungeon has, and an ordinary one beside it."""
    checked = 0
    for seed in SEEDS:
        sim = simulation(seed)
        for tier in (CityTier.OUTPOST, CityTier.BULWARK, CityTier.SANCTUARY):
            city = a_city(sim, tier)
            for dtype in (DungeonType.QUEST, DungeonType.BASIC):
                d = sim._make_dungeon(dtype, city)
                lo, hi = bounds(sim.cfg, d.floors)
                assert lo <= d.resolve_max <= hi, (
                    f"seed {seed}, {tier.value}, {dtype.value}: {d.floors} floors "
                    f"gave a timer of {d.resolve_max} days, outside {lo} to {hi}")
                checked += 1
    assert checked == len(SEEDS) * 6


def test_a_deeper_quest_dungeon_waits_longer() -> None:
    """A Pillar's Quest dungeon (50 to 70 floors) always outwaits an Outpost's
    (20 to 30). Under the flat timer both were 25 days, which is the control."""
    outpost, pillar = [], []
    for seed in SEEDS:
        sim = simulation(seed)
        outpost.append(sim._make_dungeon(
            DungeonType.QUEST, a_city(sim, CityTier.OUTPOST)).resolve_max)
        pillar.append(sim._make_dungeon(
            DungeonType.QUEST, a_city(sim, CityTier.PILLAR)).resolve_max)

    assert min(pillar) > max(outpost), (
        f"a Pillar's Quest timers {min(pillar)} to {max(pillar)} overlap an "
        f"Outpost's {min(outpost)} to {max(outpost)}")
    assert len(set(outpost)) > 1, "every Outpost Quest timer is the same figure"


def test_the_trees_resolve_bonus_reaches_a_quest_timer() -> None:
    """The same seed with and without ten days of resolve bonus: the same draws,
    so the same depth and the same variation, and ten days between them."""
    for seed in SEEDS:
        plain = simulation(seed)
        bonus = simulation(seed, TEN_DAYS)
        a = plain._make_dungeon(DungeonType.QUEST, a_city(plain, CityTier.OUTPOST))
        b = bonus._make_dungeon(DungeonType.QUEST, a_city(bonus, CityTier.OUTPOST))
        assert a.floors == b.floors, "the tree changed the depth; it must not here"
        assert b.resolve_max - a.resolve_max == 10, (
            f"seed {seed}: {a.resolve_max} days without the bonus, "
            f"{b.resolve_max} with it")


def test_a_fallen_city_never_resolves() -> None:
    """Its timer is still past the end of any run."""
    sim = simulation(0)
    d = sim._make_dungeon(DungeonType.FALLEN_CITY, a_city(sim, CityTier.OUTPOST))
    assert d.resolve_max == int(sim.cfg.fallen_city_resolve_days) == 999


def test_no_dungeon_row_carries_a_timer_range_and_nothing_reads_one() -> None:
    """The field went on 2026-10-16 because nothing read it but the flat Quest
    timer and the Fallen City figure, which have their own homes now."""
    assert "resolve_days" not in {f.name for f in dataclasses.fields(DungeonSpec)}

    package = pathlib.Path(__file__).resolve().parents[1] / "cataclysm_sim"
    readers = [p.name for p in package.glob("*.py")
               if "resolve_days[" in p.read_text(encoding="utf-8")
               or "spec.resolve_days" in p.read_text(encoding="utf-8")]
    assert readers == [], f"a timer range is read again in {readers}"
