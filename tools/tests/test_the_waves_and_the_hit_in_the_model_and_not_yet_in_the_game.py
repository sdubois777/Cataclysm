"""The model opens a run with 12 dungeons, then brings 8 a wave, and hits for 0.7; the game does not yet.

**THE TWO HALVES DIFFER ON PURPOSE, AND THIS FILE IS THE RECORD OF IT.**

Until 2026-10-16 `TestTheCadence` in `test_surge_port.py` held
`UCataclysmSurgeScheduler::DungeonsPerSurge` equal to
`TuningConfig.surge_dungeon_count`, and `TestWhatADungeonIs.SPECS` held the
four ordinary rows' damage equal in the model and in the game. On that day the
model changed three things together. The owner, verbatim: "I want surges to
summon a lot more dungeons. Or maybe the initial surge spawns more and then the
first real surge is 6 to 10? Idk, but 6 is boring." and "the obvious solution
here is to adjust the damage dungeons do." The coordinating session chose,
under the owner's delegation of numbers:

  * `surge_opening_count` 12, new: the first wave of a run;
  * `surge_dungeon_count` 8: every later wave, before escalation;
  * every ordinary (`BASIC`) row of `DUNGEON_SPECS` at 0.7 of its damage.

The game keeps 5 a wave, has no opening size, and keeps the old rows until the
layer that takes this package into it. The ruling for this file: "a 'model and
not yet in the game' check file, as on 2026-10-13, failing when the game layer
lands; the hand-worked Unreal figures move with the game layer", a labelled
judgement by the coordinating session under the owner's delegation, 2026-10-16.
It copies the shape of
`test_the_surge_is_90_days_and_5_in_the_model_and_not_yet_in_the_game.py`,
which the game's layer of 2026-10-13 deleted when it was done.

**EACH CATACLYSM'S QUEST DUNGEONS ARE ONE FIXED SUB-TYPE IN THE MODEL, AND THE
GAME STILL ROLLS ONE.** The owner, 2026-10-16, verbatim: "War quest dungeons
WILL be sieges. Each cataclysm's quest dungeons will be one specific type." The
model's `TuningConfig.QUEST_SUBTYPE_BY_CATACLYSM` names one for each, two of
them new (Ascendant and Hollow); the game's `UCataclysmSurgeScheduler::RollSubType`
rolls every Quest dungeon's, and its `ECataclysmDungeonSubType` has neither new
name. From 2026-10-16 until this layer the model instead barred a Quest
dungeon from the Siege, as an interim rule, and this file held that pair; the
coordinating session ruled on 2026-10-18 that these checks hold the difference
that now exists instead.

THIS FILE EXISTS TO FAIL WHEN THE GAME'S NUMBERS MOVE. Each failure message
says what to do then.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

# THE MODULE AND NOT ITS CLASS. A `Test` class imported by name would be
# collected and run a second time from this file.
import test_surge_port as surge_port  # noqa: E402

SURGE_HEADER = surge_port.SURGE_HEADER

#: What the model ships since 2026-10-16.
THE_MODEL_WAVES = {"surge_opening_count": 12, "surge_dungeon_count": 8}

#: The four ordinary rows, (defence, people), in the model since 2026-10-16.
THE_MODEL_ROWS = {
    "Outpost": (70.0, 175.0),
    "Bulwark": (189.0, 700.0),
    "Sanctuary": (448.0, 1680.0),
    "Pillar": (840.0, 3150.0),
}

#: The same four rows in the game, which are the model's before 2026-10-16.
THE_GAME_ROWS = {
    "Outpost": (100.0, 250.0),
    "Bulwark": (270.0, 1000.0),
    "Sanctuary": (640.0, 2400.0),
    "Pillar": (1200.0, 4500.0),
}

#: What the game has for a wave.
THE_GAME_PER_SURGE = 5.0

RESTORE = (
    "If the game now has the model's numbers, this divergence is over. Do "
    "these things: (1) put DungeonsPerSurge back in TestTheCadence.NUMBERS "
    "in tools/tests/test_surge_port.py, write the model's four BASIC rows "
    "into TestWhatADungeonIs.SPECS there and take out the skip of the Basic "
    "damage in test_the_model_holds_the_thirteen_specs_this_test_expects, "
    "and add a check that the game's opening size equals "
    "surge_opening_count; then delete this file; (2) "
    "TestTheHandWorkedFiguresInTheUnrealTest in that file takes its ladders "
    "at the game's constants and gives the model an opening size equal to "
    "DungeonsPerSurge, because the game had none: give it the game's opening "
    "size, work the new ladder out from the model, and change its figures "
    "and the ones CataclysmSurgeTests.cpp asserts together; (3) the three "
    "numbers went into the model together: check the game has all three, "
    "or it has part of the package; (4) add a dated sentence to the "
    "docs/DECISIONS.md entry of 2026-10-16. If the game has some OTHER "
    "number, the two halves have parted with no decision behind it: find "
    "out which is meant.")


RESTORE_QUEST_TYPES = (
    "If the game now gives a Quest dungeon its Cataclysm's sub-type, this "
    "divergence is over. Do these things: (1) check the game's mapping is "
    "the model's TuningConfig.QUEST_SUBTYPE_BY_CATACLYSM, Cataclysm by "
    "Cataclysm; (2) move the comparison into tools/tests/test_dungeon_subtype_port.py; "
    "(3) delete the Quest sub-type tests and their part of the control from "
    "this file; (4) add a dated sentence to the docs/DECISIONS.md entry of "
    "2026-10-18 on each Cataclysm's Quest dungeons.")

#: Where the game names its sub-types.
KIND_HEADER = SURGE_HEADER.parent / "CataclysmDungeonKind.h"


def the_games_sub_types() -> list[str]:
    """The names in the game's `ECataclysmDungeonSubType`, as written."""
    if not KIND_HEADER.is_file():
        pytest.skip(f"{KIND_HEADER.name} is not present")
    body = re.search(r"enum class ECataclysmDungeonSubType\s*:\s*uint8\s*\{(.*?)\};",
                     KIND_HEADER.read_text(encoding="utf-8"), re.DOTALL)
    assert body, (f"ECataclysmDungeonSubType is gone from {KIND_HEADER.name}, "
                  "so this file cannot read the game's sub-types")
    return re.findall(r"^\s*(\w+)\s*=", body.group(1), re.MULTILINE)


def the_games_quest_roll() -> bool:
    """Whether the game's dungeon maker rolls every kind's sub-type, the
    Quest dungeon's included, in its one assignment."""
    source_path = SURGE_HEADER.parent / "CataclysmSurge.cpp"
    if not source_path.is_file():
        pytest.skip(f"{source_path.name} is not present")
    text = source_path.read_text(encoding="utf-8")
    return text.count("Dungeon.SubType = RollSubType(Stream, bSiegeAllowed, Kind);") == 1


def the_models_quest_types() -> dict[str, str]:
    return dict(the_model().QUEST_SUBTYPE_BY_CATACLYSM)


def the_header() -> str:
    if not SURGE_HEADER.is_file():
        pytest.skip(f"{SURGE_HEADER.name} is not present")
    return SURGE_HEADER.read_text(encoding="utf-8")


def the_games_per_surge() -> float:
    found = re.findall(
        r"static\s+constexpr\s+\w+\s+DungeonsPerSurge\s*=\s*(-?[0-9.]+)f?\s*;",
        the_header())
    assert len(found) == 1, (
        f"{SURGE_HEADER.name} declares DungeonsPerSurge {len(found)} times; "
        "this file reads it to record that the game's wave is not the "
        "model's, and can no longer find it. Has it been renamed?")
    return float(found[0])


def the_games_rows() -> dict:
    """The game's four ordinary rows, read by `test_surge_port`'s parser of
    `SpecFor` rather than a second copy of it."""
    found = surge_port.TestWhatADungeonIs.unreal_specs()
    rows = {}
    for tier in THE_GAME_ROWS:
        assert ("Basic", tier) in found, (
            f"SpecFor has no Basic row for the {tier} that this file can read")
        _, _, defence, people = found[("Basic", tier)]
        rows[tier] = (defence, people)
    return rows


def the_model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def the_models_rows() -> dict:
    from cataclysm_sim.config import CityTier, DungeonType

    cfg = the_model()
    rows = {}
    for tier in THE_MODEL_ROWS:
        spec = cfg.spec(DungeonType.BASIC, CityTier(tier))
        rows[tier] = (spec.defense_damage, spec.population_damage)
    return rows


# ---------------------------------------------------------------------------
# The model
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("field", sorted(THE_MODEL_WAVES))
def test_the_model_opens_with_12_and_then_brings_8(field: str) -> None:
    assert getattr(the_model(), field) == THE_MODEL_WAVES[field], (
        f"TuningConfig().{field} is {getattr(the_model(), field)} and the "
        f"package chosen on 2026-10-16 has {THE_MODEL_WAVES[field]}. If it "
        "was put back to match the game, do not: the two differ on purpose "
        "until the game takes the package. Set it by name in a measurement.")


def test_the_models_first_wave_is_the_opening_size_and_the_next_is_not() -> None:
    """The opening size is READ, and read only at wave index 0. A field
    nothing reads would pass the test above."""
    from types import SimpleNamespace

    from cataclysm_sim.engine import Simulation

    cfg = the_model()
    first = Simulation.surge_count(SimpleNamespace(cfg=cfg, surge_index=0))
    second = Simulation.surge_count(SimpleNamespace(cfg=cfg, surge_index=1))
    assert (first, second) == (12, 8), (
        f"the model's first two waves are {first} and {second}, not 12 and 8")


@pytest.mark.parametrize("tier", sorted(THE_MODEL_ROWS))
def test_the_models_ordinary_row_is_0_7_of_what_it_was(tier: str) -> None:
    assert the_models_rows()[tier] == pytest.approx(THE_MODEL_ROWS[tier]), (
        f"the model's Basic row on the {tier} is {the_models_rows()[tier]}, "
        f"not the {THE_MODEL_ROWS[tier]} chosen on 2026-10-16. If it was put "
        "back to match the game, do not: the two differ on purpose until the "
        "game takes the package.")


@pytest.mark.parametrize("tier", sorted(THE_MODEL_ROWS))
def test_the_models_row_is_the_games_times_0_7(tier: str) -> None:
    """The two hand-written tables in this file are one rule, not two lists."""
    for model, game in zip(THE_MODEL_ROWS[tier], THE_GAME_ROWS[tier],
                           strict=True):
        assert model == pytest.approx(game * 0.7)


def test_the_model_gives_each_cataclysms_quest_dungeons_one_sub_type() -> None:
    """The rule of 2026-10-16, read as the data, and then as the roll: a field
    the surge did not read would pass the first assertion."""
    types = the_models_quest_types()
    assert types.get("War") == "Siege", (
        f"TuningConfig().QUEST_SUBTYPE_BY_CATACLYSM is {types}; War's Quest "
        "dungeons are Sieges. The owner, 2026-10-16, verbatim: \"War quest "
        "dungeons WILL be sieges. Each cataclysm's quest dungeons will be one "
        "specific type.\" If it was taken out to match the game, do not: the "
        "two differ on purpose until the game takes the rule.")

    from dataclasses import replace

    from cataclysm_sim.config import DungeonType
    from cataclysm_sim.engine import Simulation

    sim = Simulation(replace(the_model(), quest_dungeon_chance=1.0), seed=0)
    sim.active_types = ("Death",)
    sim.dungeons.clear()
    sim.trigger_surge()
    made = {d.subtype for d in sim.dungeons.values() if d.dtype is DungeonType.QUEST}
    assert made == {types["Death"]}, (
        f"a Death surge's Quest dungeons came out {made}, not {types['Death']}")

    sim = Simulation(replace(the_model(), quest_dungeon_chance=0.0), seed=0)
    sim.active_types = ("Death",)
    sim.dungeons.clear()
    sim.trigger_surge()
    rolled = {d.subtype for d in sim.dungeons.values()}
    assert len(rolled) > 1, (
        f"CONTROL: the same surge's ordinary dungeons came out {rolled}, one "
        "sub-type, so the assertion above is not about the Quest rule")


# ---------------------------------------------------------------------------
# The game
# ---------------------------------------------------------------------------

def test_the_game_still_brings_5_a_wave() -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME'S CONSTANT MOVES.**"""
    assert the_games_per_surge() == THE_GAME_PER_SURGE, (
        f"UCataclysmSurgeScheduler::DungeonsPerSurge is now "
        f"{the_games_per_surge():g}; it was {THE_GAME_PER_SURGE:g} when the "
        "model went to an opening wave of 12 and then 8 on 2026-10-16. "
        + RESTORE)


def test_the_game_still_has_no_opening_size() -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME GAINS ONE.** Read as any
    constant in the scheduler's header whose name says opening or first
    surge. A name this does not anticipate would pass it, so the wave test
    above is the one that will notice first."""
    named = re.findall(
        r"static\s+constexpr\s+\w+\s+(\w*(?:Opening|FirstSurge|FirstWave)\w*)",
        the_header())
    assert not named, (
        f"CataclysmSurge.h now declares {named}. The game has an opening "
        "wave size; the model has had one, 12, since 2026-10-16. " + RESTORE)


@pytest.mark.parametrize("tier", sorted(THE_GAME_ROWS))
def test_the_games_ordinary_row_is_still_the_old_one(tier: str) -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME'S ROWS MOVE.**"""
    assert the_games_rows()[tier] == pytest.approx(THE_GAME_ROWS[tier]), (
        f"SpecFor's Basic row on the {tier} is now "
        f"{the_games_rows()[tier]}; it was {THE_GAME_ROWS[tier]} when the "
        f"model went to {THE_MODEL_ROWS[tier]} on 2026-10-16. " + RESTORE)


def test_the_game_still_rolls_a_quest_dungeons_sub_type() -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME TAKES THE RULE.** Read as the
    game's one sub-type assignment, which rolls for every kind, and as its
    sub-type list, which lacks the two names the model's mapping added."""
    assert the_games_quest_roll(), (
        "CataclysmSurge.cpp no longer assigns every dungeon's sub-type with "
        "RollSubType in one line. The game may now give a Quest dungeon its "
        "Cataclysm's sub-type. " + RESTORE_QUEST_TYPES)
    names = the_games_sub_types()
    missing = sorted(set(the_models_quest_types().values()) - set(names))
    assert missing == ["Ascendant", "Hollow"], (
        f"the game's ECataclysmDungeonSubType is {names}; of the model's Quest "
        f"sub-types it lacks {missing}, where it lacked Ascendant and Hollow. "
        + RESTORE_QUEST_TYPES)


# ---------------------------------------------------------------------------
# The control
# ---------------------------------------------------------------------------

def test_the_two_really_do_differ() -> None:
    """**THE CONTROL.** If the two were equal this file would be recording a
    divergence that does not exist, and the rows belong back in
    `test_surge_port.py`."""
    model = the_model()
    assert the_games_per_surge() != float(model.surge_dungeon_count), (
        "DungeonsPerSurge equals TuningConfig().surge_dungeon_count. " + RESTORE)
    assert the_games_per_surge() != float(model.surge_opening_count), (
        "DungeonsPerSurge equals TuningConfig().surge_opening_count. " + RESTORE)
    games, models = the_games_rows(), the_models_rows()
    for tier in THE_GAME_ROWS:
        assert games[tier] != pytest.approx(models[tier]), (
            f"the {tier}'s Basic row is the same in both halves. " + RESTORE)
    model_fixes_it = bool(the_models_quest_types())
    game_fixes_it = (not the_games_quest_roll()
                     or set(the_models_quest_types().values()) <= set(the_games_sub_types()))
    assert model_fixes_it != game_fixes_it, (
        "the model and the game agree on whether a Quest dungeon's sub-type "
        "is fixed by its Cataclysm. " + RESTORE_QUEST_TYPES)
