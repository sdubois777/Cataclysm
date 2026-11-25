"""The merchant sells through the bag's own functions, pays before it takes, and its button and screen are wired in.

WHY THIS EXISTS. Issue #48, slice S1. The automation tests `Cataclysm.Merchant.` drive the price, the sale and the
screen's presses. What no test that runs can see is:
  - the map's button and the screen's Close reaching the player controller, since a headless test has no controller;
  - the merchant reading the bag only through the inventory component's functions. The coordinating session asked for
    that on 2026-11-22, so that the inventory tabs being planned (#2849) have one place to change.

WHAT IS CHECKED:

  1. `UCataclysmMerchant::Sell` banks the gold (`AddGoldFor`) before it takes the piece (`RemoveItemAt`), and takes the
     gold back if the piece cannot be taken;
  2. neither the merchant nor its screen reads the bag's slot array: no `GetSlots`, no `.Slots`, only `ItemAt`,
     `RemoveItemAt` and `SlotCount`;
  3. the price is the tier's pile with no floor's bonus: `GoldPileMean(..., DifficultyTier, 1)`;
  4. the empire map's "Merchant" button and the screen's Close call `ToggleMerchant`, and the controller counts the
     merchant's screen among the full screens.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
MERCHANT = SOURCE / "Items" / "CataclysmMerchant.cpp"
SCREEN = SOURCE / "Interface" / "CataclysmMerchantWidget.cpp"


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


def test_a_sale_pays_before_it_takes_the_piece() -> None:
    sell = body(code(MERCHANT), "bool UCataclysmMerchant::Sell(")
    paid = sell.find("UCataclysmGameInstance::AddGoldFor(WorldContext, Price, bIntoTheCharacter)")
    taken = sell.find("Bag->RemoveItemAt(CarriedSlot)")
    assert paid != -1 and taken != -1 and paid < taken, "the gold must be banked before the piece is taken"
    after = sell[taken:]
    assert "UCataclysmGameInstance::SpendGoldFor(WorldContext, Price, bFromTheCharacter);" in after, (
        "if the piece cannot be taken, the gold must be taken back")


def test_the_bag_is_read_only_through_the_inventory_components_functions() -> None:
    for path in (MERCHANT, SCREEN):
        text = code(path)
        assert "GetSlots" not in text, f"{path.name} reads the slot array"
        assert not re.search(r"(->|\.)Slots\b", text), f"{path.name} reads the slot array"
        assert "ItemAt(" in text, f"{path.name} reads no cell through ItemAt"
    pieces = body(code(MERCHANT), "TArray<int32> UCataclysmMerchant::CarriedPiecesOf(")
    assert "UCataclysmInventoryComponent::SlotCount" in pieces and "Bag->ItemAt(Cell)" in pieces


def test_the_price_is_the_tiers_pile_with_no_floor_bonus() -> None:
    pile = body(code(MERCHANT), "double UCataclysmMerchant::PileGoldAt(")
    assert "GoldPileMean(UCataclysmDropRoll::LoadGoldTable(), DifficultyTier, 1)" in pile


def test_the_button_and_the_close_reach_the_controller() -> None:
    handler = body(code(SOURCE / "Interface" / "CataclysmEmpireMapWidget.cpp"),
                   "void UCataclysmEmpireMapWidget::HandleCityClicked(")
    branch = handler[handler.find("if (Value == MerchantButtonName())"):]
    assert branch.startswith("if (Value == MerchantButtonName())"), "the click handler has no merchant branch"
    assert "Controller->ToggleMerchant();" in branch[:400]
    press = body(code(SCREEN), "bool UCataclysmMerchantWidget::Press(")
    close = press[press.find('Command == TEXT("Close")'):]
    assert "Controller->ToggleMerchant();" in close[:400]
    full = body(code(SOURCE / "Player" / "CataclysmPlayerController.cpp"),
                "bool ACataclysmPlayerController::IsAFullScreenOpen(")
    assert "MerchantScreen" in full
