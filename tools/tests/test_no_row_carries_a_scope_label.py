"""No row of any generated table carries a `Scope.*` tag other than `Scope.Global`.

WHY THIS EXISTS. Issue #1620. Seven `Scope.*` tags (BasicOnly, BossOnly, FirstHit,
MeleeOnly, RangedOnly, WhileMoving, WhileStationary) read like restrictions, and
nothing in the game reads them: an enchantment's `Tags` column is descriptive apart
from `Item.Slot.*`, and no effect row requires a scope tag. Each restriction a
sentence states is enforced on its effect rows instead, by a condition
(`target_is_boss`, `while_moving`, `target_not_yet_struck_by_you`), a trigger
(`first_hit_dealt`) or a required tag (`Slot.Basic`). A reader seeing
`Scope.MeleeOnly` on a row could not tell that it restricts nothing, and the owner
ruled on two such rows that the distance, not melee, was the constraint (2026-09-12).

The ruling of 2026-09-14, under the owner's delegation: the scope tags are
descriptive labels, not intent; remove them from the data. The 48 that were in the
enchantment and weapon-skill rows were removed in the same change as this test.

WHY THE TAGS STAY REGISTERED. Ruled by the coordinating session on 2026-10-28: the
eight `Scope.*` tags stay in the Tags sheet and `CataclysmTags.ini`, because deleting
a registered gameplay tag could break a binary asset that names it, which no search of
text can rule out. This test is the guard instead.

WHY `Scope.Global` IS ALLOWED. `UCataclysmStatPipeline` requests it by name and treats
a modifier requiring it as applying to everything, the same as an empty requirement
(`CataclysmStatPipeline.cpp`). It is the one scope tag with a meaning in the game.

WHAT IS CHECKED. Every column whose name contains "tag", in every CSV under
`game/Data`. A failure names the table, column, row and tag.
"""

from __future__ import annotations

import csv
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"

#: The one scope tag the game reads.
ALLOWED = {"Scope.Global"}

#: THE COLUMNS THIS MUST AT LEAST READ, so a renamed column cannot leave the check
#: reading nothing and passing: the three that held scope labels until #1620.
MUST_READ = {
    ("EnchantmentsPositive.csv", "Tags"),
    ("EnchantmentsNegative.csv", "Tags"),
    ("WeaponSkills.csv", "Tags"),
}


def scope_labels(cell: str) -> list[str]:
    """The `Scope.*` tags in a comma-separated cell that the game does not read."""
    tags = [tag.strip() for tag in (cell or "").split(",") if tag.strip()]
    return [tag for tag in tags if tag.startswith("Scope.") and tag not in ALLOWED]


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


def test_the_scan_reads_every_column_that_held_scope_labels():
    """THE SCOPE, checked first, so the test below cannot pass by reading nothing."""
    missing = sorted(MUST_READ - set(tag_columns()))
    assert not missing, (
        f"These columns were not found under {DATA}: {missing}. A column was renamed "
        "or a table moved; update MUST_READ, or the check is reading less than it says.")


def test_no_row_carries_a_scope_label():
    wrong = []
    for (table, column), rows in tag_columns().items():
        for row in rows:
            labels = scope_labels(row[column])
            if labels:
                key = next(iter(row.values()), "")
                wrong.append(f"{table} {column}, row {key}: {', '.join(labels)}")
    assert not wrong, (
        "These rows carry a Scope.* tag that nothing in the game reads (issue #1620). "
        "State the restriction on the effect rows (a condition, a trigger or a "
        "required tag) and remove the scope tag from the design workbook, "
        "docs/All_Things_Cataclysm.xlsx, then regenerate:\n  " + "\n  ".join(wrong))


def test_a_scope_label_is_found_and_scope_global_is_not():
    """The helper, on made-up cells, so no change to the real tables can hide a
    mistake in it."""
    assert scope_labels("Scope.BossOnly, Stat.Offense.Global") == ["Scope.BossOnly"]
    assert scope_labels("Type.Melee,Scope.WhileStationary ") == ["Scope.WhileStationary"]
    assert scope_labels("Scope.Global") == []
    # A TAG THAT ONLY CONTAINS THE WORD IS NOT A SCOPE TAG.
    assert scope_labels("Stat.Offense.Global, Keyword.Scope") == []
    assert scope_labels("") == []
