"""Death's Abomination lairs in the model and in the game: issue #51 slice B5, with the simulation slice A2.

THE RULES, the design's Dead Rising accepted by the owner on 2026-11-24 and L1 to L9 ruled by the coordinating session
on 2026-11-25:

1. A lair is a Quest dungeon Death sent (a Horde).
2. It counts the cities fallen when it appeared, the fall that made it included.
3. A fall while Death is active makes one, placed as a fall moves a Quest dungeon, after War's Siege and before the
   fall's surge, with no cap.
4. Its boss, the Cursed Abomination, is at the Boss rung, and its health, damage and gear are the step to the power of
   the count. The step is the model's `abomination_step`, sized by slice A2.

The model, `sim/cataclysm_sim/`, is the original; `sim/tests/test_an_abomination_lair.py` checks its behaviour and the
Unreal group `Cataclysm.AbominationLairs.` the game's. This file reads both sources and holds them to the same numbers
and shapes.
"""
from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
RECORDS_H = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
ENGINE = REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start)]


def model():
    import sys
    sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def test_the_step_is_the_model_s() -> None:
    step = re.search(r"static constexpr float AbominationStep = ([0-9.]+)f;", read(EMPIRE / "CataclysmSurge.h"))
    assert step, "AbominationStep is gone from CataclysmSurge.h"
    cfg = model()
    assert float(step.group(1)) == cfg.abomination_step
    assert cfg.death_lairs is True, "the model's lairs are switched off"


def test_a_lair_is_death_s_quest_dungeon_in_both() -> None:
    lair = body(read(EMPIRE / "CataclysmSurge.cpp"), "bool FCataclysmDungeon::IsALair() const")
    assert "Type == ECataclysmDungeonType::Quest" in lair and "Cataclysm == ECataclysmType::Death" in lair
    engine = read(ENGINE)
    is_lair = engine[engine.index("    def is_lair("):engine.index("    def abomination_multiplier(")]
    assert "d.dtype is DungeonType.QUEST" in is_lair and 'd.source == "Death"' in is_lair


def test_the_count_is_taken_where_each_half_makes_a_lair() -> None:
    """The game counts in `RollWave` and in `MakeLairOnAFall`; the model in `_make_dungeon`, which both its paths
    reach. CONTROL: each half counts the map's fallen cities, not something else."""
    surge = read(EMPIRE / "CataclysmSurge.cpp")
    wave = body(surge, "TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(")
    assert "Dungeon.CitiesFallenBefore = Map.FallenCityCount();" in wave
    run = read(EMPIRE / "CataclysmEmpireRun.cpp")
    assert "Made.CitiesFallenBefore = Map->FallenCityCount();" in body(run, "void UCataclysmEmpireRun::MakeLairOnAFall(")
    engine = read(ENGINE)
    make = engine[engine.index("    def _make_dungeon("):engine.index("    def _carries_the_corrupted_stalker(")]
    assert "d.cities_fallen_before = len(self.empire.fallen_cities())" in make


def test_a_fall_makes_the_lair_in_the_same_place_in_both() -> None:
    fell = body(read(EMPIRE / "CataclysmEmpireRun.cpp"), "void UCataclysmEmpireRun::CityFell(")
    war = fell.index("MakeWarQuestSiegeOnAFall(CityId, OutReport);")
    lair = fell.index("MakeLairOnAFall(CityId, OutReport);")
    assert war < lair < fell.index("FireSurge(OutReport.Day, /* bFromCityFall */ true, OutReport);")
    assert "if (ActiveCataclysms.Contains(ECataclysmType::Death))" in fell[war:lair]
    maker = body(read(EMPIRE / "CataclysmEmpireRun.cpp"), "void UCataclysmEmpireRun::MakeLairOnAFall(")
    assert "UCataclysmSurgeScheduler::PickRefuge(" in maker and "LairSeedFor(" in maker
    assert "Stream" not in maker.replace("FRandomStream", ""), "the lair no longer draws from a stream of its own"

    engine = read(ENGINE)
    fall = engine[engine.index("    def _fall("):engine.index("    def _lair_on_a_fall(")]
    at = fall.index("self._lair_on_a_fall(city)")
    assert fall.index("self._war_siege_on_a_fall(city)") < at < fall.index("self.trigger_surge(from_city_fall=True)")
    assert 'cfg.death_lairs and "Death" in self.active_types' in fall[:at]
    assert "self._quest_refuge(_CARRIES_NO_SIEGE, fallen)" in engine


def test_the_boss_is_the_cursed_abomination_at_the_boss_rung() -> None:
    """The rung, the making on the last wave, the finding again on a load (not scaled twice), and the gear."""
    mode = read(GAME_MODE)
    rung = body(mode, "int32 ACataclysmDungeonGameMode::PopulationRungFor(")
    assert "IsALair() && IsOnTheLastFloor()" in rung and "ACataclysmEnemyCharacter::FirstBossRarityStep" in rung
    wave = body(mode, "int32 ACataclysmDungeonGameMode::ContinueTheWaveArriving()")
    assert "MakeTheCursedAbomination(Enemy, /*bScale=*/true);" in wave
    assert mode.count("MakeTheCursedAbomination(&Creature, /*bScale=*/false);") == 1, "a load does not find it again"
    make = body(mode, "void ACataclysmDungeonGameMode::MakeTheCursedAbomination(")
    for mark in ("Boss->SetHealth(Boss->GetStartingMaxHealth() * Stronger);", "Boss->SetAbominationDamageMultiplier(Stronger);",
                 "Boss->AbominationGearMore = Stronger;", "Dungeon->AbominationMultiplier()"):
        assert mark in make, mark
    drops = read(SOURCE / "Cataclysm" / "Items" / "CataclysmDroppedItem.cpp")
    assert "ExpectedGearDrops(Drops, EnemyRarity, LootQuantity) * FMath::Max(0.0f, GearMore)" in drops
    enemy = read(SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp")
    assert "/*GearMore=*/AbominationGearMore" in enemy


def test_the_run_record_step_writes_the_count_and_every_chain_test_adds_it() -> None:
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);",
                        read(RECORDS_H).split("class CATACLYSM_API UCataclysmRunSave")[1])
    assert version and int(version.group(1)) >= 22, version and version.group(1)
    before = json.loads(read(FIXTURES / "Run_v21.json"))
    after = json.loads(read(FIXTURES / "Run_v22.json"))
    assert after["SchemaVersion"] == 22
    for d in after["Dungeons"]:
        assert d.pop("CitiesFallenBefore") == 0
    after["SchemaVersion"] = 21
    assert after == before, "Run_v22.json differs from Run_v21.json beyond its version and the one field"
    assert "| `Run_v22.json` |" in read(FIXTURES / "README.md")
    chain21 = chain22 = 0
    for path in SOURCE.rglob("*.cpp"):
        # THE STEP'S OWN MIGRATION TEST runs `Run_v21.json` through the one step, not the chain.
        if path.name == "CataclysmAbominationLairsTests.cpp":
            continue
        text = read(path)
        twenty_one = len(re.findall(r"AddTheRunsVersion21Defaults\(", text))
        twenty_two = len(re.findall(r"AddTheRunsVersion22Defaults\(", text))
        assert twenty_one == twenty_two, f"{path.name}: {twenty_one} reach 21 and {twenty_two} reach 22"
        chain21 += twenty_one
        chain22 += twenty_two
    assert chain22 == chain21 >= 16, (chain21, chain22)
