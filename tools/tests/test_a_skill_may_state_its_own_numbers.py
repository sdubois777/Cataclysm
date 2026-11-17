"""A skill may state its own damage, cooldown and mana cost, or take its slot's.

WHY THIS EXISTS. Issue #836. Until 2026-08-22 a skill had none of the three: the
Skill Slots sheet of `docs/All_Things_Cataclysm.xlsx` held one of each per slot,
`game/Data/SkillSlots.csv` carried them to the game, and the game applied them to
whatever skill sat in that slot.

That was right while a skill could only go in the slot it was designed for. The
project owner decided that **a slot is a key** and any skill may go in any slot,
which makes it wrong: the same skill would be worth 250% of weapon damage on the
right mouse button and 400% on R, its power following the key rather than the
skill. `docs/DECISIONS.md` records the decision and what it costs.

WHAT IS CHECKED HERE. The three columns exist in the generated table, and the
rule for reading a cell. The rule is where the traps are:

- **Blank means "the row says nothing"** and is the ordinary case. Every one of
  the 398 rows is blank today, so a blank that produced a zero would make every
  skill in the game deal no damage, cost nothing and have no cooldown.
- **Zero is a real answer**, which is why the sentinel is -1 rather than 0. A
  Support skill deals 0% of weapon damage by design, a skill may have no
  cooldown, and a skill may be free.
- **A negative other than the sentinel is refused**, because a row meaning to
  state one would be read as stating nothing and nothing would report it.

WHAT IS NOT CHECKED. Whether any particular skill's number is right. Nobody has
written one yet; the mechanism landed before the numbers on purpose, so that
nothing behaves differently until somebody does. The automation test
`Cataclysm.SkillNumbers.TheSkillTableCarriesAllThreeFigures` counts how many
rows state each of the three and says so in its output.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS_CSV = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: The three columns, and what each is called in the generated table.
COLUMNS = ("DamagePercent", "Cooldown", "ManaCost")


def rows() -> list[dict[str, str]]:
    if not WEAPON_SKILLS_CSV.is_file():
        pytest.fail("game/Data/WeaponSkills.csv does not exist. Run "
                    "`python tools/generate_datatables.py`.")
    with WEAPON_SKILLS_CSV.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def test_the_generated_table_carries_all_three() -> None:
    parsed = rows()
    assert parsed, "game/Data/WeaponSkills.csv has no rows"

    for column in COLUMNS:
        assert column in parsed[0], (
            f"game/Data/WeaponSkills.csv has no {column} column. A skill takes "
            f"its slot's figure without one, and a slot is a key: the same "
            f"skill would be worth a different amount on a different key. "
            f"Issue #836.")


def test_every_cell_is_a_number_and_none_is_a_stray_negative() -> None:
    for row in rows():
        for column in COLUMNS:
            value = float(row[column])

            # -1 IS THE ONLY NEGATIVE THAT MEANS ANYTHING. It means the row says
            # nothing. Any other negative is a row that meant something and will
            # be read as silence.
            assert value >= 0.0 or value == -1.0, (
                f"{row['Name']} states a {column} of {value}. The only negative "
                f"with a meaning is -1, which means the row says nothing.")


def test_a_blank_cell_means_the_row_says_nothing() -> None:
    # THE ORDINARY CASE, AND THE ONE THAT MUST NOT BECOME ZERO. Every one of the
    # 398 rows is blank today; a blank read as zero would make every skill in the
    # game deal no damage and cost nothing.
    assert gen.skill_number("", "damage percentage", "test") == -1.0
    assert gen.UNSTATED_SKILL_NUMBER == -1.0


def test_zero_is_a_real_answer_and_not_silence() -> None:
    # A SUPPORT SKILL DEALS 0% OF WEAPON DAMAGE BY DESIGN, which is what the
    # slot's own table says, so zero has to survive as zero rather than being
    # read as an empty cell.
    assert gen.skill_number("0", "damage percentage", "test") == 0.0
    assert gen.skill_number("0.0", "cooldown", "test") == 0.0


def test_a_figure_is_carried_through() -> None:
    assert gen.skill_number("250", "damage percentage", "test") == 250.0
    assert gen.skill_number("2.5", "cooldown", "test") == 2.5


def test_a_cell_that_is_not_a_number_is_refused() -> None:
    with pytest.raises(gen.DataError) as raised:
        gen.skill_number("soon", "damage percentage", "Weapon Skills row 4")

    message = str(raised.value)
    assert "Weapon Skills row 4" in message, (
        "the error does not say which row it was, and there are 398 of them")
    assert "blank" in message, (
        "the error does not say what to do instead, which is to leave the cell "
        "blank and take the slot's figure")


def test_a_negative_is_refused_rather_than_read_as_silence() -> None:
    # THE TRAP THIS CLOSES. -1 already means "the row says nothing", so a row
    # written as -5 would be a row that meant something being silently ignored.
    with pytest.raises(gen.DataError) as raised:
        gen.skill_number("-5", "cooldown", "Weapon Skills row 9")

    assert "-1" in str(raised.value), (
        "the error does not explain that -1 is the sentinel, which is the whole "
        "reason a negative cannot be passed through")
