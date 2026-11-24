"""The empty-board surge as the owner ruled it on 2026-11-23, issue #2865.

THE RULING, VERBATIM "yes, your rec":
  1. the board counts as empty when no ORDINARY dungeon stands; Quest dungeons, the
     Cataclysm boss dungeon and Fallen City dungeons do not count;
  2. a surge fired by an empty board brings the previous surge's count plus 4, and
     `surge_count_max` does not cap it;
  3. `surge_interval_min` does not brake it;
  4. the +4 is checked in this model before the game changes.

Each part is its own switch on `TuningConfig` and all three ship at the behaviour of
2026-09-07 until the game side is written (see the comment on
`TuningConfig.empty_board_counts_ordinary_only`). These tests switch them on and hold
each part, with a control showing the switch is what changed it.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import DungeonType, Simulation


def do_nothing(sim, dungeons):
    return None


def ruled(**kwargs) -> TuningConfig:
    """The three parts of the ruling on."""
    return dataclasses.replace(TuningConfig(), empty_board_counts_ordinary_only=True,
                               empty_board_surge_grows=True, empty_board_surge_braked=False,
                               **kwargs)


def todays(**kwargs) -> TuningConfig:
    return dataclasses.replace(TuningConfig(), **kwargs)


def a_standing_city(sim: Simulation):
    for city in sim.empire.cities.values():
        if not city.fallen:
            return city
    raise AssertionError("the whole empire has already fallen")


def parked(cfg: TuningConfig, since: float = 999.0, last_count: int = 8) -> Simulation:
    """An empty board, the clock not due, and a last surge of `last_count` dungeons that
    the next `step` sees as `since` days ago. `test_surge_when_the_board_empties.parked`,
    with the last count settable."""
    sim = Simulation(cfg, seed=1)
    sim.dungeons.clear()
    sim.day = 1000
    sim.next_surge_day = 1e9
    sim.surge_log = [(int(sim.day + 1 - since), cfg.surge_interval_days, last_count)]
    return sim


def host_for(sim: Simulation, dtype: DungeonType):
    return (sim.empire.cities[sim.empire.pillar_id]
            if dtype is DungeonType.CATACLYSM else a_standing_city(sim))


class TestOnlyAnOrdinaryDungeonKeepsTheBoardFull:
    @pytest.mark.parametrize("dtype", [DungeonType.QUEST, DungeonType.FALLEN_CITY,
                                       DungeonType.CATACLYSM])
    def test_a_dungeon_that_is_not_ordinary_leaves_the_board_empty(self, dtype):
        sim = parked(ruled())
        sim._make_dungeon(dtype, host_for(sim, dtype))

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 1, (
            f"a {dtype.name} dungeon kept the board from reading as empty. The owner "
            "ruled on 2026-11-23 that only an ordinary dungeon counts; issue #2865")

    @pytest.mark.parametrize("dtype", [DungeonType.QUEST, DungeonType.FALLEN_CITY,
                                       DungeonType.CATACLYSM])
    def test_control_with_the_switch_off_it_counts_as_standing(self, dtype):
        sim = parked(todays())
        sim._make_dungeon(dtype, host_for(sim, dtype))

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0

    def test_an_ordinary_dungeon_still_keeps_the_board_full(self):
        sim = parked(ruled())
        sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0


class TestItBringsThePreviousCountPlusFour:
    def test_the_previous_count_plus_four(self):
        sim = parked(ruled(), last_count=8)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 1
        assert sim.surge_log[-1][2] == 8 + 4

    def test_it_is_not_capped_at_the_most_dungeons_per_surge(self):
        cfg = ruled()
        sim = parked(cfg, last_count=cfg.surge_count_max)

        sim.step(do_nothing)

        assert sim.surge_log[-1][2] == cfg.surge_count_max + 4 > cfg.surge_count_max

    def test_control_with_the_switch_off_it_brings_the_scheduled_size(self):
        sim = parked(todays(), last_count=8)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 1
        assert sim.surge_log[-1][2] != 8 + 4
        assert sim.surge_log[-1][2] <= todays().surge_count_max

    def test_a_scheduled_surge_is_not_grown(self):
        """Only an empty board grows a wave: the clock's surge keeps `surge_count()`."""
        sim = parked(ruled(), last_count=8)
        sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))
        sim.next_surge_day = sim.day + 1

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0
        assert sim.surge_log[-1][2] != 8 + 4


class TestTheMinimumGapDoesNotBrakeIt:
    def test_it_fires_the_day_after_a_surge(self):
        sim = parked(ruled(), since=1.0)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 1, (
            "a surge one day ago braked the empty-board surge. The owner ruled on "
            "2026-11-23 that the 25-day minimum gap does not apply to it; issue #2865")

    def test_control_with_the_switch_off_the_gap_brakes_it(self):
        sim = parked(todays(), since=1.0)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0


class TestTheShippedDefaults:
    def test_all_three_ship_at_the_rule_of_2026_09_07_until_the_game_changes(self):
        """Point 4 of the ruling: the model is measured before the game changes, and the
        two change together. `tools/tests/test_surge_port.py` holds them equal."""
        cfg = TuningConfig()
        assert cfg.empty_board_counts_ordinary_only is False
        assert cfg.empty_board_surge_grows is False
        assert cfg.empty_board_surge_braked is True
        assert cfg.empty_board_surge_extra == 4
