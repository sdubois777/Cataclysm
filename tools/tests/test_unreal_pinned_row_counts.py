"""Every generated table is imported by the Unreal data table test, and that test names no table that is gone.

`game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp` imports every generated CSV through its row struct with a
`CHECK_TABLE(RowType, "File.csv")` line, which catches an import problem the engine reports. This file checks the list
of those lines against `game/Data/`, in both directions.

THE ROW COUNTS ARE GONE, BY ISSUE #2928 (2026-11-27). Each `CHECK_TABLE` line pinned its table's row count by hand, and
this file compared every pin with the CSV, because a stale pin failed only in the Unreal suite (twice on 2026-08-14,
issues #339 and #504). The pins only restated the CSVs and moved with every row added, so they were removed along with
the comparison, `test_every_pinned_count_matches_the_csv`. The name of this file is kept because other files and the
decisions log cite it.

WHAT IS ASSERTED HERE.

    the C++ file still has CHECK_TABLE lines this parser can read
    it imports every generated CSV, and names no CSV that does not exist
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
UNREAL_TEST = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
               / "CataclysmDataTableTests.cpp")

#: `CHECK_TABLE(FCataclysmAffixRow, "Affixes.csv")`, allowing the alignment
#: whitespace the file uses.
IMPORTED = re.compile(
    r"CHECK_TABLE\(\s*\w+\s*,\s*\"([^\"]+\.csv)\"\s*\)")

def csv_files() -> list[pathlib.Path]:
    if not DATA.is_dir():
        pytest.skip("game/Data/ is not present")
    return sorted(DATA.glob("*.csv"))


@pytest.fixture(scope="module")
def imported() -> set[str]:
    if not UNREAL_TEST.is_file():
        pytest.skip(f"{UNREAL_TEST.name} is not present")
    found = set(IMPORTED.findall(UNREAL_TEST.read_text(encoding="utf-8")))
    assert found, (
        f"no CHECK_TABLE lines could be parsed out of {UNREAL_TEST.name}. "
        f"Either the macro changed shape, in which case fix the pattern in this "
        f"file, or the lines are gone. Every assertion below would otherwise "
        f"pass having compared nothing.")
    return found


def test_every_imported_file_exists(imported) -> None:
    """A CHECK_TABLE line naming a CSV that is gone fails in Unreal as a load error."""
    missing = sorted(name for name in imported if not (DATA / name).is_file())
    assert not missing, (
        f"{UNREAL_TEST.name} imports {missing}, which do not exist in game/Data/. "
        f"Either the table was removed and its line was not, or the name is misspelt.")


def test_every_generated_csv_is_imported(imported) -> None:
    """The other direction. A new table with no CHECK_TABLE line is a table whose import nothing checks."""
    missing = sorted(path.name for path in csv_files() if path.name not in imported)
    assert not missing, (
        f"these generated tables have no CHECK_TABLE line in {UNREAL_TEST.name}: {missing}. Add one for each.")
