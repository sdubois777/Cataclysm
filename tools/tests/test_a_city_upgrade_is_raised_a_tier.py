"""A bought city upgrade is raised to tier 2 and tier 3 in place.

WHY THIS EXISTS. Issue #1265, the coordinating session's rulings of 2026-11-22 (Q6 to Q11). Continuous integration
compiles no C++, so the automation tests in `CataclysmCityUpgradeRaiseTests.cpp` and
`CataclysmCityUpgradeRaiseScreenTests.cpp` never run on a pull request; these checks read the source for each ruling.

WHAT IS CHECKED:

  1. the run refuses a raise in the ruled order, takes no slot for it, and asks for the next tier only (Q6, Q8);
  2. a finished build of a held row is a raise: the map raises it in place and keeps its first finished day, a raised
     maximum is folded in as (1 + value), a one-time upgrade fires again, an interval keeps the sooner trigger (Q7,
     Q9 to Q11);
  3. the purchase prices and builds a raise at its tier through the purchase's own body, and the screen and the
     console reach it.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
GAME = SOURCE / "Cataclysm"


def code(path: pathlib.Path) -> str:
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return re.sub(r"\s+", " ", text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


def in_order(text: str, *parts: str) -> bool:
    places = [text.find(part) for part in parts]
    return -1 not in places and places == sorted(places)


def test_the_run_refuses_a_raise_in_order_and_takes_no_slot() -> None:
    would = body(code(EMPIRE / "CataclysmEmpireRun.cpp"),
                 "ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldRaiseCityUpgrade(")
    assert in_order(would,
                    "return ECataclysmCityUpgradeResult::NoTierToRaiseTo;",
                    "return ECataclysmCityUpgradeResult::CityHasFallen;",
                    "return ECataclysmCityUpgradeResult::CityIsBesieged;",
                    ": ECataclysmCityUpgradeResult::NotHeld;",
                    "return ECataclysmCityUpgradeResult::AtTheTopTier;",
                    "if (Raised.Tier != Held->Tier + 1)",
                    "if (!City->Building.IsEmpty())",
                    "return ECataclysmCityUpgradeResult::NotEnoughGold;"), "the raise's refusals are not in the ruled order"
    assert "FreeUpgradeSlots" not in would, "a raise is refused for the slots, but it takes none (Q6)"
    assert "Held->Tier >= UCataclysmCityUpgradeRules::HighestTier" in would
    assert "static constexpr int32 HighestTier = 3;" in code(EMPIRE / "CataclysmCityUpgrade.h")


def test_a_finished_build_of_a_held_row_is_a_raise_in_place() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    complete = body(run, "void UCataclysmEmpireRun::CompleteCityUpgrade(")
    assert in_order(complete, "Holder->HasUpgrade(Bought.RowName)", "Map->RaiseUpgrade(CityId, Bought, Day);",
                    "Bought.BoughtDay = Day;", "Map->AddUpgrade(CityId, Bought);")
    assert complete.index("RemoveShareOfDungeonsOn(CityId, Bought.Value);") > complete.index(
        "Map->AddUpgrade(CityId, Bought);"), "a raise of the dungeon removal does not fire again (Q9)"
    raise_map = body(code(EMPIRE / "CataclysmEmpireMap.cpp"), "bool UCataclysmEmpireMap::RaiseUpgrade(")
    assert "BoughtDay" not in raise_map, "a raise moves the day Enduring Foundations counts from"
    assert "City->MaxDefence *= (1.0f + Raised.Value) / OldFactor;" in raise_map, "a raised maximum is not refolded (Q11)"
    assert "City->MaxPopulation *= (1.0f + Raised.Value) / OldFactor;" in raise_map
    assert "FMath::Min(Held->NextTriggerDay, Next)" in raise_map, "an interval does not keep the sooner trigger (Q10)"
    assert "RestoreOnce(*City, Raised.Effect, Raised.Value);" in raise_map, "a raised restore does not fire again (Q9)"
    assert "Held->Tier = Raised.Tier;" in raise_map and "Held->Value = Raised.Value;" in raise_map


def test_the_purchase_prices_and_builds_a_raise_at_its_tier_through_one_body() -> None:
    purchase = code(GAME / "Player" / "CataclysmCityUpgradePurchase.cpp")
    raise_body = body(purchase, "ECataclysmCityUpgradeResult UCataclysmCityUpgradePurchase::Raise(")
    assert "UCataclysmCityUpgradeMapping::MakeAtTier(RowName, Tier)" in raise_body
    assert "return ECataclysmCityUpgradeResult::NoTierToRaiseTo;" in raise_body
    # THE COMMENTS ARE STRIPPED, so the `bRaise` argument reads as a bare `true`.
    assert "PayAndBuild(WorldContext, Run, CityId, RowName, bRush, Raised, Tier, true)" in raise_body, (
        "a raise is not charged through the purchase's own body")
    days = body(purchase, "int32 UCataclysmCityUpgradePurchase::BuildDaysIn(")
    assert "UCataclysmCityUpgradeRules::BuildDaysFor(UpgradeTier)" in days, "a raise takes tier 1's days"
    mapping = code(GAME / "Data" / "CataclysmCityUpgradeMapping.cpp")
    has_tier = body(mapping, "bool UCataclysmCityUpgradeMapping::HasTier(")
    assert "Row.Tier2Raw" in has_tier and "Row.Tier3Raw" in has_tier, "a tier is not read from the row's raw cells"


def test_the_screen_and_the_console_reach_a_raise() -> None:
    layout = code(GAME / "Interface" / "CataclysmCityScreenLayout.cpp")
    add = body(layout, "void UCataclysmCityScreenLayout::AddTheRaise(")
    assert "Run->WouldRaiseCityUpgrade(CityId, Raised, Offer.RaisePrice, GoldHeld + Credit)" in add
    what = body(layout, "FString UCataclysmCityScreenLayout::RaiseWhatFor(")
    assert "removes %d%% of this city's dungeons when built" in what, "the ruled example's words are not said"
    widget = code(GAME / "Interface" / "CataclysmCityScreenWidget.cpp")
    assert "UCataclysmCityUpgradePurchase::Raise(this, Run, CityId, Value, bRush)" in widget
    character = code(GAME / "Character" / "CataclysmPlayerCharacter.cpp")
    assert 'TEXT("Cataclysm.RaiseCityUpgrade")' in character
    assert "UCataclysmCityUpgradePurchase::Raise(World, Run, CityId, RowName, bRush)" in character
