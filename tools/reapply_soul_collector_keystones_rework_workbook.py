"""Write the Soul Collector keystone rework's rows into the design workbook's Passive Effects sheet.

    python tools/reapply_soul_collector_keystones_rework_workbook.py
    python tools/reapply_soul_collector_keystones_rework_workbook.py --check
    python tools/reapply_soul_collector_keystones_rework_workbook.py --workbook <copy.xlsx> --no-generate

ISSUE #2951, the rework of 2026-11-27. The owner's words, the five answers and the rulings are in `docs/DECISIONS.md`;
the sentences these rows stand behind are written into the tree file by
`tools/apply_soul_collector_keystones_rework_2026_11_27.py`. Twenty nodes change: the starting node, eight keystones and
eleven small nodes. Every number is a judgement under the owner's delegation, the small nodes' the coordinating
session's ruling within the owner's approval.

THE WORKBOOK CANNOT BE MERGED, so this script is how the change reaches it: it rewrites a node only when the node holds
exactly the rows it held on `development` 79d3da751 (each node's `before`), leaves alone one already holding the new
rows, and refuses anything else. It uses the machinery of `tools/reapply_death_trees_workbook.py` (`plan_effects` with
`Node.before`), checks by reading the workbook back that no other sheet or cell moved, and `--check` writes nothing and
exits 1 when something would be written.

AFTERWARDS the DataTable assets need the editor step,
`python tools/run_editor_python.py tools/generate_datatable_assets.py`.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_death_trees_workbook as base  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"

ReapplyError = base.ReapplyError

#: The rows, by node. They are held by `tools/reapply_death_trees_workbook.py`, which writes every Death row, so that
#: the two scripts cannot disagree about what the Soul Collector's nodes hold.
NODES: tuple[base.Node, ...] = base.SOUL_COLLECTOR_REWORK


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Rewrite the twenty nodes' rows in the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    base.refuse_what_a_save_would_lose(book)
    effects = base.plan_effects(book[base.EFFECTS_SHEET], NODES)
    if not effects.changed:
        return ["every row of the Soul Collector keystone rework is already written; nothing written"]
    if check_only:
        return effects.report + [f"{effects.changed} change(s) to make; nothing written (--check)"]

    others = {s.title: base._snapshot(s) for s in book.worksheets if s.title != base.EFFECTS_SHEET}
    base._write(book[base.EFFECTS_SHEET], effects)
    book.save(path)

    # READ BACK FROM DISK: every other sheet as it was, and the rows as planned.
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if base._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and it was not written")
    base._read_back(after, effects, base.EFFECTS_SHEET)
    return effects.report + [f"wrote {effects.changed} change(s); {base.EFFECTS_SHEET!r} now holds "
                             f"{len(effects.new)} rows (was {len(effects.old)}), and no other sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true", help="write the rows without regenerating the CSVs")
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
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
