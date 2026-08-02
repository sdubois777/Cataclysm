"""Overwhelm: what happens when you fight above your Power Score.

There is no hard gate. Enemies above your Power Score strip your mitigation --
all of it, not just resistances, so the mechanic lands on armour, block and
evasion builds the same way it lands on a resistance stacker:

    gap       = max(0, enemyScore - playerScore)
    overwhelm = min(CAP, RATE * gap / tierWidth)

Rated against TIER WIDTH rather than a flat point step, because a flat step is
worth 17% of a T1 tier but only 6% of a T8 one -- which made a maxed T1 player
eat 13% penetration at their own final boss while a maxed T8 player ate 47%.
"""

from __future__ import annotations

import math

from . import scoring

# Baseline mitigation a geared player is assumed to be sitting on (GDD caps
# resistances at 70%). Used to convert overwhelm into a damage multiplier.
BASE_MITIGATION = 0.70

OVERWHELM_RATE = 0.25       # mitigation lost per 1.0x tier width of shortfall
OVERWHELM_CAP = 0.50


def overwhelm(player_power: float, enemy_score: float, tier_width: float,
              rate: float = OVERWHELM_RATE, cap: float = OVERWHELM_CAP) -> float:
    if tier_width <= 0:
        return 0.0
    gap = max(0.0, enemy_score - player_power)
    return min(cap, rate * gap / tier_width)


def damage_multiplier(ow: float, base_mitigation: float = BASE_MITIGATION) -> float:
    """How much harder you get hit relative to sitting at your mitigation cap."""
    eff = max(0.0, base_mitigation - ow)
    return (1.0 - eff) / (1.0 - base_mitigation)


# Floors are sampled rather than walked one at a time -- a 150-floor dungeon
# evaluated across thousands of campaigns is otherwise the whole runtime.
_SAMPLE_FRACS = (0.1, 0.25, 0.4, 0.55, 0.7, 0.85, 0.95)


def floor_threat(tier: int, dtype: str, subtype: str, total_floors: int,
                 floor: int, modifier_score: float) -> float:
    """Expected enemy Power Score on one floor, rarity-weighted."""
    s = scoring.enemy_scores(tier, dtype, subtype, total_floors, floor,
                             modifier_score)
    return sum(s[r] * w for r, w in scoring.DUNGEON_SCORE_MIX)


def death_chance(player_power: float, tier: int, dtype: str, subtype: str,
                 total_floors: int, modifier_score: float,
                 per_floor_risk: float, boss_risk_multiplier: float,
                 boss_rarity: str = "Boss",
                 rate: float = OVERWHELM_RATE,
                 cap: float = OVERWHELM_CAP) -> float:
    """Probability of dying at least once on the way through.

    Risk on a floor is proportional to how much harder Overwhelm makes you get
    hit. A player who out-powers every floor takes no risk at all.
    """
    tw = scoring.tier_width(tier)
    survive = 1.0

    # Ordinary floors, sampled and weighted by how many floors each stands for.
    seg = total_floors / len(_SAMPLE_FRACS)
    for frac in _SAMPLE_FRACS:
        floor = max(1, min(total_floors, round(total_floors * frac)))
        threat = floor_threat(tier, dtype, subtype, total_floors, floor,
                              modifier_score)
        ow = overwhelm(player_power, threat, tw, rate, cap)
        risk = per_floor_risk * (damage_multiplier(ow) - 1.0)
        if risk > 0:
            survive *= math.pow(max(0.0, 1.0 - risk), seg)

    # The thing on the last floor.
    boss = scoring.enemy_scores(tier, dtype, subtype, total_floors,
                                total_floors, modifier_score)[boss_rarity]
    ow = overwhelm(player_power, boss, tw, rate, cap)
    boss_risk = min(0.95, per_floor_risk * boss_risk_multiplier
                    * (damage_multiplier(ow) - 1.0))
    survive *= max(0.05, 1.0 - boss_risk)

    return 1.0 - survive
