"""A Fallen City is as deep as the ordinary dungeons the city carried when it fell.

ISSUE #1341. `docs/Cataclysm_GDD_v2.md` section VIII: "Floor count equals the
number of dungeons that were in the city when it fell (minimum 20/40/60 for
Outpost/Bulwark/Sanctuary)." The game's `MakeFallenCityDungeon` does that; the
model rolled a depth from the spec row's whole range and discarded the count.
The coordinating session ruled on 2026-10-29 that the minimum is the spec's
shallow end, the deep end is not used for a Fallen City, and the Pillar's 80 is
the spec's, since the design gives the Pillar none.

THE CONTROL is a Basic dungeon made the same way on the same board, whose depth
is still a roll inside its own spec range.
"""
from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def board() -> Simulation:
    """No surge of any kind, no tree, and nothing standing, so a fall makes one
    Fallen City and the tree adds no floors to it."""
    cfg = replace(TuningConfig(), tier=1, surge_on_city_fall=False,
                  surge_on_empty_board=False).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=3)
    sim.next_surge_day = float("inf")
    sim.dungeons.clear()
    return sim


def a_city(sim: Simulation, tier: CityTier):
    return next(c for c in sim.empire.cities.values()
                if c.tier is tier and not c.fallen)


def fallen_city_on(sim: Simulation, city):
    made = [d for d in sim.dungeons.values()
            if d.dtype is DungeonType.FALLEN_CITY and d.city_id == city.cid]
    assert len(made) == 1, f"expected one Fallen City on the city, found {len(made)}"
    return made[0]


def test_a_city_carrying_more_than_the_minimum_is_that_deep():
    sim = board()
    city = a_city(sim, CityTier.OUTPOST)
    for _ in range(50):
        sim._make_dungeon(DungeonType.BASIC, city)
    sim._fall(city)
    fallen = fallen_city_on(sim, city)
    assert fallen.floors == 50, (
        f"a city that fell carrying 50 dungeons became a {fallen.floors}-floor "
        "Fallen City. The design: floor count equals the dungeons that were in "
        "the city when it fell. Issue #1341.")
    assert fallen.tree_floors == 0


@pytest.mark.parametrize("tier, least", [(CityTier.OUTPOST, 20),
                                         (CityTier.BULWARK, 40),
                                         (CityTier.SANCTUARY, 60)])
def test_a_quiet_city_is_the_designs_minimum(tier, least):
    """Three dungeons is under every minimum, so each size gives its own: the
    design's 20 / 40 / 60, which are the spec rows' shallow ends."""
    sim = board()
    city = a_city(sim, tier)
    assert sim.cfg.spec(DungeonType.FALLEN_CITY, tier).floors[0] == least
    for _ in range(3):
        sim._make_dungeon(DungeonType.BASIC, city)
    sim._fall(city)
    assert fallen_city_on(sim, city).floors == least


def test_a_quest_dungeon_and_the_walked_dungeon_are_not_counted():
    """A Quest dungeon moves away rather than being absorbed, and the one being
    walked stays; neither is in the count, as in the game. 40 ordinary dungeons
    stand beside them, so a count that included either would read 41 or 42.
    Both counts are above the Outpost's rolled range of 20 to 35, so a roll
    cannot pass this by landing on the right number."""
    sim = board()
    city = next(c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST and not c.fallen
                and sim.empire.adjacent_exposed_cities(c))
    for _ in range(40):
        sim._make_dungeon(DungeonType.BASIC, city)
    sim._make_dungeon(DungeonType.QUEST, city)
    sim.current = sim._make_dungeon(DungeonType.BASIC, city)
    sim._fall(city)
    assert fallen_city_on(sim, city).floors == 40


def test_control_a_basic_dungeon_still_rolls_inside_its_range():
    """Only the Fallen City reads the count. Every Basic dungeon made on the
    same board falls inside its own spec range, and they are not all one depth,
    so the roll is still a roll."""
    sim = board()
    city = a_city(sim, CityTier.OUTPOST)
    lo, hi = sim.cfg.spec(DungeonType.BASIC, CityTier.OUTPOST).floors
    depths = [sim._make_dungeon(DungeonType.BASIC, city).floors for _ in range(40)]
    assert all(lo <= f <= hi for f in depths)
    assert len(set(depths)) > 1
    assert sim.making_floors is None


def test_the_roll_is_still_drawn_so_later_draws_do_not_move():
    """THE LABELLED JUDGEMENT. The Fallen City's discarded roll still takes its
    draw, so whatever is drawn after a fall is the same with the rule as without
    it. Checked by comparing the generator's state after a fall with the state
    after making a Fallen City the old way, by a roll, on an identical board."""
    a, b = board(), board()
    city_a, city_b = a_city(a, CityTier.OUTPOST), a_city(b, CityTier.OUTPOST)
    a._fall(city_a)
    b.empire.cities[city_b.cid].fallen = True
    b.empire._invalidate()
    b._make_dungeon(DungeonType.FALLEN_CITY, city_b)
    assert a.rng.getstate() == b.rng.getstate()
