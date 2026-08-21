"""A dungeon floor must be big enough to walk across and small enough to finish.

WHY THIS IS A PYTHON TEST. Continuous integration never builds the C++ -- issue
#20 is the self-hosted runner that would -- so the automation tests in
`game/Source/Cataclysm/Tests/CataclysmFloorGeneratorTests.cpp` only ever run on a
developer's machine. This runs on every pull request. It reads the generator's
size constants out of the source text and checks what they mean in metres and
seconds against the player's designed walking speed.

WHAT IT IS GUARDING. The project owner set the pace on 2026-08-21: "Each floor
should take the player between 2-5 minutes to complete so long as they're being
efficient and don't get unlucky when searching for the stairs leading down to the
next floor." The floor's size in cells and the size of a cell in centimetres are
the two numbers that decide how much of that is walking, and neither is written
down anywhere else. Halving the cell size would quietly turn a two-minute floor
into a thirty-second one and nothing would notice.

WHAT IT CANNOT CHECK. How long a fight takes, or how long searching for the
stairs takes. Only the automation tests know the actual route the generator lays
out, and none of them knows how long combat lasts. So this holds the crossing --
walking the long side of the floor once, at the designed speed, with nothing in
the way -- and the band it holds it in is a judgement, stated below.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                    / "CataclysmFloorGenerator.h")
CLASS_STATS_CSV = REPO_ROOT / "game" / "Data" / "ClassStats.csv"

#: Metres to centimetres. The design works in metres; Unreal walks in
#: centimetres.
CM_PER_METRE = 100.0

#: The band the crossing has to land in, in seconds, and it is a JUDGEMENT.
#:
#: Under the floor of this band a floor is one room and the walk to the stairs is
#: a formality. Over the ceiling, a single crossing eats most of a five-minute
#: floor before anything has been fought. Nothing in the design document fixes
#: either number; what is fixed is the two-to-five minute target the crossing has
#: to fit inside several times over.
LEAST_SECONDS_TO_CROSS = 25.0
MOST_SECONDS_TO_CROSS = 90.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def constant(name: str, kind: str) -> float:
    """The value of a `static constexpr <kind> <name> = <number>;` line."""
    suffix = r"f" if kind == "float" else ""
    match = re.search(
        rf"static\s+constexpr\s+{kind}\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?){suffix}\s*;",
        source(GENERATOR_HEADER),
    )
    if match is None:
        pytest.fail(
            f"{GENERATOR_HEADER.name} has no "
            f"'static constexpr {kind} {name} = <number>;' line. If it was "
            f"renamed, rename it here too; if it was deleted, nothing checks "
            f"how big a dungeon floor is any more."
        )
    return float(match.group(1))


def class_stat(class_name: str, stat: str) -> float:
    """One `Base` figure from game/Data/ClassStats.csv."""
    if not CLASS_STATS_CSV.is_file():
        pytest.fail(f"{CLASS_STATS_CSV.relative_to(REPO_ROOT)} does not exist")

    with CLASS_STATS_CSV.open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["ClassName"] == class_name and row["Stat"] == stat:
                return float(row["Base"])

    pytest.fail(
        f"game/Data/ClassStats.csv has no {stat} row for {class_name}. It is "
        f"generated from the design workbook, so either the sheet lost the row "
        f"or the generator stopped writing it."
    )


@pytest.fixture(scope="module")
def cell_size_cm() -> float:
    return constant("CellSizeCm", "float")


@pytest.fixture(scope="module")
def default_width() -> float:
    return constant("DefaultWidth", "int32")


@pytest.fixture(scope="module")
def default_height() -> float:
    return constant("DefaultHeight", "int32")


@pytest.fixture(scope="module")
def walk_speed_metres() -> float:
    return class_stat("Default", "movement_speed")


def test_the_walk_speed_is_a_real_number(walk_speed_metres) -> None:
    """Everything below divides by it."""
    assert walk_speed_metres > 0.0, (
        "the Default class walks at zero metres per second, so no crossing time "
        "can be computed"
    )


def test_crossing_a_floor_takes_a_stated_time(cell_size_cm, default_width,
                                              default_height,
                                              walk_speed_metres) -> None:
    """Walking the long side of a floor once lands inside the stated band."""
    longest_side_cells = max(default_width, default_height)
    metres = longest_side_cells * cell_size_cm / CM_PER_METRE
    seconds = metres / walk_speed_metres

    assert LEAST_SECONDS_TO_CROSS <= seconds <= MOST_SECONDS_TO_CROSS, (
        f"a dungeon floor is {longest_side_cells:.0f} cells of "
        f"{cell_size_cm:.0f} cm, which is {metres:.0f} metres, and crossing it "
        f"once at the designed {walk_speed_metres:.1f} m/s takes "
        f"{seconds:.0f} seconds. That is outside the "
        f"{LEAST_SECONDS_TO_CROSS:.0f} to {MOST_SECONDS_TO_CROSS:.0f} second "
        f"band this test holds. The project owner's target is two to five "
        f"minutes for a whole floor including fighting and finding the stairs, "
        f"so one crossing has to fit inside that several times over. Either the "
        f"grid size or the cell size moved; decide which is right rather than "
        f"widening the band."
    )


def test_a_passage_two_cells_wide_is_wide_enough_to_be_a_corridor(
        cell_size_cm) -> None:
    """The generator's answer to "no tiny tedious hallways" has to mean something.

    `ConnectionWidth` is two cells, chosen because a passage one cell wide can
    only be left the way it was entered. That is only an answer if two cells is
    actually a corridor rather than a gap. Four metres is roughly three player
    capsules across, so two cells is eight metres.
    """
    two_cells_metres = 2 * cell_size_cm / CM_PER_METRE
    assert two_cells_metres >= 4.0, (
        f"a two-cell passage is {two_cells_metres:.1f} metres across. Below "
        f"four metres it stops reading as a corridor two people can walk down "
        f"and the width no longer answers the constraint it was chosen for."
    )


def test_the_header_says_the_floors_size_in_metres_correctly(
        cell_size_cm, default_width) -> None:
    """The prose beside the constant must agree with the constant.

    Issue #468 records this class of failure: prose next to a number went stale
    for a day and nothing noticed. The header says, beside `DefaultWidth`, how
    many metres the floor is. This checks the arithmetic in that sentence.
    """
    text = source(GENERATOR_HEADER)
    match = re.search(r"(\d+)\s*x\s*(\d+)\s*m is (\d+) metres", text)
    if match is None:
        pytest.fail(
            "CataclysmFloorGenerator.h no longer states the floor's size in "
            "metres in the form '40 x 4 m is 160 metres'. Either restore the "
            "sentence or delete this test, but do not leave a claim about "
            "metres in the header that nothing checks."
        )

    said_cells, said_cell_metres, said_total = (int(part) for part in match.groups())

    assert said_cells == int(default_width), (
        f"the header says a floor is {said_cells} cells across and "
        f"DefaultWidth is {default_width:.0f}"
    )
    assert said_cell_metres == int(cell_size_cm / CM_PER_METRE), (
        f"the header says a cell is {said_cell_metres} m and CellSizeCm is "
        f"{cell_size_cm:.0f} cm"
    )
    assert said_total == said_cells * said_cell_metres, (
        f"the header says {said_cells} x {said_cell_metres} m is {said_total} "
        f"metres, and it is {said_cells * said_cell_metres}"
    )
