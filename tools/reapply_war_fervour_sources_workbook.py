"""Write the War starting nodes' Fervour rows into the design workbook's Passive Effects sheet.

    python tools/reapply_war_fervour_sources_workbook.py
    python tools/reapply_war_fervour_sources_workbook.py --check

Slice C of `plan_death_and_war.md` (2026-11-24): the rows that make the three War
classes fill Fervour, on the starting nodes whose sentences
`tools/apply_war_fervour_sources_2026_11_24.py` makes state them. Nine rows:

    Bulwark_basic_trunk_000    fervour_per_hit_taken 1, fervour_per_block 1,
                               fervour_decay_out_of_combat_per_second 5
    Berserker_trunk_000        fervour_per_critical_strike 1,
                               fervour_decay_out_of_combat_per_second 10
    Saboteur_basic_trunk_000   fervour_per_trap_placed 1, fervour_per_gadget_placed 1,
                               fervour_per_trap_triggered 1, fervour_per_device_hit 1

`docs/All_Things_Cataclysm.xlsx` is a binary nobody can merge, so the rows are
written by this script and the script is what a reviewer reads. IT IS IDEMPOTENT:
a node already holding exactly its rows is left alone. IT REFUSES RATHER THAN
GUESSES: a node holding any other rows was written by somebody else since, and
nothing is written. A node with no rows gets its rows appended at the end of the
sheet. Every other row, and every other sheet, is left as it was, which the
script checks by reading the workbook back from disk.

THE ROW HELPERS ARE `tools/reapply_demonic_tree_rework_workbook.py`'s, imported:
the row shape, the cells it writes, the refusal of a workbook holding formulas,
charts or images, and the read-back snapshot.

Then it regenerates the CSVs with `tools/generate_datatables.py`, unless
`--no-generate`. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_demonic_tree_rework_workbook as rows_of  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"
SHEET = rows_of.SHEET
R = rows_of.Row
ReapplyError = rows_of.ReapplyError

#: Each starting node, the `Node Name` its rows carry, and its rows.
NODES: tuple[tuple[str, str, tuple[rows_of.Row, ...]], ...] = (
    ("Bulwark_basic_trunk_000", "Fervour", (
        R("fervour_per_hit_taken", "flat", 1),
        R("fervour_per_block", "flat", 1),
        R("fervour_decay_out_of_combat_per_second", "flat", 5),
    )),
    ("Berserker_trunk_000", "Fervour", (
        R("fervour_per_critical_strike", "flat", 1),
        R("fervour_decay_out_of_combat_per_second", "flat", 10),
    )),
    ("Saboteur_basic_trunk_000", "Fervour", (
        R("fervour_per_trap_placed", "flat", 1),
        R("fervour_per_gadget_placed", "flat", 1),
        R("fervour_per_trap_triggered", "flat", 1),
        R("fervour_per_device_hit", "flat", 1),
    )),
)


def plan(sheet) -> tuple[list[str], list[dict[str, object]], list[str]]:
    """The header, the rows to append, and the report. Raises ReapplyError."""
    header = [rows_of._text(cell.value) for cell in sheet[1]]
    while header and header[-1] is None:
        header.pop()
    missing = [column for column in rows_of.COLUMNS if column not in header]
    if missing:
        raise ReapplyError(f"{SHEET!r} has no column(s) {missing}; its header is {header}")

    held: dict[str, list[rows_of.Row]] = {}
    for values in sheet.iter_rows(min_row=2, max_col=len(header), values_only=True):
        cells = dict(zip(header, values, strict=False))
        node = rows_of._text(cells.get("Node"))
        if node:
            held.setdefault(node, []).append(rows_of.row_from_cells(cells))

    append: list[dict[str, object]] = []
    report: list[str] = []
    for node, label, wanted in NODES:
        current = held.get(node, [])
        if rows_of._multiset(current) == rows_of._multiset(wanted):
            report.append(f"{node}: already holds its {len(wanted)} row(s); left alone")
            continue
        if current:
            raise ReapplyError(
                f"{node} holds {[row.key() for row in current]}, which are not the rows slice C "
                f"writes; somebody has written it since. Nothing was written")
        for row in wanted:
            append.append(rows_of.cells_from_row(node, label, row))
            report.append(f"{node}: + {rows_of._describe(row)}")
    return header, append, report


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    book = openpyxl.load_workbook(path)
    rows_of.refuse_what_a_save_would_lose(book)
    sheet = book[SHEET]
    header, append, report = plan(sheet)
    if not append:
        return report + ["every row is already written; nothing written"]
    if check_only:
        return report + [f"{len(append)} row(s) to write; nothing written (--check)"]

    others = {s.title: rows_of._snapshot(s) for s in book.worksheets if s.title != SHEET}
    before = rows_of._snapshot(sheet)
    # THE LAST ROW HOLDING ANYTHING, so a blank row inside the sheet is not mistaken for its end.
    last = max((cell.row for row in sheet.iter_rows(min_row=2, max_col=len(header)) for cell in row
                if cell.value is not None), default=1)
    for offset, cells in enumerate(append, start=1):
        for column, name in enumerate(header, start=1):
            sheet.cell(last + offset, column).value = cells.get(name)
    book.save(path)

    after = openpyxl.load_workbook(path)
    for title, cells in others.items():
        if rows_of._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and only {SHEET!r} was written")
    written = rows_of._snapshot(after[SHEET])
    kept = {where: value for where, value in written.items() if where[0] <= last}
    if kept != before:
        raise ReapplyError(f"a row above the appended ones changed on saving in {SHEET!r}")
    _, still_missing, _ = plan(after[SHEET])
    if still_missing:
        raise ReapplyError(f"{SHEET!r} did not read back with the rows written")
    return report + [f"wrote {len(append)} row(s) after sheet row {last}; no other row or sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true",
                        help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if any(line.endswith("(--check)") for line in report) else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)],
                            cwd=REPO_ROOT)
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
