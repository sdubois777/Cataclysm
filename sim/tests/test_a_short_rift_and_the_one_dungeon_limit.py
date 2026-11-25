"""The Rift itself, as the owner ruled it on 2026-11-24 (issue #51), and the variants measured on the way.

THE RULING, verbatim from the newest comment on issue #51: "let's make them 4 floors?", "Keep the short timer for
rifts. It's only fair, it has 4 floors.", "Rifts always have 8 dungeon modifiers. Keep it spicy", "No limit" and "(b) 8
modifiers only (Recommended)". So a Rift is exactly 4 floors (`rift_floors`), with the quest timer of 4 floors
(`rift_timer_from_quest_depth` False), exactly 8 dungeon modifiers (`rift_modifiers`) and the enemies of an ordinary
4-floor dungeon (`rift_strength_from_quest_depth` False). Those are the defaults; `demonic_rifts` itself ships off.

THE VARIANTS STAY MEASURABLE: the rolled depth's timer, the rolled depth's enemies (Rift floor i fights floor
ceil(i x rolled depth / 4) of a dungeon that deep), and the one-dungeon limit (`rift_one_sent_at_a_time`, a Rift sends
only while none of the dungeons it sent still stands), which the owner refused: "No limit".

EACH TEST HAS A CONTROL, and each names the switches it needs rather than leaning on the defaults.
"""
from __future__ import annotations

import math
from dataclasses import replace

from cataclysm_sim import combat, scoring
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def simulation(seed: int = 0, **over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1,
                  **{"demonic_rifts": True, "surge_on_empty_board": False, **over}).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    sim.active_types = ("Demonic",)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def quest_on(sim: Simulation, tier: CityTier, source: str = "Demonic"):
    city = next(c for c in sim.empire.cities.values() if c.tier is tier)
    sim.making_for = source
    try:
        d = sim._make_dungeon(DungeonType.QUEST, city)
    finally:
        sim.making_for = ""
    d.source = source
    return d


def test_the_switches_default_to_the_ruling() -> None:
    cfg = TuningConfig()
    assert cfg.demonic_rifts is False, "the Rifts ship on before the game has them"
    assert cfg.rift_floors == 4
    assert cfg.rift_timer_from_quest_depth is False
    assert cfg.rift_strength_from_quest_depth is False
    assert cfg.rift_modifiers == 8
    assert cfg.rift_one_sent_at_a_time is False


def test_a_rift_made_with_the_defaults_is_the_ruled_one() -> None:
    """4 floors, a 4-floor dungeon's enemies, 8 modifiers, the timer of 4 floors. The control is the same Rift made
    as deep as any Quest dungeon, which keeps its rolled depth and its longer timer."""
    from cataclysm_sim.modifiers import pool_for
    cfg = TuningConfig()
    for seed in range(10):
        sim = simulation(seed=seed)
        sim.modifier_pool = pool_for(sim.active_types)
        d = quest_on(sim, CityTier.BULWARK)
        assert (d.floors, d.strength_floors, d.tree_floors, d.run_days) == (4, 0, 0, 4)
        assert len(slotted(d)) == 8
        assert d.resolve_max <= int((cfg.resolve_base_days + 4 * cfg.resolve_floor_ratio) * (1 + cfg.resolve_jitter))
        deep = quest_on(simulation(seed=seed, rift_floors=0), CityTier.BULWARK)
        assert deep.floors > 4 and deep.resolve_max > d.resolve_max


# -- eight modifiers ------------------------------------------------------------------------------------------------


def slotted(d) -> list[str]:
    """The modifiers in a dungeon's slots: the Corrupted Stalker is granted outside them."""
    return [n for n in d.modifier_names if n != "Corrupted Stalker"]


def test_a_rift_carries_eight_different_demonic_modifiers_at_tier_one() -> None:
    from cataclysm_sim.modifiers import MODIFIERS, pool_for
    demonic = {n for n, _ in MODIFIERS["Demonic"]}
    assert len(demonic) == 14, "Demonic's column, which is the whole pool at tier 1, has changed size"
    for seed in range(30):
        sim = simulation(seed=seed, rift_floors=4, rift_modifiers=8)
        # THE POOL A TIER 1 DEMONIC CAMPAIGN HAS. `simulation` sets the active Cataclysm after the pool was built from
        # the seed's own.
        sim.modifier_pool = pool_for(sim.active_types)
        assert {n for n, _ in sim.modifier_pool} == demonic
        d = quest_on(sim, CityTier.OUTPOST)
        names = slotted(d)
        assert len(names) == 8 and len(set(names)) == 8, names
        assert set(names) <= demonic


def test_control_without_the_switch_a_tier_one_rift_carries_one() -> None:
    d = quest_on(simulation(rift_modifiers=0), CityTier.OUTPOST)
    assert len(slotted(d)) == 1


def test_control_an_ordinary_dungeon_beside_it_still_carries_one() -> None:
    sim = simulation(rift_floors=4, rift_modifiers=8)
    city = next(c for c in sim.empire.cities.values() if c.tier is CityTier.OUTPOST)
    sim.making_for = "Demonic"
    try:
        basic = sim._make_dungeon(DungeonType.BASIC, city)
    finally:
        sim.making_for = ""
    assert len(slotted(basic)) == 1


def test_a_rift_carries_eight_at_tier_four_too() -> None:
    for seed in range(10):
        cfg = replace(TuningConfig(), tier=4, demonic_rifts=True, rift_floors=4, rift_modifiers=8,
                      surge_on_empty_board=False).with_tree(TREE_NONE)
        sim = Simulation(cfg, seed=seed)
        sim.active_types = ("Demonic",)
        assert len(slotted(quest_on(sim, CityTier.OUTPOST))) == 8


def test_with_its_own_strength_a_short_rift_fights_its_own_four_floors() -> None:
    d = quest_on(simulation(rift_floors=4, rift_strength_from_quest_depth=False), CityTier.BULWARK)
    assert (d.floors, d.strength_floors, d.tree_floors) == (4, 0, 0)
    control = quest_on(simulation(rift_strength_from_quest_depth=True), CityTier.BULWARK)
    assert control.strength_floors > 4


# -- the short Rift -------------------------------------------------------------------------------------------------


def test_a_short_rift_has_four_floors_and_keeps_its_rolled_depth_as_its_strength() -> None:
    for seed in range(20):
        deep = quest_on(simulation(seed=seed, rift_floors=0), CityTier.BULWARK)
        short = quest_on(simulation(seed=seed, rift_strength_from_quest_depth=True), CityTier.BULWARK)
        assert (short.floors, short.run_days, short.tree_floors) == (4, 4, 0)
        assert short.strength_floors == deep.floors, "the strength is not the depth the Rift was rolled at"
        lo, hi = TuningConfig().spec(DungeonType.QUEST, CityTier.BULWARK).floors
        assert lo <= short.strength_floors <= hi
        assert deep.strength_floors == 0


def test_control_only_a_demonic_quest_dungeon_with_the_rifts_on_is_shortened() -> None:
    assert quest_on(simulation(rift_floors=4, demonic_rifts=False), CityTier.OUTPOST).floors > 4
    sim = simulation(rift_floors=4)
    sim.active_types = ("Death",)
    assert quest_on(sim, CityTier.OUTPOST, source="Death").floors > 4


def test_a_short_rift_keeps_the_timer_of_its_rolled_depth() -> None:
    for seed in range(20):
        deep = quest_on(simulation(seed=seed, rift_floors=0), CityTier.OUTPOST)
        short = quest_on(simulation(seed=seed, rift_timer_from_quest_depth=True), CityTier.OUTPOST)
        assert short.resolve_max == deep.resolve_max


def test_control_with_its_own_timer_a_short_rift_runs_out_sooner_by_the_depth_ratio() -> None:
    cfg = TuningConfig()
    for seed in range(20):
        deep = quest_on(simulation(seed=seed, rift_floors=0), CityTier.OUTPOST)
        short = quest_on(simulation(seed=seed, rift_floors=4, rift_timer_from_quest_depth=False), CityTier.OUTPOST)
        ratio = ((cfg.resolve_base_days + 4 * cfg.resolve_floor_ratio)
                 / (cfg.resolve_base_days + deep.floors * cfg.resolve_floor_ratio))
        assert short.resolve_max < deep.resolve_max
        assert abs(short.resolve_in - deep.resolve_in * ratio) < 1e-6, "the same jitter was not used"


def test_shortening_a_rift_takes_no_draw() -> None:
    a = simulation(seed=3, rift_floors=0)
    b = simulation(seed=3, rift_floors=4)
    quest_on(a, CityTier.OUTPOST)
    quest_on(b, CityTier.OUTPOST)
    assert a.rng.random() == b.rng.random()


def test_rift_floor_i_fights_floor_ceil_i_times_depth_over_four() -> None:
    """Worked by hand for a 25-deep Rift: its four floors fight floors 7, 13, 19 and 25, and its boss is floor 25's.
    The control below is the same function given a strength of 0, which walks 4 floors of a 4-floor dungeon."""
    cfg = TuningConfig()
    tier, power, mod = 1, scoring.tier_bounds(1)[0], 30.0
    tw = scoring.tier_width(tier)
    survive = 1.0
    for floor in (7, 13, 19, 25):
        threat = combat.floor_threat(tier, "Quest", "Elite", 25, floor, mod)
        risk = cfg.per_floor_risk * (combat.damage_multiplier(combat.overwhelm(power, threat, tw)) - 1.0)
        survive *= max(0.0, 1.0 - risk)
    boss = scoring.enemy_scores(tier, "Quest", "Elite", 25, 25, mod)["Boss"]
    boss_risk = min(0.95, cfg.per_floor_risk * cfg.boss_risk_multiplier
                    * (combat.damage_multiplier(combat.overwhelm(power, boss, tw)) - 1.0))
    survive *= max(0.05, 1.0 - boss_risk)

    def chance(strength):
        return combat.death_chance(power, tier, "Quest", "Elite", 4, mod, cfg.per_floor_risk,
                                   cfg.boss_risk_multiplier, strength_floors=strength)

    assert [math.ceil(i * 25 / 4) for i in (1, 2, 3, 4)] == [7, 13, 19, 25]
    assert abs(chance(25) - (1.0 - survive)) < 1e-12
    assert chance(25) > chance(0), "a short Rift is no harder than a plain 4-floor dungeon"


def test_a_short_rift_is_less_deadly_than_walking_its_whole_depth() -> None:
    """Fewer floors walked: the same boss, four floors of risk instead of 25."""
    cfg = TuningConfig()
    power = scoring.tier_bounds(1)[0]
    short = combat.death_chance(power, 1, "Quest", "Elite", 4, 30.0, cfg.per_floor_risk,
                                cfg.boss_risk_multiplier, strength_floors=25)
    deep = combat.death_chance(power, 1, "Quest", "Elite", 25, 30.0, cfg.per_floor_risk,
                               cfg.boss_risk_multiplier)
    assert 0.0 < short < deep


def test_a_strength_equal_to_the_floors_is_the_ordinary_path() -> None:
    cfg = TuningConfig()
    power = scoring.tier_bounds(1)[0]
    args = (power, 1, "Quest", "Elite", 25, 30.0, cfg.per_floor_risk, cfg.boss_risk_multiplier)
    assert combat.death_chance(*args, strength_floors=25) == combat.death_chance(*args)


def test_the_simulation_reads_the_strength() -> None:
    sim = simulation(rift_strength_from_quest_depth=True)
    short = quest_on(sim, CityTier.OUTPOST)
    sim.power = sim.tier_min
    plain = replace(short, strength_floors=0)
    assert sim.death_chance(short) > sim.death_chance(plain)
    assert sim.dungeon_power(short) > sim.dungeon_power(plain)


# -- the one-dungeon limit ------------------------------------------------------------------------------------------


def run_days(sim: Simulation, n: int) -> None:
    for _ in range(n):
        sim._empire_day()


def rift_with_timer_out_of_the_way(sim: Simulation):
    d = quest_on(sim, CityTier.OUTPOST)
    d.resolve_max = 10_000
    d.resolve_in = 10_000.0
    return d


def sent(sim: Simulation):
    return [d for d in sim.dungeons.values() if d.dtype is DungeonType.BASIC]


def test_a_rift_sends_no_second_dungeon_while_its_first_stands() -> None:
    sim = simulation(rift_one_sent_at_a_time=True)
    rift_with_timer_out_of_the_way(sim)
    run_days(sim, 5)
    first = sent(sim)
    assert len(first) == 1
    first[0].resolve_in = 10_000.0
    run_days(sim, 5)
    assert sim.rift_spawns == 1, "a Rift sent a second dungeon while its first stood"
    sim.dungeons.pop(first[0].did)
    run_days(sim, 5)
    assert sim.rift_spawns == 2, "a Rift whose dungeon was cleared sent nothing on its next day"


def test_control_without_the_limit_it_sends_on_its_tenth_day() -> None:
    sim = simulation()
    rift_with_timer_out_of_the_way(sim)
    run_days(sim, 5)
    sent(sim)[0].resolve_in = 10_000.0
    run_days(sim, 5)
    assert sim.rift_spawns == 2
