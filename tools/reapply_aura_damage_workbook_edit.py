"""Re-apply the workbook edit of issues #2869 and #2875 to a fresh copy, then regenerate.

WHY THIS EXISTS. `docs/All_Things_Cataclysm.xlsx` is a binary git cannot merge, and
two branches carried an unmerged edit to it at once on 2026-11-24: this one
(`fix/auras-2869`) and `feat/abyssal-rifts-2864`. Whichever lands second takes
the other's workbook whole and re-applies its own cells. This script is that
re-application for `fix/auras-2869`, recorded as data rather than as a binary.

THE SIX CELLS, KEYED ON CONTENT AND NEVER ON A ROW NUMBER:

    Weapon Skills, the row whose Skill Name is "Conflagration" (#2869)
        Skill Description, Shape Params (Radius=10 -> 4), Damage Percent (-> 10)
    Weapon Skills, the row whose Skill Name is "Living Pyre" (#2875)
        Damage Percent (-> 66.67, the Ultimate's 400% spread over its six
        pulses: the owner's ruling of 2026-11-24 on #2875)
    Skill Slots, the row whose Slot is "Aura" (#2869, #2875)
        Damage Lowest (15 -> 10), Note

IT REFUSES TO WRITE unless every one of the six cells holds either the value it
had before the edit or the value the edit gives it, and it verifies afterwards
that those six are the only cells in the workbook that changed. A cell holding
anything else means the sheet moved under it, and a person has to look.

    python tools/reapply_aura_damage_workbook_edit.py            # apply, then regenerate
    python tools/reapply_aura_damage_workbook_edit.py --check    # report, write nothing

Run against the workbook already carrying the edit, it reports all six already
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

CONFLAGRATION_TEXT_BEFORE = (
    "Emanate a ring of hellfire in a 10 meter radius. Enemies within it are "
    "continuously set alight and have their Demonic resistance reduced by 15%. "
    "Allies within it deal 8% increased damage. Drains mana per second while "
    "active.")
CONFLAGRATION_TEXT_AFTER = (
    "Emanate a ring of hellfire in a 4 meter radius. Enemies within it take 10% "
    "weapon damage each second, are continuously set alight and have their "
    "Demonic resistance reduced by 15%. Allies within it deal 8% increased "
    "damage. Drains mana per second while active.")
CONFLAGRATION_PARAMS_BEFORE = ("Radius=10; Interval=1; Burn=1; Effect=Shred; "
                               "EffectMagnitude=15; AllyIncreasedDamage=8")
CONFLAGRATION_PARAMS_AFTER = ("Radius=4; Interval=1; Burn=1; Effect=Shred; "
                              "EffectMagnitude=15; AllyIncreasedDamage=8")
AURA_NOTE_BEFORE = ("Persistent and toggled, draining resource per second. This "
                    "is per second rather than per use.")
AURA_NOTE_AFTER = ("Persistent and toggled, draining resource per second. This "
                   "is per second rather than per use, and it is what an aura "
                   "stating damage is typically worth: an aura whose row states "
                   "none deals none.")

#: (sheet, key column, key value, edited column, value before, value after)
EDITS = (
    ("Weapon Skills", "Skill Name", "Conflagration", "Skill Description",
     CONFLAGRATION_TEXT_BEFORE, CONFLAGRATION_TEXT_AFTER),
    ("Weapon Skills", "Skill Name", "Conflagration", "Shape Params",
     CONFLAGRATION_PARAMS_BEFORE, CONFLAGRATION_PARAMS_AFTER),
    ("Weapon Skills", "Skill Name", "Conflagration", "Damage Percent", None, 10),
    ("Weapon Skills", "Skill Name", "Living Pyre", "Damage Percent", None,
     66.67),
    ("Skill Slots", "Slot", "Aura", "Damage Lowest", 15, 10),
    ("Skill Slots", "Slot", "Aura", "Note", AURA_NOTE_BEFORE, AURA_NOTE_AFTER),
)


def snapshot(book) -> dict:
    return {(ws.title, cell.coordinate): cell.value
            for ws in book.worksheets for row in ws.iter_rows() for cell in row
            if cell.value is not None}


def find_cell(book, sheet, key_column, key, column):
    ws = book[sheet]
    header = {str(c.value).strip(): i for i, c in enumerate(ws[1])
              if c.value is not None}
    for name in (key_column, column):
        if name not in header:
            sys.exit(f"{sheet} has no column {name!r}; refusing")
    rows = [row for row in ws.iter_rows(min_row=2)
            if row[header[key_column]].value == key]
    if len(rows) != 1:
        sys.exit(f"{sheet}: expected one row with {key_column} {key!r}, "
                 f"found {len(rows)}; refusing")
    return rows[0][header[column]]


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
    to_write = []
    for sheet, key_column, key, column, old, new in EDITS:
        cell = find_cell(book, sheet, key_column, key, column)
        where = f"{sheet}!{cell.coordinate} ({key}, {column})"
        if cell.value == new:
            print(f"already set   {where}")
        elif cell.value == old:
            print(f"to change     {where}: {old!r} -> {new!r}")
            to_write.append((cell, new))
        else:
            sys.exit(f"{where} holds {cell.value!r}, neither the value before "
                     f"this edit ({old!r}) nor after it ({new!r}); refusing")

    if args.check or not to_write:
        print(f"{len(to_write)} of {len(EDITS)} cells to change; nothing written")
    else:
        for cell, new in to_write:
            cell.value = new
        book.save(args.workbook)
        after = snapshot(openpyxl.load_workbook(args.workbook))
        changed = {k for k in set(before) | set(after)
                   if before.get(k) != after.get(k)}
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
