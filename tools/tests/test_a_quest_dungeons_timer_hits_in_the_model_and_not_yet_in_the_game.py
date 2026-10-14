"""A Quest dungeon's timer hits its host city in the model, and NOT YET in the game.

**THE TWO HALVES DIFFER ON PURPOSE, AND THIS FILE IS THE RECORD OF IT.**

The owner, 2026-10-08, verbatim: "Quest dungeons will do their city/population
damage, and either refresh their timer and stay on that city, or potentially
move to an adjacent city." The model has the rule since 2026-10-13:
`TuningConfig.quest_timer_hits_city` defaults to True, and `Simulation._resolve`
calls `Simulation._quest_hit` for a Quest dungeon on a standing city before it
refreshes the timer and before the move. The size is the ordinary (`BASIC`) row
of the host city's tier at a scale of 1.0, times the tree's damage reduction.

The game does not have it. `UCataclysmEmpireRun::ResolveDungeon` sends every
dungeon that is not ordinary to `RelocateQuestDungeon` and returns before any
damage. The rule goes into the game in a later layer, with the rest of the
package of 2026-10-13.

WHERE THE RULE WILL LIVE IN THE GAME: inside the `if (!Dungeon->Resolves())`
block of `ResolveDungeon`, for a Quest dungeon on a standing city, before
`RelocateQuestDungeon`: a `Map->Damage` of the ordinary row of the HOST's tier
(`UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic, <the host's
tier>)`), and `CityFell` when it fells the city.

THIS FILE EXISTS TO FAIL WHEN THE GAME GAINS THE RULE. Each failure message
says what to do then.

A LINE THAT IS A COMMENT DOES NOT COUNT, in either language.
"""

from __future__ import annotations

import inspect
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
EMPIRE_RUN = EMPIRE / "CataclysmEmpireRun.cpp"
SURGE_HEADER = EMPIRE / "CataclysmSurge.h"

RESOLVE = "void UCataclysmEmpireRun::ResolveDungeon("
RELOCATE = "void UCataclysmEmpireRun::RelocateQuestDungeon("

WHEN_THE_GAME_GAINS_IT = (
    "If the game now hits a city when a Quest dungeon's timer runs out, this "
    "divergence is over. Do these things: (1) delete this file's two "
    "'not yet' tests and replace them with ones that hold the halves "
    "together: both hit only a standing city, both size the hit from the "
    "ordinary row of the HOST's tier and not from the dungeon's own row or "
    "the tier its depth was rolled from, neither scales it by the Quest "
    "dungeon's depth, both hit BEFORE the timer is refreshed and before the "
    "move, and a fall absorbs the dungeon in both; (2) decide whether the "
    "game counts the hit in DungeonsDetonated, because the model counts it in "
    "quest_hits and NOT in resolved, and "
    "test_both_halves_count_a_detonation_where_the_city_pays in "
    "test_surge_port.py holds those two equal; (3) rename "
    "test_both_single_it_out_by_kind_and_only_the_game_lets_it_off in "
    "test_surge_port.py; (4) add a dated sentence to the docs/DECISIONS.md "
    "entry of 2026-10-13, and give docs/Cataclysm_GDD_v2.md the rule: its "
    "section 'Where a Quest Dungeon Moves To, and What It Takes With It' "
    "still says the timer costs the host city nothing.")


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


def test_in_the_model_a_quest_dungeons_timer_hits_by_default() -> None:
    from cataclysm_sim.config import TuningConfig

    assert TuningConfig().quest_timer_hits_city is True, (
        "The model's default no longer has a Quest dungeon's timer hit its "
        "city. The recommendation the owner accepted on 2026-10-13 is that it "
        "does. If this was switched off to match the game, do not: the two "
        "differ on purpose until the game gains the rule. Set it False in a "
        "measurement, not in the default.")
    assert TuningConfig().quest_hit_scales_with_depth is False, (
        "The model's default now scales a Quest dungeon's hit by its depth. "
        "The reading taken on 2026-10-13 is an ordinary dungeon's size, the "
        "ordinary row at a scale of 1.0.")


def test_the_model_hits_before_it_refreshes_and_before_the_move() -> None:
    from cataclysm_sim.engine import Simulation

    code = python_code_of(Simulation._resolve)
    where = "Simulation._resolve"

    marks = ("if d.dtype is DungeonType.QUEST:",
             "if cfg.quest_timer_hits_city and not city.fallen:",
             "self._quest_hit(d, city)",
             "if d.did not in self.dungeons:",
             "if cfg.quest_relocates:")
    for mark in marks:
        assert code.count(mark) == 1, (
            f"{where} holds {mark!r} {code.count(mark)} times; this check "
            "expects one")

    places = [code.index(mark) for mark in marks]
    refresh = code.index("d.resolve_in = float(d.resolve_max)", places[0])

    assert places == sorted(places) and places[3] < refresh < places[4], (
        f"{where} no longer asks the kind, then asks quest_timer_hits_city "
        "and whether the city stands, then hits, then stops if the fall "
        "absorbed the dungeon, then refreshes the timer, then moves, in that "
        "order.")


def test_the_models_hit_reads_the_ordinary_row_of_the_host() -> None:
    from cataclysm_sim.engine import Simulation

    code = python_code_of(Simulation._quest_hit)

    assert "cfg.spec(DungeonType.BASIC, city.tier)" in code, (
        "Simulation._quest_hit no longer reads the ordinary row of the tier "
        "of the city it was handed. The size ruled on 2026-10-13 is an "
        "ordinary dungeon's hit for the city the Quest dungeon stands on.")
    for not_this in ("d.city_tier", "d.defense_damage", "d.population_damage",
                     "self.resolved"):
        assert not_this not in code, (
            f"Simulation._quest_hit reads {not_this}. The hit is sized by "
            "the host's ordinary row, not by the dungeon's own fields or the "
            "tier its depth was rolled from, and it is counted in quest_hits "
            "and not in resolved.")
    assert "self._fall(city)" in code


def test_the_game_does_not_hit_a_city_for_a_quest_dungeon_yet() -> None:
    """**THIS TEST EXISTS TO FAIL WHEN THE GAME GAINS THE RULE.**

    The game's block for a dungeon that is not ordinary runs from
    `if (!Dungeon->Resolves())` to its `return;`. Today it holds the call to
    `RelocateQuestDungeon` and nothing that damages a city, and the one
    `Map->Damage` in the function comes after it.
    """
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)

    asked = code.index("if (!Dungeon->Resolves())")
    block = code[asked:code.index("return;", asked)]

    assert "RelocateQuestDungeon(DungeonId, OutReport);" in block
    for call in ("Damage(", "Bite(", "CityFell("):
        assert call not in block, (
            f"UCataclysmEmpireRun::ResolveDungeon now calls {call} for a "
            "dungeon that is not ordinary. " + WHEN_THE_GAME_GAINS_IT)

    relocate = cpp_code_of(EMPIRE_RUN, RELOCATE)
    for call in ("Damage(", "Bite(", "CityFell("):
        assert call not in relocate, (
            f"UCataclysmEmpireRun::RelocateQuestDungeon now calls {call}. "
            + WHEN_THE_GAME_GAINS_IT)


def test_the_game_still_answers_ordinary_and_nothing_else() -> None:
    """**ALSO FAILS WHEN THE GAME GAINS THE RULE BY THE OTHER ROUTE**, which
    is `Resolves()` starting to answer true for a Quest dungeon."""
    if not SURGE_HEADER.is_file():
        pytest.skip(f"{SURGE_HEADER.name} is not present")
    header = SURGE_HEADER.read_text(encoding="utf-8")

    assert ("bool Resolves() const { return Type == "
            "ECataclysmDungeonType::Basic; }") in header, (
        "FCataclysmDungeon::Resolves is no longer 'ordinary and nothing "
        "else'. " + WHEN_THE_GAME_GAINS_IT)


def test_the_games_function_is_still_the_one_that_was_read() -> None:
    """**THE CONTROL ON THE 'NOT YET' TEST**, which a renamed or emptied
    function would pass."""
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)

    for mark in ("if (!Dungeon->Resolves())",
                 "RelocateQuestDungeon(DungeonId, OutReport);",
                 "Map->Damage(CityId, Defence, Population)",
                 "CityFell(CityId, OutReport);"):
        assert code.count(mark) == 1, (
            f"UCataclysmEmpireRun::ResolveDungeon holds {mark!r} "
            f"{code.count(mark)} times; this file reads that function to say "
            "the game does not yet hit a city for a Quest dungeon, and it no "
            "longer looks like the function that was read")

    assert (code.index("if (!Dungeon->Resolves())")
            < code.index("RelocateQuestDungeon(DungeonId, OutReport);")
            < code.index("Map->Damage(CityId, Defence, Population)")
            < code.index("CityFell(CityId, OutReport);"))

    relocate = cpp_code_of(EMPIRE_RUN, RELOCATE)
    assert "Dungeon->CityId = MovingTo;" in relocate
