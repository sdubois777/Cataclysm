"""A walk's days run when the player leaves the dungeon, and the clear counts first.

THE OWNER'S DECISION OF 2026-10-24, verbatim: "to be clear, all of the
resolutions and day/timer updates happen once a player leaves a dungeon. So
it's not like it will just end their run in the middle of a dungeon if another
dungeon was set to resolve in the middle of the run." And on the order: "Clear
counts, city still falls". `TuningConfig.days_resolve_on_exit` is the switch,
on by default; `Simulation._walk_day` and `Simulation._settle_owed` are the
rule. The game's half is `UCataclysmEmpireRun::SettleDaysOwed`.

THE CODE'S OWN ORDER ON A DEATH: the death's resolve, then the walk's days,
then the death's own days, ruled by the coordinating session on 2026-10-24.
Through the walk's days the walked dungeon still counts as the one the player
is inside, so its timer stays paused, ruled the same day.

EACH RULE HAS ITS CONTROL: the same scene with the switch off, where the days
run as the walk goes.
"""

from __future__ import annotations

import dataclasses

from cataclysm_sim.config import DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation

SEED = 3
WALK = 5


def simulation(**overrides) -> Simulation:
    """An empty board on which no wave is due."""
    settings = dict(surge_on_empty_board=False)
    settings.update(overrides)
    sim = Simulation(dataclasses.replace(TuningConfig(), **settings), seed=SEED)
    sim.next_surge_day = float("inf")
    return sim


def an_outpost(sim: Simulation):
    for city in sim.empire.cities.values():
        if not city.fallen and city.cid != sim.empire.pillar_id:
            return city
    raise AssertionError("no standing city but the Pillar")


def nobody_decides(sim, dungeons):
    return None


def walking(sim: Simulation, dtype: DungeonType, city, *, dies: bool):
    """The player inside a dungeon of `dtype` on `city` for `WALK` days, and
    certain to clear it or certain to die at its end."""
    d = sim._make_dungeon(dtype, city)
    sim.death_chance = (lambda _d: 1.0) if dies else (lambda _d: 0.0)
    sim.current = d
    sim.days_remaining = WALK
    return d


def test_the_switch_is_on_by_default():
    assert TuningConfig().days_resolve_on_exit is True


def test_nothing_happens_in_the_empire_while_the_player_walks():
    sim = simulation()
    city = an_outpost(sim)
    other = sim._make_dungeon(DungeonType.BASIC, city)
    other.resolve_in = 2.0
    walking(sim, DungeonType.BASIC, city, dies=False)
    day, defence = sim.day, city.defense

    for _ in range(WALK - 1):
        sim.step(nobody_decides)

    assert sim.day == day, "the day moved while the player was inside"
    assert sim.owed_days == WALK - 1
    assert other.resolve_in == 2.0, "another dungeon's timer moved during the walk"
    assert other.did in sim.dungeons
    assert city.defense == defence


def test_control_with_the_switch_off_the_days_run_as_the_walk_goes():
    sim = simulation(days_resolve_on_exit=False)
    city = an_outpost(sim)
    other = sim._make_dungeon(DungeonType.BASIC, city)
    other.resolve_in = 2.0
    walking(sim, DungeonType.BASIC, city, dies=False)
    day = sim.day

    for _ in range(WALK - 1):
        sim.step(nobody_decides)

    assert sim.day == day + WALK - 1
    assert sim.owed_days == 0
    assert other.did not in sim.dungeons, (
        "CONTROL: the other dungeon's timer did not run out during the walk, "
        "so the test above does not show the switch is what stopped it")


def test_leaving_credits_the_clear_and_then_runs_the_walks_days():
    """Clear counts, city still falls: the city falls to the other dungeon in
    the walk's days, after the walk was credited."""
    sim = simulation()
    city = an_outpost(sim)
    other = sim._make_dungeon(DungeonType.BASIC, city)
    other.resolve_in = 2.0
    other.defense_damage = 10_000.0
    walked = walking(sim, DungeonType.BASIC, city, dies=False)
    day = sim.day

    for _ in range(WALK):
        sim.step(nobody_decides)

    assert sim.cleared == 1 and sim.basic_cleared == 1, "the clear was not counted"
    assert walked.did not in sim.dungeons
    assert city.fallen, "the other dungeon did not fell the city in the walk's days"
    assert sim.day == day + WALK, "the walk's days did not all run"
    assert sim.current is None and sim.owed_days == 0


def test_a_death_resolves_then_the_walk_runs_its_days_then_the_death_its_own():
    """In a Quest dungeon, which a death does not resolve and which stays on
    the map: its timer stays paused through the walk's days, and counts again
    through the death's."""
    sim = simulation()
    city = an_outpost(sim)
    quest = walking(sim, DungeonType.QUEST, city, dies=True)
    quest.resolve_in = 30.0
    day = sim.day

    for _ in range(WALK):
        sim.step(nobody_decides)

    assert sim.deaths == 1
    assert sim.day == day + WALK, "the walk's days did not run when the death ended it"
    assert quest.resolve_in == 30.0, (
        "the walked dungeon's timer moved in the walk's days, which were spent "
        "inside it")
    assert sim.dying and sim.days_remaining == sim.cfg.death_day_cost, (
        "the death's own days were not still to come after the walk's")

    for _ in range(sim.cfg.death_day_cost):
        sim.step(nobody_decides)

    assert sim.day == day + WALK + sim.cfg.death_day_cost
    assert quest.resolve_in == 30.0 - sim.cfg.death_day_cost, (
        "the Quest dungeon's timer did not count again through the death's days")


def test_a_death_in_an_ordinary_dungeon_hits_its_city_once_before_the_walks_days():
    sim = simulation()
    city = an_outpost(sim)
    walked = walking(sim, DungeonType.BASIC, city, dies=True)
    walked.resolve_in = 100.0
    before = city.defense

    for _ in range(WALK):
        sim.step(nobody_decides)

    assert city.defense < before, "the death did not resolve the dungeon"
    assert walked.did not in sim.dungeons, "the resolved dungeon did not leave"
    assert sim.dying


def test_a_campaign_that_ends_inside_a_walk_ends_on_its_last_day():
    sim = simulation(max_days=WALK + 2)
    city = an_outpost(sim)
    d = sim._make_dungeon(DungeonType.BASIC, city)
    sim.death_chance = lambda _d: 0.0
    sim.day = 3
    sim.current = d
    sim.days_remaining = 50

    while sim.day < sim.cfg.max_days:
        sim.step(nobody_decides)

    assert sim.day == sim.cfg.max_days, "the campaign ran past its last day"
    assert sim.current is d, "the walk was ended rather than its days run"
