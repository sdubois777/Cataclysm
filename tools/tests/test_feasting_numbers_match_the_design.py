"""What Feasting does in the engine must agree with what its design row says.

Feasting is the first effect in the game whose size grows with a count of
something. A creature carrying it gains a Feast stack each time it is hit, and
each stack shortens its attack interval. Issue #1720.

WHICH IS AUTHORITATIVE. The design. `game/Data/StatusEffects.csv` carries the
row, and that file is GENERATED from the Buffs sheet of
`docs/All_Things_Cataclysm.xlsx` -- so a change starts in the workbook and
`python tools/generate_datatables.py` writes the CSV. Never edit the CSV by hand.

ITS THREE NUMBERS LIVE IN TWO PLACES, WHICH IS WHY THIS FILE EXISTS.

  4% per stack   the `Strength` column, and re-tuning it is a data change
  up to 5        `UCataclysmStacks::CapFor`, in C++
  5 seconds      `UCataclysmStacks::WindowSecondsFor`, in C++

The cap and the window cannot be columns: the table has nowhere to put a stack
cap or a stack window, and `UCataclysmStacks` is what reads them. So the row
states all three in its sentence and two of them are only checkable against
source text. That is the same fault #904 was about -- a number stated in prose
and nowhere a program could read it -- answered the same way #904 answered it,
by checking that the two places agree.

WHY THIS READS SOURCE TEXT RATHER THAN RUNNING ANYTHING. Continuous integration
never builds the C++ and never opens the editor, so an automation test cannot run
on a pull request. `Cataclysm.Enemy.FeastingSpeedsUpACreaturesAttacksAndNotItsWalking`
checks the arithmetic by running it; this checks that the numbers have not
drifted, which is what a pull request can see. The same division
`test_commander_buff_matches_the_design.py` makes, and for the same reason.

WHAT IS NOT CHECKED HERE. Nothing applies this buff to a creature yet. The row
describes an effect; which creature or modifier grants it is a separate question
and a different file's job, exactly as it is for `Debuff_Withered_Touch`. So this
guards the numbers, not the reachability.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
STACKS_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmStacks.cpp")
STACKS_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmStacks.h")
ENEMY_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmEnemyCharacter.h")
ENEMY_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmEnemyCharacter.cpp")

ROW = "Buff_Feasting"

#: What the row's own sentence says, and what each number must equal. The phrase
#: is quoted from the workbook exactly, so that editing the sentence without
#: editing the number fails here.
PER_STACK = 4.0
PER_STACK_PHRASE = "its attack speed is increased by 4%"
CAP = 5
CAP_PHRASE = "up to 5 stacks"
WINDOW = 5.0
WINDOW_PHRASE = "A stack lasts 5 seconds"


@pytest.fixture(scope="module")
def row() -> dict[str, str]:
    if not STATUS_EFFECTS.is_file():
        pytest.skip(f"{STATUS_EFFECTS} has not been generated")
    with STATUS_EFFECTS.open(newline="", encoding="utf-8-sig") as handle:
        found = {r["Name"]: r for r in csv.DictReader(handle)}
    assert ROW in found, (
        f"{ROW} is not in {STATUS_EFFECTS.name}. It is a row of the Buffs sheet "
        "of docs/All_Things_Cataclysm.xlsx; regenerate with "
        "python tools/generate_datatables.py")
    return found[ROW]


def test_the_row_states_all_three_numbers_in_its_own_words(row) -> None:
    """The sentence is where a designer reads them, so it must state them.

    Two of the three are in C++ and cannot be columns. If the sentence stopped
    saying them, the only record of what the code is supposed to answer would be
    the code itself, and this whole file would be comparing C++ to C++.
    """
    description = row["Description"]
    for phrase in (PER_STACK_PHRASE, CAP_PHRASE, WINDOW_PHRASE):
        assert phrase in description, (
            f"the {ROW} description no longer says {phrase!r}. It reads:\n"
            f"  {description}\n"
            "Edit the sentence in the Buffs sheet of "
            "docs/All_Things_Cataclysm.xlsx and this expectation together, or "
            "the number it states and the number the game uses can drift.")


def test_the_strength_column_matches_the_sentence(row) -> None:
    """The one number that IS a column."""
    assert float(row["Strength"]) == pytest.approx(PER_STACK), (
        f"{ROW} states {PER_STACK_PHRASE!r} and its Strength column holds "
        f"{row['Strength']}. Column D of the Buffs sheet is Strength.")


def stack_case(text: str, function_start: str, kind: str) -> str:
    """The value a switch in CataclysmStacks.cpp returns for one kind.

    READ BY LOCATING THE FUNCTION FIRST. `case ECataclysmStackKind::Feast:`
    appears in three switches in that file, so a search of the whole file would
    find whichever came first and the test would compare the wrong number.
    """
    start = text.index(function_start)
    end = text.index("\n}", start)
    body = text[start:end]
    match = re.search(
        r"case\s+ECataclysmStackKind::" + kind + r":\s*return\s+([0-9.]+)f?;",
        body)
    assert match, (
        f"no `case ECataclysmStackKind::{kind}:` with a plain return inside "
        f"{function_start!r}. If the switch became a table, this test has to be "
        "rewritten to read the table -- do not delete it, because the numbers "
        "still live in two places.")
    return match.group(1)


@pytest.fixture(scope="module")
def stacks_text() -> str:
    return STACKS_SOURCE.read_text(encoding="utf-8")


def test_the_cap_in_cpp_matches_the_sentence(stacks_text) -> None:
    value = stack_case(stacks_text, "int32 UCataclysmStacks::CapFor", "Feast")
    assert int(float(value)) == CAP, (
        f"the row says {CAP_PHRASE!r} and UCataclysmStacks::CapFor answers "
        f"{value} for Feast.")


def test_the_window_in_cpp_matches_the_sentence(stacks_text) -> None:
    value = stack_case(stacks_text,
                       "float UCataclysmStacks::WindowSecondsFor", "Feast")
    assert float(value) == pytest.approx(WINDOW), (
        f"the row says {WINDOW_PHRASE!r} and "
        f"UCataclysmStacks::WindowSecondsFor answers {value} for Feast.")


def test_five_stacks_is_exactly_what_commander_gives(stacks_text) -> None:
    """Why these three numbers and not others, pinned so it stays true.

    Five stacks of 4% is 20%, and 20% is `CommanderIncreasePercent` -- the only
    other thing in the game that speeds a creature up. A fully fed creature is as
    quick as an inspired one and no quicker, which is the ceiling somebody can
    reason about. Change any of the three and this says so.
    """
    text = ENEMY_HEADER.read_text(encoding="utf-8")
    match = re.search(
        r"CommanderIncreasePercent\s*=\s*([0-9.]+)f?;", text)
    assert match, (
        "CommanderIncreasePercent is no longer a constant in "
        f"{ENEMY_HEADER.name}")
    commander = float(match.group(1))
    assert CAP * PER_STACK == pytest.approx(commander), (
        f"{CAP} stacks of {PER_STACK}% is {CAP * PER_STACK}%, and Commander "
        f"gives {commander}%. These were chosen to be equal so that the fastest "
        "a feasting creature can get is a number already in the game. If that "
        "is no longer wanted, change this test deliberately rather than the "
        "numbers quietly.")


def test_the_multiplier_reads_the_row_rather_than_a_literal() -> None:
    """The per-stack figure must come from the data, not be written in C++.

    THIS IS THE FAULT THE COLUMN EXISTS TO PREVENT. A 4 typed into
    FeastingMultiplier would make every other check here pass while re-tuning
    the buff in the sheet did nothing at all.
    """
    text = ENEMY_SOURCE.read_text(encoding="utf-8")
    start = text.index("float ACataclysmEnemyCharacter::FeastingMultiplier")
    body = text[start:text.index("\n}", start)]

    assert "NumbersForEffectTag" in body, (
        "FeastingMultiplier does not read NumbersForEffectTag, so its per-stack "
        "figure is not coming from the Feasting row. Debuff_Cripple is the "
        "pattern: the row's own figure, so re-tuning it is a data change.")

    # And no bare 4 sitting where the magnitude should be.
    assert not re.search(r"=\s*4(\.0f?)?\s*;", body), (
        "FeastingMultiplier assigns a literal 4, which is the row's number "
        "written into C++ as well. One of the two will be edited alone.")
