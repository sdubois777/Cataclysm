"""The Hollow, the Void's Quest dungeon, in the model and in the game.

**BOTH HALVES HOLD THE RULE, AND THIS FILE HOLDS THEM TOGETHER.**

The owner, 2026-10-16, on the Void's Quest dungeons, verbatim: "So, maybe
Hollow works, but we shouldn't just flat out erase a city if they player can't
get to it in time." The model has the Hollow since 2026-10-18
(`Simulation.hollow_floor_minimum`, `_hollow_stacks_for`, `hollow_hit_stacks`,
`_hollow_shrink`, and the Hollow branch of `_quest_hit`). The game has it since
2026-10-22: `FCataclysmDungeon::HollowFrom`, `IsHollow`, `HollowMinimum`,
`HollowFloorsLeftToLose` and `HollowStacksOnto`, the three
`UCataclysmSurgeScheduler::Hollow*` constants, `UCataclysmEmpireRun::
ShrinkHollows`, and the Hollow's half of the Quest block of `ResolveDungeon`.

WHAT IS COMPARED, AND HOW EACH CHECK FAILS:

- **The minimum.** The game's `HollowMinimum` is read for its numbers and its
  share, and run against `Simulation.hollow_floor_minimum` for every depth a
  Hollow can be made with from 1 to 60. Fails when the share or the floor of
  one moves on either side; `test_the_minimum_comparison_sees_a_changed_share`
  shows it does.
- **The stacks.** The game's `HollowStacksOnto` is read for its numbers -- the
  3 a Hollow that cannot shrink adds, the 1, the 2 and the half that rounds it,
  the nought `lost` is held above, the one short of ten the cap leaves, the
  nought it never goes below -- and run, in single precision as the game does
  it, against `Simulation.hollow_hit_stacks` for every Hollow made with 1 to 60
  floors, at every depth from three below its minimum to three above what it
  was made with, on a city carrying 0 to 11 stacks. Fails when any of those
  numbers moves on either side; the two stacks controls show it does.
- **The shrink.** How many floors a Hollow loses in a day, the game's
  `HollowFloorsLostPerDay` and `HollowFloorsLeftToLose` against one day of the
  model's own `_hollow_shrink`, for the same depths and a city's upgrade
  having added or taken up to five floors.
- **The constants.** The share kept, the floors lost a day and the line below
  which a hit cannot erase, against `TuningConfig`; ten against
  `void_stacks_to_erase` is held by
  `tools/tests/test_void_stacks_in_the_model_and_in_the_game.py`.
- **The place in the day, and who is left alone.** Both halves shrink after the
  Siege bites and before any timer is acted on, and both skip a Hollow made
  today and the one the player is inside. Read from both sources, and the
  model's skips are run as well.
- **Who is a Hollow, and the hit.** A Quest dungeon whose sub-type is Hollow,
  in both; `MakeDungeon` remembers its plain natural floors as the model's
  `_make_dungeon` does; and the Quest hit adds the stacks instead of damage on
  both sides.

A LINE THAT IS A COMMENT DOES NOT COUNT, in either language.
"""

from __future__ import annotations

import functools
import inspect
import math
import pathlib
import re
import struct
from types import SimpleNamespace

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
MAP_H = EMPIRE / "CataclysmEmpireMap.h"
SURGE_H = EMPIRE / "CataclysmSurge.h"
SURGE_CPP = EMPIRE / "CataclysmSurge.cpp"
EMPIRE_RUN = EMPIRE / "CataclysmEmpireRun.cpp"

MINIMUM = "int32 FCataclysmDungeon::HollowMinimum() const"
LEFT_TO_LOSE = "int32 FCataclysmDungeon::HollowFloorsLeftToLose() const"
STACKS = "int32 FCataclysmDungeon::HollowStacksOnto(int32 CityStacks) const"
IS_HOLLOW = "bool FCataclysmDungeon::IsHollow() const"
MAKE = "FCataclysmDungeon UCataclysmSurgeScheduler::MakeDungeon("
ADVANCE = "FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay()"
SHRINK = "void UCataclysmEmpireRun::ShrinkHollows(int32 Today)"
RESOLVE = "void UCataclysmEmpireRun::ResolveDungeon("

#: Every depth a Hollow is checked as having been made with.
MADE_WITH = range(1, 61)


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def code_lines(text: str) -> str:
    """Every line that is not a `//` comment line. The functions read here
    hold no block comments."""
    return "\n".join(
        line for line in text.splitlines()
        if not line.strip().startswith("//")
    )


def cpp_code_of(text: str, signature: str) -> str:
    """One C++ function's code, comment lines left out, whitespace collapsed."""
    count = text.count(signature)
    assert count == 1, f"the source holds {signature!r} {count} times"
    start = text.index(signature)
    end = text.find("\n}\n", start)
    if end == -1:
        end = text.find("\r\n}\r\n", start)
    assert end != -1, f"{signature!r} is not closed"
    return re.sub(r"\s+", " ", code_lines(text[start:end]))


def python_code_of(function) -> str:
    """One Python function's code: no docstring, and nothing after a `#`."""
    source = inspect.getsource(function)
    if function.__doc__:
        source = re.sub(r'""".*?"""', "", source, count=1, flags=re.DOTALL)
    return re.sub(r"\s+", " ", "\n".join(
        line.split("#", 1)[0] for line in source.splitlines()))


def f32(value: float) -> float:
    """A value as a single-precision float holds it, which is how the game
    works the share and the stacks out."""
    return struct.unpack("f", struct.pack("f", value))[0]


def header_constant(header: str, kind: str, name: str) -> float:
    found = re.search(
        rf"static constexpr {kind} {name} = ([\d.]+)f?;", header)
    assert found, f"no 'static constexpr {kind} {name}' in the header; renamed?"
    return float(found.group(1))


# ---------------------------------------------------------------------------
# The model, asked as the game would ask it
# ---------------------------------------------------------------------------

@functools.lru_cache(maxsize=1)
def the_model():
    """An object carrying the model's Hollow methods and its default config,
    without building a whole campaign. ONE, SHARED: a caller that runs the
    shrink sets `dungeons`, `day` and `current` itself before it does."""
    from cataclysm_sim.config import TuningConfig
    from cataclysm_sim.engine import Simulation

    class Owner:
        cfg = TuningConfig()
        hollow_floor_minimum = Simulation.hollow_floor_minimum
        _hollow_stacks_for = Simulation._hollow_stacks_for
        hollow_hit_stacks = Simulation.hollow_hit_stacks
        _hollow_shrink = Simulation._hollow_shrink

        def __init__(self):
            self.dungeons = {}
            self.day = 1
            self.current = None

        def _walk_days(self, floors, subtype):
            return floors

    return Owner()


def a_hollow(made_with: int, floors: int, tree_floors: int = 0,
             spawned_day: int = 0):
    """A real model `Dungeon` that is a Hollow."""
    from cataclysm_sim.config import CityTier, DungeonType
    from cataclysm_sim.engine import Dungeon

    return Dungeon(did=1, dtype=DungeonType.QUEST, city_id=0,
                   city_tier=CityTier.OUTPOST, floors=floors, run_days=floors,
                   resolve_max=30, resolve_in=30.0, defense_damage=0.0,
                   population_damage=0.0, spawned_day=spawned_day,
                   subtype="Hollow", tree_floors=tree_floors,
                   hollow_from=made_with)


# ---------------------------------------------------------------------------
# The game's arithmetic, read out of its source
# ---------------------------------------------------------------------------

def the_games_minimum(header: str, source: str):
    share = header_constant(header, "float", "HollowShareKept")
    code = cpp_code_of(source, MINIMUM)
    found = re.search(
        r"return FMath::Max\((\d+), FMath::CeilToInt\( ?"
        r"HollowFrom \* UCataclysmSurgeScheduler::HollowShareKept\)\);", code)
    assert found, ("FCataclysmDungeon::HollowMinimum no longer answers "
                   "max(N, ceil(HollowFrom x HollowShareKept))")
    least = int(found.group(1))

    def minimum(made_with: int) -> int:
        return max(least, math.ceil(f32(made_with * f32(share))))

    return minimum


def the_games_stacks(header: str, source: str, map_header: str):
    minimum = the_games_minimum(header, source)
    below = int(header_constant(header, "int32", "HollowCannotEraseBelow"))
    found_ten = re.search(r"static constexpr int32 VoidStacksToErase = (\d+);",
                          map_header)
    assert found_ten, "CataclysmEmpireMap.h no longer declares VoidStacksToErase"
    ten = int(found_ten.group(1))

    code = cpp_code_of(source, STACKS)
    where = "FCataclysmDungeon::HollowStacksOnto"

    assert "const int32 Span = HollowFrom - HollowMinimum();" in code, (
        f"{where} no longer measures the span as HollowFrom less the minimum")
    unshrinkable = re.search(r"int32 Stacks = (\d+); if \(Span > 0\) \{", code)
    assert unshrinkable, f"{where} no longer answers one that cannot shrink on its own"
    assert "const int32 Natural = Floors - UpgradeFloors;" in code, (
        f"{where} no longer reads the natural floors, Floors less UpgradeFloors")
    lost = re.search(
        r"const int32 Lost = FMath::Clamp\(HollowFrom - Natural, (\d+), Span\);", code)
    assert lost, f"{where} no longer holds what was lost between nought and the span"
    rise = re.search(
        r"Stacks = FMath::FloorToInt\(([\d.]+)f \+ ([\d.]+)f \* Lost / Span "
        r"\+ ([\d.]+)f\);", code)
    assert rise, f"{where} no longer answers floor(base + rise x lost / span + half)"
    cap = re.search(
        r"if \(CityStacks < UCataclysmSurgeScheduler::HollowCannotEraseBelow\) "
        r"\{ Stacks = FMath::Min\( ?Stacks, "
        r"UCataclysmEmpireMap::VoidStacksToErase - (\d+) - CityStacks\); \}", code)
    assert cap, f"{where} no longer holds a city below the line short of erasure"
    floor = re.search(r"return FMath::Max\((\d+), Stacks\);", code)
    assert floor, f"{where} no longer answers at least a floor of stacks"

    most = int(unshrinkable.group(1))
    lost_low = int(lost.group(1))
    base, rise_by, half = (f32(float(rise.group(i))) for i in (1, 2, 3))
    short = int(cap.group(1))
    least = int(floor.group(1))

    def stacks(made_with: int, natural: int, city_stacks: int) -> int:
        span = made_with - minimum(made_with)
        added = most
        if span > 0:
            gone = min(max(made_with - natural, lost_low), span)
            # SINGLE PRECISION, ONE OPERATION AT A TIME, as the game works it.
            share = f32(f32(rise_by * gone) / span)
            added = math.floor(f32(f32(base + share) + half))
        if city_stacks < below:
            added = min(added, ten - short - city_stacks)
        return max(least, added)

    return stacks


def the_games_shrink(header: str, source: str):
    """How many floors the game takes off a Hollow in a day."""
    minimum = the_games_minimum(header, source)
    per_day = int(header_constant(header, "int32", "HollowFloorsLostPerDay"))

    left = cpp_code_of(source, LEFT_TO_LOSE)
    where = "FCataclysmDungeon::HollowFloorsLeftToLose"
    assert "if (!IsHollow() || HollowFrom <= 0) { return 0; }" in left, (
        f"{where} no longer answers nought for a dungeon that is not a Hollow "
        "or was made with no depth")
    assert "const int32 Natural = Floors - UpgradeFloors;" in left
    found = re.search(
        r"return FMath::Max\((\d+), FMath::Min\(Natural - HollowMinimum\(\), "
        r"Floors - (\d+)\)\);", left)
    assert found, (f"{where} no longer answers max(N, min(natural - minimum, "
                   "floors - M))")
    least, keep = int(found.group(1)), int(found.group(2))

    shrink = cpp_code_of(read(EMPIRE_RUN), SHRINK)
    assert ("const int32 Lose = FMath::Min( UCataclysmSurgeScheduler::"
            "HollowFloorsLostPerDay, Dungeon.HollowFloorsLeftToLose());") in shrink, (
        "UCataclysmEmpireRun::ShrinkHollows no longer loses the lesser of a "
        "day's floors and what is left to lose")

    def lose(made_with: int, floors: int, upgrade: int) -> int:
        if made_with <= 0:
            return 0
        natural = floors - upgrade
        return min(per_day, max(least, min(natural - minimum(made_with),
                                           floors - keep)))

    return lose


# ---------------------------------------------------------------------------
# The comparisons
# ---------------------------------------------------------------------------

def minimum_disagreements(game) -> list[str]:
    model = the_model()
    return [f"made with {made}: model {model.hollow_floor_minimum(a_hollow(made, made))}, "
            f"game {game(made)}"
            for made in MADE_WITH
            if model.hollow_floor_minimum(a_hollow(made, made)) != game(made)]


def stacks_disagreements(game) -> list[str]:
    model = the_model()
    found = []
    for made in MADE_WITH:
        least = model.hollow_floor_minimum(a_hollow(made, made))
        for natural in range(max(1, least - 3), made + 4):
            hollow = a_hollow(made, natural)
            for city_stacks in range(0, 12):
                city = SimpleNamespace(void_stacks=city_stacks)
                wanted = model.hollow_hit_stacks(hollow, city)
                ours = game(made, natural, city_stacks)
                if wanted != ours:
                    found.append(f"made with {made}, at {natural}, city at "
                                 f"{city_stacks}: model {wanted}, game {ours}")
    return found


def shrink_disagreements(game) -> list[str]:
    found = []
    for made in MADE_WITH:
        for natural in range(1, made + 3):
            for upgrade in range(-5, 6):
                floors = natural + upgrade
                if floors < 1:
                    continue
                model = the_model()
                hollow = a_hollow(made, floors, tree_floors=upgrade)
                model.dungeons = {hollow.did: hollow}
                model.day = hollow.spawned_day + 1
                model.current = None
                model._hollow_shrink()
                wanted = floors - hollow.floors
                ours = game(made, floors, upgrade)
                if wanted != ours:
                    found.append(f"made with {made}, {floors} floors of which "
                                 f"{upgrade} added: model loses {wanted}, "
                                 f"game {ours}")
    return found


def test_the_games_minimum_is_the_models() -> None:
    found = minimum_disagreements(the_games_minimum(read(SURGE_H), read(SURGE_CPP)))
    assert not found, ("The game's HollowMinimum and the model's "
                       "hollow_floor_minimum differ: " + "; ".join(found[:8]))


def test_the_minimum_comparison_sees_a_changed_share() -> None:
    """**THE CONTROL**: a Hollow that keeps three fifths disagrees."""
    header = read(SURGE_H)
    anchor = "static constexpr float HollowShareKept = 0.5f;"
    assert header.count(anchor) == 1, "the control's anchor is gone"
    changed = header.replace(anchor, anchor.replace("0.5f", "0.6f"))
    assert minimum_disagreements(the_games_minimum(changed, read(SURGE_CPP))), (
        "A share of three fifths changed nothing, so the comparison above "
        "cannot see a changed share")


def test_the_games_stacks_are_the_models() -> None:
    game = the_games_stacks(read(SURGE_H), read(SURGE_CPP), read(MAP_H))
    found = stacks_disagreements(game)
    assert not found, ("The game's HollowStacksOnto and the model's "
                       "hollow_hit_stacks add different stacks: "
                       + "; ".join(found[:8]))


def test_the_stacks_comparison_sees_a_changed_rise_and_a_changed_rounding() -> None:
    """**THE CONTROL**: a rise of three rather than two disagrees, and so does
    cutting the half off rather than rounding it up."""
    source = read(SURGE_CPP)
    anchor = "Stacks = FMath::FloorToInt(1.0f + 2.0f * Lost / Span + 0.5f);"
    assert source.count(anchor) == 1, "the control's anchor is gone"
    for changed in (anchor.replace("2.0f *", "3.0f *"),
                    anchor.replace("+ 0.5f)", "+ 0.0f)")):
        game = the_games_stacks(read(SURGE_H), source.replace(anchor, changed),
                                read(MAP_H))
        assert stacks_disagreements(game), (
            f"{changed!r} changed nothing, so the comparison above cannot see it")


def test_the_stacks_comparison_sees_a_changed_cap() -> None:
    """**THE CONTROL**: a cap that leaves a city two short, or a line at nine
    rather than eight, disagrees."""
    source = read(SURGE_CPP)
    anchor = "UCataclysmEmpireMap::VoidStacksToErase - 1 - CityStacks"
    assert source.count(anchor) == 1, "the control's anchor is gone"
    two_short = source.replace(anchor, anchor.replace("- 1 -", "- 2 -"))
    assert stacks_disagreements(
        the_games_stacks(read(SURGE_H), two_short, read(MAP_H)))

    header = read(SURGE_H)
    line = "static constexpr int32 HollowCannotEraseBelow = 8;"
    assert header.count(line) == 1, "the control's anchor is gone"
    nine = header.replace(line, line.replace("= 8;", "= 9;"))
    assert stacks_disagreements(the_games_stacks(nine, read(SURGE_CPP), read(MAP_H)))


def test_the_games_shrink_is_the_models() -> None:
    found = shrink_disagreements(the_games_shrink(read(SURGE_H), read(SURGE_CPP)))
    assert not found, ("The game's ShrinkHollows and the model's _hollow_shrink "
                       "take different floors off: " + "; ".join(found[:8]))


def test_the_shrink_comparison_sees_a_hollow_shrunk_to_nothing() -> None:
    """**THE CONTROL**: a shrink allowed to take the last floor disagrees."""
    source = read(SURGE_CPP)
    anchor = "FMath::Min(Natural - HollowMinimum(), Floors - 1)"
    assert source.count(anchor) == 1, "the control's anchor is gone"
    changed = source.replace(anchor, anchor.replace("Floors - 1", "Floors - 0"))
    assert shrink_disagreements(the_games_shrink(read(SURGE_H), changed))


def test_the_three_constants_are_the_models() -> None:
    from cataclysm_sim.config import TuningConfig

    cfg = TuningConfig()
    header = read(SURGE_H)
    assert header_constant(header, "float", "HollowShareKept") == \
        cfg.hollow_floor_share_kept == 0.5
    assert header_constant(header, "int32", "HollowFloorsLostPerDay") == \
        cfg.hollow_floors_lost_per_day == 1
    assert header_constant(header, "int32", "HollowCannotEraseBelow") == \
        cfg.hollow_hit_cannot_erase_below == 8


# ---------------------------------------------------------------------------
# The place in the day, and who is left alone
# ---------------------------------------------------------------------------

def day_order_problems(game_source: str) -> list[str]:
    """What is wrong with where each half shrinks its Hollows in the day."""
    from cataclysm_sim.engine import Simulation

    problems = []

    game = cpp_code_of(game_source, ADVANCE)
    marks = ("ApplySiegeDamage(Report);", "ShrinkHollows(Today);",
             "for (const int32 DungeonId : Resolved) { ResolveDungeon(DungeonId, Report); }")
    if any(game.count(mark) != 1 for mark in marks):
        problems.append("UCataclysmEmpireRun::AdvanceDay does not hold the Siege "
                        "bite, the shrink and the resolve loop once each")
    elif [game.index(m) for m in marks] != sorted(game.index(m) for m in marks):
        problems.append("UCataclysmEmpireRun::AdvanceDay no longer shrinks the "
                        "Hollows after the Siege bites and before the resolves")

    model = python_code_of(Simulation.step)
    marks = ("self._apply_siege_damage()", "self._hollow_shrink()",
             "self._resolve(d)")
    if any(model.count(mark) != 1 for mark in marks):
        problems.append("Simulation.step does not hold the Siege bite, the "
                        "shrink and the resolve once each")
    elif [model.index(m) for m in marks] != sorted(model.index(m) for m in marks):
        problems.append("Simulation.step no longer shrinks the Hollows after "
                        "the Siege bites and before the resolves")

    return problems


def left_alone_problems(game_source: str) -> list[str]:
    """What is wrong with which Hollows each half leaves alone."""
    from cataclysm_sim.engine import Simulation

    problems = []

    game = cpp_code_of(game_source, SHRINK)
    if "const int32 Inside = Clock->CurrentDungeonId;" not in game:
        problems.append("ShrinkHollows no longer asks the clock which dungeon "
                        "the player is inside")
    if ("if (!Dungeon.IsHollow() || Dungeon.SpawnedDay >= Today "
            "|| Dungeon.DungeonId == Inside) { continue; }") not in game:
        problems.append("ShrinkHollows no longer skips a dungeon that is not a "
                        "Hollow, one made today, and the one the player is inside")
    for timer in ("SetResolveDays", "DaysUntilResolve", "ResolveDays"):
        if timer in game:
            problems.append(f"ShrinkHollows touches {timer}; a Hollow's timer "
                            "does not move")
    if "Clock->SetFloors(Dungeon.DungeonId, Dungeon.Floors);" not in game:
        problems.append("ShrinkHollows no longer tells the clock the floors")

    model = python_code_of(Simulation._hollow_shrink)
    if "self.day <= d.spawned_day" not in model:
        problems.append("Simulation._hollow_shrink no longer skips a Hollow "
                        "made today")
    if ("if self.current is not None and d.did == self.current.did: continue"
            not in model):
        problems.append("Simulation._hollow_shrink no longer skips the dungeon "
                        "the player is inside")
    for timer in ("resolve_in", "resolve_max"):
        if timer in model:
            problems.append(f"Simulation._hollow_shrink touches {timer}")

    return problems


def test_both_halves_shrink_after_the_siege_and_before_the_resolves() -> None:
    problems = day_order_problems(read(EMPIRE_RUN))
    assert not problems, "; ".join(problems)


def test_the_day_order_check_sees_a_shrink_moved_after_the_resolves() -> None:
    """**THE CONTROL**: the shrink moved below the resolve loop is seen."""
    source = read(EMPIRE_RUN)
    shrink = "\tShrinkHollows(Today);"
    assert source.count(shrink) == 1, "the control's anchor is gone"
    moved = source.replace(shrink, "").replace(
        "\tReport.bPillarExposed = Map->IsPillarExposed();\n",
        shrink + "\n\tReport.bPillarExposed = Map->IsPillarExposed();\n", 1)
    if moved == source.replace(shrink, ""):
        moved = source.replace(shrink, "").replace(
            "\tReport.bPillarExposed = Map->IsPillarExposed();\r\n",
            shrink + "\r\n\tReport.bPillarExposed = Map->IsPillarExposed();\r\n", 1)
    assert day_order_problems(moved), (
        "A shrink moved after the resolves was not seen")


def test_both_halves_leave_a_new_hollow_and_the_walked_one_alone() -> None:
    problems = left_alone_problems(read(EMPIRE_RUN))
    assert not problems, "; ".join(problems)


def test_the_models_skips_are_real() -> None:
    """The model's two skips, run rather than read: a Hollow made today and
    the one the player is inside keep their floors, and the same Hollow on a
    later day, nobody inside it, loses one."""
    model = the_model()
    hollow = a_hollow(20, 20, spawned_day=5)
    model.dungeons = {hollow.did: hollow}
    model.current = None

    model.day = 5
    model._hollow_shrink()
    assert hollow.floors == 20, "the model shrank a Hollow on the day it was made"

    model.day = 6
    model.current = hollow
    model._hollow_shrink()
    assert hollow.floors == 20, "the model shrank the Hollow the player is inside"

    model.current = None
    model._hollow_shrink()
    assert hollow.floors == 19, "CONTROL: the model did not shrink the Hollow at all"


def test_the_left_alone_check_sees_the_walked_guard_removed() -> None:
    """**THE CONTROL**: the game's guard on the walked dungeon taken out is
    seen."""
    source = read(EMPIRE_RUN)
    guard = "\n\t\t\t|| Dungeon.DungeonId == Inside)"
    if source.count(guard) != 1:
        guard = guard.replace("\n", "\r\n")
    assert source.count(guard) == 1, "the control's anchor is gone"
    assert left_alone_problems(source.replace(guard, ")"))


# ---------------------------------------------------------------------------
# Who is a Hollow, what it remembers, and the hit
# ---------------------------------------------------------------------------

def test_a_hollow_is_a_quest_dungeon_whose_sub_type_is_hollow_in_both() -> None:
    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._hollow_adds_stacks)
    assert 'd.dtype is DungeonType.QUEST and d.subtype == "Hollow"' in model

    game = cpp_code_of(read(SURGE_CPP), IS_HOLLOW)
    assert ("return Type == ECataclysmDungeonType::Quest "
            "&& SubType == ECataclysmDungeonSubType::Hollow;") in game, (
        "FCataclysmDungeon::IsHollow no longer answers a Quest dungeon whose "
        "sub-type is Hollow, and nothing else")


def test_both_halves_remember_the_natural_floors_a_hollow_was_made_with() -> None:
    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._make_dungeon)
    assert 'if subtype == "Hollow": d.hollow_from = natural' in model, (
        "Simulation._make_dungeon no longer gives a Hollow its natural floors")

    source = read(SURGE_CPP)
    made = re.sub(r"\s+", " ", code_lines(
        source.split(MAKE, 1)[1].split("return Dungeon;", 1)[0]))
    assert "if (Dungeon.IsHollow()) { Dungeon.HollowFrom = Rolled; }" in made, (
        "MakeDungeon no longer gives a Hollow the floors it rolled")
    assert made.index("Dungeon.SubType = Fixed;") < made.index(
        "Dungeon.HollowFrom = Rolled;"), (
        "MakeDungeon remembers the floors before it has fixed the sub-type")
    assert "const int32 Rolled = Dungeon.Floors;" in made
    assert made.index("const int32 Rolled = Dungeon.Floors;") < made.index(
        "Dungeon.Floors = FMath::Max(1, Dungeon.Floors + Deeper - Shallower);"), (
        "MakeDungeon keeps the rolled floors after the city's upgrades moved them")


def test_the_hit_adds_the_stacks_instead_of_damage_in_both() -> None:
    from cataclysm_sim.engine import Simulation

    model = python_code_of(Simulation._quest_hit)
    for mark in ("if self._hollow_adds_stacks(d):",
                 "added = self.hollow_hit_stacks(d, city)",
                 "city.void_stacks += added"):
        assert model.count(mark) == 1, f"Simulation._quest_hit holds {mark!r} " \
                                       f"{model.count(mark)} times"
    assert model.index("added = self.hollow_hit_stacks(d, city)") < model.index(
        "else:") < model.index("city.defense -="), (
        "Simulation._quest_hit no longer adds a Hollow's stacks instead of "
        "taking defence")

    game = cpp_code_of(read(EMPIRE_RUN), RESOLVE)
    marks = ("const bool bHollow = Dungeon->IsHollow();",
             "const int32 HollowStacks = bHollow ? "
             "Dungeon->HollowStacksOnto(Host->VoidStacks) : 0;",
             "if (bHollow) { Ordinary.DefenceDamage = 0.0f; "
             "Ordinary.PopulationDamage = 0.0f; }",
             "++QuestHits;",
             "if (Map->Damage(HostId, Ordinary.DefenceDamage, "
             "Ordinary.PopulationDamage) || (bHollow && "
             "Map->AddVoidStacks(HostId, HollowStacks))) "
             "{ CityFell(HostId, OutReport); return; }")
    for mark in marks:
        assert game.count(mark) == 1, (
            f"UCataclysmEmpireRun::ResolveDungeon holds {mark!r} "
            f"{game.count(mark)} times; one is the rule")
    places = [game.index(mark) for mark in marks]
    assert places == sorted(places), (
        "ResolveDungeon no longer works the Hollow's stacks out and zeroes its "
        "damage before it counts the hit and deals it")
