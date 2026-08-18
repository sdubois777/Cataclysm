"""The Unreal test that compares each DataTable asset with its CSV must name
every table the asset generator builds.

WHY THIS EXISTS. Issue #702. `game/Source/Cataclysm/Tests/
CataclysmDataTableTests.cpp` holds two lists of the generated tables:

    CHECK_TABLE(...) lines   one per table, pinning its row count
    the Tables[] array in FCataclysmDataTableAssetsTest, pairing each asset
                             with the CSV it was built from

Only the first was guarded. `tools/tests/test_unreal_pinned_row_counts.py` fails
when a generated CSV has no `CHECK_TABLE` line, so that list stayed complete. The
`Tables[]` array had no such guard and quietly fell two behind: `DT_MinionTypes`
and `DT_MinionScaling` landed with issues #337 and #338 and were never added, so
for three days neither asset was compared against its CSV and every test in the
repository passed.

WHAT THAT COMPARISON IS FOR, and why leaving a table out of it is not harmless.
A `.uasset` is what a packaged build loads; the CSV is only what a pull request
shows. An asset that was never rebuilt after its CSV changed holds the old
numbers, and the only thing that reads the rows back out of the asset and
compares them is that automation test. A table missing from its list has no
check on its contents at all.

WHY IT IS CHECKED FROM PYTHON. The same reason
`test_csv_columns_match_their_row_structs.py` gives: continuous integration
builds no C++, so nothing on a pull request runs the automation tests. Comparing
two lists is a text comparison and needs no engine.

WHAT THIS DOES NOT DO. It does not check that the comparison itself is right,
only that no table is left out of it. Whether the asset really matches the CSV
is what the automation test decides, and that needs the editor.
"""

from __future__ import annotations

import ast
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSET_GENERATOR = REPO_ROOT / "tools" / "generate_datatable_assets.py"
UNREAL_TEST = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
               / "CataclysmDataTableTests.cpp")

#: `{ TEXT("DT_Affixes"),  TEXT("Affixes.csv") },` with the alignment
#: whitespace the file uses.
PAIR = re.compile(
    r"\{\s*TEXT\(\"(DT_\w+)\"\)\s*,\s*TEXT\(\"([^\"]+\.csv)\"\)\s*\}")


@pytest.fixture(scope="module")
def generated() -> dict[str, str]:
    """Asset name against CSV file, out of the generator's own TABLES list.

    Read rather than imported: `generate_datatable_assets.py` imports `unreal`
    at module scope and only runs inside the editor's Python. Two other tests
    here read it the same way and for the same reason.
    """
    if not ASSET_GENERATOR.is_file():
        pytest.skip(f"{ASSET_GENERATOR.name} is not present")

    tree = ast.parse(ASSET_GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "TABLES"
                for target in node.targets):
            return {asset: csv_file for asset, csv_file, _struct
                    in (ast.literal_eval(element)
                        for element in node.value.elts)}
    pytest.fail(f"{ASSET_GENERATOR.name} no longer has a TABLES list")


@pytest.fixture(scope="module")
def compared() -> dict[str, str]:
    """Asset name against CSV file, out of the Unreal test's Tables[] array."""
    if not UNREAL_TEST.is_file():
        pytest.skip(f"{UNREAL_TEST.name} is not present")
    return dict(PAIR.findall(UNREAL_TEST.read_text(encoding="utf-8")))


def test_the_parser_actually_found_the_pairs(compared, generated) -> None:
    """Without this, a regular expression that matches nothing makes every
    assertion below vacuous and the file reads as coverage while providing none.

    The floor is what the generator builds, which is correct by construction and
    rises on its own when a table is added.
    """
    assert len(compared) >= len(generated), (
        f"only {len(compared)} asset/CSV pair(s) were parsed out of "
        f"{UNREAL_TEST.name}: {sorted(compared)}. The generator builds "
        f"{len(generated)}, so either the array has stopped matching the "
        "pattern in this file or it really is that far behind. Both tests "
        "below would otherwise pass having compared almost nothing.")


def test_every_generated_asset_is_compared(compared, generated) -> None:
    """The failure this file was written for. An asset absent from that array
    is one whose rows nothing ever reads back out and checks."""
    missing = sorted(set(generated) - set(compared))
    assert not missing, (
        f"{UNREAL_TEST.name} never compares {missing} against their CSVs. "
        "Nothing else does either: the automation test's Tables[] array is the "
        "only thing that reads the rows back out of a DataTable asset. Add a "
        "line for each.")


def test_no_asset_is_compared_that_is_not_built(compared, generated) -> None:
    """The other direction. A pair naming an asset the generator does not build
    fails in the editor as a load error, which reads like missing content."""
    stray = sorted(set(compared) - set(generated))
    assert not stray, (
        f"{UNREAL_TEST.name} compares {stray}, which "
        f"{ASSET_GENERATOR.name} does not build. Either the table was removed "
        "and the line was not, or the asset name is misspelt.")


def test_the_two_lists_agree_on_which_csv_each_asset_came_from(
        compared, generated) -> None:
    """An asset paired with the wrong CSV compares two unrelated tables, and the
    failure names neither the real asset nor the real file."""
    wrong = {asset: (compared[asset], generated[asset])
             for asset in sorted(set(compared) & set(generated))
             if compared[asset] != generated[asset]}
    assert not wrong, (
        f"{UNREAL_TEST.name} and {ASSET_GENERATOR.name} disagree about which "
        "CSV an asset was built from. Asset: compared against, built from -- "
        + "; ".join(f"{a}: {c}, {g}" for a, (c, g) in wrong.items()))
