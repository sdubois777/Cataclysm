"""The dungeon level's navigation bounds must cover a whole dungeon floor.

WHAT GOES WRONG WITHOUT THIS, AND IT IS SILENT. A navigation mesh is only built
inside a bounds volume. `tools/generate_dungeon_map.py` places one in
`L_Dungeon`, sized from its own copy of how big a floor is, because the level is
authored once and the floor is generated at run time. If the C++ constants move
and that copy does not, the volume stops covering the outer ring of every floor.
Nothing reports it. Enemies out there simply never path, and click-to-move into
that ring finds nothing -- the failure issue #142 records, where the character
turns to face the point clicked and stops.

WHY IT IS A PYTHON TEST. Continuous integration never builds the C++ and never
opens the editor -- issue #20 is the self-hosted runner that would -- so nothing
on a pull request compiles the constants or regenerates the map. This reads both
sides as text and compares them, and it runs on every pull request.

WHICH SIDE IS AUTHORITATIVE. The C++. When this fails, the usual fix is to change
the numbers in `tools/generate_dungeon_map.py` and then regenerate the level:

    python tools/run_editor_python.py tools/generate_dungeon_map.py

WHAT IT CANNOT CHECK. Whether `game/Content/Maps/L_Dungeon.umap` was actually
regenerated after the script changed. The map is a binary asset and nothing here
can read the volume inside it. So this catches the script disagreeing with the
C++, not the map disagreeing with the script.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                    / "CataclysmFloorGenerator.h")
FLOOR_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                / "CataclysmDungeonFloor.h")
MAP_SCRIPT = REPO_ROOT / "tools" / "generate_dungeon_map.py"
MAP_ASSET = REPO_ROOT / "game" / "Content" / "Maps" / "L_Dungeon.umap"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def cpp_constant(path: pathlib.Path, name: str, kind: str) -> float:
    """The value of a `static constexpr <kind> <name> = <number>;` line."""
    suffix = "f" if kind == "float" else ""
    match = re.search(
        rf"static\s+constexpr\s+{kind}\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?){suffix}\s*;",
        source(path),
    )
    if match is None:
        pytest.fail(
            f"{path.name} has no 'static constexpr {kind} {name} = <number>;' "
            f"line. If it was renamed, rename it here too; if it was deleted, "
            f"nothing checks that the dungeon level covers a floor any more."
        )
    return float(match.group(1))


def script_constant(name: str) -> float:
    """The value of a `NAME = <number>` line in the map generator."""
    match = re.search(rf"^{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)\s*$",
                      source(MAP_SCRIPT), re.MULTILINE)
    if match is None:
        pytest.fail(
            f"tools/generate_dungeon_map.py has no '{name} = <number>' line. "
            f"It is one of the numbers that decide how big the navigation "
            f"bounds in L_Dungeon are."
        )
    return float(match.group(1))


@pytest.fixture(scope="module")
def floor_width_cm() -> float:
    """How wide the LARGEST dungeon floor is, from the C++.

    The largest and not the usual one. A floor's size is rolled per floor, the
    level is authored once, and every floor is played in it.
    """
    return (cpp_constant(GENERATOR_HEADER, "MostFloorSide", "int32")
            * cpp_constant(GENERATOR_HEADER, "CellSizeCm", "float"))


def test_the_map_script_agrees_with_the_cpp_about_a_cell() -> None:
    """The script copies the cell size and the cell count; both must match."""
    assert script_constant("CELL_SIZE_CM") == cpp_constant(
        GENERATOR_HEADER, "CellSizeCm", "float"), (
        "tools/generate_dungeon_map.py and CataclysmFloorGenerator.h disagree "
        "about how wide a cell is. The C++ is authoritative."
    )
    assert script_constant("FLOOR_CELLS_ACROSS") == cpp_constant(
        GENERATOR_HEADER, "MostFloorSide", "int32"), (
        "tools/generate_dungeon_map.py and CataclysmFloorGenerator.h disagree "
        "about how many cells the LARGEST floor is across. The C++ is "
        "authoritative, and the largest is what the level has to cover."
    )


def test_floors_really_do_vary_in_size() -> None:
    """The two ends of the range are different, so a floor's size is rolled.

    THIS USED TO CHECK A FLOOR WAS SQUARE, because the script sizes the bounds
    from a single number and a floor was 40 by 40 always. Both axes are now
    rolled from the same range, so one number still covers both, and what is
    worth checking instead is that the range is a range.

    It is the cheapest guard there is against the whole variety of floor sizes
    being quietly turned off, which is the state this project was in until
    2026-08-21: every floor of every dungeon exactly the same size.
    """
    least = cpp_constant(GENERATOR_HEADER, "LeastFloorSide", "int32")
    most = cpp_constant(GENERATOR_HEADER, "MostFloorSide", "int32")

    assert most > least, (
        f"a dungeon floor is always {most:.0f} cells on a side, so every floor "
        f"of a dungeon is the same size. LeastFloorSide and MostFloorSide in "
        f"CataclysmFloorGenerator.h are what make floors differ in size."
    )
    assert most - least >= 8, (
        f"a dungeon floor is between {least:.0f} and {most:.0f} cells, a range "
        f"of {most - least:.0f}. That is too narrow to read as two different "
        f"floors; 8 cells is 32 metres."
    )


def test_the_navigation_bounds_are_built_from_the_floors_own_width() -> None:
    """The width is a sum, not a literal, and the sum has to stay that shape.

    Pinned because the test below reproduces this arithmetic. A script that
    started writing a fixed number instead would leave that test comparing two
    values neither of which came from the map.
    """
    assert re.search(r"^NAV_WIDTH_CM\s*=\s*FLOOR_WIDTH_CM\s*\+\s*NAV_MARGIN_CM\s*$",
                     source(MAP_SCRIPT), re.MULTILINE), (
        "tools/generate_dungeon_map.py no longer sizes its navigation bounds as "
        "the floor's width plus a margin. Whatever it does now, this test and "
        "the one below have to be taught it."
    )
    assert re.search(r"^FLOOR_WIDTH_CM\s*=\s*FLOOR_CELLS_ACROSS\s*\*\s*CELL_SIZE_CM\s*$",
                     source(MAP_SCRIPT), re.MULTILINE), (
        "tools/generate_dungeon_map.py no longer works out a floor's width as "
        "its cell count times its cell size."
    )


def test_the_navigation_bounds_are_wider_than_a_floor(floor_width_cm) -> None:
    """Wider, not merely equal, so the mesh reaches past the outermost wall."""
    nav_width = (script_constant("FLOOR_CELLS_ACROSS")
                 * script_constant("CELL_SIZE_CM")
                 + script_constant("NAV_MARGIN_CM"))

    assert nav_width > floor_width_cm, (
        f"the navigation bounds in L_Dungeon are {nav_width:.0f} cm across and "
        f"a floor is {floor_width_cm:.0f} cm. The outer ring of every dungeon "
        f"would have no navigation mesh, enemies would stop pathing there, and "
        f"nothing would report why."
    )


def test_the_navigation_bounds_are_tall_enough_for_the_geometry() -> None:
    """From under the ground blocks to over the walls."""
    ground = cpp_constant(FLOOR_HEADER, "GroundThicknessCm", "float")
    wall = cpp_constant(FLOOR_HEADER, "WallHeightCm", "float")
    nav_height = script_constant("NAV_HEIGHT_CM")

    assert nav_height > ground + wall, (
        f"the navigation bounds in L_Dungeon are {nav_height:.0f} cm tall, and "
        f"the geometry runs from {ground:.0f} cm below the walking surface to "
        f"{wall:.0f} cm above it. The tops of the walls would be outside the "
        f"volume."
    )


def test_the_readme_names_the_dungeon_level_and_it_is_there() -> None:
    """The readme's claim about the dungeon level has something checking it.

    REPLACING A GUARD THAT WENT DORMANT. `game/README.md` used to say "`L_Sandbox`
    is the only map", and
    `tools/tests/test_game_readme_is_true.py::test_no_procedural_dungeon_claim_is_still_true`
    checked that by counting the project's maps. Adding `L_Dungeon` made that
    claim false, so the sentence was rewritten -- and that test skips itself when
    the sentence is gone, which is right for it and leaves the new sentence
    unchecked. This is the replacement.

    Issue #468 records what happens otherwise: prose beside a fact goes stale and
    nothing notices.
    """
    readme = REPO_ROOT / "game" / "README.md"
    text = source(readme)

    assert "L_Dungeon" in text, (
        "game/README.md does not mention L_Dungeon, the level a generated "
        "dungeon floor is played in. If the level was removed, remove this test "
        "too; if it was renamed, say so in the readme."
    )
    assert MAP_ASSET.is_file(), (
        "game/README.md says L_Dungeon exists and "
        "game/Content/Maps/L_Dungeon.umap does not."
    )


def test_the_dungeon_level_exists() -> None:
    """The script has actually been run and its output committed.

    A generator whose output was never committed is a level nobody can open, and
    the failure looks like the map missing rather than like a step being skipped.
    """
    assert MAP_ASSET.is_file(), (
        "game/Content/Maps/L_Dungeon.umap is not there. Build it with "
        "'python tools/run_editor_python.py tools/generate_dungeon_map.py' and "
        "commit it."
    )
