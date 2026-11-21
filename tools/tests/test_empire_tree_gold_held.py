"""The empire upgrade tree's slice 6c: the gold held and the day's income.

WHY THIS EXISTS. Issue #2741 slice 6c, the coordinating session's B6 to B8 of 2026-11-20. The Gilded Path and Venture
Capital give a kill magic find from the gold held, Imperial Reserve reduces every city's damage by it, and Pax Imperialis
and Wealth Interest pay into the balance as days pass. The balance is the Cataclysm module's and the days and the damage
are the empire module's, so the game instance hands the run two functions. The owner ruled on Tax Collection, Treasury
Guard, Global Credit and Sovereign Loans on 2026-11-20, after this slice was written, so none of them may say it waits
on a ruling.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeGoldHeld.` automation
tests never run on a pull request. And the hand-over -- the functions the game instance sets, and where the run calls
them -- is where no automation test reaches.

WHAT IS CHECKED:

  1. every step C++ counts in is its node's own words;
  2. the game instance gives a run both functions where it gives the run its tree, as a run begins and at every refresh,
     which a load calls; and the day's function pays what `DailyIncomeFor` works out into the balance;
  3. the run calls the day's function after the day's resolutions, and every city's damage takes Imperial Reserve's
     factor from the gold held;
  4. a kill adds the gold held's magic find before its paying branch, from the gold held and the tier's scale;
  5. the four nodes the owner ruled on wait on what is not built yet -- Tax Collection and Treasury Guard on their gold
     rules, Global Credit and Sovereign Loans on a gold debt -- and no node waits on a ruling about them.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
TREE_H = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.h"
MAP_H = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireMap.h"
RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"
INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
ENEMY = SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str, where: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {where} is not closed")


def constant(text: str, name: str) -> int:
    found = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", text)
    assert found, f"no {name}"
    return int(found.group(1))


def test_every_step_is_its_nodes_own_words() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        words = {row["NodeName"]: row["Description"] for row in csv.DictReader(handle)}
    tree = code(TREE_H)
    wanted = {
        "The Gilded Path": f"per {constant(tree, 'GildedPathGoldAStep'):,} Gold",
        "Venture Capital": f"Every {constant(tree, 'VentureCapitalGoldAStep'):,} Gold",
        "Imperial Reserve": f"per {constant(code(MAP_H), 'ImperialReserveGoldAStep'):,} Gold",
        "Pax Imperialis": f"every {constant(tree, 'PaxCitizensAStep'):,} surviving citizens",
        "Wealth Interest": f"Every {constant(tree, 'WealthInterestDaysAStep')} days",
    }
    wrong = [f"{name}: {phrase!r} is not in {words[name]!r}" for name, phrase in wanted.items()
             if phrase not in words[name]]
    assert not wrong, "\n".join(wrong)


def test_the_game_instance_hands_the_run_both_functions_where_it_hands_it_the_tree() -> None:
    instance = code(INSTANCE)
    begin = instance.find("EmpireRun->SetTreeBonuses(EmpireTreeBonusesFor(this));")
    assert -1 < begin < instance.find("WireTheRunToTheBalance(this, EmpireRun);"), (
        "a run begins without being told the balance after its tree")
    refresh = body(instance, "void UCataclysmGameInstance::RefreshEmpireTreeBonuses(", "CataclysmGameInstance.cpp")
    assert "WireTheRunToTheBalance(WorldContext, Run);" in refresh, (
        "a refresh, which a load calls, does not tell the run the balance again")
    wire = body(instance, "void UCataclysmGameInstance::WireTheRunToTheBalance(", "CataclysmGameInstance.cpp")
    for given in ("Run->GoldHeldAtTierOne =", "Run->OnDayPassed ="):
        assert given in wire, f"WireTheRunToTheBalance does not set {given}"
    day = wire[wire.index("Run->OnDayPassed ="):]
    for step in ("UCataclysmEmpireTree::DailyIncomeFor(", "AddGoldFor(", "UCataclysmDropRoll::GoldScaleFor("):
        assert step in day, f"the day's function does not call {step}"
    held = wire[wire.index("Run->GoldHeldAtTierOne ="):wire.index("Run->OnDayPassed =")]
    assert "GoldHeldBy(" in held and "UCataclysmDropRoll::GoldScaleFor(" in held, (
        "the gold held is not read from the balance in tier 1 gold")


def test_the_run_calls_the_day_after_its_resolutions_and_every_city_takes_the_reserve() -> None:
    run = code(RUN)
    day = body(run, "FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay()", "CataclysmEmpireRun.cpp")
    resolved = day.find("ResolveDungeon(DungeonId, Report);")
    called = day.find("OnDayPassed(*this, Day());")
    assert -1 < resolved < called, "AdvanceDay does not call the day's function after the day's resolutions"
    factors = body(run, "FCataclysmTreeDamageFactors UCataclysmEmpireRun::TreeDamageFactorsFor(", "CataclysmEmpireRun.cpp")
    assert re.search(r"TreeBonuses\.GoldDamageFactorFor\(\s*GoldHeldAtTierOne\s*\?\s*GoldHeldAtTierOne\(\)\s*:\s*0\.0\s*\)",
                     factors), "every city's damage does not take Imperial Reserve's factor from the gold held"
    for part in ("Taken.Defence *= ByGold;", "Taken.Population *= ByGold;"):
        assert part in factors, f"the reserve's factor is not applied: {part}"


def test_a_kill_adds_the_gold_helds_magic_find_before_it_rolls() -> None:
    handler = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    added = handler.find("MagicFind += TreeDrops.MagicFindFromGold;")
    assert -1 < handler.find("EmpireTreeDropRulesIn(World)") < added < handler.find("if (bPays)"), (
        "the kill does not add the gold held's magic find between asking for its rules and rolling")
    here = body(code(MODE), "FCataclysmEmpireTreeDropRules ACataclysmDungeonGameMode::EmpireTreeDropRulesHere()",
                "CataclysmDungeonGameMode.cpp")
    for filled in ("State.GoldHeld =", "State.GoldScale = UCataclysmDropRoll::GoldScaleFor(Run->DifficultyTier);"):
        assert filled in here, f"the kill's rules do not fill {filled}"


def test_the_nodes_the_owner_ruled_on_wait_on_what_is_not_built_and_none_on_a_ruling() -> None:
    ruled = {"Tax Collection": "gold rules", "Treasury Guard": "gold rules",
             "Global Credit": "gold debt", "Sovereign Loans": "gold debt"}
    reasons = {name: gen.EMPIRE_TREE_REFUSED.get(name) for name in ruled}
    assert reasons == ruled, f"a node the owner ruled on (2026-11-20) is built or refused for another reason: {reasons}"
    for system in set(ruled.values()):
        assert gen.EMPIRE_TREE_SYSTEMS[system].endswith("which is not built yet (issue #2741)") or             gen.EMPIRE_TREE_SYSTEMS[system].endswith("which are not built yet (issue #2741)"), (system, "says more")
    assert "gold ruling" not in gen.EMPIRE_TREE_SYSTEMS, "a reason still says the owner has not ruled on a gold node"
