"""An effect's numeric columns, checked against what its own Description says.

WHY THIS FILE EXISTS. Issue #904. Ten of the eleven ailments a gear affix can
apply stated a duration of zero seconds and zero damage in
`game/Data/StatusEffects.csv`, and seven of them stated real numbers in prose in
their Description and nowhere a program could read them. Four could not be
written down at all, because `DurationSeconds` and `PercentOfHit` were the only
numeric columns and Cripple's 30% slow, Weaken's 20% damage reduction, Shred's 10
resistance and Void Splinter's 1% of current health per second had nowhere to go.

Four columns were added -- `Strength`, `StrengthCap`, `DurationCap` and
`PercentOfCurrentHealth` -- and the seven prose-stated effects filled in.

WHAT THIS GUARDS, AND WHY IT IS THE PROSE AND NOT A LIST OF NUMBERS. Moving a
number out of a sentence and into a column creates a second place for it to live,
so the two can now disagree. That is the same fault in a new shape: before, the
column said zero and the sentence said 30%; after, the column could say 30 while
somebody edits the sentence to 40%. So every expectation below carries the exact
phrase the workbook uses to state it, and the test asserts BOTH that the phrase
is still in the Description and that the column matches the number in it. Change
one without the other and this fails.

WHY BURN IS NOT IN THE PROSE TABLE. Burn's Description states no numbers at all
-- "A player-applied effect. Deals damage over time. One stack only." -- so there
is no sentence to anchor its 4 seconds and 20% against. It is checked separately,
against the figures `docs/Cataclysm_GDD_v2.md` states in the body text instead.

Related: #363 and #588, both closed, which were the same shape for stun durations
stated only in prose. #899, the eleven ailment affixes, which #904 blocks.
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
    "DoT_Necrosis": [
        ("DurationSeconds", 5.0, "deals damage over time for 5 seconds"),
        ("Strength", 25.0, "enemy's healing by 25%"),
        ("StrengthCap", 100.0, "once that reduction reaches 100%"),
    ],
    "DoT_Void_Splinter": [
        ("DurationSeconds", 4.0, "over 4 seconds"),
        ("PercentOfCurrentHealth", 1.0, "1% of current HP per second"),
    ],
}

#: Burn's figures, which its Description does not state. `docs/Cataclysm_GDD_v2.md`
#: does, in the sentence explaining that a stated number is per tick: "Burn is
#: written as 4 seconds and 20% of the hit, and that means 20% of the hit every
#: second for four seconds, which is 80% of the hit altogether."
BURN = {"DurationSeconds": 4.0, "PercentOfHit": 20.0}

#: The six numeric columns, in the order `tools/generate_datatables.py` reads
#: them out of the sheet. Duplicated here deliberately rather than imported, so
#: that reordering the generator's tuple fails this test instead of silently
#: agreeing with itself.
NUMERIC_COLUMNS = ("DurationSeconds", "PercentOfHit", "Strength",
                   "StrengthCap", "DurationCap", "PercentOfCurrentHealth")


@pytest.fixture(scope="module")
def rows() -> dict[str, dict[str, str]]:
    if not STATUS_EFFECTS.is_file():
        pytest.skip(f"{STATUS_EFFECTS} has not been generated")
    with STATUS_EFFECTS.open(newline="", encoding="utf-8") as handle:
        found = {row["Name"]: row for row in csv.DictReader(handle)}
    assert found, f"{STATUS_EFFECTS} holds no rows"
    return found


def test_the_table_has_all_six_numeric_columns(rows) -> None:
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
            f"Fix the cell on the {'DoTs' if key.startswith('DoT_') else 'Debuffs'} "
            f"sheet of docs/All_Things_Cataclysm.xlsx and regenerate with "
            f"`python tools/generate_datatables.py`. Issue #904.")


def test_burn_still_carries_the_figures_the_design_document_quotes(rows) -> None:
    """Burn's Description states no numbers, so it is anchored to the GDD.

    Checked because everything else in the pool is priced against it: it is the
    one damage over time effect that has always had numbers, and the sentence in
    `docs/Cataclysm_GDD_v2.md` explaining the per-tick rule quotes them.
    """
    row = rows["DoT_Burn"]
    for column, expected in BURN.items():
        actual = float(row[column])
        assert actual == pytest.approx(expected), (
            f"DoT_Burn's {column} is {actual} and docs/Cataclysm_GDD_v2.md "
            f"says {expected}. That sentence reads \"Burn is written as 4 "
            f"seconds and 20% of the hit\", so changing the cell means editing "
            f"the design document in the same change. Issue #904.")


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


def test_no_effect_is_measured_against_both_the_hit_and_the_target(rows) -> None:
    """`PercentOfHit` and `PercentOfCurrentHealth` are alternatives.

    An effect states one or the other. Both filled in would mean two different
    amounts of damage per tick with nothing saying which wins, and the engine
    would silently apply one of them.
    """
    both = [key for key, row in rows.items()
            if float(row["PercentOfHit"]) > 0.0
            and float(row["PercentOfCurrentHealth"]) > 0.0]
    assert not both, (
        f"{both} state a percent of the hit AND a percent of the target's "
        f"current health. An effect is measured against one or the other. "
        f"Issue #904.")


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
    heading = "## The six numeric columns on Buffs, Debuffs and DoTs"
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
