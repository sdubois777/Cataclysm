"""A status effect that moves a stat says whether its strength is points or a share.

WHY THIS EXISTS. Issue #1256. Column I of the Buffs, Debuffs and DoTs sheets says
WHICH stat an effect's `Strength` moves, and until column J nothing said what that
number was measured IN.

The two cases cannot be told apart by reading the description, which is the whole
reason a column was needed:

  * Shred says it reduces a resistance "by 10" and Abyssal Aura says "by 25%".
    Both subtract that many points, because a resistance is measured in per cent
    already.
  * Weaken says it reduces the enemy's damage "by 20%" and means one fifth of the
    number, whatever the number is.

Applying the second as the first is not a rounding difference. `attack_damage` is
9 on an Imp, so subtracting 20 points clamps to 9 and a "20% reduction" lands as
9%; at Weaken's 80% cap it lands as 9% as well, and the effect never reaches its
cap on any enemy in the game.

WHAT THIS FILE HOLDS.

  * The generator accepts the two operations and an empty cell, and rejects
    anything else by name.
  * It refuses an operation written beside an empty stat column, because that row
    has nothing to apply it to.
  * The written table carries the column, and Weaken is a proportion while Shred
    and Abyssal Aura are not.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"

sys.path.insert(0, str(REPO_ROOT / "sim"))
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables  # noqa: E402


@pytest.fixture(scope="module")
def rows() -> list[dict]:
    with STATUS_EFFECTS.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def by_name(rows: list[dict], name: str) -> dict:
    for row in rows:
        if row["Name"] == name:
            return row
    raise AssertionError(f"{name} is not a row of StatusEffects.csv")


# -- the generator's own answer ------------------------------------------------


def test_an_empty_cell_means_points():
    """Every row written before column J existed keeps the behaviour it had."""
    assert generate_datatables.status_effect_operation(
        None, 1, "Debuffs", "armor") == ""
    assert generate_datatables.status_effect_operation(
        "", 1, "Debuffs", "armor") == ""


def test_both_operations_are_accepted_and_lowercased():
    assert generate_datatables.status_effect_operation(
        "proportion", 1, "Debuffs", "attack_damage") == "proportion"
    assert generate_datatables.status_effect_operation(
        "  Proportion  ", 1, "Debuffs", "attack_damage") == "proportion"
    assert generate_datatables.status_effect_operation(
        "points", 1, "Debuffs", "armor") == "points"


def test_a_word_the_game_cannot_apply_stops_the_generator():
    """The check is the point of the column, so the check is what is proved.

    "percent" is the plausible near miss: it is what a designer would write, it
    is not what the game answers to, and without this it would read as points.
    """
    with pytest.raises(generate_datatables.DataError) as raised:
        generate_datatables.status_effect_operation(
            "percent", 7, "Debuffs", "attack_damage")

    message = str(raised.value)
    assert "row 7" in message
    assert "percent" in message
    assert "proportion" in message, (
        "the error has to name what to write instead, or it tells a designer "
        "they are wrong without telling them what is right")


def test_an_operation_beside_an_empty_stat_column_stops_the_generator():
    """That row has nothing to apply an operation to.

    Silently ignoring it would hide the likeliest authoring mistake: filling
    column J and forgetting column I.
    """
    with pytest.raises(generate_datatables.DataError) as raised:
        generate_datatables.status_effect_operation(
            "proportion", 12, "Debuffs", "")

    assert "row 12" in str(raised.value)


# -- and what reached the table -----------------------------------------------


def test_the_column_exists(rows):
    """A missing column would make every check below vacuously true."""
    assert rows, "StatusEffects.csv holds no rows"
    assert "MovesStatBy" in rows[0], (
        "StatusEffects.csv has no MovesStatBy column. It is column J of the "
        "Buffs, Debuffs and DoTs sheets, written by "
        "tools/generate_datatables.py. Without it nothing says whether an "
        "effect's Strength is points off a stat or a share of it, which is "
        "issue #1256.")


def test_weakens_strength_is_a_share_of_the_stat_it_names(rows):
    weaken = by_name(rows, "Debuff_Weaken")
    assert weaken["MovesStat"].strip() == "attack_damage"
    assert weaken["MovesStatBy"].strip() == "proportion"


def test_the_rows_that_subtract_points_still_do(rows):
    """The control, and it is the half that a wrong change would break silently.

    Shred and Abyssal Aura are the two rows that named a stat before Weaken did.
    Both take points off a resistance. If the new column turned them into shares,
    Shred's 10 would become a 10% cut of a resistance rather than 10 points off
    it, and no other test in this repository would notice.
    """
    for name in ("Debuff_Shred", "Debuff_Abyssal_Aura"):
        row = by_name(rows, name)
        assert row["MovesStat"].strip(), f"{name} names no stat"
        assert row["MovesStatBy"].strip() in ("", "points"), (
            f"{name} subtracts points from a resistance, which is measured in "
            f"per cent already, so a share would be a different effect")


def test_only_rows_that_name_a_stat_carry_an_operation(rows):
    """The rule the generator enforces, checked against what it actually wrote."""
    for row in rows:
        if row["MovesStatBy"].strip():
            assert row["MovesStat"].strip(), (
                f"{row['Name']} says how to apply its strength but not to what")
