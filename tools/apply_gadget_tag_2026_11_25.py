"""Put `Type.Gadget` on every machine that is not a trap, and on the passive rows that mean gadgets alone.

    python tools/apply_gadget_tag_2026_11_25.py
    python tools/apply_gadget_tag_2026_11_25.py --check

THE COORDINATING SESSION'S RULING OF 2026-11-25 (the Saboteur survey's question 1): "add Type.Gadget to every machine
that is not a trap, and move every PASSIVE row that means gadgets only onto it". Until then a row meaning gadgets named
`Type.Deployable`, which traps carry too, so "Your gadgets reserve 10% reduced Fervour" would have lowered a trap's
reserve as well.

What this writes into `docs/All_Things_Cataclysm.xlsx`, and nothing else:

  - Tags: a row declaring `Type.Gadget`, appended after the last, since the generator writes
    `game/Config/Tags/CataclysmTags.ini` from this sheet and refuses a tag it does not declare.
  - Minion Types: `Type.Gadget` after `Type.Deployable` in the Tags of BoltTurret, Ballista and SkillGadget, the three
    deployable types that are not traps (`ACataclysmMinion::IsGadget`). SpikeTrap and SkillTrap are left as they are.
  - Passive Effects: Support Network's two "+3% gadget damage per point" rows (`Saboteur_basic_gadget_stem_005`,
    `attack_damage` and `spell_damage`) from `Type.Deployable` to `Type.Gadget`. The Saboteur's other gadget rows are
    written on `Type.Gadget` by their own script.

NO ENCHANTMENT ROW IS TOUCHED: the ruling keeps their Weights and Tags. The decisions entry lists the enchantment rows
that mean gadgets and still read `Type.Deployable`.

IT IS IDEMPOTENT: a cell already in its new state is left alone. IT REFUSES RATHER THAN GUESSES: a cell holding
anything but its old or its new text was changed by somebody else, and nothing is written. Every other cell of the two
sheets, and every other sheet, is checked unchanged by reading the workbook back.
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
TAG_LIST = REPO_ROOT / "tools" / "generate_gameplay_tags.py"
ReapplyError = helpers.ReapplyError

#: (minion type, Tags before, Tags after).
MINION_TAGS = tuple(
    (name, f"Type.Minion, Type.Deployable, Minion.Machine{rest}",
     f"Type.Minion, Type.Deployable, Type.Gadget, Minion.Machine{rest}")
    for name, rest in (("BoltTurret", ", Minion.Projectile"), ("Ballista", ", Minion.Projectile"), ("SkillGadget", "")))

#: The Tags sheet's new row: (Tag Name, Description).
GADGET_TAG = ("Type.Gadget", "Deployable machines that are not traps: turrets, ballistae, skill gadgets")

#: (node, stat, Required Tags before, Required Tags after).
PASSIVE_TAGS = (
    ("Saboteur_basic_gadget_stem_005", "attack_damage", "Type.Deployable", "Type.Gadget"),
    ("Saboteur_basic_gadget_stem_005", "spell_damage", "Type.Deployable", "Type.Gadget"),
)


def _cell_of(sheet, header: list, key_columns: dict[str, str], column: str):
    """The one cell of `column` on the one row whose `key_columns` hold those values. Refuses none or several."""
    found = [row for row in sheet.iter_rows(min_row=2)
             if all(row[header.index(name)].value == value for name, value in key_columns.items())]
    if len(found) != 1:
        raise ReapplyError(f"{sheet.title!r}: {len(found)} rows hold {key_columns}, not one. Nothing was written")
    return found[0][header.index(column)]


def plan(book) -> list[tuple[object, str, str]]:
    """(cell, new text, what it is) for every cell to write; refuses a cell somebody changed."""
    writes = []
    tags = book["Tags"]
    declared = [row for row in tags.iter_rows(min_row=2) if row[0].value == GADGET_TAG[0]]
    if len(declared) > 1:
        raise ReapplyError(f"the Tags sheet declares {GADGET_TAG[0]} {len(declared)} times. Nothing was written")
    if declared:
        if declared[0][1].value != GADGET_TAG[1]:
            raise ReapplyError(f"the Tags sheet declares {GADGET_TAG[0]} as {declared[0][1].value!r}, not "
                               f"{GADGET_TAG[1]!r}: somebody has written it since. Nothing was written")
    else:
        last = max((cell.row for row in tags.iter_rows(min_row=2, max_col=2) for cell in row
                    if cell.value is not None), default=1)
        writes.append((tags.cell(last + 1, 1), GADGET_TAG[0], f"Tags: + {GADGET_TAG[0]}"))
        writes.append((tags.cell(last + 1, 2), GADGET_TAG[1], f"Tags: {GADGET_TAG[0]}'s description"))
    minions = book["Minion Types"]
    header = [cell.value for cell in minions[1]]
    for name, before, after in MINION_TAGS:
        cell = _cell_of(minions, header, {"Minion Type": name}, "Tags")
        if cell.value == after:
            continue
        if cell.value != before:
            raise ReapplyError(f"Minion Types {name}'s Tags read {cell.value!r}, neither {before!r} nor {after!r}: "
                               f"somebody has changed it since. Nothing was written")
        writes.append((cell, after, f"Minion Types {name}: Tags + Type.Gadget"))
    effects = book[helpers.SHEET]
    header = [cell.value for cell in effects[1]]
    for node, stat, before, after in PASSIVE_TAGS:
        cell = _cell_of(effects, header, {"Node": node, "Stat": stat}, "Required Tags")
        if cell.value == after:
            continue
        if cell.value != before:
            raise ReapplyError(f"{node} {stat}'s Required Tags read {cell.value!r}, neither {before!r} nor {after!r}: "
                               f"somebody has changed it since. Nothing was written")
        writes.append((cell, after, f"{node} {stat}: Required Tags {before} -> {after}"))
    return writes


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Write the new tags the workbook at `path` lacks; return the report."""
    book = openpyxl.load_workbook(path)
    helpers.refuse_what_a_save_would_lose(book)
    writes = plan(book)
    if not writes:
        return ["every tag is already written; nothing written"]
    report = [what for _, _, what in writes]
    if check_only:
        return report + [f"{len(writes)} cell(s) to write; nothing written (--check)"]
    before = {sheet.title: helpers._snapshot(sheet) for sheet in book.worksheets}
    changed = {(cell.parent.title, cell.row, cell.column): text for cell, text, _ in writes}
    for cell, text, _ in writes:
        cell.value = text
    book.save(path)
    after = openpyxl.load_workbook(path)
    for title, cells in before.items():
        expected = dict(cells)
        for (sheet_title, row, column), text in changed.items():
            if sheet_title == title:
                expected[(row, column)] = text
        if helpers._snapshot(after[title]) != expected:
            raise ReapplyError(f"the sheet {title!r} did not read back as written")
    if plan(after):
        raise ReapplyError("the workbook did not read back with the tags written")
    return report + [f"wrote {len(writes)} cell(s); no other cell or sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true", help="write the tags without regenerating the CSVs")
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
    # THE CSVs, AND THE TAG LIST UNREAL READS (`game/Config/Tags/CataclysmTags.ini`), which is written from the Tags
    # sheet by its own generator.
    for command in ([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], [sys.executable, str(TAG_LIST)]):
        result = subprocess.run(command, cwd=REPO_ROOT)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
