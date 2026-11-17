"""The row counts pinned by hand in the Unreal test, checked against the CSVs.

WHY THIS EXISTS. `game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp` pins
the row count of every generated table with a `CHECK_TABLE(...)` line, so that a
generator change silently dropping rows shows up there rather than as missing
content much later. Those numbers are written by hand.

**Nothing on a pull request ran them.** Continuous integration builds no C++ at
all: `.github/workflows/ci.yml` is a Linux job that runs `ruff`, `pytest` and the
two generator `--check` modes. The Unreal automation tests only run when somebody
runs `python tools/unreal_build.py tests` on a Windows machine with the engine
installed. So a change that adds or removes a row leaves that file stale, merges
cleanly, and the staleness is found later by whoever next runs the engine tests.

HOW IT WENT WRONG, on 2026-08-14, twice in one afternoon. Issue #339 merged the
two minion count enchantments into one, taking `EnchantmentsPositive.csv` from
380 rows to 379. Issue #504 added the Corrupted Stalker, taking
`DungeonModifiers.csv` from 116 to 117. **Both merged with the pinned numbers
left at their old values**, and the full Python suite passed both times.

WHAT WAS THERE BEFORE. `tools/tests/test_class_sheets_match_the_model.py` has a
class called `TestTheCountsThatAreAssertedInUnreal` whose docstring says "No
Python test can catch a stale number there, and it has been missed before, so
both are stated here as well". It restates two of the numbers by hand. That
helps for those two tables and makes the duplication worse: there are then three
copies of the same fact, and it covered neither of the two tables that went
stale.

**Reading the pinned line and comparing it is strictly better than restating it.**
It needs no maintenance, covers every table at once, and cannot itself go stale.

WHAT THIS DOES NOT COVER, and it matters. It reads `CHECK_TABLE` lines only.
The same C++ file had three other hard-coded 116s, in the `ValuesSurviveImport`
test, counting how many dungeon modifiers carry a weight, a description and a
Cataclysm type. Adding the Corrupted Stalker for issue #504 made all three false
and this file said nothing, because they are ordinary `TestEqual` calls.

Those three were not pinned better; they were **rewritten to compare against the
table's own row count**, which is what they meant all along -- the claim is that
EVERY modifier carries these, not that 116 do. That is the right cure for a
hard-coded count that is really a restatement of the table size, and it is
better than teaching this parser to find every integer literal in a C++ test.

WHAT IS ASSERTED HERE.

    the C++ file still has CHECK_TABLE lines this parser can read
    it pins a count for every generated CSV, and names no CSV that does not
      exist
    every pinned count equals the CSV's real row count
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
UNREAL_TEST = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
               / "CataclysmDataTableTests.cpp")

#: `CHECK_TABLE(FCataclysmAffixRow, "Affixes.csv", 80)`, allowing the alignment
#: whitespace the file uses.
PINNED = re.compile(
    r"CHECK_TABLE\(\s*\w+\s*,\s*\"([^\"]+\.csv)\"\s*,\s*(\d+)\s*\)")

def csv_files() -> list[pathlib.Path]:
    if not DATA.is_dir():
        pytest.skip("game/Data/ is not present")
    return sorted(DATA.glob("*.csv"))


@pytest.fixture(scope="module")
def pinned() -> dict[str, int]:
    if not UNREAL_TEST.is_file():
        pytest.skip(f"{UNREAL_TEST.name} is not present")
    found = {name: int(count)
             for name, count in PINNED.findall(
                 UNREAL_TEST.read_text(encoding="utf-8"))}
    assert found, (
        f"no CHECK_TABLE lines could be parsed out of {UNREAL_TEST.name}. "
        f"Either the macro changed shape, in which case fix the pattern in this "
        f"file, or the pins are gone. Every assertion below would otherwise "
        f"pass having compared nothing.")
    return found


def rows_in(name: str) -> int:
    path = DATA / name
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return sum(1 for _ in csv.DictReader(handle))


def test_every_pinned_count_matches_the_csv(pinned) -> None:
    """The whole point. A pinned number that no longer matches means the Unreal
    automation test fails on the next machine that runs it, and nothing on a
    pull request said so."""
    wrong = {name: (count, rows_in(name))
             for name, count in sorted(pinned.items())
             if (DATA / name).is_file() and count != rows_in(name)}
    assert not wrong, (
        f"{UNREAL_TEST.name} pins row counts that the generated CSVs do not "
        "have. File: pinned, actual -- "
        + "; ".join(f"{n}: {p}, {a}" for n, (p, a) in wrong.items())
        + ". Update the CHECK_TABLE line and say in a comment why the count "
          "moved, which is what the existing comments there do.")


def test_every_pinned_file_exists(pinned) -> None:
    """A pin naming a CSV that is gone would be skipped by the check above and
    would fail in Unreal as a load error rather than a count mismatch."""
    missing = sorted(name for name in pinned if not (DATA / name).is_file())
    assert not missing, (
        f"{UNREAL_TEST.name} pins row counts for {missing}, which do not exist "
        f"in game/Data/. Either the table was removed and the pin was not, or "
        f"the name is misspelt.")


def test_every_generated_csv_is_pinned(pinned) -> None:
    """The other direction. A new table with no pin is a table whose row count
    nothing checks, which is the failure the pins exist to prevent."""
    unpinned = sorted(path.name for path in csv_files()
                      if path.name not in pinned)
    assert not unpinned, (
        f"these generated tables have no CHECK_TABLE line in "
        f"{UNREAL_TEST.name}: {unpinned}. Every table's row count is pinned "
        f"there, so a generator change that silently drops rows is caught. Add "
        f"one for each.")
