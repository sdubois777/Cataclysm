"""The Hollow, the Void's Quest dungeon: it loses a floor a day to half its depth, and its hit adds void stacks.

The plan the coordinating session accepted on 2026-10-16: from the day after it
is made it loses one floor a day, down to half its natural floors rounded up; its
walk and reward shrink with it and its timer does not; its hit adds 1 stack at
full depth rising evenly to 3 at its smallest, and no single hit erases a city
below 8 stacks. The owner, 2026-10-16: "So, maybe Hollow works, but we shouldn't
just flat out erase a city if they player can't get to it in time."

EVERY CASE HAS A CONTROL: an ordinary Void dungeon, or War's Quest dungeon.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(**over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1, surge_on_city_fall=False, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.active_types = ("Void",)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def outposts(sim: Simulation):
    return [c for c in sim.empire.exposed_cities() if c.tier is CityTier.OUTPOST]


def made_for(sim: Simulation, cataclysm: str, dtype: DungeonType, city):
    sim.making_for = cataclysm
    try:
        d = sim._make_dungeon(dtype, city)
    finally:
        sim.making_for = ""
    d.source = cataclysm
    return d


def hollow(sim: Simulation, city, depth: int = 20):
    d = made_for(sim, "Void", DungeonType.QUEST, city)
    assert d.subtype == "Hollow"
    d.floors, d.tree_floors, d.hollow_from = depth, 0, depth
    return d


def test_the_numbers_are_the_plans() -> None:
    cfg = TuningConfig()
    assert (cfg.hollow_floors_lost_per_day, cfg.hollow_floor_share_kept,
            cfg.hollow_hit_cannot_erase_below) == (1, 0.5, 8)


def test_a_hollow_remembers_the_depth_it_was_made_with() -> None:
    sim = simulation()
    city = outposts(sim)[0]
    d = made_for(sim, "Void", DungeonType.QUEST, city)
    assert d.subtype == "Hollow" and d.hollow_from == d.natural_floors > 0
    war = made_for(sim, "War", DungeonType.QUEST, outposts(sim)[1])
    assert war.hollow_from == 0, "CONTROL: War's Quest dungeon is no Hollow"


def test_it_loses_a_floor_a_day_from_the_day_after_down_to_half() -> None:
    """Made on day 0 with 21 floors: it keeps 21 on day 0, has 20 on day 1, and
    stops at 11 (half of 21, rounded up). Its timer does not move; its walk
    shrinks. CONTROL: an ordinary Void dungeon keeps its floors."""
    sim = simulation()
    a, b = outposts(sim)[:2]
    d = hollow(sim, a, 21)
    d.spawned_day = sim.day = 0
    ordinary = made_for(sim, "Void", DungeonType.BASIC, b)
    ordinary.spawned_day = 0
    keep = ordinary.floors
    timer, walk = d.resolve_max, d.run_days

    sim._hollow_shrink()
    assert d.floors == 21, "not on the day it was made"
    floors = []
    for day in range(1, 16):
        sim.day = day
        sim._hollow_shrink()
        floors.append(d.floors)
    assert floors[:3] == [20, 19, 18]
    assert min(floors) == 11 and floors[-1] == 11
    assert d.resolve_max == timer
    assert d.run_days < walk
    assert ordinary.floors == keep


def test_the_one_the_player_is_inside_does_not_shrink() -> None:
    sim = simulation()
    d = hollow(sim, outposts(sim)[0])
    d.spawned_day, sim.day = 0, 3
    sim.current = d
    sim._hollow_shrink()
    assert d.floors == 20
    sim.current = None
    sim._hollow_shrink()
    assert d.floors == 19, "CONTROL: once the player is out it shrinks"


def test_its_hit_adds_1_at_full_depth_rising_evenly_to_3_at_its_smallest() -> None:
    sim = simulation()
    d = hollow(sim, outposts(sim)[0], 20)
    got = {}
    for lost in (0, 2, 3, 5, 7, 8, 10):
        d.floors = 20 - lost
        got[lost] = sim._hollow_stacks_for(d)
    assert got == {0: 1, 2: 1, 3: 2, 5: 2, 7: 2, 8: 3, 10: 3}, got


def test_its_hit_adds_stacks_and_does_no_damage() -> None:
    """CONTROL: War's Quest dungeon's hit on another Outpost takes defence."""
    sim = simulation()
    a, b = outposts(sim)[:2]
    d = hollow(sim, a)
    war = made_for(sim, "War", DungeonType.QUEST, b)
    before = a.defense, a.population, b.defense
    sim._quest_hit(d, a)
    sim._quest_hit(war, b)
    assert a.void_stacks == 1 and (a.defense, a.population) == before[:2]
    assert b.void_stacks == 0 and b.defense < before[2]


def test_one_hit_does_not_erase_a_city_below_eight_stacks() -> None:
    """At its smallest a Hollow adds 3: from 7 stacks that would be 10, and it
    stops at 9. CONTROL: from 8 it reaches 11 and the city is erased, and the
    Hollow moves on."""
    sim = simulation()
    a = outposts(sim)[0]
    d = hollow(sim, a, 20)
    d.floors = 10
    a.void_stacks = 7
    sim._quest_hit(d, a)
    assert a.void_stacks == 9 and not a.fallen

    sim = simulation()
    a = outposts(sim)[0]
    d = hollow(sim, a, 20)
    d.floors = 10
    a.void_stacks = 8
    sim._quest_hit(d, a)
    assert a.void_stacks == 11 and a.fallen and a.erased
    assert d.city_id != a.cid, "a Quest dungeon moves on when its city falls"


def test_the_policies_see_the_hit_that_would_erase_the_city() -> None:
    """CONTROL: the same Hollow on a city at 7 stacks cannot erase it."""
    sim = simulation()
    a = outposts(sim)[0]
    d = hollow(sim, a, 20)
    d.floors = 10
    a.void_stacks = 8
    assert policies._quest_hit_fells(sim, d, a)
    a.void_stacks = 7
    assert not policies._quest_hit_fells(sim, d, a)


def test_with_the_stacks_off_a_hollow_hits_as_any_quest_dungeon() -> None:
    sim = simulation(void_dungeons_add_stacks=False)
    a = outposts(sim)[0]
    d = hollow(sim, a)
    before = a.defense
    sim._quest_hit(d, a)
    assert a.defense < before and a.void_stacks == 0
