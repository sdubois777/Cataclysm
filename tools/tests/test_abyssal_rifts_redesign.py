"""Abyssal Rifts after the owner's redesign: several Boss rifts a floor, a hunter every 3 seconds, stronger with age.

WHY THIS EXISTS. Issue #2864, the owner's words of 2026-11-23: "maybe more than one should be able to spawn per floor,
and they should spawn an enemy every 3 seconds that hunts you on the floor from the moment you enter, so you can
basically follow the trail back to the source. I think this means the rifts should also be targetable and able to be
killed, but they should spawn as Bosses and have much more health. The enemies the spawn from them should slowly
increase in difficulty the longer the rift is alive." Continuous integration compiles no C++, so the automation tests in
`Cataclysm.DungeonModifierEffects.` and `Cataclysm.SaveLoad.` never run on a pull request; these checks read the source,
the data and the save fixtures.

WHAT IS CHECKED:

  1. the owner's figure, three seconds between hunters, is the constant the game reads, and more than one rift is
     placed on a floor;
  2. a rift is placed at the Boss rung, with nothing that keeps a blow off it, and its death is a success;
  3. a hunter is sent with the row's sight, paying nothing, raised by the rule, and as strong as its rift's age makes it
     through the two figures a save keeps;
  4. the floor's rule state saves each rift's health and seconds stood, and the load puts both back; the hunters are
     saved by their rift's place;
  5. the run record's step 19 to 20 replaces the one rift's state, is in the chain, and the record is at 20 or later;
     the committed `Run_v20.json` is `Run_v19.json` with only that changed; every chain test adds the step's write.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
DUNGEON = SOURCE / "Dungeon"
SAVE = SOURCE / "Save"
TESTS = SOURCE / "Tests"
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


def constant(header: str, name: str) -> str:
    found = re.search(rf"static constexpr \w+ {name} = ([^;]+);", header)
    assert found, f"no constant {name}"
    return found.group(1).strip()


# --------------------------------------------------------------------------
# 1. The owner's figure, and more than one rift
# --------------------------------------------------------------------------

def test_a_hunter_every_three_seconds_and_more_than_one_rift_a_floor() -> None:
    header = flat(code(DUNGEON / "CataclysmDungeonModifierEffects.h"))
    assert constant(header, "AbyssalRiftsSecondsBetweenHunters") == "3.0f", "the owner said every 3 seconds"
    assert int(constant(header, "AbyssalRiftsPerFloor")) >= 2, "the owner asked for more than one rift a floor"
    assert int(constant(header, "AbyssalRiftsPerLargeFloor")) >= int(constant(header, "AbyssalRiftsPerFloor"))
    effects = flat(code(DUNGEON / "CataclysmDungeonModifierEffects.cpp"))
    due = body(effects, "bool UCataclysmDungeonModifierEffects::AbyssalRiftsHunterIsDue(")
    assert "SecondsSinceLastSent >= AbyssalRiftsSecondsBetweenHunters" in due
    assert "OwnStanding < AbyssalRiftsMostHuntersPerRift" in due
    mode = code(DUNGEON / "CataclysmDungeonGameMode.cpp")
    place = body(mode, "void ACataclysmDungeonGameMode::PlaceTheRifts(")
    assert "Effects::AbyssalRiftsFor(Plan.Width * Plan.Height)" in place
    assert "EternalChorusCells(*CurrentFloor, Count, Effects::AbyssalRiftsCellsSalt)" in place


# --------------------------------------------------------------------------
# 2. A rift is a Boss the player can kill
# --------------------------------------------------------------------------

def test_a_rift_is_a_boss_that_a_blow_can_kill_and_its_death_is_a_success() -> None:
    header = flat(code(DUNGEON / "CataclysmDungeonModifierEffects.h"))
    assert constant(header, "AbyssalRiftsRiftRung") == "4", "the owner's 'spawn as Bosses' is the Boss rung, 4"
    assert float(constant(header, "AbyssalRiftsHealthMultiplier").rstrip("f")) > 1.0, "'much more health'"
    mode = code(DUNGEON / "CataclysmDungeonGameMode.cpp")
    assert ("static_assert(UCataclysmDungeonModifierEffects::AbyssalRiftsRiftRung == "
            "ACataclysmEnemyCharacter::FirstBossRarityStep") in flat(mode)
    place = body(mode, "void ACataclysmDungeonGameMode::PlaceTheRifts(")
    assert "Rift->SetRarityStep(Effects::AbyssalRiftsRiftRung);" in place
    assert "Rift->SetHealth(Health);" in place
    assert "bCannotBeHurt" not in place and "bShrouded" not in place, "nothing may keep a blow off a rift"
    health = body(mode, "float ACataclysmDungeonGameMode::AbyssalRiftStartingHealth(")
    assert "Effects::AbyssalRiftsRiftRung, Built)" in health
    assert "Built.Health * Effects::AbyssalRiftsHealthMultiplier" in health
    death = body(mode, "void ACataclysmDungeonGameMode::NoteDeathForAbyssalRifts(")
    assert "++AbyssalRiftSuccesses;" in death
    assert "CloseTheRiftAt(Index, false);" in death, "a killed rift sends no more"


# --------------------------------------------------------------------------
# 3. A hunter hunts, pays nothing, and is as strong as its rift's age makes it
# --------------------------------------------------------------------------

def test_a_hunter_hunts_pays_nothing_and_is_sent_as_strong_as_its_rifts_age() -> None:
    mode = code(DUNGEON / "CataclysmDungeonGameMode.cpp")
    step = body(mode, "void ACataclysmDungeonGameMode::StepAbyssalRifts(")
    assert "SpawnPlacedCreature(Placement, Effects::AbyssalRiftsHunterSightMultiplier, 0)" in step
    assert "Effects::AbyssalRiftsHunterMultiplierFor(One.SecondsAlive)" in step
    assert "Hunter->SetHealth(Hunter->GetStartingMaxHealth() * Stronger);" in step
    assert "Hunter->SetPlacedDamageMultiplier(Stronger);" in step
    assert "Hunter->bDiesUnpaid = true;" in step and "Hunter->bRaisedByARule = true;" in step
    assert "FloorEnemies.Add" not in step, "a hunter is not one of the floor's creatures"
    assert "One.bClosed" in step, "a killed rift must send no more"
    assert "ECataclysmDungeonCreature::CorruptedSentinel" in step, "a hunter that cannot move hunts nobody"
    effects = flat(code(DUNGEON / "CataclysmDungeonModifierEffects.cpp"))
    steps = body(effects, "int32 UCataclysmDungeonModifierEffects::AbyssalRiftsStrengthStepsFor(")
    assert "FMath::Clamp(Steps, 0, AbyssalRiftsMostStrengthSteps)" in steps, "the strength must stop at its cap"


# --------------------------------------------------------------------------
# 4. The save keeps each rift's health and age, and its hunters
# --------------------------------------------------------------------------

def test_the_save_keeps_each_rifts_health_and_age_and_its_hunters() -> None:
    header = flat(code(SAVE / "CataclysmSaveRecords.h"))
    state = header[header.index("struct CATACLYSM_API FCataclysmSavedFloorRuleState"):]
    state = state[:state.index("};")]
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\) TArray<float> AbyssalRiftHealthLeft;", state)
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\) TArray<float> AbyssalRiftSecondsAlive;", state)
    assert "AbyssalRiftState" not in state, "the one rift's state is replaced, not kept beside the new fields"
    mode = code(DUNGEON / "CataclysmDungeonGameMode.cpp")
    gather = body(mode, "FCataclysmSavedFloorRuleState ACataclysmDungeonGameMode::FloorRuleStateNow(")
    assert "Out.AbyssalRiftHealthLeft.Add(Left);" in gather
    assert "Out.AbyssalRiftSecondsAlive.Add(One.SecondsAlive);" in gather
    restore = body(mode, "void ACataclysmDungeonGameMode::RestoreFloorRuleState(")
    assert "CloseTheRiftAt(Index, true);" in restore
    assert "One.SecondsAlive = FMath::Max(0.0f, State.AbyssalRiftSecondsAlive[Index]);" in restore
    assert "SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Left);" in restore
    raised = body(mode, "bool ACataclysmDungeonGameMode::RaisedByOf(")
    assert "OutTag = FName(RaisedAbyssalRiftHunter);" in raised
    roles = body(mode, "void ACataclysmDungeonGameMode::RestoreCreatureRoles(")
    assert "AbyssalRifts[Saved.RaisedGroup].Sent.Add(&Creature);" in roles


# --------------------------------------------------------------------------
# 5. The step 19 to 20, the committed file and the chain tests
# --------------------------------------------------------------------------

def test_the_step_replaces_the_one_rifts_state_and_is_in_the_chain() -> None:
    records = code(SAVE / "CataclysmSaveRecords.cpp")
    step = body(records, "bool Migrate_19_to_20(")
    assert 'RemoveField(TEXT("AbyssalRiftState"));' in step
    assert 'SetArrayField(TEXT("AbyssalRiftHealthLeft"), Health);' in step
    assert 'SetArrayField(TEXT("AbyssalRiftSecondsAlive"), Seconds);' in step
    assert '{ 19, TEXT("Migrate_19_to_20"), &Migrate_19_to_20 },' in flat(records)
    header = flat(code(SAVE / "CataclysmSaveRecords.h"))
    run_record = header[header.index("class CATACLYSM_API UCataclysmRunSave "):]
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_record)
    assert version and int(version.group(1)) >= 20, "the run record does not declare version 20 or later"


def test_the_committed_run_file_is_the_previous_one_with_each_rifts_health_and_age() -> None:
    before = json.loads((FIXTURES / "Run_v19.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v20.json").read_text(encoding="utf-8"))
    assert after.pop("SchemaVersion") == before.pop("SchemaVersion") + 1 == 20
    rules_after = after["Floor"]["RuleState"]
    health = rules_after.pop("AbyssalRiftHealthLeft")
    seconds = rules_after.pop("AbyssalRiftSecondsAlive")
    before["Floor"]["RuleState"].pop("AbyssalRiftState")
    assert after == before, "Run_v20.json differs from Run_v19.json beyond its version and the rifts' two lists"
    assert len(health) == len(seconds) >= 2, "the file holds no two rifts"
    assert any(value > 0 for value in health) and any(value == 0 for value in health), (
        f"Run_v20.json's rifts hold {health}: a standing one and a killed one, not what the step writes")
    assert any(value > 0 for value in seconds), f"its rifts' seconds are {seconds}, what the step writes"
    assert "| `Run_v20.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")


def test_every_chain_test_adds_the_new_steps_write() -> None:
    # A FILE MAY CALL THE NEW ONE MORE OFTEN: the step's own test starts from `Run_v19.json`, which needs only it.
    missing = [path.name for path in sorted(TESTS.glob("*.cpp"))
               if code(path).count("AddTheRunsVersion20Defaults(*Expected)")
               < code(path).count("AddTheRunsVersion19Defaults(*Expected)")]
    assert not missing, f"these run an older run file through the chain without the step 19 to 20's write: {missing}"
