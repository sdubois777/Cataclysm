"""A Quest dungeon is not absorbed when its city falls: it moves.

THE OWNER, 2026-10-16, VERBATIM: "Quest dungeons don't go away when a city falls.
They just move to an adjacent city. So eventually they should be hitting
something. Other than that they work the same as any other dungeon but provide
quest progress and should probably be themed towards whatever the quest is for."

RULED THE SAME DAY by the coordinating session: the relocation's own filter (an
exposed city that has not fallen and is not the Pillar, and not a besieged city
for a dungeon carrying a Siege); the move on a fall is certain; an adjacent city
first, else the nearest qualifying city counted in lanes, ties by one draw; if
none qualifies anywhere it stays. A Quest hit that fells its own host moves it
and refreshes its timer, with no second move that day; on any other fall it
keeps its remaining timer. The walked Quest dungeon moves and the walk goes on.
Ordinary dungeons are absorbed as before.

EVERY CASE HAS A CONTROL: an ordinary dungeon on the same city, or the same
board without the condition the case is about.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def board() -> Simulation:
    cfg = replace(TuningConfig(), tier=1, surge_on_city_fall=False,
                  surge_on_empty_board=False).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=3)
    sim.next_surge_day = float("inf")
    sim.dungeons.clear()
    return sim


def a_rim_outpost(sim: Simulation):
    for c in sim.empire.cities.values():
        if c.tier is CityTier.OUTPOST and not c.fallen and sim.empire.adjacent_exposed_cities(c):
            return c
    raise AssertionError("no standing Outpost with an exposed neighbour")


def test_it_moves_to_an_adjacent_exposed_city_and_keeps_what_it_is() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    quest = sim._make_dungeon(DungeonType.QUEST, city)
    quest.subtype = "Timed"
    quest.resolve_in = 7.0
    ordinary = sim._make_dungeon(DungeonType.BASIC, city)
    floors, timer, left = quest.floors, quest.resolve_max, quest.resolve_in

    sim._fall(city)

    assert quest.did in sim.dungeons, "the Quest dungeon was absorbed by the fall"
    moved_to = sim.empire.cities[quest.city_id]
    assert moved_to.cid != city.cid
    assert moved_to.cid in sim.empire.neighbours(city), "it did not move to a neighbour"
    assert sim.empire.is_exposed(moved_to) and moved_to.cid != sim.empire.pillar_id
    assert (quest.floors, quest.resolve_max, quest.resolve_in, quest.subtype) == \
        (floors, timer, left, "Timed"), "a move keeps the floors, the timer, what is left of it and the sub-type"
    assert sim.quest_moves_on_a_fall == 1
    assert ordinary.did not in sim.dungeons, "CONTROL: the ordinary dungeon on the same city is absorbed"


def test_with_no_neighbour_left_it_goes_to_the_nearest_exposed_city_by_lanes() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    for k in sim.empire.neighbours(city):
        sim.empire.cities[k].fallen = True
    sim.empire._invalidate()
    assert not sim.empire.adjacent_exposed_cities(city), "set-up: no neighbour qualifies"
    quest = sim._make_dungeon(DungeonType.QUEST, city)

    sim._fall(city)

    assert quest.did in sim.dungeons
    moved_to = sim.empire.cities[quest.city_id]
    assert moved_to.cid not in sim.empire.neighbours(city) and moved_to.cid != city.cid
    assert sim.empire.is_exposed(moved_to) and moved_to.cid != sim.empire.pillar_id
    # THE NEAREST: no qualifying city is closer in lanes than the one chosen.
    distance = {city.cid: 0}
    frontier = [city.cid]
    while frontier:
        ring = []
        for k in frontier:
            for j in sim.empire.neighbours(sim.empire.cities[k]):
                if j not in distance:
                    distance[j] = distance[k] + 1
                    ring.append(j)
        frontier = ring
    nearest = min(distance[c.cid] for c in sim.empire.cities.values()
                  if sim.empire.is_exposed(c) and c.cid != sim.empire.pillar_id)
    assert distance[moved_to.cid] == nearest


def test_with_nowhere_left_at_all_it_stays_on_the_fallen_city() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    for c in sim.empire.cities.values():
        if c.cid not in (city.cid, sim.empire.pillar_id):
            c.fallen = True
    sim.empire._invalidate()
    quest = sim._make_dungeon(DungeonType.QUEST, city)

    sim._fall(city)

    assert quest.did in sim.dungeons and quest.city_id == city.cid
    assert sim.quest_moves_on_a_fall == 0


def test_a_quest_dungeon_carrying_a_siege_does_not_move_onto_a_besieged_city() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    # EVERY NEIGHBOUR BUT THE PILLAR, read before the fall: the fall itself
    # exposes the inward ones, so they are targets once it has happened.
    near = [sim.empire.cities[k] for k in sim.empire.neighbours(city)
            if k != sim.empire.pillar_id]
    free = near[-1]
    for c in near[:-1]:
        sim._make_dungeon(DungeonType.BASIC, c).subtype = "Siege"
    quest = sim._make_dungeon(DungeonType.QUEST, city)
    quest.subtype = "Siege"

    sim._fall(city)

    assert quest.city_id == free.cid, (
        f"it went to {quest.city_id}; the only neighbour without a Siege is {free.cid}")


def test_a_quest_hit_that_fells_its_own_host_moves_it_and_refreshes_its_timer() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    city.defense = 1.0
    quest = sim._make_dungeon(DungeonType.QUEST, city)
    quest.resolve_in = 0.0

    sim._resolve(quest)

    assert city.fallen, "set-up: the Quest hit felled its host"
    assert quest.did in sim.dungeons, "it was absorbed by the fall its own hit caused"
    assert quest.city_id in sim.empire.neighbours(city)
    assert quest.resolve_in == float(quest.resolve_max), "its timer refreshes: it has just hit"
    assert sim.quest_moves_on_a_fall == 1


def test_control_a_quest_hit_that_does_not_fell_its_host_does_not_move_it_by_a_fall() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    quest = sim._make_dungeon(DungeonType.QUEST, city)
    quest.resolve_in = 0.0

    sim._resolve(quest)

    assert not city.fallen
    assert sim.quest_moves_on_a_fall == 0


def test_the_walked_quest_dungeon_moves_and_stays_the_one_walked() -> None:
    sim = board()
    city = a_rim_outpost(sim)
    quest = sim._make_dungeon(DungeonType.QUEST, city)
    sim.current = quest

    sim._fall(city)

    assert quest.did in sim.dungeons and quest.city_id != city.cid
    assert sim.current is quest
