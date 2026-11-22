"""Gems are carried in stacks, one per gem and rarity, and every place that reads a carried slot knows a gem stack.

WHY THIS EXISTS. Issue #46, slice G2. A carried slot held a gear item or a stack of one crafting material, and every
reader of it was written for those two: the cell's name, count, colour and border, the tooltip, and dropping a slot
on the floor. A gem stack is a third kind, so each of them needed a gem branch. Two of them fail badly without one:

  - `UCataclysmInventoryComponent::SlotIsEmpty` would call a gem stack empty, so the next pick-up would overwrite it;
  - `UCataclysmWearing::DropCarried` would hand `UCataclysmDropSpawner::PutOnTheFloor` an empty item and no material,
    which it does not refuse, and then empty the slot: the gems would be destroyed. Gems reach the floor with gem
    drops (slice G5), so until then a gem stack is refused.

Continuous integration compiles no C++, so these are checked from the source; `Cataclysm.GemStacks.`
(`game/Source/Cataclysm/Tests/CataclysmGemStacksTests.cpp`) tests what they do.

WHAT IS CHECKED:

  1. `FCataclysmCarriedSlot` declares `Gem` and `GemRarity`, each saved (`SaveGame`);
  2. `SlotIsEmpty` for a carried slot reads its gem;
  3. a stack is found by gem AND rarity, so two rarities of one gem are two stacks (ruled 2026-11-21);
  4. `DropCarried` refuses a gem stack before it asks the floor, and `Explain` says why;
  5. the cell's name, count, colour and border, and the tooltip, each read a slot's gem;
  6. `UCataclysmGems::TableAssetPath` names the asset `tools/generate_datatable_assets.py` makes from `Gems.csv`.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
INVENTORY_H = SOURCE / "Items" / "CataclysmInventoryComponent.h"
INVENTORY_CPP = SOURCE / "Items" / "CataclysmInventoryComponent.cpp"
WEARING_CPP = SOURCE / "Items" / "CataclysmWearing.cpp"
SCREEN_CPP = SOURCE / "Interface" / "CataclysmInventoryScreen.cpp"
TOOLTIP_CPP = SOURCE / "Interface" / "CataclysmItemTooltip.cpp"
GEMS_CPP = SOURCE / "Items" / "CataclysmGems.cpp"
ASSETS_SCRIPT = REPO_ROOT / "tools" / "generate_datatable_assets.py"


def code(path: pathlib.Path) -> str:
    """The file with each `//` comment taken off its line, so a mention in a comment cannot satisfy a check."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where}: the body of {signature!r} never closes")


def test_the_carried_slot_saves_its_gem_and_rarity() -> None:
    slot = body(code(INVENTORY_H), "struct CATACLYSM_API FCataclysmCarriedSlot", INVENTORY_H.name)
    squeezed = re.sub(r"\s+", " ", slot)
    for declaration in ("FName Gem;", "ECataclysmRarity GemRarity = ECataclysmRarity::Everyday;"):
        found = re.search(r"UPROPERTY\(([^)]*)\) " + re.escape(declaration), squeezed)
        assert found, f"FCataclysmCarriedSlot does not declare {declaration!r} as a UPROPERTY"
        assert "SaveGame" in found.group(1), (
            f"{declaration!r} is not SaveGame, so a carried gem would be dropped from the character record silently")


def test_a_gem_stack_is_not_an_empty_slot() -> None:
    found = body(code(INVENTORY_CPP), "bool UCataclysmInventoryComponent::SlotIsEmpty(const FCataclysmCarriedSlot",
                 INVENTORY_CPP.name)
    assert "Slot.Gem.IsNone()" in found, (
        "SlotIsEmpty does not read the slot's gem, so a gem stack reads as empty and the next pick-up overwrites it")


def test_a_stack_is_found_by_gem_and_rarity() -> None:
    found = body(code(INVENTORY_CPP), "int32 UCataclysmInventoryComponent::SlotOfGems(", INVENTORY_CPP.name)
    assert "Slots[Slot].Gem == Gem" in found and "Slots[Slot].GemRarity == Rarity" in found, (
        "SlotOfGems must match both the gem and its rarity: one stack per gem and rarity, ruled 2026-11-21")
    adding = body(code(INVENTORY_CPP), "int32 UCataclysmInventoryComponent::AddGems(", INVENTORY_CPP.name)
    assert "SlotOfGems(Gem, Rarity)" in adding, "AddGems does not look for the stack of that gem at that rarity"


def test_a_dropped_gem_stack_goes_to_the_floor_as_gems() -> None:
    # Slice G5 lifted G2's refusal: a gem stack lies on the floor whole, so DropCarried must hand the floor the gem,
    # its rarity and the count, or the drop would hold nothing and the gems would be destroyed.
    dropping = body(code(WEARING_CPP), "ECataclysmWearResult UCataclysmWearing::DropCarried(", WEARING_CPP.name)
    floor = dropping[dropping.find("PutOnTheFloor("):]
    assert dropping.find("PutOnTheFloor(") != -1, "DropCarried no longer asks the floor"
    call = floor[:floor.find(")")]
    # "Leaving.Gem," WITH ITS COMMA: without it the check is satisfied by "Leaving.GemRarity" alone (proof GX8).
    for argument in ("Leaving.Gem,", "Leaving.GemRarity,", "bGems ? Leaving.Quantity : 0"):
        assert argument in call, f"DropCarried does not give the floor {argument}, so a dropped gem stack is lost"


@pytest.mark.parametrize("path, signature", [
    (SCREEN_CPP, "FString UCataclysmInventoryScreen::LabelFor("),
    (SCREEN_CPP, "FString UCataclysmInventoryScreen::QuantityTextFor("),
    (SCREEN_CPP, "FLinearColor UCataclysmInventoryScreen::ColourFor("),
    (SCREEN_CPP, "int32 UCataclysmInventoryScreen::BorderThicknessFor("),
    (TOOLTIP_CPP, "TArray<FString> UCataclysmItemTooltip::LinesFor(\n\tconst FCataclysmCarriedSlot& Slot"),
])
def test_each_reader_of_a_carried_slot_knows_a_gem_stack(path: pathlib.Path, signature: str) -> None:
    text = code(path)
    if signature not in text:
        signature = signature.replace("\n\t", "")
    assert "Slot.Gem" in body(text, signature, path.name), (
        f"{signature.strip()} does not read the slot's gem, so a gem stack is drawn or described as a material")


def test_the_gem_table_path_is_the_asset_made_from_gems_csv() -> None:
    assert '("DT_Gems", "Gems.csv", "CataclysmGemRow")' in ASSETS_SCRIPT.read_text(encoding="utf-8"), (
        "tools/generate_datatable_assets.py no longer makes DT_Gems from Gems.csv")
    assert 'TableAssetPath = TEXT("/Game/Data/DT_Gems.DT_Gems");' in code(GEMS_CPP), (
        "UCataclysmGems::TableAssetPath does not name /Game/Data/DT_Gems, so no gem name or effect can be read")
