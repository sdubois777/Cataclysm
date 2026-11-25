"""A thrall loaded from a save keeps the skill that took it and that skill's level factor.

WHY THIS EXISTS. Issue #2840, ruled by the coordinating session on 2026-11-22. `UCataclysmSummonSkill::Possess` sets
`TakenBySkillRow` and `TakenLevelFactorOnCreatures` on a creature it takes; neither was saved, so a loaded thrall's blows
credited no skill, lost the level factor, and its Fervour reserve never fell when the skill levelled. The run record's
step 16 to 17 gives a thrall in an older file Subjugate's row and a factor of 1 (Q1), WHICH RESTS ON SUBJUGATE BEING THE
ONE ROW THAT TAKES A CREATURE: a second such row would make every older thrall Subjugate's whether it was or not.
Continuous integration compiles no C++, so the automation tests in `Cataclysm.ThrallSave.` never run on a pull request;
these checks read the data and the source.

WHAT IS CHECKED:

  1. exactly one row of `WeaponSkills.csv` takes a creature ("Possess=1" in its shape parameters), and it is the row
     the step and the fixtures' helper write;
  2. the saved creature declares both fields, the save writes both for a thrall and the load puts both back;
  3. the step 16 to 17 writes both on every creature, Subjugate's row only where the player commanded it, and is in the
     chain; the run record is at 17 or later;
  4. the committed `Run_v17.json` is `Run_v16.json` with its version and the two fields on each creature, its thrall's
     not the defaults; and every chain test adds the step's write.
"""

from __future__ import annotations

import csv
import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SAVE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save"
TESTS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"

#: The one row that takes a creature: Subjugate, the Demonic Staff's ultimate.
SUBJUGATE = "Demonic_Staff_Ultimate"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return flat(text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


# --------------------------------------------------------------------------
# 1. Exactly one row takes a creature
# --------------------------------------------------------------------------

def test_exactly_one_skill_row_takes_a_creature() -> None:
    with (DATA / "WeaponSkills.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    taking = [row["Name"] for row in rows
              if re.search(r"(?:^|;)\s*Possess\s*=\s*1(?:\.0*)?\s*(?:;|$)", row.get("ShapeParams") or "")]
    assert taking == [SUBJUGATE], (
        f"the rows that take a creature are {taking}. The run record's step 16 to 17 gives every thrall in an older "
        f"file {SUBJUGATE}'s row (issue #2840, Q1), which is true only while it is the one row that takes; a second "
        "needs a ruling on what its older thralls get")
    records = code(SAVE / "CataclysmSaveRecords.cpp")
    assert f'SubjugateRow = TEXT("{SUBJUGATE}");' in flat(records), "the step does not name the row that takes"
    assert f'TEXT("{SUBJUGATE}")' in code(TESTS / "CataclysmSaveFixtures.h"), "the fixtures' helper names another row"


# --------------------------------------------------------------------------
# 2. The record carries both fields both ways
# --------------------------------------------------------------------------

def test_the_saved_creature_carries_both_fields_both_ways() -> None:
    header = flat(code(SAVE / "CataclysmSaveRecords.h"))
    creature = header[header.index("struct CATACLYSM_API FCataclysmSavedCreature"):]
    creature = creature[:creature.index("};")]
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\) FName TakenBySkillRow;", creature)
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\) float TakenLevelFactorOnCreatures = 1\.0f;", creature)
    gather = flat(code(SAVE / "CataclysmSaveGather.cpp"))
    commanded = gather[gather.index("Record.bCommandedByThePlayer = true;"):]
    commanded = commanded[:commanded.index("}")]
    assert "Record.TakenBySkillRow = Creature->TakenBySkillRow;" in commanded
    assert "Record.TakenLevelFactorOnCreatures = Creature->TakenLevelFactorOnCreatures;" in commanded
    thrall = body(code(SAVE / "CataclysmSaveApply.cpp"), "bool FCataclysmSaveApply::ThrallInto(")
    assert "Creature.TakenBySkillRow = Saved.TakenBySkillRow;" in thrall
    assert "Creature.TakenLevelFactorOnCreatures = Saved.TakenLevelFactorOnCreatures;" in thrall


# --------------------------------------------------------------------------
# 3. The step 16 to 17
# --------------------------------------------------------------------------

def test_the_step_gives_a_thrall_subjugate_at_level_one_and_every_other_creature_nothing() -> None:
    # THE RUN RECORD'S OWN STEPS: the character record has a `Migrate_16_to_17` too since issue #837 slice 1 (its
    # remembered defaults), and a search of the whole file would find that one first.
    records = body(code(SAVE / "CataclysmSaveRecords.cpp"), "namespace CataclysmRunSaveMigration")
    step = body(records, "bool Migrate_16_to_17(")
    assert 'TryGetBoolField(TEXT("bCommandedByThePlayer"), bThrall);' in step
    assert ('SetStringField(TEXT("TakenBySkillRow"), bThrall ? FString(SubjugateRow) : FString(TEXT("None")));'
            in step)
    assert 'SetNumberField(TEXT("TakenLevelFactorOnCreatures"), 1.0);' in step
    assert '{ 16, TEXT("Migrate_16_to_17"), &Migrate_16_to_17 },' in flat(records)
    header = flat(code(SAVE / "CataclysmSaveRecords.h"))
    run_record = header[header.index("class CATACLYSM_API UCataclysmRunSave "):]
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_record)
    assert version and int(version.group(1)) >= 17, "the run record does not declare version 17 or later"


# --------------------------------------------------------------------------
# 4. The committed file and the chain tests
# --------------------------------------------------------------------------

def test_the_committed_run_file_is_the_previous_one_with_each_creatures_taking_skill() -> None:
    before = json.loads((FIXTURES / "Run_v16.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v17.json").read_text(encoding="utf-8"))
    assert after.pop("SchemaVersion") == before.pop("SchemaVersion") + 1 == 17
    taken = []
    for creature in after["Floor"]["Creatures"]:
        taken.append((creature.pop("TakenBySkillRow"), creature.pop("TakenLevelFactorOnCreatures"),
                      creature["bCommandedByThePlayer"]))
    assert after == before, "Run_v17.json differs from Run_v16.json beyond its version and the two fields"
    thralls = [(row, factor) for row, factor, commanded in taken if commanded]
    others = [(row, factor) for row, factor, commanded in taken if not commanded]
    assert thralls == [(SUBJUGATE, 1.5)], f"Run_v17.json's thrall states {thralls}, not a row and a non-default factor"
    assert others and all(pair == ("None", 1) for pair in others), f"its other creatures state {others}"
    assert "| `Run_v17.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")


def test_every_chain_test_adds_the_new_steps_write() -> None:
    missing = [path.name for path in sorted(TESTS.glob("*.cpp"))
               if code(path).count("AddTheRunsVersion16Defaults(*Expected)")
               != code(path).count("AddTheRunsVersion17Defaults(*Expected)")]
    assert not missing, f"these run an older run file through the chain without the step 16 to 17's write: {missing}"
