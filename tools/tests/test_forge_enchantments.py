"""The forge's Imprint Enchantment and Change Enchantment Negative (issue #47, slice F3), ruled by the coordinating
session on 2026-11-22 (the forge survey's Q14 to Q18).

The Crafting sheet's rows, `game/Data/ForgeOperations.csv`: Imprint Enchantment, an Aetherial Shard, 5 CR, 2 base days,
Base Gold 500; Change Enchantment Negative, a Regret Essence, 100 CR, 7 base days, Base Gold 1,750. An imprint draws a
pair as a drop draws them (Q14), the pairs on the piece counting as taken, and gives up the affix the player chooses
(Q15), only on a piece with every slot filled (Q16) at the gear level the rarity it becomes needs (the design's rarity
table, "Legendary ... Requires gear level 4+"). A change draws a different negative (Q18) under the weight rule (Q17)
and never touches a set's pair. `Cataclysm.ForgeEnchantments.` tests what they do; this checks the shape.

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the sheet's two rows hold the figures the forge's constants state, and their materials are crafting materials --
     without it a changed sheet and an unchanged constant charge two prices;
  2. each rule reads its row's Base Gold and refuses for its material before its gold, gold last, as every craft;
  3. an imprint is refused on a piece with a slot empty, and its rarity's gear level is read from the gear rarity table,
     a missing table refusing rather than reading as no gate;
  4. a drop draws what it drew before: the pairs already on a piece are read into the taken sets without touching the
     stream, and only the imprint passes any;
  5. a changed negative is drawn from the bands at or below its positive's weight, every negative on the piece taken,
     never for a set's pair, with a roll from a stream of its own;
  6. the craft keeps slice F1's order (`CraftAffix`): gold, material, then the draw, from the item as it stood and the
     day before the craft's days pass, so a reload draws the same; and a draw that fails gives back what was taken;
  7. the screen offers an imprint only on a piece with every slot filled, and no change for a set's pair.

NOT CHECKED HERE: that no enchantment Weight moved. Nothing in this slice writes `game/Data/Enchantments*.csv`, and the
pull request's diff is what shows it.
"""
from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
DROP_CPP = SOURCE / "Items" / "CataclysmDropRoll.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"


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
            return flat(text[at:index + 1])
    raise AssertionError(f"{signature} is never closed")


def constant(name: str) -> float:
    found = re.search(rf"static constexpr (?:int32|float) {name} = (0x[0-9A-F]+|\d+(?:\.\d+)?)f?;", code(FORGE_H))
    assert found, f"{FORGE_H.name} declares no number {name}"
    return float(int(found.group(1), 16)) if found.group(1).startswith("0x") else float(found.group(1))


def rows(name: str) -> dict[str, dict]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def calls(text: str, name: str) -> list[str]:
    """Every call of `name(` in `text`, to its closing parenthesis, flattened."""
    found = []
    for match in re.finditer(re.escape(name) + r"\(", text):
        depth, index = 0, match.end() - 1
        while index < len(text):
            depth += {"(": 1, ")": -1}.get(text[index], 0)
            if depth == 0:
                break
            index += 1
        found.append(flat(text[match.start():index + 1]))
    return found


def test_the_sheets_two_rows_hold_the_forges_figures() -> None:
    operations = rows("ForgeOperations.csv")
    materials = rows("CraftingMaterials.csv")
    names = dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+Row) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))
    for row_field, material_field, residue, days in (
            ("ImprintOperationRow", "AetherialShardRow", "ImprintResidue", "ImprintBaseDays"),
            ("ChangeNegativeOperationRow", "RegretEssenceRow", "ChangeNegativeResidue", "ChangeNegativeBaseDays")):
        row = operations.get(names.get(row_field, ""))
        assert row is not None, f"ForgeOperations.csv has no row {names.get(row_field)}, which {row_field} names"
        material = materials.get(names.get(material_field, ""))
        assert material is not None, f"CraftingMaterials.csv has no row {names.get(material_field)}"
        assert row["Materials"] == material["MaterialName"], (row["Materials"], material["MaterialName"])
        assert row["CRImpact"] == f"{constant(residue):g} CR", (row["CRImpact"], constant(residue))
        assert float(row["BaseDays"]) == constant(days), (row["BaseDays"], constant(days))
        assert float(row["BaseGold"]) == constant("GoldPerCraftDay") * constant(days), row["BaseGold"]


def test_each_rule_reads_its_row_and_refuses_for_its_material_before_its_gold() -> None:
    text = code(FORGE_CPP)
    for signature, row, material, gold in (
            ("FCataclysmForgeEnchantmentWork UCataclysmForge::ImprintOf(", "ImprintOperationRow",
             "Imprinting an enchantment takes an Aetherial Shard", 'GoldRefusal(TEXT("Imprinting an enchantment")'),
            ("FCataclysmForgeEnchantmentWork UCataclysmForge::ChangeNegativeOf(", "ChangeNegativeOperationRow",
             "Changing an enchantment's drawback takes a Regret Essence",
             "GoldRefusal(TEXT(\"Changing an enchantment's drawback\")")):
        rule = body(text, signature)
        assert f"BaseGoldOf({row}," in rule, f"{signature} does not read its row's Base Gold"
        assert "DaysFor(" in rule and "GoldFor(" in rule, f"{signature} does not use DaysFor and GoldFor"
        assert -1 < rule.find(material) < rule.find(gold), f"{signature} asks for gold before its material"
        assert rule.rfind("Out.Refusal") == rule.find("Out.Refusal = " + gold), f"{signature}: gold is not the last refusal"


def test_an_imprint_needs_every_slot_filled_and_the_gear_level_its_rarity_needs() -> None:
    rule = body(code(FORGE_CPP), "FCataclysmForgeEnchantmentWork UCataclysmForge::ImprintOf(")
    assert ("Item.EnchantmentCount + Item.Affixes.Num() < UCataclysmItemValues::SlotsPerPiece" in rule), (
        "an imprint is not refused on a piece with a slot empty (Q16)")
    missing = rule.find("if (GearRarityTable == nullptr)")
    gate = rule.find("Out.GearLevelNeeded = UCataclysmDropRoll::GearLevelGateFor(GearRarityTable, Out.RarityAfter);")
    refused = rule.find("if (Item.GearLevel < Out.GearLevelNeeded)")
    assert -1 < missing < gate < refused, (
        "an imprint does not read the gear level the rarity after needs, refusing a missing table first "
        f"(positions {missing}, {gate}, {refused})")
    assert "Item.Affixes.Num() - 1, Out.RarityAfter" in rule, "the rarity after is not one affix fewer and one pair more"


def test_a_drop_draws_what_it_drew_before_and_only_the_imprint_passes_the_pairs_on_a_piece() -> None:
    roll = body(code(DROP_CPP), "bool UCataclysmDropRoll::RollEnchantments(")
    block = roll[roll.find("if (AlreadyOn)"):roll.find("for (int32 Filled = 0;")]
    assert "TakenPositives.Add(Held.Positive);" in block and "TakenNegatives.Add(Held.Negative);" in block, (
        "the pairs on the piece are not read into the taken sets")
    assert "Stream" not in block, "reading the pairs on the piece touches the stream, which moves every seeded drop"
    passing = []
    for path in SOURCE.rglob("*.cpp"):
        if "/Tests/" in path.as_posix():
            continue
        for call in calls(code(path), "RollEnchantments"):
            if call.count(",") >= 6 and not call.startswith("RollEnchantments( const"):
                passing.append(f"{path.name}: {call[:140]}")
    assert passing == [
        "CataclysmForge.cpp: RollEnchantments(PositiveTable, NegativeTable, Slot, 1, Stream, Rolled, &Item.Enchantments)"
    ], f"what passes the pairs on a piece: {passing}"
    # THE CONTROL: the sweep sees the drop's own call.
    assert any("OutItem.EnchantmentCount, Stream" in call for call in calls(code(DROP_CPP), "RollEnchantments")), (
        "the sweep did not find the drop's call, so it proves nothing")


def test_a_changed_negative_is_drawn_at_or_below_its_positives_weight_and_never_for_a_set() -> None:
    draw = body(code(DROP_CPP), "bool UCataclysmDropRoll::DrawReplacementNegative(")
    assert "EnchantmentSetId(*Positive) > 0" in draw, "a set's pair can have its negative redrawn"
    assert "for (const FCataclysmRolledEnchantment& Held : OnThePiece) { Taken.Add(Held.Negative); }" in draw, (
        "not every negative on the piece is taken, so the same one, or another pair's, can be drawn again (Q18)")
    assert "bBandCanSupply.Add(Band <= PositiveBand && bHasUntaken);" in draw, (
        "the bands offered are not held to the positive's weight, so a milder negative can be drawn (Q17)")
    assert "DrawEnchantmentWeight(bBandCanSupply, Stream)" in draw and "DrawEnchantmentInBand(" in draw, (
        "the negative is not drawn as a drop draws one")
    assert "EnchantmentValueStreamSalt" in draw and "OutPair.NegativeRoll = Values.GetFraction();" in draw, (
        "the new negative's roll is not drawn from a stream of its own")
    assert "OutPair = Kept;" in draw, "the positive and its roll are not kept"


def test_the_craft_keeps_slice_f1s_order_and_draws_from_the_item_and_day_before_the_days_pass() -> None:
    work = body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoEnchantmentWork(")
    imprint = work.find("UCataclysmForge::DrawImprint(Before, BaseTable, Positives, Negatives, Run.WalkSeedMask, "
                        "Run.Day(),")
    change = work.find("UCataclysmForge::DrawChangedNegative(Before, Plan.Pair, BaseTable, Positives, Negatives, "
                       "Run.WalkSeedMask, Run.Day(),")
    # SLICE F5: THE GOLD, THE MATERIAL, THE ITEM AND THE DAYS ARE `DoItemCraft`'S, in that order, and the draw is inside
    # the change it is handed; `test_every_item_craft_goes_through_one_sequence.py` holds the order and that a refused change gives back
    # the material, the gold and the mark.
    handed = work.find("DoItemCraft(")
    assert -1 < handed < imprint and handed < change, (
        "the draw from the item as it stood is not inside the change DoItemCraft makes "
        f"(positions {handed}, {imprint}, {change})")
    failed = work[work.find("if (!bDrawn) {"):]
    assert "OutRefusal = TEXT(" in failed and "return bDrawn;" in failed, (
        "a draw that fails does not refuse the change, so DoItemCraft would not give back what was taken")


def test_the_screen_offers_an_imprint_on_a_full_piece_and_no_change_for_a_set() -> None:
    actions = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeEnchantmentAction> UCataclysmForgeLayout::EnchantmentActionsFor(")
    assert "Item.EnchantmentCount + Item.Affixes.Num() >= UCataclysmItemValues::SlotsPerPiece" in actions, (
        "the screen offers an imprint on a piece with a slot empty")
    assert "UCataclysmDropRoll::EnchantmentSetId(*Positive) > 0" in actions, "the screen offers to change a set's drawback"
