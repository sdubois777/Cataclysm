"""What a dungeon's hit is scaled by, in the model and in the game.

THE RULE, IN THE MODEL SINCE 2026-10-13 AND IN THE GAME SINCE THE LAYER THAT
ADDED `FCataclysmDungeon::UpgradeFloors`. The owner accepted the coordinating
session's recommendation on 2026-10-13, in that session's words: a dungeon's
hit is sized by its NATURAL floors, not the floors the Explorer tree adds.

BOTH HALVES NOW HOLD THE SAME RULE, AND THIS FILE HOLDS THEM TOGETHER.

  * THE MODEL. `Simulation._resolve` scales by `Dungeon.natural_floors`, which
    is `floors` minus `tree_floors`, the floors the empire tree added or
    removed.
  * THE GAME. `FCataclysmDungeon::BiteScale` divides `Floors` less
    `UpgradeFloors` by the midpoint of the floor range of
    `SpecFor(Type, CityTier)`. `UCataclysmSurgeScheduler::MakeDungeon` rolls
    the depth, keeps it, applies the city's two floor upgrades, and stores the
    difference in `UpgradeFloors`, after the clamp at one floor.
  * IN BOTH, the total floors still set the walk and the resolve timer.

WHERE THE HALVES DIFFER, AND WHY THAT IS NOT HELD EQUAL. The game has no
empire tree and the model has no city upgrades. The floors the game sets aside
are the ones `DungeonFloorsMore` and `DungeonFloorsFewer` moved. The ruling
names the tree; extending it to the two city upgrades is a labelled judgement
of the coordinating session, 2026-10-13, recorded in `docs/DECISIONS.md`. So
this file compares the two formulas and the order each is built in. It cannot
compare a figure.

A LINE THAT IS A COMMENT DOES NOT COUNT, in either language.
"""

from __future__ import annotations

import inspect
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
SURGE_CPP = EMPIRE / "CataclysmSurge.cpp"
SURGE_HEADER = EMPIRE / "CataclysmSurge.h"

WHEN_THE_GAME_CHANGES = (
    "The game's half of what a hit is scaled by has changed. Both halves are "
    "held to one rule: a hit is scaled by the floors a dungeon was rolled "
    "with, over the midpoint of the row the depth was rolled from, and the "
    "total floors still set the walk and the timer. If the game's half was "
    "changed on purpose, say so in docs/DECISIONS.md, change the model or "
    "record the difference, and change this check with it.")


def cpp_code(path: pathlib.Path) -> str:
    """A C++ file with its comments taken out and its whitespace flattened."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return " ".join(text.split())


def python_code_of(function) -> str:
    """One Python function's code: no docstring, and nothing after a `#`."""
    source = inspect.getsource(function)
    if function.__doc__:
        source = re.sub(r'""".*?"""', "", source, count=1, flags=re.DOTALL)
    return "\n".join(line.split("#", 1)[0] for line in source.splitlines())


# ------------------------------------------------------------------ the model

def test_the_model_sizes_a_hit_by_natural_floors_by_default() -> None:
    from cataclysm_sim.config import TuningConfig

    assert TuningConfig().hit_scales_with_natural_floors is True, (
        "The model's default scales a hit by the total floors again, which "
        "include what the empire tree adds. The recommendation the owner "
        "accepted on 2026-10-13 is that a hit is sized by natural floors. "
        "Set it False in a measurement, not in the default.")


def test_the_model_divides_natural_floors_by_the_rows_midpoint() -> None:
    from cataclysm_sim.engine import Dungeon, Simulation

    resolve = python_code_of(Simulation._resolve)
    assert resolve.count("scale = self._hit_floors(d) / typical") == 1, (
        "Simulation._resolve no longer scales an ordinary dungeon's hit by "
        "`self._hit_floors(d) / typical`.")
    assert "d.floors / typical" not in resolve
    assert "cfg.spec(d.dtype, d.city_tier).floors" in resolve, (
        "Simulation._resolve no longer takes the midpoint from the row the "
        "dungeon's depth was rolled from; the game's BiteScale reads "
        "SpecFor(Type, CityTier).")
    assert "typical = (lo + hi) / 2.0" in resolve

    asked = python_code_of(Simulation._hit_floors)
    assert ("d.natural_floors if cfg.hit_scales_with_natural_floors "
            "else d.floors") in asked

    natural = python_code_of(Dungeon.natural_floors.fget)
    assert "return self.floors - self.tree_floors" in natural


def test_the_model_takes_the_trees_floors_after_the_natural_depth() -> None:
    from cataclysm_sim.engine import Simulation

    code = python_code_of(Simulation._make_dungeon)
    marks = ("self.rng.randint(lo, hi) * floors_mult",
             "natural = floors",
             "cfg.tree.floors_added(",
             "tree_floors=floors - natural")
    for mark in marks:
        assert code.count(mark) == 1, (
            f"Simulation._make_dungeon holds {mark!r} {code.count(mark)} "
            "times; this check expects one")
    places = [code.index(mark) for mark in marks]
    assert places == sorted(places), (
        "Simulation._make_dungeon no longer rolls the depth with the "
        "pattern's multiplier, then records it as natural, then adds the "
        "tree's floors, then stores the difference as tree_floors.")


# ------------------------------------------------------------------- the game

def test_the_game_divides_the_rolled_floors_by_the_same_midpoint() -> None:
    code = cpp_code(SURGE_CPP)

    start = code.index("float FCataclysmDungeon::BiteScale() const")
    body = code[start:code.index("float FCataclysmDungeon::WalkDaysPerFloor()")]

    assert "UCataclysmSurgeScheduler::SpecFor(Type, CityTier)" in body
    assert ("const float Typical = (Spec.LeastFloors + Spec.MostFloors) "
            "/ 2.0f;") in body
    assert ("return Typical <= 0.0f ? 1.0f : (Floors - UpgradeFloors) "
            "/ Typical;") in body, (
        "FCataclysmDungeon::BiteScale no longer returns "
        "(Floors - UpgradeFloors) / Typical. " + WHEN_THE_GAME_CHANGES)
    assert ": Floors / Typical" not in body, (
        "FCataclysmDungeon::BiteScale divides the total floors again, which "
        "include what a city upgrade moved. " + WHEN_THE_GAME_CHANGES)


def test_the_game_keeps_the_rolled_depth_apart_from_what_an_upgrade_moved(
) -> None:
    """`MakeDungeon` rolls, keeps the roll, applies the two city upgrades,
    and stores the difference AFTER the clamp at one floor. The model's
    `_make_dungeon` does the same four things in the same order for the
    tree's floors; `test_the_model_takes_the_trees_floors_after_the_natural_depth`
    above is its half."""
    code = cpp_code(SURGE_CPP)

    roll = ("Dungeon.Floors = FMath::Max(1, Stream.RandRange("
            "Spec.LeastFloors, Spec.MostFloors));")
    kept = "const int32 Rolled = Dungeon.Floors;"
    moved = "Dungeon.Floors = FMath::Max(1, Dungeon.Floors + Deeper - Shallower);"
    stored = "Dungeon.UpgradeFloors = Dungeon.Floors - Rolled;"
    marks = (roll, kept, moved, stored)
    for mark in marks + ("ECataclysmCityUpgradeEffect::DungeonFloorsMore",
                         "ECataclysmCityUpgradeEffect::DungeonFloorsFewer"):
        assert code.count(mark) == 1, (
            f"CataclysmSurge.cpp holds {mark!r} {code.count(mark)} times; "
            "this check expects one. " + WHEN_THE_GAME_CHANGES)
    places = [code.index(mark) for mark in marks]
    assert places == sorted(places), (
        "MakeDungeon no longer rolls the depth, keeps it, applies the city's "
        "floor upgrades, and then stores the difference, in that order. "
        + WHEN_THE_GAME_CHANGES)
    assert code.count("Dungeon.UpgradeFloors =") == 1, (
        "Something else in CataclysmSurge.cpp sets UpgradeFloors. "
        + WHEN_THE_GAME_CHANGES)
    assert "Dungeon.UpgradeFloors = Deeper - Shallower" not in code, (
        "UpgradeFloors is taken before the clamp at one floor, so a clamped "
        "dungeon's hit is no longer sized by the floors it rolled.")

    header = cpp_code(SURGE_HEADER)
    assert header.count("int32 UpgradeFloors = 0;") == 1, (
        "FCataclysmDungeon::UpgradeFloors is gone or no longer starts at "
        "nought. Nought is what makes a hand-built dungeon, a Fallen City "
        "and an old save sized by all of their floors. "
        + WHEN_THE_GAME_CHANGES)
    for name in ("NaturalFloors", "RolledFloors", "TreeFloors"):
        assert name not in header and name not in code, (
            f"The game now has {name} beside UpgradeFloors. "
            + WHEN_THE_GAME_CHANGES)


def test_in_the_game_the_walk_and_the_timer_still_read_the_total_floors(
) -> None:
    """ONLY THE HIT. The model's walk and timer read `d.floors`; the game's
    read `Dungeon.Floors`, after the upgrades."""
    code = cpp_code(SURGE_CPP)

    stored = code.index("Dungeon.UpgradeFloors = Dungeon.Floors - Rolled;")
    walk = "Dungeon.Floors * UCataclysmDayClock::DaysPerFloor"
    timer = "UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors)"
    assert code.count(walk) == 2, (
        f"MakeDungeon works the walk from {walk!r} {code.count(walk)} times "
        "and two were read on 2026-10-13: a Cow Level and every other "
        "dungeon. " + WHEN_THE_GAME_CHANGES)
    assert code.count(timer) == 1, WHEN_THE_GAME_CHANGES
    assert code.index(walk) > stored and code.index(timer) > stored, (
        "The walk or the timer is worked before the city's floor upgrades "
        "are applied. " + WHEN_THE_GAME_CHANGES)

    made = code[code.index(
        "Dungeon.UpgradeFloors = Dungeon.Floors - Rolled;"):]
    assert "Rolled *" not in made and "ResolveDaysFor(Rolled)" not in made, (
        "The walk or the timer now reads the rolled depth. Only the hit "
        "does. " + WHEN_THE_GAME_CHANGES)

    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._make_dungeon)
    assert "natural" in model and "tree_floors=floors - natural" in model


def test_nothing_else_in_the_game_moves_a_dungeons_floors() -> None:
    """The roll, the city upgrades, and a Fallen City's depth. A fourth
    assignment would be a new source of floors, and whether it is natural
    would need saying."""
    code = cpp_code(SURGE_CPP)

    assert code.count("Dungeon.Floors =") == 3, (
        f"CataclysmSurge.cpp assigns Dungeon.Floors "
        f"{code.count('Dungeon.Floors =')} times and three were read on "
        "2026-10-13: the roll, the city's upgrades, and a Fallen City's "
        "depth. " + WHEN_THE_GAME_CHANGES)
    assert "Dungeon.Floors +=" not in code and "Dungeon.Floors -=" not in code
