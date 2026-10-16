"""A Quest dungeon never rolls a Siege; an ordinary one still can. INTERIM.

THE OWNER, 2026-10-16, VERBATIM: "The only quest dungeons that will be sieges are Wars. So this should be fine and provide the player with a bit of an incentive to address them asap."
And then: "Oh I should be more clear. War quest dungeons WILL be sieges. Each cataclysm's quest dungeons will be one specific type."
So a Quest dungeon's sub-type will be set by its Cataclysm and War's will be a
Siege. Until the other Cataclysms' types are named the model rolls them as
before, without the Siege, because War is not built. Built as one more row of
`TuningConfig.SUBTYPES_FORBIDDEN_ON`, beside the Cataclysm's Cow Level: the roll
draws from the other sub-types, which share the excluded weight in proportion,
and costs no extra draw.

EVERY CASE HAS A CONTROL: an ordinary dungeon under the same roll.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(seed: int = 0) -> Simulation:
    cfg = replace(TuningConfig(), tier=1).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.next_surge_day = float("inf")
    sim.dungeons.clear()
    return sim


def an_outpost(sim: Simulation):
    return next(c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST and not c.fallen)


def test_the_kinds_lists_differ_only_by_the_siege() -> None:
    sim = simulation()
    quest = sim.subtypes_allowed_on(DungeonType.QUEST)
    ordinary = sim.subtypes_allowed_on(DungeonType.BASIC)
    assert "Siege" not in quest
    assert "Siege" in ordinary, "CONTROL: an ordinary dungeon may still roll a Siege"
    assert [n for n in ordinary if n != "Siege"] == quest
    assert "Cow Level" not in sim.subtypes_allowed_on(DungeonType.CATACLYSM), (
        "the Cataclysm's own exclusion still holds")


def test_a_draw_that_would_be_a_siege_is_not_one_for_a_quest_dungeon() -> None:
    """The stream is replaced by one that takes the Siege whenever it is
    offered: the ordinary roll comes out a Siege, the Quest roll cannot."""
    class SiegeWhenOffered:
        def choices(self, population, weights=None, k=1):
            return ["Siege"] if "Siege" in population else [population[0]]

    sim = simulation()
    city = an_outpost(sim)
    sim.rng = SiegeWhenOffered()
    assert sim._roll_subtype(DungeonType.BASIC, city) == "Siege", "CONTROL"
    assert sim._roll_subtype(DungeonType.QUEST, city) != "Siege"


def test_over_many_rolls_no_quest_dungeon_is_a_siege() -> None:
    quest_sieges = ordinary_sieges = 0
    for seed in range(20):
        sim = simulation(seed)
        city = an_outpost(sim)
        for _ in range(50):
            quest_sieges += sim._roll_subtype(DungeonType.QUEST, city) == "Siege"
            ordinary_sieges += sim._roll_subtype(DungeonType.BASIC, city) == "Siege"
    assert quest_sieges == 0
    assert ordinary_sieges > 0, "CONTROL: 1,000 ordinary rolls gave no Siege, so this compares nothing"
