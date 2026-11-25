"""Re-apply the workbook edit of the three damage-type auras to a fresh copy, then regenerate.

WHY THIS EXISTS. `docs/All_Things_Cataclysm.xlsx` is a binary git cannot merge, and other branches edit it too --
`feat/death-skills-2885` carries the old Pall of Ending row. Whichever lands second takes the other's workbook whole
and re-applies its own cells. This script is that re-application for `feat/damage-type-auras`, recorded as data rather
than as a binary.

THE OWNER'S RULINGS OF 2026-11-25, verbatim: "make it 10% more and I'm good with it" (Death), "I like those
recommendations. Go with A for both" (Demonic, War), and "I think we should avoid making all of the aura's kinda
samey". So each aura's ally half now does a different thing.

THE SEVEN CELLS, KEYED ON CONTENT AND NEVER ON A ROW NUMBER, all in Weapon Skills:

    "Pall of Ending"   Skill Description, Shape Params: you, your minions and your allies deal 10% MORE damage
    "Conflagration"    Skill Description, Shape Params: a burning enemy dying inside passes its burn on, within 4 m
    "Blood and Iron"   Skill Description, Shape (-> Aura), Shape Params: built; armour -10% and movement -15% on
                       enemies, and you and your allies cannot be slowed and move 15% faster

IT REFUSES TO WRITE unless every cell holds either the value it had before the edit or the value the edit gives it,
and it verifies afterwards that those cells are the only ones in the workbook that changed.

    python tools/reapply_damage_type_auras_workbook_edit.py            # apply, then regenerate
    python tools/reapply_damage_type_auras_workbook_edit.py --check    # report, write nothing

Run against the workbook already carrying the edit, it reports every cell already set and writes nothing, which is
its positive control.
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

PALL_TEXT_BEFORE = (
    "Let the grave's cold spread 10 meters around you. Enemies within it deal 15% less damage while they stay and for "
    "5 seconds after they leave. Allies within it deal 8% increased damage. Drains mana per second while active.")
PALL_TEXT_AFTER = (
    "Let the grave's cold spread 10 meters around you. Enemies within it deal 15% less damage while they stay and for "
    "5 seconds after they leave. You, your minions and your allies within it deal 10% more damage. Drains mana per "
    "second while active.")
PALL_PARAMS_BEFORE = "Radius=10; Interval=1; Effect=Weaken; EffectMagnitude=15; AllyIncreasedDamage=8"
PALL_PARAMS_AFTER = "Radius=10; Interval=1; Effect=Weaken; EffectMagnitude=15; AllyMoreDamage=10"

CONFLAGRATION_TEXT_BEFORE = (
    "Emanate a ring of hellfire in a 4 meter radius. Enemies within it take 10% weapon damage each second, are "
    "continuously set alight and have their Demonic resistance reduced by 15%. Allies within it deal 8% increased "
    "damage. Drains mana per second while active.")
CONFLAGRATION_TEXT_AFTER = (
    "Emanate a ring of hellfire in a 4 meter radius. Enemies within it take 10% weapon damage each second, are "
    "continuously set alight and have their Demonic resistance reduced by 15%. When a burning enemy dies within it, "
    "its burn passes to the nearest other enemy within 4 meters. Drains mana per second while active.")
CONFLAGRATION_PARAMS_BEFORE = "Radius=4; Interval=1; Burn=1; Effect=Shred; EffectMagnitude=15; AllyIncreasedDamage=8"
CONFLAGRATION_PARAMS_AFTER = "Radius=4; Interval=1; Burn=1; Effect=Shred; EffectMagnitude=15; BurnPassesOnDeath=4"

BLOOD_TEXT_BEFORE = (
    "Emanate an aura of martial dominance in a 10 meter radius. Enemies within the aura have their armor reduced by "
    "10% and their movement speed reduced by 15%. Allies within the aura deal 8% increased physical damage. Drains "
    "mana per second while active.")
BLOOD_TEXT_AFTER = (
    "Emanate an aura of martial dominance in a 10 meter radius. Enemies within the aura have their armor reduced by "
    "10% and their movement speed reduced by 15%. You and your allies within the aura cannot be slowed and move 15% "
    "faster. Drains mana per second while active.")
BLOOD_PARAMS_AFTER = ("Radius=10; Interval=1; EnemyArmourRemoved=10; EnemyMovementSpeedReduced=15; "
                      "AllyMovementSpeed=15; Immune=Slow; AllyImmune=Slow")

#: (sheet, key column, key value, edited column, value before, value after)
EDITS = (
    ("Weapon Skills", "Skill Name", "Pall of Ending", "Skill Description", PALL_TEXT_BEFORE, PALL_TEXT_AFTER),
    ("Weapon Skills", "Skill Name", "Pall of Ending", "Shape Params", PALL_PARAMS_BEFORE, PALL_PARAMS_AFTER),
    ("Weapon Skills", "Skill Name", "Conflagration", "Skill Description", CONFLAGRATION_TEXT_BEFORE,
     CONFLAGRATION_TEXT_AFTER),
    ("Weapon Skills", "Skill Name", "Conflagration", "Shape Params", CONFLAGRATION_PARAMS_BEFORE,
     CONFLAGRATION_PARAMS_AFTER),
    ("Weapon Skills", "Skill Name", "Blood and Iron", "Skill Description", BLOOD_TEXT_BEFORE, BLOOD_TEXT_AFTER),
    ("Weapon Skills", "Skill Name", "Blood and Iron", "Shape", None, "Aura"),
    ("Weapon Skills", "Skill Name", "Blood and Iron", "Shape Params", None, BLOOD_PARAMS_AFTER),
)


def snapshot(book) -> dict:
    return {(ws.title, cell.coordinate): cell.value
            for ws in book.worksheets for row in ws.iter_rows() for cell in row
            if cell.value is not None}


def find_cell(book, sheet, key_column, key, column):
    ws = book[sheet]
    header = {str(c.value).strip(): i for i, c in enumerate(ws[1]) if c.value is not None}
    for name in (key_column, column):
        if name not in header:
            sys.exit(f"{sheet} has no column {name!r}; refusing")
    rows = [row for row in ws.iter_rows(min_row=2) if row[header[key_column]].value == key]
    if len(rows) != 1:
        sys.exit(f"{sheet}: expected one row with {key_column} {key!r}, found {len(rows)}; refusing")
    return rows[0][header[column]]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true", help="report what would change and write nothing")
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
            sys.exit(f"{where} holds {cell.value!r}, neither the value before this edit ({old!r}) nor after it "
                     f"({new!r}); refusing")

    if args.check or not to_write:
        print(f"{len(to_write)} of {len(EDITS)} cells to change; nothing written")
    else:
        for cell, new in to_write:
            cell.value = new
        book.save(args.workbook)
        after = snapshot(openpyxl.load_workbook(args.workbook))
        changed = {k for k in set(before) | set(after) if before.get(k) != after.get(k)}
        intended = {(c.parent.title, c.coordinate) for c, _ in to_write}
        if changed != intended:
            sys.exit(f"the save changed {sorted(changed - intended)} and missed {sorted(intended - changed)}; "
                     "inspect the workbook")
        print(f"wrote {len(to_write)} cells, and no other cell changed")

    if args.check or args.no_generate:
        return 0
    return subprocess.call([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], cwd=REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main())
