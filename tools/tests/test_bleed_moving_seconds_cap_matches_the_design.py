"""A bleed's four seconds of movement, in the engine, must be the four its design row states.

Issue #918, ruled 2026-10-31. `DoT_Bleed` in `game/Data/StatusEffects.csv` says a bleed "ticks only while the
affected target is moving, stopping after 4 seconds of movement". The duration and the damage are columns, which
`test_status_effect_numbers_match_their_prose.py` checks against the same sentence. The four seconds are not: the
table has no column for a cap on moving time, and `UCataclysmDebuffs::BleedMovingSecondsCap` in C++ is what the
gate in `UCataclysmVitalAttributeSet` reads. So the number lives in a sentence and in a header, and this checks the
two agree, the same division `test_feasting_numbers_match_the_design.py` makes for a stack cap.

WHICH IS AUTHORITATIVE. The design. The CSV is generated from the DoTs sheet of `docs/All_Things_Cataclysm.xlsx`, so
a change starts in the workbook and `python tools/generate_datatables.py` writes the CSV.

WHY SOURCE TEXT. Continuous integration never builds the C++. The automation tests
`Cataclysm.Ailments.ABleedOnAMovingTargetStopsAfterFourSecondsOfMovement` and
`Cataclysm.Ailments.ABleedTickingTwiceASecondStopsAfterFourSecondsOfMovementNotAfterFourTicks` check what the gate
does with the number; this checks the number has not drifted from the sentence, which is what a pull request sees.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
DEBUFFS_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                  / "CataclysmDebuffs.h")
VITAL_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                / "CataclysmVitalAttributeSet.cpp")

ROW = "DoT_Bleed"

#: The number, and the row's own words for it, quoted from the workbook exactly.
CAP_SECONDS = 4.0
CAP_PHRASE = "stopping after 4 seconds of movement"
GATE_PHRASE = "ticks only while the affected target is moving"


@pytest.fixture(scope="module")
def description() -> str:
    if not STATUS_EFFECTS.is_file():
        pytest.skip(f"{STATUS_EFFECTS} has not been generated")
    with STATUS_EFFECTS.open(newline="", encoding="utf-8-sig") as handle:
        found = {r["Name"]: r for r in csv.DictReader(handle)}
    assert ROW in found, (
        f"{ROW} is not in {STATUS_EFFECTS.name}. It is a row of the DoTs sheet of "
        "docs/All_Things_Cataclysm.xlsx; regenerate with python tools/generate_datatables.py")
    return found[ROW]["Description"]


def test_the_row_states_the_gate_and_the_four_seconds(description: str) -> None:
    """The sentence is the only design record of the number, so it must keep saying it."""
    for phrase in (GATE_PHRASE, CAP_PHRASE):
        assert phrase in description, (
            f"the {ROW} description no longer says {phrase!r}. It reads:\n  {description}\n"
            "Edit the sentence in the DoTs sheet and this expectation together, or the number it states and the "
            "number the game uses can drift.")


def test_the_cap_in_cpp_matches_the_sentence() -> None:
    text = DEBUFFS_HEADER.read_text(encoding="utf-8")
    match = re.search(r"static\s+constexpr\s+float\s+BleedMovingSecondsCap\s*=\s*([0-9.]+)f?\s*;", text)
    assert match, (
        f"BleedMovingSecondsCap is no longer a constant in {DEBUFFS_HEADER.name}. If it moved or became a row, "
        "rewrite this test to read it there -- do not delete it, because the number still lives in two places.")
    assert float(match.group(1)) == pytest.approx(CAP_SECONDS), (
        f"the {ROW} row says {CAP_PHRASE!r} and UCataclysmDebuffs::BleedMovingSecondsCap is {match.group(1)}.")


def test_the_gate_reads_the_constant_rather_than_a_literal() -> None:
    """A 4 typed into the gate would leave the check above passing while the constant did nothing."""
    text = VITAL_SOURCE.read_text(encoding="utf-8")
    # COMMENTS OUT FIRST, so a comment naming the constant cannot stand in for code reading it.
    code = re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL))
    assert "UCataclysmDebuffs::BleedMovingSecondsCap" in code, (
        f"{VITAL_SOURCE.name} no longer names UCataclysmDebuffs::BleedMovingSecondsCap, so the bleed gate is not "
        "reading the constant this file pins.")
