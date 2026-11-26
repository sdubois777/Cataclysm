"""Demonic's Infernal Rifts are the same rules in the model and in the game (issue #51, slice B2).

THE OWNER'S RULINGS OF 2026-11-24 on issue #51, each verbatim: "Rift = Quest dungeon (Recommended)", "Anywhere but the
Pillar (Recommended)", "Every death, no gap (Recommended)", "let's make them 4 floors?", "Keep the short timer for
rifts", "Rifts always have 8 dungeon modifiers. Keep it spicy", "No limit" and "(b) 8 modifiers only (Recommended)".

THE MODEL: `TuningConfig.demonic_rifts` and the five `rift_*` settings, `Simulation.is_rift`, `trigger_surge`,
`_rift_spawns`, `_surge_for_a_rift_death`. THE GAME: `FCataclysmDungeon::IsARift`, `UCataclysmSurgeScheduler::
RiftFloors`, `RiftSendEveryDays`, `RiftSendsOn`, `PickRiftCity`, `PickRiftTarget`, `MakeDungeon`, `RollWave`,
`UCataclysmDungeonModifierRules::RiftModifierCount`, `UCataclysmEmpireRun::GiveModifiersFrom`, `SendFromRifts`,
`SurgeForADeathInARift`, `AdvanceDay`, and `ACataclysmDungeonGameMode::EndTheDungeonForADeath`.

THE C++ IS READ AS CODE, COMMENTS TAKEN OUT (`code_only`), because the comments beside these rules name every
identifier the checks look for.

BOTH HALVES HAVE THE RIFTS ON. The model's `demonic_rifts` ships on in the same change as the game's Rifts, ruled
by the coordinating session on 2026-11-24 "so the model and the game agree"; until that ruling the model's shipped off
and this file held the gap. `test_both_halves_ship_the_rifts_on` holds it closed.
"""
from __future__ import annotations

import inspect
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
SURGE_H = EMPIRE / "CataclysmSurge.h"
SURGE_CPP = EMPIRE / "CataclysmSurge.cpp"
MODIFIER_H = EMPIRE / "CataclysmDungeonModifier.h"
RUN_CPP = EMPIRE / "CataclysmEmpireRun.cpp"
GAME_MODE_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def code_only(text: str) -> str:
    """C++ with its comments taken out and its whitespace flattened (`test_surge_port.code_only`)."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return " ".join(text.split())


def body_of(source: str, signature: str) -> str:
    """One C++ function, from its signature to the first closing brace in the first column, as code only."""
    parts = source.split(signature, 1)
    if len(parts) != 2:
        pytest.fail(f"could not find {signature} -- has it been renamed?")
    return code_only(parts[1].split("\n}", 1)[0])


def constant(text: str, name: str, where: str) -> int:
    match = re.search(rf"static\s+constexpr\s+int32\s+{name}\s*=\s*(-?\d+)\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {where}; has it been renamed?")
    return int(match.group(1))


def in_order(text: str, marks: list[str], where: str) -> None:
    places = []
    for mark in marks:
        assert text.count(mark) == 1, f"{where} holds {mark!r} {text.count(mark)} times; this check expects one"
        places.append(text.index(mark))
    assert places == sorted(places), f"{where} no longer has {marks} in this order"


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


# -- the numbers ----------------------------------------------------------------------------------------------------


def test_a_rift_is_four_floors_in_both(model) -> None:
    assert model.rift_floors == 4
    assert constant(read(SURGE_H), "RiftFloors", SURGE_H.name) == model.rift_floors


def test_a_rift_sends_every_five_days_in_both(model) -> None:
    assert model.rift_spawn_every_days == 5
    assert constant(read(SURGE_H), "RiftSendEveryDays", SURGE_H.name) == model.rift_spawn_every_days


def test_a_rift_carries_eight_modifiers_in_both(model) -> None:
    assert model.rift_modifiers == 8
    assert constant(read(MODIFIER_H), "RiftModifierCount", MODIFIER_H.name) == model.rift_modifiers


def test_the_ruled_forms_ship_in_the_model(model) -> None:
    """The 4-floor timer ("Keep the short timer for rifts"), a 4-floor dungeon's enemies ("(b) 8 modifiers only") and
    no limit on sending ("No limit"). The game has no switch for any of them: it reads a dungeon's own floors."""
    assert model.rift_timer_from_quest_depth is False
    assert model.rift_strength_from_quest_depth is False
    assert model.rift_one_sent_at_a_time is False
    assert model.rift_death_brings_surge is True


def test_both_halves_ship_the_rifts_on(model) -> None:
    """The game's Rifts have no switch: every Demonic Quest dungeon is one. So the model's ships on."""
    assert model.demonic_rifts is True, "the model's Rifts ship off while the game has them"
    assert "bool FCataclysmDungeon::IsARift() const" in read(SURGE_CPP)


# -- what a Rift is -------------------------------------------------------------------------------------------------


def test_a_rift_is_a_demonic_quest_dungeon_in_both() -> None:
    from cataclysm_sim.engine import Simulation
    asked = inspect.getsource(Simulation.is_rift)
    assert "d.dtype is DungeonType.QUEST" in asked and 'd.source == "Demonic"' in asked
    game = body_of(read(SURGE_CPP), "bool FCataclysmDungeon::IsARift() const")
    assert "Type == ECataclysmDungeonType::Quest && Cataclysm == ECataclysmType::Demonic" in game


def test_a_rift_is_exactly_its_floors_with_the_timer_of_those_floors_in_both() -> None:
    """The game rolls a Rift's depth from four to four, one draw as any roll is, and lets no upgrade or tree floor move
    it; the walk and the timer are worked out from those floors after, so the timer is a 4-floor dungeon's. The model's
    `_make_dungeon` gives the same with `rift_timer_from_quest_depth` off."""
    game = body_of(read(SURGE_CPP), "FCataclysmDungeon UCataclysmSurgeScheduler::MakeDungeon(")
    in_order(game, ["const bool bARift = Kind == ECataclysmDungeonType::Quest && Cataclysm == ECataclysmType::Demonic;",
                    "if (bARift) { Spec.LeastFloors = RiftFloors; Spec.MostFloors = RiftFloors; }",
                    "Dungeon.Floors = FMath::Max(1, Stream.RandRange(Spec.LeastFloors, Spec.MostFloors));",
                    "const int32 Deeper = bARift ? 0 :",
                    "const int32 Shallower = bARift ? 0 :",
                    "Dungeon.UpgradeFloors = Dungeon.Floors - Rolled;",
                    "UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors)"], "MakeDungeon")
    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation._make_dungeon)
    assert "short = cfg.resolve_base_days + cfg.rift_floors * cfg.resolve_floor_ratio" in model
    assert "floors = cfg.rift_floors" in model


# -- where it opens -------------------------------------------------------------------------------------------------


def test_a_rift_opens_on_any_standing_city_but_the_pillar_by_tier_weight_in_both() -> None:
    source = read(SURGE_CPP)
    wave = body_of(source, "TArray<FCataclysmDungeon> UCataclysmSurgeScheduler::RollWave(")
    # NOT `const` SINCE ISSUE #51 SLICE B4: a third War Quest roll on a day is made Basic after it is drawn.
    in_order(wave, ["ECataclysmDungeonType Kind = RollKind(Stream);",
                    "if (Kind == ECataclysmDungeonType::Quest && Sender == ECataclysmType::Demonic)",
                    "PickRiftCity(Map, RiftDraws, Standing)",
                    "FCataclysmDungeon Dungeon = MakeDungeon("], "RollWave")
    pick = body_of(source, "int32 UCataclysmSurgeScheduler::PickRiftCity(")
    for mark in ("Map.AliveCities()", "CityId == Map.PillarId", "TargetWeightFor(City->Tier)", "RoomLeftOn("):
        assert mark in pick, f"PickRiftCity no longer reads {mark}"
    assert "IsExposed" not in pick, "a Rift no longer opens behind the front"

    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation.trigger_surge)
    assert "anywhere = self.empire.alive_cities()" in model
    assert "rw = [cfg.SURGE_TARGET_WEIGHT[c.tier] for c in anywhere]" in model


# -- what it sends --------------------------------------------------------------------------------------------------


def test_a_rift_sends_on_every_fifth_day_after_it_opened_in_both() -> None:
    game = body_of(read(SURGE_CPP), "bool UCataclysmSurgeScheduler::RiftSendsOn(")
    assert "const int32 Age = Day - SpawnedDay; return Age > 0 && Age % RiftSendEveryDays == 0;" in game
    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation._rift_spawns)
    assert "age = self.day - d.spawned_day" in model and "if age <= 0 or age % every:" in model


def test_it_sends_to_an_adjacent_city_a_surge_could_target_by_tier_weight_in_both() -> None:
    pick = body_of(read(SURGE_CPP), "int32 UCataclysmSurgeScheduler::PickRiftTarget(")
    for mark in ("AdjacentCities(From)", "CityId == Map.PillarId", "!Map.IsExposed(CityId)",
                 "TargetWeightFor(City->Tier)"):
        assert mark in pick, f"PickRiftTarget no longer reads {mark}"
    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation._rift_spawns)
    assert "targets = self.empire.adjacent_exposed_cities(host)" in model
    assert "weights = [cfg.SURGE_TARGET_WEIGHT[c.tier] for c in targets]" in model


def test_what_it_sends_is_a_basic_demonic_dungeon_made_as_a_surge_makes_one_in_both() -> None:
    game = body_of(read(RUN_CPP), "void UCataclysmEmpireRun::SendFromRifts(")
    for mark in ("Surges->MakeDungeon(NextDungeonId, *To, Today, Draws, bSiegeAllowed, "
                 "ECataclysmDungeonType::Basic, ECataclysmType::Demonic)",
                 "Sent.Cataclysm = ECataclysmType::Demonic;", "GiveModifiersFrom(Sent, Draws);",
                 "OutReport.RiftSent.Add(Sent.DungeonId);", "UCataclysmSurgeScheduler::RiftSendsOn("):
        assert mark in game, f"SendFromRifts no longer holds {mark}"
    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation._rift_spawns)
    assert 'self.making_for = "Demonic"' in model and "self._make_dungeon(DungeonType.BASIC, target," in model


def test_it_sends_after_the_days_resolves_and_before_the_empty_board_check_in_both() -> None:
    game = body_of(read(RUN_CPP), "FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay(")
    in_order(game, ["ResolveDungeon(DungeonId, Report);", "SendFromRifts(Today, Report);",
                    "Surges->IsDueOnEmptyBoard("], "AdvanceDay")
    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation._empire_day)
    in_order(model, ["self._resolve(d)", "self._rift_spawns()", "self._maybe_surge_on_empty_board()"],
             "Simulation._empire_day")


# -- its modifiers --------------------------------------------------------------------------------------------------


def test_a_rifts_modifiers_are_eight_whatever_the_tier_in_both() -> None:
    game = body_of(read(RUN_CPP), "void UCataclysmEmpireRun::GiveModifiersFrom(")
    in_order(game, ["UCataclysmDungeonModifierRules::Draw(Pool, Count, Draws);",
                    "if (Dungeon.IsARift())",
                    # FROM THE POOL WITHOUT THE DEMONIC GUIDE AT TIER 1 since 2026-11-25: the owner, "Make demonic guide
                    # not spawn on the Rifts" and "at least not on t1".
                    "UCataclysmDungeonModifierRules::Draw(UCataclysmDungeonModifierRules::RiftPoolFor(Pool, "
                    "DifficultyTier), UCataclysmDungeonModifierRules::RiftModifierCount, RiftDraws);"],
             "GiveModifiersFrom")
    from cataclysm_sim.engine import Simulation
    model = inspect.getsource(Simulation._make_dungeon)
    assert "if a_rift and cfg.rift_modifiers > 0:" in model and "n_mods = cfg.rift_modifiers" in model
    assert "if a_rift and cfg.tier <= cfg.rift_without_the_guide_up_to_tier:" in model, \
        "the model's Rift no longer leaves the Demonic Guide out at tier 1"


# -- a death in one -------------------------------------------------------------------------------------------------


def test_a_death_in_a_rift_brings_an_ordinary_surge_after_the_walks_days_in_both() -> None:
    mode = body_of(read(GAME_MODE_CPP), "bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()")
    in_order(mode, ["DungeonDiedIn->IsARift()", "->ResolveDungeonOnDeath(", "LeaveEmpireDungeon();",
                    "Run->SurgeForADeathInARift()", "->AdvanceDays("], "EndTheDungeonForADeath")
    surge = body_of(read(RUN_CPP), "FCataclysmDayReport UCataclysmEmpireRun::SurgeForADeathInARift()")
    assert "FireSurge(Report.Day, false, Report);" in surge, "a Rift's death surge is no longer an ordinary one"
    assert "IsDueOnCityFall" not in surge and "IsDue(" not in surge, "a Rift's death surge is braked"

    from cataclysm_sim.engine import Simulation
    walk = inspect.getsource(Simulation._walk_day)
    # ONE BLOCK, IN THIS ORDER: `_settle_owed` also ends a walk the campaign's last day cut short, with no surge.
    block = re.compile(r"self\._finish_current\(\)\s*self\._settle_owed\(walked\.did\)\s*self\._surge_for_a_rift_death\(\)")
    assert block.search(walk), "the model's Rift surge no longer follows the walk's days"
    model = inspect.getsource(Simulation._surge_for_a_rift_death)
    assert "self.trigger_surge()" in model and "surge_interval_min" not in model
