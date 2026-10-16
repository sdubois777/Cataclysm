"""A Quest dungeon's timer hits its host city, in the model and in the game.

**BOTH HALVES HOLD THE RULE, AND THIS FILE HOLDS THEM TOGETHER.**

The owner, 2026-10-08, verbatim: "Quest dungeons will do their city/population
damage, and either refresh their timer and stay on that city, or potentially
move to an adjacent city." The model has the rule since 2026-10-13:
`TuningConfig.quest_timer_hits_city` defaults to True, and `Simulation._resolve`
calls `Simulation._quest_hit` for a Quest dungeon on a standing city before it
refreshes the timer and before the move. The size is the ordinary (`BASIC`) row
of the host city's tier at a scale of 1.0, times the tree's damage reduction.

The game has it since its layer of 2026-10-13. Until then this file was named
`test_a_quest_dungeons_timer_hits_in_the_model_and_not_yet_in_the_game.py` and
recorded the difference.

WHERE THE RULE LIVES IN THE GAME: inside the `if (!Dungeon->Resolves())` block
of `UCataclysmEmpireRun::ResolveDungeon`, for a Quest dungeon on a standing
city, before `RelocateQuestDungeon`: a `Map->Damage` of the ordinary row of the
HOST's tier (`UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic,
Host->Tier)`), and `CityFell` and a return when it fells the city.

WHAT IS HELD TOGETHER: both hit only a standing city; both size the hit from
the ordinary row of the HOST's tier and not from the dungeon's own row or the
tier its depth was rolled from; neither scales it by the Quest dungeon's
depth; both hit before the move; a hit that fells the city ends there in both;
both count the hit apart from an ordinary dungeon's.

WHERE THE HALVES DIFFER, AND IT IS NOT HELD HERE. The game has no empire tree,
so nothing reduces the hit there. The game's clock refills the timer before
the hit and the model refills it after; no hit reads the timer. The model's
fall spares the dungeon the player is inside and the game's does not.

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

IF_THE_GAME_CHANGES = (
    "The game's half of a Quest dungeon's timer hitting its host has changed. "
    "Both halves are held to one rule; the docstring at the top of this file "
    "lists what is held. If the game was changed on purpose, say so in "
    "docs/DECISIONS.md, change the model or record the difference, and "
    "change this check with it.")


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


def the_games_quest_block() -> str:
    """The game's code for a dungeon that is not ordinary: from the question
    to the first line of the ordinary path."""
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)
    asked = code.index("if (!Dungeon->Resolves())")
    return code[asked:code.index("const int32 CityId = Dungeon->CityId;")]


def test_the_game_hits_a_standing_host_before_the_move_and_a_fall_ends_it(
) -> None:
    """The game's half of
    `test_the_model_hits_before_it_refreshes_and_before_the_move`."""
    block = the_games_quest_block()
    where = "The Quest block of UCataclysmEmpireRun::ResolveDungeon"

    kind = "if (Dungeon->Type == ECataclysmDungeonType::Quest)"
    host = "const FCataclysmCity* Host = Map->Find(HostId);"
    stands = "if (Host != nullptr && !Host->bFallen)"
    row = "SpecFor(ECataclysmDungeonType::Basic, Host->Tier);"
    counted = "++QuestHits;"
    told = "OutReport.QuestHits.Add(HostId);"
    hit = ("Map->Damage(HostId, Ordinary.DefenceDamage, "
           "Ordinary.PopulationDamage)")
    fall = "CityFell(HostId, OutReport);"
    move = "RelocateQuestDungeon(DungeonId, OutReport);"
    marks = (kind, host, stands, row, counted, told, hit, fall, move)

    for mark in marks:
        assert block.count(mark) == 1, (
            f"{where} holds {mark!r} {block.count(mark)} times; this check "
            "expects one. " + IF_THE_GAME_CHANGES)
    places = [block.index(mark) for mark in marks]
    assert places == sorted(places), (
        f"{where} no longer asks the kind, finds the host, asks that it "
        "stands, reads the ordinary row of its tier, counts and reports the "
        "hit, deals it, fells the city, and only then moves the dungeon. "
        + IF_THE_GAME_CHANGES)
    assert "return;" in block[block.index(fall):block.index(move)], (
        f"{where} goes on to the move after the hit felled the city. The "
        "fall absorbed the dungeon; nothing is left to move and no draw may "
        "be taken. " + IF_THE_GAME_CHANGES)

    relocate = cpp_code_of(EMPIRE_RUN, RELOCATE)
    for call in ("Damage(", "Bite(", "CityFell("):
        assert call not in relocate, (
            f"UCataclysmEmpireRun::RelocateQuestDungeon now calls {call}. "
            "The hit belongs before the move, in ResolveDungeon. "
            + IF_THE_GAME_CHANGES)


def test_the_games_hit_reads_the_ordinary_row_of_the_host() -> None:
    """The game's half of
    `test_the_models_hit_reads_the_ordinary_row_of_the_host`."""
    block = the_games_quest_block()

    for not_this in ("BiteScale", "Dungeon->CityTier",
                     "Dungeon->DefenceDamage", "Dungeon->PopulationDamage",
                     "DungeonsDetonated", "Floors"):
        assert not_this not in block, (
            f"The Quest block of UCataclysmEmpireRun::ResolveDungeon reads "
            f"{not_this}. The hit is sized by the host's ordinary row at a "
            "scale of one, not by the dungeon's own fields, its depth or the "
            "tier its depth was rolled from, and it is counted in QuestHits "
            "and not in DungeonsDetonated. " + IF_THE_GAME_CHANGES)

    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._quest_hit)
    assert "self.quest_hits += 1" in model, (
        "Simulation._quest_hit no longer counts the hit in quest_hits, which "
        "is what the game's QuestHits is a port of.")


def test_the_game_still_answers_ordinary_and_nothing_else() -> None:
    """THE HIT DID NOT COME BY THE OTHER ROUTE, which would be `Resolves()`
    answering true for a Quest dungeon. That would also make a death in one
    resolve it, and scale its hit by its depth."""
    if not SURGE_HEADER.is_file():
        pytest.skip(f"{SURGE_HEADER.name} is not present")
    header = SURGE_HEADER.read_text(encoding="utf-8")

    assert ("bool Resolves() const { return Type == "
            "ECataclysmDungeonType::Basic; }") in header, (
        "FCataclysmDungeon::Resolves is no longer 'ordinary and nothing "
        "else'. " + IF_THE_GAME_CHANGES)


def test_the_games_function_is_still_the_one_that_was_read() -> None:
    """**THE CONTROL**: the function read is still the one with the ordinary
    path after the Quest block."""
    code = cpp_code_of(EMPIRE_RUN, RESOLVE)

    for mark in ("if (!Dungeon->Resolves())",
                 "RelocateQuestDungeon(DungeonId, OutReport);",
                 "Map->Damage(CityId, Defence, Population)",
                 "CityFell(CityId, OutReport);"):
        assert code.count(mark) == 1, (
            f"UCataclysmEmpireRun::ResolveDungeon holds {mark!r} "
            f"{code.count(mark)} times; this file reads that function to say "
            "how the game hits a city for a Quest dungeon, and it no longer "
            "looks like the function that was read")

    assert (code.index("if (!Dungeon->Resolves())")
            < code.index("RelocateQuestDungeon(DungeonId, OutReport);")
            < code.index("Map->Damage(CityId, Defence, Population)")
            < code.index("CityFell(CityId, OutReport);"))

    relocate = cpp_code_of(EMPIRE_RUN, RELOCATE)
    assert "Dungeon->CityId = MovingTo;" in relocate
