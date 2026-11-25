"""The empire tree's crafting nodes on a forge craft's residue and days, #2741's crafting group slices C1 and C2, checked in
the source as text.

Ruled by the coordinating session on 2026-11-25: Tool Maintenance, Limited Knowledge (below +6 before the craft) and
Heat Shielding (a tier 3+ craft material only) multiply the residue a craft adds, each compounded per point; Stable
Compounds takes 0.025 a point off the Chaos Stabilizer's share; Master's Touch leaves the residue at 0 on a draw of 1% a
whole hundred; the days, after the Tainted Shards, take the multipliers, then Master Craftsman's days off, then are
rounded, a craft of a day keeping one and a craft of none staying at none. The costs are worked out once and passed to
`OptionsOn` and the cleanse's plan, so the question and the press read the same figures (G2). The Unreal tests in
`CataclysmForgeTreeCostsTests.cpp` check the figures; these check what a test of figures cannot see:

  1. the eight nodes are built in slice 13 with their rows;
  2. `OptionsOn` keeps the options' own figures, then applies the tree, before its gold refusal;
  3. every options call the game makes, and the cleanse's plan, passes the costs, each with its craft's own material;
  4. every question with options says what the tree changed, and so does the cleanse's;
  5. the costs read the item's gear level and the craft's own material, and the days keep one and leave none at none.
"""

from __future__ import annotations

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
WIDGET_CPP = SOURCE / "Interface" / "CataclysmForgeWidget.cpp"

EIGHT = {"Tool Maintenance": [("CraftResidueKept", 0.05)],
         "Limited Knowledge": [("LowLevelCraftResidueKept", 0.025)],
         "Heat Shielding": [("RareMaterialCraftResidueKept", 0.025)],
         "Stable Compounds": [("StabilizerShareReduced", 0.025)],
         "Master's Touch": [("ResidueResetChancePerHundred", 0.01)],
         "Master Craftsman": [("CraftDaysOff", 1.0)],
         "Industrial Speed": [("CraftDaysReduced", 0.10)],
         "Sovereign's Assembly": [("CraftDaysReducedMore", 0.5)]}


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


def test_the_eight_nodes_are_built_in_slice_thirteen_with_their_rows() -> None:
    for name, effects in EIGHT.items():
        assert gen.EMPIRE_TREE_BUILT.get(name) == 13, f"{name} is not built in slice 13"
        assert name not in gen.EMPIRE_TREE_REFUSED, f"{name} is still refused"
        assert gen.EMPIRE_TREE_EFFECTS.get(name) == effects, f"{name}'s effects are not {effects}"
        for effect in effects:
            assert effect[0] in gen.EMPIRE_TREE_EFFECT_KINDS


def test_options_on_keeps_the_options_figures_then_applies_the_tree_before_its_refusal() -> None:
    options = flat(body(code(FORGE_CPP), "UCataclysmForge::OptionsOn("))
    shards = options.find("Out.Days = Days - Out.DaysSaved;")
    kept_residue = options.find("Out.OptionsResidueAfter = Out.ResidueAfter;")
    kept_days = options.find("Out.OptionsDays = Out.Days;")
    factor = options.find("Out.ResidueAfter = ResidueBefore + (Out.ResidueAfter - ResidueBefore) * TreeShare;")
    reset = options.find("Out.ResidueAfter = 0.0f;")
    days = options.find("Out.Days = TreeDaysFor(Out.Days, Costs);")
    refusal = options.find("CraftGold + Out.Gold")
    assert -1 < shards < kept_residue < kept_days < factor < reset < days < refusal, (
        "OptionsOn does not keep the options' figures and then apply the tree's residue, reset and days, after the shards "
        "and before its gold refusal")
    assert "StabilizedResidueShare - Costs.StabilizerShareReduced" in options, "Stable Compounds does not act on the share"


def test_every_options_call_and_the_cleanse_pass_the_costs_with_the_crafts_own_material() -> None:
    craft = code(CRAFT_CPP)
    calls = 0
    for path, costs in ((CRAFT_CPP, "CostsFor(WorldContext, Target, "),
                        (WIDGET_CPP, "UCataclysmForgeCraft::CostsFor(Context(), ChosenTarget, ")):
        text = code(path)
        for match in re.finditer(r"OptionsFor\(", text):
            statement = flat(text[match.start():text.find(";", match.start())])
            if "const UObject* WorldContext" in statement:
                continue  # the declaration or the definition
            assert costs in statement, f"{path.name}: an options call without the costs: {statement[:120]}"
            material = statement.split(costs, 1)[1]
            assert material.startswith(("Plan.Material)", "Plan.Stone)")), f"{path.name}: the costs of another material"
            calls += 1
    assert calls == 14, f"{calls} options calls: seven crafts and the screen's seven questions are expected"
    upgrade = flat(body(craft, "ECataclysmForgeResult UCataclysmForgeCraft::Upgrade("))
    assert "CostsFor(WorldContext, Target, Plan.Stone)" in upgrade, "the upgrade's costs are not its stone's"
    cleanse = flat(body(craft, "FCataclysmForgeCleanse UCataclysmForgeCraft::CleansePlanFor("))
    assert "CostsFor(WorldContext, Target, FName(UCataclysmForge::PurifiedEssenceRow))" in cleanse
    assert "Pricing, Costs);" in flat(body(craft, "FCataclysmForgeOptionEffect UCataclysmForgeCraft::OptionsFor("))


def test_every_question_with_options_and_the_cleanse_say_what_the_tree_changed() -> None:
    widget = code(WIDGET_CPP)
    questions = re.findall(r"Message = UCataclysmForge::(\w+Question)\((.*?);", widget, flags=re.S)
    with_options = [(name, flat(statement)) for name, statement in questions if "OptionsQuestion(Effect" in statement]
    assert len(with_options) == 7, [name for name, _ in with_options]
    for name, text in with_options:
        options = text.find("UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days)")
        costs = text.find("UCataclysmForge::CostsText(Effect, Plan.ResidueBefore)")
        assert -1 < options < costs, f"{name} does not say what the tree changed after the options' sentences"
    cleanse = [flat(statement) for name, statement in questions if name == "CleanseQuestion"]
    assert cleanse and "UCataclysmForge::CleanseCostsText(Plan)" in cleanse[0]


def test_the_costs_read_the_gear_level_and_the_crafts_material_and_the_days_keep_one() -> None:
    craft = flat(body(code(CRAFT_CPP), "FCataclysmForgeCosts ForgeCostsOf("))
    assert "Item.GearLevel < UCataclysmForge::LimitedKnowledgeBelowLevel" in craft
    assert "UCataclysmForgeCraft::MaterialTierOf(CraftMaterial) >= UCataclysmForge::HeatShieldingLowestTier" in craft
    header = flat(code(FORGE_H))
    assert "static constexpr int32 LimitedKnowledgeBelowLevel = 6;" in header
    assert "static constexpr int32 HeatShieldingLowestTier = 3;" in header
    assert "static constexpr float MastersTouchResidueStep = 100.0f;" in header
    salts = re.findall(r"static constexpr int32 (\w+Salt) = (0x[0-9A-Fa-f]+);", header)
    assert "MastersTouchSalt" in dict(salts)
    assert len({value.upper() for _, value in salts}) == len(salts), f"two draws of the forge share a salt: {salts}"
    days = flat(body(code(FORGE_CPP), "int32 UCataclysmForge::TreeDaysFor("))
    assert "if (Days <= 0) { return 0; }" in days, "a craft of no day does not stay at none"
    assert "return FMath::Max(1, FMath::RoundToInt(Taken));" in days, "a craft of a day does not keep one"
    touch = flat(body(code(FORGE_CPP), "bool UCataclysmForge::ResidueResetLands("))
    assert "FMath::FloorToFloat(FMath::Max(0.0f, Item.Residue) / MastersTouchResidueStep)" in touch, (
        "Master's Touch does not count whole hundreds of the residue before the craft")
