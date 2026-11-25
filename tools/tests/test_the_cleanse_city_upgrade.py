"""The cleanse, the last city upgrade, built on difficulty tier 3 and above and once a run.

WHY THIS EXISTS. Issue #2838, ruled by the coordinating session on 2026-11-22. The row reads "Cleanse every player city
of half of the dungeons on them excluding Quest and Fallen City dungeons. The cities lose 50% of their remaining
defenses and population. Can only purchase once, and will only be available on T3 and above." It was refused as not
built until then, because "T3" was read as an upgrade tier nothing reached. Continuous integration compiles no C++, so
the automation tests in `CataclysmCityCleanseTests.cpp` and `CataclysmCityCleanseSaveTests.cpp` never run on a pull
request; these checks read the source for each ruling, where a slip would compile and change the game.

WHAT IS CHECKED:

  1. the cleanse is built, and its two refusals say the ruled words;
  2. its gates are the run's, checked before anything about the city, and the run is spent at purchase;
  3. it removes half of each city's counted dungeons, never a Quest, Fallen City or Cataclysm dungeon, on every city
     that has not fallen, and takes half of what each holds;
  4. the run record's step 15 to 16 writes the flag false, the record declares it, the save carries it both ways, the
     committed `Run_v16.json` is `Run_v15.json` with it true, and every chain test adds the step's write.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
SAVE = SOURCE / "Cataclysm" / "Save"
TESTS = SOURCE / "Cataclysm" / "Tests"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"


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


def case(text: str, label: str) -> str:
    """What follows `case <label>:` up to the next `case` or `default`."""
    start = text.find(f"case {label}:")
    assert start != -1, f"no case {label}"
    following = re.search(r"\bcase\b|\bdefault\b", text[start + 5:])
    return text[start:start + 5 + (following.start() if following else len(text))]


# --------------------------------------------------------------------------
# 1. Built, with the ruled words
# --------------------------------------------------------------------------

def test_the_cleanse_is_built_and_its_refusals_say_the_ruled_words() -> None:
    rules = code(EMPIRE / "CataclysmCityUpgrade.cpp")
    built = flat(body(rules, "bool UCataclysmCityUpgradeRules::IsBuilt("))
    returns_true = built[:built.index("return true;")]
    assert "ECataclysmCityUpgradeEffect::CleanseEveryCity" in returns_true, (
        "IsBuilt does not return true for the cleanse, so a purchase refuses it as not built")
    header = flat(code(EMPIRE / "CataclysmCityUpgrade.h"))
    for constant, value in (("CleanseLowestDifficultyTier", "3"), ("CleanseDungeonShare", "0.5f"),
                            ("CleanseLossShare", "0.5f")):
        assert re.search(rf"static constexpr \w+ {constant} = {re.escape(value)};", header), (
            f"UCataclysmCityUpgradeRules::{constant} is not {value}, the ruled figure")
    text = body(rules, "FString UCataclysmCityUpgradeRules::ResultText(")
    below = case(text, "ECataclysmCityUpgradeResult::BelowItsDifficultyTier")
    assert 'TEXT("Available from difficulty tier %d.")' in below and "CleanseLowestDifficultyTier" in below
    assert 'return TEXT("Already bought this run.");' in case(text, "ECataclysmCityUpgradeResult::AlreadyBoughtThisRun")


# --------------------------------------------------------------------------
# 2. The run's gates, and spent at purchase
# --------------------------------------------------------------------------

def test_the_gates_are_the_runs_and_come_before_the_city() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    would = body(run, "ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldBuyCityUpgrade(")
    gate = would.index("Upgrade.Effect == ECataclysmCityUpgradeEffect::CleanseEveryCity")
    assert would.index("DifficultyTier < UCataclysmCityUpgradeRules::CleanseLowestDifficultyTier", gate) < would.index(
        "return ECataclysmCityUpgradeResult::BelowItsDifficultyTier;", gate)
    assert would.index("if (bCleanseBoughtThisRun)", gate) < would.index(
        "return ECataclysmCityUpgradeResult::AlreadyBoughtThisRun;", gate)
    assert gate < would.index("Map->Find(CityId)"), "the cleanse's gates come after a city is looked up"
    buy = body(run, "ECataclysmCityUpgradeResult UCataclysmEmpireRun::BuyCityUpgrade(")
    spent = buy.index("bCleanseBoughtThisRun = true;")
    assert spent < buy.index("Map->StartBuild(CityId, Bought);"), "the run is not spent before the build starts"
    assert "bCleanseBoughtThisRun = false;" in body(run, "void UCataclysmEmpireRun::BeginForCharacter("), (
        "a new run does not give the cleanse back")


# --------------------------------------------------------------------------
# 3. What it does
# --------------------------------------------------------------------------

def test_it_halves_the_counted_dungeons_and_what_each_city_holds() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    complete = body(run, "void UCataclysmEmpireRun::CompleteCityUpgrade(")
    assert "Bought.Effect == ECataclysmCityUpgradeEffect::CleanseEveryCity" in complete and "CleanseEveryCity();" in complete
    cleanse = body(run, "void UCataclysmEmpireRun::CleanseEveryCity(")
    assert "if (!City.bFallen)" in cleanse, "the cleanse does not leave a fallen city out"
    for kind in ("Quest", "FallenCity", "Cataclysm"):
        assert f"Dungeon.Type != ECataclysmDungeonType::{kind}" in cleanse, f"a {kind} dungeon is counted"
    assert "Clock->DaysUntilResolveFor(Left) < Clock->DaysUntilResolveFor(Right)" in cleanse, "not soonest first"
    assert "FMath::RoundToInt(Counted.Num() * UCataclysmCityUpgradeRules::CleanseDungeonShare)" in cleanse
    assert "RemoveDungeon(Counted[Index]);" in cleanse and "ClearDungeon" not in cleanse, (
        "the cleanse clears dungeons, which pays for them, where it should remove them")
    assert "City->Defence = Map->DefenceOf(*City) * (1.0f - UCataclysmCityUpgradeRules::CleanseLossShare);" in cleanse
    assert "City->Population = Map->PopulationOf(*City) * (1.0f - UCataclysmCityUpgradeRules::CleanseLossShare);" in cleanse
    assert "NotePopulationAgainstHalf();" in cleanse


# --------------------------------------------------------------------------
# 4. The run record, version 16
# --------------------------------------------------------------------------

def test_the_run_record_step_and_field() -> None:
    records = code(SAVE / "CataclysmSaveRecords.cpp")
    # THE RUN RECORD'S OWN STEPS: the character record has a `Migrate_15_to_16` too since issue #2849, and it comes
    # first in the file.
    run = body(records, "namespace CataclysmRunSaveMigration")
    step = body(run, "bool Migrate_15_to_16(")
    assert 'Record->SetBoolField(TEXT("bCleanseBoughtThisRun"), false);' in step
    assert '{ 15, TEXT("Migrate_15_to_16"), &Migrate_15_to_16 },' in run
    header = flat(code(SAVE / "CataclysmSaveRecords.h"))
    run_record = header[header.index("class CATACLYSM_API UCataclysmRunSave "):]
    # AT LEAST 16, read rather than pinned: a later step moves it on (17 for issue #2840), as every other check of the
    # run record's version reads it.
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_record)
    assert version and int(version.group(1)) >= 16, "the run record does not declare version 16 or later"
    assert re.search(r"UPROPERTY\(SaveGame[^)]*\)\s*bool bCleanseBoughtThisRun = false;", run_record)
    assert "Record.bCleanseBoughtThisRun = Run.bCleanseBoughtThisRun;" in code(SAVE / "CataclysmSaveGather.cpp")
    assert "Run.bCleanseBoughtThisRun = Record.bCleanseBoughtThisRun;" in code(SAVE / "CataclysmSaveApply.cpp")


def test_the_committed_run_file_is_the_previous_one_with_the_cleanse_bought() -> None:
    before = json.loads((FIXTURES / "Run_v15.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v16.json").read_text(encoding="utf-8"))
    assert after.pop("SchemaVersion") == before.pop("SchemaVersion") + 1 == 16
    assert after.pop("bCleanseBoughtThisRun") is True, "Run_v16.json states the default, so a read proves nothing"
    assert after == before, "Run_v16.json differs from Run_v15.json beyond its version and the flag"
    assert "| `Run_v16.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")


def test_every_chain_test_adds_the_new_steps_write() -> None:
    missing = [path.name for path in sorted(TESTS.glob("*.cpp"))
               if code(path).count("AddTheRunsVersion15Defaults(*Expected)")
               != code(path).count("AddTheRunsVersion16Defaults(*Expected)")]
    assert not missing, f"these run an older run file through the chain without the step 15 to 16's write: {missing}"
    assert 'Expected.SetBoolField(TEXT("bCleanseBoughtThisRun"), false);' in code(TESTS / "CataclysmSaveFixtures.h")
