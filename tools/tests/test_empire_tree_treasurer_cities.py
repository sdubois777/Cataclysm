"""The empire upgrade tree's slice 7: the four Treasury nodes that read a Treasurer city upgrade.

WHY THIS EXISTS. Issue #2741 slice 7. Commercial Hubs, Economic Zones, Trade Routes and The Golden Touch (the
coordinating session's R1 to R4 of 2026-11-20) waited on a Treasurer city upgrade until #2781 made those buyable. This
slice builds them on four readings this session proposed and the coordinating session RULED AS PROPOSED on 2026-11-21;
every check below names the reading it holds:

  T1  a "Treasury Upgrade" or "economic upgrade" is an upgrade of the City Upgrades sheet's Treasurer branch, and a
      Treasurer city is a standing city holding at least one; Commercial Hubs reads the dungeon's own city
  T2  Economic Zones is a "more" on the gold a Treasurer city generates: a kill's piles and the Salvage Rights payout
      of a dungeon standing on one, all of Trade Routes' gold, and Pax Imperialis' and Tax Collection's share counted
      from the citizens living in Treasurer cities; not Wealth Interest, Cleanup Crew or credit
  T3  "per city" counts the Treasurer cities, with no cap and no fallen city; Trade Routes pays every 20th day
  T4  The Golden Touch's crafting materials act now, through the material roll's expected count

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeTreasurer.`
automation tests never run on a pull request; and the kill and the day hook are where no automation test reaches.

WHAT IS CHECKED:

  1. the branch C++ reads is the sheet's, and the sheet's branch has exactly the four rows T1 names; Trade Routes'
     step is its own words;
  2. the tree fills each node's own field;
  3. a Treasurer city is not fallen and holds an upgrade of that branch, and the run counts them and their citizens
     among the standing cities; the dungeon state asks whether the dungeon's own city is one;
  4. a kill's gold adds The Golden Touch for each Treasurer city and Commercial Hubs on one, and multiplies Economic
     Zones' "more" on one; the Salvage Rights payout takes that "more" and Cleanup Crew does not;
  5. the day pays Trade Routes every 20th day, raised wholly by Economic Zones, and raises Pax and Tax by the
     Treasurer cities' share of the citizens and Wealth Interest not at all; the day's total and its log include it;
  6. the drop rules' materials figure reaches the kill's material roll, and draws nothing;
  7. the four are built in slice 7 and no node waits on a Treasurer city upgrade.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
UPGRADES_CSV = REPO_ROOT / "game" / "Data" / "CityUpgrades.csv"
TREE_H = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.h"
TREE_CPP = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.cpp"
INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
ENEMY = SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

SLICE_7 = ("Commercial Hubs", "Economic Zones", "Trade Routes", "The Golden Touch")


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


def case(text: str, kind: str) -> str:
    """The lines of the `case` for `kind`, up to its `break`."""
    start = text.find(f"case ECataclysmEmpireTreeEffect::{kind}:")
    assert start != -1, f"CataclysmEmpireTree.cpp has no case for {kind}"
    return text[start:text.index("break;", start)]


def flat(text: str) -> str:
    """Whitespace collapsed to single spaces, so a check reads across the code's line breaks."""
    return re.sub(r"\s+", " ", text)


def test_the_branch_is_the_sheets_and_holds_the_four_rows_t1_names() -> None:
    branch = re.search(r'UCataclysmEmpireTree::TreasurerBranch\s*=\s*TEXT\("([^"]+)"\)', code(TREE_CPP))
    assert branch, "CataclysmEmpireTree.cpp does not define TreasurerBranch"
    with UPGRADES_CSV.open(encoding="utf-8", newline="") as handle:
        effects = sorted(row["Effect"] for row in csv.DictReader(handle) if row["Branch"] == branch.group(1))
    assert effects == ["Dungeons here drop 25% more gold.", "Dungeons here drop 25% more loot.",
                       "Dungeons here have 10% increased magic find.", "Dungeons here provide 2x more experience"], (
        f"the City Upgrades sheet's {branch.group(1)!r} rows are {effects}: T1 counts these four and no other")

    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        words = {row["NodeName"]: row["Description"] for row in csv.DictReader(handle)}
    days = re.search(r"\bTradeRoutesDaysAStep\s*=\s*(\d+)\s*;", code(TREE_H))
    assert days, "CataclysmEmpireTree.h has no TradeRoutesDaysAStep"
    assert f"every {days.group(1)} days" in words["Trade Routes"], (
        f"Trade Routes' step is {days.group(1)} days and its text is {words['Trade Routes']!r}")


def test_the_tree_fills_each_nodes_own_field() -> None:
    tree = code(TREE_CPP)
    for kind in ("GoldIncreasedOnTreasurerCity", "GoldMoreFromTreasurerCities",
                 "GoldEveryTwentyDaysPerTreasurerCity", "GoldAndMaterialsPerTreasurerCity"):
        assert f"Bonuses.{kind} += Total;" in case(tree, kind), f"the {kind} row does not fill its own field"


def test_a_treasurer_city_stands_and_holds_the_branch_and_the_run_counts_them() -> None:
    tree = code(TREE_CPP)
    city = flat(body(tree, "bool UCataclysmEmpireTree::IsTreasurerCity(", "CataclysmEmpireTree.cpp"))
    assert re.search(r"if \( ?City\.bFallen ?\) \{ return false; \}", city), (
        "a fallen city counts as a Treasurer city (T3, X1)")
    assert "UCataclysmCityUpgradeMapping::BranchFor(Upgrade.RowName) == TreasurerBranch" in city, (
        "a Treasurer city is not one holding an upgrade of the Treasurer branch (T1)")

    empire = body(tree, "FCataclysmEmpireTreeDungeonState UCataclysmEmpireTree::EmpireStateFor(",
                  "CataclysmEmpireTree.cpp")
    standing = empire[empire.index("++State.StandingCities;"):]
    counted = flat(standing)
    assert "if (IsTreasurerCity(City)) { ++State.TreasurerCities; State.TreasurerCitizens += Run.Map->PopulationOf(City);" in counted, (
        "the run does not count the Treasurer cities and the citizens living in them among the standing cities")

    dungeon = flat(body(tree, "FCataclysmEmpireTreeDungeonState UCataclysmEmpireTree::DungeonStateFor(",
                        "CataclysmEmpireTree.cpp"))
    assert "Run.Map->Find(Dungeon.CityId)" in dungeon and "State.bDungeonCityIsTreasurer = DungeonCity != nullptr && IsTreasurerCity(*DungeonCity);" in dungeon, (
        "the dungeon state does not ask whether the dungeon's own city is a Treasurer city (T1)")


def test_a_kills_gold_and_the_salvage_payout_take_the_four_where_t1_to_t3_say() -> None:
    tree = code(TREE_CPP)
    gold = flat(body(tree, "float UCataclysmEmpireTree::GoldMultiplierFor(", "CataclysmEmpireTree.cpp"))
    increased = gold[gold.index("const float Increased"):gold.index("const float Stimulus")]
    assert "Bonuses.GoldAndMaterialsPerTreasurerCity * static_cast<float>(FMath::Max(0, State.TreasurerCities))" in increased, (
        "The Golden Touch's increase is not added for each Treasurer city (T3)")
    assert "(State.bDungeonCityIsTreasurer ? Bonuses.GoldIncreasedOnTreasurerCity : 0.0f)" in increased, (
        "Commercial Hubs' increase is not added while the dungeon's city is a Treasurer city (T1)")
    assert "State.bDungeonCityIsTreasurer ? FMath::Max(0.0f, Bonuses.GoldMoreFromTreasurerCities) : 0.0f" in gold, (
        "Economic Zones' more does not depend on the dungeon's city (T2)")
    assert re.search(r"return .*\* \(1\.0f \+ Zones\);", gold), "Economic Zones' more does not multiply a kill's gold"

    pay = flat(body(tree, "FCataclysmEmpireTreeClearPay UCataclysmEmpireTree::ClearPayFor(", "CataclysmEmpireTree.cpp"))
    salvage = pay[pay.index("if (Bonuses.GoldPerFloorAtClear"):pay.index("if (Bonuses.UnpickedGoldShareAtClear")]
    cleanup = pay[pay.index("if (Bonuses.UnpickedGoldShareAtClear"):]
    assert "State.bDungeonCityIsTreasurer ? FMath::Max(0.0, static_cast<double>(Bonuses.GoldMoreFromTreasurerCities)) : 0.0" in salvage, (
        "the Salvage Rights payout does not take Economic Zones' more on a Treasurer city (T2)")
    assert salvage.rstrip().endswith("* (1.0 + Zones)); }"), "the Salvage Rights payout is not multiplied by the more"
    assert "Zones" not in cleanup and "Treasurer" not in cleanup, (
        "Cleanup Crew is raised by a slice 7 node, which B3 forbids: the piles were already raised")
    assert "GoldIncreasedOnTreasurerCity" not in salvage and "GoldAndMaterialsPerTreasurerCity" not in salvage, (
        "Commercial Hubs or The Golden Touch raises a payout, which is not dropped gold")


def test_the_day_pays_trade_routes_and_raises_pax_and_tax_by_the_treasurer_share() -> None:
    income = flat(body(code(TREE_CPP), "FCataclysmEmpireTreeDailyIncome UCataclysmEmpireTree::DailyIncomeFor(",
                       "CataclysmEmpireTree.cpp"))
    assert "State.TreasurerCitizens) / static_cast<double>(State.Citizens)" in income, (
        "Pax and Tax are not raised by the share of citizens living in Treasurer cities (T2)")
    pax = income[income.index("Income.Pax ="):income.index(";", income.index("Income.Pax ="))]
    tax = income[income.index("Income.Tax ="):income.index(";", income.index("Income.Tax ="))]
    interest = income[income.index("Income.Interest ="):income.index(";", income.index("Income.Interest ="))]
    assert "* CitizensRaised" in pax and "* CitizensRaised" in tax, "Pax or Tax leaves Economic Zones' share out"
    assert "CitizensRaised" not in interest and "Zones" not in interest, (
        "Wealth Interest is raised by Economic Zones; the gold held is tied to no city (T2)")

    trade = income[income.index("if (Bonuses.GoldEveryTwentyDaysPerTreasurerCity"):]
    trade = trade[:trade.index("}") + 1]
    assert re.search(r"Day > 0 && Day % TradeRoutesDaysAStep == 0", trade), "Trade Routes is not paid every 20th day"
    for part in ("Income.Trade =", "static_cast<double>(State.TreasurerCities) * Scale * Raised * (1.0 + Zones)"):
        assert part in trade, f"Trade Routes' payment does not hold {part!r}"

    assert re.search(r"Total\(\)\s*const\s*\{\s*return\s+Pax\s*\+\s*Interest\s*\+\s*Tax\s*\+\s*Trade\s*;", code(TREE_H)), (
        "the day's total leaves Trade Routes out")
    text = INSTANCE.read_text(encoding="utf-8")
    assert "Trade Routes %lld" in text and "Income.Trade" in code(INSTANCE), (
        "the day hook's log line does not say what Trade Routes paid")


def test_the_golden_touchs_materials_reach_the_material_roll_and_draw_nothing() -> None:
    rules = flat(body(code(TREE_CPP), "FCataclysmEmpireTreeDropRules UCataclysmEmpireTree::DropRulesFor(",
                      "CataclysmEmpireTree.cpp"))
    assert ("Rules.MaterialsMultiplier = 1.0f + FMath::Max(0.0f, Bonuses.GoldAndMaterialsPerTreasurerCity) "
            "* static_cast<float>(FMath::Max(0, State.TreasurerCities));") in rules, (
        "the drop rules do not carry The Golden Touch's materials for each Treasurer city (T4)")
    rolls = flat(body(code(TREE_H), "bool RollsAnything() const", "CataclysmEmpireTree.h"))
    assert "Materials" not in rolls, "the materials figure counts as a roll, so a kill would draw more than it did"

    handler = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    drops = re.search(r"UCataclysmDropSpawner::SpawnDropsFor\(([^;]*)\);", handler)
    # NOT THE LAST ARGUMENT SINCE ISSUE #51 SLICE B5: the Cataclysm boss's piece and the gear's step follow it.
    assert drops and re.search(r"CityRewards\.MaterialsMore \* TreeDrops\.MaterialsMultiplier\s*(,|$)",
                               flat(drops.group(1)).rstrip()), (
        "the kill does not give its material roll The Golden Touch's figure beside the city's")


def test_the_four_are_built_in_slice_7_and_none_waits_on_a_treasurer_upgrade() -> None:
    built = {name: gen.EMPIRE_TREE_BUILT.get(name) for name in SLICE_7}
    assert built == dict.fromkeys(SLICE_7, 7), built
    assert "treasurer upgrade" not in gen.EMPIRE_TREE_SYSTEMS, (
        "EMPIRE_TREE_SYSTEMS still has a reason no node waits on")
    assert "treasurer upgrade" not in gen.EMPIRE_TREE_REFUSED.values()
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        waiting = [row["NodeName"] for row in csv.DictReader(handle) if "Treasurer" in row["WaitsOn"]]
    assert not waiting, f"these nodes still say they wait on a Treasurer city upgrade: {waiting}"
