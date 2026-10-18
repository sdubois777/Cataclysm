"""A War Quest Siege: at most two a wave, a quarter of the bite, growth from its arrival.

THE OWNER, 2026-10-17, VERBATIM, choosing among measured options: "the first
option, but make it up to 2 a wave. So they player isn't just stuck there forever
waiting for the next surge to progress." `TuningConfig.quest_sieges_per_wave_max`
and `quest_siege_bite_scale` carry it, and `Dungeon.days_on_its_city` is what a
Siege's growth counts.

EVERY CASE HAS A CONTROL that the rule must leave alone.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(cataclysm: str, seed: int = 0, **over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.active_types = (cataclysm,)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def test_the_numbers_are_the_owners() -> None:
    cfg = TuningConfig()
    assert cfg.quest_sieges_per_wave_max == 2
    assert cfg.quest_siege_bite_scale == 0.25


def test_a_war_wave_carries_at_most_two_quest_dungeons() -> None:
    """Every roll asks for a Quest dungeon; a War wave of eight gets two and six
    ordinary ones. CONTROL: a Death wave of eight is eight Quest dungeons."""
    for seed in range(5):
        sim = simulation("War", seed=seed, quest_dungeon_chance=1.0,
                         surge_opening_count=8, surge_dungeon_count=8)
        sim.trigger_surge()
        kinds = [d.dtype for d in sim.dungeons.values()]
        assert len(kinds) == 8, kinds
        assert kinds.count(DungeonType.QUEST) == 2, (seed, kinds)
        assert kinds.count(DungeonType.BASIC) == 6, (seed, kinds)

        sim = simulation("Death", seed=seed, quest_dungeon_chance=1.0,
                         surge_opening_count=8, surge_dungeon_count=8)
        sim.trigger_surge()
        assert sim.dungeons and all(d.dtype is DungeonType.QUEST for d in sim.dungeons.values()), (
            "CONTROL: only a Cataclysm whose Quest dungeons are Sieges is capped")


def test_the_cap_is_per_wave() -> None:
    sim = simulation("War", quest_dungeon_chance=1.0, surge_opening_count=4, surge_dungeon_count=4)
    sim.trigger_surge()
    sim.trigger_surge()
    quests = [d for d in sim.dungeons.values() if d.dtype is DungeonType.QUEST]
    assert len(quests) == 4, "two waves, two each"


def two_outposts(sim: Simulation):
    outposts = [c for c in sim.empire.cities.values() if c.tier is CityTier.OUTPOST]
    return outposts[0], outposts[1]


def siege(sim: Simulation, dtype: DungeonType, city, spawned: int, arrived: int | None = None):
    d = sim._make_dungeon(dtype, city)
    d.subtype = "Siege"
    d.spawned_day = spawned
    d.arrived_day = arrived
    return d


def test_a_quest_siege_bites_a_quarter_of_an_ordinary_one() -> None:
    """Two Outposts, one Siege each, the same age. CONTROL: the ordinary one
    bites what the Siege row says, the 1% share plus 2.5 points a day."""
    sim = simulation("War")
    quest_city, basic_city = two_outposts(sim)
    siege(sim, DungeonType.QUEST, quest_city, spawned=0)
    siege(sim, DungeonType.BASIC, basic_city, spawned=0)
    sim.day = 10
    before = quest_city.defense, basic_city.defense
    sim._apply_siege_damage()
    quest_bite = before[0] - quest_city.defense
    basic_bite = before[1] - basic_city.defense
    cfg = sim.cfg
    assert basic_bite == (basic_city.max_defense * cfg.siege_defence_bite_per_day
                          + cfg.siege_damage_growth_per_day * 10)
    assert quest_bite == basic_bite * 0.25, (quest_bite, basic_bite)


def test_a_sieges_growth_counts_from_its_arrival_on_this_city() -> None:
    """A Quest Siege made on day 0 that arrived here on day 10 has stood here 0
    days on day 10, so only its flat share bites. CONTROL: one that never moved
    has stood 10."""
    sim = simulation("War")
    moved_city, stayed_city = two_outposts(sim)
    moved = siege(sim, DungeonType.QUEST, moved_city, spawned=0, arrived=10)
    stayed = siege(sim, DungeonType.QUEST, stayed_city, spawned=0)
    assert moved.days_on_its_city(10) == 0
    assert stayed.days_on_its_city(10) == 10
    sim.day = 10
    before = moved_city.defense, stayed_city.defense
    sim._apply_siege_damage()
    cfg = sim.cfg
    flat = moved_city.max_defense * cfg.siege_defence_bite_per_day * 0.25
    assert before[0] - moved_city.defense == flat
    assert before[1] - stayed_city.defense == flat + cfg.siege_damage_growth_per_day * 10 * 0.25


def test_a_fall_that_moves_a_quest_dungeon_records_the_day_it_arrived() -> None:
    sim = simulation("War")
    sim.cfg = replace(sim.cfg, surge_on_city_fall=False)
    city = next(c for c in sim.empire.exposed_cities() if c.cid != sim.empire.pillar_id)
    d = siege(sim, DungeonType.QUEST, city, spawned=0)
    sim.day = 40
    sim._fall(city)
    assert d.city_id != city.cid, "it should have moved"
    assert d.arrived_day == 40
    assert d.days_on_its_city(40) == 0


def test_the_policies_read_the_same_bite() -> None:
    """`policies.siege_daily_damage` is what the player plans by; it must give
    what `_apply_siege_damage` takes. CONTROL: the ordinary Siege."""
    sim = simulation("War")
    quest_city, basic_city = two_outposts(sim)
    q = siege(sim, DungeonType.QUEST, quest_city, spawned=0)
    b = siege(sim, DungeonType.BASIC, basic_city, spawned=0)
    sim.day = 10
    planned = policies.siege_daily_damage(sim, q, quest_city), policies.siege_daily_damage(sim, b, basic_city)
    before = quest_city.defense, basic_city.defense
    sim._apply_siege_damage()
    assert planned == (before[0] - quest_city.defense, before[1] - basic_city.defense)
    assert planned[0] == planned[1] * 0.25
