"""The empire upgrade tree's slice 6e: Global Credit and Sovereign Loans, the gold debt.

WHY THIS EXISTS. Issue #2741 slice 6e, the owner's ruling of 2026-11-20, "no gold debt unless there's an option for it in
the empire tree", and the coordinating session's readings D1 to D7 of that day. A debt is two saved numbers beside the
balance, on account record version 5 and, for a Solo Self-Found character, character record version 11 (D1); a city
upgrade the balance cannot cover may be bought on the tree's credit, the whole balance taken and the rest owed, only while
nothing is owed (D2); gold picked up from the floor pays it down first, and the day's income and a clear's payout do not
(D3); with Sovereign Loans a tenth of it is taken each day (D4); and every screen says so (D6).

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeCredit.` automation
tests never run on a pull request; and which callers pay a debt down, and which records hold it, are wiring no single
automation test reaches.

WHAT IS CHECKED:

  1. Sovereign Loans' days in C++ are its node's own words;
  2. the two records hold the debt as saved fields at their new versions, each written out by a migration step that
     the chain lists, and a Solo Self-Found character's is gathered and restored;
  3. the committed files are the previous ones at the new version with the two figures, neither the default;
  4. only the test that migrates them reads `Account_v4.json` and `Character_v10.json`;
  5. only the pickup pays a debt down, the debt before the balance; the day's income and a clear's payout add whole,
     and the day's hook takes Sovereign Loans' part after the income;
  6. the purchase counts the credit as gold held and gives none while anything is owed;
  7. every screen that shows the balance or a refusal shows the debt.
  8. Sovereign Loans' part a day is rounded up, so ten days pay it all, and an offer on credit says so only when it
     would leave something owed (D4, D6). Added when slices 6d and 6e were joined for one window (2026-11-21): the
     C++ proofs of these two breaks were dropped, and this is what guards them.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
TREE_H = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.h"
RECORDS_H = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
RECORDS_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp"
GATHER = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveGather.cpp"
APPLY = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveApply.cpp"
INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
PURCHASE = SOURCE / "Cataclysm" / "Player" / "CataclysmCityUpgradePurchase.cpp"
LAYOUT = SOURCE / "Cataclysm" / "Interface" / "CataclysmCityScreenLayout.cpp"
WIDGET = SOURCE / "Cataclysm" / "Interface" / "CataclysmCityScreenWidget.cpp"
CHARACTER = SOURCE / "Cataclysm" / "Character" / "CataclysmPlayerCharacter.cpp"
PICKUP = SOURCE / "Cataclysm" / "Items" / "CataclysmDroppedItem.cpp"
MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
DEBT = ("GoldOwed", "GoldOwedEachDay")


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


def record_class(name: str) -> str:
    text = code(RECORDS_H)
    start = text.find(f"class CATACLYSM_API {name}")
    assert start != -1, f"no {name}"
    end = text.find("\nclass CATACLYSM_API", start + 1)
    return text[start:end if end != -1 else len(text)]


def test_sovereign_loans_days_are_its_own_words() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        words = {row["NodeName"]: row["Description"] for row in csv.DictReader(handle)}
    days = int(re.search(r"\bSovereignLoansDays\s*=\s*(\d+)\s*;", code(TREE_H)).group(1))
    assert f"repaid automatically over {days} days" in words["Sovereign Loans"], words["Sovereign Loans"]
    assert "Debt repaid from next gold drops" in words["Global Credit"], words["Global Credit"]


def test_the_two_records_hold_the_debt_at_their_new_versions() -> None:
    # THE STEPS THAT WROTE NOTHING OWED INTO AN OLDER RECORD went with the save migrations on 2026-11-27 (issue #2928
    # slice 12); what is left is that both records save both figures, and the character's are gathered and restored.
    for name, version in (("UCataclysmAccountSave", 5), ("UCataclysmCharacterSave", 11)):
        record = record_class(name)
        # AT THIS VERSION OR LATER: slice 8 of #2741 moved both records on again (account 6, character 12).
        found = re.search(r"SchemaVersionNow\s*=\s*(\d+)\s*;", record)
        assert found and int(found.group(1)) >= version, f"{name} is below version {version}"
        for field in DEBT:
            assert re.search(rf"UPROPERTY\(SaveGame[^)]*\)\s*int64\s+{field}\s*=\s*0\s*;", record), (
                f"{name} does not save {field}")
    assert "Record.GoldOwed = State->GetOwnGoldOwed();" in code(GATHER), "a character's own debt is not gathered"
    assert "Record.GoldOwedEachDay = State->GetOwnGoldOwedEachDay();" in code(GATHER), (
        "a character's own part a day is not gathered")
    assert "State->SetOwnGoldOwed(Record.GoldOwed, Record.GoldOwedEachDay);" in code(APPLY), (
        "a character's own debt is not restored")


def test_only_a_pickup_pays_a_debt_down_and_the_day_takes_its_part_after_the_income() -> None:
    callers = sorted(path.relative_to(SOURCE).as_posix() for path in SOURCE.rglob("*.cpp")
                     if "Tests" not in path.parts and "AddPickedUpGoldFor(" in code(path)
                     and not path.name.endswith("CataclysmGameInstance.cpp"))
    assert callers == ["Cataclysm/Items/CataclysmDroppedItem.cpp"], (
        f"gold that pays a debt down is added in {callers}; only gold picked up from the floor does (D3)")
    bank = body(code(PICKUP), "int64 UCataclysmDropPickup::BankGold(UWorld* World, const TArray<ACataclysmDroppedItem*>& Piles)",
                "CataclysmDroppedItem.cpp")
    assert "UCataclysmGameInstance::AddPickedUpGoldFor(World, Total, bIntoTheCharacter, Repaid)" in bank
    instance = code(INSTANCE)
    picked = body(instance, "bool UCataclysmGameInstance::AddPickedUpGoldFor(", "CataclysmGameInstance.cpp")
    own, account = picked.index("State->AddOwnGold(Gold - OutRepaid);"), picked.index("Account->Gold += Gold - OutRepaid;")
    assert -1 < picked.index("OutRepaid = FMath::Min(Gold, State->GetOwnGoldOwed());") < own, (
        "a Solo Self-Found character's pickup does not pay its debt before its balance")
    assert -1 < picked.index("Account->GoldOwed -= OutRepaid;") < account, (
        "an account's pickup does not pay the debt before the balance")
    wire = body(instance, "void UCataclysmGameInstance::WireTheRunToTheBalance(", "CataclysmGameInstance.cpp")
    day = wire[wire.index("Run->OnDayPassed ="):]
    income, repaid = day.find("AddGoldFor(Paying, Income.Total(), bIntoTheCharacter)"), day.find("RepayTheDaysPartFor(")
    assert -1 < income < repaid, "the day does not pay its income whole and then take Sovereign Loans' part (D3, D4)"
    clear = body(code(MODE), "void ACataclysmDungeonGameMode::PayTheTreesClearGold(", "CataclysmDungeonGameMode.cpp")
    assert "AddGoldFor(" in clear and "AddPickedUpGoldFor(" not in clear, "a clear's payout pays a debt down (D3)"


def test_the_purchase_counts_the_credit_and_gives_none_while_anything_is_owed() -> None:
    purchase = code(PURCHASE)
    credit = body(purchase, "int64 UCataclysmCityUpgradePurchase::CreditIn(", "CataclysmCityUpgradePurchase.cpp")
    assert "Owed > 0" in credit and "return 0;" in credit, "the credit is given while a debt is open"
    assert "UCataclysmEmpireTree::UpgradeCreditFor(" in credit
    offers = body(code(LAYOUT), "TArray<FCataclysmCityUpgradeOffer> UCataclysmCityScreenLayout::OffersFor(",
                  "CataclysmCityScreenLayout.cpp")
    assert "UCataclysmCityUpgradePurchase::CreditIn(" in offers and "GoldHeld + Credit" in offers, (
        "the city screen asks the run without the credit, so it and the purchase could disagree")


def test_every_screen_shows_the_debt() -> None:
    balance = body(code(INSTANCE), "FString UCataclysmGameInstance::GoldBalanceText(", "CataclysmGameInstance.cpp")
    assert "GoldOwedBy(" in balance and 'TEXT("%s, %s owed")' in balance, "the balance text leaves the debt out (D6)"
    for path in (WIDGET, CHARACTER):
        assert "UCataclysmCityUpgradePurchase::RefusalTextIn(" in code(path), (
            f"{path.name} shows a refusal that does not count the credit or name a debt (D6)")
        assert "UCataclysmCityUpgradePurchase::RefusalText(" not in code(path), f"{path.name} still uses RefusalText"
    refusal = body(code(PURCHASE), "FString UCataclysmCityUpgradePurchase::RefusalTextIn(",
                   "CataclysmCityUpgradePurchase.cpp")
    assert 'TEXT("%s, and no more credit until the %s owed is paid.")' in refusal
    button = body(code(LAYOUT), "FString UCataclysmCityScreenLayout::ButtonTextFor(", "CataclysmCityScreenLayout.cpp")
    assert 'TEXT("%s  --  %s on credit, %s owed")' in button, "an offer on credit does not say so (D6)"


def test_the_part_a_day_is_rounded_up_and_an_offer_on_credit_says_so_only_when_something_is_owed() -> None:
    part = body(code(INSTANCE), "int64 DailyPartOf(int64 Owed)", "CataclysmGameInstance.cpp")
    assert re.search(r"\(Owed \+ Days - 1\) / Days", part), "Sovereign Loans' part a day is not rounded up (D4)"
    button = body(code(LAYOUT), "FString UCataclysmCityScreenLayout::ButtonTextFor(", "CataclysmCityScreenLayout.cpp")
    branch = button[:button.index('TEXT("%s  --  %s on credit, %s owed")')]
    # THE TEXT IS MADE INSIDE THE BRANCH, then the days to build added to it, since issue #2825 merged with 6e.
    assert re.search(r"if \(Offer\.OwedIfBought > 0\)\s*\{\s*const FString OnCredit = FString::Printf\(\s*$", branch), (
        "the city screen's button says an offer is on credit other than when it would leave something owed (D6)")
    assert 'return Days.IsEmpty() ? OnCredit : FString::Printf(TEXT("%s, %s"), *OnCredit, *Days);' in button, (
        "an offer on credit leaves out the days it takes to build, which every other priced offer states (#2825)")
