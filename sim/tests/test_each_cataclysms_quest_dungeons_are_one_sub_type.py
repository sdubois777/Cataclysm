"""Each Cataclysm's Quest dungeons are one sub-type, not rolled.

THE OWNER, 2026-10-16, VERBATIM: "Oh I should be more clear. War quest dungeons
WILL be sieges. Each cataclysm's quest dungeons will be one specific type." and
"I really thought you would have picked horde for dead rising... as the whole
undead zombie swarm thing kinda makes sense there. I assumed hell on earth would
be Elite." The mapping is `TuningConfig.QUEST_SUBTYPE_BY_CATACLYSM`; Ascendant and
Hollow are new names whose rules come later. A War Quest dungeon re-draws its city
away from a besieged one (ruled by the coordinating session the same day).

EVERY CASE HAS A CONTROL: an ordinary dungeon of the same wave, which still rolls.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation
from cataclysm_sim.patterns import PATTERNS

MAPPING = {"War": "Siege", "Death": "Horde", "Demonic": "Elite", "Pestilence": "Timed",
           "Famine": "Sacrificial", "Chaos": "Volatile", "Celestial": "Ascendant", "Void": "Hollow"}


def simulation(cataclysm: str, seed: int = 0, **over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.active_types = (cataclysm,)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def test_the_mapping_is_the_owners() -> None:
    assert TuningConfig().QUEST_SUBTYPE_BY_CATACLYSM == MAPPING


def test_every_quest_dungeon_a_surge_makes_takes_its_cataclysms_type() -> None:
    for cataclysm, subtype in MAPPING.items():
        sim = simulation(cataclysm, quest_dungeon_chance=1.0)
        sim.trigger_surge()
        quests = [d for d in sim.dungeons.values() if d.dtype is DungeonType.QUEST]
        assert quests, f"{cataclysm}: the surge made no Quest dungeon"
        assert {d.subtype for d in quests} == {subtype}, (cataclysm, {d.subtype for d in quests})
        assert all(d.source == cataclysm for d in quests)


def test_control_an_ordinary_dungeon_still_rolls_and_never_takes_a_new_type() -> None:
    seen = set()
    for seed in range(30):
        sim = simulation("Void", seed=seed, quest_dungeon_chance=0.0)
        sim.trigger_surge()
        seen |= {d.subtype for d in sim.dungeons.values() if d.dtype is DungeonType.BASIC}
    assert len(seen) > 1, "ordinary dungeons came out one sub-type, so they are not rolling"
    assert not seen & {"Ascendant", "Hollow"}, seen


def test_a_quest_dungeon_made_without_a_cataclysm_still_rolls() -> None:
    sim = simulation("War")
    city = next(c for c in sim.empire.cities.values() if c.tier is CityTier.OUTPOST)
    kinds = {sim._make_dungeon(DungeonType.QUEST, city).subtype for _ in range(60)}
    assert len(kinds) > 1, kinds


def test_a_war_quest_dungeon_redraws_its_city_away_from_a_siege() -> None:
    """Every target but one holds a Siege: a War Quest dungeon lands on that one.
    ONE DUNGEON, because once it lands the free city holds a Siege too, and the
    next would land anyway. CONTROL: with every target besieged it lands anyway,
    as a Siege."""
    sim = simulation("War", quest_dungeon_chance=1.0, surge_dungeon_count=1, surge_opening_count=1)
    targets = [c for c in sim.empire.exposed_cities() if c.cid != sim.empire.pillar_id]
    # THE FREE CITY MUST BE ONE WAR AIMS AT: its pattern gives some cities no weight.
    free = next(c for c in reversed(targets) if PATTERNS["War"].weight(sim.empire, c) > 0)
    for c in [c for c in targets if c is not free]:
        sim._make_dungeon(DungeonType.BASIC, c).subtype = "Siege"
    sim.trigger_surge()
    new = [d for d in sim.dungeons.values() if d.dtype is DungeonType.QUEST]
    assert new and all(d.subtype == "Siege" for d in new)
    assert {d.city_id for d in new} == {free.cid}, (
        f"War Quest dungeons landed on {sorted({d.city_id for d in new})}; the only city without a Siege is {free.cid}")

    sim = simulation("War", quest_dungeon_chance=1.0, surge_dungeon_count=2, surge_opening_count=2)
    for c in [c for c in sim.empire.exposed_cities() if c.cid != sim.empire.pillar_id]:
        sim._make_dungeon(DungeonType.BASIC, c).subtype = "Siege"
    sim.trigger_surge()
    new = [d for d in sim.dungeons.values() if d.dtype is DungeonType.QUEST]
    assert new and all(d.subtype == "Siege" for d in new), "CONTROL: it lands anyway, as a Siege"
