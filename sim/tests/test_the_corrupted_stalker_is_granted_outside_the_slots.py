"""The Corrupted Stalker is granted by a flat 5% chance on any dungeon, outside the slots.

THE RULE. The project owner decided on 2026-09-05, recorded 2026-10-24 in
`docs/DECISIONS.md` under "2026-09-05 (recorded 2026-10-24) -- The Corrupted
Stalker is granted by a flat 5% chance on any dungeon, outside the modifier
slots": every dungeon has an independent 5% chance of carrying it, it is never
drawn into a slot, and it still adds its weight of 20 to the Modifier Score.
Issue #1308 is the building.

TWO RULINGS OF THE COORDINATING SESSION, 2026-10-25. The roll is on a stream of
its own, so the shared `rng` does not move and every slot draw stays what it
was; and the chance is flat, Sacrificial and Cataclysm dungeons included.

EVERY CASE HAS A CONTROL: the same board with the chance at 0.
"""

from __future__ import annotations

import dataclasses
import math
import pathlib

from cataclysm_sim.config import CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation
from cataclysm_sim.modifiers import CORRUPTED_STALKER, GENERIC, MODIFIERS

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
NAME, WEIGHT = CORRUPTED_STALKER

#: Enough dungeons that a binomial band at five standard deviations is narrow.
DUNGEONS = 4000


def simulation(chance: float, seed: int = 11, tier: int = 3) -> Simulation:
    cfg = dataclasses.replace(TuningConfig(), tier=tier,
                              corrupted_stalker_chance=chance)
    return Simulation(cfg, seed=seed)


def an_outpost(sim: Simulation):
    return next(c for c in sim.empire.cities.values()
                if c.tier == CityTier.OUTPOST)


def dungeons(sim: Simulation, count: int = DUNGEONS):
    city = an_outpost(sim)
    return [sim._make_dungeon(DungeonType.BASIC, city) for _ in range(count)]


def test_the_chance_is_the_owners_five_percent():
    assert TuningConfig().corrupted_stalker_chance == 0.05
    design = (REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    assert "Every dungeon has an independent 5% chance of carrying this modifier" in design, (
        "the design document no longer states the 5% chance this pins")


def test_the_row_granted_is_the_tables_generic_row_at_weight_twenty():
    assert MODIFIERS[GENERIC] == [CORRUPTED_STALKER]
    assert (NAME, WEIGHT) == ("Corrupted Stalker", 20)


def test_about_one_dungeon_in_twenty_carries_it():
    made = dungeons(simulation(0.05))
    carrying = sum(1 for d in made if NAME in d.modifier_names)
    mean = DUNGEONS * 0.05
    band = 5 * math.sqrt(DUNGEONS * 0.05 * 0.95)
    assert mean - band <= carrying <= mean + band, (
        f"{carrying} of {DUNGEONS} dungeons carried it; 5% is {mean:.0f}, "
        f"and five standard deviations either side is {band:.0f}")


def test_a_carrying_dungeon_holds_it_once_and_is_worth_twenty_more():
    made = dungeons(simulation(0.05))
    control = dungeons(simulation(0.0))
    carriers = 0
    for d, plain in zip(made, control, strict=True):
        if NAME in d.modifier_names:
            carriers += 1
            assert d.modifier_names.count(NAME) == 1
            assert d.modifier_score == plain.modifier_score + WEIGHT
        else:
            assert d.modifier_score == plain.modifier_score
    assert carriers > 0, "CONTROL: no dungeon carried it, so nothing above was checked"


def test_the_grant_moves_no_other_draw():
    """The ruling of 2026-10-25: the slot draws, the depths and everything
    else drawn on `rng` are the same with the grant as without it."""
    with_grant = simulation(0.05)
    without = simulation(0.0)
    made = dungeons(with_grant)
    control = dungeons(without)
    for d, plain in zip(made, control, strict=True):
        assert tuple(n for n in d.modifier_names if n != NAME) == plain.modifier_names
        assert (d.floors, d.subtype, d.resolve_max) == (plain.floors, plain.subtype, plain.resolve_max)
    assert with_grant.rng.getstate() == without.rng.getstate()


def test_a_sacrificial_dungeon_carries_it_once_at_most_and_at_the_same_rate():
    """Flat, ruled 2026-10-25: Sacrificial doubles the slots and not this."""
    made = dungeons(simulation(0.05))
    sacrificial = [d for d in made if d.subtype == "Sacrificial"]
    assert sacrificial, "CONTROL: no Sacrificial dungeon was made, so nothing was checked"
    assert all(d.modifier_names.count(NAME) <= 1 for d in sacrificial)
    assert any(NAME in d.modifier_names for d in sacrificial), (
        f"none of {len(sacrificial)} Sacrificial dungeons carried it")


def test_with_the_chance_at_nought_no_dungeon_carries_it():
    assert not any(NAME in d.modifier_names for d in dungeons(simulation(0.0)))


def test_the_same_seed_grants_it_to_the_same_dungeons_and_another_seed_does_not():
    first = [NAME in d.modifier_names for d in dungeons(simulation(0.05, seed=4))]
    again = [NAME in d.modifier_names for d in dungeons(simulation(0.05, seed=4))]
    other = [NAME in d.modifier_names for d in dungeons(simulation(0.05, seed=5))]
    assert first == again
    assert first != other
