"""An ordinary dungeon leaves after it hits its city, in the model and in the game.

**BOTH HALVES HOLD THE RULE, AND THIS FILE HOLDS THEM TOGETHER.**

The owner ruled on 2026-10-08, verbatim: "Ordinary dungeons will do their
city/population damage, and then despawn." The model has the rule since
2026-10-12: `TuningConfig.dungeon_persists_after_resolve` defaults to False and
`Simulation._resolve` removes an ordinary dungeon that hit a standing city. The
game has it since its layer of 2026-10-13, which went in together with the
retuned surge (5 dungeons every 90 days) and with a Quest dungeon's timer
hitting its city. Until then this file was named
`test_an_ordinary_dungeon_leaves_in_the_model_and_not_yet_in_the_game.py` and
recorded the difference.

WHERE THE RULE LIVES IN THE GAME: one `RemoveDungeon(DungeonId)` call at the
end of `UCataclysmEmpireRun::ResolveDungeon`, after the damage and the
possible `CityFell`. Both resolve paths, a timer and a death, go through that
function, and it is reached at that point only for an ordinary dungeon on a
standing city.

WHERE IT DOES NOT LIVE: `UCataclysmDayClock::bDungeonPersistsAfterResolve`.
That constant's false branch removes EVERY timer that ran out, whatever the
kind of dungeon, so it would strip the timers of Quest dungeons and Dungeon
Cities while those dungeons stay listed. `tools/tests/test_day_clock_port.py`
holds the constant at true on its own.

WHERE THE HALVES STILL DIFFER, AND IT IS NOT HELD HERE. A city's fall absorbs
the dungeon the player is inside in the game and spares it in the model.

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
        "then despawns, and the game removes it too. Set the setting True in "
        "a measurement, not in the default.")


def test_the_model_removes_it_last_and_only_after_the_citys_fall() -> None:
    """Where the model's rule sits, which is where the game's does.

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


def test_the_game_removes_it_last_and_only_after_the_citys_fall() -> None:
    """The game's half of the test above, mark for mark.

    THE FALL FIRST. A hit that fells the city makes `CityFell` absorb every
    dungeon on it, this one included, and `RemoveDungeon` then answers false,
    so the day's report names the dungeon as absorbed and not as left.
    """
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)
    where = "UCataclysmEmpireRun::ResolveDungeon"

    kind = "if (!Dungeon->Resolves())"
    fallen = "if (City == nullptr || City->bFallen)"
    counted = "++DungeonsDetonated;"
    hit = "Map->Damage(CityId, Defence, Population)"
    fall = "CityFell(CityId, OutReport);"
    removed = "if (RemoveDungeon(DungeonId))"
    told = "OutReport.HitAndLeft.Add(CityId);"
    marks = (kind, fallen, counted, hit, fall, removed, told)

    for mark in marks:
        assert code.count(mark) == 1, (
            f"{where} holds {mark!r} {code.count(mark)} times; this check "
            "expects one")
    places = [code.index(mark) for mark in marks]
    assert places == sorted(places), (
        f"{where} no longer asks the kind, then the city, then counts, hits "
        "and fells, then removes the dungeon and reports the city, in that "
        "order. A Quest dungeon, a Fallen City dungeon and a dungeon on a "
        "fallen city must return before the removal, and the fall must "
        "absorb the dungeon that fells its city.")

    assert code.count("RemoveDungeon(") == 1, (
        f"{where} removes a dungeon in {code.count('RemoveDungeon(')} places. "
        "One removal, at the end, is the rule.")
    assert "ClearDungeon(" not in code, (
        f"{where} clears the dungeon that hit its city. A dungeon that leaves "
        "is not a clear: nothing is counted as cleared and no objective is "
        "earned.")
    assert "return;" in code[code.index(kind):code.index(fallen)], (
        f"{where} asks whether the dungeon is ordinary and goes on whatever "
        "the answer, so the removal reaches kinds that do not leave.")
    assert "return;" in code[code.index(fallen):code.index(counted)], (
        f"{where} no longer returns for a city that has already fallen, so "
        "a dungeon that hit nothing would be removed.")
    assert "return;" not in code[code.index(counted):], (
        f"{where} can return between counting the hit and removing the "
        "dungeon, so a dungeon that hit could stay.")

    from cataclysm_sim.config import TuningConfig

    assert TuningConfig().dungeon_persists_after_resolve is False


def test_the_game_still_bites_and_fells_in_the_function_the_rule_joined(
) -> None:
    """**THE CONTROL ON THE TEST ABOVE**: the function read is still the one
    that hits the city and sends a Quest dungeon to its move."""
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)

    for mark in ("Map->Damage(CityId, Defence, Population)",
                 "CityFell(CityId, OutReport);",
                 "RelocateQuestDungeon(DungeonId, OutReport);"):
        assert code.count(mark) == 1, (
            f"UCataclysmEmpireRun::ResolveDungeon holds {mark!r} "
            f"{code.count(mark)} times; this file reads that function to say "
            "where the game removes a dungeon that hit its city, and it no "
            "longer looks like the function that was read")

    assert (code.index("RelocateQuestDungeon(DungeonId, OutReport);")
            < code.index("Map->Damage(CityId, Defence, Population)")
            < code.index("CityFell(CityId, OutReport);"))

