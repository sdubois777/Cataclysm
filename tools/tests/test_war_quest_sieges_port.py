"""War's Quest Sieges in the model and in the game: issue #51 slice B4, the War quest.

THE RULES, the owner's of 2026-10-16 and 2026-10-17 and the plan's reading of section
XI accepted on 2026-11-24, with W1 to W3 ruled by the coordinating session on
2026-11-25:

1. War's Quest dungeons are Sieges (`test_dungeon_subtype_port.py` compares the pair).
2. At most two War Quest Sieges are made on one day, waves and falls together.
3. One drawn onto a city holding a Siege re-draws its city among those holding none.
4. A Quest Siege bites a quarter of an ordinary Siege, flat share and growth alike.
5. A Siege's growth counts the days on the city it stands on now.
6. A city's fall to War (the felling hit sent by War) makes a War Quest Siege on a
   neighbour holding none.

The model, `sim/cataclysm_sim/`, is the original. Its behaviour is checked in
`sim/tests/test_a_war_quest_siege_is_capped_and_bites_a_quarter.py` and
`sim/tests/test_a_fall_to_war_starts_the_next_siege.py`; the game's in the Unreal
group `Cataclysm.WarQuestSieges.`. This file reads both sources and holds the two to
the same numbers and the same shapes. Each check has a control or names what it
cannot see.
"""
from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
SURGE_H = EMPIRE / "CataclysmSurge.h"
SURGE_CPP = EMPIRE / "CataclysmSurge.cpp"
RUN_CPP = EMPIRE / "CataclysmEmpireRun.cpp"
RECORDS_H = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
TESTS = REPO_ROOT / "game" / "Source"
ENGINE = REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    """The text of one C++ function, from its signature to the next line holding only `}`."""
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end]


def model():
    import sys
    sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def test_the_two_numbers_are_the_model_s() -> None:
    """Rules 2 and 4: the game's constants are the model's."""
    header = read(SURGE_H)
    per_day = re.search(r"static constexpr int32 QuestSiegesPerDay = (\d+);", header)
    share = re.search(r"static constexpr float QuestSiegeBiteShare = ([0-9.]+)f;", header)
    assert per_day and share, "the two constants are gone from CataclysmSurge.h"
    cfg = model()
    assert int(per_day.group(1)) == cfg.quest_sieges_per_day_max == 2
    assert float(share.group(1)) == cfg.quest_siege_bite_scale == 0.25
    assert cfg.war_fall_makes_a_quest_siege is True, "the model's fall rule is switched off"


def test_the_wave_caps_by_the_day_and_re_draws_in_both() -> None:
    """Rules 2 and 3 in the two wave loops. The game counts what was made today before
    the wave (`QuestSiegesMadeOn(Today)`), makes a further roll Basic, and re-draws a
    besieged city with `PickCityWithoutASiege`; the model does the same in
    `trigger_surge`. CONTROL: the re-draw takes its own stream, so `Stream` is not
    named inside it."""
    wave = body(read(SURGE_CPP), "TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(")
    assert "int32 QuestSiegesToday = QuestSiegesAlreadyToday;" in wave
    assert "if (QuestSiegesToday >= QuestSiegesPerDay)" in wave
    assert "Kind = ECataclysmDungeonType::Basic;" in wave
    redraw = wave[wave.index("if (!bSiegeAllowed)"):wave.index("bSiegeAllowed = true;")]
    assert "PickCityWithoutASiege(Map, Draws, Standing, Sieges)" in redraw
    assert "WarSiegeSeedFor(" in redraw and "Stream" not in redraw.replace("FRandomStream", ""), (
        "the re-draw no longer takes a stream of its own")
    assert "QuestSiegesMadeOn(Today)" in body(read(RUN_CPP), "void UCataclysmEmpireRun::FireSurge(")

    engine = read(ENGINE)
    surge = engine[engine.index("    def trigger_surge("):engine.index("    def _rift_spawns(")]
    assert "quest_sieges: dict[str, int] = self.quest_sieges_made_today()" in surge
    assert ">= cfg.quest_sieges_per_day_max" in surge


def test_growth_counts_from_arrival_and_the_quest_share_bites_in_both() -> None:
    """Rules 4 and 5. The game's daily share and its forecast read `DaysOnItsCity` and
    the share; every move sets `ArrivedDay` -- the timer's and the fall's, two -- as
    the model's two moves set `arrived_day`. CONTROL: the growth still multiplies the
    days stood, so the share is not standing in for the growth."""
    run = read(RUN_CPP)
    siege = body(run, "void UCataclysmEmpireRun::ApplySiegeDamage(")
    assert "Dungeon.DaysOnItsCity(Clock->Day)" in siege
    assert "Dungeon.SiegeBiteShare()" in siege
    assert siege.count("* Host.Share *") == 2, "the share must bite the defence and the population"
    assert "SiegeDamageGrowthPerDay * Host.DaysStood" in siege
    assert "Dungeon.SpawnedDay" not in siege, "the share counts from the spawn day again"
    assert "Dungeon.DaysOnItsCity(Clock->Day + Ahead)" in run and "* Dungeon.SiegeBiteShare() * Taken" in run, (
        "the forecast no longer bites as the share does")

    moves = re.findall(r"Dungeon->CityId = (\w+);\r?\n\s*Dungeon->ArrivedDay = OutReport\.Day;", run)
    assert sorted(moves) == ["MovingTo", "Refuge"], moves
    assert len(re.findall(r"Dungeon->CityId = ", run)) == 2, "a third move of a dungeon's city was added"

    engine = read(ENGINE)
    assert len(re.findall(r"d\.city_id = \w+\.cid\r?\n\s*d\.arrived_day = self\.day", engine)) == 2


def test_every_felling_hit_says_who_hit_in_both() -> None:
    """Rule 6's W1. Each of the game's three `CityFell` calls is preceded, inside its
    hit, by `HitBy = ...`; `CityFell` reads and clears it. The model's three
    `self._fall(city)` calls in the hits are preceded by `self.hit_by = ...`.
    CONTROL: the count of `CityFell` calls is three, so a fourth fall without a
    sender fails here."""
    run = read(RUN_CPP)
    calls = [m.start() for m in re.finditer(r"^\s*CityFell\(\w+, OutReport\);", run, flags=re.M)]
    assert len(calls) == 3, len(calls)
    for at in calls:
        before = run[max(0, at - 600):at]
        assert re.search(r"HitBy = (Dungeon->Cataclysm|Host\.Sender);", before), run[at - 200:at]
    fell = body(run, "void UCataclysmEmpireRun::CityFell(")
    assert "const ECataclysmType FelledBy = HitBy;" in fell and "HitBy = ECataclysmType::None;" in fell
    assert "Felled->FellTo = FelledBy;" in fell

    engine = read(ENGINE)
    hits = [m.start() for m in re.finditer(r"self\._fall\(city\)", engine)]
    assert len(hits) == 3, len(hits)
    for at in hits:
        assert re.search(r"self\.hit_by = (d\.source|source)\r?\n", engine[max(0, at - 300):at])
    fall = engine[engine.index("    def _fall("):engine.index("    def quest_sieges_made_today(")]
    assert 'felled_by, self.hit_by = self.hit_by, ""' in fall and "city.fell_to = felled_by" in fall


def test_a_fall_to_war_makes_the_next_siege_in_the_same_place_in_both() -> None:
    """Rule 6. Both halves make it after the Fallen City and before the fall's surge,
    only when War felled the city."""
    fell = body(read(RUN_CPP), "void UCataclysmEmpireRun::CityFell(")
    made = fell.index("MakeWarQuestSiegeOnAFall(CityId, OutReport);")
    assert fell.index("AddFallenCityDungeon(CityId, DungeonsAbsorbed, OutReport);") < made
    assert made < fell.index("FireSurge(OutReport.Day, /* bFromCityFall */ true, OutReport);")
    assert "if (FelledBy == ECataclysmType::War)" in fell[:made]

    engine = read(ENGINE)
    fall = engine[engine.index("    def _fall("):engine.index("    def quest_sieges_made_today(")]
    at = fall.index("self._war_siege_on_a_fall(city)")
    assert fall.index("self._make_dungeon(DungeonType.FALLEN_CITY, city)") < at < fall.index(
        "self.trigger_surge(from_city_fall=True)")
    assert 'felled_by == "War"' in fall[:at]

    maker = body(read(RUN_CPP), "void UCataclysmEmpireRun::MakeWarQuestSiegeOnAFall(")
    assert "QuestSiegesMadeOn(Today) >= UCataclysmSurgeScheduler::QuestSiegesPerDay" in maker
    assert "ECataclysmDungeonType::Quest, ECataclysmType::War" in maker
    assert "PickWarSiegeCity(" in maker and "Stream" not in maker.replace("FRandomStream", ""), (
        "the fall's Siege no longer draws from a stream of its own")


def test_the_run_record_step_writes_the_two_fields_and_every_chain_test_adds_them() -> None:
    """The save: version 21, `Run_v21.json` is `Run_v20.json` with only the version and
    the two fields, and every test that runs an older run file through the whole chain
    adds what the step writes. CONTROL: the fixture's fallen city carries a figure, not
    the default, so a load that ignored the field could be seen."""
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", read(RECORDS_H).split(
        "class CATACLYSM_API UCataclysmRunSave")[1])
    # 21 OR LATER: issue #51 slice B5 took the record to 22 on top of this step.
    assert version and int(version.group(1)) >= 21, version and version.group(1)

    before = json.loads(read(FIXTURES / "Run_v20.json"))
    after = json.loads(read(FIXTURES / "Run_v21.json"))
    assert after["SchemaVersion"] == 21
    for d in after["Dungeons"]:
        assert d.pop("ArrivedDay") == -1
    fell_to = [c.pop("FellTo") for c in after["Cities"]]
    assert fell_to == ["None" if not c["bFallen"] else "Demonic" for c in before["Cities"]], fell_to
    assert "Demonic" in fell_to, "CONTROL: the fixture's fallen city carries a figure"
    after["SchemaVersion"] = 20
    assert after == before, "Run_v21.json differs from Run_v20.json beyond its version and the two fields"
    assert "| `Run_v21.json` |" in read(FIXTURES / "README.md")

    chain20 = chain21 = 0
    for path in TESTS.rglob("*.cpp"):
        # THE STEP'S OWN MIGRATION TEST runs `Run_v20.json` through the one step, not the chain, so it adds 21 alone.
        if path.name == "CataclysmWarQuestSiegesTests.cpp":
            continue
        text = read(path)
        twenty = len(re.findall(r"AddTheRunsVersion20Defaults\(", text))
        twenty_one = len(re.findall(r"AddTheRunsVersion21Defaults\(", text))
        assert twenty == twenty_one, f"{path.name}: {twenty} chain tests reach 20 and {twenty_one} reach 21"
        assert "Run_v20.json" not in text or re.search(r"Run_v2[1-9]\.json", text), (
            f"{path.name} reads Run_v20.json as the committed file")
        chain20 += twenty
        chain21 += twenty_one
    assert chain21 == chain20 >= 15, (chain20, chain21)
