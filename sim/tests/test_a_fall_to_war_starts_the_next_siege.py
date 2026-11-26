"""A city's fall to War starts the next Siege (issue #51 slice B4).

THE RULING. The design's section XI says War's player cities "become aggressive and
begin attacking each other"; the plan read that as the War Quest Sieges being those
attacks and a city's fall to War starting the next one, and the owner accepted it on
2026-11-24 ("Your recommendations work for all of those death/war questions"). The
coordinating session ruled the details on 2026-11-25: the fall is to War when the hit
that felled the city was sent by War (W1); at most `quest_sieges_per_day_max` War
Quest Sieges are made on one day, waves and falls together (W2); what the fall makes
is a Quest dungeon, which counts toward War's objectives (W3).

EVERY CASE HAS A CONTROL that the rule must leave alone. The game's half is
`Cataclysm.WarQuestSieges.`, and `tools/tests/test_war_quest_sieges_port.py` holds
the two to the same rules.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim.config import TREE_NONE, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(seed: int = 0, **over) -> Simulation:
    """A War campaign with an empty board, no clock surge, and no surge on a fall, so
    nothing lands but what the fall itself makes."""
    cfg = replace(TuningConfig(), tier=1, surge_on_city_fall=False, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.active_types = ("War",)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def a_front_city_with_exposed_neighbours(sim: Simulation):
    """An exposed city, not the Pillar, with at least one exposed neighbour."""
    for city in sim.empire.cities.values():
        if (city.cid != sim.empire.pillar_id and sim.empire.is_exposed(city)
                and sim.empire.adjacent_exposed_cities(city)):
            return city
    raise AssertionError("set-up: no front city has an exposed neighbour")


def fell(sim: Simulation, city, by: str) -> None:
    """Fell `city` as a hit sent by `by` does: the hit sets `hit_by`, then `_fall`."""
    city.defense = 0.0
    sim.hit_by = by
    sim._fall(city)


def war_quest_sieges(sim: Simulation):
    return [d for d in sim.dungeons.values()
            if d.dtype is DungeonType.QUEST and d.subtype == "Siege" and d.source == "War"]


def test_a_fall_to_war_makes_one_war_quest_siege_on_a_neighbour() -> None:
    """The fall records War on the city and makes one War Quest Siege, a Quest dungeon,
    on an exposed neighbour. CONTROL: the same fall sent by Demonic records Demonic and
    makes nothing."""
    for seed in range(5):
        sim = simulation(seed)
        city = a_front_city_with_exposed_neighbours(sim)
        fell(sim, city, "War")
        assert city.fell_to == "War"
        made = war_quest_sieges(sim)
        assert len(made) == 1, (seed, made)
        assert made[0].city_id in {c.cid for c in sim.empire.adjacent_exposed_cities(city)}
        assert sim.war_sieges_on_a_fall == 1
        assert sim.hit_by == "", "the fall clears who hit"

        control = simulation(seed)
        city = a_front_city_with_exposed_neighbours(control)
        fell(control, city, "Demonic")
        assert city.fell_to == "Demonic"
        assert war_quest_sieges(control) == [], "CONTROL: a fall to Demonic makes no Siege"
        assert control.war_sieges_on_a_fall == 0


def test_no_siege_when_every_neighbour_already_holds_one() -> None:
    """Every neighbour holds a Siege, so none is made. EVERY NEIGHBOUR AND NOT ONLY
    THE EXPOSED ONES: the fall exposes the cities it shielded, which then qualify.
    CONTROL: with one exposed neighbour free, one is made, on that neighbour."""
    sim = simulation()
    city = a_front_city_with_exposed_neighbours(sim)
    for n in [sim.empire.cities[k] for k in sim.empire.neighbours(city)]:
        d = sim._make_dungeon(DungeonType.BASIC, n)
        d.subtype = "Siege"
    fell(sim, city, "War")
    assert war_quest_sieges(sim) == []

    control = simulation()
    city = a_front_city_with_exposed_neighbours(control)
    free = control.empire.adjacent_exposed_cities(city)[0]
    for n in [control.empire.cities[k] for k in control.empire.neighbours(city) if k != free.cid]:
        d = control._make_dungeon(DungeonType.BASIC, n)
        d.subtype = "Siege"
    fell(control, city, "War")
    made = war_quest_sieges(control)
    assert [d.city_id for d in made] == [free.cid], "CONTROL: the one free neighbour takes it"


def test_the_day_s_two_count_the_falls_and_the_waves_together() -> None:
    """Two War Quest Sieges already made today, so a fall makes none. CONTROL: the
    next day, the same fall makes one."""
    sim = simulation(quest_dungeon_chance=1.0, surge_opening_count=4, surge_dungeon_count=4)
    sim.trigger_surge()
    assert len(war_quest_sieges(sim)) == 2, "set-up: today's wave made the day's two"
    city = a_front_city_with_exposed_neighbours(sim)
    fell(sim, city, "War")
    assert len(war_quest_sieges(sim)) == 2, "a third on one day is not made"

    control = simulation(quest_dungeon_chance=1.0, surge_opening_count=4, surge_dungeon_count=4)
    control.trigger_surge()
    control.day += 1
    city = a_front_city_with_exposed_neighbours(control)
    fell(control, city, "War")
    assert len(war_quest_sieges(control)) == 3, "CONTROL: the next day's fall makes one"


def test_a_retaken_city_was_felled_by_nobody_and_the_switch_turns_the_rule_off() -> None:
    sim = simulation()
    city = a_front_city_with_exposed_neighbours(sim)
    fell(sim, city, "War")
    sim._retake(city)
    assert city.fell_to == "", "a retaken city stands, felled by nobody"

    off = simulation(war_fall_makes_a_quest_siege=False)
    city = a_front_city_with_exposed_neighbours(off)
    fell(off, city, "War")
    assert city.fell_to == "War", "the record is kept with the rule off"
    assert war_quest_sieges(off) == [], "with the switch off a fall to War makes nothing"
