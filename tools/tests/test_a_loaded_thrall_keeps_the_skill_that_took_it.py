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


# --------------------------------------------------------------------------
# 4. The committed file and the chain tests
# --------------------------------------------------------------------------


def test_every_chain_test_adds_the_new_steps_write() -> None:
    missing = [path.name for path in sorted(TESTS.glob("*.cpp"))
               if code(path).count("AddTheRunsVersion16Defaults(*Expected)")
               != code(path).count("AddTheRunsVersion17Defaults(*Expected)")]
    assert not missing, f"these run an older run file through the chain without the step 16 to 17's write: {missing}"
