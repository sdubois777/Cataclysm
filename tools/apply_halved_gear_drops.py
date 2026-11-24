"""Write the halved Gear Drops column into the design workbook, and regenerate the CSVs.

    python tools/apply_halved_gear_drops.py                     # write the cells, then run tools/generate_datatables.py
    python tools/apply_halved_gear_drops.py --check             # say whether the workbook holds them, change nothing
    python tools/apply_halved_gear_drops.py --workbook COPY.xlsx --no-generate    # a copy, nothing regenerated

WHY THIS EXISTS. Issue #2853: the project owner ruled on 2026-11-24, choosing "Halve gear drops (Recommended)", that the
whole gear drop column halves. That column is the Gear Drops column of the Enemy Drops sheet of
`docs/All_Things_Cataclysm.xlsx`, which is authoritative. The branch that made the change (`feat/drop-supply-2853`) was
told not to edit the workbook, because other branches held unmerged edits to it and a binary cannot be merged. So the
branch carries everything that follows from the column -- `game/Data/EnemyDrops.csv` as the generator makes it from the
halved column, `loot.ENEMY_GEAR_DROPS`, the design document, the tests -- and this script, which makes the workbook
agree when it is free. Until it is run, `tools/tests/test_generate_datatables.py`'s
`test_the_committed_csvs_are_current` and `tools/tests/test_enemy_drop_sheet_matches_the_model.py`'s gear checks fail
on that branch, and say so.

WHAT IT WRITES, found by each row's Enemy Rarity and never by position, so rows another branch added or moved do not
matter: the six Gear Drops cells, each from its old figure to half of it, and the Common row's Note, whose old text
said "One kill in six drops something". Nothing else: the Gem Drops column stays where it is (tier 8 keeps its gem rate,
ruled the same day), and so do the Material and Gold Drops columns.

WHAT IT REFUSES. A workbook holding formulas, charts or images, because an openpyxl save drops a chart or an image and
would replace a formula with nothing without raising; a sheet with no such row, or more than one; and a Gear Drops cell
holding neither its old figure nor its new one, which means somebody else changed it and this must be looked at by hand.
It reads the file back and refuses if any cell other than the seven it meant to write changed.

AFTERWARDS `game/Content/Data/DT_EnemyDrops.uasset` needs the editor step,
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
SHEET = "Enemy Drops"
RARITY_HEADER = "Enemy Rarity"
GEAR_HEADER = "Gear Drops"
NOTE_HEADER = "Note"

#: Each enemy rarity's Gear Drops, before issue #2853 and after it: halved, every rarity alike.
GEAR_DROPS: dict[str, tuple[float, float]] = {
    "Common":         (0.16, 0.08),
    "Elite":          (0.5, 0.25),
    "Legendary":      (1.0, 0.5),
    "Herald":         (2.0, 1.0),
    "Boss":           (5.0, 2.5),
    "Cataclysm Boss": (12.0, 6.0),
}

#: The Common row's Note, which states the old figure's consequence.
NOTE_ROW = "Common"
OLD_NOTE = ("Path of Exile's figure for a normal monster, taken as the starting point. One kill in six drops something, "
            "and it adds no magic find, so a Common kill is the baseline both columns are read against.")
NEW_NOTE = ("Half of Path of Exile's figure for a normal monster, 0.16, which was the starting point; the whole column was "
            "halved on 2026-11-24 (issue #2853). About one kill in thirteen drops something, and it adds no magic find, "
            "so a Common kill is the baseline both columns are read against.")


class ApplyError(Exception):
    """Something about the workbook that makes writing the cells unsafe or impossible."""


def columns(sheet) -> dict[str, int]:
    """The sheet's header, each name to its 1-based column."""
    header = [cell.value for cell in sheet[1]]
    found = {}
    for name in (RARITY_HEADER, GEAR_HEADER, NOTE_HEADER):
        if header.count(name) != 1:
            raise ApplyError(f"{SHEET!r} has {header.count(name)} {name!r} column(s) in its header {header}, not one")
        found[name] = header.index(name) + 1
    return found


def rows_of(sheet, rarity_column: int) -> dict[str, int]:
    """The sheet row number of each of the six enemy rarities, found by its name."""
    out = {}
    for rarity in GEAR_DROPS:
        found = [row[0].row for row in sheet.iter_rows(min_row=2, min_col=rarity_column, max_col=rarity_column)
                 if row[0].value == rarity]
        if len(found) != 1:
            raise ApplyError(f"expected one {rarity!r} row on {SHEET!r}, found {len(found)}: rows {found}")
        out[rarity] = found[0]
    return out


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
        raise ApplyError(f"the workbook holds {formulas} formula(s), {charts} chart(s) and {images} image(s), which an "
                         "openpyxl save can lose without saying so; make the edit by hand in Excel instead")


def same(value, figure: float) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and abs(float(value) - figure) < 1e-12


def planned(sheet) -> list[tuple[int, int, object]]:
    """The cells still to write, as (row, column, new value). Refuses a Gear Drops cell holding neither figure."""
    at = columns(sheet)
    rows = rows_of(sheet, at[RARITY_HEADER])
    out = []
    for rarity, (old, new) in GEAR_DROPS.items():
        value = sheet.cell(rows[rarity], at[GEAR_HEADER]).value
        if same(value, new):
            continue
        if not same(value, old):
            raise ApplyError(f"{rarity}'s Gear Drops is {value!r}, neither {old:g} (before #2853) nor {new:g} (after "
                             "it); somebody else changed it, so look at it by hand")
        out.append((rows[rarity], at[GEAR_HEADER], new))
    note = sheet.cell(rows[NOTE_ROW], at[NOTE_HEADER]).value
    if note == OLD_NOTE:
        out.append((rows[NOTE_ROW], at[NOTE_HEADER], NEW_NOTE))
    elif note != NEW_NOTE:
        raise ApplyError(f"the {NOTE_ROW} row's Note is {note!r}, neither the old text nor the new; look at it by hand")
    return out


def apply(path: pathlib.Path = WORKBOOK) -> str:
    """Write the halved column into the workbook at `path`. Answers what it did."""
    book = openpyxl.load_workbook(path)
    refuse_what_a_save_would_lose(book)
    sheet = book[SHEET]
    writes = planned(sheet)
    if not writes:
        return "the Enemy Drops sheet already holds the halved Gear Drops column; nothing written"
    before = snapshot(book)
    for row, column, value in writes:
        sheet.cell(row, column).value = value
    book.save(path)

    # READ BACK FROM DISK: the same sheets in the same order, and only the planned cells changed.
    after_book = openpyxl.load_workbook(path)
    if after_book.sheetnames != list(before):
        raise ApplyError(f"the sheets changed: {list(before)} became {after_book.sheetnames}")
    after = snapshot(after_book)
    changed = sorted((title, key) for title, cells in after.items()
                     for key in set(cells) | set(before[title]) if cells.get(key) != before[title].get(key))
    meant = sorted((SHEET, (row, column)) for row, column, _ in writes)
    if changed != meant:
        raise ApplyError(f"the cells that changed are {changed}, not the {len(meant)} meant: {meant}")
    return f"{len(writes)} cell(s) written on {SHEET!r}: the Gear Drops column is halved, and no other cell changed"


def check(path: pathlib.Path = WORKBOOK) -> bool:
    """Whether the workbook at `path` already holds the halved column and the new note."""
    book = openpyxl.load_workbook(path)
    return not planned(book[SHEET])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="say whether the workbook holds the column; change nothing")
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK, help="the workbook to write (a copy, say)")
    parser.add_argument("--no-generate", action="store_true", help="write the cells without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        if args.check:
            held = check(args.workbook)
            print("The Enemy Drops sheet holds the halved Gear Drops column." if held
                  else "The Enemy Drops sheet does NOT hold the halved Gear Drops column.", flush=True)
            return 0 if held else 1
        print(apply(args.workbook), flush=True)
    except ApplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    if args.no_generate:
        return 0
    generator = REPO_ROOT / "tools" / "generate_datatables.py"
    for extra in ([], ["--check"]):
        result = subprocess.run([sys.executable, str(generator), "--workbook", str(args.workbook), *extra],
                                cwd=REPO_ROOT)
        if result.returncode != 0:
            return result.returncode
    print("Now rebuild the asset: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
