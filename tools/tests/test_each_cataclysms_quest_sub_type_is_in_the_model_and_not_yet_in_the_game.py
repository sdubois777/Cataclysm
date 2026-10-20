"""Each Cataclysm's Quest dungeons are one fixed sub-type in the model; the game still rolls one.

**THE TWO HALVES DIFFER ON PURPOSE, AND THIS FILE IS THE RECORD OF IT.**

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

**IT HELD A SECOND DIFFERENCE UNTIL 2026-10-20, under another name.** This file
was `test_the_waves_and_the_hit_in_the_model_and_not_yet_in_the_game.py` and
also pinned the model's opening wave of 12, its 8 a wave and its ordinary rows
at 0.7 against the game's 5 a wave, no opening size and the old rows, all three
chosen for the model on 2026-10-16. The game's layer of 2026-10-20 took all
three, put the comparisons back into `test_surge_port.py` as this file said to,
deleted the wave and row checks from here and renamed the file for what it
still holds. `docs/DECISIONS.md`, 2026-10-20.

THIS FILE EXISTS TO FAIL WHEN THE GAME'S RULE MOVES. Each failure message
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


RESTORE_QUEST_TYPES = (
    "If the game now gives a Quest dungeon its Cataclysm's sub-type, this "
    "divergence is over. Do these things: (1) check the game's mapping is "
    "the model's TuningConfig.QUEST_SUBTYPE_BY_CATACLYSM, Cataclysm by "
    "Cataclysm; (2) move the comparison into tools/tests/test_dungeon_subtype_port.py; "
    "(3) delete this file, whose other checks went with the game's layer of "
    "2026-10-20; (4) add a dated sentence to the docs/DECISIONS.md entry of "
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


def the_model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


# ---------------------------------------------------------------------------
# The model
# ---------------------------------------------------------------------------

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
    divergence that does not exist, and the comparison belongs in
    `test_dungeon_subtype_port.py`."""
    model_fixes_it = bool(the_models_quest_types())
    game_fixes_it = (not the_games_quest_roll()
                     or set(the_models_quest_types().values()) <= set(the_games_sub_types()))
    assert model_fixes_it != game_fixes_it, (
        "the model and the game agree on whether a Quest dungeon's sub-type "
        "is fixed by its Cataclysm. " + RESTORE_QUEST_TYPES)
