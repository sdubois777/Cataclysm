"""THE APPROVED AFFIX RANGES OF ISSUE #2883, APPLIED TO THE SIMULATION. ANALYSIS BRANCH ONLY.

WHY THIS EXISTS. Issue #2884, the enemy balance audit, has to measure the enemies against a character wearing the affix
values the project owner approved on 2026-11-24 ("Approve as written (Recommended)", issue #2883). That proposal lives
on branch `proposal/affix-ranges` at `2cb5074060868bd790cce9a1e95c03f60a6bcb27`: `tools/affix_range_proposal.py`
computes it, and its `simulated()` measured it by patching five value methods for the length of one call. This module
makes the same values the simulation's own, so every function and every test in `sim/` and `tools/` reads them, which
is what "report which tests the new numbers move" needs.

WHAT IT HOLDS, AND WHERE EACH PART COMES FROM.
  `TIER_RANGES`  every non-hybrid affix's seven tier ranges at +0, whole numbers, printed by the proposal's
                 `proposal()`. `tools/tests/test_enemy_rebalance_2884.py` holds the two equal, so this copy cannot drift
                 from the approved one.
  `SLOTS`        the fourteen affixes that roll on fewer kinds of piece (ruling 3, "Fewer slots", and the owner's D5).
  the rule       an upgrade multiplies each end of the +0 range by 1 + 0.15 x level, in whole hundredths, a half going
                 up, a two-handed weapon doubling inside the rounding; a roll in [0, 1] picks one of the whole numbers
                 in the range, each equally likely; a hybrid at tier N grants each half at that half's tier N - 2, and
                 rolls tiers 3 to 7 only; an implicit is its stated +10 figure over 2.5, rounded half up, at least 1,
                 and upgraded the same way. All of it is the proposal's own arithmetic (`upgraded`, `roll_value`,
                 `hybrid_half`, `simulated`'s `implicit_value`), copied rather than imported: `sim/` must not depend on
                 `tools/`.

WHAT IT DOES NOT DO. The design workbook and `game/Data/*.csv` are untouched, so every test that holds the simulation
equal to the game's data on a changed figure fails on this branch. That is a branch that applied the proposal to one
side only, not a fault in either side.

Pure standard library, as the whole simulation is.
"""

from __future__ import annotations

import math

#: Each upgrade level adds this to the multiplier, in hundredths: 1.00, 1.15 ... 2.50. `affixes.GEAR_LEVEL_FACTOR`
#: is 0.15 and `affixes.py` checks the two agree.
LEVEL_STEP_HUNDREDTHS = 15

#: A two-handed weapon doubles its affixes and implicits inside the rounding. `affixes.TWO_HANDED_MULTIPLIER`.
TWO_HANDED_FACTOR = 2

#: The highest upgrade level. `affixes.MAX_GEAR_LEVEL`.
MAX_GEAR_LEVEL = 10

#: A hybrid at tier N grants each half at that half's own tier N - 2, so a hybrid rolls tiers 3 to 7 only.
HYBRID_TIER_DROP = 2

#: Every non-hybrid affix: seven (low, high) ranges at +0, tier 1 first. Printed by `affix_range_proposal.proposal()`.
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
    'Increased damage against War enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
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
    'Increased damage against Void enemies':
        ((40, 47), (48, 59), (60, 69), (70, 84), (85, 99), (100, 119), (120, 160)),
    'Increased damage over time': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased damage over time duration': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased minion damage': ((13, 15), (16, 18), (19, 21), (22, 26), (27, 31), (32, 37), (38, 50)),
    'Increased minion health': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Increased minion attack speed': ((8, 9), (10, 11), (12, 13), (14, 15), (16, 17), (18, 19), (20, 21)),
    'Flat energy shield regeneration': ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)),
}

#: The fourteen affixes that roll on fewer kinds of piece. Ruling 3 ("Fewer slots (Recommended)") and the owner's D5
#: (flat crit chance on weapons and gloves, flat mana regeneration on belts and necklaces). A hybrid's slots stay the
#: intersection of its halves', which `affixes._hybrid` already computes, so six hybrids narrow with them.
SLOTS: dict[str, tuple[str, ...]] = {
    'Flat evasion': ('Chest',),
    'Flat maximum class resource': ('Necklace', 'Relic'),
    'Flat health regeneration': ('Belt', 'Chest', 'Necklace'),
    'Flat mana regeneration': ('Belt', 'Necklace'),
    'Flat life leech': ('Weapon',),
    'Flat block chance': ('Belt', 'Shoulders'),
    'Flat damage reduction': ('Pants',),
    'Flat retaliation': ('Head', 'Shoulders'),
    'Flat crowd control resistance': ('Belt', 'Boots'),
    'Flat critical strike chance': ('Gloves', 'Weapon'),
    'Flat penetration': ('Weapon',),
    'Flat mana leech': ('Weapon',),
    'Flat energy shield leech': ('Weapon',),
    'Flat energy shield regeneration': ('Chest', 'Head', 'Shoulders'),
}


def level_hundredths(gear_level: int) -> int:
    """An upgrade level's multiplier in whole hundredths: +0 is 100, +10 is 250."""
    if not 0 <= gear_level <= MAX_GEAR_LEVEL:
        raise ValueError(f"gear level {gear_level} outside 0-{MAX_GEAR_LEVEL}")
    return 100 + LEVEL_STEP_HUNDREDTHS * gear_level


def upgraded(base: int, gear_level: int, two_handed: bool = False) -> int:
    """What a +0 whole number shows at an upgrade level: multiplied in whole hundredths, rounded once, a half up."""
    hands = TWO_HANDED_FACTOR if two_handed else 1
    return (base * level_hundredths(gear_level) * hands + 50) // 100


def round_half_up(value: float) -> int:
    return math.floor(value + 0.5) if value >= 0 else -math.floor(-value + 0.5)


def roll_value(low: int, high: int, roll: float) -> int:
    """One of the range's whole numbers, each equally likely: the roll in [0, 1] cut into equal shares."""
    steps = high - low
    index = min(math.floor(min(max(roll, 0.0), 1.0) * (steps + 1)), steps)
    return low + index


def covers(name: str) -> bool:
    return name in TIER_RANGES


def value(name: str, tier: int, roll: float = 1.0, gear_level: int = MAX_GEAR_LEVEL,
          two_handed: bool = False) -> int:
    """A rolled affix's whole number: the roll's position among the tier's range at the piece's upgrade level."""
    if not 1 <= tier <= len(TIER_RANGES[name]):
        raise ValueError(f"{name}: tier {tier} outside 1-{len(TIER_RANGES[name])}")
    low, high = TIER_RANGES[name][tier - 1]
    return roll_value(upgraded(low, gear_level, two_handed), upgraded(high, gear_level, two_handed), roll)


def hybrid_half(name: str, hybrid_tier: int, roll: float = 1.0, gear_level: int = MAX_GEAR_LEVEL,
                two_handed: bool = False) -> int:
    """One half of a hybrid: its own affix two tiers down. A hybrid below tier 3 grants nothing."""
    if hybrid_tier <= HYBRID_TIER_DROP:
        return 0
    return value(name, hybrid_tier - HYBRID_TIER_DROP, roll, gear_level, two_handed)


def implicit_at_zero(stated_at_max: float) -> int:
    """An implicit's whole +0 value: its stated +10 figure over 2.5, rounded half up, at least 1."""
    return max(1, round_half_up(stated_at_max * 100 / level_hundredths(MAX_GEAR_LEVEL)))


def implicit_value(stated_at_max: float, gear_level: int = MAX_GEAR_LEVEL, two_handed: bool = False) -> int:
    return upgraded(implicit_at_zero(stated_at_max), gear_level, two_handed)
