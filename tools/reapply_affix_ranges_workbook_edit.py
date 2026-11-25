"""Re-apply the affix range edit of issue #2883 to the design workbook, then regenerate.

WHY THIS EXISTS. `docs/All_Things_Cataclysm.xlsx` is a binary git cannot merge, and other
branches carry unmerged edits to it. Whichever lands second takes the other's workbook
whole and re-applies its own cells. This script is that re-application for
`feat/affixes-and-rebalance-2883-2884`, recorded as data rather than as a binary.

WHAT IT WRITES, KEYED ON CONTENT AND NEVER ON A ROW NUMBER:

    Affixes      fourteen columns, T1 Min .. T7 Max, added after the last column if
                 absent, holding each non-hybrid affix's seven whole-number ranges at
                 +0 (73 rows, 1,022 cells); and Allowed Slots narrowed on 20 rows.
    Item Bases   two columns, Implicit 1 At +0 and Implicit 2 At +0, added the same
                 way, holding each implicit's whole +0 value (76 cells on 55 bases).
    Enchantments and Enchantment Effects
                 the thirteen enchantments whose numbers become whole (D8 of the
                 approved proposal, accepted by the coordinating session on
                 2026-11-25 with row 4 as a new stat, melee_reach_centimetres): each
                 sentence, every effect row carrying it, the nine row names the new
                 sentences give, and the values the sentences now state.

THE VALUES ARE FROZEN HERE, NOT COMPUTED. They are what the approved proposal's
`apply` wrote (`tools/affix_range_proposal.py` on `proposal/affix-ranges` at
2cb5074060868bd790cce9a1e95c03f60a6bcb27; the owner approved it on issue #2883,
2026-11-24: "Approve as written (Recommended)"), run against development
db7427d7a, and they equal the analysis branch's `affix_ranges_2883.TIER_RANGES`.
Freezing them means a later change to the CSVs cannot change what this writes.

Top Value and Floor are left alone: the coordinating session ruled on 2026-11-25
that this change keeps both columns, unused by the new value rule, and issue #2904
retires them.

IT REFUSES TO WRITE unless every cell it owns holds either the value it had before
the edit or the value the edit gives it, and it verifies afterwards that those
cells are the only cells in the workbook that changed.

    python tools/reapply_affix_ranges_workbook_edit.py            # apply, then regenerate
    python tools/reapply_affix_ranges_workbook_edit.py --check    # report, write nothing

Run against the workbook already carrying the edit, it reports every cell already
set and writes nothing, which is its positive control.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"

TIERS = (1, 2, 3, 4, 5, 6, 7)
TIER_COLUMNS = tuple(f"T{t} {end}" for t in TIERS for end in ("Min", "Max"))
IMPLICIT_COLUMNS = ("Implicit 1 At +0", "Implicit 2 At +0")

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

#: Allowed Slots, (before, after), for the twenty rows the proposal narrows.
SLOT_LISTS: dict[str, tuple[str, str]] = {
    'Flat evasion': (
        'Boots, Chest, Gloves, Head, Pants, Ring, Shoulders',
        'Chest'),
    'Flat maximum class resource': (
        'Belt, Necklace, Relic, Ring',
        'Necklace, Relic'),
    'Flat health regeneration': (
        'Belt, Boots, Chest, Necklace, Ring',
        'Belt, Chest, Necklace'),
    'Flat mana regeneration': (
        'Belt, Boots, Chest, Necklace, Ring',
        'Belt, Necklace'),
    'Flat life leech': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Weapon'),
    'Flat block chance': (
        'Belt, Boots, Chest, Head, Pants, Ring, Shoulders',
        'Belt, Shoulders'),
    'Flat damage reduction': (
        'Belt, Boots, Chest, Head, Pants, Ring, Shoulders',
        'Pants'),
    'Flat retaliation': (
        'Belt, Boots, Chest, Head, Pants, Ring, Shoulders',
        'Head, Shoulders'),
    'Flat crowd control resistance': (
        'Belt, Boots, Chest, Head, Pants, Ring, Shoulders',
        'Belt, Boots'),
    'Flat critical strike chance': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Gloves, Weapon'),
    'Flat penetration': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Weapon'),
    'Armor and evasion': (
        'Boots, Chest, Head, Pants, Ring, Shoulders',
        'Chest'),
    'Evasion and energy shield': (
        'Chest, Head, Ring, Shoulders',
        'Chest'),
    'Critical strike chance and multiplier': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Gloves, Weapon'),
    'Health and mana regeneration': (
        'Belt, Boots, Chest, Necklace, Ring',
        'Belt, Necklace'),
    'Penetration and critical strike multiplier': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Weapon'),
    'Block chance and crowd control resistance': (
        'Belt, Boots, Chest, Head, Pants, Ring, Shoulders',
        'Belt'),
    'Flat mana leech': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Weapon'),
    'Flat energy shield leech': (
        'Gloves, Necklace, Relic, Ring, Weapon',
        'Weapon'),
    'Flat energy shield regeneration': (
        'Chest, Head, Relic, Ring, Shoulders',
        'Chest, Head, Shoulders'),
}

#: Item Bases: (Implicit 1 At +0, Implicit 2 At +0) by Base Name; None where a base has no second implicit.
IMPLICITS_AT_ZERO: dict[str, tuple[int | None, int | None]] = {
    'Helm': (80, None),
    'Hood': (2, None),
    'Circlet': (22, 4),
    'Visage': (28, 2),
    'Cuirass': (176, None),
    'Jerkin': (3, None),
    'Vestment': (48, 10),
    'Hauberk': (72, None),
    'Carapace': (88, 36),
    'Pauldrons': (66, None),
    'Mantle': (1, None),
    'Epaulets': (1, None),
    'Spaulders': (4, None),
    'Gauntlets': (52, None),
    'Grips': (4, None),
    'Handwraps': (2, None),
    'Vambraces': (5, None),
    'Greaves': (100, None),
    'Leggings': (2, None),
    'Kilt': (52, None),
    'Trousers': (26, 5),
    'Sabatons': (58, 2),
    'Treads': (5, None),
    'Striders': (1, 3),
    'Sollerets': (32, 2),
    'Girdle': (52, None),
    'Sash': (24, None),
    'Cord': (1, None),
    'Cinch': (60, None),
    'Band': (4, None),
    'Signet': (6, None),
    'Loop': (24, None),
    'Circle': (12, 1),
    'Amulet': (24, None),
    'Pendant': (2, None),
    'Torc': (38, None),
    'Locket': (22, 4),
    'Idol': (11, None),
    'Fetish': (4, None),
    'Reliquary': (4, None),
    'Effigy': (4, None),
    'Sword': (16, 2),
    'Dagger': (10, 3),
    'Axe': (18, None),
    'Fist': (12, 4),
    'Wand': (15, 7),
    'Whip': (13, 5),
    'Shield': (5, 104),
    'Crossbow': (15, 8),
    'Greatsword': (31, None),
    'Greataxe': (29, 9),
    'Spear': (26, 2),
    'Staff': (26, 13),
    'Two-Handed Crossbow': (26, 3),
    'Warhammer': (34, None),
}


#: D8: (sheet column, sentence before, sentence after, row name before, row name after).
#: The row name is what `generate_datatables.row_name(kind, sentence[:48])` gives each
#: sentence; the nine that change are in FCataclysmEnchantmentRenames and the pin file
#: `tools/tests/enchantment_row_names.txt`, so a saved item keeps its enchantment.
BRUTE_BEFORE = ("Brute's Heart (10-Piece Bonus): Every 30 seconds, you can unleash a powerful "
                "\"Ground Pound\" that deals 500% of your armor as damage to all enemies in a large "
                "radius and stuns them for 3 seconds. The cooldown of this ability is reduced by "
                "0.5 seconds per kill")
BRUTE_AFTER = BRUTE_BEFORE.replace("reduced by 0.5 seconds per kill",
                                   "reduced by 1 second for every 2 kills")
SENTENCES: tuple[tuple[str, str, str, str, str], ...] = (
    ("Positives",
     "Your damage is increased by 0.01%-0.05% permanently for every 1000 enemies killed this run",
     "Your damage is increased by 1%-5% permanently for every 100,000 enemies killed this run",
     "Positive_Your_damage_is_increased_by_0_01_0_05_permanen",
     "Positive_Your_damage_is_increased_by_1_5_permanently_fo"),
    ("Positives",
     "Your maximum HP is increased by 0.01%-0.05% permanently for every 1000 enemies killed this run",
     "Your maximum HP is increased by 1%-5% permanently for every 100,000 enemies killed this run",
     "Positive_Your_maximum_HP_is_increased_by_0_01_0_05_perm",
     "Positive_Your_maximum_HP_is_increased_by_1_5_permanentl"),
    ("Positives",
     "Each class point spent above 100 grants 0.5%-1% increased damage",
     "Every 2 class points spent above 100 grant 1%-2% increased damage",
     "Positive_Each_class_point_spent_above_100_grants_0_5_1",
     "Positive_Every_2_class_points_spent_above_100_grant_1_2"),
    ("Positives",
     "Your melee skills have +0.5-1 metre reach",
     "Your melee skills have +50-100 centimetres reach",
     "Positive_Your_melee_skills_have_0_5_1_metre_reach",
     "Positive_Your_melee_skills_have_50_100_centimetres_reach"),
    ("Negatives",
     "Your skills deal 1.5%-2.5% less damage for every 10 class points spent above 100",
     "Your skills deal 3%-5% less damage for every 20 class points spent above 100",
     "Negative_Your_skills_deal_1_5_2_5_less_damage_for_every",
     "Negative_Your_skills_deal_3_5_less_damage_for_every_20"),
    ("Negatives",
     "Your maximum HP is reduced by 1.5%-2.5% for every 10 class points above 50",
     "Your maximum HP is reduced by 3%-5% for every 20 class points above 50",
     "Negative_Your_maximum_HP_is_reduced_by_1_5_2_5_for_ever",
     "Negative_Your_maximum_HP_is_reduced_by_3_5_for_every_20"),
    ("Positives", BRUTE_BEFORE, BRUTE_AFTER,
     "Positive_Brute_s_Heart_10_Piece_Bonus_Every_30_seconds",
     "Positive_Brute_s_Heart_10_Piece_Bonus_Every_30_seconds"),
    ("Positives",
     "Enemies you stagger are also briefly rooted for 0.5-1.5 seconds",
     "Enemies you stagger are also briefly rooted for 1-2 seconds",
     "Positive_Enemies_you_stagger_are_also_briefly_rooted_for",
     "Positive_Enemies_you_stagger_are_also_briefly_rooted_for"),
    ("Positives",
     "Your heavy attack cooldown is reduced by 0.5-1.5 seconds each time you land a critical strike",
     "Your heavy attack cooldown is reduced by 1-2 seconds each time you land a critical strike",
     "Positive_Your_heavy_attack_cooldown_is_reduced_by_0_5_1_5",
     "Positive_Your_heavy_attack_cooldown_is_reduced_by_1_2_sec"),
    ("Positives",
     "Each spell cast reduces your next spell cooldown by 0.5-1.5 seconds",
     "Each spell cast reduces your next spell cooldown by 1-2 seconds",
     "Positive_Each_spell_cast_reduces_your_next_spell_cooldown",
     "Positive_Each_spell_cast_reduces_your_next_spell_cooldown"),
    ("Negatives",
     "Point blank AOE skills have a 0.75-1.5 second cast delay before firing",
     "Point blank AOE skills have a 1-2 second cast delay before firing",
     "Negative_Point_blank_AOE_skills_have_a_0_75_1_5_second_ca",
     "Negative_Point_blank_AOE_skills_have_a_1_2_second_cast_de"),
    ("Negatives",
     "Critical strikes trigger a 0.5-1 second global cooldown on all your skills",
     "Critical strikes trigger a 1-2 second global cooldown on all your skills",
     "Negative_Critical_strikes_trigger_a_0_5_1_second_global_c",
     "Negative_Critical_strikes_trigger_a_1_2_second_global_coo"),
    ("Negatives",
     "After using a charge skill you are briefly stunned for 0.5-1 second",
     "After using a charge skill you are briefly stunned for 1-2 seconds",
     "Negative_After_using_a_charge_skill_you_are_briefly_stunn",
     "Negative_After_using_a_charge_skill_you_are_briefly_stunn"),
)

#: D8: the effect values each new sentence states. (sentence after, the column and value
#: that pick the one effect row among those carrying the sentence, or None when only one
#: row carries it, then {column: (before, after)}). Brute's Heart keeps 0.5 a kill: the
#: coordinating session ruled on 2026-11-25 to change its sentence only, the same rate.
EFFECT_VALUES: tuple[tuple[str, tuple[str, str] | None, dict[str, tuple[object, object]]], ...] = (
    (SENTENCES[0][2], ("Stat", "attack_damage"),
     {"Value Low": (0.01, 1), "Value High": (0.05, 5), "Scale Step": (1000, 100000)}),
    (SENTENCES[0][2], ("Stat", "spell_damage"),
     {"Value Low": (0.01, 1), "Value High": (0.05, 5), "Scale Step": (1000, 100000)}),
    (SENTENCES[1][2], ("Stat", "max_health"),
     {"Value Low": (0.01, 1), "Value High": (0.05, 5), "Scale Step": (1000, 100000)}),
    (SENTENCES[2][2], ("Stat", "attack_damage"),
     {"Value Low": (0.5, 1), "Value High": (1, 2), "Scale Step": (1, 2)}),
    (SENTENCES[2][2], ("Stat", "spell_damage"),
     {"Value Low": (0.5, 1), "Value High": (1, 2), "Scale Step": (1, 2)}),
    (SENTENCES[3][2], None,
     {"Stat": ("melee_reach_metres", "melee_reach_centimetres"),
      "Value Low": (0.5, 50), "Value High": (1, 100)}),
    (SENTENCES[4][2], ("Stat", "attack_damage"),
     {"Value Low": (-1.5, -3), "Value High": (-2.5, -5), "Scale Step": (10, 20)}),
    (SENTENCES[4][2], ("Stat", "spell_damage"),
     {"Value Low": (-1.5, -3), "Value High": (-2.5, -5), "Scale Step": (10, 20)}),
    (SENTENCES[5][2], None,
     {"Value Low": (-1.5, -3), "Value High": (-2.5, -5), "Scale Step": (10, 20)}),
    (SENTENCES[7][2], None, {"Value Low": (0.5, 1), "Value High": (1.5, 2)}),
    (SENTENCES[8][2], None, {"Value Low": (0.5, 1), "Value High": (1.5, 2)}),
    (SENTENCES[9][2], None, {"Value Low": (0.5, 1), "Value High": (1.5, 2)}),
    (SENTENCES[10][2], None, {"Value Low": (0.75, 1), "Value High": (1.5, 2)}),
    (SENTENCES[11][2], None, {"Stack Seconds": (0.5, 1), "Stack Seconds High": (1, 2)}),
    (SENTENCES[12][2], None, {"Value Low": (0.5, 1), "Value High": (1, 2)}),
)


def enchantment_cells(book) -> list[tuple[str, int, int, object, object, str]]:
    """Every cell the D8 edit owns, as `planned_cells` lists them."""
    cells = []
    sheet = book["Enchantments"]
    header = {str(c.value).strip(): c.column for c in sheet[1] if c.value is not None}
    effects = book["Enchantment Effects"]
    effect_header = {str(c.value).strip(): c.column for c in effects[1] if c.value is not None}
    for column, before, after, name_before, name_after in SENTENCES:
        found = [r for r in range(2, sheet.max_row + 1)
                 if sheet.cell(row=r, column=header[column]).value in (before, after)]
        if len(found) != 1:
            sys.exit(f"Enchantments: expected one {column} row reading {before[:50]!r} or its "
                     f"new words, found {len(found)}; refusing")
        cells.append(("Enchantments", found[0], header[column], before, after,
                      f"{column}: {after[:50]}"))
        carrying = [r for r in range(2, effects.max_row + 1)
                    if effects.cell(row=r, column=effect_header["Effect"]).value in (before, after)]
        if not carrying:
            sys.exit(f"Enchantment Effects: no row carries {before[:50]!r}; refusing")
        for r in carrying:
            cells.append(("Enchantment Effects", r, effect_header["Effect"], before, after,
                          f"Effect: {after[:50]}"))
            if name_before != name_after:
                cells.append(("Enchantment Effects", r, effect_header["Enchantment"],
                              name_before, name_after, f"Enchantment: {name_after}"))
    for sentence, pick, values in EFFECT_VALUES:
        before_sentence = next(s[1] for s in SENTENCES if s[2] == sentence)
        rows = [r for r in range(2, effects.max_row + 1)
                if effects.cell(row=r, column=effect_header["Effect"]).value
                in (before_sentence, sentence)]
        if pick is not None:
            rows = [r for r in rows
                    if effects.cell(row=r, column=effect_header[pick[0]]).value == pick[1]]
        if len(rows) != 1:
            sys.exit(f"Enchantment Effects: expected one row for {sentence[:50]!r} {pick}, "
                     f"found {len(rows)}; refusing")
        for column, (before, after) in values.items():
            cells.append(("Enchantment Effects", rows[0], effect_header[column], before, after,
                          f"{column}: {sentence[:40]}"))
    return cells


def snapshot(book) -> dict:
    return {(ws.title, cell.coordinate): cell.value
            for ws in book.worksheets for row in ws.iter_rows() for cell in row
            if cell.value is not None}


def columns(ws, wanted: tuple[str, ...]) -> dict[str, int]:
    """Header name -> 1-based column, adding any of `wanted` that is absent after
    the last column, in the order given."""
    header = {str(c.value).strip(): c.column for c in ws[1] if c.value is not None}
    last = max(header.values())
    for title in wanted:
        if title not in header:
            last += 1
            header[title] = last
    return header


def rows_by(ws, header: dict[str, int], key_column: str) -> dict[str, int]:
    """Key value -> row number, refusing a key that appears twice."""
    found: dict[str, int] = {}
    for r in range(2, ws.max_row + 1):
        key = ws.cell(row=r, column=header[key_column]).value
        if key is None:
            continue
        if key in found:
            sys.exit(f"{ws.title}: {key_column} {key!r} appears on rows {found[key]} "
                     f"and {r}; refusing")
        found[key] = r
    return found


def planned_cells(book) -> list[tuple[str, int, int, object, object, str]]:
    """Every cell this edit owns: (sheet, row, column, value before, value after, label)."""
    cells = []
    affixes = book["Affixes"]
    header = columns(affixes, TIER_COLUMNS)
    for title in TIER_COLUMNS:
        cells.append(("Affixes", 1, header[title], None, title, f"header {title}"))
    by_name = rows_by(affixes, header, "Affix Name")
    for name, ranges in TIER_RANGES.items():
        if name not in by_name:
            sys.exit(f"Affixes has no row named {name!r}; refusing")
        for t, (low, high) in zip(TIERS, ranges, strict=True):
            for end, value in (("Min", low), ("Max", high)):
                cells.append(("Affixes", by_name[name], header[f"T{t} {end}"], None, value,
                              f"{name}, T{t} {end}"))
    for name, (before, after) in SLOT_LISTS.items():
        if name not in by_name:
            sys.exit(f"Affixes has no row named {name!r}; refusing")
        cells.append(("Affixes", by_name[name], header["Allowed Slots"], before, after,
                      f"{name}, Allowed Slots"))

    bases = book["Item Bases"]
    header = columns(bases, IMPLICIT_COLUMNS)
    for title in IMPLICIT_COLUMNS:
        cells.append(("Item Bases", 1, header[title], None, title, f"header {title}"))
    by_base = rows_by(bases, header, "Base Name")
    for name, values in IMPLICITS_AT_ZERO.items():
        if name not in by_base:
            sys.exit(f"Item Bases has no row named {name!r}; refusing")
        for title, value in zip(IMPLICIT_COLUMNS, values, strict=True):
            if value is not None:
                cells.append(("Item Bases", by_base[name], header[title], None, value,
                              f"{name}, {title}"))
    return cells + enchantment_cells(book)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing")
    parser.add_argument("--no-generate", action="store_true",
                        help="edit the workbook and do not regenerate the CSVs")
    args = parser.parse_args()

    book = openpyxl.load_workbook(args.workbook)
    before = snapshot(book)
    planned = planned_cells(book)
    to_write = []
    already = 0
    for sheet, row, column, old, new, label in planned:
        cell = book[sheet].cell(row=row, column=column)
        where = f"{sheet}!{cell.coordinate} ({label})"
        if cell.value == new:
            already += 1
        elif cell.value == old:
            to_write.append((cell, new))
        else:
            sys.exit(f"{where} holds {cell.value!r}, neither the value before this edit "
                     f"({old!r}) nor after it ({new!r}); refusing")
    print(f"{already} of {len(planned)} cells already set, {len(to_write)} to change")

    if args.check or not to_write:
        print("nothing written")
    else:
        for cell, new in to_write:
            cell.value = new
        book.save(args.workbook)
        after = snapshot(openpyxl.load_workbook(args.workbook))
        changed = {k for k in set(before) | set(after) if before.get(k) != after.get(k)}
        intended = {(c.parent.title, c.coordinate) for c, _ in to_write}
        if changed != intended:
            sys.exit(f"the save changed {sorted(changed - intended)} and missed "
                     f"{sorted(intended - changed)}; inspect the workbook")
        print(f"wrote {len(to_write)} cells, and no other cell changed")

    if args.check or args.no_generate:
        return 0
    return subprocess.call([sys.executable, str(GENERATOR),
                            "--workbook", str(args.workbook)], cwd=REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main())
