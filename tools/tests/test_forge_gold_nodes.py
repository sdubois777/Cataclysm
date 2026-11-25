"""The empire tree's five forge gold nodes, issue #47 slice F6a, checked in the source as text.

Ruled by the coordinating session on 2026-11-22 (Q1 to Q10; Architectural Synergy's 30% cap the owner's) and on
2026-11-24 (A: the pricing is a trailing parameter whose default is no discount, so every plan call the game makes has
to pass the character's, which a check here makes sure of; B: an option's gold is discounted at its own material's tier,
one Forge Upgrades roll a craft). The Unreal tests in `CataclysmForgeGoldNodesTests.cpp` check the figures; these check
what a test of figures cannot see: that every price goes through the pricing before its refusal, that the game never
leaves it at its default, and that the count is added only once a craft is done.

  1. the five nodes are built in slice 11 with their rows, and their caps are the rulings';
  2. the factor holds the reductions at 75% and takes Forge Upgrades' tenth after, as the city upgrade's does;
  3. every plan with a price applies the pricing after its price and before its gold refusal;
  4. an option's gold is discounted at its own material, the Chaos Stabilizer's and the Tainted Shard's;
  5. every plan call and every options call the game makes passes a pricing (ruling A);
  6. Treasury Synergy's count is added once a craft is done, never before a refusal, in tier 1 gold;
  7. both records save the count: the account at version 8, the character at 18 (after skill selection's 17), each
     with its committed file and every chain test stepping to it;
  8. the forge screen says what the tree took off a price.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
import generate_datatables as gen  # noqa: E402

SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
TREE_H = SOURCE / "Player" / "CataclysmEmpireTree.h"
TREE_CPP = SOURCE / "Player" / "CataclysmEmpireTree.cpp"
WIDGET_CPP = SOURCE / "Interface" / "CataclysmForgeWidget.cpp"
RECORDS_H = SOURCE / "Save" / "CataclysmSaveRecords.h"
RECORDS_CPP = SOURCE / "Save" / "CataclysmSaveRecords.cpp"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"

FIVE = {"Workshop Upgrades": [("CraftGoldReduced", 0.015)],
        "Apprentice Oversight": [("LowTierCraftGoldReduced", 0.20)],
        "Architectural Synergy": [("CraftGoldReducedPerBranchStep", 0.01, 10, "Treasury", 0.30)],
        "Treasury Synergy": [("CraftGoldReducedPerSpendingStep", 0.01, 0, "", 0.30)],
        "Forge Upgrades": [("CraftGoldTenthOffChance", 0.01)]}

#: Each plan with a price, and the material it prices at (the plan's own field).
PLANS = {"UpgradeOf": "Out.Stone", "CombineOf": "Out.From", "GemCombineOf": "NAME_None",
         "SocketGemOf": "Out.Material", "UnsocketGemOf": "Out.Material", "AddSocketOf": "Out.Material",
         "ChangeSocketTypeOf": "Out.Material", "AffixWorkOf": "Out.Material", "DeterministicAffixOf": "Out.Material",
         "AffixShiftOf": "Out.Material", "DamageTypeWorkOf": "Out.Material", "ImprintOf": "Out.Material",
         "ChangeNegativeOf": "Out.Material", "CleanseOf": "Out.Material"}


def code(path: pathlib.Path) -> str:
    """The file with its comments blanked, so a comment cannot satisfy a check."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The braces-matched body of the function or namespace whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} is not closed")


def test_the_five_nodes_are_built_with_their_rows_and_caps() -> None:
    for name, effects in FIVE.items():
        assert gen.EMPIRE_TREE_BUILT.get(name) == 11, f"{name} is not built in slice 11"
        assert name not in gen.EMPIRE_TREE_REFUSED, f"{name} is still refused"
        assert gen.EMPIRE_TREE_EFFECTS.get(name) == effects, f"{name}'s row is {gen.EMPIRE_TREE_EFFECTS.get(name)}"
    kinds = gen.EMPIRE_TREE_EFFECT_KINDS
    assert kinds[-5:] == ("CraftGoldReduced", "LowTierCraftGoldReduced", "CraftGoldReducedPerBranchStep",
                          "CraftGoldReducedPerSpendingStep", "CraftGoldTenthOffChance")


def test_the_factor_holds_the_reductions_at_three_quarters_and_takes_the_tenth_after() -> None:
    header = flat(code(TREE_H))
    for constant in ("static constexpr float CraftGoldReductionCap = UpgradePriceReductionCap;",
                     "static constexpr int64 TreasurySynergyGoldAStep = 50000;",
                     "static constexpr float ForgeUpgradesFactor = 0.9f;",
                     "static constexpr int32 LowTierCraftHighestTier = 2;"):
        assert constant in header, f"the tree's header lacks {constant}"
    tree = code(TREE_CPP)
    factor = flat(body(tree, "float UCataclysmEmpireTree::CraftGoldFactorFor("))
    assert "CraftGoldFromSpendingFor(Bonuses, CraftGoldSpentAtTierOne)" in factor
    assert "MaterialTier >= 1 && MaterialTier <= LowTierCraftHighestTier" in factor
    capped = factor.find("FMath::Clamp(Reduced, 0.0f, CraftGoldReductionCap)")
    tenth = factor.find("AfterTheReductions * ForgeUpgradesFactor")
    assert -1 < capped < tenth, "the tenth is not taken after the reductions are held at their cap (Q9)"
    spending = flat(body(tree, "float UCataclysmEmpireTree::CraftGoldFromSpendingFor("))
    assert "FMath::FloorToDouble(GoldSpentAtTierOne / static_cast<double>(TreasurySynergyGoldAStep))" in spending
    assert "FMath::Min(Share, Bonuses.CraftGoldSpendingCap)" in spending
    player = body(tree, "FCataclysmEmpireTreePlayerBonuses UCataclysmEmpireTree::PlayerBonusesFor(")
    assert ("case ECataclysmEmpireTreeEffect::CraftGoldReduced: case ECataclysmEmpireTreeEffect::"
            "CraftGoldReducedPerBranchStep: Bonuses.CraftGoldReduced += Total;") in flat(player)


def test_every_plan_applies_the_pricing_after_its_price_and_before_its_refusal() -> None:
    forge = code(FORGE_CPP)
    header = flat(code(FORGE_H))
    for name, material in PLANS.items():
        match = re.search(rf"\n\w[\w<>]* UCataclysmForge::{name}\(", forge)
        assert match, name
        signature = flat(forge[match.start():forge.index("{", match.start())]).rstrip()
        assert signature.endswith("int64 GoldHeld, const FCataclysmForgePricing& Pricing)"), f"{name}: {signature[-80:]}"
        declared = re.search(rf"static \w+ {name}\([^;]*\);", header)
        assert declared and declared.group(0).endswith(
            "const FCataclysmForgePricing& Pricing = FCataclysmForgePricing());"), f"{name}'s default is not no discount"
        text = flat(body(forge, f"UCataclysmForge::{name}("))
        prices = [m.end() for m in re.finditer(r"Out\.Gold = (GoldFor|CombineGoldFor)\(", text)]
        applied = text.find(f"Out.Gold = Pricing.Apply(Out.Gold, {material});")
        refused = text.find("if (Out.GoldHeld < Out.Gold)")
        assert prices and applied != -1 and refused != -1, name
        assert max(prices) < applied < refused, f"{name} does not apply the pricing between its price and its refusal"


def test_an_options_gold_is_discounted_at_its_own_material() -> None:
    options = flat(body(code(FORGE_CPP), "UCataclysmForge::OptionsOn("))
    stabilizer = options.find("Out.Gold += GoldFor(BaseGoldOf(ReduceResidueGainOperationRow")
    stabilizer_priced = options.find(
        "Out.Gold = GoldBeforeTheStabilizer + Pricing.Apply(Out.Gold - GoldBeforeTheStabilizer, FName(ChaosStabilizerRow));")
    shards = options.find("Out.Gold += GoldFor(BaseGoldOf(AccelerateCraftOperationRow")
    shards_priced = options.find(
        "Out.Gold = GoldBeforeTheShards + Pricing.Apply(Out.Gold - GoldBeforeTheShards, FName(TaintedShardRow));")
    refusal = options.find("CraftGold + Out.Gold")
    assert -1 < stabilizer < stabilizer_priced < shards < shards_priced < refusal, (
        "an option's gold is not discounted at its own material before the options' gold refusal (ruling B)")


def test_every_plan_and_options_call_the_game_makes_passes_a_pricing() -> None:
    """Ruling A: the default prices at full, so a call that leaves it out would charge a player holding the nodes the
    whole price. A plan call with no item passes no gold held either; it refuses, and needs none."""
    craft = code(CRAFT_CPP)
    passed = 0
    for match in re.finditer(r"UCataclysmForge::(\w+Of)\(", craft):
        index, depth = match.end(), 1
        while depth:
            depth += {"(": 1, ")": -1}.get(craft[index], 0)
            index += 1
        arguments = flat(craft[match.end():index - 1])
        if "GoldHeldIn(WorldContext)" not in arguments:
            continue
        assert arguments.endswith((", PricingFor(WorldContext, Target)", ", CombinePricingFor(WorldContext, Level - 1)",
                                   ", CombinePricingFor(WorldContext, static_cast<int32>(Rarity))")), (
            f"UCataclysmForge::{match.group(1)} is called with the character's gold and without its pricing: {arguments}")
        passed += 1
    assert passed == len(PLANS), f"{passed} plan calls pass the gold held; the plans with a price are {len(PLANS)}"
    options = flat(body(craft, "FCataclysmForgeOptionEffect UCataclysmForgeCraft::OptionsFor("))
    assert "GoldTierOf(WorldContext), GoldHeldIn(WorldContext), Pricing)" in options
    calls = 0
    for path, pricing in ((CRAFT_CPP, "PricingFor(WorldContext, Target)"),
                          (WIDGET_CPP, "UCataclysmForgeCraft::PricingFor(Context(), ChosenTarget)")):
        text = code(path)
        for match in re.finditer(r"OptionsFor\(", text):
            statement = flat(text[match.start():text.find(";", match.start())])
            if "const UObject* WorldContext" in statement:
                continue  # the declaration or the definition
            assert pricing in statement, f"{path.name}: an options call without the craft's pricing: {statement[:120]}"
            calls += 1
    assert calls == 14, f"{calls} options calls: seven crafts and the screen's seven questions are expected"


def test_the_count_is_added_once_a_craft_is_done() -> None:
    craft = code(CRAFT_CPP)
    count = flat(body(craft, "void CountTheForgeGold("))
    assert "UCataclysmDropRoll::GoldScaleFor(Tier)" in count
    assert "UCataclysmGameInstance::AddGoldSpentAtTheForgeFor(WorldContext, static_cast<double>(Gold) / Scale);" in count
    for signature, gold in (("ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(", "Step.Gold"),
                            ("bool UCataclysmForgeCraft::Combine(", "Plan.Gold"),
                            ("bool UCataclysmForgeCraft::CombineGems(", "Plan.Gold")):
        text = flat(body(craft, signature))
        counted = text.find(f"CountTheForgeGold(WorldContext, {gold});")
        assert counted != -1 and text.count("CountTheForgeGold(") == 1, f"{signature} does not count its gold once"
        paid = text.find("PayForTheCraft(")
        last_refund = text.rfind("GiveTheGoldBack(")
        saved = text.find("SaveWhereTheGoldCameFrom(")
        assert -1 < paid < counted and last_refund < counted < saved, (
            f"{signature} counts its gold where a refused craft could still give it back, or after its save")


def test_both_records_save_the_count() -> None:
    header = flat(code(RECORDS_H))
    assert header.count("double GoldSpentAtTheForge = 0.0;") == 2, "the account and the character records each hold it"
    records = code(RECORDS_CPP)
    for space, step, row in (
            ("namespace CataclysmAccountSaveMigration", "bool Migrate_7_to_8(", '{ 7, TEXT("Migrate_7_to_8"), &Migrate_7_to_8 }'),
            ("namespace CataclysmCharacterSaveMigration", "bool Migrate_17_to_18(",
             '{ 17, TEXT("Migrate_17_to_18"), &Migrate_17_to_18 }')):
        steps = body(records, space)
        assert row in flat(steps), f"{step} is not a step of {space}"
        written = flat(body(steps, step))
        assert 'Record->SetNumberField(TEXT("GoldSpentAtTheForge"), Held);' in written and "return false;" in written
    old = json.loads((FIXTURES / "Account_v7.json").read_text(encoding="utf-8"))
    new = json.loads((FIXTURES / "Account_v8.json").read_text(encoding="utf-8"))
    assert new.pop("SchemaVersion") == 8 and old.pop("SchemaVersion") == 7
    assert new.pop("GoldSpentAtTheForge") == 62500.25, "the committed account's count is not a figure"
    assert new == old, "Account_v8.json is not Account_v7.json with the forge count"
    assert "| `Account_v8.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")
    old = json.loads((FIXTURES / "Character_v17.json").read_text(encoding="utf-8"))
    new = json.loads((FIXTURES / "Character_v18.json").read_text(encoding="utf-8"))
    assert new.pop("SchemaVersion") == 18 and old.pop("SchemaVersion") == 17
    assert new.pop("GoldSpentAtTheForge") == 41250.75, "the committed character's count is not a figure"
    assert new == old, "Character_v18.json is not Character_v17.json with the forge count"
    assert "| `Character_v18.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")
    for older, newer in (("AddTheAccountsVersion7Defaults(", "AddTheAccountsVersion8Defaults("),
                         ("AddTheCharactersVersion17Defaults(", "AddTheCharactersVersion18Defaults(")):
        stepping = 0
        for path in sorted((SOURCE / "Tests").glob("*.cpp")):
            text = code(path)
            if older in text:
                stepping += 1
                assert text.count(newer) >= text.count(older), f"{path.name} steps through {older} and not {newer}"
        assert stepping >= 3, f"only {stepping} test files step through {older}"
    gather = flat(code(SOURCE / "Save" / "CataclysmSaveGather.cpp"))
    apply = flat(code(SOURCE / "Save" / "CataclysmSaveApply.cpp"))
    assert "Record.GoldSpentAtTheForge = State->GetOwnGoldSpentAtTheForge();" in gather
    assert "State->SetOwnGoldSpentAtTheForge(Record.GoldSpentAtTheForge);" in apply


def test_the_forge_screen_says_what_the_tree_took_off() -> None:
    widget = code(WIDGET_CPP)
    questions = re.findall(r"Message = UCataclysmForge::(\w+Question)\((.*?);", widget, flags=re.S)
    priced = [(name, statement) for name, statement in questions if name != "DismantleQuestion"]
    assert len(priced) == 8, [name for name, _ in priced]
    for name, statement in priced:
        assert "UCataclysmForge::DiscountText(UCataclysmForgeCraft::PricingFor(Context(), ChosenTarget)," in flat(
            statement), f"{name} does not say what the empire tree took off its price"
