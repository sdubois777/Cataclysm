"""Death's Abomination lairs in the model (issue #51 slice B5, with the simulation slice A2).

THE DESIGN, `docs/Cataclysm_GDD_v2.md`, Dead Rising: "Cursed Abominations spawn as cities fall, growing stronger with
each subsequent loss ... Each fallen city adds a more powerful Abomination." Accepted by the owner on 2026-11-24, with
the details ruled by the coordinating session on 2026-11-25: a surge's Death Quest dungeon is a lair, and every fall
while Death is active adds one (Q6, L2); its boss's health and damage are multiplied by `abomination_step` once for
every city fallen when it appeared, the fall that made it included (Q7, L1).

EVERY CASE HAS A CONTROL that the rule must leave alone. The game's half is `Cataclysm.AbominationLairs.`, and
`tools/tests/test_abomination_lairs_port.py` holds the two to the same rules.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import combat
from cataclysm_sim.config import TREE_NONE, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(facing: tuple[str, ...] = ("Death",), seed: int = 0, **over) -> Simulation:
    """A campaign with an empty board, no clock surge and no surge on a fall."""
    cfg = replace(TuningConfig(), tier=1, surge_on_city_fall=False, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.active_types = facing
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def front_cities(sim: Simulation):
    """The exposed cities that are not the Pillar and have an exposed neighbour."""
    return [c for c in sim.empire.cities.values()
            if c.cid != sim.empire.pillar_id and sim.empire.is_exposed(c)
            and sim.empire.adjacent_exposed_cities(c)]


def fell(sim: Simulation, city, by: str = "Death") -> None:
    city.defense = 0.0
    sim.hit_by = by
    sim._fall(city)


def lairs(sim: Simulation):
    return [d for d in sim.dungeons.values() if sim.is_lair(d)]


def make_quest(sim: Simulation, sender: str, city):
    sim.making_for = sender
    try:
        d = sim._make_dungeon(DungeonType.QUEST, city)
    finally:
        sim.making_for = ""
    d.source = sender
    return d


def test_a_lair_counts_the_cities_fallen_as_it_appears() -> None:
    """A Death Quest dungeon made after two falls counts two. CONTROLS: one made before any fall counts nought, and a
    Quest dungeon War sends is no lair and counts nought."""
    sim = simulation(death_lairs=True)
    city = front_cities(sim)[0]
    first = make_quest(sim, "Death", city)
    assert first.cities_fallen_before == 0 and sim.is_lair(first)

    sim = simulation(facing=("War",))
    one, two = front_cities(sim)[:2]
    fell(sim, one, "War")
    fell(sim, two, "War")
    standing = front_cities(sim)[0]
    lair = make_quest(sim, "Death", standing)
    war = make_quest(sim, "War", standing)
    assert lair.cities_fallen_before == 2
    assert war.cities_fallen_before == 0 and not sim.is_lair(war), "CONTROL: War's Quest dungeon is no lair"


def test_a_fall_while_death_is_active_adds_a_lair_that_counts_it() -> None:
    """The fall makes one lair on an exposed neighbour, counting the fallen city (L1). CONTROL: the same fall with
    Death not active makes none."""
    for seed in range(5):
        sim = simulation(seed=seed)
        city = front_cities(sim)[0]
        fell(sim, city, "Death")
        made = lairs(sim)
        assert len(made) == 1 and sim.lairs_on_a_fall == 1, (seed, made)
        assert made[0].city_id in {c.cid for c in sim.empire.adjacent_exposed_cities(city)}
        assert made[0].cities_fallen_before == 1, "the fall that made it is counted"
        assert made[0].subtype == "Horde"

        control = simulation(facing=("War",), seed=seed)
        fell(control, front_cities(control)[0], "War")
        assert lairs(control) == [] and control.lairs_on_a_fall == 0, "CONTROL: Death is not active"


def test_every_fall_adds_one_with_no_cap() -> None:
    """Three falls, three lairs, counting one, two and three (L2: no cap)."""
    sim = simulation()
    counts = []
    for _ in range(3):
        city = front_cities(sim)[0]
        fell(sim, city, "Demonic")
        counts.append(max(d.cities_fallen_before for d in lairs(sim)))
    assert len(lairs(sim)) == 3
    assert counts == [1, 2, 3]


def test_the_boss_grows_by_the_step_once_a_fallen_city() -> None:
    """With a step of 1.2, a lair counting three multiplies its boss by 1.728 and is deadlier than one counting
    nought. CONTROLS: a step of 1.0 changes nothing, and a dungeon that is no lair is multiplied by one."""
    sim = simulation(abomination_step=1.2)
    city = front_cities(sim)[0]
    lair = make_quest(sim, "Death", city)
    lair.cities_fallen_before = 3
    assert abs(sim.abomination_multiplier(lair) - 1.2 ** 3) < 1e-12
    sim.power = 1.0  # well behind every floor, so the boss's term is not at its floor
    deadlier = sim.death_chance(lair)
    lair.cities_fallen_before = 0
    assert sim.death_chance(lair) < deadlier

    flat = simulation(abomination_step=1.0)
    same = make_quest(flat, "Death", front_cities(flat)[0])
    same.cities_fallen_before = 3
    assert flat.abomination_multiplier(same) == 1.0, "CONTROL: a step of 1.0"
    basic = sim._make_dungeon(DungeonType.BASIC, city)
    assert sim.abomination_multiplier(basic) == 1.0, "CONTROL: a dungeon that is no lair"


def test_a_multiplier_of_one_is_the_old_death_chance() -> None:
    """`combat.death_chance` with `boss_score_multiplier` 1.0 is exactly the figure without it, on both paths."""
    common = dict(player_power=50.0, tier=1, dtype="Quest", subtype="Horde", modifier_score=0.0,
                  per_floor_risk=0.02, boss_risk_multiplier=3.0)
    for floors, strength in ((12, 0), (4, 12)):
        before = combat.death_chance(total_floors=floors, strength_floors=strength, **common)
        after = combat.death_chance(total_floors=floors, strength_floors=strength, boss_score_multiplier=1.0,
                                    **common)
        stronger = combat.death_chance(total_floors=floors, strength_floors=strength, boss_score_multiplier=2.0,
                                       **common)
        assert before == after
        assert stronger > before, "CONTROL: a larger multiplier makes the walk deadlier"


def test_the_switch_turns_lairs_off() -> None:
    sim = simulation(death_lairs=False)
    fell(sim, front_cities(sim)[0], "Death")
    assert lairs(sim) == [] and sim.lairs_on_a_fall == 0
    made = make_quest(sim, "Death", front_cities(sim)[0])
    assert made.cities_fallen_before == 0 and not sim.is_lair(made)
