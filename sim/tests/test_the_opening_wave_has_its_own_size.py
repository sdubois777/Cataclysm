"""The first wave of a run has its own size; every later wave is the ordinary one.

THE RULE, 2026-10-16. The owner asked for bigger waves ("I want surges to summon a
lot more dungeons. Or maybe the initial surge spawns more and then the first real
surge is 6 to 10?") and the coordinating session chose, under the owner's
delegation of numbers, an opening wave of 12 and then 8 every 90 days, with every
ordinary row's damage times 0.7.

EVERY CASE HAS A CONTROL: the second wave of the same run, and the same opening
with the setting moved.
"""
from __future__ import annotations

import dataclasses

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, SurgeMode, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(**overrides) -> Simulation:
    cfg = dataclasses.replace(TuningConfig(), tier=1, **overrides).with_tree(TREE_NONE)
    return Simulation(cfg, seed=0)


def test_the_opening_wave_is_twelve_and_the_next_is_eight() -> None:
    sim = simulation()
    assert sim.surge_index == 0
    assert sim.surge_count() == 12
    sim.surge_index = 1
    assert sim.surge_count() == 8, "CONTROL: the second wave is the ordinary one"


def test_the_opening_wave_follows_its_own_setting_and_no_other() -> None:
    sim = simulation(surge_opening_count=9, surge_dungeon_count=4)
    assert sim.surge_count() == 9
    sim.surge_index = 3
    assert sim.surge_count() == 4


def test_the_ceiling_and_the_lethality_multiplier_reach_the_opening_wave() -> None:
    capped = simulation(surge_opening_count=20)
    assert capped.surge_count() == capped.cfg.surge_count_max == 14

    heretic = simulation(surge_dungeon_multiplier=1.25)
    assert heretic.surge_count() == 15, "12 x 1.25, rounded down"


def test_swelling_growth_counts_from_the_opening_wave() -> None:
    """Under SWELLING the growth is added per wave index; the opening wave has
    index 0 and so takes none, and the second wave is the ordinary size plus one
    step."""
    sim = simulation(surge_mode=SurgeMode.SWELLING, surge_count_growth=1.0)
    assert sim.surge_count() == 12
    sim.surge_index = 1
    assert sim.surge_count() == 9


def test_the_run_actually_opens_with_twelve() -> None:
    """The first wave the day loop fires asks for twelve. The count it logs is
    then scaled by the active Cataclysms' volume, which is not this rule."""
    sim = simulation()
    asked = []
    original = sim.surge_count

    def recording():
        asked.append(original())
        return asked[-1]

    sim.surge_count = recording
    sim.step(lambda s, ds: None)
    assert sim.surge_log, "no wave fired on the first day"
    assert asked[0] == 12, asked


def test_an_ordinary_hit_is_seven_tenths_of_what_it_was() -> None:
    """The four ordinary rows, against the figures of 2026-10-13."""
    cfg = TuningConfig()
    before = {CityTier.OUTPOST: (100.0, 250.0), CityTier.BULWARK: (270.0, 1000.0),
              CityTier.SANCTUARY: (640.0, 2400.0), CityTier.PILLAR: (1200.0, 4500.0)}
    for tier, (defence, people) in before.items():
        spec = cfg.spec(DungeonType.BASIC, tier)
        assert spec.defense_damage == round(defence * 0.7, 6), tier
        assert spec.population_damage == round(people * 0.7, 6), tier
