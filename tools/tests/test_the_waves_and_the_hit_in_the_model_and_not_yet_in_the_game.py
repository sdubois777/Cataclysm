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

**A QUEST DUNGEON MAY NOT ROLL A SIEGE IN THE MODEL, SINCE 2026-10-16, AND THE
GAME STILL ALLOWS IT.** The owner, that day, verbatim: "The only quest dungeons
that will be sieges are Wars." and "War quest dungeons WILL be sieges. Each
cataclysm's quest dungeons will be one specific type." The model's
`TuningConfig.SUBTYPES_FORBIDDEN_ON` bars Quest and Siege as an interim rule
until each Cataclysm's Quest type is named; the game's
`UCataclysmSurgeScheduler::BarredSubTypeOn` bars only the Cataclysm's Cow
Level. Until that day `test_dungeon_subtype_port.py` compared every pair the
model bars with the game; it now compares the Cataclysm pair, and this file
holds the Quest pair, by a labelled judgement by the coordinating session under
the owner's delegation, 2026-10-16.

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


RESTORE_SIEGE = (
    "If the game now bars a Quest dungeon from rolling a Siege, this "
    "divergence is over. Do these things: (1) check WHICH rule the game took: "
    "the model's interim one (no Quest dungeon is a Siege) or the owner's "
    "named one (each Cataclysm's Quest dungeons are one type, and War's are "
    "Sieges); if the game has the named one and the model the interim one, "
    "the halves still differ and this file should say so instead; (2) in "
    "tools/tests/test_dungeon_subtype_port.py make "
    "test_the_game_bars_the_same_pair_the_model_does compare every pair the "
    "model bars again, and change test_the_game_constrains_only_the_cataclysm "
    "to expect the Quest case; (3) delete the two Quest-and-Siege tests and "
    "their part of the control from this file; (4) add a dated sentence to "
    "the docs/DECISIONS.md entry of 2026-10-16.")


def the_games_barred_kinds() -> list[str]:
    """The dungeon kinds `BarredSubTypeOn` names a `case` for, read out of the
    switch the way `test_dungeon_subtype_port.py` reads it."""
    source_path = SURGE_HEADER.parent / "CataclysmSurge.cpp"
    if not source_path.is_file():
        pytest.skip(f"{source_path.name} is not present")
    body = re.search(
        r"ECataclysmDungeonSubType UCataclysmSurgeScheduler::"
        r"BarredSubTypeOn\((.*?)\n\}\n",
        source_path.read_text(encoding="utf-8"), re.DOTALL)
    assert body, ("UCataclysmSurgeScheduler::BarredSubTypeOn is gone from "
                  "CataclysmSurge.cpp, so this file cannot read which kinds "
                  "the game bars a sub-type on")
    return re.findall(r"case\s+ECataclysmDungeonType::(\w+)\s*:",
                      body.group(1))


def the_models_quest_bar() -> tuple[str, ...]:
    from cataclysm_sim.config import DungeonType
    return tuple(the_model().SUBTYPES_FORBIDDEN_ON.get(DungeonType.QUEST, ()))


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


def test_the_model_bars_a_quest_dungeon_from_the_siege() -> None:
    """The interim rule of 2026-10-16, read as the data, and then as the roll:
    a field the roll did not read would pass the first assertion."""
    assert the_models_quest_bar() == ("Siege",), (
        f"TuningConfig().SUBTYPES_FORBIDDEN_ON bars {the_models_quest_bar()} "
        "on a Quest dungeon. Since 2026-10-16 it bars the Siege; the owner, "
        "verbatim: \"The only quest dungeons that will be sieges are Wars.\" "
        "If it was taken out to match the game, do not: the two differ on "
        "purpose until the game takes the rule.")

    from cataclysm_sim.config import DungeonType
    from cataclysm_sim.engine import Simulation

    sim = Simulation(the_model(), seed=0)
    assert "Siege" not in sim.subtypes_allowed_on(DungeonType.QUEST), (
        "Simulation.subtypes_allowed_on still offers a Quest dungeon the Siege")
    assert "Siege" in sim.subtypes_allowed_on(DungeonType.BASIC), (
        "CONTROL: an ordinary dungeon may not roll a Siege either, so the "
        "assertion above is not about the Quest rule")


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


def test_the_game_still_lets_a_quest_dungeon_roll_a_siege() -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME BARS THE PAIR.** Read as a
    `case` for the Quest kind in `BarredSubTypeOn`, which is where the game
    keeps the one pair it bars."""
    kinds = the_games_barred_kinds()
    assert "Quest" not in kinds, (
        f"BarredSubTypeOn now names {kinds}. The game bars a sub-type on a "
        "Quest dungeon; the model has barred the Siege there since "
        "2026-10-16. " + RESTORE_SIEGE)
    assert kinds == ["Cataclysm"], (
        f"BarredSubTypeOn names {kinds}, so this file cannot read whether the "
        "game bars the Quest pair. Find out what changed before trusting it")


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
    model_bars_it = "Siege" in the_models_quest_bar()
    game_bars_it = "Quest" in the_games_barred_kinds()
    assert model_bars_it != game_bars_it, (
        "the model and the game agree on whether a Quest dungeon may roll a "
        "Siege. " + RESTORE_SIEGE)
