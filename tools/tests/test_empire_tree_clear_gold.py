"""The empire upgrade tree's slice 6b: the five gold nodes that need the run record, at run record version 12.

WHY THIS EXISTS. Issue #2741 slice 6b, the coordinating session's B1 to B5 of 2026-11-20. Price Controls reads a city's
saved mark of Surge damage, Stimulus Package a saved run flag, Cleanup Crew the walk's saved gold counts, and Salvage
Rights and Investment Funds pay at a clear. The run record went to version 12 for the three saved figures.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeClearGold.`
automation tests never run on a pull request. And the order at a clear -- the pay worked out before the clear removes the
dungeon and its floors, and paid only once the clear stands -- is where no automation test reaches.

WHAT IS CHECKED:

  1. `Run_v12.json` is `Run_v11.json` at version 12 with the three saved figures added, none of them the default or
     what the migration writes, and nothing else changed;
  2. the run record is at version 12 or later, carries the figures, and `Migrate_11_to_12` writes false, false and 0
     and 0, and is in the chain; the save gathers and applies the run's flag and the city's mark;
  3. `UCataclysmEmpireMap::Damage` marks a city it takes anything from, and every caller of it outside the tests is in
     the run, one of C4's three; the run's flag is checked after a day's resolutions and after a death's, cleared at a
     reclaim, and set nowhere else;
  4. the clear works the pay out before `Run->ClearDungeon` and pays it inside the branch where the clear stands;
  5. the walk counts the gold its kills drop and the gold picked up, begins at nought, and saves and restores both;
  6. every test that runs an older run file through the chain adds what `Migrate_11_to_12` writes, and no test reads
     `Run_v11.json` but the one that migrates it; the fixtures README lists `Run_v12.json`.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE = SOURCE / "Cataclysm" / "Save"
TESTS = SOURCE / "Cataclysm" / "Tests"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
SPAWNER = SOURCE / "Cataclysm" / "Items" / "CataclysmDroppedItem.cpp"

#: The one test that reads `Run_v11.json` since the run record went to version 12: it migrates it.
MIGRATION_TEST = "ARunFileFromVersion11MigratesUnmarkedAndUncounted"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def braced(text: str, start: int, where: str) -> str:
    """The braces-matched block that opens at the first `{` after `start`."""
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{where} is not closed")


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    return braced(text, start, f"{signature!r} in {where}")


def test_a_bite_marks_its_city_and_the_flag_is_checked_after_bites_and_cleared_at_a_reclaim() -> None:
    damage = body(code(EMPIRE / "CataclysmEmpireMap.cpp"), "bool UCataclysmEmpireMap::Damage(",
                  "CataclysmEmpireMap.cpp")
    assert re.search(r"if\s*\(\s*DefenceTaken\s*>\s*0\.0f\s*\|\|\s*PopulationTaken\s*>\s*0\.0f\s*\)\s*\{\s*"
                     r"City->bTookSurgeDamage\s*=\s*true\s*;", damage), (
        "UCataclysmEmpireMap::Damage does not mark a city it takes anything from")

    callers = []
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.parts:
            continue
        callers.extend([path.name] * len(re.findall(r"\bMap->Damage\(", code(path))))
    assert callers == ["CataclysmEmpireRun.cpp"] * 3, (
        f"the callers of Map->Damage outside the tests are {callers}: the mark is Surge damage only while every one "
        "is a dungeon's (C4's three in the run)")

    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    day = body(run, "FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay()", "CataclysmEmpireRun.cpp")
    resolved = day.find("ResolveDungeon(DungeonId, Report);")
    noted = day.find("NotePopulationAgainstHalf();")
    assert -1 < resolved < noted, "AdvanceDay does not check the population after the day's resolutions"
    death = body(run, "UCataclysmEmpireRun::ResolveDungeonOnDeath(", "CataclysmEmpireRun.cpp")
    assert death.find("ResolveDungeon(DungeonId, Report);") < death.find("NotePopulationAgainstHalf();"), (
        "a death's resolution is not followed by the population check")
    clear = body(run, "bool UCataclysmEmpireRun::ClearDungeon(", "CataclysmEmpireRun.cpp")
    reclaimed = clear.find("++CitiesReclaimed;")
    assert reclaimed != -1 and re.search(r"bPopulationFellBelowHalf\s*=\s*false\s*;", clear[reclaimed:]), (
        "a reclaim does not clear the run's flag")
    assert len(re.findall(r"bPopulationFellBelowHalf\s*=\s*true\s*;", run)) == 1, (
        "the run's flag is set somewhere other than NotePopulationAgainstHalf")


def test_the_clear_works_the_pay_out_before_it_removes_the_dungeon_and_pays_once_it_stands() -> None:
    mode = code(MODE)
    clearing = body(mode, "bool ACataclysmDungeonGameMode::ClearEmpireDungeon(", "CataclysmDungeonGameMode.cpp")
    pay = clearing.find("EmpireTreeClearPayHere()")
    removed = clearing.find("Run->ClearDungeon(EmpireDungeonId)")
    assert -1 < pay < removed, (
        "the clear does not work out the tree's pay before Run->ClearDungeon removes the dungeon and its floors (B4)")
    stands = clearing.find("if (bCleared)", removed)
    paid = clearing.find("PayTheTreesClearGold(ClearPay);", removed)
    assert -1 < stands < paid and paid < stands + len(braced(clearing, stands, "the clear's branch")), (
        "the tree's pay is not paid inside the branch where the clear stands")
    here = body(mode, "FCataclysmEmpireTreeClearPay ACataclysmDungeonGameMode::EmpireTreeClearPayHere()",
                "CataclysmDungeonGameMode.cpp")
    for read in ("Dungeon->Floors", "UCataclysmDropRoll::GoldScaleFor(Run->DifficultyTier)", "GoldDroppedThisWalk",
                 "GoldPickedUpThisWalk"):
        assert read in here, f"the clear's pay does not read {read}"


def test_the_walk_counts_its_gold_from_nought_and_saves_it() -> None:
    spawner = code(SPAWNER)
    gold = body(spawner, "int32 UCataclysmDropSpawner::SpawnGoldFor(", "CataclysmDroppedItem.cpp")
    placed = gold.find("PutOnTheFloor(")
    assert -1 < placed < gold.find("ACataclysmDungeonGameMode::NoteGoldDroppedIn(World, Amount);"), (
        "SpawnGoldFor does not count each pile it puts down")
    bank = body(spawner, "int64 UCataclysmDropPickup::BankGold(", "CataclysmDroppedItem.cpp")
    assert "ACataclysmDungeonGameMode::NoteGoldPickedUpIn(World, Total);" in bank, (
        "a gold pickup is not counted in the walk")

    mode = code(MODE)
    enter = body(mode, "bool ACataclysmDungeonGameMode::EnterEmpireDungeon(", "CataclysmDungeonGameMode.cpp")
    for field in ("GoldDroppedThisWalk", "GoldPickedUpThisWalk"):
        assert re.search(rf"\b{field}\s*=\s*0\s*;", enter), f"a walk does not begin with {field} at nought"
    saved = body(mode, "FCataclysmSavedWalk ACataclysmDungeonGameMode::WalkStateNow()", "CataclysmDungeonGameMode.cpp")
    restored = body(mode, "void ACataclysmDungeonGameMode::RestoreWalkState(", "CataclysmDungeonGameMode.cpp")
    for field in ("GoldDroppedThisWalk", "GoldPickedUpThisWalk"):
        assert re.search(rf"Walk\.{field}\s*=\s*{field}\s*;", saved), f"the walk does not save {field}"
        assert re.search(rf"\b{field}\s*=\s*Walk\.{field}\s*;", restored), f"the walk does not restore {field}"


