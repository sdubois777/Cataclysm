"""The Void's stacks on cities, in the model and in the game.

**BOTH HALVES HOLD THE RULE, AND THIS FILE HOLDS THEM TOGETHER.**

The owner, 2026-10-16, verbatim: "All void dungeons when they resolve add 1-3
void stacks to a city instead of doing damage to the city or population and at
10 stacks the city is erased." The model has it since 2026-10-18
(`Simulation._adds_void_stacks`, `_void_stacks_for`, `_fall`, `_retake`) and
aims the Void by the stacks, at most twice at ten (`patterns._void`; the owner
chose "Stacks, up to 2x"). The game has it since 2026-10-22: `FCataclysmCity::
VoidStacks`, `UCataclysmEmpireMap::AddVoidStacks` and `Retake`,
`FCataclysmDungeon::AddsVoidStacks` and `VoidStacksItAdds`, and
`UCataclysmSurgeScheduler::TargetWeightForSender`.

WHAT IS COMPARED, AND HOW EACH CHECK FAILS:

- **The thirds.** The game's `VoidStacksItAdds` is read for its numbers -- the
  3 that makes thirds, the 1 it adds, the 0 and 2 it clamps to, the 1 a single
  depth answers -- and run against `Simulation._void_stacks_for` for every
  ordinary row and every depth from below the row to above it. Fails when any
  of those numbers changes on either side, and
  `test_the_thirds_comparison_sees_a_changed_number` shows it does.
  **THE MODEL IS ASKED WITH A MULTIPLIER OF ONE.** It reads the thirds of the
  row's range times the sender's `floors_mult`, 1.25 for the Void; the game
  rolls the Void's depths from the plain row, because no Cataclysm's depth
  multiplier is ported (#53), and reads the thirds of that. Ruled by the
  coordinating session on 2026-10-22. So the comparison is of the rule, asked
  of a dungeon whose sender has no multiplier.
- **The pull.** The game's `VoidTargetPullAtFull` and the cap in
  `TargetWeightForSender` against `patterns._void`, at every stack count from
  0 to 20. Fails when either side's size or cap moves, and
  `test_the_pull_comparison_sees_a_changed_number` shows it does.
- **Ten.** `UCataclysmEmpireMap::VoidStacksToErase` against
  `TuningConfig().void_stacks_to_erase`. Fails when either moves.
- **The retake.** `UCataclysmEmpireMap::Retake` and `Simulation._retake` each
  clear the stacks. Fails when either line goes.
- **Who adds them.** An ordinary dungeon the Void sent, on both sides, and the
  game's resolve chooses stacks or damage for its one hit. Fails when either
  side widens or narrows it.

A LINE THAT IS A COMMENT DOES NOT COUNT, in either language.
"""

from __future__ import annotations

import inspect
import pathlib
import re
from types import SimpleNamespace

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
MAP_H = EMPIRE / "CataclysmEmpireMap.h"
MAP_CPP = EMPIRE / "CataclysmEmpireMap.cpp"
SURGE_H = EMPIRE / "CataclysmSurge.h"
SURGE_CPP = EMPIRE / "CataclysmSurge.cpp"
EMPIRE_RUN = EMPIRE / "CataclysmEmpireRun.cpp"

STACKS_FOR = "int32 FCataclysmDungeon::VoidStacksItAdds() const"
ADDS = "bool FCataclysmDungeon::AddsVoidStacks() const"
WEIGHT = "float UCataclysmSurgeScheduler::TargetWeightForSender("
PICK = "TArray<int32> UCataclysmSurgeScheduler::PickTargets("
ADD_STACKS = "bool UCataclysmEmpireMap::AddVoidStacks("
RETAKE = "bool UCataclysmEmpireMap::Retake("
FALL = "bool UCataclysmEmpireMap::Fall("
RESOLVE = "void UCataclysmEmpireRun::ResolveDungeon("


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def code_lines(text: str) -> str:
    """Every line that is not a `//` comment line. The functions read here
    hold no block comments, and a code line may open with a `*`."""
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
    assert end != -1, f"{signature!r} is not closed"
    return re.sub(r"\s+", " ", code_lines(text[start:end]))


def python_code_of(function) -> str:
    """One Python function's code: no docstring, and nothing after a `#`."""
    source = inspect.getsource(function)
    if function.__doc__:
        source = re.sub(r'""".*?"""', "", source, count=1, flags=re.DOTALL)
    return "\n".join(line.split("#", 1)[0] for line in source.splitlines())


# ---------------------------------------------------------------------------
# The thirds
# ---------------------------------------------------------------------------

def the_games_thirds(source: str):
    """`VoidStacksItAdds` read out of the source, as a function of the natural
    floors and the row's two ends."""
    code = cpp_code_of(source, STACKS_FOR)
    where = "FCataclysmDungeon::VoidStacksItAdds"

    assert "const int32 Span = Spec.MostFloors - Spec.LeastFloors;" in code, (
        f"{where} no longer measures the row as most less least floors")
    guard = re.search(r"if \(Span <= 0\) \{ return (\d+); \}", code)
    assert guard, f"{where} no longer answers a single-depth row on its own"
    assert "const int32 Natural = Floors - UpgradeFloors;" in code, (
        f"{where} no longer reads the natural floors, Floors less UpgradeFloors, "
        "so a city's floor upgrades would move the stacks")
    third = re.search(
        r"const int32 Third = (\d+) \* \(Natural - Spec\.LeastFloors\) / Span;", code)
    assert third, f"{where} no longer works the third out as N x (natural - least) / span"
    answer = re.search(r"return (\d+) \+ FMath::Clamp\(Third, (\d+), (\d+)\);", code)
    assert answer, f"{where} no longer answers base + clamp(third, low, high)"

    single = int(guard.group(1))
    parts = int(third.group(1))
    base, low, high = (int(answer.group(i)) for i in (1, 2, 3))

    def stacks(natural: int, least: int, most: int) -> int:
        span = most - least
        if span <= 0:
            return single
        # C++ whole-number division cuts towards nought, as `int()` does.
        return base + min(high, max(low, int(parts * (natural - least) / span)))

    return stacks


def the_models_thirds(natural: int, least: int, most: int, tier, dtype) -> int:
    """`Simulation._void_stacks_for` for a dungeon whose sender has no depth
    multiplier, on a row of these two ends."""
    from cataclysm_sim.engine import Simulation

    owner = SimpleNamespace(
        cfg=SimpleNamespace(spec=lambda _t, _c: SimpleNamespace(floors=(least, most))))
    dungeon = SimpleNamespace(dtype=dtype, city_tier=tier, natural_floors=natural,
                              source="")
    return Simulation._void_stacks_for(owner, dungeon)


def disagreements(game) -> list[str]:
    from cataclysm_sim.config import CityTier, DungeonType, TuningConfig

    cfg = TuningConfig()
    found = []
    rows = [(tier, *cfg.spec(DungeonType.BASIC, tier).floors) for tier in CityTier]
    # AND A ROW OF ONE DEPTH, which no table holds, for the guard.
    rows.append((CityTier.OUTPOST, 9, 9))
    for tier, least, most in rows:
        for natural in range(least - 3, most + 4):
            model = the_models_thirds(natural, least, most, tier, DungeonType.BASIC)
            ours = game(natural, least, most)
            if model != ours:
                found.append(f"{tier.name} {least}-{most} at {natural}: "
                             f"model {model}, game {ours}")
    return found


def test_the_model_is_asked_with_no_depth_multiplier() -> None:
    """The sender `the_models_thirds` names, nobody, has a multiplier of one, so
    the model reads the plain row as the game does."""
    from cataclysm_sim.patterns import DEFAULT, PATTERNS

    assert "" not in PATTERNS
    assert DEFAULT.floors_mult == 1.0


def test_the_games_thirds_are_the_models() -> None:
    game = the_games_thirds(read(SURGE_CPP))
    found = disagreements(game)
    assert not found, (
        "The game's VoidStacksItAdds and the model's _void_stacks_for give a "
        "different number of stacks: " + "; ".join(found[:8]))


def test_the_thirds_comparison_sees_a_changed_number() -> None:
    """**THE CONTROL ON THE TEST ABOVE**: the same reading of a source whose
    thirds are halves disagrees with the model."""
    source = read(SURGE_CPP)
    anchor = "const int32 Third = 3 * (Natural - Spec.LeastFloors) / Span;"
    assert source.count(anchor) == 1, "the control's anchor is gone"
    halves = source.replace(anchor, anchor.replace("3 *", "2 *"))
    assert disagreements(the_games_thirds(halves)), (
        "Reading thirds as halves changed nothing, so the comparison above "
        "cannot see a changed number")


# ---------------------------------------------------------------------------
# The pull
# ---------------------------------------------------------------------------

def the_games_pull(header: str, source: str):
    """The game's Void weight over a city's tier weight, as a function of its
    stacks, read out of the source."""
    pulled = re.search(
        r"static constexpr float VoidTargetPullAtFull = ([\d.]+)f;", header)
    assert pulled, "CataclysmSurge.h no longer declares VoidTargetPullAtFull"
    ten = the_games_ten()

    code = cpp_code_of(source, WEIGHT)
    where = "UCataclysmSurgeScheduler::TargetWeightForSender"
    assert "const float Weight = TargetWeightFor(City.Tier);" in code, (
        f"{where} no longer starts from the city's tier weight")
    assert "if (Sender != ECataclysmType::Void) { return Weight; }" in code, (
        f"{where} no longer leaves every sender but the Void at the tier weight")
    share = re.search(
        r"const float Share = FMath::Min\( ?([\d.]+)f, "
        r"static_cast<float>\(City\.VoidStacks\) "
        r"/ static_cast<float>\(UCataclysmEmpireMap::VoidStacksToErase\)\);", code)
    assert share, f"{where} no longer takes the stacks as a capped share of ten"
    answer = re.search(
        r"return Weight \* \(([\d.]+)f \+ VoidTargetPullAtFull \* Share\);", code)
    assert answer, f"{where} no longer answers weight x (1 + pull x share)"

    pull = float(pulled.group(1))
    cap = float(share.group(1))
    base = float(answer.group(1))

    def ratio(stacks: int) -> float:
        return base + pull * min(cap, stacks / ten)

    return ratio


def the_models_pull(stacks: int) -> float:
    from cataclysm_sim.config import CityTier
    from cataclysm_sim.patterns import PATTERNS, _tier_weight
    from cataclysm_sim.world import City

    city = City(cid=0, name="Outpost (0,0)", tier=CityTier.OUTPOST, r=0, c=0,
                void_stacks=stacks)
    return PATTERNS["Void"].weight(None, city) / _tier_weight(city)


def pull_disagreements(game) -> list[str]:
    return [f"{stacks} stacks: model {the_models_pull(stacks)}, game {game(stacks)}"
            for stacks in range(0, 21)
            if abs(the_models_pull(stacks) - game(stacks)) > 1e-9]


def test_the_games_pull_is_the_models() -> None:
    game = the_games_pull(read(SURGE_H), read(SURGE_CPP))
    found = pull_disagreements(game)
    assert not found, (
        "The game's TargetWeightForSender and the model's patterns._void pull "
        "the Void by different amounts: " + "; ".join(found[:8]))
    assert game(0) == 1.0 and game(10) == 2.0, (
        "The owner chose 'Stacks, up to 2x' on 2026-10-18: nought stacks pull "
        "by nothing and ten pull twice")


def test_the_pull_comparison_sees_a_changed_number() -> None:
    """**THE CONTROL ON THE TEST ABOVE**: a pull of 2, three times at ten,
    disagrees with the model."""
    header = read(SURGE_H)
    anchor = "static constexpr float VoidTargetPullAtFull = 1.0f;"
    assert header.count(anchor) == 1, "the control's anchor is gone"
    tripled = header.replace(anchor, anchor.replace("1.0f", "2.0f"))
    assert pull_disagreements(the_games_pull(tripled, read(SURGE_CPP))), (
        "A pull of three times at ten changed nothing, so the comparison above "
        "cannot see a changed number")


def test_the_pick_rolls_a_void_slot_against_the_pulled_weights() -> None:
    """The weight is used where a wave lands, and only the slot's sender is
    asked of it."""
    code = cpp_code_of(read(SURGE_CPP), PICK)
    where = "UCataclysmSurgeScheduler::PickTargets"
    for mark in ("DrawCataclysm ? DrawCataclysm() : ECataclysmType::None;",
                 "if (Sender == ECataclysmType::Void)",
                 "TargetWeightForSender(*City, Sender)",
                 "float Roll = Stream.FRand() * SlotTotal;",
                 "Roll -= (*SlotWeights)[Index];"):
        assert code.count(mark) == 1, (
            f"{where} holds {mark!r} {code.count(mark)} times; one is the rule")
    assert (code.index("DrawCataclysm ? DrawCataclysm()")
            < code.index("TargetWeightForSender(*City, Sender)")
            < code.index("Stream.FRand()")), (
        f"{where} no longer draws the slot's sender, then weighs the cities "
        "for it, then rolls the slot")


# ---------------------------------------------------------------------------
# Ten, the retake, and who adds them
# ---------------------------------------------------------------------------

def the_games_ten() -> int:
    found = re.search(r"static constexpr int32 VoidStacksToErase = (\d+);", read(MAP_H))
    assert found, "CataclysmEmpireMap.h no longer declares VoidStacksToErase"
    return int(found.group(1))


def test_ten_stacks_erase_a_city_in_both() -> None:
    from cataclysm_sim.config import TuningConfig

    assert the_games_ten() == TuningConfig().void_stacks_to_erase == 10, (
        "The game's VoidStacksToErase and the model's void_stacks_to_erase "
        "differ, or neither is the owner's ten")

    code = cpp_code_of(read(MAP_CPP), ADD_STACKS)
    where = "UCataclysmEmpireMap::AddVoidStacks"
    for mark in ("if (City == nullptr || City->bFallen) { return false; }",
                 "City->VoidStacks += FMath::Max(0, Stacks);",
                 "if (City->VoidStacks < VoidStacksToErase) { return false; }",
                 "City->bDoomed = true; return Fall(CityId);"):
        assert mark in code, (
            f"{where} no longer holds {mark!r}: it must add nothing to a fallen "
            "city, add the stacks, and at ten mark the city and fell it")
    fall = cpp_code_of(read(MAP_CPP), FALL)
    assert "if (City->bDoomed) { City->bErased = true; }" in fall, (
        "UCataclysmEmpireMap::Fall no longer erases a city the tenth stack "
        "marked, so ten stacks would fell a city that could be retaken")


def test_retaking_a_city_clears_its_stacks_in_both() -> None:
    from cataclysm_sim.engine import Simulation

    assert "city.void_stacks = 0" in python_code_of(Simulation._retake), (
        "Simulation._retake no longer clears a city's void stacks")
    assert "City->VoidStacks = 0;" in cpp_code_of(read(MAP_CPP), RETAKE), (
        "UCataclysmEmpireMap::Retake no longer clears a city's Void stacks, "
        "as Simulation._retake does")


def test_only_an_ordinary_void_dungeon_adds_them_in_both() -> None:
    from cataclysm_sim.engine import Simulation
    from cataclysm_sim.patterns import PATTERNS

    model = python_code_of(Simulation._adds_void_stacks)
    assert "d.dtype is DungeonType.BASIC" in model
    assert ".erases_cities" in model
    erasing = sorted(name for name, p in PATTERNS.items() if p.erases_cities)
    assert erasing == ["Void"], (
        f"In the model {erasing} erase cities; the game names the Void alone")

    game = cpp_code_of(read(SURGE_CPP), ADDS)
    assert ("return Type == ECataclysmDungeonType::Basic "
            "&& Cataclysm == ECataclysmType::Void;") in game, (
        "FCataclysmDungeon::AddsVoidStacks no longer answers an ordinary "
        "dungeon the Void sent, and nothing else")

    resolve = cpp_code_of(read(EMPIRE_RUN), RESOLVE)
    where = "UCataclysmEmpireRun::ResolveDungeon"
    marks = ("const bool bVoid = Dungeon->AddsVoidStacks();",
             "const int32 Stacks = bVoid ? Dungeon->VoidStacksItAdds() : 0;",
             "const float Defence = bVoid ? 0.0f : Dungeon->DefenceDamage * Scale;",
             "const float Population = bVoid ? 0.0f : Dungeon->PopulationDamage * Scale;",
             "++DungeonsDetonated;",
             "if (Map->Damage(CityId, Defence, Population) "
             "|| (bVoid && Map->AddVoidStacks(CityId, Stacks))) "
             "{ CityFell(CityId, OutReport); }")
    for mark in marks:
        assert resolve.count(mark) == 1, (
            f"{where} holds {mark!r} {resolve.count(mark)} times; one is the "
            "rule. A Void dungeon's points are nought and its stacks are "
            "added instead, for the one hit an ordinary dungeon makes")
    places = [resolve.index(mark) for mark in marks]
    assert places == sorted(places), (
        f"{where} no longer works out the stacks and the points, counts the "
        "hit, and then hits, in that order; the model counts a Void "
        "dungeon's timer as a resolve")
