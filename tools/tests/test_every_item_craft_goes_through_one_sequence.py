"""Every item craft at the forge goes through one sequence, `UCataclysmForgeCraft::DoItemCraft`, and the residue tools
are applied in one place (issue #47, slice F5), ruled by the coordinating session on 2026-11-22.

Seven item crafts each carried their own copy of the same steps until slice F5: Upgrade, socketing, socket work, the
affix crafts, the affix additions and shifts, the damage-type crafts and the enchantment crafts. They now hand
`DoItemCraft` a step (the materials, one gold payment, the residue and days after the options) and two lambdas (the
item's change and the craft's own line). The two combines change no item, add no residue and pass no day, and keep
their own short sequence; the material nodes never act on them (F6's Q11).

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. DoItemCraft keeps the order every copy kept: the threshold question, the mark, the gold, the materials (through
     `TakeMaterials`, F6b's one place), the hooks' take, the change, the residue, the item put back, the hooks' after,
     the days, the build, the craft's line, the options' line, the save. A change made after the days would read the
     wrong day for a drawn result; a line said before them would state days that have not passed;
  2. every failure after the gold is taken gives the gold back, and every failure after the materials gives them back;
  3. every item craft calls DoItemCraft, and no other function in the file pays, takes materials, puts an item back or
     passes days, except the two combines' own payment and stones -- so F6b's nodes, and any later rule on what a craft
     takes, have one place to act;
  4. the options: a Chaos Stabilizer halves the residue added by a fixed share with no draw (Q27); Tainted Shards
     remove only the residue's penalty days, `CR / 100` rounded down (Q24, G2); each charges its row's Base Gold
     through `GoldFor`, the Shards once per shard (G1); the three rows hold the constants;
  5. the cleanse halves the residue, takes `max(1, ceil(R / 500))` days (Q25), and lifts no Consumption mark (Q26);
  6. `TakeMaterials` reads each material's tier from the table (Q17).
"""
from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

# THE SOCKET CRAFTS' COMMENT STRIPPER, BODY READER AND CONSTANT READER, one copy for every forge slice.
import test_the_forge_adds_and_changes_sockets as sockets  # noqa: E402

SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
DATA = REPO_ROOT / "game" / "Data"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
code = sockets.code
body = sockets.body
constant = sockets.constant

ITEM_CRAFTS = ["Upgrade", "DoSocketing", "DoSocketWork", "CraftAffix", "DoAffixChange", "CraftDamageType",
               "DoEnchantmentWork", "Cleanse"]


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def shared() -> str:
    return squeeze(body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft("))


# --------------------------------------------------------------------------
# 1 and 2: the one sequence
# --------------------------------------------------------------------------

def test_the_one_sequence_keeps_the_order_every_copy_kept() -> None:
    text = shared()
    steps = ["bCrosses = Now < Threshold && After >= Threshold;", "State->MarkByConsumption();",
             "if (!PayForTheCraft(WorldContext, Step.Gold, bGoldFromTheCharacter))",
             "if (!TakeMaterials(Inventory, Step.Materials, Taken))", "Hooks.TakeBeforeTheChange()",
             "if (!ChangeTheItem(Before, Changed, *Run, Refusal))", "Changed.Residue = Step.ResidueAfter;",
             "if (!PutTheItem(Changed))", "Hooks.AfterTheChange()", "Run->AdvanceDays(Step.Days);",
             "State->SetConsumptionBuild(", "SayTheLine(Name, DayBefore, Run->Day());",
             "FCataclysmPlayLoopLog::ForgeOptionsLine(",
             "UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::ForgeCrafted);"]
    places = [text.find(step) for step in steps]
    missing = [step for step, place in zip(steps, places, strict=True) if place == -1]
    assert not missing, f"DoItemCraft no longer does {missing}"
    assert places == sorted(places), (
        "DoItemCraft no longer keeps the order: "
        + " -> ".join(step for _, step in sorted(zip(places, steps, strict=True))))


def test_every_failure_after_the_gold_gives_back_what_was_taken() -> None:
    text = shared()
    after = text[text.find("if (!PayForTheCraft(WorldContext, Step.Gold, bGoldFromTheCharacter))"):]
    after = after[after.find("}") + 1:]
    exits = [match.start() for match in re.finditer(r"return ECataclysmForgeResult::Refused;", after)]
    assert len(exits) == 5, f"expected five refusals after the payment, found {len(exits)}"
    previous = 0
    for number, at in enumerate(exits):
        segment = after[previous:at]
        assert "GiveTheGoldBack(WorldContext, Step.Gold);" in segment and "TakeBackTheMark();" in segment, (
            f"refusal {number + 1} after the payment keeps the gold or the mark: {segment[-200:]}")
        if number > 0:
            assert "GiveMaterialsBack(Inventory, Taken);" in segment, (
                f"refusal {number + 1}, after the materials were taken, keeps them: {segment[-200:]}")
        previous = at


# --------------------------------------------------------------------------
# 3: one place
# --------------------------------------------------------------------------

def function_bodies(text: str) -> dict[str, str]:
    out = {}
    for match in re.finditer(r"^\S[^\n;]*UCataclysmForgeCraft::(\w+)\(", text, flags=re.MULTILINE):
        out[match.group(1)] = body(text, match.group(0))
    return out


def test_every_item_craft_hands_its_craft_to_the_one_sequence() -> None:
    bodies = function_bodies(code(CRAFT_CPP))
    for name in ITEM_CRAFTS:
        assert name in bodies, f"CataclysmForgeCraft.cpp no longer defines {name}"
        assert "DoItemCraft(" in bodies[name], f"{name} no longer goes through DoItemCraft"


def test_no_other_function_pays_takes_puts_back_or_passes_days() -> None:
    bodies = function_bodies(code(CRAFT_CPP))
    allowed = {
        "PayForTheCraft(": {"DoItemCraft", "Combine", "CombineGems"},
        # Dismantle removes only materials it has just added itself, when a later add fails and it puts everything back
        # (issue #2848). It takes nothing the player paid with.
        "RemoveMaterial(": {"TakeMaterials", "Combine", "Dismantle"},
        "ReplaceEquippedAt(": {"DoItemCraft"},
        "ReplaceItemAt(": {"DoItemCraft"},
        "AdvanceDays(": {"DoItemCraft"},
    }
    assert len(bodies) > 20, f"only {len(bodies)} functions read from CataclysmForgeCraft.cpp; the reader is wrong"
    for call, owners in allowed.items():
        found = {name for name, text in bodies.items() if call in text}
        assert found <= owners, f"{sorted(found - owners)} call {call} outside the one sequence"
        assert found, f"nothing calls {call}; the check reads the wrong file"


# --------------------------------------------------------------------------
# 4 and 5: the options and the cleanse
# --------------------------------------------------------------------------

def test_the_three_rows_hold_the_tools_figures() -> None:
    with (DATA / "ForgeOperations.csv").open(encoding="utf-8", newline="") as handle:
        operations = {row["Name"]: row for row in csv.DictReader(handle)}
    names = dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+Row) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))
    stabilizer = operations[names["ReduceResidueGainOperationRow"]]
    shard = operations[names["AccelerateCraftOperationRow"]]
    cleanse = operations[names["CleanseOperationRow"]]
    assert stabilizer["CRImpact"] == f"-{100 - round(constant('StabilizedResidueShare') * 100)}% CR gain", stabilizer
    assert shard["BaseDays"] == f"-{constant('DaysPerTaintedShard'):g}", shard
    assert cleanse["CRImpact"] == f"-{100 - round(constant('CleansedResidueShare') * 100)}% CR", cleanse
    assert cleanse["BaseDays"] == f"1 day per {constant('ResiduePerCleanseDay'):g} CR", cleanse
    for row in (stabilizer, shard, cleanse):
        assert float(row["BaseGold"]) == constant("GoldPerCraftDay"), row


def test_the_options_halve_the_residue_and_remove_only_penalty_days_each_at_its_rows_gold() -> None:
    rule = squeeze(body(code(FORGE_CPP), "FCataclysmForgeOptionEffect UCataclysmForge::OptionsOn("))
    assert "Out.ResidueAfter = ResidueBefore + Added * StabilizedResidueShare;" in rule, "the Stabilizer no longer halves"
    assert "Random" not in rule and "FRand" not in rule, "an option draws; Q27 makes the halving certain"
    assert "const int32 Penalty = PenaltyDaysFor(ResidueBefore);" in rule, "the Shards no longer read the penalty days"
    assert "if (Shards * DaysPerTaintedShard > Penalty)" in rule, "more Shards than penalty days are not refused (Q24, G2)"
    assert ("GoldFor(BaseGoldOf(ReduceResidueGainOperationRow, BaseGoldFor(0)), ResidueBefore, DifficultyTier);" in rule
            and "GoldFor(BaseGoldOf(AccelerateCraftOperationRow, BaseGoldFor(0)), ResidueBefore, DifficultyTier) * Shards;"
            in rule), "an option no longer charges its row's Base Gold through GoldFor, the Shards once per shard (G1)"
    penalty = squeeze(body(code(FORGE_CPP), "int32 UCataclysmForge::PenaltyDaysFor("))
    assert "FMath::FloorToInt(FMath::Max(0.0f, Residue) / ResiduePerPenaltyDay)" in penalty, (
        "the penalty days are no longer the residue's CR / 100, rounded down")


def test_the_cleanse_halves_the_residue_in_its_rows_days_and_lifts_no_mark() -> None:
    rule = squeeze(body(code(FORGE_CPP), "FCataclysmForgeCleanse UCataclysmForge::CleanseOf("))
    assert "Out.ResidueAfter = FMath::Max(0.0f, Item.Residue) * CleansedResidueShare;" in rule, "the cleanse no longer halves"
    assert ("Out.Days = FMath::Max(1, FMath::CeilToInt(FMath::Max(0.0f, Item.Residue) / ResiduePerCleanseDay));"
            in rule), "a cleanse no longer takes a day per 500 residue or part of it, at least one (Q25)"
    craft = code(CRAFT_CPP)
    cleanse = body(craft, "ECataclysmForgeResult UCataclysmForgeCraft::Cleanse(")
    assert "ClearConsumptionMark" not in cleanse, "the cleanse lifts a Consumption mark (Q26)"
    taken_back = squeeze(shared())
    assert taken_back.count("State->ClearConsumptionMark();") == 1 and "if (bCrosses && State != nullptr && !bWasMarked)" in (
        taken_back), "DoItemCraft clears a mark it did not set, so a cleanse could lift one (Q26)"


# --------------------------------------------------------------------------
# 6: the tier of each material taken
# --------------------------------------------------------------------------

def test_the_material_step_reads_each_materials_tier() -> None:
    text = code(CRAFT_CPP)
    tier = body(text, "int32 UCataclysmForgeCraft::MaterialTierOf(")
    assert "UCataclysmDropRoll::LoadCraftingMaterialTable()" in tier and "Row->Tier" in tier, (
        "a material's tier is no longer read from the crafting material table (Q17)")
    step = body(text, "FCataclysmForgeCraftStep ForgeStepOf(")
    assert "Step.Materials.Append(Effect.Materials);" in step, "the options' materials are not in the step's list (Q17)"
