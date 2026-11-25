"""A held item pressed onto a gear slot is worn there, and a refused press is shown on the screen (issue #837).

WHY THIS EXISTS. Slice 3 of the skill selection plan, approved by the project owner on 2026-11-24 ("Approve all three
(Recommended)", Q5 and Q6): pressing a held item onto a gear slot wears it in that slot, what was there going into the
bag, and the refusal of taking off the only weapon worn is shown on the inventory screen, in a sentence naming that
press.

WHAT IS CHECKED, by reading the C++ with comments removed, because a test world has no screen to press on:

  1. the left press on a gear slot wears the held item there when something is held, asks about the Consumption
     Threshold for that slot, and picks the worn piece up only when nothing is held;
  2. the threshold question and its "Wear it" carry the slot pressed all the way to the wear;
  3. every press's answer is shown on the screen in the words `UCataclysmInventoryScreen::PressLineFor` gives, and the
     widget draws it for its seconds;
  4. the last weapon's sentence names the press, and the design document's controls table states it.

WHAT IS NOT. What the press does to the items, and when the line is shown: the `Cataclysm.Wearing.` automation tests in
`CataclysmWearHeldIntoTests.cpp` check those.
"""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))

from test_the_play_loop_is_logged_at_each_step import function_body, read_code, squeeze  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
CONTROLLER = SOURCE / "Player" / "CataclysmPlayerController.cpp"
PAWN = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
WEARING = SOURCE / "Items" / "CataclysmWearing.cpp"
WIDGET = SOURCE / "Interface" / "CataclysmInventoryWidget.cpp"
DESIGN = ROOT / "docs" / "Inventory_Screen_Design.md"


def test_a_press_on_a_gear_slot_while_holding_wears_the_held_item_there() -> None:
    press = function_body(read_code(CONTROLLER), "void ACataclysmPlayerController::PressOnTheInventoryScreen()")
    at = press.find("InventoryScreen->GearSlotUnderCursor(Point);")
    assert at != -1, "the left press no longer asks which gear slot it landed on"
    holding = press.find("if (Bag && Bag->IsHolding())", at)
    worn = press.find("UCataclysmWearing::WearHeldInto( Bag, Wearer->GetEquipment(), Worn, Went);", at)
    picked = press.find("UCataclysmWearing::PickUpWorn(", at)
    assert -1 not in (holding, worn, picked), (
        "the press on a gear slot no longer wears a held item there (WearHeldInto) and otherwise picks the worn piece "
        "up (PickUpWorn)")
    assert holding < worn < picked, (
        "the press on a gear slot must ask whether something is held first, and wear it there before it reaches the "
        "pick-up, which answers AlreadyHolding")
    asked = press.find("AskBeforeWearing(Wearer, HeldCell, Worn);", worn)
    assert asked != -1 and asked < picked, (
        "a held press that would cross the Consumption Threshold no longer asks first, about the slot pressed")


def test_the_question_is_about_the_slot_pressed_and_its_yes_wears_it_there() -> None:
    code = read_code(CONTROLLER)
    ask = function_body(code, "void ACataclysmPlayerController::AskBeforeWearing(")
    assert "UCataclysmWearing::CrossingFor(Bag, Wearer->GetEquipment(), CarriedSlot, 0, Into)" in ask, (
        "the question's total is no longer worked out for the slot pressed")
    assert "Now->WearPastTheThreshold(CarriedSlot, &AskedAbout, Went, Into)" in ask, (
        "the question's \"Wear it\" no longer wears the item in the slot pressed")
    right = function_body(code, "void ACataclysmPlayerController::RightPressOnTheInventoryScreen()")
    assert "AskBeforeWearing(Wearer, Carried, ECataclysmGearSlot::Count);" in right, (
        "the right press's question no longer leaves the slot to Equip")

    # SQUEEZED BEFORE IT IS SEARCHED: the two overloads' signatures run over several lines, and differ only in the last
    # parameter. Comments are gone, so the wear's `/*bConfirmedPastThreshold=*/` is too.
    pawn = squeeze(read_code(PAWN))
    named = function_body(pawn, "ECataclysmWearResult ACataclysmPlayerCharacter::WearPastTheThreshold(int32 CarriedSlot, "
                                "const FCataclysmItem* AskedAbout, ECataclysmGearSlot& OutSlot, ECataclysmGearSlot Into)")
    for call in ("UCataclysmWearing::CrossingFor(Inventory, Equipment, CarriedSlot, 0, Into)",
                 "UCataclysmWearing::WearAfterConfirming(Inventory, Equipment, CarriedSlot, *AskedAbout, Went, 0, Into)",
                 "UCataclysmWearing::WearFromCarried(Inventory, Equipment, CarriedSlot, Went, true, 0, Into)"):
        assert call in named, f"the confirmed wear no longer passes the slot pressed on: {call}"
    plain = function_body(pawn, "ECataclysmWearResult ACataclysmPlayerCharacter::WearPastTheThreshold(int32 CarriedSlot, "
                                "const FCataclysmItem* AskedAbout, ECataclysmGearSlot& OutSlot) {")
    assert "return WearPastTheThreshold(CarriedSlot, AskedAbout, OutSlot, ECataclysmGearSlot::Count);" in plain, (
        "the confirmed wear without a slot no longer leaves the slot to Equip")


def test_a_refused_press_is_shown_on_the_screen() -> None:
    report = function_body(read_code(CONTROLLER), "void ACataclysmPlayerController::ReportInventoryPress(")
    assert "const FString Line = UCataclysmInventoryScreen::PressLineFor(Result);" in report, (
        "a press's answer is no longer worded by UCataclysmInventoryScreen::PressLineFor")
    assert "InventoryScreen->ShowPressLine(Line," in report, "a press's answer is no longer shown on the screen"

    widget = read_code(WIDGET)
    tick = function_body(widget, "void UCataclysmInventoryWidget::NativeTick(")
    assert "FScreen::PressLineIsShown(PressLineText, PressLinePostedAt, World->GetTimeSeconds())" in tick, (
        "the inventory widget no longer shows the press's answer for its seconds")
    build = function_body(widget, "void UCataclysmInventoryWidget::BuildTree()")
    assert "Root->AddChildToOverlay(PressLineBacking);" in build, (
        "the press's answer is no longer drawn on the screen")


def test_the_last_weapon_sentence_names_the_press_and_the_design_states_it() -> None:
    explain = function_body(read_code(WEARING), "FString UCataclysmWearing::Explain(ECataclysmWearResult Result)")
    assert '"Press another weapon onto this hand to swap them."' in explain, (
        "the last weapon's refusal no longer names the press that swaps it (the owner's Q6 of 2026-11-24)")
    # THE CONTROLS TABLE'S LEFT ROW, under "How it is operated": another table in the document has a "Left" column.
    section = DESIGN.read_text(encoding="utf-8").split("## How it is operated", 1)[-1]
    row = next((line for line in section.splitlines() if line.startswith("| Left |")), "")
    assert "While holding an item, wear the held item in this slot; what was here goes into the bag" in row, (
        f"{DESIGN.name}'s controls table no longer says what a left press on a worn item does while holding one")
