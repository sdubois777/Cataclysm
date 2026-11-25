"""Write three owner rulings of 2026-11-25 into the design workbook and the War tree files.

    python tools/apply_war_owner_rulings_2026_11_25.py
    python tools/apply_war_owner_rulings_2026_11_25.py --check

THE OWNER'S RULINGS OF 2026-11-25, relayed by the coordinating session, each verbatim in the decisions entry:

  1. Berserking (`Berserker_trunk_009`), "yeah lower it to 1%": its `berserking_damage_per_fervour` row goes from 10 to
     1 a point, and its sentence from "+10% bonus damage" to "+1% bonus damage".
  2. The 18 enchantments whose sentences say "gadget" and whose 21 rows read `Type.Deployable`, "agree with your rec":
     each of those rows requires `Type.Gadget` instead, so it reaches the machines that are not traps. The enchantment
     that says "deployables" keeps `Type.Deployable`.
  3. The Unbreakable Wall (Bulwark Final Oath, option 1), "accepted": the sentence "Every block negates the whole hit."
     and one row, the flag `every_block_negates`.

IDEMPOTENT AND REFUSING: a cell or sentence already holding the new text is left alone; one holding the old text is
written; one holding anything else is refused and nothing is saved. The Wall's row uses slice D's row machinery
(`tools/reapply_war_data_rows_workbook.py`), which refuses the same way. `--check` writes nothing and exits 1 when
anything would be written.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DOCS = REPO_ROOT / "docs"
LABEL = "the owner's rulings of 2026-11-25"

#: Ruling 1: the Berserking row's value per point.
BERSERKING = ("Berserker_trunk_009", "berserking_damage_per_fervour", 10, 1)

#: Ruling 2: the 18 enchantments that say "gadget" and read `Type.Deployable`, 21 rows between them.
GADGET_ENCHANTMENTS = (
    "Positive_Gadgets_deal_20_40_increased_damage",
    "Positive_Gadgets_deal_bonus_damage_equal_to_3_6_of_your",
    "Positive_While_stationary_your_gadgets_deal_20_40_incr",
    "Positive_Gadgets_have_40_70_increased_HP",
    "Positive_Gadgets_last_30_60_longer",
    "Positive_Gadgets_fire_20_40_faster",
    "Positive_Gadgets_apply_a_15_25_armor_reduction_to_enemi",
    "Positive_Gadgets_deal_5_10_increased_damage_for_each_se",
    "Positive_Gadgets_that_survive_for_15_seconds_gain_a_perma",
    "Positive_Gadgets_have_20_40_increased_attack_range",
    "Positive_When_any_of_your_gadgets_is_destroyed_all_remai",
    "Positive_Your_gadgets_prioritize_the_highest_HP_enemy_wit",
    "Positive_Gadgets_fire_at_all_enemies_within_range_simulta",
    "Positive_When_a_gadget_kills_an_enemy_it_fires_an_additi",
    "Positive_Your_gadgets_are_invincible_but_lose_5_of_their",
    "Positive_Gadgets_copy_the_last_skill_you_used_and_fire_it",
    "Positive_When_a_gadget_is_destroyed_it_arms_all_traps_wit",
    "Positive_When_a_gadget_is_destroyed_the_nearest_active_t",
)
GADGET_ROWS = 21
OLD_TAG, NEW_TAG = "Type.Deployable", "Type.Gadget"


@dataclass(frozen=True)
class Sentence:
    tree: str
    node: str
    option: int | None
    before: str
    after: str


SENTENCES = (
    Sentence("Berserker", "trunk_009", None,
             "When you reach maximum Fervour your next melee attack consumes all Fervour. For each point of Fervour "
             "consumed that attack deals +10% bonus damage per point invested.",
             "When you reach maximum Fervour your next melee attack consumes all Fervour. For each point of Fervour "
             "consumed that attack deals +1% bonus damage per point invested."),
    Sentence("Bulwark", "capstone_200", 1,
             "You may equip a shield in your primary weapon slot. You may now block AOE damage.",
             "Every block negates the whole hit."),
)

#: Ruling 3: The Unbreakable Wall's row, in the shape `base.NODES` takes.
NODES = (("Bulwark_capstone_200", "Final Oath", (base.Row("every_block_negates", "flat", 1, option=1),)),)


class RulingError(Exception):
    """A cell or sentence holds neither the old text nor the new."""


def _cells(path: pathlib.Path, check_only: bool) -> list[str]:
    book = openpyxl.load_workbook(path)
    report: list[str] = []
    writes: list[tuple[object, object]] = []

    passive = book[base.SHEET]
    header = [cell.value for cell in passive[1]]
    node, stat, before, after = BERSERKING
    found = [index for index in range(2, passive.max_row + 1)
             if passive.cell(index, header.index("Node") + 1).value == node
             and passive.cell(index, header.index("Stat") + 1).value == stat]
    if len(found) != 1:
        raise RulingError(f"{node} has {len(found)} {stat} rows, not 1. Nothing was written")
    cell = passive.cell(found[0], header.index("Value Per Point") + 1)
    if cell.value == after:
        report.append(f"{node} {stat}: already {after}; left alone")
    elif cell.value == before:
        writes.append((cell, after))
        report.append(f"{node} {stat}: {before} -> {after}")
    else:
        raise RulingError(f"{node} {stat} is {cell.value!r}, neither {before} nor {after}. Nothing was written")

    enchantments = book["Enchantment Effects"]
    header = [cell.value for cell in enchantments[1]]
    rows = [index for index in range(2, enchantments.max_row + 1)
            if enchantments.cell(index, header.index("Enchantment") + 1).value in GADGET_ENCHANTMENTS]
    if len(rows) != GADGET_ROWS:
        raise RulingError(f"{len(rows)} rows belong to the 18 gadget enchantments, not {GADGET_ROWS}. Nothing was written")
    tag_column = header.index("Required Tags") + 1
    changed = 0
    for index in rows:
        cell = enchantments.cell(index, tag_column)
        if cell.value == NEW_TAG:
            continue
        if cell.value != OLD_TAG:
            name = enchantments.cell(index, header.index("Enchantment") + 1).value
            raise RulingError(f"{name} requires {cell.value!r}, neither {OLD_TAG} nor {NEW_TAG}. Nothing was written")
        writes.append((cell, NEW_TAG))
        changed += 1
    report.append(f"gadget enchantment rows: {changed} of {GADGET_ROWS} to {NEW_TAG}" if changed
                  else f"gadget enchantment rows: all {GADGET_ROWS} already {NEW_TAG}; left alone")

    if writes and not check_only:
        for target, value in writes:
            target.value = value
        book.save(path)
    report.append(f"{len(writes)} cell(s) {'to write' if check_only else 'written'}")
    return report


def _sentences(folder: pathlib.Path, check_only: bool) -> list[str]:
    report: list[str] = []
    for each in SENTENCES:
        path = folder / f"{each.tree}_Class_Tree_Final.json"
        text = path.read_bytes().decode("utf-8")
        data = json.loads(text)
        (found,) = [n for n in data["nodes"] if n["id"] == each.node]
        current = (found["data"]["description"] if each.option is None
                   else found["data"]["options"][each.option - 1]["description"])
        where = f"{each.tree} {each.node}" + (f" option {each.option}" if each.option else "")
        if current == each.after:
            report.append(f"{where}: already written; left alone")
            continue
        if current != each.before:
            raise RulingError(f"{where} says {current!r}, which is neither the sentence this script replaces nor what "
                              f"it writes; somebody has written it since. Nothing was written")
        old, new = json.dumps(each.before)[1:-1], json.dumps(each.after)[1:-1]
        if text.count(old) != 1:
            raise RulingError(f"{where}: the old sentence appears {text.count(old)} times in {path.name}")
        if not check_only:
            path.write_bytes(text.replace(old, new).encode("utf-8"))
        report.append(f"{where}: {'to write' if check_only else 'written'}")
    return report


def apply(path: pathlib.Path = WORKBOOK, folder: pathlib.Path = DOCS, check_only: bool = False) -> list[str]:
    """Everything the three rulings write; return the report. The last line says whether anything was or would be.

    EVERY PART IS CHECKED BEFORE ANY IS WRITTEN, so a refusal in the tree files leaves the workbook as it was too.
    """
    if not check_only:
        apply(path, folder, check_only=True)
    report = _cells(path, check_only)
    cells_pending = not report[-1].startswith("0 cell")
    sentences = _sentences(folder, check_only)
    sentences_pending = any(not line.endswith("left alone") for line in sentences)
    rows = base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)
    rows_pending = rows[-1] != "every row is already written; nothing written"
    report += sentences + rows
    if not (cells_pending or sentences_pending or rows_pending):
        report.append("everything is already written; nothing written")
    elif check_only:
        report.append("something would be written; nothing written (--check)")
    else:
        report.append("written")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if anything would be written")
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--docs", type=pathlib.Path, default=DOCS)
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, args.docs, check_only=args.check)
    except (RulingError, base.ReapplyError) as error:
        print(error)
        return 2
    print("\n".join(report))
    return 1 if report[-1].endswith("(--check)") else 0


if __name__ == "__main__":
    sys.exit(main())
