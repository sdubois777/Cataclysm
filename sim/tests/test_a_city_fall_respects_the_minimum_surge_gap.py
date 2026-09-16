"""A city falling fires a surge only when the last one is `surge_interval_min`
days behind, like the other two triggers.

WHY THIS EXISTS. Issue #1432. `Simulation.trigger_surge` is reached from three
places: the scheduled clock, which spaces itself by `surge_gap()`; the
board-empty trigger, which refuses inside `surge_interval_min`; and a city
falling, which had no spacing at all. At difficulty tier 4 with no empire tree
the fall trigger was 89% of every surge that fired and 82% of those landed
inside the 25-day floor, so a fall the day after a surge landed a whole second
wave. Ruled on 2026-09-14 under the owner's delegation: the fall respects the
same minimum gap.

DRIVEN BY HAND, the way `test_surge_when_the_board_empties.py` drives the
board-empty trigger: the last surge is written into the log at a chosen
distance and `_fall` is called on a standing city, so the test says exactly
how long the gap is and does not depend on a campaign producing one.

THE CONTROL. A fall exactly at the gap fires. The two tests differ by one day
and nothing else, so a trigger that never fired would fail the control.
"""

from __future__ import annotations

import dataclasses

from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import Simulation


def a_standing_city(sim: Simulation):
    for city in sim.empire.cities.values():
        if not city.fallen and city.cid != sim.empire.pillar_id:
            return city
    raise AssertionError("the whole empire has already fallen")


def with_a_surge(since: float, **overrides) -> Simulation:
    """A simulation whose last surge landed `since` days before today, with the
    scheduled clock pushed out of the way so only the fall can fire one."""
    cfg = dataclasses.replace(TuningConfig(), **{"surge_on_city_fall": True, **overrides})
    sim = Simulation(cfg, seed=1)
    sim.day = 1000
    sim.next_surge_day = 1e9
    sim.surge_log = [(int(sim.day - since), cfg.surge_interval_days, 4)]
    return sim


def test_a_fall_inside_the_minimum_gap_fires_no_surge():
    sim = with_a_surge(since=24.0, surge_interval_min=25.0)

    sim._fall(a_standing_city(sim))

    assert len(sim.surge_log) == 1, (
        "a city falling 24 days after the last surge landed a second wave, "
        "inside the 25-day surge_interval_min the other triggers respect. "
        "Issue #1432.")


def test_a_fall_exactly_at_the_minimum_gap_fires_one():
    """THE CONTROL on the test above; one day apart and nothing else."""
    sim = with_a_surge(since=25.0, surge_interval_min=25.0)

    sim._fall(a_standing_city(sim))

    assert len(sim.surge_log) == 2, "a fall at exactly the minimum gap did not fire"


def test_the_fall_itself_still_stands_when_the_surge_is_withheld():
    """Only the extra wave is withheld: the city is down, its Fallen City
    dungeon exists, and the lane is open, exactly as before."""
    sim = with_a_surge(since=1.0, surge_interval_min=25.0)
    city = a_standing_city(sim)
    before = len(sim.dungeons)

    sim._fall(city)

    assert city.fallen
    assert len(sim.dungeons) == before + 1, "the Fallen City dungeon was not made"
    assert len(sim.surge_log) == 1


def test_switching_the_trigger_off_still_fires_nothing_at_any_gap():
    sim = with_a_surge(since=999.0, surge_on_city_fall=False)

    sim._fall(a_standing_city(sim))

    assert len(sim.surge_log) == 1
