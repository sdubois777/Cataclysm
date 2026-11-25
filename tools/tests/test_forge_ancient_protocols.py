"""Ancient Techniques and Residue Protocols, #2741's crafting group slice C6, checked in the source as text.

Ruled by the coordinating session on 2026-11-25 under the owner's delegation: Ancient Techniques takes a day a point off
a forge craft on an item with four affixes or more BEFORE the craft (R2), with Master Craftsman's days off; Residue
Protocols ignores 5% a point of the Worn Residue the Consumption Threshold counts (R1), through one function. The Unreal
tests in `CataclysmForgeAncientProtocolsTests.cpp` check the figures; these check what a test of figures cannot see:

  1. the two nodes are built in slice 15 with their rows and their reworded sentences;
  2. every comparison of Worn Residue with the threshold reads the one function, and nothing else reads a raw sum;
  3. Ancient Techniques reads the item before the craft, at four affixes, into the days off.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
import generate_datatables as gen  # noqa: E402

SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
WEARING_CPP = SOURCE / "Items" / "CataclysmWearing.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
SHEET_CPP = SOURCE / "Interface" / "CataclysmCharacterSheetWidget.cpp"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"


def code(path: pathlib.Path) -> str:
    """The file with its comments blanked, so a comment cannot satisfy a check."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


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
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} is not closed")


def test_the_two_nodes_are_built_in_slice_fifteen_with_their_sentences() -> None:
    assert gen.EMPIRE_TREE_BUILT.get("Ancient Techniques") == 15
    assert gen.EMPIRE_TREE_BUILT.get("Residue Protocols") == 15
    assert gen.EMPIRE_TREE_EFFECTS["Ancient Techniques"] == [("CraftDaysOffOnFullItems", 1.0)]
    assert gen.EMPIRE_TREE_EFFECTS["Residue Protocols"] == [("WornResidueIgnored", 0.05)]
    text = NODES_CSV.read_text(encoding="utf-8")
    assert "-1 Day crafting time per point for crafts with 4 or more affixes." in text
    assert "Ignore 5% of Worn Residue per point when it is measured against the Consumption Threshold." in text


def test_every_comparison_with_the_threshold_reads_the_one_function() -> None:
    crossing = flat(body(code(WEARING_CPP), "FCataclysmThresholdCrossing UCataclysmWearing::CrossingFor("))
    assert "Crossing.Now = CountedWornResidue(Equipment, Equipment->WornResidue());" in crossing
    assert "Crossing.After = CountedWornResidue(Equipment, Equipment->WornResidueIfEquipped(*Carried, Into));" in crossing
    sequence = flat(body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft("))
    assert "const float Now = UCataclysmWearing::CountedWornResidue(WorldContext, Equipment->WornResidue());" in sequence
    assert ("const float After = UCataclysmWearing::CountedWornResidue(WorldContext, Equipment->WornResidue() + Added);"
            in sequence)
    sheet = flat(code(SHEET_CPP))
    assert ("UCataclysmCharacterSheetLayout::WornResidueLine( Equipment ? UCataclysmWearing::CountedWornResidue(Equipment, "
            "Equipment->WornResidue()) : 0.0f, ShownDifficultyTier(), UCataclysmWearing::IgnoredWornResidueShare(Equipment))"
            in sheet), "the sheet's line does not show what the threshold counts"
    counted = flat(body(code(WEARING_CPP), "float UCataclysmWearing::CountedWornResidueOf("))
    assert "return WornResidue * (1.0f - FMath::Clamp(IgnoredShare, 0.0f, 1.0f));" in counted
    # NO OTHER RAW SUM MEETS THE THRESHOLD: every `WornResidue()` outside the equipment component is one of the four above
    # (the equip crossing's, the forge's two and the sheet's), each inside `CountedWornResidue`.
    raw = []
    for path in SOURCE.rglob("*.cpp"):
        if "Tests" in path.parts or path.name == "CataclysmEquipmentComponent.cpp":
            continue
        raw += [path.name for _ in re.finditer(r"->WornResidue\(\)", code(path))]
    assert sorted(raw) == sorted(["CataclysmWearing.cpp", "CataclysmForgeCraft.cpp", "CataclysmForgeCraft.cpp",
                                  "CataclysmCharacterSheetWidget.cpp"]), raw


def test_ancient_techniques_reads_the_item_before_the_craft_at_four_affixes() -> None:
    costs = flat(body(code(CRAFT_CPP), "FCataclysmForgeCosts ForgeCostsOf("))
    assert ("if (Item.Affixes.Num() >= UCataclysmForge::AncientTechniquesLowestAffixes) { Out.DaysOff += "
            "Tree.CraftDaysOffOnFullItems; }") in costs
    assert "static constexpr int32 AncientTechniquesLowestAffixes = 4;" in flat(code(FORGE_H))
