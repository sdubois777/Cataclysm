"""A Vengeful Wraith is saved as a wraith and comes back from a load as one, with the run record at version 10.

WHY THIS EXISTS. Issue #2725: a saved creature carried no field saying it was a Vengeful Wraith, and
`ACataclysmDungeonGameMode::RestoreCreatureRoles` had a branch for every other rule creature and none for a wraith, so
after a load a wraith took every hit in full, hit 20% less hard, moved at its kind's speed, and was not in the rule's
list, so a later rung change did not put its figures back either. The coordinating session approved the fix on
2026-11-19: `FCataclysmSavedCreature::bIsVengefulWraith`, written by `CreatureRolesInto` and read back by
`RestoreCreatureRoles`, at RUN RECORD version 10 -- the run record's own number, reached the same day as the character
record's own 10 (issue #2764).

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the automation tests
`Cataclysm.SaveLoad.AVengefulWraithComesBackAWraithAfterALoad` and
`Cataclysm.SaveLoad.ARunFileFromVersion9MigratesWithNoWraith` never run on a pull request. Reading the source and the
committed save files as text does.

WHAT IS CHECKED:

  1. `Run_v10.json` is `Run_v9.json` at version 10 with `bIsVengefulWraith` true on its first creature and false on its
     second, and nothing else changed;
  2. the saved creature carries the field, the run record is at version 10 or later, and `Migrate_9_to_10` writes false
     on every creature of the floor and is in the chain;
  3. `CreatureRolesInto` writes the field from the creature's mark and the rule's list, and `RestoreCreatureRoles` puts
     a saved wraith in the list BEFORE it asks for the figures, which `ApplyVengefulWraithFigures` writes only for a
     creature in the list;
  4. every test that runs an older run file through the whole chain adds what `Migrate_9_to_10` writes, and no test
     reads `Run_v9.json` but the one that migrates it;
  5. the fixtures README lists `Run_v10.json`.

WHAT IS NOT: what a restored wraith does in a fight. The automation tests check that.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE = SOURCE / "Cataclysm" / "Save"
TESTS = SOURCE / "Cataclysm" / "Tests"
GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"

#: The one test that reads `Run_v9.json` since the run record went to version 10: it migrates it.
MIGRATION_TEST = "ARunFileFromVersion9MigratesWithNoWraith"


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


def test_run_v10_is_run_v9_with_the_first_creature_a_wraith_and_nothing_else_changed() -> None:
    before = json.loads((FIXTURES / "Run_v9.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v10.json").read_text(encoding="utf-8"))
    assert before["SchemaVersion"] == 9 and after["SchemaVersion"] == 10
    flags = [creature.get("bIsVengefulWraith") for creature in after["Floor"]["Creatures"]]
    assert flags == [True, False], (
        f"Run_v10.json's creatures are {flags}: the first must be a wraith, which is not the default, so the test that "
        "reads it shows the field is read, and the second not one, so each creature keeps its own")
    assert all("bIsVengefulWraith" not in creature for creature in before["Floor"]["Creatures"])

    rest = json.loads(json.dumps(after))
    rest["SchemaVersion"] = 9
    for creature in rest["Floor"]["Creatures"]:
        del creature["bIsVengefulWraith"]
    assert rest == before, "Run_v10.json differs from Run_v9.json beyond its version and its creatures' wraith marks"


def test_the_run_record_is_at_version_10_and_migrate_9_to_10_writes_no_wraith() -> None:
    """Version 10 or later; the field on the saved creature; a step from 9 writing false on each creature, in the chain."""
    header = code(SAVE / "CataclysmSaveRecords.h")
    run_class = header.split("class CATACLYSM_API UCataclysmRunSave")[1]
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_class)
    assert version is not None and int(version.group(1)) >= 10, "the run record is below version 10"
    creature = braced(header, header.index("struct CATACLYSM_API FCataclysmSavedCreature"), "FCataclysmSavedCreature")
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\)\s*bool\s+bIsVengefulWraith\s*=\s*false\s*;", creature), (
        "FCataclysmSavedCreature does not carry a saved bIsVengefulWraith, false by default")

    records = code(SAVE / "CataclysmSaveRecords.cpp")
    migrations = braced(records, records.index("namespace CataclysmRunSaveMigration"), "CataclysmRunSaveMigration")
    step = body(migrations, "bool Migrate_9_to_10(", "the run record's migrations")
    creatures = step[step.find('TEXT("Floor")'):]
    assert 'TEXT("Creatures")' in creatures and re.search(
        r'SetBoolField\(\s*TEXT\("bIsVengefulWraith"\)\s*,\s*false\s*\)', creatures), (
        "Migrate_9_to_10 does not write a bIsVengefulWraith of false on each creature of the floor")
    chain = body(migrations, "const FCataclysmSaveMigrationStep RunSteps[] =", "the run record's chain")
    steps = re.findall(r"\{\s*(\d+)\s*,\s*TEXT\(\"(Migrate_\d+_to_\d+)\"\)", chain)
    assert ("9", "Migrate_9_to_10") in steps, f"the run record's chain has no step from 9: {steps}"


def test_the_game_mode_writes_the_mark_and_puts_a_saved_wraith_back_in_the_list_first() -> None:
    mode = code(GAME_MODE)
    gather = body(mode, "void ACataclysmDungeonGameMode::CreatureRolesInto(", "the dungeon game mode")
    assert re.search(r"Record\.bIsVengefulWraith\s*=\s*Creature\.bIsVengefulWraith\s*&&\s*VengefulWraiths\.Contains\(",
                     gather), "CreatureRolesInto does not write the wraith mark from the creature's flag and the list"

    restore = body(mode, "void ACataclysmDungeonGameMode::RestoreCreatureRoles(", "the dungeon game mode")
    branch = restore.find("if (Saved.bIsVengefulWraith)")
    assert branch != -1, "RestoreCreatureRoles has no branch for a saved wraith"
    wraith = braced(restore, branch, "RestoreCreatureRoles' wraith branch")
    listed = wraith.find("VengefulWraiths.Add(&Creature);")
    figures = wraith.find("ApplyVengefulWraithFigures(&Creature);")
    assert listed != -1 and figures != -1 and listed < figures, (
        "a saved wraith is not put in the rule's list before its figures are asked for; ApplyVengefulWraithFigures "
        "writes nothing for a creature outside the list")


def test_every_chain_test_adds_version_10_and_only_the_migration_reads_run_v9() -> None:
    nine = 0
    for path in sorted(TESTS.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        nine_here = text.count("CataclysmSaveFixtures::AddTheRunsVersion9Defaults(*Expected);")
        ten_here = text.count("CataclysmSaveFixtures::AddTheRunsVersion10Defaults(*Expected);")
        assert nine_here <= ten_here, (
            f"{path.name} adds what Migrate_8_to_9 writes in {nine_here} place(s) and what Migrate_9_to_10 writes in "
            f"{ten_here}: a chain test that stops at version 9 compares the wrong file")
        nine += nine_here
        for match in re.finditer(r'"Run_v9\.json"', text):
            before = text[:match.start()]
            name = re.findall(r'"Cataclysm\.[A-Za-z]+\.([A-Za-z0-9]+)"', before)
            assert name and name[-1] == MIGRATION_TEST, (
                f"{path.name} reads Run_v9.json in {name[-1] if name else 'a test with no name before it'}: the "
                "committed run file is Run_v10.json since the run record went to version 10 (issue #2725)")
    assert nine >= 8, f"only {nine} chain tests were found; the sweep reads the wrong folder"
    helpers = code(TESTS / "CataclysmSaveFixtures.h")
    defaults = body(helpers, "inline void AddTheRunsVersion10Defaults(", "CataclysmSaveFixtures.h")
    assert re.search(r'SetBoolField\(\s*TEXT\("bIsVengefulWraith"\)\s*,\s*false\s*\)', defaults)


def test_the_fixtures_readme_lists_run_v10() -> None:
    readme = (FIXTURES / "README.md").read_text(encoding="utf-8")
    assert "| `Run_v10.json` |" in readme, "game/Tests/SaveFixtures/README.md does not list Run_v10.json"
