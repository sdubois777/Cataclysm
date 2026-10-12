"""A death resolves an ordinary dungeon in the model as it does in the game. Issue #41.

TWO COPIES OF A RULE ARE TWO RULES. The game has resolved an ordinary dungeon on
a death since 2026-10-10: `ACataclysmDungeonGameMode::EndTheDungeonForADeath`
and `UCataclysmEmpireRun::ResolveDungeonOnDeath`. The model gained the same rule
on 2026-10-12, in `Simulation._finish_current` and
`Simulation._resolve_on_death`. This file reads both as text and holds them to
the four things that are about where a line sits, which no measurement of a
campaign separates:

1. THE ORDER IS RESOLVE, LEAVE, CHARGE THE DAYS, in both.
2. BOTH ASK THE KIND BEFORE THEY RESOLVE, so a death never reaches the
   relocation of a Quest dungeon in either.
3. BOTH RESOLVE THROUGH THE CALL A TIMER RUNNING OUT MAKES, once, and neither
   repeats its arithmetic.
4. THE DESIGNED BEHAVIOUR IS THE DEFAULT IN BOTH: the game's playtest switch is
   0 and the model's setting is on.

WHAT IT DOES NOT CHECK. What a death takes from a city: that is
`sim/tests/test_a_death_resolves_an_ordinary_dungeon.py` for the model and the
Unreal automation tests for the game. Nor the game's half of the order on its
own, which `test_a_death_ends_the_dungeon.py` already holds; it is read again
here so that one failing test names both files.

WHAT THE TWO HALVES DO NOT SHARE, AND THIS FILE DOES NOT PRETEND THEY DO. A
death in the Cataclysm's own dungeon ends the run in the model and leaves the
player standing in the level in the game, where nothing puts such a dungeon on
the map yet. The model was left as it was, by instruction.

A LINE THAT IS A COMMENT DOES NOT COUNT, in either language. Both functions
name every call below in their comments.
"""

from __future__ import annotations

import inspect
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
EMPIRE_RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"

SWITCH = "Cataclysm.DeathKeepsThePlayerInTheDungeon"


def cpp_code_of(path: pathlib.Path, signature: str) -> str:
    """One C++ function's lines of code, with every comment line left out."""
    text = path.read_text(encoding="utf-8")
    start = text.find(signature)
    assert start != -1, f"{path.name} has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"
    return "\n".join(
        line for line in text[start:end].splitlines()
        if not line.strip().startswith("//")
    )


def python_code_of(function) -> str:
    """One Python function's code: no docstring, and nothing after a `#`."""
    source = inspect.getsource(function)
    if function.__doc__:
        source = re.sub(r'""".*?"""', "", source, count=1, flags=re.DOTALL)
    return "\n".join(line.split("#", 1)[0] for line in source.splitlines())


def position_of(code: str, call: str, where: str) -> int:
    """Where one call sits in a function's code. It must be there exactly once."""
    count = code.count(call)
    assert count == 1, (
        f"{where} holds {call!r} {count} times; this check expects one")
    return code.index(call)


def the_models_death_branch() -> str:
    """`_finish_current` from the death roll to the first line of a clear."""
    from cataclysm_sim.engine import Simulation

    code = python_code_of(Simulation._finish_current)
    roll = "if self.rng.random() < self.death_chance(d):"
    clear = "self.cleared += 1"
    assert code.count(roll) == 1 and code.count(clear) == 1, (
        "Simulation._finish_current no longer rolls for a death once and "
        "counts a clear once, so this check cannot find the death's branch")
    return code[code.index(roll):code.index(clear)]


def test_both_halves_resolve_then_leave_then_charge_the_days() -> None:
    model = the_models_death_branch()
    where = "the death branch of Simulation._finish_current"

    resolve = position_of(model, "self._resolve_on_death(d)", where)
    leave = position_of(model, "self.current = None", where)
    charge = position_of(model, "self.days_remaining = cfg.death_day_cost", where)

    assert resolve < leave, (
        "The model leaves the dungeon before the death resolves it. The game "
        "resolves while the player is still inside; the ruling of 2026-10-09 "
        "is resolve, leave, charge.")
    assert leave < charge, (
        "The model charges a death's days before the dungeon is left.")

    game = cpp_code_of(
        GAME_MODE, "bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()")
    where = "ACataclysmDungeonGameMode::EndTheDungeonForADeath"

    resolve = position_of(game, "->ResolveDungeonOnDeath(", where)
    leave = position_of(game, "LeaveEmpireDungeon();", where)
    charge = position_of(game, "->AdvanceDays(", where)

    assert resolve < leave < charge, (
        "The game no longer resolves, leaves and charges in that order, and "
        "the model was written to copy that order.")


def test_the_model_resolves_on_a_death_only_when_the_setting_is_on() -> None:
    model = the_models_death_branch()
    where = "the death branch of Simulation._finish_current"

    asked = position_of(model, "if cfg.death_resolves_dungeon:", where)
    resolve = position_of(model, "self._resolve_on_death(d)", where)

    assert asked < resolve, (
        "The model resolves a dungeon on a death before asking whether "
        "death_resolves_dungeon is on, so the model as it was before "
        "2026-10-12 can no longer be measured.")
    assert "self._resolve(" not in model, (
        "Simulation._finish_current calls _resolve itself. A death's resolve "
        "goes through _resolve_on_death, which asks the kind first.")


def test_both_halves_ask_the_kind_before_they_resolve() -> None:
    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._resolve_on_death)
    where = "Simulation._resolve_on_death"

    kind = position_of(model, "not d.resolves", where)
    resolve = position_of(model, "self._resolve(d)", where)

    assert kind < resolve, (
        "The model resolves on a death before asking whether the dungeon is a "
        "kind that resolves. For a Quest dungeon _resolve runs the relocation "
        "and draws from the campaign's stream, which belongs to its timer.")
    assert "return" in model[kind:resolve], (
        "The model asks whether the dungeon resolves and goes on to resolve "
        "it whatever the answer.")

    game = cpp_code_of(
        EMPIRE_RUN,
        "FCataclysmDayReport UCataclysmEmpireRun::ResolveDungeonOnDeath(")
    where = "UCataclysmEmpireRun::ResolveDungeonOnDeath"

    kind = position_of(game, "!Dungeon->Resolves()", where)
    resolve = position_of(game, "ResolveDungeon(DungeonId, Report);", where)

    assert kind < resolve, (
        "The game resolves on a death before asking the kind, and the model "
        "was written to copy the question.")


def test_neither_half_repeats_the_arithmetic_of_a_resolve() -> None:
    """One call to the timer's own function, and no damage worked out beside it."""
    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._resolve_on_death)
    for mark in ("defense_damage", "population_damage", "city.defense -=",
                 "city.population -=", "self.resolved", "times_resolved",
                 "self.rng"):
        assert mark not in model, (
            f"Simulation._resolve_on_death holds {mark!r}. What a death takes "
            "is what Simulation._resolve takes, by calling it; a second copy "
            "of that arithmetic is a second rule.")

    game = cpp_code_of(
        EMPIRE_RUN,
        "FCataclysmDayReport UCataclysmEmpireRun::ResolveDungeonOnDeath(")
    for mark in ("Map->Damage(", "DefenceDamage", "PopulationDamage",
                 "DungeonsDetonated", "RelocateQuestDungeon"):
        assert mark not in game, (
            f"UCataclysmEmpireRun::ResolveDungeonOnDeath holds {mark!r}. It "
            "reached the city only through ResolveDungeon when the model "
            "copied it.")


def test_the_designed_behaviour_is_the_default_in_both() -> None:
    from cataclysm_sim.config import TuningConfig

    declared = re.search(
        r"TAutoConsoleVariable<int32>\s+CVarDeathKeepsThePlayerInTheDungeon\(\s*"
        r'TEXT\("' + re.escape(SWITCH) + r'"\),\s*(-?\d+),',
        GAME_MODE.read_text(encoding="utf-8"))
    assert declared is not None, (
        f"The dungeon game mode no longer declares the console variable {SWITCH}.")

    game_resolves = declared.group(1) == "0"
    model_resolves = TuningConfig().death_resolves_dungeon

    assert game_resolves is True and model_resolves is True, (
        f"By default a death resolves an ordinary dungeon in the game: "
        f"{game_resolves} ({SWITCH} defaults to {declared.group(1)}), and in "
        f"the model: {model_resolves} (TuningConfig.death_resolves_dungeon). "
        "Both must be the designed behaviour.")
