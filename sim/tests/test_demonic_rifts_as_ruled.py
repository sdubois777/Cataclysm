"""Demonic's Infernal Rifts (Hell on Earth, issue #51), as the owner ruled them on 2026-11-24.

THE RULINGS, each the option the owner chose on issue #51 (comment 7149391315), verbatim:
  Q1 "Rift = Quest dungeon (Recommended)": a Rift IS a Demonic Quest dungeon.
  Q3/Q4 "Anywhere but the Pillar (Recommended)": it opens on any standing city but the Pillar, behind the front
     included, by the surge's tier weights; every 5 days from the day it opened, not reset when it moves, an unsealed
     Rift sends an ordinary Basic Demonic dungeon, made as a surge makes one, to an adjacent city a surge could target,
     and nothing when no neighbour qualifies.
  Q5 "Every death, no gap (Recommended)": each death in a Rift brings an ordinary surge at once, with no
     least-interval brake.

`TuningConfig.demonic_rifts` SHIPS OFF until the measurement is accepted and the game has the Rifts (slice B2); these
tests switch it on. EACH RULE HAS A CONTROL that fails if the rule is not what made the test pass.

DRIVEN BY HAND: the scheduled clock and the empty-board surge are out of the way, the board starts empty, and Demonic
is the only active Cataclysm.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation, active_cataclysms_for


def simulation(seed: int = 0, **over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1,
                  **{"demonic_rifts": True, "surge_on_empty_board": False, **over}).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.active_types = ("Demonic",)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def rift_on(sim: Simulation, city, source: str = "Demonic"):
    """A Demonic Quest dungeon on `city`, opened today, with its own timer out of the way."""
    sim.making_for = source
    try:
        d = sim._make_dungeon(DungeonType.QUEST, city)
    finally:
        sim.making_for = ""
    d.source = source
    d.resolve_max = 10_000
    d.resolve_in = 10_000.0
    return d


def city_of(sim: Simulation, tier: CityTier):
    return next(c for c in sim.empire.cities.values() if c.tier is tier)


def sent(sim: Simulation):
    return [d for d in sim.dungeons.values() if d.dtype is DungeonType.BASIC]


def run_days(sim: Simulation, n: int) -> None:
    for _ in range(n):
        sim._empire_day()


# -- the switches ---------------------------------------------------------------------------------------------------


def test_the_rifts_ship_off_with_the_rulings_numbers() -> None:
    cfg = TuningConfig()
    assert cfg.demonic_rifts is False, "the Rifts ship on before the measurement is accepted and the game has them"
    assert cfg.rift_spawn_every_days == 5, "the design's 5 days, section XI"
    assert cfg.rift_death_brings_surge is True


def test_a_campaign_without_demonic_is_the_same_with_the_rifts_on() -> None:
    """The Rift code takes no draw unless Demonic sends a Quest dungeon, so a campaign that never faces Demonic is
    unchanged to the last field."""
    base = TuningConfig()
    seed = next(s for s in range(100) if "Demonic" not in active_cataclysms_for(base, s))
    off = Simulation(base, seed=seed).run(policies.triage)
    on = Simulation(replace(base, demonic_rifts=True), seed=seed).run(policies.triage)
    assert on == off


# -- Q1 and Q3: where a Rift opens -------------------------------------------------------------------------------


def rift_tiers(**over) -> list:
    tiers = []
    for seed in range(40):
        sim = simulation(seed=seed, quest_dungeon_chance=1.0, **over)
        sim.trigger_surge()
        for d in sim.dungeons.values():
            assert d.dtype is DungeonType.QUEST and d.source == "Demonic"
            city = sim.empire.cities[d.city_id]
            assert city.cid != sim.empire.pillar_id, "a Rift opened on the Pillar"
            assert not city.fallen
            tiers.append(city.tier)
    return tiers


def test_a_rift_opens_behind_the_front_by_the_surges_tier_weights() -> None:
    """At the start only the 12 rim Outposts are exposed; the 8 Bulwarks and 4 Sanctuaries are sealed. By the tier
    weights 5, 3 and 1.5 a Rift opens on an Outpost 60 times in 90 (12 x 5 over 12 x 5 + 8 x 3 + 4 x 1.5), on a
    Bulwark 24 in 90 and on a Sanctuary 6 in 90. The band is wide enough for 40 waves of 8."""
    tiers = rift_tiers()
    n = len(tiers)
    assert n >= 300
    share = {t: tiers.count(t) / n for t in (CityTier.OUTPOST, CityTier.BULWARK, CityTier.SANCTUARY)}
    assert 0.58 < share[CityTier.OUTPOST] < 0.75, share
    assert 0.18 < share[CityTier.BULWARK] < 0.35, share
    assert 0.02 < share[CityTier.SANCTUARY] < 0.12, share


def test_control_with_the_rifts_off_a_demonic_quest_dungeon_keeps_to_the_front() -> None:
    assert set(rift_tiers(demonic_rifts=False)) == {CityTier.OUTPOST}


def test_a_rift_never_opens_on_a_fallen_city() -> None:
    sim = simulation(quest_dungeon_chance=1.0, surge_on_city_fall=False)
    fallen = [c for c in sim.empire.cities.values() if c.tier is CityTier.BULWARK][:4]
    for c in fallen:
        c.fallen = True
    sim.empire._invalidate()
    for _ in range(30):
        sim.trigger_surge()
    hosts = {d.city_id for d in sim.dungeons.values()}
    assert hosts and not hosts & {c.cid for c in fallen}


# -- Q4: every 5 days, an ordinary dungeon to a neighbour ----------------------------------------------------------


def test_a_rift_sends_one_basic_dungeon_on_its_fifth_day_and_its_tenth() -> None:
    sim = simulation()
    host = city_of(sim, CityTier.OUTPOST)
    rift = rift_on(sim, host)
    run_days(sim, 4)
    assert sent(sim) == [], "a Rift sent a dungeon before its fifth day"
    run_days(sim, 1)
    first = sent(sim)
    assert len(first) == 1 and sim.rift_spawns == 1
    d = first[0]
    assert d.source == "Demonic" and d.dtype is DungeonType.BASIC
    target = sim.empire.cities[d.city_id]
    assert target.cid in sim.empire.neighbours(host), "the dungeon went to a city not adjacent to the Rift"
    assert sim.empire.is_exposed(target) and target.cid != sim.empire.pillar_id
    lo, hi = sim.cfg.spec(DungeonType.BASIC, target.tier).floors
    assert lo <= d.floors <= hi, "not made as a surge makes one for that city"
    run_days(sim, 4)
    assert sim.rift_spawns == 1
    run_days(sim, 1)
    assert sim.rift_spawns == 2
    assert rift.did in sim.dungeons, "sending a dungeon removed the Rift"


def test_control_a_demonic_quest_dungeon_sends_nothing_with_the_rifts_off() -> None:
    sim = simulation(demonic_rifts=False)
    rift_on(sim, city_of(sim, CityTier.OUTPOST))
    run_days(sim, 10)
    assert sent(sim) == [] and sim.rift_spawns == 0


def test_control_a_spawn_interval_of_zero_sends_nothing() -> None:
    sim = simulation(rift_spawn_every_days=0)
    rift_on(sim, city_of(sim, CityTier.OUTPOST))
    run_days(sim, 10)
    assert sent(sim) == []


def test_a_quest_dungeon_of_another_cataclysm_is_not_a_rift() -> None:
    sim = simulation()
    sim.active_types = ("Death",)
    rift_on(sim, city_of(sim, CityTier.OUTPOST), source="Death")
    run_days(sim, 10)
    assert sent(sim) == []


def test_the_count_is_not_reset_when_the_rift_moves() -> None:
    sim = simulation()
    host = city_of(sim, CityTier.OUTPOST)
    rift = rift_on(sim, host)
    run_days(sim, 7)
    assert sim.rift_spawns == 1
    new_host = next(sim.empire.cities[k] for k in sim.empire.neighbours(host)
                    if sim.empire.is_exposed(sim.empire.cities[k]))
    rift.city_id = new_host.cid
    rift.arrived_day = sim.day
    run_days(sim, 3)
    assert sim.rift_spawns == 2, "the Rift's count restarted when it moved"
    newest = max(sent(sim), key=lambda d: d.did)
    assert newest.city_id in sim.empire.neighbours(new_host), "it sent from where it stood before the move"


def test_a_rift_with_no_qualifying_neighbour_sends_nothing_until_a_lane_opens() -> None:
    """A Sanctuary's neighbours at the start are sealed Bulwarks and the Pillar. The control is the same Rift once an
    Outpost shielding one of those Bulwarks has fallen: the Bulwark is exposed and receives the next dungeon."""
    sim = simulation(surge_on_city_fall=False)
    sanctuary = city_of(sim, CityTier.SANCTUARY)
    rift_on(sim, sanctuary)
    run_days(sim, 5)
    assert sent(sim) == [], "a Rift sent a dungeon to a sealed city or the Pillar"

    bulwark = next(sim.empire.cities[k] for k in sim.empire.neighbours(sanctuary)
                   if sim.empire.cities[k].tier is CityTier.BULWARK)
    sim.empire.cities[bulwark.outward[0]].fallen = True
    sim.empire._invalidate()
    assert sim.empire.is_exposed(bulwark)
    run_days(sim, 5)
    landed = sent(sim)
    assert len(landed) == 1
    assert sim.empire.cities[landed[0].city_id].cid in sim.empire.neighbours(sanctuary)
    assert sim.empire.is_exposed(sim.empire.cities[landed[0].city_id])


def test_a_sealed_rift_sends_nothing() -> None:
    sim = simulation()
    rift = rift_on(sim, city_of(sim, CityTier.OUTPOST))
    run_days(sim, 3)
    sim.dungeons.pop(rift.did)
    run_days(sim, 7)
    assert sent(sim) == []


# -- Q5: a death in a Rift brings a surge ---------------------------------------------------------------------------


def die_in(sim: Simulation, d, walk_days: int = 3, death: float = 1.0) -> int:
    """The player walks `d` for `walk_days` and dies (or clears it, at `death` 0) at the end. Returns the day the walk
    began."""
    sim.death_chance = lambda _d: death
    began = sim.day
    sim.current = d
    sim.days_remaining = walk_days
    for _ in range(walk_days):
        sim._walk_day()
    return began


def test_a_death_in_a_rift_brings_a_surge_the_day_after_a_surge() -> None:
    sim = simulation()
    rift = rift_on(sim, city_of(sim, CityTier.OUTPOST))
    sim.surge_log = [(sim.day, sim.cfg.surge_interval_days, 8)]
    began = die_in(sim, rift, walk_days=3)
    assert sim.deaths == 1
    assert len(sim.surge_log) == 2, "a death in a Rift brought no surge"
    assert sim.surges_from_rift_deaths == 1
    assert sim.surge_log[-1][0] == began + 3, "the surge did not land on the day of the death, after the walk's days"
    assert rift.did in sim.dungeons, "the death removed the Rift"


def test_control_a_death_in_a_quest_dungeon_that_is_not_a_rift_brings_none() -> None:
    sim = simulation(demonic_rifts=False)
    quest = rift_on(sim, city_of(sim, CityTier.OUTPOST))
    die_in(sim, quest)
    assert sim.deaths == 1 and sim.surge_log == []


def test_control_the_switch_off_brings_none() -> None:
    sim = simulation(rift_death_brings_surge=False)
    rift = rift_on(sim, city_of(sim, CityTier.OUTPOST))
    die_in(sim, rift)
    assert sim.deaths == 1 and sim.surge_log == []


def test_control_sealing_a_rift_brings_no_surge_and_is_one_demonic_objective() -> None:
    sim = simulation()
    rift = rift_on(sim, city_of(sim, CityTier.OUTPOST))
    die_in(sim, rift, death=0.0)
    assert sim.deaths == 0 and sim.surge_log == []
    assert rift.did not in sim.dungeons
    assert sim.objectives_by_type["Demonic"] == 1


def test_two_deaths_bring_two_surges() -> None:
    sim = simulation()
    rift = rift_on(sim, city_of(sim, CityTier.OUTPOST))
    die_in(sim, rift, walk_days=2)
    die_in(sim, rift, walk_days=2)
    assert sim.surges_from_rift_deaths == 2 and len(sim.surge_log) == 2


# -- a whole campaign -----------------------------------------------------------------------------------------------


def test_a_demonic_campaign_opens_rifts_that_send_and_kill() -> None:
    base = TuningConfig()
    seed = next(s for s in range(100) if "Demonic" in active_cataclysms_for(base, s))
    sim = Simulation(replace(base, demonic_rifts=True), seed=seed)
    sim.run(policies.triage)
    assert sim.rifts_opened > 0 and sim.rift_spawns > 0
    control = Simulation(base, seed=seed)
    control.run(policies.triage)
    assert (control.rifts_opened, control.rift_spawns, control.surges_from_rift_deaths) == (0, 0, 0)
