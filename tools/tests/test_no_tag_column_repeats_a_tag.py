"""No row of any generated table lists the same tag twice in a tag column.

WHY THIS EXISTS. Issue #1823. Five enchantment rows carried a repeated tag, all of
them typed into the design workbook that way: "Slot.Ultimate, Slot.Ultimate" and
"Slot.Movement, Slot.Movement". It changed nothing in play -- an enchantment's
`Tags` is a plain string, and the one reader in the game,
`UCataclysmDropRoll::EnchantmentSuitsSlot`, gives the same answer for a tag listed
once or twice -- but it meant the column could not be read as a count of anything,
and nothing reported it. The five cells were corrected in the same change as this
test.

WHAT IS CHECKED. Every CSV under `game/Data`, which the generator writes from the
workbook and the game reads, and in each every column whose name contains "tag".
The columns are FOUND BY NAME rather than listed, so a new table with a tag column
is covered without this file being edited. Ruled by the coordinating session on
2026-10-28: a test, and not a refusal in `tools/generate_datatables.py`, because
this catches it before merge and covers new tables on its own.

A failure names each table, column, row and repeated tag.
"""

from __future__ import annotations

import collections
import csv
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"

#: THE COLUMNS THIS MUST AT LEAST READ, so a renamed column or a moved folder
#: cannot leave the check reading nothing and passing. Each held a tag list when
#: this was written (issue #1823); the scan is not limited to them.
MUST_READ = {
    ("EnchantmentsPositive.csv", "Tags"),
    ("EnchantmentsNegative.csv", "Tags"),
    ("EnchantmentEffects.csv", "RequiredTags"),
    ("PassiveEffects.csv", "RequiredTags"),
    ("WeaponSkills.csv", "Tags"),
    ("MinionTypes.csv", "Tags"),
}


def repeated_tags(cell: str) -> list[str]:
    """The tags a comma-separated cell lists more than once, in first-seen order."""
    tags = [tag.strip() for tag in (cell or "").split(",") if tag.strip()]
    return [tag for tag, count in collections.Counter(tags).items() if count > 1]


def tag_columns() -> dict[tuple[str, str], list[dict[str, str]]]:
    """Every (table, column) whose column name contains "tag", with the table's rows."""
    found: dict[tuple[str, str], list[dict[str, str]]] = {}
    for path in sorted(DATA.glob("*.csv")):
        with path.open(encoding="utf-8-sig", newline="") as handle:
            rows = list(csv.DictReader(handle))
        if not rows:
            continue
        for column in rows[0]:
            if column and "tag" in column.lower():
                found[(path.name, column)] = rows
    return found


def test_the_scan_reads_every_tag_column_it_must():
    """THE SCOPE, checked first, so the test below cannot pass by reading nothing."""
    read = set(tag_columns())
    missing = sorted(MUST_READ - read)
    assert not missing, (
        f"These tag columns were not found under {DATA}: {missing}. A column was "
        "renamed or a table moved; update MUST_READ, or the check is reading less "
        "than it says.")


def test_no_row_lists_a_tag_twice():
    wrong = []
    for (table, column), rows in tag_columns().items():
        for row in rows:
            repeats = repeated_tags(row[column])
            if repeats:
                key = next(iter(row.values()), "")
                wrong.append(f"{table} {column}, row {key}: {', '.join(repeats)}")
    assert not wrong, (
        "These rows list a tag more than once. The tag is written twice in the "
        "design workbook, docs/All_Things_Cataclysm.xlsx; remove the second copy "
        "there and regenerate:\n  " + "\n  ".join(wrong))


def test_a_repeat_is_found_and_a_single_tag_is_not():
    """The helper, on made-up cells, so no change to the real tables can hide a
    mistake in it."""
    assert repeated_tags("Slot.Ultimate, Slot.Ultimate") == ["Slot.Ultimate"]
    assert repeated_tags(
        "Stat.Utility.Movespeed, Slot.Movement, Slot.Movement, Keyword.Consume"
    ) == ["Slot.Movement"]
    assert repeated_tags("Slot.Movement, Stat.Utility.Movespeed") == []
    assert repeated_tags("") == []
    # SPACING DOES NOT HIDE A REPEAT, since the cells are written "A, B".
    assert repeated_tags("Slot.Heavy,Slot.Heavy ") == ["Slot.Heavy"]
