"""The empire upgrade tree's slice 6a: the gold a kill drops and a city upgrade's price, and no false word about gold.

WHY THIS EXISTS. Issue #2741 slice 6a, the coordinating session's rulings of 2026-11-20. Gold has been a system the game
has since #2742, so the 41 nodes the tree refused for gold now say what they really wait on, and eleven of them act:
eight raise the gold a kill drops (G1 to G3) and three lower a city upgrade's price (P1, P2). Price Controls is 6b's:
its P3 reading, "below its maximum now", was withdrawn the same day.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeGold.` automation
tests never run on a pull request. And the wiring is where no automation test reaches: a kill's own call, and the one
price function every screen and the purchase share.

WHAT IS CHECKED:

  1. no node's refusal and no comment in the game's source says the game has no gold, or that a city upgrade is free;
  2. no node is refused for its gold rules, which slice 6d finished, and none for gold or a gold cost;
  3. a kill passes its drop rules' gold multiplier to `SpawnGoldFor`, which multiplies each pile after its draws and
     leaves it as rolled at exactly 1; and the drop rules carry `GoldMultiplierFor`;
  4. the steps C++ counts in are the nodes' own words, and the price cap is the ruling's 75%;
  5. there is one price: `PriceIn` reads the tree and the city, and the city screen's offers, the purchase and the
     console command all ask it with a context and a city.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
TREE = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree"
ENEMY = SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp"
SPAWNER = SOURCE / "Cataclysm" / "Items" / "CataclysmDroppedItem.cpp"
PURCHASE = SOURCE / "Cataclysm" / "Player" / "CataclysmCityUpgradePurchase.cpp"
LAYOUT = SOURCE / "Cataclysm" / "Interface" / "CataclysmCityScreenLayout.cpp"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: NONE: slice 6c built the last nodes its gold rules reached; the owner's rulings of 2026-11-20 sent the rest to 6d
#: and 6e.
SLICE_6B: set[str] = set()

#: WORDS NO SCREEN OR COMMENT MAY SAY ANY MORE, each true until #2742 and false since.
FALSE_GOLD = ("gold, which the game does not have yet", "free and immediate today", "and there is no gold",
              "and there is no gold. Issue", "Issues #41 and #1264")


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


def test_nothing_the_game_shows_or_says_claims_there_is_no_gold() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    shown = [row["NodeName"] for row in rows
             for column in ("WaitsOn", "Option1WaitsOn", "Option2WaitsOn", "Option3WaitsOn")
             if any(words in row.get(column, "") for words in FALSE_GOLD)]
    assert not shown, f"the empire tree screen still tells the player the game has no gold: {shown}"
    said = [f"{path.relative_to(REPO_ROOT)}: {words!r}"
            for path in sorted(SOURCE.rglob("*.*")) if path.suffix in (".h", ".cpp")
            for words in FALSE_GOLD if words in path.read_text(encoding="utf-8", errors="replace")]
    assert not said, "a source file still says the game has no gold:\n" + "\n".join(said)


def test_no_node_waits_on_its_gold_rules_and_none_on_gold() -> None:
    waiting = {name for name, system in gen.EMPIRE_TREE_REFUSED.items() if system == "gold rules"}
    assert waiting == SLICE_6B, (sorted(waiting - SLICE_6B), sorted(SLICE_6B - waiting))
    assert not set(gen.EMPIRE_TREE_REFUSED.values()) & {"gold", "upgrade cost"}, (
        "a node is still refused for gold or a gold cost, which the game has had since #2742")


def test_a_kill_multiplies_its_gold_after_the_draws_and_the_rules_carry_the_figure() -> None:
    handler = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    call = re.search(r"UCataclysmDropSpawner::SpawnGoldFor\(([^;]*)\);", handler)
    assert call is not None, "the kill no longer drops gold through SpawnGoldFor"
    # THE LAST ARGUMENT, TIMES THE CITY'S "MORE GOLD" since issue #2781.
    last = call.group(1).rsplit(",", 1)[-1]
    assert "TreeDrops.GoldMultiplier" in last, (
        f"the kill does not pass its drop rules' gold multiplier: SpawnGoldFor({call.group(1).strip()})")

    spawner = body(code(SPAWNER), "int32 UCataclysmDropSpawner::SpawnGoldFor(", "CataclysmDroppedItem.cpp")
    rolled = spawner.find("UCataclysmDropRoll::RollGoldPile(")
    multiplied = spawner.find("GoldMultiplier", rolled)
    placed = spawner.find("PutOnTheFloor(", rolled)
    assert -1 < rolled < multiplied < placed, (
        "SpawnGoldFor does not multiply each pile between its roll and putting it down, so the multiplier could draw")
    assert re.search(r"GoldMultiplier\s*==\s*1\.0f\s*\?\s*Rolled", spawner), (
        "SpawnGoldFor does not leave a pile as rolled at a multiplier of exactly 1")

    tree = code(TREE.with_suffix(".cpp"))
    rules = body(tree, "FCataclysmEmpireTreeDropRules UCataclysmEmpireTree::DropRulesFor(", "CataclysmEmpireTree.cpp")
    assert re.search(r"Rules\.GoldMultiplier\s*=\s*GoldMultiplierFor\(\s*Bonuses\s*,\s*State\s*\)", rules), (
        "DropRulesFor does not carry the gold multiplier a kill reads")


def test_the_steps_are_the_nodes_words_and_the_price_cap_is_the_ruling() -> None:
    header = code(TREE.with_suffix(".h"))
    citizens = re.search(r"ProsperityCitizensAStep\s*=\s*(\d+)\s*;", header)
    upgrades = re.search(r"FiscalPolicyUpgradesAStep\s*=\s*(\d+)\s*;", header)
    cap = re.search(r"UpgradePriceReductionCap\s*=\s*([0-9.]+)f\s*;", header)
    assert citizens and upgrades and cap, "CataclysmEmpireTree.h no longer states the two steps and the price cap"
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        words = {row["NodeName"]: row["Description"] for row in csv.DictReader(handle)}
    assert f"every {int(citizens.group(1)):,} citizens" in words["Prosperity Index"], words["Prosperity Index"]
    assert f"every {int(upgrades.group(1))} active City upgrades" in words["Fiscal Policy"], words["Fiscal Policy"]
    # P1 OF 2026-11-20, A JUDGEMENT: the reductions add and are held at 75%, so a price never falls below a quarter.
    assert float(cap.group(1)) == 0.75


def test_there_is_one_price_and_every_screen_and_the_purchase_ask_it() -> None:
    purchase = code(PURCHASE)
    price = body(purchase, "int64 UCataclysmCityUpgradePurchase::PriceIn(", "CataclysmCityUpgradePurchase.cpp")
    for read in ("UCataclysmEmpireTree::UpgradePriceFactorFor(", "UCataclysmGameInstance::EmpireTreePlayerBonusesFor("):
        assert read in price, f"PriceIn does not read {read}"
    buy = body(purchase, "ECataclysmCityUpgradeResult UCataclysmCityUpgradePurchase::Buy(",
               "CataclysmCityUpgradePurchase.cpp")
    assert re.search(r"PriceIn\(\s*WorldContext\s*,\s*Run\s*,\s*RowName\s*,\s*CityId\s*\)", buy), (
        "Buy does not charge the price PriceIn gives for this city")
    offers = body(code(LAYOUT), "TArray<FCataclysmCityUpgradeOffer> UCataclysmCityScreenLayout::OffersFor(",
                  "CataclysmCityScreenLayout.cpp")
    assert re.search(r"PriceIn\(\s*WorldContext\s*,\s*Run\s*,\s*RowName\s*,\s*CityId\s*\)", offers), (
        "the city screen's offers do not show the price PriceIn gives for this city")

    calls = []
    for path in sorted(SOURCE.rglob("*.cpp")):
        text = code(path)
        for match in re.finditer(r"UCataclysmCityUpgradePurchase::PriceIn\(([^;]*?)\)\s*[;)]", text):
            calls.append((path.name, match.group(1)))
    assert len(calls) >= 6, f"only {len(calls)} calls of PriceIn were found; the search reads the wrong files"
    short = [f"{name}: PriceIn({arguments.strip()})" for name, arguments in calls
             if arguments.count(",") != 3]
    assert not short, "a call of PriceIn does not name a context and a city:\n" + "\n".join(short)
