"""Each run has its own mask on its dungeons' walk seeds, saved on the run record at version 8, and a run saved before
it keeps exactly the seeds it had.

WHY THIS EXISTS. Issue #2749, B10, ruled by the coordinating session on 2026-11-18: "two runs' dungeons must differ,
because the design's dungeons are procedurally generated and a new character meeting the same dungeon 0 every run is
not that". Until then `FCataclysmDungeon::WalkSeed` was `max(1, DungeonId + 1)`, identifiers start at 0 in every run,
and every run's dungeon N walked the same floors with the same creatures. Now the identifier plus one is XORed with the
run's `UCataclysmEmpireRun::WalkSeedMask`, and `Migrate_7_to_8` writes a mask of 0 into a run saved at version 7, which
is the seed it had: "the migration should write the value that reproduces today's WalkSeed exactly, so a walk saved
mid-dungeon loads the same floors it was on".

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.WalkSeed.` automation tests
never run on a pull request. Reading the source and the committed save files as text does.

WHAT IS CHECKED:

  1. `Run_v8.json` is `Run_v7.json` at version 8 with a mask added that is not 0, and nothing else changed;
  2. the run record is at version 8 or later, and its `Migrate_7_to_8` writes a mask of 0 and is the chain's step from
     7 (it was the last until the run record went to version 9 on 2026-11-19, issue #2741 slice 3b, which
     `tools/tests/test_empire_tree_event_counts_are_saved.py` checks);
  3. the walk seed XORs the identifier plus one with the run's mask, never below 1;
  4. `Begin` draws the mask from the run's seed, and the save gathers and applies it;
  5. the fixtures README lists `Run_v8.json`.

WHAT IS NOT: that the floors a mask gives differ, or that a loaded walk carves the floor it was on. The automation
tests check those.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE = SOURCE / "Cataclysm" / "Save"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"


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


def test_run_v8_is_run_v7_with_a_mask_that_is_not_the_default() -> None:
    """The fixture rule: the new file is the old one at the new version, plus the new key, not at its default."""
    before = json.loads((FIXTURES / "Run_v7.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v8.json").read_text(encoding="utf-8"))

    assert before["SchemaVersion"] == 7
    assert after["SchemaVersion"] == 8
    assert "WalkSeedMask" not in before, "Run_v7.json holds WalkSeedMask, which version 7 did not have"
    assert isinstance(after.get("WalkSeedMask"), int) and after["WalkSeedMask"] != 0, (
        f"Run_v8.json's WalkSeedMask is {after.get('WalkSeedMask')!r}: 0 is the default and the migration's value, so "
        "a file holding it would pass whether the field was read or not")

    rest = {key: value for key, value in after.items() if key != "WalkSeedMask"}
    rest["SchemaVersion"] = 7
    assert rest == before, "Run_v8.json differs from Run_v7.json beyond its version and its mask"


def test_the_run_record_is_at_version_8_and_migrate_7_to_8_writes_a_mask_of_0() -> None:
    """Version 8 or later, and a step from 7 that writes 0 -- the mask that keeps every version 7 seed -- in the chain."""
    header = code(SAVE / "CataclysmSaveRecords.h")
    run_class = header.split("class CATACLYSM_API UCataclysmRunSave")[1]
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_class)
    assert version is not None and int(version.group(1)) >= 8, "the run record is below version 8"
    assert re.search(r"\bint32\s+WalkSeedMask\s*=\s*0\s*;", run_class), "the run record does not carry WalkSeedMask"

    migrations = run_migrations()
    step = body(migrations, "bool Migrate_7_to_8(", "the run record's migrations")
    assert re.search(r'SetNumberField\(\s*TEXT\("WalkSeedMask"\)\s*,\s*0\s*\)', step), (
        "Migrate_7_to_8 does not write a WalkSeedMask of 0, so a run saved at version 7 would walk other floors")

    chain = body(migrations, "const FCataclysmSaveMigrationStep RunSteps[] =", "the run record's chain")
    steps = re.findall(r"\{\s*(\d+)\s*,\s*TEXT\(\"(Migrate_\d+_to_\d+)\"\)", chain)
    assert ("7", "Migrate_7_to_8") in steps, f"the run record's chain has no step from 7: {steps}"


def test_the_walk_seed_xors_the_identifier_plus_one_with_the_runs_mask() -> None:
    """`max(1, (DungeonId + 1) ^ RunMask)`: a mask of 0 is the seed before B10, and never below 1."""
    seed = body(code(EMPIRE / "CataclysmSurge.cpp"), "int32 FCataclysmDungeon::WalkSeed(int32 RunMask) const",
                "CataclysmSurge.cpp")
    assert re.search(r"FMath::Max\(\s*1\s*,\s*\(\s*DungeonId\s*\+\s*1\s*\)\s*\^\s*RunMask\s*\)", seed), (
        "FCataclysmDungeon::WalkSeed is no longer max(1, (DungeonId + 1) ^ RunMask)")


def test_begin_draws_the_mask_and_the_save_carries_it() -> None:
    """Drawn from the run's seed by `Begin`, never below 0; gathered into the record and applied back to the run."""
    begin = code(EMPIRE / "CataclysmEmpireRun.cpp")
    assert re.search(r"WalkSeedMask\s*=\s*UCataclysmRoster::MixedSeed\(\s*InSeed\s*,\s*WalkSeedSalt\s*\)\s*&\s*0x7FFFFFFF",
                     begin), "UCataclysmEmpireRun no longer draws WalkSeedMask from the run's seed"
    assert re.search(r"Record\.WalkSeedMask\s*=\s*Run\.WalkSeedMask\s*;", code(SAVE / "CataclysmSaveGather.cpp")), (
        "the save no longer gathers the run's WalkSeedMask, so a reloaded run's dungeons would carve other floors")
    assert re.search(r"Run\.WalkSeedMask\s*=\s*Record\.WalkSeedMask\s*;", code(SAVE / "CataclysmSaveApply.cpp")), (
        "the save no longer applies the record's WalkSeedMask to the run")


def test_the_fixtures_readme_lists_run_v8() -> None:
    readme = (FIXTURES / "README.md").read_text(encoding="utf-8")
    assert "| `Run_v8.json` |" in readme, "game/Tests/SaveFixtures/README.md does not list Run_v8.json"
