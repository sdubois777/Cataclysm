"""The decisions log's enchantment codes still point at the rows they describe.

WHY THIS EXISTS. Issue #1672. `docs/DECISIONS.md` identifies eleven enchantment
rows by codes like `P092` and `N151`. **Those codes exist nowhere else in the
project.** The generated tables carry no identifier column, the workbook sheet
behind them carries none either, and a search of every tracked file finds the
codes only in the decisions log itself. They are 1-based positions in a
generated file's data rows: `P092` is the 92nd row of
`game/Data/EnchantmentsPositive.csv`.

**A position degrades silently.** One row inserted above shifts every code after
it, and a shifted code still names a real row -- a reader following `P253` gets
an enchantment, reads it, and has no reason to doubt it. There is no error and
no mismatch to notice; the entry simply describes a different row than the
decision was about. Epic #45 is adding rows to these very tables, so the shift
is not hypothetical.

The entry now quotes each row's text beside its code, because the text is
durable and the position is not. This file is what makes that quoting load
bearing: **it fails, naming the code, the moment a position stops holding the
text recorded for it.**

WHAT IS ASSERTED HERE.

    the entry is found exactly once, so the codes are read from the entry that
      records them and not from somewhere else in a 40,000-line file
    the slice really is one entry, checked against a neighbouring entry's words
    the table quotes eleven codes, which is what keeps the per-code checks from
      passing on an empty list when the table is reformatted away
    the table's split into positive and negative codes matches the count the
      entry states in prose beside it, so the two halves of one claim agree
    every quoted code is in range for its table, and holds the row quoted for it
    the entry's own reason for listing the rows rather than describing them is
      true: searching the two tables for "stagger" finds ten of the eleven
    the test file the entry names by path is this one, and exists

WHAT THIS CANNOT CHECK is whether the eleven are the right eleven. That was the
owner's judgement, recorded in the entry as differing from their stated count of
ten, and nothing automatic can settle it -- which is the reason the entry
records the difference rather than resolving it.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"
DATA = REPO_ROOT / "game" / "Data"

#: Identifies the one entry the codes are recorded in. A fragment of the heading
#: rather than the whole of it, so the em dash and the date are not in the way.
ENTRY_ANCHOR = "leaves the target Staggered for a second"

#: A quoted row: `| P092 | Staggered enemies take 20%-35% ... |`
CODE_ROW = re.compile(r"^\|\s*([PN])(\d{3})\s*\|\s*(.+?)\s*\|$")

#: The prose beside the table states the split: "has nine and ... has two".
COUNT_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
               "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11,
               "twelve": 12}

FILE_OF = {"P": "EnchantmentsPositive.csv", "N": "EnchantmentsNegative.csv"}


def entry_text() -> str:
    """The one decisions entry these codes are recorded in, and nothing else."""
    lines = DECISIONS.read_text(encoding="utf-8").splitlines()
    starts = [i for i, line in enumerate(lines)
              if line.startswith("## ") and ENTRY_ANCHOR in line]
    assert len(starts) == 1, (
        f"expected one heading containing {ENTRY_ANCHOR!r}, found {len(starts)} "
        f"at lines {[i + 1 for i in starts]}"
    )
    start = starts[0]
    for end in range(start + 1, len(lines)):
        if lines[end].startswith("## "):
            return "\n".join(lines[start:end])
    return "\n".join(lines[start:])


def quoted_codes() -> list[tuple[str, str]]:
    """Every `| code | row text |` in that entry, in the order written."""
    found = []
    for line in entry_text().splitlines():
        match = CODE_ROW.match(line)
        if match:
            letter, number, text = match.groups()
            found.append((f"{letter}{number}", text))
    return found


def rows_of(letter: str) -> list[dict]:
    path = DATA / FILE_OF[letter]
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


#: Read once at import so each code is its own test and names itself on failure.
#: An empty list here would make those tests vanish rather than fail, which is
#: what `test_the_entry_quotes_eleven_codes` is for.
QUOTED = quoted_codes()


def test_the_entry_is_found_exactly_once():
    assert entry_text().startswith("## "), "the slice does not begin at a heading"


def test_the_slice_really_is_one_entry():
    """A control: the neighbouring entry's subject must not be inside the slice."""
    entry = entry_text()
    whole = DECISIONS.read_text(encoding="utf-8")
    neighbour = "One world subsystem announces every hit, death and skill used"
    assert neighbour in whole, "the neighbouring entry this control relies on has moved"
    assert neighbour not in entry, "the slice ran past the end of its own entry"
    assert len(entry) < len(whole) / 10, (
        f"the slice is {len(entry)} of {len(whole)} characters, which is not one entry"
    )


def test_the_entry_quotes_eleven_codes():
    """Without this the per-code checks would pass on an empty table."""
    assert len(QUOTED) == 11, (
        f"expected eleven quoted codes, found {len(QUOTED)}: "
        f"{[code for code, _ in QUOTED]}"
    )
    assert len({code for code, _ in QUOTED}) == 11, "a code is quoted twice"


def test_the_table_split_matches_the_count_stated_in_prose():
    """The entry states the split in words; the table states it in rows."""
    entry = entry_text()
    stated = re.search(
        r"EnchantmentsPositive\.csv` has (\w+) and\s+`EnchantmentsNegative\.csv` "
        r"has (\w+)",
        entry,
    )
    assert stated, "the entry no longer states the split in prose"
    positive, negative = (COUNT_WORDS[word] for word in stated.groups())
    counted = {"P": 0, "N": 0}
    for code, _ in QUOTED:
        counted[code[0]] += 1
    assert (counted["P"], counted["N"]) == (positive, negative), (
        f"the prose says {positive} positive and {negative} negative; "
        f"the table quotes {counted['P']} and {counted['N']}"
    )


@pytest.mark.parametrize("code,quoted", QUOTED, ids=[code for code, _ in QUOTED])
def test_every_quoted_code_still_holds_its_row(code, quoted):
    letter, number = code[0], int(code[1:])
    rows = rows_of(letter)
    assert 1 <= number <= len(rows), (
        f"{code} is out of range: {FILE_OF[letter]} has {len(rows)} data rows"
    )
    actual = rows[number - 1]["Effect"]
    assert actual == quoted, (
        f"{code} is the {number}th data row of {FILE_OF[letter]}, and that row has "
        f"moved.\n  the log quotes: {quoted}\n  the row now says: {actual}"
    )


def test_searching_for_the_word_finds_ten_not_eleven():
    """The entry's stated reason for listing the rows rather than describing them."""
    matched = [code for code, _ in QUOTED
               if re.search(r"stagger", dict(QUOTED)[code], re.I)]
    assert len(matched) == 10, (
        f"the entry says a word search finds ten of the eleven; it finds "
        f"{len(matched)}"
    )
    missing = [code for code, _ in QUOTED if code not in matched]
    assert missing == ["P255"], (
        f"the entry names P255 as the row that never says the word; it is {missing}"
    )


def test_the_entry_names_this_file_and_it_exists():
    """Nothing else checks paths named in the decisions log."""
    named = re.findall(r"`(tools/tests/[\w/]+\.py)`", entry_text())
    assert named, "the entry no longer names the test that holds its codes"
    for path in named:
        assert (REPO_ROOT / path).is_file(), f"the entry names {path}, which is absent"
    assert f"tools/tests/{pathlib.Path(__file__).name}" in named, (
        f"the entry names {named}, not this file"
    )
