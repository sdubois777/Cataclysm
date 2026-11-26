"""Re-apply the Abyssal Rifts row's new text to the design workbook, and regenerate the CSVs.

    python tools/reapply_abyssal_rifts_row.py            # write the cell, then run tools/generate_datatables.py
    python tools/reapply_abyssal_rifts_row.py --check    # say whether the workbook already holds it, change nothing

WHY THIS EXISTS. Issue #2864, the owner's redesign of Abyssal Rifts, changed one cell of
`docs/All_Things_Cataclysm.xlsx`: the Description of the Demonic row "Abyssal Rifts" on the Dungeon Modifiers sheet.
The workbook is a binary git cannot merge, so when another branch's workbook edit reaches `development` first, this
branch takes `development`'s workbook and redoes its own edit on top of it. This script is that redo: it finds the row
by its Cataclysm type and name rather than by its position, so rows another branch added or moved do not matter, writes
the one cell, reads the file back to prove that cell is the only one that changed, and regenerates
`game/Data/DungeonModifiers.csv` and the other generated tables with `tools/generate_datatables.py`.

WHAT IT REFUSES. A workbook holding formulas, charts or images, because an openpyxl save drops a chart or an image and
would replace a formula with nothing without raising (measured 0, 0 and 0 on 2026-11-24); and a sheet with no such row,
or more than one.

AFTERWARDS `game/Content/Data/DT_DungeonModifiers.uasset` needs the editor step,
`python tools/run_editor_python.py tools/generate_datatable_assets.py`, as any change to a generated CSV does.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
SHEET = "Dungeon Modifiers"
CATACLYSM = "Demonic"
NAME = "Abyssal Rifts"

#: The row's text before #2864, which `development` held until this edit lands.
OLD_TEXT = ("Portals to the Abyss open up, unleashing waves of demonic creatures. Players must close these rifts by "
            "defeating waves of enemies within a given time limit. The difficulty and rewards increase with each "
            "successfully closed rift.")

#: The row's text since #2864. `UCataclysmDungeonModifierEffects::AbyssalRiftsKey` quotes it.
NEW_TEXT = ("Portals to the Abyss tear open across the dungeon, several to a floor. From the moment you enter, each rift "
            "sends a demonic creature every 3 seconds to hunt you down, leaving a trail back to its source. A rift is a "
            "boss with great health that must be destroyed to stop it, and the creatures it sends grow stronger the "
            "longer it stays open. The rewards increase with each rift destroyed.")


class ReapplyError(Exception):
    """Something about the workbook that makes writing the cell unsafe or impossible."""


def find_row(sheet) -> int:
    """The sheet row number of the Abyssal Rifts row, found by its Cataclysm type and name."""
    found = [row[0].row for row in sheet.iter_rows(min_row=2)
             if row[0].value == CATACLYSM and row[1].value == NAME]
    if len(found) != 1:
        raise ReapplyError(f"expected one {CATACLYSM} {NAME!r} row on {SHEET!r}, found {len(found)}: rows {found}")
    return found[0]


def snapshot(book) -> dict[str, dict[tuple[int, int], object]]:
    """Every cell's value, sheet by sheet."""
    return {sheet.title: {(cell.row, cell.column): cell.value for row in sheet.iter_rows() for cell in row}
            for sheet in book.worksheets}


def refuse_what_a_save_would_lose(book) -> None:
    formulas = sum(1 for sheet in book.worksheets for row in sheet.iter_rows() for cell in row
                   if isinstance(cell.value, str) and cell.value.startswith("="))
    charts = sum(len(sheet._charts) for sheet in book.worksheets)
    images = sum(len(sheet._images) for sheet in book.worksheets)
    if formulas or charts or images:
        raise ReapplyError(f"the workbook holds {formulas} formula(s), {charts} chart(s) and {images} image(s), which an "
                           "openpyxl save can lose without saying so; make the edit by hand in Excel instead")


def apply(path: pathlib.Path = WORKBOOK) -> str:
    """Write the new text into the row's Description cell of the workbook at `path`. Answers what it did."""
    book = openpyxl.load_workbook(path)
    refuse_what_a_save_would_lose(book)
    sheet = book[SHEET]
    header = [cell.value for cell in sheet[1]]
    if header[:4] != ["Cataclysm Type", "Modifier Name", "Weight", "Description"]:
        raise ReapplyError(f"{SHEET!r} has the header {header}, not the four columns this script writes by position")
    row = find_row(sheet)
    if sheet.cell(row, 4).value == NEW_TEXT:
        return f"row {row} already holds the new text; nothing written"
    before = snapshot(book)
    sheet.cell(row, 4).value = NEW_TEXT
    book.save(path)

    # READ BACK FROM DISK: the same sheets in the same order, and only the one cell changed.
    after_book = openpyxl.load_workbook(path)
    if after_book.sheetnames != list(before):
        raise ReapplyError(f"the sheets changed: {list(before)} became {after_book.sheetnames}")
    after = snapshot(after_book)
    changed = [(title, key) for title, cells in after.items()
               for key in set(cells) | set(before[title]) if cells.get(key) != before[title].get(key)]
    if changed != [(SHEET, (row, 4))]:
        raise ReapplyError(f"cells other than {SHEET!r} row {row} column D changed: {changed}")
    return f"row {row} written: its Description is the #2864 text, and no other cell changed"


def check(path: pathlib.Path = WORKBOOK) -> bool:
    """Whether the workbook at `path` already holds the new text."""
    book = openpyxl.load_workbook(path, read_only=True)
    sheet = book[SHEET]
    for row in sheet.iter_rows(min_row=2, values_only=True):
        if row[0] == CATACLYSM and row[1] == NAME:
            return row[3] == NEW_TEXT
    raise ReapplyError(f"no {CATACLYSM} {NAME!r} row on {SHEET!r}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="say whether the workbook holds the text; change nothing")
    parser.add_argument("--no-generate", action="store_true", help="write the cell without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        if args.check:
            held = check()
            print("The Abyssal Rifts row holds the #2864 text." if held
                  else "The Abyssal Rifts row does NOT hold the #2864 text.", flush=True)
            return 0 if held else 1
        print(apply(), flush=True)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    if args.no_generate:
        return 0
    generator = REPO_ROOT / "tools" / "generate_datatables.py"
    for extra in ([], ["--check"]):
        result = subprocess.run([sys.executable, str(generator), *extra], cwd=REPO_ROOT)
        if result.returncode != 0:
            return result.returncode
    print("Now rebuild the asset: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
