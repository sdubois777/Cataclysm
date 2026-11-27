"""Add where the hand holds each weapon mesh to the Weapon Meshes sheet: six grip columns, and the three swords turned.

    python tools/apply_weapon_grips_2026_11_27.py
    python tools/apply_weapon_grips_2026_11_27.py --check

THE OWNER, PLAY-TESTING ON 2026-11-27: "he's holding the greatsword by the tip.. instead of the handle". The hand
socket holds a mesh's origin, and the Medieval Weapons pack put the origin of `SM_Sword_3`, `SM_Sword_1` and
`SM_Dagger` at the tip of the blade, with the hilt at the far end of +Z. Measured on 2026-11-27 from each mesh's own
vertex positions, read out of the `.uasset` without the editor; `game/docs/weapon-source-assets.md` records how and
the figures.

What this writes into `docs/All_Things_Cataclysm.xlsx`, and nothing else:

  - Weapon Meshes: six columns after Scale -- Grip X, Grip Y, Grip Z (centimetres, the mesh point the hand holds, in
    the mesh's own axes, before Scale) and Grip Pitch, Grip Yaw, Grip Roll (degrees, the turn about that point).
  - Every row gets all six. The Sword, Dagger and Greatsword hold their grip and are turned 180 degrees of roll so the
    blade points away from the hand; the Warhammer holds its mace's grip, lower on the shaft; every other row is 0.

IT IS IDEMPOTENT: a cell already holding its new value is left alone. IT REFUSES RATHER THAN GUESSES: a grip cell
holding anything but nothing or its new value was changed by somebody else, and nothing is written. Every other cell
of the sheet, and every other sheet, is checked unchanged by reading the workbook back.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_demonic_tree_rework_workbook as helpers  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"
SHEET = "Weapon Meshes"
ReapplyError = helpers.ReapplyError

#: The six columns, in the order they follow Scale.
COLUMNS = ("Grip X", "Grip Y", "Grip Z", "Grip Pitch", "Grip Yaw", "Grip Roll")

#: Base name -> (Grip X, Grip Y, Grip Z, Grip Pitch, Grip Yaw, Grip Roll). A base not named here gets six zeros.
#:
#: THE THREE SWORDS: the grip is the middle of the narrow run between the crossguard and the pommel, read in 1 cm
#: slices of each mesh's vertices (SM_Sword_3 grip Z 78-88, SM_Sword_1 63-76, SM_Dagger 22-31), and 180 degrees of
#: roll puts the blade toward +Z from the hand, as every mesh held at its origin points.
#: THE WARHAMMER: SM_Mace's origin is on its plain shaft about 10 cm above its wrapped grip (Z -30 to -9), so it is held
#: at Z -19 and not turned. A judgement on where the grip is, which the owner is asked to look at.
GRIPS = {
    "Sword": (0, 0, 69.5, 0, 0, 180),
    "Dagger": (0, 0, 26.5, 0, 0, 180),
    "Greatsword": (0, 0, 83, 0, 0, 180),
    "Warhammer": (0, 0, -19, 0, 0, 0),
}


def _header(sheet) -> list:
    return [cell.value for cell in sheet[1]]


def plan(book) -> list[tuple[int, int, object, str]]:
    """Every cell to write, as (row, column, value, description); refuses a cell somebody else changed."""
    sheet = book[SHEET]
    header = _header(sheet)
    if "Base Name" not in header or "Scale" not in header:
        raise ReapplyError(f"{SHEET!r} has no 'Base Name' or 'Scale' column")
    base_column = header.index("Base Name") + 1
    first = header.index("Scale") + 2
    writes = []
    for offset, name in enumerate(COLUMNS):
        column = first + offset
        held = sheet.cell(row=1, column=column).value
        if held not in (None, name):
            raise ReapplyError(f"{SHEET!r} column {column} is headed {held!r}, not empty or {name!r}")
        if held is None:
            writes.append((1, column, name, f"added the column {name!r}"))
    for row in range(2, sheet.max_row + 1):
        base = sheet.cell(row=row, column=base_column).value
        if not base:
            continue
        values = GRIPS.get(base, (0, 0, 0, 0, 0, 0))
        for offset, value in enumerate(values):
            column = first + offset
            held = sheet.cell(row=row, column=column).value
            if held is None:
                writes.append((row, column, value, f"{base}: {COLUMNS[offset]} = {value}"))
            elif not isinstance(held, (int, float)) or float(held) != float(value):
                raise ReapplyError(f"{SHEET!r} row {row} ({base}) {COLUMNS[offset]} holds {held!r}, not {value}; "
                                   f"somebody else changed it")
    unknown = sorted(set(GRIPS) - {sheet.cell(row=r, column=base_column).value for r in range(2, sheet.max_row + 1)})
    if unknown:
        raise ReapplyError(f"{SHEET!r} has no row for {unknown}")
    return writes


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Write the grip cells the workbook at `path` lacks; return the report."""
    book = openpyxl.load_workbook(path)
    helpers.refuse_what_a_save_would_lose(book)
    writes = plan(book)
    if not writes:
        return ["every grip is already written; nothing written"]
    report = [what for _, _, _, what in writes]
    if check_only:
        return report + [f"{len(writes)} cell(s) to write; nothing written (--check)"]
    before = {sheet.title: helpers._snapshot(sheet) for sheet in book.worksheets}
    sheet = book[SHEET]
    for row, column, value, _ in writes:
        sheet.cell(row=row, column=column).value = value
    book.save(path)
    after = openpyxl.load_workbook(path)
    for title, cells in before.items():
        expected = dict(cells)
        if title == SHEET:
            for row, column, value, _ in writes:
                expected[(row, column)] = value
        if helpers._snapshot(after[title]) != expected:
            raise ReapplyError(f"the sheet {title!r} did not read back as written")
    if plan(after):
        raise ReapplyError("the workbook did not read back with every grip written")
    return report + [f"wrote {len(writes)} cell(s); no other cell or sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true", help="write the cells without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if report[-1].endswith("(--check)") else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], cwd=REPO_ROOT)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
