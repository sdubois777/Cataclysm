"""The Ascendant, Celestial's Quest dungeon, approximated: one more average modifier a floor, down to four.

Its design is that each floor adds one more active Edict, up to 4. The model has
no Edicts, so the plan the coordinating session accepted on 2026-10-16 stands one
average modifier's score in for each, floor by floor (`TuningConfig.
ascendant_extra_modifiers_cap`). Its Core, its Edicts growing stronger, and the
Edict it leaves on its city are not modelled.

EVERY CASE HAS A CONTROL: the extra turned off, or War's Quest dungeon.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import combat
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(cataclysm: str = "Celestial", **over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.active_types = (cataclysm,)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def quest(sim: Simulation, cataclysm: str):
    city = next(c for c in sim.empire.exposed_cities() if c.tier is CityTier.OUTPOST)
    sim.making_for = cataclysm
    try:
        d = sim._make_dungeon(DungeonType.QUEST, city)
    finally:
        sim.making_for = ""
    return d


def test_the_number_is_the_plans() -> None:
    assert TuningConfig().ascendant_extra_modifiers_cap == 4


def test_one_more_modifier_a_floor_down_to_four() -> None:
    got = [combat.floor_modifier_score(10.0, f, 3.0, 4) for f in (1, 2, 3, 4, 5, 30)]
    assert got == [13.0, 16.0, 19.0, 22.0, 22.0, 22.0]
    assert combat.floor_modifier_score(10.0, 30, 3.0, 0) == 10.0, "CONTROL: no cap, no extra"
    assert combat.floor_modifier_score(10.0, 30) == 10.0, "CONTROL: the default adds nothing"


def death_chances(sim: Simulation, d) -> list[float]:
    out = []
    for power in range(0, 4001, 50):
        sim.power = float(power)
        out.append(sim.death_chance(d))
    return out


def test_an_ascendant_is_more_dangerous_than_the_same_dungeon_without_it() -> None:
    """Across a sweep of player power, never safer and at some power strictly
    more dangerous. CONTROL: with the extra turned off it is the same dungeon."""
    sim = simulation()
    d = quest(sim, "Celestial")
    assert d.subtype == "Ascendant"
    with_it = death_chances(sim, d)
    off = simulation(ascendant_extra_modifiers_cap=0)
    off.modifier_pool = sim.modifier_pool
    without = death_chances(off, d)
    assert all(a >= b for a, b in zip(with_it, without, strict=True))
    assert any(a > b for a, b in zip(with_it, without, strict=True))


def test_another_quest_dungeon_is_unchanged() -> None:
    """CONTROL: War's Quest dungeon, a Siege, gets no extra."""
    sim = simulation("War")
    d = quest(sim, "War")
    assert d.subtype == "Siege"
    off = simulation("War", ascendant_extra_modifiers_cap=0)
    off.modifier_pool = sim.modifier_pool
    assert death_chances(sim, d) == death_chances(off, d)


def test_the_extra_is_the_average_modifier_of_the_campaigns_pool() -> None:
    sim = simulation()
    pool = sim.modifier_pool
    assert pool, "the Celestial campaign has modifiers to draw from"
    assert sim.average_modifier_score() == sum(s for _, s in pool) / len(pool)
    sim.modifier_pool = []
    assert sim.average_modifier_score() == 0.0
