"""City upgrades cost gold: the prices, where they are read, and what a purchase pays from and saves.

WHY THIS EXISTS. Issue #2742's second slice. The project owner ruled on 2026-11-18 that city upgrades are the first
thing gold buys and that an upgrade stays instant, and the coordinating session priced them the same day (G4: 10,000
gold at upgrade tier 1 and difficulty tier 1, 2x and 4x at upgrade tiers 2 and 3, scaled to the run's tier by the same
scale as a gold pile). The `Cataclysm.CityUpgradePrice.` automation tests check what the game does; continuous
integration compiles no C++, so this file checks, by reading it, that the pieces are wired where the rulings put them,
and that the workbook, the generated table and the design document state the same prices.

WHAT IS CHECKED:

  1. the City Upgrades sheet's three price columns are what `game/Data/CityUpgrades.csv` was generated from, every row
     with a tier ladder at 10,000, 20,000 and 40,000 and the one without none;
  2. the generator refuses a price on a row with no ladder, a missing price, a price that falls with the upgrade tier
     and one that is not a whole number above nought;
  3. the design document's price at each difficulty tier is the CSV's tier 1 price scaled by `Gold.csv`'s pile column,
     the scale `UCataclysmCityUpgradeMapping::PriceFor` reads;
  4. the empire layer is told the price and the gold held and decides, checking the price last, and holds no price of
     its own;
  5. one function charges -- `UCataclysmCityUpgradePurchase::Buy` -- taking the gold before the run records the
     upgrade, and the city screen and the console command both buy through it;
  6. a purchase raises `CityUpgradeBought`, which writes the run and character records even after another write that
     frame, and `AccountChanged` when the gold came from the account.

WHAT IS NOT. That any of it does what it says when run; the automation tests do.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import generate_datatables as gen  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
DATA = REPO_ROOT / "game" / "Data"
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: G4: an upgrade's price at upgrade tiers 1, 2 and 3, at difficulty tier 1.
LADDER_PRICES = (10000, 20000, 40000)

#: The sentence of the design document that states the price at every difficulty tier.
PRICE_SENTENCE = re.compile(
    r"Every upgrade costs ([\d,]+) gold at upgrade tier 1 on difficulty tier 1.*?The price follows the run's "
    r"difficulty tier by the same scale as a gold pile: (.*?), so an upgrade costs", re.S)


def strip_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, string literals kept."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j == -1 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def code(relative: str) -> str:
    path = SOURCE / relative
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. If it moved, move it here too.")
    return squeeze(strip_comments(path.read_bytes().decode("utf-8")))


def braced_from(text: str, start: int, what: str) -> str:
    open_at = text.find("{", start)
    assert open_at != -1, f"{what} has no opening brace"
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    raise AssertionError(f"{what} is never closed")


def body(relative: str, signature: str) -> str:
    text = code(relative)
    signature = squeeze(signature)
    assert text.count(signature) == 1, (
        f"{signature} should be defined once in {relative}; it appears {text.count(signature)} times")
    return braced_from(text, text.find(signature), signature)


def rows(name: str) -> list[dict[str, str]]:
    path = DATA / name
    if not path.is_file():
        pytest.fail(f"game/Data/{name} does not exist; run tools/generate_datatables.py")
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


# --------------------------------------------------------------------------
# 1 and 2: the prices, in the workbook and the generated table, and what the generator refuses
# --------------------------------------------------------------------------

def test_every_upgrade_with_a_tier_ladder_is_priced_and_the_one_without_is_not() -> None:
    priced = [row for row in rows("CityUpgrades.csv") if row["Tier2Raw"] or row["Tier3Raw"]]
    unpriced = [row for row in rows("CityUpgrades.csv") if not (row["Tier2Raw"] or row["Tier3Raw"])]
    assert len(priced) == 23 and len(unpriced) == 1, (
        f"CityUpgrades.csv has {len(priced)} rows with a tier ladder and {len(unpriced)} without; the sheet had 23 and 1 "
        "when the prices were ruled (G4), so a new row needs its price ruled too")
    wrong = [row["Name"] for row in priced
             if (int(row["Tier1Gold"]), int(row["Tier2Gold"]), int(row["Tier3Gold"])) != LADDER_PRICES]
    assert not wrong, f"these upgrades are not priced 10,000, 20,000 and 40,000 (G4): {wrong}"
    assert all(int(row[f"Tier{t}Gold"]) == 0 for row in unpriced for t in (1, 2, 3)), (
        "the upgrade with no tier ladder, the unbranched last resort, has a price; there is no tier to price")


def test_the_workbook_city_upgrades_sheet_is_what_the_table_was_generated_from() -> None:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")
    book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
    generated = {row["Name"]: (int(row["Tier1Gold"]), int(row["Tier2Gold"]), int(row["Tier3Gold"]))
                 for row in gen.city_upgrades(book)}
    table = {row["Name"]: (int(row["Tier1Gold"]), int(row["Tier2Gold"]), int(row["Tier3Gold"]))
             for row in rows("CityUpgrades.csv")}
    assert generated == table, (
        "the City Upgrades sheet's prices and game/Data/CityUpgrades.csv differ: run tools/generate_datatables.py")


@pytest.mark.parametrize("cells, has_ladder, refused", [
    (["10000", "20000", "40000"], True, None),
    (["", "", ""], False, None),
    (["10000", "", ""], False, "has a price but no tier ladder"),
    (["10000", "", "40000"], True, "is empty"),
    (["10000", "5000", "40000"], True, "cannot cost less"),
    (["10000.5", "20000", "40000"], True, "whole number of gold above nought"),
    (["0", "20000", "40000"], True, "whole number of gold above nought"),
], ids=["a ladder priced", "no ladder, no price", "a price with no ladder", "a missing price", "a falling price",
        "a fraction", "nought"])
def test_the_generator_reads_the_three_prices_and_refuses_a_wrong_one(cells, has_ladder, refused) -> None:
    headers = {column: index for index, column in enumerate(gen.CITY_UPGRADE_GOLD_COLUMNS)}
    if refused is None:
        prices = gen._city_upgrade_prices(cells, headers, 7, "an upgrade", has_ladder)
        assert prices == ([10000, 20000, 40000] if has_ladder else [0, 0, 0])
        return
    with pytest.raises(gen.DataError, match=refused):
        gen._city_upgrade_prices(cells, headers, 7, "an upgrade", has_ladder)


# --------------------------------------------------------------------------
# 3: the price at each difficulty tier, in the design document and by the pile scale
# --------------------------------------------------------------------------

def scaled_prices() -> dict[int, int]:
    """The tier 1 price at each difficulty tier, scaled as `UCataclysmCityUpgradeMapping::PriceFor` scales it."""
    piles = {int(row["DifficultyTier"]): float(row["GoldPerPile"]) for row in rows("Gold.csv")}
    tier_one = LADDER_PRICES[0]
    return {tier: round(tier_one * pile / piles[1]) for tier, pile in piles.items()}


def test_the_document_states_the_price_at_every_tier_as_the_pile_scale_gives_it() -> None:
    document = DESIGN_DOCUMENT.read_bytes().decode("utf-8")
    match = PRICE_SENTENCE.search(document)
    assert match, (
        f"{DESIGN_DOCUMENT.name}'s City Upgrades section no longer states the price at every tier in the sentence "
        "this reads; the comparison below would pass having compared nothing")
    stated = {1: int(match.group(1).replace(",", ""))}
    for amount, tier in re.findall(r"([\d,]+) at (?:tier )?(\d)", match.group(2)):
        stated[int(tier)] = int(amount.replace(",", ""))
    assert stated == scaled_prices(), (
        f"{DESIGN_DOCUMENT.name} states {stated} and the CSVs give {scaled_prices()} (the tier 1 price times Gold Per "
        "Pile over its tier 1 figure). Regenerate the tables or correct the document.")


def test_the_price_is_scaled_by_the_pile_column_the_gold_drops_read() -> None:
    price = body("Cataclysm/Data/CataclysmCityUpgradeMapping.cpp",
                 "int64 UCataclysmCityUpgradeMapping::PriceFor(FName RowName, int32 DifficultyTier)")
    assert "UCataclysmDropRoll::GoldRow(GoldTable, DifficultyTier)" in price, "the price is not read at the tier asked"
    assert "UCataclysmDropRoll::GoldRow(GoldTable, 1)" in price, "the price is not scaled against tier 1's pile"
    assert "PriceAt(Row->Tier1Gold, AtTier->GoldPerPile, AtTierOne->GoldPerPile)" in price, (
        "the price is not the row's tier 1 gold times the pile at the tier over the pile at tier 1")
    purchase = body("Cataclysm/Player/CataclysmCityUpgradePurchase.cpp",
                    "int64 UCataclysmCityUpgradePurchase::PriceIn(const UCataclysmEmpireRun* Run, FName RowName)")
    assert "Run->DifficultyTier" in purchase, "a purchase is not priced at the run's own difficulty tier"


# --------------------------------------------------------------------------
# 4 and 5: who decides, and who charges
# --------------------------------------------------------------------------

def test_the_empire_layer_is_told_the_price_and_the_gold_and_checks_the_price_last() -> None:
    header = code("CataclysmEmpire/Empire/CataclysmCityUpgrade.h")
    assert "GoldCost =" not in header and "IsFreeAndInstant" not in header, (
        "UCataclysmCityUpgradeRules holds a price of its own again; the price is data, in the Cataclysm module")
    would = body("CataclysmEmpire/Empire/CataclysmEmpireRun.cpp",
                 "ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldBuyCityUpgrade(")
    gold = would.find("if (GoldCost > 0 && GoldHeld < GoldCost)")
    assert gold != -1, "WouldBuyCityUpgrade does not refuse a price the gold held does not meet"
    for earlier in ("ECataclysmCityUpgradeResult::CityHasFallen", "ECataclysmCityUpgradeResult::NoSlotsLeft",
                    "ECataclysmCityUpgradeResult::AlreadyBought", "ECataclysmCityUpgradeResult::CityIsBesieged"):
        assert would.find(earlier) != -1 and would.find(earlier) < gold, (
            f"the price is checked before {earlier.rsplit('::', 1)[1]}, so a player is told of gold when no gold would "
            "change the answer")


def test_one_function_charges_and_takes_the_gold_before_the_run_records_the_upgrade() -> None:
    charging = sorted(path.relative_to(SOURCE).as_posix() for path in SOURCE.rglob("*.cpp")
                      if "/Tests/" not in path.as_posix()
                      and "SpendGoldFor(" in squeeze(strip_comments(path.read_bytes().decode("utf-8")))
                      and not path.name.endswith("CataclysmGameInstance.cpp"))
    assert charging == ["Cataclysm/Player/CataclysmCityUpgradePurchase.cpp"], (
        f"gold is spent in {charging}; UCataclysmCityUpgradePurchase::Buy is the one place that charges")
    buy = body("Cataclysm/Player/CataclysmCityUpgradePurchase.cpp",
               "ECataclysmCityUpgradeResult UCataclysmCityUpgradePurchase::Buy(")
    asked = buy.find("Run->WouldBuyCityUpgrade(CityId, Upgrade, Price, Held)")
    spent = buy.find("UCataclysmGameInstance::SpendGoldFor(WorldContext, Price, bFromTheCharacter)")
    bought = buy.find("Run->BuyCityUpgrade(CityId, Upgrade, Price, Held)")
    assert -1 not in (asked, spent, bought) and asked < spent < bought, (
        "Buy does not ask the run, then take the gold, then have the run record the upgrade, in that order; an upgrade "
        "could be held unpaid")


def test_every_refusal_has_words_of_its_own() -> None:
    """Each reason a purchase can be refused has a sentence in `ResultText`, so no refusal reads "Refused.".

    A besieged city's did until issue #2742's second slice: the city screen and the console command said "Refused."
    for it. Found in passing and fixed at the coordinating session's request of 2026-11-18.
    """
    header = code("CataclysmEmpire/Empire/CataclysmCityUpgrade.h")
    declared = braced_from(header, header.find("enum class ECataclysmCityUpgradeResult"), "ECataclysmCityUpgradeResult")
    results = re.findall(r"(\w+)\s*=\s*\d+", declared)
    assert len(results) >= 11, f"ECataclysmCityUpgradeResult reads as {results}; this check would compare too little"
    worded = body("CataclysmEmpire/Empire/CataclysmCityUpgrade.cpp",
                  "FString UCataclysmCityUpgradeRules::ResultText(")
    unworded = [name for name in results if f"case ECataclysmCityUpgradeResult::{name}:" not in worded]
    assert not unworded, (
        f"UCataclysmCityUpgradeRules::ResultText has no case for {unworded}, so a purchase refused for it says "
        "\"Refused.\" on the city screen and the console")


@pytest.mark.parametrize("relative, signature", [
    ("Cataclysm/Interface/CataclysmCityScreenWidget.cpp", "void UCataclysmCityScreenWidget::HandleOfferClicked("),
    ("Cataclysm/Character/CataclysmPlayerCharacter.cpp", "GCataclysmBuyCityUpgrade("),
], ids=["the city screen", "the console command"])
def test_every_purchase_goes_through_the_one_that_charges(relative: str, signature: str) -> None:
    text = code(relative)
    at = text.find(squeeze(signature))
    assert at != -1, f"{signature} is not in {relative}"
    purchase = braced_from(text, at, signature)
    assert "UCataclysmCityUpgradePurchase::Buy(" in purchase, f"{signature} does not buy through the one that charges"
    assert "->BuyCityUpgrade(" not in purchase, f"{signature} asks the run to buy directly, which charges nothing"


# --------------------------------------------------------------------------
# 6: what a purchase saves
# --------------------------------------------------------------------------

def test_a_purchase_writes_the_records_it_changed() -> None:
    buy = body("Cataclysm/Player/CataclysmCityUpgradePurchase.cpp",
               "ECataclysmCityUpgradeResult UCataclysmCityUpgradePurchase::Buy(")
    assert ("if (Price > 0 && !bFromTheCharacter) { UCataclysmSaveWriter::NoteTriggerIn(World, "
            "ECataclysmSaveTrigger::AccountChanged); }") in buy, (
        "a purchase paid from the account does not raise AccountChanged, so the account's gold is not written")
    assert "UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::CityUpgradeBought);" in buy, (
        "a purchase does not raise CityUpgradeBought, so the run record's upgrade waits for the clock")
    triggers = "Cataclysm/Save/CataclysmSaveTriggers.cpp"
    character = body(triggers, "bool UCataclysmSaveTriggers::WritesTheCharacterRecord(")
    assert "case ECataclysmSaveTrigger::CityUpgradeBought:" in character, (
        "CityUpgradeBought does not write the character record, which holds a Solo Self-Found character's gold")
    again = body(triggers, "bool UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame(")
    assert "ECataclysmSaveTrigger::CityUpgradeBought" in again, (
        "a purchase's run record write can be skipped behind an earlier write that frame, which gathered the city "
        "before it held the upgrade")
    listed = body(triggers, "TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every(")
    assert "ECataclysmSaveTrigger::CityUpgradeBought" in listed, "the trigger is not in the list its tests walk"
