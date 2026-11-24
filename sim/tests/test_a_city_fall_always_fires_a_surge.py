"""Every city fall fires a surge, whatever the gap since the last one.

THE OWNER'S RULING OF 2026-11-24 on issue #2865, verbatim: "surges from cities
falling should happen regardless". The owner's case: an Outpost fell at most 24
days after the previous surge and fired none (issue #2866). From issue #1432
(2026-09-14) until then a fall inside `surge_interval_min` (25 days) fired
nothing; `city_fall_surge_braked` keeps that rule behind a switch that ships off,
and `test_a_city_fall_respects_the_minimum_surge_gap.py` holds it there.

DRIVEN BY HAND, as that file is: the last surge is written into the log at a
chosen distance, the scheduled clock is pushed out of the way, and `_fall` is
called on a standing city.

EACH TEST HAS A CONTROL: the same fall with `city_fall_surge_braked` on fires
nothing, so a test that passed because the gap was long enough would fail its
control.

`city_fall_advances_escalation` IS NOT CHANGED by the ruling: a fall's surge
still advances the escalation counter.
"""

from __future__ import annotations

import dataclasses

from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import Simulation


def standing_cities(sim: Simulation, n: int):
    found = [city for city in sim.empire.cities.values()
             if not city.fallen and city.cid != sim.empire.pillar_id]
    assert len(found) >= n, "the empire has fewer standing cities than the test needs"
    return found[:n]


def with_a_surge(since: float, **overrides) -> Simulation:
    """A simulation whose last surge landed `since` days before today, with the
    scheduled clock pushed out of the way so only a fall can fire one."""
    cfg = dataclasses.replace(TuningConfig(), **{"surge_on_city_fall": True, **overrides})
    sim = Simulation(cfg, seed=1)
    sim.day = 1000
    sim.next_surge_day = 1e9
    sim.surge_log = [(int(sim.day - since), cfg.surge_interval_days, 4)]
    return sim


def test_the_ruling_ships():
    cfg = TuningConfig()
    assert cfg.surge_on_city_fall is True
    assert cfg.city_fall_surge_braked is False, (
        "a fall's surge is braked by default. The owner ruled on 2026-11-24 that "
        "surges from cities falling should happen regardless. Issue #2865")
    assert cfg.city_fall_advances_escalation is True, "the ruling left escalation as it was"


def test_a_fall_the_day_after_a_surge_fires_one():
    sim = with_a_surge(since=1.0)
    sim._fall(standing_cities(sim, 1)[0])
    assert len(sim.surge_log) == 2, "a fall one day after a surge fired no surge. Issue #2865"


def test_control_with_the_brake_on_the_same_fall_fires_none():
    sim = with_a_surge(since=1.0, city_fall_surge_braked=True)
    sim._fall(standing_cities(sim, 1)[0])
    assert len(sim.surge_log) == 1


def test_a_fall_24_days_after_a_surge_fires_one():
    """The owner's case of issue #2866: at most 24 days after the previous surge."""
    sim = with_a_surge(since=24.0)
    sim._fall(standing_cities(sim, 1)[0])
    assert len(sim.surge_log) == 2


def test_control_with_the_brake_on_24_days_fires_none():
    sim = with_a_surge(since=24.0, city_fall_surge_braked=True)
    sim._fall(standing_cities(sim, 1)[0])
    assert len(sim.surge_log) == 1


def test_two_cities_falling_on_one_day_fire_two():
    sim = with_a_surge(since=1.0)
    first, second = standing_cities(sim, 2)
    sim._fall(first)
    sim._fall(second)
    assert len(sim.surge_log) == 3, "two falls on one day did not fire a surge each. Issue #2865"
    assert sim.surge_log[1][0] == sim.surge_log[2][0] == sim.day


def test_control_with_the_brake_on_two_falls_on_one_day_fire_none():
    sim = with_a_surge(since=1.0, city_fall_surge_braked=True)
    first, second = standing_cities(sim, 2)
    sim._fall(first)
    sim._fall(second)
    assert len(sim.surge_log) == 1


def test_a_falls_surge_still_advances_escalation():
    sim = with_a_surge(since=1.0)
    before = sim.surge_index
    sim._fall(standing_cities(sim, 1)[0])
    assert sim.surge_index == before + 1


def test_switching_the_trigger_off_still_fires_nothing():
    sim = with_a_surge(since=999.0, surge_on_city_fall=False)
    sim._fall(standing_cities(sim, 1)[0])
    assert len(sim.surge_log) == 1
