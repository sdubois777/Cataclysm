"""An effect's numeric columns, checked against what its own Description says.

WHY THIS FILE EXISTS. Issue #904. Ten of the eleven ailments a gear affix can
apply stated a duration of zero seconds and zero damage in
`game/Data/StatusEffects.csv`. Seven of those ten stated real numbers in prose in
their Description and nowhere a program could read them, four could not be
written down at all with the two numeric columns that existed, and three -- Bleed,
Poison and Disease -- had no numbers anywhere because nobody had chosen any.

Five columns were added in the end: `Strength`, `StrengthCap`, `DurationCap`,
`PercentOfCurrentHealth` and `FlatDamagePerTick`. All eleven ailments now state
what they do.

WHAT THIS GUARDS, AND WHY IT IS THE PROSE AND NOT A LIST OF NUMBERS. Moving a
number out of a sentence and into a column creates a second place for it to live,
so the two can now disagree. That is the same fault in a new shape: before, the
column said zero and the sentence said 30%; after, the column could say 30 while
somebody edits the sentence to 40%. So every expectation below carries the exact
phrase the workbook uses to state it, and the test asserts BOTH that the phrase
is still in the Description and that the column matches the number in it. Change
one without the other and this fails.

Related: #363 and #588, both closed, which were the same shape for stun durations
stated only in prose. #899, the eleven ailment affixes, which #904 blocks. #918,
#919 and #920, the three mechanics these descriptions now state and which nothing
implements yet.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
README = REPO_ROOT / "docs" / "README.md"

#: Row key -> list of (column, value, the phrase in the Description stating it).
#:
#: The phrase is quoted from the workbook exactly. It is what makes this a check
#: on two sources agreeing rather than a restatement of one of them.
CLAIMS: dict[str, list[tuple[str, float, str]]] = {
    # --- the six damage over time effects ------------------------------------
    "DoT_Bleed": [
        ("DurationSeconds", 5.0, "for 5 seconds"),
        ("FlatDamagePerTick", 20.0, "a base of 20 damage a second"),
    ],
    "DoT_Poison": [
        ("DurationSeconds", 8.0, "for 8 seconds"),
        ("FlatDamagePerTick", 20.0, "a base of 20 damage a second"),
    ],
    "DoT_Disease": [
        ("DurationSeconds", 6.0, "for 6 seconds"),
        ("FlatDamagePerTick", 12.0, "a base of 12 damage a second"),
    ],
    "DoT_Burn": [
        ("DurationSeconds", 4.0, "for 4 seconds"),
        ("FlatDamagePerTick", 25.0, "a base of 25 damage a second"),
    ],
    "DoT_Necrosis": [
        ("DurationSeconds", 10.0, "for 10 seconds"),
        ("FlatDamagePerTick", 10.0, "a base of 10 damage a second"),
        # Necrosis starts at its cap, so one phrase states both. The magnitude
        # rule then extends the duration from the first point rather than ever
        # raising the strength.
        ("Strength", 100.0, "already total at 100%"),
        ("StrengthCap", 100.0, "already total at 100%"),
    ],
    "DoT_Void_Splinter": [
        ("DurationSeconds", 4.0, "over 4 seconds"),
        ("PercentOfCurrentHealth", 1.0, "1% of current HP per second"),
    ],
    # --- the five debuffs ----------------------------------------------------
    "Debuff_Madness": [
        ("DurationSeconds", 3.0, "friend or foe, for 3 seconds"),
    ],
    "Debuff_Cripple": [
        ("DurationSeconds", 4.0, "attack speed by 30% for 4 seconds"),
        ("Strength", 30.0, "movement and attack speed by 30%"),
        ("StrengthCap", 80.0, "raises the reduction to a cap of 80%"),
    ],
    "Debuff_Shred": [
        ("DurationSeconds", 6.0, "resistance by 10 for 6 seconds"),
        ("Strength", 10.0, "resistance by 10"),
    ],
    "Debuff_Weaken": [
        ("DurationSeconds", 5.0, "damage by 20% for 5 seconds"),
        ("Strength", 20.0, "enemy's damage by 20%"),
        ("StrengthCap", 80.0, "raises the reduction to a cap of 80%"),
    ],
    "Debuff_Stun": [
        ("DurationSeconds", 0.75, "lasts 0.75 seconds at 100% chance to stun"),
        ("DurationCap", 3.0, "400% gives 3, which is the cap"),
    ],
}

#: The seven numeric columns, in the order `tools/generate_datatables.py` reads
#: them out of the sheet. Duplicated here deliberately rather than imported, so
#: that reordering the generator's tuple fails this test instead of silently
#: agreeing with itself.
NUMERIC_COLUMNS = ("DurationSeconds", "PercentOfHit", "Strength", "StrengthCap",
                   "DurationCap", "PercentOfCurrentHealth", "FlatDamagePerTick")

#: The three alternative ways of saying what one tick deals. An effect states
#: exactly one, and which one it picks is a design statement rather than a
#: detail: a flat amount grows only with the attacker's damage over time stats,
#: a percent of the hit grows with those AND with the hit, and a percent of the
#: target's health ignores the attacker altogether.
DAMAGE_BASES = ("FlatDamagePerTick", "PercentOfHit", "PercentOfCurrentHealth")

#: The six effects that deal damage over time and can be applied by the player.
#: Every one had zero damage and, except Burn, zero duration before #904.
DAMAGE_OVER_TIME = ("DoT_Bleed", "DoT_Poison", "DoT_Disease", "DoT_Burn",
                    "DoT_Necrosis", "DoT_Void_Splinter")


@pytest.fixture(scope="module")
def rows() -> dict[str, dict[str, str]]:
    if not STATUS_EFFECTS.is_file():
        pytest.skip(f"{STATUS_EFFECTS} has not been generated")
    with STATUS_EFFECTS.open(newline="", encoding="utf-8") as handle:
        found = {row["Name"]: row for row in csv.DictReader(handle)}
    assert found, f"{STATUS_EFFECTS} holds no rows"
    return found


def test_the_table_has_all_seven_numeric_columns(rows) -> None:
    """A column the generator writes but the table lacks would make every
    assertion below raise a KeyError, which says nothing about the numbers."""
    any_row = next(iter(rows.values()))
    missing = [column for column in NUMERIC_COLUMNS if column not in any_row]
    assert not missing, (
        f"{STATUS_EFFECTS} has no {missing} column. Either "
        f"`tools/generate_datatables.py` stopped writing it, or the order of "
        f"STATUS_EFFECT_NUMBERS there no longer matches NUMERIC_COLUMNS here. "
        f"Issue #904.")


@pytest.mark.parametrize("key", sorted(CLAIMS))
def test_every_number_stated_in_prose_is_also_in_its_column(key, rows) -> None:
    assert key in rows, f"{STATUS_EFFECTS} has no {key} row"
    row = rows[key]
    description = row["Description"]
    sheet = "DoTs" if key.startswith("DoT_") else "Debuffs"

    for column, expected, phrase in CLAIMS[key]:
        # THE PROSE HALF. If the sentence changed, the column may now be wrong
        # and this test can no longer tell, so it fails rather than passing.
        assert phrase in description, (
            f"{key}: the Description no longer contains {phrase!r}, which is "
            f"what states its {column} of {expected}. Either the wording "
            f"changed, in which case update the phrase here and check the "
            f"column still matches, or the number changed, in which case the "
            f"column and this expectation both need updating. Issue #904.\n"
            f"Description is: {description}")

        # THE NUMBER HALF, read out of the phrase rather than trusted from the
        # table above, so the two cannot drift apart in this file either.
        in_phrase = [float(n) for n in re.findall(r"\d+(?:\.\d+)?", phrase)]
        assert expected in in_phrase, (
            f"{key}: {expected} is not one of the numbers in the phrase "
            f"{phrase!r}, so this expectation contradicts itself")

        actual = float(row[column])
        assert actual == pytest.approx(expected), (
            f"{key}: its Description says {phrase!r} but the {column} column "
            f"holds {actual}. The workbook's prose and its columns disagree. "
            f"Fix the cell on the {sheet} sheet of "
            f"docs/All_Things_Cataclysm.xlsx and regenerate with "
            f"`python tools/generate_datatables.py`. Issue #904.")


@pytest.mark.parametrize("key", DAMAGE_OVER_TIME)
def test_every_damage_over_time_effect_states_an_amount(key, rows) -> None:
    """The original fault of #904, in one line.

    An effect worth zero applies nothing, and is indistinguishable from an
    effect nobody wrote. Five of these six were in that state, and the sixth,
    Burn, was the only one anybody had given numbers to.
    """
    row = rows[key]
    stated = {base: float(row[base]) for base in DAMAGE_BASES}
    assert any(value > 0.0 for value in stated.values()), (
        f"{key} states no damage at all: {stated}. It would apply an effect "
        f"worth nothing, which is what issue #904 was opened about. Fill in a "
        f"base on the DoTs sheet of docs/All_Things_Cataclysm.xlsx.")
    assert float(row["DurationSeconds"]) > 0.0, (
        f"{key} states a duration of zero, so nothing would be applied however "
        f"much damage it names. Issue #904.")


@pytest.mark.parametrize("key", DAMAGE_OVER_TIME)
def test_every_damage_over_time_effect_states_exactly_one_base(key, rows) -> None:
    """One of the three, never two.

    Two would mean two different amounts per tick with nothing saying which
    wins. `FCataclysmStatusEffectNumbers::DamagePerTickAgainst` sums the two it
    can apply, so a row stating both would deal both rather than silently
    losing one -- but a row stating both is a mistake either way, and this is
    what says so.
    """
    row = rows[key]
    stated = [base for base in DAMAGE_BASES if float(row[base]) > 0.0]
    assert len(stated) == 1, (
        f"{key} states {len(stated)} of the three damage bases, {stated}, and "
        f"an effect states exactly one. A flat amount grows only with the "
        f"attacker's damage over time stats, a percent of the hit grows with "
        f"those and with the hit, and a percent of the target's current health "
        f"ignores the attacker. They are not combinable. Issue #904.")


def test_no_two_damage_over_time_effects_are_the_same_effect(rows) -> None:
    """The project owner's requirement, checked rather than assumed.

    Stated on 2026-08-24: "in every ARPG, dots are basically the same and
    there's always one that is the best. I wanted to ensure ours are different
    with different effects and some working better in certain situations than
    others." Two effects with the same duration and the same amount are the
    same effect under two names, whatever their descriptions say.

    This checks the numbers only. The conditions that make each one situational
    -- bleed's movement gate, disease's spread on death, Necrosis's healing
    denial -- are #918, #919 and #920, and no test can check them until they
    exist.
    """
    shapes: dict[tuple[float, ...], list[str]] = {}
    for key in DAMAGE_OVER_TIME:
        row = rows[key]
        shape = tuple(round(float(row[column]), 4) for column
                      in ("DurationSeconds", *DAMAGE_BASES))
        shapes.setdefault(shape, []).append(key)

    clashes = {shape: keys for shape, keys in shapes.items() if len(keys) > 1}
    assert not clashes, (
        f"these damage over time effects have identical numbers, so they are "
        f"one effect under several names: {clashes}. The design requires each "
        f"to be better in a different situation. Issue #904.")


def test_shred_states_no_numeric_cap_because_its_cap_is_the_target(rows) -> None:
    """The one place an empty cell is the answer rather than a gap.

    Every other capped effect names a number. Shred's magnitude rises "until
    that resistance reaches zero", which is a property of whatever it is applied
    to, so there is no number to write. A future editor filling this in with a
    guess is the thing being guarded against.
    """
    row = rows["Debuff_Shred"]
    assert float(row["StrengthCap"]) == 0.0, (
        f"Debuff_Shred has a StrengthCap of {row['StrengthCap']}. It should "
        f"have none: its Description says the reduction rises \"until that "
        f"resistance reaches zero\", which is the target's resistance and not "
        f"a number belonging to this effect. Zero here means no numeric cap. "
        f"Issue #904.")
    assert "until that resistance reaches zero" in row["Description"], (
        "Debuff_Shred's Description no longer says its cap is the target's "
        "resistance reaching zero, so the empty StrengthCap above may now be "
        "wrong rather than deliberate.")


def test_no_effect_at_all_is_measured_against_two_bases(rows) -> None:
    """The same rule as above, across every row rather than the six ailments.

    A buff or an enemy-applied debuff filling in two bases would be the same
    mistake, and none of them is in DAMAGE_OVER_TIME.
    """
    offenders = {
        key: [base for base in DAMAGE_BASES if float(row[base]) > 0.0]
        for key, row in rows.items()
        if sum(1 for base in DAMAGE_BASES if float(row[base]) > 0.0) > 1
    }
    assert not offenders, (
        f"{offenders} each state more than one of the three damage bases. An "
        f"effect is measured against a flat amount, or the hit, or the "
        f"target's current health -- one of the three. Issue #904.")


def test_the_readme_documents_every_numeric_column(rows) -> None:
    """`docs/README.md` is the only place saying what these columns are.

    The three sheets have no heading row, which is deliberate and which a test
    in `test_docs_readme_sheet_table_is_true.py` depends on. That makes the
    README the schema, and a column documented nowhere is a column the next
    person has to reverse-engineer out of the generator.
    """
    if not README.is_file():
        pytest.skip("docs/README.md is not present")
    text = README.read_text(encoding="utf-8")
    # RENAMED ON 2026-09-05, when column I was added for issue #1144. It
    # names a stat rather than holding a number, so "the numeric columns"
    # stopped being true of the table as a whole. What this test checks is
    # unchanged: every numeric column still has to be documented there.
    heading = "## The columns on Buffs, Debuffs and DoTs"
    assert heading in text, (
        f"docs/README.md has no {heading!r} section. Those three sheets have no "
        f"heading row, so that section is the only statement of what their "
        f"columns mean. Issue #904.")
    section = text[text.index(heading):]
    end = section.find("\n## ", len(heading))
    section = section[:end if end != -1 else len(section)]

    undocumented = [column for column in NUMERIC_COLUMNS
                    if f"`{column}`" not in section]
    assert not undocumented, (
        f"docs/README.md's column table does not mention {undocumented}. Add a "
        f"row for each, because nothing else says what the column holds. "
        f"Issue #904.")
