"""A Quest dungeon whose city falls moves, in the model and in the game.

**BOTH HALVES HOLD THE RULE, AND THIS FILE HOLDS THEM TOGETHER.**

The owner, 2026-10-16, verbatim: "Quest dungeons don't go away when a city
falls. They just move to an adjacent city. So eventually they should be hitting
something." The model has the rule since that day: `Simulation._fall` sends
each Quest dungeon on the fallen city to `Simulation._quest_refuge` rather than
popping it. The game has it since 2026-10-21: `UCataclysmEmpireRun::CityFell`
sends each one to `UCataclysmSurgeScheduler::PickRefuge` rather than to
`RemoveDungeon`.

WHAT IS HELD TOGETHER: a Quest dungeon is not absorbed in either, while an
ordinary one still is; the refuge is an adjacent city first and then the
nearest by lanes, breadth first over every link; a dungeon carrying a Siege is
refused a city already holding one, at the same cap; the choice is one draw,
and none when nowhere qualifies, with no coin; the city falls before the moves,
and the moves' draws come before the Fallen City dungeon and the fall's surge;
the game's Dungeon City counts only what was absorbed, which is the design's
sentence.

WHERE THE HALVES DIFFER, AND IT IS NOT HELD HERE. The model's fall spares the
ordinary dungeon the player is inside and the game's absorbs it, issue #2399.
The model rolls a Fallen City's depth from its spec and the game derives it
from the count absorbed, issue #1341. The model records the day a dungeon
arrived on a city and the game has no such field.

EACH CHECK CARRIES ITS OWN CONTROL, marked as one, and the last test is the
control on all of them: the functions read are still the ones that were read.

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
SURGE = EMPIRE / "CataclysmSurge.cpp"
SURGE_HEADER = EMPIRE / "CataclysmSurge.h"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

CITY_FELL = "void UCataclysmEmpireRun::CityFell("
RELOCATE = "void UCataclysmEmpireRun::RelocateQuestDungeon("
PICK_REFUGE = "int32 UCataclysmSurgeScheduler::PickRefuge("
PICK_RELOCATION = "int32 UCataclysmSurgeScheduler::PickRelocation("
MAKE_FALLEN_CITY = (
    "FCataclysmDungeon UCataclysmSurgeScheduler::MakeFallenCityDungeon(")

IF_THE_GAME_CHANGES = (
    "The game's half of a Quest dungeon moving when its city falls has "
    "changed. Both halves are held to one rule; the docstring at the top of "
    "this file lists what is held. If the game was changed on purpose, say so "
    "in docs/DECISIONS.md, change the model or record the difference, and "
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


def flat(code: str) -> str:
    """Code with its whitespace flattened, so a wrapped statement is found."""
    return " ".join(code.split())


def python_code_of(function) -> str:
    """One Python function's code: no docstring, and nothing after a `#`."""
    source = inspect.getsource(function)
    if function.__doc__:
        source = re.sub(r'""".*?"""', "", source, count=1, flags=re.DOTALL)
    return "\n".join(line.split("#", 1)[0] for line in source.splitlines())


def once(code: str, mark: str, where: str) -> int:
    """Where `mark` is in `code`, which must hold it exactly once."""
    assert code.count(mark) == 1, (
        f"{where} holds {mark!r} {code.count(mark)} times; this check expects "
        "one")
    return code.index(mark)


def model_fall() -> str:
    from cataclysm_sim.engine import Simulation
    return python_code_of(Simulation._fall)


def model_refuge() -> str:
    from cataclysm_sim.engine import Simulation
    return python_code_of(Simulation._quest_refuge)


def model_resolve() -> str:
    from cataclysm_sim.engine import Simulation
    return python_code_of(Simulation._resolve)


def test_in_both_a_fall_moves_a_quest_dungeon_and_absorbs_an_ordinary_one(
) -> None:
    where = "Simulation._fall"
    code = model_fall()
    quest = once(code, "if d.dtype is DungeonType.QUEST:", where)
    refuge = once(code, "refuge = self._quest_refuge(d, city)", where)
    moved = once(code, "d.city_id = refuge.cid", where)
    popped = once(code, "self.dungeons.pop(d.did, None)", where)
    assert quest < refuge < moved < popped, (
        f"{where} no longer asks the kind, asks for a refuge and moves a Quest "
        "dungeon there before the line that absorbs a dungeon.")
    assert "continue" in code[moved:popped], (
        f"{where} goes on from a Quest dungeon's move to the line that absorbs "
        "it.")

    where = "UCataclysmEmpireRun::CityFell"
    code = flat(cpp_code_of(EMPIRE_RUN, CITY_FELL))
    quest = once(code, "if (Dungeon->Type == ECataclysmDungeonType::Quest)",
                 where)
    picked = once(code, "UCataclysmSurgeScheduler::PickRefuge(", where)
    moved = once(code, "Dungeon->CityId = Refuge;", where)
    relocated = once(code, "OutReport.Relocated.Add(DungeonId);", where)
    by_a_fall = once(code, "OutReport.MovedByAFall.Add(DungeonId);", where)
    absorbed = once(code, "OutReport.Absorbed.Add(DungeonId);", where)
    removed = once(code, "RemoveDungeon(DungeonId);", where)
    assert quest < picked < moved < absorbed < removed, (
        f"{where} no longer asks the kind, asks PickRefuge and moves a Quest "
        "dungeon before the lines that absorb a dungeon. "
        + IF_THE_GAME_CHANGES)
    assert moved < relocated < absorbed and moved < by_a_fall < absorbed, (
        f"{where} no longer names a fall's move in Relocated and MovedByAFall. "
        + IF_THE_GAME_CHANGES)
    assert "continue;" in code[by_a_fall:absorbed], (
        f"{where} goes on from a Quest dungeon's move to the lines that absorb "
        "it. " + IF_THE_GAME_CHANGES)

    # **THE CONTROL**: AN ORDINARY DUNGEON IS STILL ABSORBED IN BOTH. The
    # positions above put each half's absorbing line after its Quest branch;
    # this asks that neither half has narrowed the absorbing line to some
    # other kind, which would pass the order above while absorbing nothing.
    assert "Type != ECataclysmDungeonType::Quest" not in code
    assert "is not DungeonType.QUEST" not in model_fall()


def test_in_both_the_refuge_is_a_neighbour_first_then_the_nearest_by_lanes(
) -> None:
    where = "Simulation._quest_refuge"
    code = model_refuge()
    adjacent = once(code, "self.empire.adjacent_exposed_cities(fallen)", where)
    asked = once(code, "if not targets:", where)
    search = once(code, "while frontier and not targets:", where)
    links = once(code, "self.empire.neighbours(", where)
    assert adjacent < asked < search < links, (
        f"{where} no longer asks the neighbours first and then, when none "
        "qualifies, searches outward ring by ring over every link.")

    where = "UCataclysmSurgeScheduler::PickRefuge"
    code = flat(cpp_code_of(SURGE, PICK_REFUGE))
    adjacent = once(code, "AdjacentCities(Fallen)", where)
    search = once(code, "while (Targets.Num() == 0 && Frontier.Num() > 0)",
                  where)
    links = once(code, "AdjacentCities(*InnerCity)", where)
    ring = once(code, "Frontier = MoveTemp(Ring);", where)
    assert adjacent < search < links < ring, (
        f"{where} no longer asks the neighbours first and then, while nothing "
        "qualifies, searches outward ring by ring over every link. "
        + IF_THE_GAME_CHANGES)

    # **THE CONTROL**: THE TIMER'S MOVE SEARCHES NO FURTHER THAN THE
    # NEIGHBOURS, in either half. The outward search is the fall's alone, so a
    # search found above is the fall's rule and not a loop every move has.
    assert "neighbours(" not in model_resolve()
    assert "while" not in cpp_code_of(SURGE, PICK_RELOCATION)


def test_in_both_a_siege_is_refused_a_city_already_holding_one() -> None:
    from cataclysm_sim.config import TuningConfig

    where = "Simulation._quest_refuge"
    code = model_refuge()
    once(code, 'if d.subtype == "Siege" and self.sieges_on(c.cid) '
               '>= cfg.siege_max_per_city:', where)
    assert code.count("allowed(") == 3, (
        f"{where} no longer asks its Siege rule of both the neighbours and the "
        "outward search.")

    where = "UCataclysmSurgeScheduler::PickRefuge"
    code = flat(cpp_code_of(SURGE, PICK_REFUGE))
    once(code, "return !(bCarriesSiege && SiegesPerCityNow.IsValidIndex(CityId) "
               "&& SiegesPerCityNow[CityId] >= SiegesPerCity);", where)
    assert code.count("Qualifies(CityId)") == 2, (
        f"{where} no longer asks its rule of both the neighbours and the "
        "outward search. " + IF_THE_GAME_CHANGES)

    where = "UCataclysmEmpireRun::CityFell"
    code = flat(cpp_code_of(EMPIRE_RUN, CITY_FELL))
    once(code, "const bool bCarriesSiege = Dungeon->SubType == "
               "ECataclysmDungeonSubType::Siege;", where)
    once(code, "PickRefuge( *Map, *FallenCity, Stream, bCarriesSiege, "
               "SiegesStanding);", where)

    # **THE CONTROL**: THE CAP IS ONE NUMBER IN BOTH, so the two comparisons
    # above refuse at the same count.
    if not SURGE_HEADER.is_file():
        pytest.skip(f"{SURGE_HEADER.name} is not present")
    header = SURGE_HEADER.read_text(encoding="utf-8")
    cap = re.search(
        r"static\s+constexpr\s+int32\s+SiegesPerCity\s*=\s*(\d+)\s*;", header)
    assert cap, "SiegesPerCity is not in CataclysmSurge.h"
    assert int(cap.group(1)) == TuningConfig().siege_max_per_city


def test_in_both_the_choice_is_one_draw_with_no_coin_and_none_for_nowhere(
) -> None:
    where = "Simulation._quest_refuge"
    code = model_refuge()
    once(code, "return self.rng.choice(targets) if targets else None", where)
    assert code.count("self.rng.") == 1, (
        f"{where} draws on the stream somewhere other than its one choice.")

    where = "UCataclysmSurgeScheduler::PickRefuge"
    code = cpp_code_of(SURGE, PICK_REFUGE)
    nowhere, chosen = code.split("return INDEX_NONE;", 1)
    assert "Stream." not in nowhere, (
        f"{where} touches the stream before it knows there is somewhere to "
        "go. The model takes no draw when nowhere qualifies. "
        + IF_THE_GAME_CHANGES)
    once(chosen, "return Targets[Stream.RandRange(0, Targets.Num() - 1)];",
         where)
    assert code.count("Stream.") == 1, (
        f"{where} draws on the stream more than once. " + IF_THE_GAME_CHANGES)

    fell = cpp_code_of(EMPIRE_RUN, CITY_FELL)
    for coin in ("FRand(", "QuestMoveChance"):
        assert coin not in fell and coin not in code, (
            f"A fall's move now reads {coin}. The move is certain, ruled on "
            "2026-10-16. " + IF_THE_GAME_CHANGES)

    # **THE CONTROL**: THE TIMER'S MOVE DOES FLIP A COIN, in both, so "no
    # coin" above is a difference between the two moves and not a search for
    # something neither half has.
    assert "self.rng.random() < cfg.quest_move_chance" in model_resolve()
    assert "Stream.FRand() >= UCataclysmSurgeScheduler::QuestMoveChance" in (
        cpp_code_of(EMPIRE_RUN, RELOCATE))


def test_in_both_the_city_falls_then_the_moves_then_its_dungeon_and_surge(
) -> None:
    where = "Simulation._fall"
    code = model_fall()
    marks = ("city.fallen = True",
             "self._quest_refuge(d, city)",
             "self._make_dungeon(DungeonType.FALLEN_CITY, city)",
             "self.trigger_surge(from_city_fall=True)")
    places = [once(code, mark, where) for mark in marks]
    assert places == sorted(places), (
        f"{where} no longer marks the city fallen, then moves its Quest "
        "dungeons, then makes the Fallen City dungeon, then fires the surge, in "
        "that order.")

    where = "UCataclysmEmpireRun::CityFell"
    code = flat(cpp_code_of(EMPIRE_RUN, CITY_FELL))
    marks = ("UCataclysmSurgeScheduler::PickRefuge(",
             "AddFallenCityDungeon(CityId, DungeonsAbsorbed, OutReport);",
             "FireSurge(OutReport.Day,")
    places = [once(code, mark, where) for mark in marks]
    assert places == sorted(places), (
        f"{where} no longer moves its Quest dungeons before it makes the "
        "Fallen City dungeon and fires the fall's surge, so the draws come in "
        "a different order from the model's. " + IF_THE_GAME_CHANGES)

    # **THE CONTROL**: THE CITY IS ALREADY FALLEN WHEN THE GAME'S MOVES ARE
    # ASKED FOR, as it is in the model. Every call of `CityFell` follows a
    # `Map->Damage` that answered that the city fell, and
    # `UCataclysmEmpireMap::Fall` marks it before that answer.
    run = EMPIRE_RUN.read_text(encoding="utf-8")
    calls = re.findall(r"^\s*CityFell\(\w+, OutReport\);", run, flags=re.M)
    after_a_fall = re.findall(
        r"if \(Map->Damage\([^\n]*\)\)\s*\{\s*CityFell\(\w+, OutReport\);", run)
    assert len(calls) == 3 and len(after_a_fall) == len(calls), (
        f"CataclysmEmpireRun.cpp calls CityFell {len(calls)} times and "
        f"{len(after_a_fall)} of them follow a Map->Damage that answered that "
        "the city fell; this check expects three of three, so that the city "
        "reads as fallen when its Quest dungeons look for somewhere to go. "
        + IF_THE_GAME_CHANGES)


def test_the_games_dungeon_city_counts_only_what_it_absorbed() -> None:
    where = "UCataclysmEmpireRun::CityFell"
    code = flat(cpp_code_of(EMPIRE_RUN, CITY_FELL))
    quest_left = once(code, "OutReport.MovedByAFall.Add(DungeonId);", where)
    absorbed = once(code, "OutReport.Absorbed.Add(DungeonId);", where)
    counted = once(code, "++DungeonsAbsorbed;", where)
    assert quest_left < absorbed < counted, (
        f"{where} no longer counts a dungeon where it absorbs it, after the "
        "Quest branch. " + IF_THE_GAME_CHANGES)
    assert "Standing.Num()" not in code, (
        f"{where} hands the Dungeon City every dungeon that was standing, Quest "
        "dungeons included. " + IF_THE_GAME_CHANGES)

    made = flat(cpp_code_of(SURGE, MAKE_FALLEN_CITY))
    assert "const int32 Absorbed = FMath::Max(1, DungeonsAbsorbed);" in made, (
        "MakeFallenCityDungeon no longer gives a city that absorbed nothing one "
        "boss. Ruled by the coordinating session on 2026-10-21.")

    # **THE CONTROL**: THE DESIGN SAYS SO, and the design is what the game is
    # held to here; the model rolls a Fallen City's depth, issue #1341, and has
    # no count to compare.
    if not DESIGN.is_file():
        pytest.skip(f"{DESIGN.name} is not present")
    design = DESIGN.read_text(encoding="utf-8")
    assert ("when it fell and was absorbed by it** (a Quest dungeon moves away "
            "instead and is not counted)") in design


def test_the_functions_read_are_still_the_ones_that_were_read() -> None:
    """**THE CONTROL** on every check above: each function read still looks
    like the function that was read, so a check above is not passing on a
    function that has become something else."""
    fell = flat(cpp_code_of(EMPIRE_RUN, CITY_FELL))
    for mark in ("OutReport.Fallen.Add(CityId);",
                 "const TArray<int32> Standing = DungeonsOn(CityId);",
                 "if (Surges->IsDueOnCityFall(OutReport.Day))"):
        assert fell.count(mark) == 1, (
            f"UCataclysmEmpireRun::CityFell holds {mark!r} {fell.count(mark)} "
            "times; this file reads that function to say what a fall does")

    refuge = flat(cpp_code_of(SURGE, PICK_REFUGE))
    for mark in ("Map.PillarId", "Map.IsExposed(CityId)", "Seen[Next] = true;"):
        assert mark in refuge, (
            f"UCataclysmSurgeScheduler::PickRefuge no longer holds {mark!r}")

    fall = model_fall()
    assert "self._fall(" not in fall and "city.erased" in fall
    assert "self.empire.is_exposed(" in model_refuge()
