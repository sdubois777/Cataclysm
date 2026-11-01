"""The seconds over which a Necrosis deals the healing it denied, in the engine, must be the five its design row states.

Issue #920, ruled by the coordinating session 2026-10-31. `DoT_Necrosis` in `game/Data/StatusEffects.csv` says a
Necrosis "denies the affected target all healing for that time, dealing the denied amount as damage over the
following 5 seconds". The ten seconds and the ten damage are columns, which
`test_status_effect_numbers_match_their_prose.py` checks against the same sentence. The five seconds are not: the
table has no column for them, and `UCataclysmDebuffs::NecrosisDeniedHealingSeconds` in C++ is what the listener in
`CataclysmSkillEffects.cpp` reads when a Necrosis ends. So the number lives in a sentence and in a header, and this
checks the two agree, as `test_bleed_moving_seconds_cap_matches_the_design.py` does for a bleed's four seconds.

WHICH IS AUTHORITATIVE. The design. The CSV is generated from the DoTs sheet of `docs/All_Things_Cataclysm.xlsx`, so
a change starts in the workbook and `python tools/generate_datatables.py` writes the CSV.

WHY SOURCE TEXT. Continuous integration never builds the C++. The automation test
`Cataclysm.Ailments.NecrosisDealsTheDeniedHealingOverTheFollowingFiveSeconds` checks what the listener does with the
number; this checks the number has not drifted from the sentence, which is what a pull request sees.
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
SKILL_EFFECTS_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                        / "CataclysmSkillEffects.cpp")

ROW = "DoT_Necrosis"

#: The number, and the row's own words for it, quoted from the workbook exactly.
DENIED_SECONDS = 5.0
DENIAL_PHRASE = "denies the affected target all healing for that time"
SECONDS_PHRASE = "dealing the denied amount as damage over the following 5 seconds"


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


def test_the_row_states_the_denial_and_the_five_seconds(description: str) -> None:
    """The sentence is the only design record of the number, so it must keep saying it."""
    for phrase in (DENIAL_PHRASE, SECONDS_PHRASE):
        assert phrase in description, (
            f"the {ROW} description no longer says {phrase!r}. It reads:\n  {description}\n"
            "Edit the sentence in the DoTs sheet and this expectation together, or the number it states and the "
            "number the game uses can drift.")


def test_the_seconds_in_cpp_match_the_sentence() -> None:
    text = DEBUFFS_HEADER.read_text(encoding="utf-8")
    match = re.search(
        r"static\s+constexpr\s+float\s+NecrosisDeniedHealingSeconds\s*=\s*([0-9.]+)f?\s*;", text)
    assert match, (
        f"NecrosisDeniedHealingSeconds is no longer a constant in {DEBUFFS_HEADER.name}. If it moved or became a "
        "row, rewrite this test to read it there -- do not delete it, because the number still lives in two places.")
    assert float(match.group(1)) == pytest.approx(DENIED_SECONDS), (
        f"the {ROW} row says {SECONDS_PHRASE!r} and UCataclysmDebuffs::NecrosisDeniedHealingSeconds is "
        f"{match.group(1)}.")


def test_the_listener_reads_the_constant_rather_than_a_literal() -> None:
    """A 5 typed into the listener would leave the check above passing while the constant did nothing."""
    text = SKILL_EFFECTS_SOURCE.read_text(encoding="utf-8")
    # COMMENTS OUT FIRST, so a comment naming the constant cannot stand in for code reading it.
    code = re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL))
    assert "UCataclysmDebuffs::NecrosisDeniedHealingSeconds" in code, (
        f"{SKILL_EFFECTS_SOURCE.name} no longer names UCataclysmDebuffs::NecrosisDeniedHealingSeconds, so the "
        "healing a Necrosis denied is not dealt over the number this file pins.")
