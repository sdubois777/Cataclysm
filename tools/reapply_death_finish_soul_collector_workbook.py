"""Write the Soul Collector's Fervour spender rows, Spirit Volley's and the `Use.SpentFervour` tag into the design workbook.

    python tools/reapply_death_finish_soul_collector_workbook.py
    python tools/reapply_death_finish_soul_collector_workbook.py --check
    python tools/reapply_death_finish_soul_collector_workbook.py --workbook <copy.xlsx> --no-generate

THE DEATH FINISH SLICE A (2026-11-26). The owner: "finish whatever is left in the death cataclysm". Five Soul Collector
nodes and one vow option had no rows, waiting on the Death weapon skills (`docs/DECISIONS.md`, the slice C3a entry's
reorder) and on new code:

  - Soul Fuel (`SoulCollector_basic_spine_009`), issue #2937, with the coordinating session's rulings 1 and 2 of
    2026-11-25 (`UCataclysmSkillTemplate::SoulFuelReaches`);
  - Deeper Draw, Pyre, Soul Shatter and Final Rites (`SoulCollector_basic_d_stem1`, `_d_a0`, `_d_a1`, `_d_a2`);
  - Spirit Volley, option 2 of `SoulCollector_capstone_50`, issue #2908 (`UCataclysmFervour::LooseSpiritVolley`).

"Skills that spend Fervour" is a tag a use carries when it spent any (`UCataclysmSkillTemplate::SpentFervourTag`), so
this also declares `Use.SpentFervour` on the Tags sheet, as `tools/apply_gadget_tag_2026_11_25.py` declared
`Type.Gadget`: the generator refuses a Required Tags naming a tag the sheet does not declare.

THIS SCRIPT OWNS ONLY THESE ROWS AND THAT ONE TAG. Its rows use the machinery of `tools/reapply_death_trees_workbook.py`
(`plan_effects`, imported), so a node holding no rows gets them appended, one holding exactly these is left alone, and
one holding anything else is refused and nothing is written. It is idempotent, refuses rather than guesses, and checks
by reading the workbook back that every other sheet and cell is unchanged. `--check` writes nothing and exits 1 when
something would be written. Run it after `tools/reapply_death_trees_workbook.py`, whose rows come first in the sheet.

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
TAG_LIST = REPO_ROOT / "tools" / "generate_gameplay_tags.py"

ReapplyError = base.ReapplyError
R = base.Row
Node = base.Node
SC = base.SC

TAGS_SHEET = "Tags"

#: The tag "skills that spend Fervour" are scoped by, and its description on the Tags sheet.
SPENT_FERVOUR_TAG = (
    "Use.SpentFervour",
    "Carried by a skill's use that spent Fervour: its cost, Soul Fuel's spend or every Fervour held. Put on the "
    "skill's tags when the use is paid for and taken off at the next use that spends none, so a passive row scoped to "
    "it reaches every blow of that use")

#: The rows, by node. Soul Fuel's 10 is one row from its first point; its damage is "+1% for every 4 points", a row at
#: each of 4, 8 and 12 (the owner's whole-numbers rule). Deeper Draw's "1 more Fervour per point" is on the same spend,
#: whose tenth point makes the 20 its sentence names. Pyre's figure is read with the use's spend in fives. Soul Shatter
#: and Final Rites are rows scoped to the tag; "Elite and stronger" is rung 1 and up, as Soul Collector's other rows on
#: the strong write it.
NODES: tuple[base.Node, ...] = (
    Node(SC + "basic_spine_009", "Soul Fuel",
         (R("skill_fervour_spend", "flat", 10, min_points=1),)
         + tuple(R("damage_per_skill_fervour_spent", "flat", 1, min_points=points) for points in (4, 8, 12))),
    Node(SC + "basic_d_stem1", "Deeper Draw", (R("skill_fervour_spend", "flat", 1),)),
    Node(SC + "basic_d_a0", "Pyre", (R("area_of_effect_per_five_fervour_spent", "flat", 1),)),
    Node(SC + "basic_d_a1", "Soul Shatter", (R("crit_chance", "increased", 4, tags=SPENT_FERVOUR_TAG[0]),)),
    Node(SC + "basic_d_a2", "Final Rites",
         base.damage("increased", 3, tags=SPENT_FERVOUR_TAG[0], cond=base.RUNG_AT_LEAST, cond_value=1)),
    Node(SC + "capstone_50", "Spirit Volley", (R("spirit_volley_weapon_percent", "flat", 30),), option=2),
)


def plan_tag(book) -> list[tuple[object, str, str]]:
    """(cell, text, what) for the tag row the Tags sheet lacks; refuses one somebody wrote differently."""
    tags = book[TAGS_SHEET]
    declared = [row for row in tags.iter_rows(min_row=2) if row[0].value == SPENT_FERVOUR_TAG[0]]
    if len(declared) > 1:
        raise ReapplyError(f"the Tags sheet declares {SPENT_FERVOUR_TAG[0]} {len(declared)} times")
    if declared:
        if declared[0][1].value != SPENT_FERVOUR_TAG[1]:
            raise ReapplyError(f"the Tags sheet declares {SPENT_FERVOUR_TAG[0]} as {declared[0][1].value!r}, not this "
                               f"script's description: somebody has written it since")
        return []
    last = max((cell.row for row in tags.iter_rows(min_row=2, max_col=2) for cell in row if cell.value is not None),
               default=1)
    return [(tags.cell(last + 1, 1), SPENT_FERVOUR_TAG[0], f"Tags: + {SPENT_FERVOUR_TAG[0]}"),
            (tags.cell(last + 1, 2), SPENT_FERVOUR_TAG[1], f"Tags: {SPENT_FERVOUR_TAG[0]}'s description")]


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Add this slice's rows and tag to the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    base.refuse_what_a_save_would_lose(book)
    effects = base.plan_effects(book[base.EFFECTS_SHEET], NODES)
    tag = plan_tag(book)
    changed = effects.changed + len(tag)
    if not changed:
        return ["every row and tag of the Death finish slice A is already written; nothing written"]
    report = effects.report + [what for _, _, what in tag]
    if check_only:
        return report + [f"{changed} change(s) to make; nothing written (--check)"]

    others = {s.title: base._snapshot(s) for s in book.worksheets if s.title not in (base.EFFECTS_SHEET, TAGS_SHEET)}
    tags_before = base._snapshot(book[TAGS_SHEET])
    base._write(book[base.EFFECTS_SHEET], effects)
    written = {(cell.row, cell.column): text for cell, text, _ in tag}
    for cell, text, _ in tag:
        cell.value = text
    book.save(path)

    # READ BACK FROM DISK: the other sheets as they were, the Tags sheet with only the new row, and the rows as planned.
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if base._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and it was not written")
    if base._snapshot(after[TAGS_SHEET]) != {**tags_before, **written}:
        raise ReapplyError(f"the sheet {TAGS_SHEET!r} did not read back as written")
    base._read_back(after, effects, base.EFFECTS_SHEET)
    return report + [f"wrote {changed} change(s); {base.EFFECTS_SHEET!r} now holds {len(effects.new)} rows (was "
                     f"{len(effects.old)}), and no other sheet changed"]


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
    # THE CSVs, AND THE TAG LIST UNREAL READS, which its own generator writes from the Tags sheet.
    for command in ([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], [sys.executable, str(TAG_LIST)]):
        result = subprocess.run(command, cwd=REPO_ROOT)
        if result.returncode != 0:
            return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
