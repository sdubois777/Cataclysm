"""An ordinary dungeon leaves after it hits its city in the model, and NOT YET in the game.

**THE TWO HALVES DIFFER ON PURPOSE, AND THIS FILE IS THE RECORD OF IT.**

The owner ruled on 2026-10-08, verbatim: "Ordinary dungeons will do their
city/population damage, and then despawn." The model has the rule since
2026-10-12: `TuningConfig.dungeon_persists_after_resolve` defaults to False and
`Simulation._resolve` removes an ordinary dungeon that hit a standing city. The
game does not have it. It goes into the game only together with retuned
numbers, because with the rule on and nothing retuned an empire nobody defends
stops losing at the lower difficulty tiers; `docs/DECISIONS.md`, 2026-10-12,
has the measurement.

WHERE THE RULE WILL LIVE IN THE GAME: one `RemoveDungeon(DungeonId)` call at
the end of `UCataclysmEmpireRun::ResolveDungeon`, after the damage and the
possible `CityFell`. Both resolve paths, a timer and a death, go through that
function, and it is reached at that point only for an ordinary dungeon on a
standing city.

WHERE IT WILL NOT LIVE: `UCataclysmDayClock::bDungeonPersistsAfterResolve`.
That constant's false branch removes EVERY timer that ran out, whatever the
kind of dungeon, so it would strip the timers of Quest dungeons and Dungeon
Cities while those dungeons stay listed. `tools/tests/test_day_clock_port.py`
compared that constant with the model's setting until 2026-10-12 and no longer
does; a check that held them equal would force that flip. It now holds the
constant at true on its own.

THIS FILE EXISTS TO FAIL WHEN THE GAME GAINS THE RULE, the way
`test_the_game_has_no_board_empty_trigger_yet` in `test_surge_port.py` existed
to fail when issue #1414 landed. Each failure message says what to do then.

A LINE THAT IS A COMMENT DOES NOT COUNT, in either language.
"""

from __future__ import annotations

import inspect
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
EMPIRE_RUN = EMPIRE / "Empire" / "CataclysmEmpireRun.cpp"

RESOLVE = "void UCataclysmEmpireRun::ResolveDungeon("


def cpp_code_of(path: pathlib.Path, signature: str) -> str:
    """One C++ function's lines of code, with every comment line left out."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    count = text.count(signature)
    assert count == 1, f"{path.name} holds {signature!r} {count} times"
    start = text.index(signature)
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


def test_in_the_model_a_resolved_ordinary_dungeon_leaves_by_default() -> None:
    from cataclysm_sim.config import TuningConfig

    assert TuningConfig().dungeon_persists_after_resolve is False, (
        "The model's default keeps a resolved ordinary dungeon on the map "
        "again. The owner ruled on 2026-10-08 that it does its damage and "
        "then despawns. If this was turned back on to match the game, do not: "
        "the two differ on purpose until the game gains the rule with retuned "
        "numbers. Set the setting True in a measurement, not in the default.")


def test_the_model_removes_it_last_and_only_after_the_citys_fall() -> None:
    """Where the model's rule sits, which is where the game's will.

    THE FALL FIRST. A hit that fells the city makes `_fall` absorb every
    dungeon on it, this one included, so the fall decides what becomes of the
    dungeon and the removal below finds nothing to remove.
    """
    from cataclysm_sim.engine import Simulation

    code = python_code_of(Simulation._resolve)
    where = "Simulation._resolve"

    for mark in ("if cfg.dungeon_persists_after_resolve:",
                 "self.dungeons.pop(d.did, None)",
                 "self._fall(city)",
                 "if not d.resolves or city.fallen:"):
        assert code.count(mark) == 1, (
            f"{where} holds {mark!r} {code.count(mark)} times; this check "
            "expects one")

    kind = code.index("if not d.resolves or city.fallen:")
    fall = code.index("self._fall(city)")
    asked = code.index("if cfg.dungeon_persists_after_resolve:")
    removed = code.index("self.dungeons.pop(d.did, None)")

    assert kind < fall < asked < removed, (
        f"{where} no longer asks the kind and the city, then fells the city, "
        "then asks dungeon_persists_after_resolve, then removes the dungeon, "
        "in that order. A Quest dungeon, a Fallen City dungeon and a dungeon "
        "on a fallen city must return before the removal, and the fall must "
        "absorb the dungeon that fells its city.")
    assert "return" in code[kind:fall], (
        f"{where} asks whether the dungeon resolves on a standing city and "
        "goes on whatever the answer, so the removal reaches kinds that do "
        "not leave.")
    assert code[removed:].strip() == "self.dungeons.pop(d.did, None)", (
        f"{where} does something after removing the dungeon. The removal is "
        "the last thing a resolve does.")


def test_the_game_does_not_remove_a_resolved_dungeon_yet() -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME GAINS THE RULE.**"""
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)

    assert "RemoveDungeon(" not in code, (
        "UCataclysmEmpireRun::ResolveDungeon now calls RemoveDungeon, so the "
        "game removes a dungeon when it resolves and this divergence is over. "
        "Do these things: (1) delete this test and replace it with one that "
        "holds the two halves together: the call is RemoveDungeon(DungeonId), "
        "it is there once, it comes after CityFell and after both early "
        "returns, and TuningConfig().dungeon_persists_after_resolve is False; "
        "(2) check the game's numbers are the retuned ones, because the rule "
        "was to go into the game only with them (surge size and timing, and "
        "dungeon damage if needed), and that "
        "Cataclysm.EmpireRun.AnUnattendedEmpireAlwaysLosesThePathToThePillar "
        "still passes; (3) add a dated sentence to the docs/DECISIONS.md "
        "entry of 2026-10-12 that says the game does not have the rule; (4) "
        "ResolveDungeonOnDeath's refill then finds no timer for an ordinary "
        "dungeon, and tools/tests/test_a_death_ends_the_dungeon.py requires "
        "that refill line, so settle both together.")


def test_the_game_still_bites_and_fells_in_the_function_the_rule_will_join(
) -> None:
    """**THE CONTROL ON THE TEST ABOVE**, which a renamed or emptied function
    would pass. The two calls the removal has to follow are still there."""
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)

    for mark in ("Map->Damage(CityId, Defence, Population)",
                 "CityFell(CityId, OutReport);",
                 "RelocateQuestDungeon(DungeonId, OutReport);"):
        assert code.count(mark) == 1, (
            f"UCataclysmEmpireRun::ResolveDungeon holds {mark!r} "
            f"{code.count(mark)} times; this file reads that function to say "
            "the game does not yet remove a resolved dungeon, and it no "
            "longer looks like the function that was read")

    assert code.rstrip().endswith("}"), (
        "UCataclysmEmpireRun::ResolveDungeon no longer ends with the block "
        "that fells the city")
    assert (code.index("RelocateQuestDungeon(DungeonId, OutReport);")
            < code.index("Map->Damage(CityId, Defence, Population)")
            < code.index("CityFell(CityId, OutReport);"))

