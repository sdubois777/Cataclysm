"""Every generated CSV's columns must match the USTRUCT that imports it.

WHY THIS FAILS SILENTLY WITHOUT A TEST. Unreal imports a DataTable row by
matching each CSV column against a property name on the row struct. A column
whose name does not match is **not an error**: the import succeeds, the row is
created, and the property keeps its default value. So a renamed column, a typo,
or a property added to the struct and never written by the generator all produce
a table that loads cleanly and carries the wrong numbers. Nothing reports it, and
the first symptom is a creature behaving as though a designed figure were zero.

WHY IT IS CHECKED HERE, IN PYTHON. There is an Unreal automation test that
catches this properly -- `Cataclysm.Data.EveryGeneratedTableImports` builds each
table through its struct and pins the row count. It needs the engine, and
continuous integration builds no C++ at all: `.github/workflows/ci.yml` is a
single Linux job. So on a pull request, nothing checks this. Comparing the CSV
header against the header file is a text comparison, needs no engine, and runs
in the same two minutes as everything else.

It does not replace the automation test. This compares NAMES; the engine test
compares behaviour, and only it can catch a column whose name matches and whose
TYPE does not.

WHAT THIS FILE PARSES, AND WHY PARSING IS SAFE HERE. Two things, both read
rather than imported:

  tools/generate_datatable_assets.py   the TABLES list, for which struct imports
                                       which CSV. It imports `unreal` at module
                                       scope, so the system Python cannot import
                                       it; the same reader in
                                       test_datatable_assets_are_current.py does
                                       this and for the same reason.
  game/Source/.../CataclysmDataRows.h   the USTRUCT blocks and their UPROPERTY
                                       names.

A test built on a parser has a failure mode of its own: a regular expression that
silently matches nothing makes every assertion below vacuous, and the file passes
while checking nothing. `test_the_parser_actually_found_the_structs` is the guard
against that, and it is deliberately the first test in the file.
"""

from __future__ import annotations

import ast
import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "game" / "Data"
GENERATOR = REPO_ROOT / "tools" / "generate_datatable_assets.py"
ROW_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Data"
              / "CataclysmDataRows.h")

#: Every row struct derives from this, which is what makes it importable as a
#: DataTable row at all.
BASE = "FTableRowBase"

#: The DataTable row key. It is the row's FName rather than a property, so it is
#: in every CSV and in no struct.
KEY_COLUMN = "Name"

#: Below this, assume the parser broke rather than that the project shrank. The
#: project has fourteen structs today and has never had fewer than ten.
FEWEST_CREDIBLE_STRUCTS = 10


def read_structs() -> dict[str, list[str]]:
    """Row struct name against its UPROPERTY names, in declaration order.

    Handles both layouts the header uses: the UPROPERTY macro on its own line
    with the declaration under it, and the one-line form
    `UPROPERTY(...) FString MaterialName;` that FCataclysmCraftingMaterialRow
    uses throughout.
    """
    if not ROW_HEADER.is_file():
        pytest.skip(f"{ROW_HEADER.name} is not present")

    text = ROW_HEADER.read_text(encoding="utf-8")
    structs: dict[str, list[str]] = {}

    block = re.compile(
        r"struct\s+(F\w+)\s*:\s*public\s+" + BASE + r"\s*\{(.*?)\n\};", re.S)
    prop = re.compile(
        r"UPROPERTY\([^)]*\)\s*([A-Za-z0-9_:<>]+)\s+(\w+)\s*(?:=|;)")

    for match in block.finditer(text):
        name, body = match.group(1), match.group(2)
        structs[name] = [found.group(2) for found in prop.finditer(body)]
    return structs


def read_tables() -> list[tuple[str, str, str]]:
    """The TABLES list out of the asset generator, read without importing it.

    The generator imports `unreal` at module scope and runs inside the editor's
    Python, so the system Python running these tests cannot import it.
    """
    if not GENERATOR.is_file():
        pytest.skip("the DataTable asset generator is not present")

    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if "TABLES" in [t.id for t in node.targets if isinstance(t, ast.Name)]:
            return [tuple(ast.literal_eval(element))
                    for element in node.value.elts]
    pytest.fail(f"{GENERATOR.name} no longer has a TABLES list")


@pytest.fixture(scope="module")
def structs() -> dict[str, list[str]]:
    return read_structs()


@pytest.fixture(scope="module")
def tables() -> list[tuple[str, str, str]]:
    return read_tables()


def csv_columns(csv_file: str) -> list[str]:
    path = DATA_DIR / csv_file
    assert path.is_file(), (
        f"{csv_file} does not exist in game/Data/. Run "
        "`python tools/generate_datatables.py`.")
    with path.open(encoding="utf-8", newline="") as handle:
        return next(csv.reader(handle))


# --------------------------------------------------------------------------
# the guard on the parser itself
# --------------------------------------------------------------------------

def test_the_parser_actually_found_the_structs(structs) -> None:
    """Without this, a broken regular expression makes the whole file vacuous.

    Every test below iterates over what the parser produced. If it produced
    nothing, they all pass having compared nothing at all, and the file reads as
    coverage while providing none. So the parser's own output is asserted first:
    enough structs, and every one of them with properties in it.
    """
    assert len(structs) >= FEWEST_CREDIBLE_STRUCTS, (
        f"only {len(structs)} row struct(s) were parsed out of "
        f"{ROW_HEADER.name}: {sorted(structs)}. The project has more than that, "
        "so the parser in this file has stopped matching the header's layout. "
        "Every other test here would pass without comparing anything.")

    empty = sorted(name for name, props in structs.items() if not props)
    assert not empty, (
        f"these row structs parsed with no properties at all: {empty}. A "
        "struct really cannot have none, so the UPROPERTY pattern in this file "
        "has stopped matching how they are written.")


def test_every_table_names_a_struct_that_exists(tables, structs) -> None:
    """The generator asks the editor for a struct by name at run time and fails
    loudly there. This says the same thing without needing the editor."""
    for asset, csv_file, struct in tables:
        assert f"F{struct}" in structs, (
            f"{asset} imports {csv_file} through F{struct}, and "
            f"{ROW_HEADER.name} declares no such row struct. Declared: "
            f"{sorted(structs)}")


# --------------------------------------------------------------------------
# the comparison this file exists for
# --------------------------------------------------------------------------

def test_every_csv_column_has_a_property_on_its_row_struct(tables,
                                                           structs) -> None:
    """A column with no matching property imports as a default, with no error.

    That is the quiet one: the number is in the CSV, the pull request shows it,
    the table loads, and the game never sees it.
    """
    for _asset, csv_file, struct in tables:
        columns = [c for c in csv_columns(csv_file) if c != KEY_COLUMN]
        properties = set(structs[f"F{struct}"])

        unmatched = sorted(set(columns) - properties)
        assert not unmatched, (
            f"{csv_file} has column(s) {unmatched} that F{struct} has no "
            "property for. Unreal does not report this: the row imports and "
            "the value is discarded. Add the property to "
            f"{ROW_HEADER.name} or stop writing the column.")


def test_every_property_is_written_by_the_generator(tables, structs) -> None:
    """The other direction, which is the more dangerous of the two.

    A property no CSV writes silently keeps its C++ default. Reading the header
    shows a designed-looking figure, reading the CSV shows nothing missing, and
    the value the game uses is whatever was typed as the default years ago.
    """
    for _asset, csv_file, struct in tables:
        columns = set(csv_columns(csv_file))
        properties = structs[f"F{struct}"]

        unwritten = sorted(p for p in properties if p not in columns)
        assert not unwritten, (
            f"F{struct} declares {unwritten}, and {csv_file} has no such "
            "column, so those properties keep their C++ defaults on every row. "
            "Either write the column from tools/generate_datatables.py or "
            f"remove the property from {ROW_HEADER.name}.")


def test_the_row_key_is_not_also_a_property(tables, structs) -> None:
    """`Name` is the row's FName. A property called Name as well would shadow
    the key and make the table's own identity a field."""
    for _asset, _csv_file, struct in tables:
        assert KEY_COLUMN not in structs[f"F{struct}"], (
            f"F{struct} declares a property called {KEY_COLUMN}, which is what "
            "Unreal uses for the row key itself.")


def test_every_csv_starts_with_the_row_key(tables) -> None:
    """Unreal takes the first column as the row name whatever it is called, so
    a table whose first column is a real field loses that field and gets
    nonsense keys."""
    for _asset, csv_file, _struct in tables:
        columns = csv_columns(csv_file)
        assert columns[0] == KEY_COLUMN, (
            f"{csv_file} begins with column {columns[0]!r}. Unreal reads the "
            f"first column as the row key, so it must be {KEY_COLUMN!r}.")
