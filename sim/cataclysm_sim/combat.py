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


# --------------------------------------------------------------------------
# What power a dungeon actually asks for
# --------------------------------------------------------------------------
#
# Issue #8. The report in `experiments.py` invited the reader to compare every
# power figure against a hard-coded range that had been derived against the
# player power anchors issue #2 replaced, and never re-derived.
#
# THERE IS NO SINGLE NUMBER, and that is the first thing to say. Death chance
# falls smoothly with power and never reaches zero at any power the tier allows,
# because the thing on the last floor of a Cataclysm dungeon outscores the
# maximum player power of its own tier: 2.0 times at tier 1, falling to 1.2
# times at tier 8. So the question "what power clears it" only has an answer
# once a death chance is named.

#: The death chance the reported threshold is quoted at. A run that dies in the
#: Cataclysm dungeon is over -- see `Engine._resolve_dungeon` -- so this is a
#: run-ending risk rather than a setback, and one in ten is a reasonable reading
#: of "can attempt it". It is a reporting choice, not a design rule.
REPORTED_DEATH_CHANCE = 0.10


def power_for_death_chance(target: float, tier: int, dtype: str, subtype: str,
                           total_floors: int, modifier_score: float,
                           per_floor_risk: float, boss_risk_multiplier: float,
                           boss_rarity: str = "Boss",
                           rate: float = OVERWHELM_RATE,
                           cap: float = OVERWHELM_CAP) -> float:
    """The player Power Score at which this dungeon kills you `target` of the time.

    Found by bisection, because `death_chance` compounds seven sampled floors
    and a boss and has no closed form. It is monotonically decreasing in power,
    which is what makes bisection valid: more power never raises the risk.

    Returns 0.0 if the dungeon is already safer than `target` at no power at all.
    The upper bound is open-ended rather than the tier ceiling, because the
    answer is routinely ABOVE that ceiling and clamping it would hide exactly
    the fact this exists to report.
    """
    if not 0.0 < target < 1.0:
        raise ValueError(f"target death chance must be between 0 and 1, "
                         f"got {target}")

    def risk(power: float) -> float:
        return death_chance(power, tier, dtype, subtype, total_floors,
                            modifier_score, per_floor_risk,
                            boss_risk_multiplier, boss_rarity, rate, cap)

    if risk(0.0) <= target:
        return 0.0

    low = 0.0
    high = max(1.0, scoring.tier_bounds(tier)[1])
    # Walk the ceiling up until the dungeon is safe enough, rather than assuming
    # the tier's own range contains the answer. It usually does not.
    for _ in range(40):
        if risk(high) <= target:
            break
        high *= 2.0
    else:
        return float("inf")

    for _ in range(60):
        mid = (low + high) / 2.0
        if risk(mid) > target:
            low = mid
        else:
            high = mid
    return high
