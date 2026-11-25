"""The affix ranges of issue #2883: every affix a whole number, from seven explicit tier ranges.

WHAT CHANGED. Until issue #2883 an affix was its `top_value` times a geometric tier fraction,
a 75%-to-100% roll band, divided back to +0, upgraded and remapped onto a stated `floor`, in
floating point and never rounded (`affixes.affix_value`, still present for the old tests until
issue #2904 retires it). The project owner approved the replacement on 2026-11-24 ("Approve as
written (Recommended)", issue #2883): each affix states seven whole-number ranges at +0, tier 1
first, and every value a player is handed is a whole number at every tier, roll and upgrade level.

THE RULE, which the game's `UCataclysmItemValues` (game/Source/Cataclysm/Items/CataclysmItem.cpp)
implements in integers and `tools/tests/test_affix_values_port.py` holds equal to this:
  rule 3  each end of a tier's +0 range is upgraded on its own: multiplied by 100 + 15 x level
          hundredths and rounded once, a half going up; a two-handed weapon doubles inside the
          rounding. Always from the +0 number, so rounding never compounds.
  rule 4  the roll in [0, 1] picks one of the whole numbers between the two upgraded ends, each
          equally likely.
  D6      a hybrid at tier N grants each half as that half's own affix at tier N - 2, so a hybrid
          rolls tiers 3 to 7; one saved at tier 1 or 2 is read as tier 3 (the coordinating
          session's ruling of 2026-11-25: clamp when read, no save migration).
  D9      an implicit is a whole +0 number, its stated +10 figure over 2.5 rounded half up and at
          least 1, upgraded by rule 3.

WHERE THE NUMBERS COME FROM. `TIER_RANGES` is what the approved proposal's `apply` wrote into the
design workbook (`tools/affix_range_proposal.py` on `proposal/affix-ranges` at 2cb507406), frozen in
`tools/reapply_affix_ranges_workbook_edit.py`; `tools/tests/test_affix_values_port.py` holds this
copy equal to `game/Data/Affixes.csv`, so neither can drift.

Pure standard library, as the whole simulation is.
"""

from __future__ import annotations

import math

#: Each upgrade level adds this many hundredths to the multiplier: +0 is 100, +10 is 250.
#: `affixes.GEAR_LEVEL_FACTOR` in whole hundredths; `affixes.py` checks the two agree. Integers,
#: because 10 x 1.15 is 11.5 on paper and 11.499999999999998 in a double, which rounds the wrong way.
LEVEL_STEP_HUNDREDTHS = 15

#: A two-handed weapon doubles its affixes and implicits inside the rounding. `affixes.TWO_HANDED_MULTIPLIER`.
TWO_HANDED_FACTOR = 2

#: The highest upgrade level. `affixes.MAX_GEAR_LEVEL`.
MAX_GEAR_LEVEL = 10

#: The highest affix tier.
MAX_TIER = 7

#: A hybrid at tier N grants each half at that half's own tier N - 2, so a hybrid rolls tiers 3 to 7.
HYBRID_TIER_DROP = 2
LOWEST_HYBRID_TIER = HYBRID_TIER_DROP + 1

#: Every non-hybrid affix's seven tier ranges at +0, tier 1 first: (T1 Min, T1 Max) ... (T7 Min, T7 Max).
TIER_RANGES: dict[str, tuple[tuple[int, int], ...]] = {
    'Flat maximum health': ((12, 13), (14, 16), (17, 20), (21, 24), (25, 29), (30, 35), (36, 48)),
    'Increased maximum health': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat maximum mana': ((4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15), (16, 17)),
    'Increased maximum mana': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat maximum energy shield': ((5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16), (17, 20)),
    'Increased maximum energy shield': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat armor': ((25, 29), (30, 35), (36, 42), (43, 49), (50, 59), (60, 74), (75, 100)),
    'Increased armor': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat evasion': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Increased evasion': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat damage': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Increased damage': ((13, 15), (16, 18), (19, 21), (22, 26), (27, 31), (32, 37), (38, 50)),
    'Increased spell damage': ((13, 15), (16, 18), (19, 21), (22, 26), (27, 31), (32, 37), (38, 50)),
    'Flat maximum class resource': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Flat health regeneration': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Increased health regeneration': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat mana regeneration': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Increased mana regeneration': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased energy shield regeneration': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat life leech': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat block chance': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat damage reduction': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat retaliation': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat crowd control resistance': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat critical strike chance': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Increased critical strike chance': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat critical strike multiplier': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Increased attack speed': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased area of effect': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased damage over time frequency': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat penetration': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Increased movement speed': ((9, 10), (11, 12), (13, 14), (15, 16), (17, 18), (19, 20), (21, 22)),
    'Flat cooldown reduction': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat magic find': ((3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16)),
    'Increased loot quantity': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Single resistance': ((10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21), (22, 23)),
    'Two resistances': ((7, 8), (9, 10), (11, 12), (13, 14), (15, 16), (17, 18), (19, 20)),
    'All resistances': ((3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16)),
    'Chance to bleed': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to poison': ((3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16)),
    'Chance to disease': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to apply void splinter': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to necrose': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to madden': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to cripple': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to weaken': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to shred': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to burn': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Chance to stun': ((2, 3), (4, 5), (6, 7), (8, 9), (10, 11), (12, 13), (14, 15)),
    'Increased agility': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased ferocity': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased constitution': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased vitality': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased mind': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased spirit': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased efficacy': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased luck': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat mana leech': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Flat energy shield leech': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
    'Increased damage against War enemies': ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Demonic enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Death enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Pestilence enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Famine enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Celestial enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Chaos enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage against Void enemies': ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage over time': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased damage over time duration': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased minion damage': ((13, 15), (16, 18), (19, 21), (22, 26), (27, 31), (32, 37), (38, 50)),
    'Increased minion health': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased minion attack speed': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat energy shield regeneration': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
}


def level_hundredths(gear_level: int) -> int:
    """An upgrade level's multiplier in whole hundredths: +0 is 100, +10 is 250. Held to 0..10."""
    level = min(max(int(gear_level), 0), MAX_GEAR_LEVEL)
    return 100 + LEVEL_STEP_HUNDREDTHS * level


def upgraded(at_zero: int, gear_level: int, two_handed: bool = False) -> int:
    """Rule 3: a +0 whole number at an upgrade level, rounded once, a half going up."""
    hands = TWO_HANDED_FACTOR if two_handed else 1
    return (at_zero * level_hundredths(gear_level) * hands + 50) // 100


def round_half_up(value: float) -> int:
    """A half rounds up, as the design states; Python's round() sends a half to the even neighbour."""
    return math.floor(value + 0.5) if value >= 0 else -math.floor(-value + 0.5)


def roll_value(low: int, high: int, roll: float) -> int:
    """Rule 4: one of the whole numbers low to high, each equally likely."""
    if high <= low:
        return low
    steps = high - low
    index = min(math.floor(min(max(roll, 0.0), 1.0) * (steps + 1)), steps)
    return low + index


def covers(name: str) -> bool:
    """Whether `name` is a non-hybrid affix with tier ranges."""
    return name in TIER_RANGES


def range_at(name: str, tier: int, gear_level: int = MAX_GEAR_LEVEL,
             two_handed: bool = False) -> tuple[int, int]:
    """A tier's range at an upgrade level: both ends of its +0 range upgraded. Tier held to 1..7."""
    low, high = TIER_RANGES[name][min(max(int(tier), 1), MAX_TIER) - 1]
    return upgraded(low, gear_level, two_handed), upgraded(high, gear_level, two_handed)


def value(name: str, tier: int, roll: float = 1.0, gear_level: int = MAX_GEAR_LEVEL,
          two_handed: bool = False) -> int:
    """A rolled affix's whole number: the roll's position among its tier's range at the piece's level."""
    return roll_value(*range_at(name, tier, gear_level, two_handed), roll)


def hybrid_tier(tier: int) -> int:
    """A hybrid's tier as it is read: 3 to 7. One saved at 1 or 2 is read as 3 (no save migration)."""
    return min(max(int(tier), LOWEST_HYBRID_TIER), MAX_TIER)


def hybrid_half(name: str, tier: int, roll: float = 1.0, gear_level: int = MAX_GEAR_LEVEL,
                two_handed: bool = False) -> int:
    """D6: one half of a hybrid at a hybrid tier is its own affix two tiers down."""
    return value(name, hybrid_tier(tier) - HYBRID_TIER_DROP, roll, gear_level, two_handed)


def implicit_at_zero(stated_at_max: float) -> int:
    """D9: an implicit's whole +0 value, its stated +10 figure over 2.5, rounded half up, at least 1."""
    return max(1, round_half_up(stated_at_max * 100 / level_hundredths(MAX_GEAR_LEVEL)))


def implicit_value(stated_at_max: float, gear_level: int = MAX_GEAR_LEVEL,
                   two_handed: bool = False) -> int:
    """An implicit at an upgrade level: its whole +0 value upgraded by rule 3."""
    return upgraded(implicit_at_zero(stated_at_max), gear_level, two_handed)
