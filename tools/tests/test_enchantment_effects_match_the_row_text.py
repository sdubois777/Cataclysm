"""Every authored enchantment effect matches the words of its enchantment.

WHY THIS EXISTS. Issue #45. An enchantment says what it does in a sentence
written for a player -- "Double your energy shield" -- and carries no stat name
and no number a machine can read. The Enchantment Effects sheet of
`docs/All_Things_Cataclysm.xlsx` is where those are written, and
`tools/generate_datatables.py` turns it into `game/Data/EnchantmentEffects.csv`.

**That is two statements of one fact in two files.** The generator already
refuses a row whose `Effect` cell does not repeat its enchantment's words
exactly, so an enchantment that is reworded cannot keep its old numbers without
anyone noticing. What that cannot catch is a number typed wrong in the first
place, and that is what this file checks.

WHAT IS ASSERTED HERE.

    every effect names an enchantment that exists
    no effect is written on a set row yet
    a row stating a range states one its enchantment's words state, in their
      order and with one sign, because the game shows the item's number in
      place of that range
    a row stating one value finds it in its enchantment's words outside any
      range, as a number or as the multiplying word the sentence uses
      ("Double", "tripled")
    every condition value appears in those words too
    an effect in the `more` bucket is on a sentence worded as a multiplier, and
      one in the `increased` bucket on a sentence worded as an increase
    a negative value is on a sentence that takes something away
    the two enchantment tables state as many ranges as were measured, which
      holds the generator's reader and the game's reader to one answer
    the coverage is what it is measured to be, so it only moves deliberately

WHAT THIS CANNOT CHECK is whether the stat chosen is the right stat. Nothing
automatic can read "Double your life leech" and know that `life_leech` rather
than `mana_leech` is meant, which is why the sheet is written by hand and
reviewed. `test_passive_effects_match_the_node_text.py` is the same check for
the passive trees.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
EFFECTS_CSV = DATA / "EnchantmentEffects.csv"
ENCHANTMENT_CSVS = (DATA / "EnchantmentsPositive.csv",
                    DATA / "EnchantmentsNegative.csv")

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: Words that state a `more` multiplier without a number, and the value each
#: one means. "Double your energy shield" is +100% more; "tripled" is +200%.
MULTIPLYING_WORDS = {"double": 100.0, "doubled": 100.0, "twice": 100.0,
                     "triple": 200.0, "tripled": 200.0, "quadrupled": 300.0,
                     "halved": -50.0}

#: A sentence worded as a multiplier, which is what the `more` bucket is for.
MULTIPLIER = re.compile(
    r"\b(more|less|double|doubled|twice|triple|tripled|quadrupled|halved)\b",
    re.IGNORECASE)

#: A sentence worded as an increase, which is the `increased` bucket.
INCREASE = re.compile(
    r"\b(increase|increased|reduced|faster|slower|longer|larger)\b",
    re.IGNORECASE)

#: A sentence that takes something away, which is where a negative value goes.
TAKING = re.compile(r"\b(less|reduced|lose|slower|shorter|halved)\b",
                    re.IGNORECASE)

#: How many rows are written, and over how many enchantments. Pinned so that
#: the coverage only moves when somebody means it to, and says so in
#: `docs/DECISIONS.md` at the same time.
AUTHORED_ROWS = 7
AUTHORED_ENCHANTMENTS = 7

#: How many ranges the two enchantment tables state, measured on 2026-09-11
#: with a separate search of the two CSV files. The game's own reader,
#: `UCataclysmItemValues::EnchantmentRanges`, is held to the same number by
#: `Cataclysm.Enchantments.EveryRangeTheTablesStateIsFoundAndReplaced`, so the
#: generator and the game cannot read the sentences differently.
STATED_RANGES = 390


def read(path: pathlib.Path) -> list[dict]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def effects() -> list[dict]:
    if not EFFECTS_CSV.is_file():
        pytest.skip(f"{EFFECTS_CSV.name} has not been generated")
    rows = read(EFFECTS_CSV)
    assert rows, (f"{EFFECTS_CSV.name} is empty, so every check below would "
                  f"pass having read nothing")
    return rows


@pytest.fixture(scope="module")
def enchantments() -> dict[str, dict]:
    rows: dict[str, dict] = {}
    for path in ENCHANTMENT_CSVS:
        for row in read(path):
            rows[row["Name"]] = row
    assert len(rows) > 500, "the two enchantment tables did not load"
    return rows


def numbers_in(text: str) -> set[float]:
    """Every number in a sentence, with thousands commas read as such."""
    return {float(n.replace(",", ""))
            for n in re.findall(r"(?:\d[\d,]*\d|\d)(?:\.\d+)?", text)}


def outside_ranges(text: str) -> str:
    """The sentence with every range it states taken out."""
    return gen.ENCHANTMENT_RANGE.sub(" ", text)


def words_of(row: dict, enchantments: dict[str, dict]) -> str:
    return enchantments[row["Enchantment"]]["Effect"]


def test_every_effect_names_an_enchantment_that_exists(effects, enchantments):
    missing = sorted(r["Name"] for r in effects
                     if r["Enchantment"] not in enchantments)
    assert not missing, (
        f"{missing} name enchantments neither table holds, so they would "
        f"apply to nothing. The generator refuses such a row, so the CSV is "
        f"older than the workbook.")


def test_no_effect_is_written_on_a_set_row_yet(effects, enchantments):
    """A set row applies by how many worn pieces carry the set, which nothing
    counts yet. Written here, it would be applied per piece."""
    on_sets = sorted(r["Name"] for r in effects
                     if enchantments[r["Enchantment"]]["EnchantmentType"]
                     .casefold() == "set")
    assert not on_sets, f"{on_sets} are effects on set rows"


def test_a_range_is_one_the_words_state(effects, enchantments):
    """The game shows the item's number in place of the range in the sentence,
    so a row whose two ends are not that range, in that order, would give the
    character one number and the player another."""
    wrong = []
    for row in effects:
        low, high = float(row["ValueLow"]), float(row["ValueHigh"])
        if low == high:
            continue
        text = words_of(row, enchantments)
        if ((low < 0) != (high < 0)
                or (abs(low), abs(high)) not in gen.enchantment_ranges(text)):
            wrong.append(f"{row['Name']}: {low:g} to {high:g} against {text!r}")
    assert not wrong, "; ".join(wrong)


def test_a_single_value_appears_in_its_words_outside_any_range(effects,
                                                              enchantments):
    """A single value that is only an end of a range the sentence states is a
    range written as one number, and would ignore the roll."""
    wrong = []
    for row in effects:
        value = float(row["ValueLow"])
        if value != float(row["ValueHigh"]):
            continue
        text = words_of(row, enchantments)
        words = {word.lower() for word in re.findall(r"[A-Za-z]+", text)}
        said = abs(value) in numbers_in(outside_ranges(text)) or any(
            MULTIPLYING_WORDS.get(word) == value for word in words)
        if not said:
            wrong.append(f"{row['Name']}: {value:g} against {text!r}")
    assert not wrong, (
        "these values appear nowhere in their enchantment's words outside a "
        "range, as a number or as a multiplying word: " + "; ".join(wrong))


def test_every_condition_value_appears_in_the_words_too(effects, enchantments):
    wrong = [f"{r['Name']}: {r['ConditionValue']} against "
             f"{words_of(r, enchantments)!r}"
             for r in effects
             if float(r["ConditionValue"])
             and float(r["ConditionValue"])
             not in numbers_in(words_of(r, enchantments))]
    assert not wrong, "; ".join(wrong)


def test_a_more_row_is_worded_as_a_multiplier(effects, enchantments):
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if r["ValueKind"] == "more"
             and not MULTIPLIER.search(words_of(r, enchantments))]
    assert not wrong, (
        "these rows are in the more bucket and their sentence does not say "
        "more, less, double or the like: " + "; ".join(wrong))


def test_an_increased_row_is_worded_as_an_increase(effects, enchantments):
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if r["ValueKind"] == "increased"
             and not INCREASE.search(words_of(r, enchantments))]
    assert not wrong, (
        "these rows are in the increased bucket and their sentence does not "
        "say increased, reduced or the like: " + "; ".join(wrong))


def test_a_negative_value_is_on_words_that_take_something_away(effects,
                                                              enchantments):
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if float(r["ValueLow"]) < 0
             and not TAKING.search(words_of(r, enchantments))]
    assert not wrong, "; ".join(wrong)


def test_the_tables_state_the_measured_number_of_ranges(enchantments):
    counted = sum(len(gen.enchantment_ranges(row["Effect"]))
                  for row in enchantments.values())
    assert counted == STATED_RANGES, (
        f"the generator reads {counted} ranges in the two enchantment tables, "
        f"and {STATED_RANGES} were measured. Either the sentences changed, or "
        f"the reader in tools/generate_datatables.py no longer reads what "
        f"UCataclysmItemValues::EnchantmentRanges reads. Change this number "
        f"and the automation test that pins the same count together.")


def test_the_coverage_is_what_it_is_measured_to_be(effects):
    assert len(effects) == AUTHORED_ROWS, (
        f"{len(effects)} effect rows, pinned at {AUTHORED_ROWS}. Change the "
        f"pin and the entry in docs/DECISIONS.md that states it together.")
    assert len({r["Enchantment"] for r in effects}) == AUTHORED_ENCHANTMENTS
