"""A status effect that has a strength says which stat that strength moves.

WHY THIS EXISTS. Issue #1144. `game/Data/StatusEffects.csv` gave every effect a
`Strength` column and no column saying what the strength was **of**. Shred's row
states 10 and its description says that is 10 resistance; Cripple's states a slow
and Weaken's a damage reduction. Nothing in the data connected a number to the
stat it moved, so that pairing lived in C++ and exactly one effect was in it.

The issue said what should trigger fixing it: "The second effect that needs one
is the point at which this should become a column rather than a second name
written into C++." Abyssal Aura -- a Demonic enemy modifier that cuts a player's
Demonic and War resistances by 25% -- was that second effect, so column I of the
Buffs, Debuffs and DoTs sheets now names the stat, and
`tools/generate_datatables.py` writes it out as `MovesStat`.

WHAT THIS FILE HOLDS.

  * Every name in that column is a stat the game actually has.
  * A misspelled name stops the generator rather than reaching the game as an
    effect that quietly moves nothing. That is the objection recorded against
    naming a thing in data at all -- "a misspelled basis would silently read as
    one of the others and a number cannot be misspelled" -- and it is answered
    by checking, so the check is what this proves.
  * The two effects that triggered the work carry what they should.
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

from cataclysm_sim.character import ALL_STATS  # noqa: E402
import generate_datatables  # noqa: E402


@pytest.fixture(scope="module")
def rows() -> list[dict]:
    with STATUS_EFFECTS.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def test_the_column_exists(rows):
    """A missing column would make every check below vacuously true."""
    assert rows, "StatusEffects.csv holds no rows"
    assert "MovesStat" in rows[0], (
        "StatusEffects.csv has no MovesStat column. It is column I of the "
        "Buffs, Debuffs and DoTs sheets, written by "
        "tools/generate_datatables.py. Without it nothing says what an "
        "effect's Strength is a strength of, which is issue #1144.")


def test_every_name_in_it_is_a_stat_the_game_has(rows):
    """A name the game does not know is an effect that moves nothing."""
    allowed = set(ALL_STATS) | {generate_datatables.STAT_OF_SOURCE_ELEMENT}

    for row in rows:
        for name in [part.strip() for part in row["MovesStat"].split(",")
                     if part.strip()]:
            assert name in allowed, (
                "{} names {!r} in its MovesStat column, and that is not one of "
                "the 46 character sheet stats nor {!r}. An effect naming a stat "
                "the game does not have moves nothing and says nothing about "
                "it.".format(row["Name"], name,
                             generate_datatables.STAT_OF_SOURCE_ELEMENT))


def test_a_misspelled_stat_stops_the_generator():
    """The check is the whole reason a name in data is safe here.

    THE OBJECTION THIS ANSWERS is recorded in `tools/generate_datatables.py`
    itself, against naming a basis in a column rather than giving it a column of
    its own: "a misspelled basis would silently read as one of the others and a
    number cannot be misspelled". That is true of an UNCHECKED name. This proves
    the name is checked, so the objection does not apply.
    """
    known = frozenset(ALL_STATS)

    # A real one goes through untouched, so the failure below is about the name
    # and not about the function refusing everything.
    assert generate_datatables.status_effect_stats(
        "resistance_demonic", 6, "Debuffs", known) == "resistance_demonic"

    # And the token that is deliberately not a stat.
    assert generate_datatables.status_effect_stats(
        generate_datatables.STAT_OF_SOURCE_ELEMENT, 23, "Debuffs",
        known) == generate_datatables.STAT_OF_SOURCE_ELEMENT

    # An empty cell is the ordinary answer and is not an error.
    assert generate_datatables.status_effect_stats(None, 1, "Debuffs",
                                                   known) == ""

    with pytest.raises(generate_datatables.DataError) as raised:
        generate_datatables.status_effect_stats(
            "resistance_demonicc", 6, "Debuffs", known)

    message = str(raised.value)
    assert "resistance_demonicc" in message, (
        "the error does not name the misspelling, so a designer would have to "
        "find it themselves")
    assert "Debuffs row 6" in message, (
        "the error does not name the sheet and row, so a designer would not "
        "know which cell to correct")


def test_a_misspelling_among_correct_names_is_still_caught():
    """One bad name in a list must not be hidden by the good ones beside it."""
    known = frozenset(ALL_STATS)

    with pytest.raises(generate_datatables.DataError):
        generate_datatables.status_effect_stats(
            "resistance_demonic, resistance_waaar", 6, "Debuffs", known)


def test_the_two_effects_that_triggered_this_carry_what_they_should(rows):
    """Shred and Abyssal Aura, which are why the column exists."""
    by_name = {row["Name"]: row for row in rows}

    shred = by_name.get("Debuff_Shred")
    assert shred is not None, "Debuff_Shred is not in StatusEffects.csv"
    assert shred["MovesStat"] == generate_datatables.STAT_OF_SOURCE_ELEMENT, (
        "Shred cuts the resistance matching whoever applied it -- a Shred from "
        "a Demonic skill cuts Demonic resistance -- so it cannot name a fixed "
        "stat and must carry the token that says so.")

    aura = by_name.get("Debuff_Abyssal_Aura")
    assert aura is not None, "Debuff_Abyssal_Aura is not in StatusEffects.csv"
    assert aura["MovesStat"] == "resistance_demonic, resistance_war", (
        "Abyssal Aura's own description says it reduces the player's Demonic "
        "and War resistances, so it names both.")

    # AND IT HAS NUMBERS AT ALL, which it did not until 2026-09-05. Every
    # numeric cell on that row was empty, so the effect applied nothing however
    # it was reached.
    assert float(aura["Strength"]) == 25.0, (
        "Abyssal Aura's description says 25%, so its Strength column says 25")
    assert float(aura["DurationSeconds"]) > 0.0, (
        "an effect with no duration is never applied at all")
