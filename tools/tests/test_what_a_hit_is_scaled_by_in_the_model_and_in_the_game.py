"""What a dungeon's hit is scaled by, in the model and in the game.

THE RULE, IN THE MODEL SINCE 2026-10-13. The owner accepted the coordinating
session's recommendation that day, in that session's words: a dungeon's hit is
sized by its NATURAL floors, not the floors the Explorer tree adds.
`Simulation._resolve` scales by `Dungeon.natural_floors`, which is `floors`
minus `tree_floors`, the floors the empire tree added or removed.

WHAT THE GAME DOES, READ ON 2026-10-13.

  * `FCataclysmDungeon::BiteScale` divides `Floors` by the midpoint of the
    floor range of `SpecFor(Type, CityTier)`. The model divides by the same
    midpoint of the same row.
  * THE GAME HAS NO EMPIRE TREE, so nothing in it adds the floors the ruling
    names. In a city with no floor upgrade, `Floors` is the rolled depth and
    the two halves scale by the same thing.
  * THE GAME DOES HAVE TWO CITY UPGRADES THAT MOVE A DUNGEON'S FLOORS AFTER
    THE ROLL, `DungeonFloorsMore` and `DungeonFloorsFewer`, in
    `UCataclysmSurgeScheduler::MakeDungeon`. `BiteScale` reads `Floors` after
    them. So in a city holding either, the game sizes a hit by floors a
    player's investment moved, which is what the model stopped doing. The
    model has no city upgrades.

SO THE TWO HALVES AGREE TODAY ONLY WHERE NO UPGRADE MOVED THE FLOORS, AND
THAT IS RECORDED HERE RATHER THAN HELD EQUAL. Whether a city upgrade's floors
are "natural" is not ruled: the ruling names the tree. It is for the layer that
takes the rule into the game.

THIS FILE FAILS WHEN EITHER HALF CHANGES WHAT IT SCALES BY. Each failure
message says what to do.

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
    "The game's half of what a hit is scaled by has changed. If the game now "
    "keeps a dungeon's natural floors apart from floors a player's "
    "investment added, do these things: (1) replace the three tests about "
    "the game in this file with ones that hold the halves together: both "
    "scale a hit by the natural floors over the midpoint of the row the "
    "depth was rolled from, and in both the total floors still set the walk "
    "and the timer; (2) the ruling of 2026-10-13 names the empire tree's "
    "floors and the game's floor changes come from two CITY upgrades, "
    "DungeonFloorsMore and DungeonFloorsFewer: say in docs/DECISIONS.md "
    "which reading the game took for them; (3) add a dated sentence to the "
    "docs/DECISIONS.md entry of 2026-10-13.")


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

def test_the_game_divides_floors_by_the_same_midpoint() -> None:
    code = cpp_code(SURGE_CPP)

    start = code.index("float FCataclysmDungeon::BiteScale() const")
    body = code[start:code.index("float FCataclysmDungeon::WalkDaysPerFloor()")]

    assert "UCataclysmSurgeScheduler::SpecFor(Type, CityTier)" in body
    assert ("const float Typical = (Spec.LeastFloors + Spec.MostFloors) "
            "/ 2.0f;") in body
    assert "return Typical <= 0.0f ? 1.0f : Floors / Typical;" in body, (
        "FCataclysmDungeon::BiteScale no longer returns Floors / Typical. "
        + WHEN_THE_GAME_CHANGES)


def test_the_games_floors_include_what_a_city_upgrade_moved() -> None:
    """**THE RECORDED DIFFERENCE.** After the roll, `MakeDungeon` adds and
    removes floors for the city's upgrades, into the same `Floors` that
    `BiteScale` reads."""
    code = cpp_code(SURGE_CPP)

    roll = ("Dungeon.Floors = FMath::Max(1, Stream.RandRange("
            "Spec.LeastFloors, Spec.MostFloors));")
    moved = "Dungeon.Floors = FMath::Max(1, Dungeon.Floors + Deeper - Shallower);"
    for mark in (roll, moved,
                 "ECataclysmCityUpgradeEffect::DungeonFloorsMore",
                 "ECataclysmCityUpgradeEffect::DungeonFloorsFewer"):
        assert code.count(mark) == 1, (
            f"CataclysmSurge.cpp holds {mark!r} {code.count(mark)} times; "
            "this check expects one. " + WHEN_THE_GAME_CHANGES)
    assert code.index(roll) < code.index(moved)

    header = cpp_code(SURGE_HEADER)
    for name in ("NaturalFloors", "RolledFloors", "TreeFloors",
                 "UpgradeFloors"):
        assert name not in header and name not in code, (
            f"The game now has {name}. " + WHEN_THE_GAME_CHANGES)


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
