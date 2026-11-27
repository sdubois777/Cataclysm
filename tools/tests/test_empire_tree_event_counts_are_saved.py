"""The empire upgrade tree's event rules count what a run does, the run record saves it at version 9, and each rule is
read where its event happens.

WHY THIS EXISTS. Issue #2741 slice 3b, ruled by the coordinating session on 2026-11-19 (Q1 to Q8): Global Vigilance
counts the dungeons cleared since the last surge, Reclaimer's Resolve the cities reclaimed in the run, and Last Stand
Protocols whether it has held; Enduring Foundations reads the day each city upgrade was bought. None of those can be
read off what a version 8 run saves, so the run record went to version 9, and `Migrate_8_to_9` writes "nothing counted,
and every upgrade held since the run began" into a run saved at version 8.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeEvents.` automation
tests never run on a pull request. Reading the source and the committed save files as text does. And a few of these
are places an automation test cannot reach every door of: every surge resets the clear count only if the reset is in
the one function every surge goes through.

WHAT IS CHECKED:

  1. `Run_v9.json` is `Run_v8.json` at version 9 with the three counts and a `BoughtDay` on every upgrade, none of them
     the default, and nothing else changed;
  2. the run record is at version 9 or later, and `Migrate_8_to_9` writes the three defaults and a `BoughtDay` of 0
     on every upgrade and is in the chain (its last step until the run record went to version 10, issue #2725);
  3. the save gathers and applies the three counts, and `Begin` starts them again;
  4. a clear raises the clear count beside `DungeonsCleared`, and `FireSurge`, which every surge goes through, sets it
     to 0 after `RecordSurge`;
  5. the Last Stand is armed immediately before the two hits a dungeon's resolution deals -- an ordinary resolve and a
     Quest dungeon's timer -- and before no other `Map->Damage` call in the run;
  6. a bought upgrade records the clock's day;
  7. the fixtures README lists `Run_v9.json`.

WHAT IS NOT: what each rule does to a city. The automation tests check that.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE = SOURCE / "Cataclysm" / "Save"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"

#: The three run fields, with the default `Migrate_8_to_9` writes for each.
COUNTS = {"ClearsSinceLastSurge": 0, "CitiesReclaimed": 0, "bSanctuaryLastStandSpent": False}


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
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where} is not closed")


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    return braced(text, start, f"{signature!r} in {where}")


def run_migrations() -> str:
    """The run record's migration namespace, comments removed."""
    text = code(SAVE / "CataclysmSaveRecords.cpp")
    start = text.find("namespace CataclysmRunSaveMigration")
    assert start != -1, "CataclysmSaveRecords.cpp no longer has a CataclysmRunSaveMigration namespace"
    return braced(text, start, "CataclysmRunSaveMigration")


def test_the_save_carries_the_counts_and_begin_starts_them_again() -> None:
    gather = code(SAVE / "CataclysmSaveGather.cpp")
    apply = code(SAVE / "CataclysmSaveApply.cpp")
    begin = body(code(EMPIRE / "CataclysmEmpireRun.cpp"), "void UCataclysmEmpireRun::BeginForCharacter(",
                 "CataclysmEmpireRun.cpp")
    for name, default in COUNTS.items():
        assert re.search(rf"Record\.{name}\s*=\s*Run\.{name}\s*;", gather), f"the save does not gather {name}"
        assert re.search(rf"Run\.{name}\s*=\s*Record\.{name}\s*;", apply), f"the save does not apply {name}"
        written = "false" if default is False else str(default)
        assert re.search(rf"\b{name}\s*=\s*{written}\s*;", begin), f"Begin does not start {name} again at {written}"


def test_a_clear_counts_and_every_surge_resets_the_count() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    clear = body(run, "bool UCataclysmEmpireRun::ClearDungeon(", "CataclysmEmpireRun.cpp")
    counted = clear.find("++DungeonsCleared;")
    assert counted != -1 and clear.find("++ClearsSinceLastSurge;", counted) != -1, (
        "ClearDungeon no longer raises ClearsSinceLastSurge beside DungeonsCleared")

    surge = body(run, "void UCataclysmEmpireRun::FireSurge(", "CataclysmEmpireRun.cpp")
    recorded = surge.find("Surges->RecordSurge(")
    assert recorded != -1 and re.search(r"ClearsSinceLastSurge\s*=\s*0\s*;", surge[recorded:]), (
        "FireSurge, which every surge goes through, no longer resets ClearsSinceLastSurge after RecordSurge")
    assert len(re.findall(r"ClearsSinceLastSurge\s*=\s*0\s*;", run)) == 2, (
        "ClearsSinceLastSurge is set to 0 somewhere other than Begin and FireSurge")


def test_the_last_stand_is_armed_before_the_two_resolution_hits_and_no_other() -> None:
    """The line before each `Map->Damage` call in the run: armed for the ordinary and Quest ones, not the Siege's."""
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    lines = [line.strip() for line in run.splitlines() if line.strip()]
    armed = {}
    for index, line in enumerate(lines):
        if "Map->Damage(" in line:
            armed[line] = lines[index - 1].startswith("ArmTheLastStandFor(")
    # THE ORDINARY RESOLVE'S IS THE ONE WITH THE VOID STACKS ON IT: a Siege's daily share calls `Map->Damage` with the
    # same three names and no stacks.
    ordinary = [line for line in armed if "Map->Damage(CityId, Defence, Population) || (bVoid" in line]
    quest = [line for line in armed if "Map->Damage(HostId, Ordinary.DefenceDamage" in line]
    others = [line for line in armed if line not in ordinary + quest]
    assert len(ordinary) == 1 and armed[ordinary[0]], "the ordinary resolve's hit is not armed on the line before it"
    assert len(quest) == 1 and armed[quest[0]], "a Quest dungeon's timer hit is not armed on the line before it"
    assert others and not any(armed[line] for line in others), (
        f"a hit that is not a dungeon's resolution is armed, or there is none to check: {others}")


def test_a_bought_upgrade_records_the_clocks_day() -> None:
    """The day an upgrade is finished, which Enduring Foundations reads: at once for a purchase with no build days,
    and on the day its build's days run out since issue #2825 (`tools/tests/test_city_upgrades_take_days_to_build.py`).
    """
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    complete = body(run, "void UCataclysmEmpireRun::CompleteCityUpgrade(", "CataclysmEmpireRun.cpp")
    assert re.search(r"Bought\.BoughtDay\s*=\s*Day\s*;", complete), "CompleteCityUpgrade does not record the day"
    buy = body(run, "ECataclysmCityUpgradeResult UCataclysmEmpireRun::BuyCityUpgrade(", "CataclysmEmpireRun.cpp")
    assert "CompleteCityUpgrade(CityId, Bought, Clock->Day);" in buy, (
        "a purchase finished at once is not stamped with the clock's day")
    advance = body(run, "void UCataclysmEmpireRun::AdvanceCityBuilds(", "CataclysmEmpireRun.cpp")
    assert "CompleteCityUpgrade(CityId, Done, Today);" in advance, "a finished build is not stamped with its day"


