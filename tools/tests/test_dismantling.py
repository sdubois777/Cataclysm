"""Dismantling a carried piece at the forge (issue #2848, slice S2), ruled by the coordinating session on 2026-11-22:
the plan of selling and dismantling, sections 6 to 8, and D1 to D5.

`Cataclysm.Dismantle.` tests what a dismantle does; this checks the shape:

  1. the yields are the ruled tables: fragments 0.06 to Masterful then 0.1, 0.2, 0.4, 0.8 (N4); Dust 0.02 a socket
     (N5); base stones 0.025 to Masterful then 0.05, 0.1, 0.2, 0.4 (section 8); Salvage Protocol 2.5% a point (N6) and
     Disassembly Expert 20% from Superb up (N7, O4); the salt "DISM" (D5);
  2. the two nodes raise the three drawn yields and never the refund;
  3. the craft goes through `DoItemCraft` with no gold and no days (D2), draws from the item as it stood and the run's
     day before anything moves (D5), reads the player's own magic find (D3), and checks for room before it removes the
     piece, in one block naming #2849 (D4);
  4. a drop and the weapon made at creation record where they arrived, and the seed text adds it only when known;
  5. the two nodes are built, with their effects, and the screen offers no dismantle for a worn piece or the weapon made
     at creation.
"""
from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
DROP_CPP = SOURCE / "Items" / "CataclysmDropRoll.cpp"
CHARACTER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def code(path: pathlib.Path) -> str:
    """The file with its comments removed, so a name written only in a comment does not count."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*(?!\s*\w+=\s*\*/).*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    depth, at = 0, text.index("{", start)
    for index in range(at, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[at:index + 1]
    raise AssertionError(f"{signature} is never closed")


def table(signature: str) -> dict[str, float]:
    found = re.findall(r"case ECataclysmRarity::(\w+):\s*return ([0-9.]+)f;", body(code(FORGE_CPP), signature))
    default = re.search(r"default:\s*return ([0-9.]+)f;", body(code(FORGE_CPP), signature))
    assert default, f"{signature} has no default"
    out = {rung: float(value) for rung, value in found}
    out["default"] = float(default.group(1))
    return out


def constant(name: str) -> str:
    found = re.search(rf"static constexpr \w+ {name} = ([^;]+);", code(FORGE_H))
    assert found, f"{FORGE_H.name} declares no {name}"
    return found.group(1).strip()


def test_the_yields_are_the_ruled_tables() -> None:
    assert table("float UCataclysmForge::DismantleFragmentsFor(") == {
        "Legendary": 0.1, "Mythical": 0.2, "Ascendant": 0.4, "Cataclysmic": 0.8, "default": 0.06}, "N4"
    assert table("float UCataclysmForge::DismantleStonesFor(") == {
        "Legendary": 0.05, "Mythical": 0.1, "Ascendant": 0.2, "Cataclysmic": 0.4, "default": 0.025}, "section 8 (a)"
    assert constant("DismantleDustPerSocket") == "0.02f", "N5"
    assert constant("SalvageProtocolIncreasePerPoint") == "0.025f", "N6"
    assert constant("DisassemblyExpertIncrease") == "0.20f", "N7"
    assert constant("DisassemblyExpertLowestRarity") == "ECataclysmRarity::Superb", "O4"
    salt = int(constant("DismantleSalt"), 16)
    assert salt.to_bytes(4, "big") == b"DISM", "D5"


def test_the_nodes_raise_the_drawn_yields_and_never_the_refund() -> None:
    plan = flat(body(code(FORGE_CPP), "FCataclysmForgeDismantle UCataclysmForge::DismantleOf("))
    for drawn in ("Out.FragmentsExpected = DismantleFragmentsFor(Out.Rarity) * Raised;",
                  "Out.DustExpected = DismantleDustPerSocket * static_cast<float>(Item.Sockets.Num()) * Raised;",
                  "Out.StonesExpected = DismantleStonesFor(Out.Rarity) * Raised;"):
        assert drawn in plan, f"a drawn yield is not raised by the nodes: {drawn}"
    refund = plan[plan.index("Out.RefundFromLevel ="):plan.index("Out.bPossible = true;")]
    assert "Raised" not in refund and "Increase" not in refund, "the refund is raised by the nodes, which was ruled against"
    increase = flat(body(code(FORGE_CPP), "float UCataclysmForge::DismantleIncreaseFor("))
    assert "FMath::Max(0.0f, SalvageIncrease) + (bSuperbOrAbove ? FMath::Max(0.0f, DisassemblyIncrease) : 0.0f)" in increase, (
        "the two nodes are not added, Disassembly Expert from Superb up")


def test_the_craft_is_instant_draws_from_the_item_and_day_and_checks_for_room_first() -> None:
    craft = flat(body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::Dismantle("))
    assert "Step.Gold = 0;" in craft and "Step.Days = 0;" in craft, "a dismantle costs gold or days (D2)"
    assert "Step.Materials" not in craft, "a dismantle takes a material (D2)"
    assert "DoItemCraft(" in craft, "a dismantle does not go through F5's shared sequence (D1)"
    assert ("UCataclysmForge::AffixCraftSeed(Before, UCataclysmForge::DismantleSalt, 0, Run.WalkSeedMask, Run.Day())"
            in craft), "the draw is not seeded from the item as it stood and the run's day (D5)"
    assert "UCataclysmDropSpawner::LootStatsOf(Character, MagicFind, LootQuantity);" in craft, (
        "the stone's material tier does not read the player's own magic find (D3)")
    raw = body(CRAFT_CPP.read_text(encoding="utf-8"), "ECataclysmForgeResult UCataclysmForgeCraft::Dismantle(")
    start = raw.find("REMOVE THIS BLOCK WHEN ISSUE #2849")
    end = raw.find("END OF THE #2849 BLOCK")
    removed = raw.find("Inventory->RemoveItemAt(Target.CarriedSlot)")
    assert -1 < start < end < removed, "the room check is not one #2849 block before the piece is removed (D4)"
    assert 'TEXT("Make room in the bag first.")' in raw[start:end], "the room check does not say what to do"


def test_a_piece_records_where_it_arrived_and_the_seed_text_adds_it_only_when_known() -> None:
    drop = flat(code(DROP_CPP))
    assert ("OutItem.GearLevel = GearLevelGateFor(GearRarityTable, Rarity); OutItem.GearLevelAtDrop = OutItem.GearLevel; "
            "OutItem.TierAtDrop = FMath::Max(1, DifficultyTier);") in drop, "a drop does not record where it arrived"
    character = flat(code(CHARACTER_CPP))
    for who in ("Chosen", "Starting"):
        assert (f"{who}.GearLevelAtDrop = 0; {who}.TierAtDrop = ACataclysmGameMode::DifficultyTierIn(GetWorld());"
                in character), f"the weapon made at creation ({who}) does not record where it arrived"
    seed = flat(body(code(FORGE_CPP), "FString UCataclysmForge::ItemSeedText("))
    assert ('return Item.GearLevelAtDrop == -1 && Item.TierAtDrop == 0 ? Text : Text + '
            'FString::Printf(TEXT(";AtDrop=%d:%d"), Item.GearLevelAtDrop, Item.TierAtDrop);') in seed, (
        "the seed text adds where a piece arrived even at the defaults, moving every pinned forge draw")


def test_the_two_nodes_are_built_and_the_screen_offers_no_dismantle_it_cannot_take() -> None:
    assert gen.EMPIRE_TREE_BUILT.get("Salvage Protocol") == 2848 and gen.EMPIRE_TREE_BUILT.get("Disassembly Expert") == 2848
    assert "Salvage Protocol" not in gen.EMPIRE_TREE_REFUSED and "Disassembly Expert" not in gen.EMPIRE_TREE_REFUSED
    assert gen.EMPIRE_TREE_EFFECTS["Salvage Protocol"] == [("DismantleMaterialsIncreased", 0.025)]
    assert gen.EMPIRE_TREE_EFFECTS["Disassembly Expert"] == [("DismantleMaterialsIncreasedSuperbAndAbove", 0.20)]
    label = flat(body(code(LAYOUT_CPP), "FString UCataclysmForgeLayout::DismantleLabelFor("))
    assert "if (bWorn || Item.Base.IsNone() || Item.bMadeAtCreation" in label, (
        "the screen offers a dismantle for a worn piece or the weapon made at creation")
    plan = flat(body(code(FORGE_CPP), "FCataclysmForgeDismantle UCataclysmForge::DismantleOf("))
    assert "if (Item.bMadeAtCreation)" in plan, "the rule does not refuse the weapon made at creation"
