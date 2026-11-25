"""The empty-board surge as the owner ruled it on 2026-11-23, issue #2865.

THE RULING, VERBATIM "yes, your rec":
  1. the board counts as empty when no ORDINARY dungeon stands; Quest dungeons, the
     Cataclysm boss dungeon and Fallen City dungeons do not count;
  2. a surge fired by an empty board brings the previous surge's count plus 4, and
     `surge_count_max` does not cap it -- SUPERSEDED on 2026-11-24, chosen option verbatim
     "Flat +4, no stacking": the SCHEDULED count plus 4, the cap and the lethality multiplier
     after it, as for any wave;
  3. `surge_interval_min` does not brake it;
  4. the +4 is checked in this model before the game changes.

Each part is its own switch on `TuningConfig`, and all three ship on, with the game's half in the
same change (see the comment on `TuningConfig.empty_board_counts_ordinary_only`). These tests
hold each part, with a control at the rule of 2026-09-07 showing the switch is what changed it.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import DungeonType, Simulation


def do_nothing(sim, dungeons):
    return None


def ruled(**kwargs) -> TuningConfig:
    """The three parts of the ruling on; a keyword overrides one of them."""
    switches = dict(empty_board_counts_ordinary_only=True, empty_board_surge_larger=True,
                    empty_board_surge_braked=False)
    switches.update(kwargs)
    return dataclasses.replace(TuningConfig(), **switches)


def todays(**kwargs) -> TuningConfig:
    """THE RULE OF 2026-09-07, the controls' world: every dungeon counts, the 25-day brake, the
    scheduled size. Named for the day it was the shipped rule, which it no longer is."""
    rule = dict(empty_board_counts_ordinary_only=False, empty_board_surge_larger=False,
                empty_board_surge_braked=True)
    rule.update(kwargs)
    return dataclasses.replace(TuningConfig(), **rule)


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


class TestItBringsTheScheduledCountPlusFour:
    """The owner's ruling of 2026-11-24, chosen option verbatim "Flat +4, no stacking": an empty-board surge is the
    scheduled count plus `empty_board_surge_extra`, never grown from the previous wave. The cap and the lethality
    multiplier apply after the +4, as to any wave: the coordinating session's judgement of the same day."""

    def test_eight_plus_four_is_twelve(self):
        sim = Simulation(ruled(), seed=1)
        sim.surge_index = 1                     # past the opening wave
        assert sim.surge_count() == 8
        assert sim.surge_count(from_empty_board=True) == 12

    def test_never_grown_from_the_previous_wave(self):
        """THE RULING OF 2026-11-23 GREW IT FROM THE LAST WAVE and was superseded: a last wave of 20 does not make
        this one 24."""
        sim = parked(ruled(), last_count=20)
        sim.surge_index = 1
        assert sim.surge_count(from_empty_board=True) == 12

    def test_the_cap_applies_after_the_four(self):
        cfg = ruled(surge_dungeon_count=13)
        sim = Simulation(cfg, seed=1)
        sim.surge_index = 1
        assert sim.surge_count(from_empty_board=True) == cfg.surge_count_max == 14

    def test_the_lethality_multiplier_applies_after_the_four(self):
        sim = Simulation(ruled(surge_dungeon_multiplier=1.25), seed=1)
        sim.surge_index = 1
        assert sim.surge_count(from_empty_board=True) == int(12 * 1.25) == 15

    def test_control_with_the_switch_off_it_is_the_scheduled_size(self):
        sim = Simulation(todays(), seed=1)
        sim.surge_index = 1
        assert sim.surge_count(from_empty_board=True) == sim.surge_count() == 8

    def test_the_wave_an_empty_board_fires_is_the_larger_one(self):
        """Through `step`, not `surge_count`: the same board and seed with the larger wave on and off."""
        counts = {}
        for larger in (True, False):
            sim = parked(ruled(empty_board_surge_larger=larger), last_count=8)
            sim.surge_index = 1
            sim.step(do_nothing)
            assert sim.surges_from_empty_board == 1
            counts[larger] = sim.surge_log[-1][2]
        assert counts[True] > counts[False], counts

    def test_a_scheduled_surge_is_not_made_larger(self):
        """Only an empty board makes a wave larger: the clock's surge keeps `surge_count()`."""
        counts = {}
        for larger in (True, False):
            sim = parked(ruled(empty_board_surge_larger=larger), last_count=8)
            sim.surge_index = 1
            sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))
            sim.next_surge_day = sim.day + 1
            sim.step(do_nothing)
            assert sim.surges_from_empty_board == 0
            counts[larger] = sim.surge_log[-1][2]
        assert counts[True] == counts[False], counts


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
    def test_all_three_ship_as_ruled(self):
        """The rulings of 2026-11-23 and 2026-11-24, shipped with the game's half in the same
        change. `tools/tests/test_surge_port.py` holds the two equal."""
        cfg = TuningConfig()
        assert cfg.empty_board_counts_ordinary_only is True
        assert cfg.empty_board_surge_larger is True
        assert cfg.empty_board_surge_braked is False
        assert cfg.empty_board_surge_extra == 4
