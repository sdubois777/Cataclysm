"""Every enemy score at tier 4 and up is lowered in the comparison with the
player's power, by a relief that grows with the tier.

ISSUE #1425. The project owner, 2026-10-29, in three answers: "Check gear
first", "Weaken tier content", "Relief grows by tier". The relief is

    content_relief_tier_widths + content_relief_per_tier_widths x (tier - 4)

tier widths, 0.45 to 0.97 across tiers 4 to 8 and none at tiers 1 to 3. It is
applied in `Simulation.death_chance` and `Simulation.dungeon_power` and nowhere
else; `scoring.py` is a copy of another repository's file and is not touched.

The hand-set case is a Basic dungeon of 80 floors with no modifiers and no
escalation, on an Outpost at tier 4, against a player at the bottom of tier 4
(`power_start_frac` 0.0, the end-of-tier-3 ceiling the design says a player
arrives with). THE CONTROL is the same dungeon with the relief set to nothing.
"""
from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import combat, scoring
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import SCORING_TYPE, Simulation

NO_RELIEF = dict(content_relief_tier_widths=0.0, content_relief_per_tier_widths=0.0)


def config(tier: int, relief: bool = True) -> TuningConfig:
    cfg = replace(TuningConfig(), tier=tier, power_start_frac=0.0).with_tree(TREE_NONE)
    return cfg if relief else replace(cfg, **NO_RELIEF)


def hand_set(cfg: TuningConfig, floors: int = 80):
    """One plain Basic dungeon on an Outpost, with nothing else on the board."""
    sim = Simulation(cfg, seed=0)
    sim.dungeons.clear()
    city = next(c for c in sim.empire.cities.values() if c.tier is CityTier.OUTPOST)
    d = sim._make_dungeon(DungeonType.BASIC, city)
    d.subtype, d.modifier_names, d.modifier_score = "None", (), 0.0
    d.power_scale = 1.0
    d.floors = floors
    return sim, d


def test_there_is_no_relief_at_tiers_one_to_three():
    cfg = TuningConfig()
    assert [cfg.content_relief(t) for t in (1, 2, 3)] == [0.0, 0.0, 0.0]


def test_the_relief_follows_the_ruled_formula_at_tiers_four_to_eight():
    cfg = TuningConfig()
    assert [cfg.content_relief(t) for t in range(4, 9)] == pytest.approx(
        [0.45, 0.58, 0.71, 0.84, 0.97])
    assert replace(cfg, tier=6).content_relief() == pytest.approx(0.71), (
        "with no argument it reads the campaign's own tier")


def test_a_tier_four_dungeon_unsafe_without_the_relief_is_safe_with_it():
    """THE CASE. Without the relief the player at the bottom of tier 4 refuses
    the dungeon; with it, the dungeon is inside the risk the policies accept."""
    tolerance = TuningConfig().death_risk_tolerance
    without, d0 = hand_set(config(4, relief=False))
    with_it, d1 = hand_set(config(4))
    assert d0.floors == d1.floors == 80
    assert without.power == with_it.power == scoring.tier_bounds(4)[0]
    assert without.death_chance(d0) > tolerance, (
        "CONTROL: without the relief this dungeon must be unsafe, or the case "
        "below proves nothing")
    assert with_it.death_chance(d1) <= tolerance, (
        f"with the relief of {with_it.cfg.content_relief():.2f} tier widths the "
        f"dungeon still kills {with_it.death_chance(d1):.3f} of the time, above "
        f"the tolerance {tolerance}. Issue #1425.")


def test_the_relief_is_the_players_power_raised_by_that_many_tier_widths():
    """The engine's arithmetic, stated: (score - r x width) x scale compared
    with power is score compared with power / scale + r x width."""
    sim, d = hand_set(config(5))
    d.power_scale = 1.3
    cfg = sim.cfg
    expected = combat.death_chance(
        player_power=sim.power / d.power_scale + cfg.content_relief() * sim.tier_width,
        tier=5, dtype=SCORING_TYPE[d.dtype], subtype=d.subtype,
        total_floors=d.floors, modifier_score=d.modifier_score,
        per_floor_risk=cfg.per_floor_risk,
        boss_risk_multiplier=cfg.boss_risk_multiplier,
        rate=cfg.overwhelm_rate, cap=cfg.overwhelm_cap)
    assert sim.death_chance(d) == pytest.approx(expected)


def test_the_headline_power_is_lowered_by_the_same_relief():
    without, d0 = hand_set(config(4, relief=False))
    with_it, d1 = hand_set(config(4))
    drop = without.dungeon_power(d0) - with_it.dungeon_power(d1)
    assert drop == pytest.approx(0.45 * scoring.tier_width(4))


@pytest.mark.parametrize("tier", [1, 2, 3])
def test_tiers_one_to_three_are_untouched_whatever_the_relief_holds(tier):
    """Even a large relief changes nothing below its first tier, so every
    figure measured at tiers 1 to 3 before this change still holds."""
    plain, d0 = hand_set(config(tier, relief=False), floors=40)
    large = replace(config(tier), content_relief_tier_widths=2.0,
                    content_relief_per_tier_widths=1.0)
    raised, d1 = hand_set(large, floors=40)
    assert raised.death_chance(d1) == plain.death_chance(d0)
    assert raised.dungeon_power(d1) == plain.dungeon_power(d0)
