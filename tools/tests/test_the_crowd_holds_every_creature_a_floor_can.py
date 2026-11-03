"""The Detour crowd holds every creature a floor can, twice over.

WHY THIS EXISTS. Issue #761. Every creature walks as a crowd agent through
`ACataclysmEnemyController`, and Unreal's `UCrowdManager` holds at most
`MaxAgents` of them. Its constructor sets 50 and `BaseEngine.ini` does not change
it, while `game/Source/Cataclysm/Dungeon/CataclysmFloorPopulation.h` records
floors of up to 510 creatures measured over 1,000 seeds of each layout. A
creature the crowd has no room for is not simulated by it; the controller takes
it back out of crowd mode and it walks without avoidance.

WHAT IS CHECKED. That `game/Config/DefaultEngine.ini` sets `MaxAgents` in the
section the engine reads it from, at no less than twice the largest measured
floor -- twice because `Cataclysm.DungeonEnemyScale 2` is a documented setting
that doubles a floor -- and that the two figures the Unreal test
`Cataclysm.Crowd.TheCrowdHoldsTwiceTheLargestFloor` copies are still these.

WHAT IS NOT CHECKED. That the engine applies the setting: the Unreal test reads it
back from the engine, and `Cataclysm.FloorCrowd.` holds sixty Imps in a live crowd
on a navigable floor. Continuous integration compiles no C++, so this is what runs
on a pull request.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ENGINE_INI = REPO_ROOT / "game" / "Config" / "DefaultEngine.ini"
FLOOR_POPULATION = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                    / "CataclysmFloorPopulation.h")
CROWD_TESTS = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
               / "CataclysmCrowdTests.cpp")

#: The section a class default of `UCrowdManager` is read from. The class is
#: `config = Engine, defaultconfig` in `AIModule`; a key in any other section is
#: silently ignored and the crowd keeps its 50.
SECTION = "[/Script/AIModule.CrowdManager]"

#: `MaxAgents=1024`, not a comment line that mentions it.
INI_MAX_AGENTS = re.compile(r"^MaxAgents\s*=\s*(\d+)\s*$", re.MULTILINE)

#: ` *     Caverns    84 to 510 creatures in  15 to 155 groups` in the header's
#: measured table.
MEASURED_ROW = re.compile(
    r"^\s*\*\s+(Halls|Caverns|Arena)\s+(\d+)\s+to\s+(\d+)\s+creatures\s+in\b",
    re.MULTILINE)

#: The two copies in the Unreal test.
COPIED = re.compile(r"constexpr\s+int32\s+(RuledMaxAgents|LargestMeasuredFloor)"
                    r"\s*=\s*(\d+)\s*;")

#: The engine's own figure, from `UCrowdManager`'s constructor in engine 5.8.
ENGINE_DEFAULT = 50


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def section_of(text: str, name: str) -> str:
    """The body of one INI section, or "" when it is not there."""
    start = text.find(name)
    if start < 0:
        return ""
    rest = text[start + len(name):]
    end = rest.find("\n[")
    return rest if end < 0 else rest[:end]


def configured_max_agents() -> int:
    body = section_of(read(DEFAULT_ENGINE_INI), SECTION)
    if not body:
        pytest.fail(
            f"game/Config/DefaultEngine.ini has no {SECTION} section, so the "
            f"crowd holds the engine's {ENGINE_DEFAULT} creatures and a floor of "
            f"hundreds leaves most of them out of it. Issue #761.")
    found = INI_MAX_AGENTS.findall(body)
    if len(found) != 1:
        pytest.fail(
            f"{SECTION} in game/Config/DefaultEngine.ini sets MaxAgents "
            f"{len(found)} times rather than once. Issue #761.")
    return int(found[0])


def measured_floors() -> dict[str, int]:
    """The largest count measured for each layout, from the header's table."""
    rows = MEASURED_ROW.findall(read(FLOOR_POPULATION))
    return {layout: int(most) for layout, _least, most in rows}


def test_the_measured_table_is_still_there_to_be_read():
    """A control: the comparison below means nothing if the table moved."""
    floors = measured_floors()
    assert set(floors) == {"Halls", "Caverns", "Arena"}, (
        f"read {sorted(floors)} from the measured table in "
        f"CataclysmFloorPopulation.h rather than the three layouts, so the "
        f"crowd's size is no longer being compared with anything. Issue #761.")


def test_the_crowd_holds_twice_the_largest_floor_measured():
    largest = max(measured_floors().values())
    max_agents = configured_max_agents()
    assert max_agents >= 2 * largest, (
        f"MaxAgents is {max_agents} in game/Config/DefaultEngine.ini, and the "
        f"largest floor measured holds {largest} creatures "
        f"(CataclysmFloorPopulation.h). The crowd must hold at least twice "
        f"that, {2 * largest}, because Cataclysm.DungeonEnemyScale 2 doubles a "
        f"floor; a creature it has no room for walks without avoidance. "
        f"Issue #761.")


def test_the_unreal_test_copies_the_same_two_figures():
    copies = dict(COPIED.findall(read(CROWD_TESTS)))
    assert set(copies) == {"RuledMaxAgents", "LargestMeasuredFloor"}, (
        f"CataclysmCrowdTests.cpp no longer declares both copied figures; found "
        f"{sorted(copies)}. Issue #761.")
    assert int(copies["RuledMaxAgents"]) == configured_max_agents(), (
        f"Cataclysm.Crowd. expects MaxAgents {copies['RuledMaxAgents']} and "
        f"game/Config/DefaultEngine.ini sets {configured_max_agents()}. Change "
        f"both together. Issue #761.")
    assert int(copies["LargestMeasuredFloor"]) == max(measured_floors().values()), (
        f"Cataclysm.Crowd. copies the largest measured floor as "
        f"{copies['LargestMeasuredFloor']} and CataclysmFloorPopulation.h now "
        f"says {max(measured_floors().values())}. Issue #761.")
