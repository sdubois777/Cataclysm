"""The empire upgrade tree's slice 6d: Treasury Guard and Tax Collection.

WHY THIS EXISTS. Issue #2741 slice 6d, the owner's rulings of 2026-11-20 relayed by the coordinating session: Treasury
Guard reduces every city's damage from the gold held ("1. to your cities"), held at its own cap and multiplied with
Imperial Reserve (D9); Tax Collection pays a share of the standing cities' citizens every tenth day, scaled to the tier
and raised as Pax Imperialis is (D8). Global Credit and Sovereign Loans, the debt the owner allowed through them, are
slice 6e's, in `tools/tests/test_empire_tree_credit.py`.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeTreasury.`
automation tests never run on a pull request; and the day hook's log line is where no automation test reaches.

WHAT IS CHECKED:

  1. every step C++ counts in is its node's own words;
  2. the tree fills each node's own fields, Treasury Guard's apart from Imperial Reserve's;
  3. the city damage factor multiplies the two nodes' reductions, each with its own step and its own cap;
  4. Tax Collection is paid on every tenth day from the standing cities' citizens, scaled and raised, and is part of
     the day's total;
  5. the day hook's log names Tax Collection's share;
  6. no node waits on a ruling any more.
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
TREE_CPP = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.cpp"
MAP_H = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireMap.h"
BONUSES_CPP = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireTreeBonuses.cpp"
INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
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


def case(text: str, kind: str) -> str:
    """The lines of the `case` for `kind`, up to its `break`."""
    start = text.find(f"case ECataclysmEmpireTreeEffect::{kind}:")
    assert start != -1, f"CataclysmEmpireTree.cpp has no case for {kind}"
    return text[start:text.index("break;", start)]


def test_every_step_is_its_nodes_own_words() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        words = {row["NodeName"]: row["Description"] for row in csv.DictReader(handle)}
    wanted = {
        "Treasury Guard": f"per {constant(code(MAP_H), 'TreasuryGuardGoldAStep'):,} Gold currently held",
        "Tax Collection": f"Every {constant(code(TREE_H), 'TaxCollectionDaysAStep')} days",
    }
    wrong = [f"{name}: {phrase!r} is not in {words[name]!r}" for name, phrase in wanted.items()
             if phrase not in words[name]]
    assert not wrong, "\n".join(wrong)


def test_the_tree_fills_each_nodes_own_fields() -> None:
    tree = code(TREE_CPP)
    guard = case(tree, "DamageTakenPerFiftyThousandGold")
    for field in ("Bonuses.ReductionPerHeldGoldStep = Row->PerPoint;", "Bonuses.HeldGoldStepPoints += Points;",
                  "Bonuses.HeldGoldReductionCap = Row->Cap;"):
        assert field in guard, f"Treasury Guard's row does not fill {field}"
    assert "GoldStepPoints" not in guard.replace("HeldGoldStepPoints", ""), (
        "Treasury Guard's row reaches Imperial Reserve's fields")
    assert "Bonuses.GoldShareOfPopulationEveryTenDays += Total;" in case(tree, "GoldShareOfPopulationEveryTenDays"), (
        "Tax Collection's row does not fill its share")


def test_the_city_damage_factor_multiplies_each_nodes_own_reduction() -> None:
    factor = body(code(BONUSES_CPP), "float FCataclysmEmpireTreeBonuses::GoldDamageFactorFor(",
                  "CataclysmEmpireTreeBonuses.cpp")
    reserve = re.search(r"KeptAfter\(\s*ReductionPerGoldStep\s*,\s*GoldStepPoints\s*,\s*(\w+)\s*,\s*GoldReductionCap\s*\)",
                        factor)
    guard = re.search(r"KeptAfter\(\s*ReductionPerHeldGoldStep\s*,\s*HeldGoldStepPoints\s*,\s*(\w+)\s*,\s*"
                      r"HeldGoldReductionCap\s*\)", factor)
    assert reserve and guard, "the factor does not take each node's reduction with its own fields and cap"
    assert re.search(r"KeptAfter\([^;]*\)\s*\*\s*CataclysmEmpireTreeBonuses::KeptAfter\(", factor), (
        "the two reductions are not multiplied")
    steps = {name: re.search(rf"\b{name}\s*=([^;]*);", factor) for name in (reserve.group(1), guard.group(1))}
    assert all(steps.values()), f"the steps are not worked out in the factor: {steps}"
    assert "ImperialReserveGoldAStep" in steps[reserve.group(1)].group(1), "Imperial Reserve's steps are not 100,000"
    assert "TreasuryGuardGoldAStep" in steps[guard.group(1)].group(1), "Treasury Guard's steps are not 50,000"


def test_tax_is_paid_every_tenth_day_from_the_standing_cities_citizens() -> None:
    income = body(code(TREE_CPP), "FCataclysmEmpireTreeDailyIncome UCataclysmEmpireTree::DailyIncomeFor(",
                  "CataclysmEmpireTree.cpp")
    tax = income[income.index("Bonuses.GoldShareOfPopulationEveryTenDays > 0.0f"):]
    tax = tax[:tax.index("}") + 1]
    assert re.search(r"Day\s*>\s*0\s*&&\s*Day\s*%\s*TaxCollectionDaysAStep\s*==\s*0", tax), (
        "Tax Collection is not paid on every tenth day alone")
    for part in ("Income.Tax =", "State.Citizens", "* Scale * Raised"):
        assert part in tax, f"Tax Collection's payment does not hold {part!r}"
    assert re.search(r"Total\(\)\s*const\s*\{\s*return\s+Pax\s*\+\s*Interest\s*\+\s*Tax\s*;", code(TREE_H)), (
        "the day's total leaves Tax Collection out")


def test_the_day_hook_logs_tax_collections_share() -> None:
    wire = body(code(INSTANCE), "void UCataclysmGameInstance::WireTheRunToTheBalance(", "CataclysmGameInstance.cpp")
    text = INSTANCE.read_text(encoding="utf-8")
    assert "Tax Collection %lld" in text and "Income.Tax" in wire, (
        "the day hook's log line does not say what Tax Collection paid")


def test_the_two_are_built_and_no_node_waits_on_a_ruling() -> None:
    assert gen.EMPIRE_TREE_BUILT.get("Treasury Guard") == 6 and gen.EMPIRE_TREE_BUILT.get("Tax Collection") == 6
    assert "gold ruling" not in gen.EMPIRE_TREE_SYSTEMS and "gold ruling" not in gen.EMPIRE_TREE_REFUSED.values(), (
        "a node still says it waits on a ruling the owner made on 2026-11-20")
