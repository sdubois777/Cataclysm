"""The carried inventory has three tabs: the 48 slots hold gear, and crafting materials and gems have stores of their own.

WHY THIS EXISTS. Issue #2849, the project owner's request of 2026-11-22 -- "I think we need to add tabs to the
inventory. One for crafting materials and one for gems. Otherwise you wont be able to carry any loot" -- and their
ruling the same day, "One cell per kind, no limit (Recommended)". Continuous integration compiles no C++, so the
automation tests `Cataclysm.InventoryTabs.` never run on a pull request; this holds the ruled shape in the source.

WHAT IS CHECKED:

  1. adding a material or a gem answers whether it was added and never looks for a free gear slot, and the gear counts
     read the gear item alone;
  2. the forge's stone combine, gem combine and unsocket ask no free gear slot for what they make or give back;
  3. the character record is at version 16, `Migrate_15_to_16` is in its steps and blanks what it moves, and the save
     gathers and restores both tabs, the tabs before the slots;
  4. `Character_v16.json` is `Character_v15.json` with its stack moved, plus one gem stack, and the README lists it, and
     every chain test that adds version 15's writes adds version 16's;
  5. the screen asks for a tab before a cell, and answers a gear cell only while the gear tab is shown;
  6. the design document's Storage section says the 48 slots are for gear, quoting the owner.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
INVENTORY_CPP = SOURCE / "Items" / "CataclysmInventoryComponent.cpp"
FORGE_CRAFT = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
RECORDS_H = SOURCE / "Save" / "CataclysmSaveRecords.h"
RECORDS_CPP = SOURCE / "Save" / "CataclysmSaveRecords.cpp"
APPLY = SOURCE / "Save" / "CataclysmSaveApply.cpp"
GATHER = SOURCE / "Save" / "CataclysmSaveGather.cpp"
CONTROLLER = SOURCE / "Player" / "CataclysmPlayerController.cpp"
WIDGET = SOURCE / "Interface" / "CataclysmInventoryWidget.cpp"
TESTS = SOURCE / "Tests"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return flat(text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


def test_a_stack_never_looks_for_a_gear_slot_and_the_gear_counts_read_gear() -> None:
    inventory = code(INVENTORY_CPP)
    for adding in ("bool UCataclysmInventoryComponent::AddMaterial(", "bool UCataclysmInventoryComponent::AddGems("):
        added = body(inventory, adding)
        assert "FirstFreeSlot" not in added and "Slots[" not in added, (
            f"{adding} reaches into the gear slots: a stack has a store of its own since #2849")
        assert "return true;" in added, f"{adding} does not answer true when it added"
    for putting in ("void UCataclysmInventoryComponent::PutMaterial(", "void UCataclysmInventoryComponent::PutGems("):
        assert "Slots[" not in body(inventory, putting), f"{putting} writes a gear slot"
    assert "SlotOfMaterial" not in inventory and "SlotOfGems" not in inventory, (
        "SlotOfMaterial or SlotOfGems is back: a stack has no gear slot to name")
    for counting in ("int32 UCataclysmInventoryComponent::NumItems(", "int32 UCataclysmInventoryComponent::FirstFreeSlot("):
        assert ".Item)" in body(inventory, counting), f"{counting} does not read the gear item alone"


def test_the_forge_asks_no_gear_slot_for_a_stack() -> None:
    craft = code(FORGE_CRAFT)
    for signature in ("bool UCataclysmForgeCraft::Combine(", "bool UCataclysmForgeCraft::CombineGems(",
                      "FCataclysmForgeSocketing UCataclysmForgeCraft::UnsocketPlanFor("):
        found = body(craft, signature)
        assert "NumFreeSlots" not in found and "IsFull" not in found, (
            f"{signature} asks for a free gear slot, which a stone or a gem never needs since #2849")
    assert "const bool bRoom = From != nullptr;" in body(craft, "FCataclysmForgeSocketing UCataclysmForgeCraft::UnsocketPlanFor(")


def test_the_record_is_at_sixteen_and_its_step_moves_and_blanks() -> None:
    header = code(RECORDS_H)
    character = header[header.index("class CATACLYSM_API UCataclysmCharacterSave"):]
    # AT 16 OR LATER: issue #837 slice 1 took it to 17 (the keys' remembered defaults); a later step keeps this one's.
    version = re.search(r"SchemaVersionNow\s*=\s*(\d+)\s*;", character)
    assert version and int(version.group(1)) >= 16, "the character record is below version 16"
    for field in ("TArray<FCataclysmCarriedSlot> CarriedMaterials;", "TArray<FCataclysmCarriedSlot> CarriedGems;"):
        assert field in flat(character), f"the character record has no {field}"
    # THE CHARACTER RECORD'S OWN STEPS: the run record has a `Migrate_15_to_16` too (issue #2838's cleanse).
    records = body(code(RECORDS_CPP), "namespace CataclysmCharacterSaveMigration")
    assert '{ 15, TEXT("Migrate_15_to_16"), &Migrate_15_to_16 }' in flat(records), "Migrate_15_to_16 is not a step"
    step = body(records, "bool Migrate_15_to_16(")
    for written in ('Slot->SetStringField(TEXT("Material"), TEXT("None"));',
                    'Slot->SetNumberField(TEXT("Quantity"), 0.0);',
                    'Record->SetArrayField(TEXT("CarriedMaterials"), Materials);',
                    'Record->SetArrayField(TEXT("CarriedGems"), Gems);'):
        assert written in step, f"Migrate_15_to_16 does not write {written}"
    assert "RemoveAt" not in step and "RemoveField" not in step, "Migrate_15_to_16 removes a cell rather than blanking it"


def test_the_save_gathers_and_restores_both_tabs_the_tabs_first() -> None:
    gathered = body(code(GATHER), "bool FCataclysmSaveGather::CharacterFrom(")
    assert "CarriedStacksFrom(*Inventory, Record.CarriedMaterials, Record.CarriedGems);" in gathered
    applied = code(APPLY)
    stacks = applied.find("Inventory->RestoreStacks(Record.CarriedMaterials, Record.CarriedGems);")
    slots = applied.find("Inventory->RestoreSlots(Record.CarriedSlots);")
    assert -1 < stacks < slots, "the tabs are not restored before the slots, so a stack in a saved slot would be lost"


def test_the_version_16_file_is_version_15_with_its_stack_moved_and_a_gem() -> None:
    old = json.loads((FIXTURES / "Character_v15.json").read_text(encoding="utf-8"))
    new = json.loads((FIXTURES / "Character_v16.json").read_text(encoding="utf-8"))
    assert new["SchemaVersion"] == 16
    moved = old["CarriedSlots"][1]
    assert moved["Material"] != "None" and moved["Quantity"] > 0, "Character_v15.json's second slot is no stack"
    assert new["CarriedMaterials"] == [moved], "the stack is not what Character_v15.json carried"
    blank = new["CarriedSlots"][1]
    assert (blank["Material"], blank["Gem"], blank["Quantity"]) == ("None", "None", 0), "its cell is not blank"
    assert new["CarriedSlots"][0] == old["CarriedSlots"][0], "the gear's cell changed"
    assert len(new["CarriedGems"]) == 1 and new["CarriedGems"][0]["GemRarity"] != "Everyday", (
        "the gems tab holds no gem at a rarity other than the default, so a field read wrongly would not show")
    rest = {key: value for key, value in new.items()
            if key not in ("SchemaVersion", "CarriedSlots", "CarriedMaterials", "CarriedGems")}
    assert rest == {key: value for key, value in old.items() if key not in ("SchemaVersion", "CarriedSlots")}, (
        "Character_v16.json changed a field the step does not write")
    assert "| `Character_v16.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")


def test_every_chain_test_adds_version_16s_writes_after_version_15s() -> None:
    fifteen = 0
    for path in sorted(TESTS.glob("*.cpp")):
        text = code(path)
        # "(" AND NOT "(*Expected)": a test may pass the step on as a function (DM11, issue #2848's dismantling).
        old = text.count("AddTheCharactersVersion15Defaults(")
        new = text.count("AddTheCharactersVersion16Defaults(")
        assert new >= old, f"{path.name} adds version 15's writes {old} time(s) and version 16's {new}"
        fifteen += old
    assert fifteen > 0, "no chain test found: the search no longer finds what it counts"


def test_the_screen_asks_for_a_tab_before_a_cell() -> None:
    press = body(code(CONTROLLER), "void ACataclysmPlayerController::PressOnTheInventoryScreen(")
    tab = press.find("InventoryScreen->TabUnderCursor(Point, Tab)")
    cell = press.find("InventoryScreen->CarriedSlotUnderCursor(Point)")
    assert -1 < tab < cell, "a press does not ask for a tab before a gear cell"
    under = body(code(WIDGET), "int32 UCataclysmInventoryWidget::CarriedSlotUnderCursor(")
    assert "Shown == ECataclysmInventoryTab::Gear" in under, (
        "a gear cell is answered while another tab is shown, from geometry it was last painted with")


def test_the_design_says_the_48_slots_are_for_gear() -> None:
    gdd = flat(GDD.read_text(encoding="utf-8"))  # Read with its line breaks as spaces: the paragraph is wrapped.
    assert "Otherwise you wont be able to carry any loot" in gdd, "the Storage section does not quote the owner (#2849)"
    assert "One cell per kind, no limit (Recommended)" in gdd, "the Storage section does not quote the ruling"


def test_the_gems_tab_pages_rather_than_running_off_the_screen() -> None:
    """The coordinating session's addition of 2026-11-22: 224 gem stacks are reachable, and a tab whose bottom rows ran
    off the screen would hide gems. One page of cells is built, as many rows as the materials tab uses, and a refresh
    shows the stacks from the page's start."""
    widget = code(WIDGET)
    built = body(widget, "void UCataclysmInventoryWidget::BuildTree(")
    assert "const int32 PerPage = FScreen::GemStacksPerPage(MaterialOrder.Num());" in built, (
        "the gems tab's cells are not one page, as many rows as the materials tab uses")
    refreshed = body(widget, "void UCataclysmInventoryWidget::RefreshStacks(")
    assert "BuildCell(" not in refreshed, "the gems tab grows its cells again rather than paging"
    assert "GemStacks[Start + Index]" in refreshed and "FScreen::GemPageStart(Page, PerPage)" in refreshed, (
        "a refresh does not show the stacks from the page's start")
    press = body(code(CONTROLLER), "void ACataclysmPlayerController::PressOnTheInventoryScreen(")
    assert "InventoryScreen->GemPagerUnderCursor(Point, PageStep)" in press, "no press turns the gems tab's page"
