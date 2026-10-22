"""Two Cataclysms' Quest dungeons are one fixed sub-type in the model; the game still rolls theirs.

**THE TWO HALVES DIFFER ON PURPOSE FOR THESE TWO, AND THIS FILE IS THE RECORD OF IT.**

The owner, 2026-10-16, verbatim: "War quest dungeons WILL be sieges. Each
cataclysm's quest dungeons will be one specific type." The model's
`TuningConfig.QUEST_SUBTYPE_BY_CATACLYSM` names one for each of the eight. The
game's `UCataclysmSurgeScheduler::QuestSubTypeFor` names one for six of them
-- Death, Demonic, Pestilence, Famine and Chaos since 2026-10-21, and the Void,
whose Quest dungeons are Hollows, since 2026-10-22 -- and
`tools/tests/test_dungeon_subtype_port.py` compares those six pairs by name.
For the other two it answers `None`, and a Quest dungeon they send rolls its
sub-type as any dungeon does:

- **War**: Siege in the model. The game rolls, because the owner has ruled War
  out of the game; under that mandate this is a deliberate gap and not an
  oversight. `docs/DECISIONS.md`, 2026-10-21.
- **Celestial**: Ascendant in the model. The game has no Ascendant sub-type.

**THIS FILE HAS HAD FOUR NAMES.** It was
`test_the_waves_and_the_hit_in_the_model_and_not_yet_in_the_game.py` until
2026-10-20, when the game took the waves and the rows and those checks went
back to `test_surge_port.py`; then
`test_each_cataclysms_quest_sub_type_is_in_the_model_and_not_yet_in_the_game.py`
until 2026-10-21, when it held all eight Cataclysms, and its restore text
assumed the game would take the whole mapping at once. The game took five; the
file was narrowed to the three it then held and renamed for them, as
`test_three_cataclysms_quest_sub_types_are_in_the_model_and_not_yet_in_the_game.py`,
until 2026-10-22, when the game took the Void's Hollow and the file was narrowed
to the two it still holds and renamed for them again.

THIS FILE EXISTS TO FAIL WHEN THE GAME'S RULE MOVES FOR EITHER OF THE TWO. Each
failure message says what to do then.
"""

from __future__ import annotations

import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

# THE MODULES AND NOT THEIR CLASSES. A `Test` class imported by name would be
# collected and run a second time from this file.
import test_dungeon_subtype_port as subtype_port  # noqa: E402

#: The two, as the model and the game's `ECataclysmType` both spell them,
#: against the sub-type the model gives their Quest dungeons. The Void was the
#: third until 2026-10-22, and left by its own restore text.
THE_TWO = {"War": "Siege", "Celestial": "Ascendant"}

RESTORE = {
    "War": (
        "The game now names a sub-type for War's Quest dungeons. Under the "
        "owner's mandate that War is not in the game they roll on purpose "
        "(docs/DECISIONS.md, 2026-10-21), so first check that mandate has been "
        "lifted. Then: (1) check the game's answer is Siege, the model's; "
        "(2) add War to FIXED_IN_THE_GAME in tools/tests/test_dungeon_subtype_port.py "
        "so the pair is compared there; (3) take War out of THE_TWO here; "
        "(4) add a dated sentence to the docs/DECISIONS.md entry of 2026-10-21 "
        "on five Cataclysms' Quest dungeons. Note the model caps War's Quest "
        "Sieges at two a wave and re-draws a besieged city; the game has "
        "neither."),
    "Celestial": (
        "The game now has an Ascendant sub-type or names one for Celestial's "
        "Quest dungeons. Do these things: (1) check QuestSubTypeFor answers "
        "Ascendant for Celestial, the model's; (2) check Ascendant is not in "
        "the game's spawn weights, as it is not in the model's "
        "SUBTYPE_SPAWN_WEIGHTS -- an ordinary dungeon never rolls it -- and "
        "that test_dungeon_subtype_port.py's NAMES still lists only what is "
        "rolled, and add Ascendant to NEVER_ROLLED there as the Hollow is; "
        "(3) add Celestial to FIXED_IN_THE_GAME there; (4) take Celestial out "
        "of THE_TWO here; (5) add a dated sentence to the docs/DECISIONS.md "
        "entry of 2026-10-21."),
}

WHEN_BOTH_ARE_GONE = (
    "When THE_TWO is empty this file records nothing: delete it, and say so "
    "in that docs/DECISIONS.md sentence.")


def the_models_quest_types() -> dict[str, str]:
    return dict(the_model().QUEST_SUBTYPE_BY_CATACLYSM)


def the_model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def the_games_sub_types() -> list[str]:
    """The names in the game's `ECataclysmDungeonSubType`, as written."""
    return subtype_port.the_games_sub_type_names(
        subtype_port.read(subtype_port.KIND_HEADER))


def the_games_answers() -> tuple[dict[str, str], str]:
    """The game's `QuestSubTypeFor`: its named cases, and its default."""
    return subtype_port.the_games_quest_sub_types(
        subtype_port.read(subtype_port.SURGE_HEADER.parent / "CataclysmSurge.cpp"))


# ---------------------------------------------------------------------------
# The model
# ---------------------------------------------------------------------------

def test_the_model_gives_each_cataclysms_quest_dungeons_one_sub_type() -> None:
    """The rule of 2026-10-16, read as the data, and then as the roll: a field
    the surge did not read would pass the first assertion."""
    types = the_models_quest_types()
    for cataclysm, sub_type in THE_TWO.items():
        assert types.get(cataclysm) == sub_type, (
            f"TuningConfig().QUEST_SUBTYPE_BY_CATACLYSM is {types}; "
            f"{cataclysm}'s Quest dungeons are {sub_type} in the model. The "
            "owner, 2026-10-16, verbatim: \"War quest dungeons WILL be sieges. "
            "Each cataclysm's quest dungeons will be one specific type.\" If "
            "it was taken out to match the game, do not: the two differ on "
            "purpose until the game takes the rule.")

    from dataclasses import replace

    from cataclysm_sim.config import DungeonType
    from cataclysm_sim.engine import Simulation

    sim = Simulation(replace(the_model(), quest_dungeon_chance=1.0), seed=0)
    sim.active_types = ("War",)
    sim.dungeons.clear()
    sim.trigger_surge()
    made = {d.subtype for d in sim.dungeons.values() if d.dtype is DungeonType.QUEST}
    assert made == {types["War"]}, (
        f"a War surge's Quest dungeons came out {made}, not {types['War']}")

    sim = Simulation(replace(the_model(), quest_dungeon_chance=0.0), seed=0)
    sim.active_types = ("War",)
    sim.dungeons.clear()
    sim.trigger_surge()
    rolled = {d.subtype for d in sim.dungeons.values()}
    assert len(rolled) > 1, (
        f"CONTROL: the same surge's ordinary dungeons came out {rolled}, one "
        "sub-type, so the assertion above is not about the Quest rule")


# ---------------------------------------------------------------------------
# The game
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("cataclysm", sorted(THE_TWO))
def test_the_game_still_rolls_this_cataclysms_quest_dungeons(cataclysm) -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME TAKES THE RULE FOR ONE OF THE
    TWO.** Read out of `QuestSubTypeFor`'s switch: the Cataclysm has no case
    of its own, and the default it falls to answers `None`, which is "roll"."""
    cases, default = the_games_answers()
    assert cataclysm not in cases, (
        f"QuestSubTypeFor now has a case for {cataclysm}, answering "
        f"{cases[cataclysm]}. " + RESTORE[cataclysm] + " " + WHEN_BOTH_ARE_GONE)
    assert default == "None", (
        f"QuestSubTypeFor's default now answers {default}, so every Cataclysm "
        "without a case -- War and Celestial -- fixes one sub-type. "
        + RESTORE[cataclysm])


def test_the_game_has_no_ascendant() -> None:
    """Ascendant, the name the model's mapping added for Celestial, is not in
    the game's `ECataclysmDungeonSubType`. Siege is, which is why War's half of
    this file is the mandate and not a missing name. Hollow, the other name
    the mapping added, has been in the game since 2026-10-22; until then this
    was `test_the_game_has_neither_new_sub_type`."""
    names = the_games_sub_types()
    missing = sorted(set(THE_TWO.values()) - set(names))
    assert missing == ["Ascendant"], (
        f"the game's ECataclysmDungeonSubType is {names}; of the two's Quest "
        f"sub-types it lacks {missing}, where it lacked Ascendant. "
        + (RESTORE["Celestial"] if "Ascendant" not in missing else ""))


# ---------------------------------------------------------------------------
# The control
# ---------------------------------------------------------------------------

def test_the_two_really_do_differ_and_the_other_six_do_not() -> None:
    """**THE CONTROL.** For each of the two the model names a sub-type and
    the game names none, so the file records a difference that exists. And the
    switch it reads is the real one: it names six other Cataclysms, each with
    the model's sub-type, so a parse that found nothing cannot pass the checks
    above by finding no case for anyone."""
    model = the_models_quest_types()
    cases, _ = the_games_answers()

    for cataclysm in THE_TWO:
        assert model.get(cataclysm) and cataclysm not in cases, (
            f"the model and the game agree on {cataclysm}'s Quest sub-type. "
            + RESTORE[cataclysm])

    assert len(cases) == 6, (
        f"QuestSubTypeFor names {sorted(cases)}; it named five Cataclysms on "
        "2026-10-21 and six on 2026-10-22, so either the parse broke or the "
        "game moved")
    for cataclysm, game_name in cases.items():
        assert subtype_port.game_name_for(model.get(cataclysm)) == game_name, (
            f"{cataclysm}: the game answers {game_name} and the model "
            f"{model.get(cataclysm)}")
