"""Wins fall at every tier: the relief and the Last Stand's depth limit start at tier 2.

THE PROJECT OWNER, 2026-10-30: "Wins fall every tier". The content relief and the
Last Stand limit began at tier 4, which left tier 3 harder to win than tier 4.
The coordinating session ruled the same day to continue the relief's line down
-- 0.32 tier widths at tier 3, 0.19 at tier 2 -- and to limit the Last Stand to
120 floors at tier 3 and 110 at tier 2, which were measured to give a fully
invested Explorer wins that fall at every step from tier 1 to tier 8. Tier 1
has neither. `docs/DECISIONS.md`, 2026-10-30.

The hand-set case is a Basic dungeon of 80 floors with no modifiers and no
escalation on an Outpost at tier 3, against a player at the bottom of tier 3.
THE CONTROL is the same dungeon with the relief set to nothing.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import scoring
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation

NO_RELIEF = dict(content_relief_tier_widths=0.0, content_relief_per_tier_widths=0.0)


def simulation(tier: int, **changes) -> Simulation:
    cfg = replace(TuningConfig(), tier=tier, power_start_frac=0.0,
                  **changes).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.next_surge_day = float("inf")
    sim.dungeons.clear()
    return sim


def outpost(sim: Simulation):
    return next(c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST and not c.fallen)


def test_tiers_two_and_three_have_a_last_stand_limit_and_it_grows_with_the_tier():
    cfg = TuningConfig()
    assert (cfg.last_stand_floor_cap(2), cfg.last_stand_floor_cap(3)) == (110, 120)
    caps = [cfg.last_stand_floor_cap(t) for t in range(2, 9)]
    assert caps == sorted(caps) and len(set(caps)) == len(caps), (
        f"the limit no longer grows with the tier: {caps}")


def test_the_relief_grows_at_every_step_from_tier_one():
    cfg = TuningConfig()
    relief = [cfg.content_relief(t) for t in range(1, 9)]
    assert relief[0] == 0.0
    assert all(b > a for a, b in zip(relief, relief[1:], strict=False)), relief


def test_a_tier_two_last_stand_is_cut_to_its_limit():
    sim = simulation(2)
    city = outpost(sim)
    for _ in range(60):
        sim._make_dungeon(DungeonType.BASIC, city)
    sim._open_last_stand()
    assert sim.last_stand.floors == 110, (
        f"a tier 2 Last Stand over 60 dungeons has {sim.last_stand.floors} "
        "floors; the limit at tier 2 is 110. 'Wins fall every tier', 2026-10-30.")


def eighty_floors(sim: Simulation):
    d = sim._make_dungeon(DungeonType.BASIC, outpost(sim))
    d.subtype, d.modifier_names, d.modifier_score = "None", (), 0.0
    d.power_scale = 1.0
    d.floors = 80
    return d


def test_a_tier_three_dungeon_unsafe_without_the_relief_is_safe_with_it():
    """THE CASE, with its control first: without the relief the player at the
    bottom of tier 3 refuses the dungeon; with 0.32 tier widths it accepts it."""
    tolerance = TuningConfig().death_risk_tolerance
    without = simulation(3, **NO_RELIEF)
    with_it = simulation(3)
    assert without.power == with_it.power == scoring.tier_bounds(3)[0]
    assert without.death_chance(eighty_floors(without)) > tolerance, (
        "CONTROL: without the relief this dungeon must be unsafe")
    assert with_it.death_chance(eighty_floors(with_it)) <= tolerance, (
        "with the tier 3 relief of 0.32 tier widths the dungeon is still unsafe. "
        "'Wins fall every tier', 2026-10-30.")
