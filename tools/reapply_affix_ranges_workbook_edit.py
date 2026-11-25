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
    return cells


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
