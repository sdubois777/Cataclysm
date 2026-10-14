"""A surge is 5 dungeons every 90 days in the model, and still 4 every 120 in the game.

**THE TWO HALVES DIFFER ON PURPOSE, AND THIS FILE IS THE RECORD OF IT.**

Until 2026-10-13 `TestTheCadence` in `test_surge_port.py` held
`UCataclysmSurgeScheduler::IntervalDays` equal to
`TuningConfig.surge_interval_days`, and `DungeonsPerSurge` equal to
`TuningConfig.surge_dungeon_count`. On that day the model went to 90 days and 5
dungeons: the owner accepted the coordinating session's recommendation, in that
session's words "surges every 90 days of 5 dungeons". The game keeps 120 and 4
until the layer that takes the whole package of 2026-10-13 into it, because the
numbers were chosen together with two rules the game does not have either: an
ordinary dungeon leaving after it hits, and a Quest dungeon's timer hitting.

THIS FILE EXISTS TO FAIL WHEN EITHER OF THE GAME'S CONSTANTS MOVES. Each
failure message says what to do then.

THE OTHER FOUR CADENCE NUMBERS ARE STILL HELD EQUAL, in `TestTheCadence`.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SURGE_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
                / "CataclysmSurge.h")

THE_MODEL = {"surge_interval_days": 90.0, "surge_dungeon_count": 5}
THE_GAME = {"IntervalDays": 120.0, "DungeonsPerSurge": 4.0}
PAIRS = {"IntervalDays": "surge_interval_days",
         "DungeonsPerSurge": "surge_dungeon_count"}

RESTORE = (
    "If the game now has the model's number, this divergence is over for "
    "that constant. Do these things: (1) put its row back in "
    "TestTheCadence.NUMBERS in tools/tests/test_surge_port.py, so the two are "
    "held equal again, and take it out of this file; when both rows are back, "
    "delete this file; (2) TestTheHandWorkedFiguresInTheUnrealTest in that "
    "file takes its ladders at the game's constants, so its figures and the "
    "ones CataclysmSurgeTests.cpp asserts (105.6f, 92.928f and the counts 4, "
    "5, 6, 13, 14, 17) change with the constant: work the new ladder out from "
    "the model and change both; (3) the number went into the model together "
    "with two rules, an ordinary dungeon leaving after it hits its city and a "
    "Quest dungeon's timer hitting its city: check the game has both, or the "
    "game has the cadence of the package without the package; (4) add a "
    "dated sentence to the docs/DECISIONS.md entry of 2026-10-13. If the game "
    "has some OTHER number, the two halves have parted with no decision "
    "behind it: find out which is meant.")


def the_games(name: str) -> float:
    if not SURGE_HEADER.is_file():
        pytest.skip(f"{SURGE_HEADER.name} is not present")
    text = SURGE_HEADER.read_text(encoding="utf-8")
    found = re.findall(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*(-?[0-9.]+)f?\s*;", text)
    assert len(found) == 1, (
        f"{SURGE_HEADER.name} declares {name} {len(found)} times; this file "
        "reads it to record that the game's cadence is not the model's, and "
        "can no longer find it. Has it been renamed?")
    return float(found[0])


@pytest.mark.parametrize("field", sorted(THE_MODEL))
def test_the_model_ships_90_days_and_5_dungeons(field: str) -> None:
    from cataclysm_sim.config import TuningConfig

    assert getattr(TuningConfig(), field) == THE_MODEL[field], (
        f"TuningConfig().{field} is {getattr(TuningConfig(), field)} and the "
        f"package accepted on 2026-10-13 has {THE_MODEL[field]}. If it was "
        "put back to match the game, do not: the two differ on purpose until "
        "the game takes the package. Set it by name in a measurement.")


@pytest.mark.parametrize("name", sorted(THE_GAME))
def test_the_game_still_has_120_days_and_4_dungeons(name: str) -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME'S CONSTANT MOVES.**"""
    assert the_games(name) == THE_GAME[name], (
        f"UCataclysmSurgeScheduler::{name} is now {the_games(name):g}; it was "
        f"{THE_GAME[name]:g} when the model went to "
        f"{THE_MODEL[PAIRS[name]]:g} on 2026-10-13. " + RESTORE)


@pytest.mark.parametrize("name", sorted(THE_GAME))
def test_the_two_really_do_differ(name: str) -> None:
    """**THE CONTROL.** If the two were equal this file would be recording a
    divergence that does not exist, and the row belongs back in
    `TestTheCadence`."""
    from cataclysm_sim.config import TuningConfig

    assert the_games(name) != float(getattr(TuningConfig(), PAIRS[name])), (
        f"UCataclysmSurgeScheduler::{name} equals TuningConfig()."
        f"{PAIRS[name]}. " + RESTORE)
