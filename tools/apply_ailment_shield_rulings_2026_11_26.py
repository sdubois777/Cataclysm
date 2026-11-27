"""Write the ailment and energy shield rulings of 2026-11-26 into the design workbook and the Ritualist tree file.

    python tools/apply_ailment_shield_rulings_2026_11_26.py
    python tools/apply_ailment_shield_rulings_2026_11_26.py --check

THE OWNER'S RULE OF 2026-11-26 that a bleed deals nothing while its target's energy shield stands left three rows
saying something false, and the coordinating session ruled on each the same day, two of them with the owner's choice:

  1. The drawback "Energy shield can now be effected by bleed" keeps its meaning under a truer stat name: its row's
     stat goes from `shield_absorbs_damage_over_time` to `bleed_drains_energy_shield` (a bleed's ticks come off the
     shield one for one, then off health). Its Weight and Tags are not touched.
  2. Warded (`Ritualist_keystone_c_kA`), the owner's choice: "Your Energy Shield absorbs damage that would bypass it."
     Its row's stat goes from `shield_absorbs_damage_over_time` to `shield_absorbs_what_bypasses_it`, and its sentence
     in `docs/Ritualist_Class_Tree_Final.json` to the owner's words.
  3. The positive enchantment "10%-20% of bleed damage you take is taken from your energy shield instead of your
     health", the owner's choice: "You take 10%-20% less damage from bleeding". Its sentence changes, so its row name
     changes too (a row name is the first 48 characters of its sentence): the Enchantments sheet's sentence, and the
     Enchantment Effects row's name, sentence, stat, kind and values, which become the shape the drawback "Bleed effects
     applied to you deal 30%-50% increased damage" already has: `damage_over_time_taken` with the bleed tag. "Less" is
     `more` at a negative figure, as every "you take N% less damage" row is. Its Weight and Tags are not touched. The old
     name is aliased to the new in `FCataclysmEnchantmentRenames::Aliases()` and in
     `tools/tests/enchantment_row_names.txt`, so a saved item carrying it still resolves.

IDEMPOTENT AND REFUSING: a cell or sentence already holding the new text is left alone; one holding the old text is
written; one holding anything else is refused and nothing is saved. EVERY PART IS CHECKED BEFORE ANY IS WRITTEN.
`--check` writes nothing and exits 1 when anything would be written.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DOCS = REPO_ROOT / "docs"

OLD_POSITIVE = "Positive_10_20_of_bleed_damage_you_take_is_taken_from_y"
NEW_POSITIVE = "Positive_You_take_10_20_less_damage_from_bleeding"
OLD_POSITIVE_TEXT = ("10%-20% of bleed damage you take is taken from your energy shield instead of your health")
NEW_POSITIVE_TEXT = "You take 10%-20% less damage from bleeding"

OLD_WARDED_TEXT = "Your Energy Shield absorbs damage over time as well as hits."
NEW_WARDED_TEXT = "Your Energy Shield absorbs damage that would bypass it."


@dataclass(frozen=True)
class Cell:
    """One cell: the sheet, the row found by `key_column` holding one of `keys`, the column, and old and new values."""

    sheet: str
    key_column: str
    keys: tuple[str, ...]
    column: str
    before: object
    after: object


CELLS = (
    Cell("Enchantment Effects", "Enchantment", ("Negative_Energy_shield_can_now_be_effected_by_bleed",), "Stat",
         "shield_absorbs_damage_over_time", "bleed_drains_energy_shield"),
    Cell("Passive Effects", "Node", ("Ritualist_keystone_c_kA",), "Stat",
         "shield_absorbs_damage_over_time", "shield_absorbs_what_bypasses_it"),
    # THE POSITIVE ENCHANTMENT'S EFFECT ROW, found by either name. Its name last, so the others can still find it.
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Effect", OLD_POSITIVE_TEXT,
         NEW_POSITIVE_TEXT),
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Stat",
         "bleed_damage_taken_from_energy_shield", "damage_over_time_taken"),
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Value Kind", "flat", "more"),
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Value Low", 10, -10),
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Value High", 20, -20),
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Required Tags", None,
         "Keyword.DoT.Bleed"),
    Cell("Enchantment Effects", "Enchantment", (OLD_POSITIVE, NEW_POSITIVE), "Enchantment", OLD_POSITIVE,
         NEW_POSITIVE),
    # THE ENCHANTMENTS SHEET'S SENTENCE, in the Positives column, which the row name is built from.
    Cell("Enchantments", "Positives", (OLD_POSITIVE_TEXT, NEW_POSITIVE_TEXT), "Positives", OLD_POSITIVE_TEXT,
         NEW_POSITIVE_TEXT),
)


class RulingError(Exception):
    """A cell or sentence holds neither the old value nor the new."""


def _cells(path: pathlib.Path, check_only: bool) -> list[str]:
    book = openpyxl.load_workbook(path)
    report: list[str] = []
    writes: list[tuple[object, object]] = []
    for each in CELLS:
        sheet = book[each.sheet]
        header = [cell.value for cell in sheet[1]]
        key_index = header.index(each.key_column) + 1
        found = [row for row in range(2, sheet.max_row + 1)
                 if sheet.cell(row, key_index).value in each.keys]
        if len(found) != 1:
            raise RulingError(f"{each.sheet}: {len(found)} rows hold {each.keys[0]}, not 1. Nothing was written")
        cell = sheet.cell(found[0], header.index(each.column) + 1)
        where = f"{each.sheet} {each.keys[0]} {each.column}"
        if cell.value == each.after:
            report.append(f"{where}: already {each.after!r}; left alone")
        elif cell.value == each.before:
            writes.append((cell, each.after))
            report.append(f"{where}: {each.before!r} -> {each.after!r}")
        else:
            raise RulingError(f"{where} is {cell.value!r}, neither {each.before!r} nor {each.after!r}. "
                              "Nothing was written")
    if writes and not check_only:
        for cell, value in writes:
            cell.value = value
        book.save(path)
    report.append(f"{len(writes)} cell(s) {'to write' if check_only else 'written'}")
    return report


def _warded(folder: pathlib.Path, check_only: bool) -> list[str]:
    path = folder / "Ritualist_Class_Tree_Final.json"
    text = path.read_bytes().decode("utf-8")
    (node,) = [n for n in json.loads(text)["nodes"] if n["id"] == "keystone_c_kA"]
    current = node["data"]["description"]
    if current == NEW_WARDED_TEXT:
        return ["Warded's sentence: already written; left alone"]
    if current != OLD_WARDED_TEXT:
        raise RulingError(f"Warded says {current!r}, neither the old sentence nor the new. Nothing was written")
    old, new = json.dumps(OLD_WARDED_TEXT)[1:-1], json.dumps(NEW_WARDED_TEXT)[1:-1]
    if text.count(old) != 1:
        raise RulingError(f"Warded's sentence appears {text.count(old)} times in {path.name}. Nothing was written")
    if not check_only:
        path.write_bytes(text.replace(old, new).encode("utf-8"))
    return [f"Warded's sentence: {'to write' if check_only else 'written'}"]


def apply(path: pathlib.Path = WORKBOOK, folder: pathlib.Path = DOCS, check_only: bool = False) -> list[str]:
    """Everything the rulings write; return the report. The last line says whether anything was or would be."""
    if not check_only:
        apply(path, folder, check_only=True)
    report = _cells(path, check_only)
    cells_pending = not report[-1].startswith("0 cell")
    warded = _warded(folder, check_only)
    warded_pending = not warded[-1].endswith("left alone")
    report += warded
    if not (cells_pending or warded_pending):
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
    except RulingError as error:
        print(error)
        return 2
    print("\n".join(report))
    return 1 if report[-1].endswith("(--check)") else 0


if __name__ == "__main__":
    sys.exit(main())
