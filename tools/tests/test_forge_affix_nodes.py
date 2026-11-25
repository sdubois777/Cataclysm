"""The empire tree's affix nodes on a forge craft, #2741's crafting group slices C3 and C4, checked in the source as text.

Ruled by the coordinating session on 2026-11-25 (the C3/C4 detail as written, D1 to D4): Master's Workshop and Precision
Crafting place a roll the forge draws (a reroll's, a Deterministic Affix's, Rough Draft's affix's); once a craft has
changed the item, Rough Draft, then Apprentice Smith, then Quality Materials act on it, each on a draw of its own, never
on a dismantle; every priced question says their chances, and the two crafts that draw a roll say the rolls. G4 split
the one "crafting" refusal into what each row still waits on. The Unreal tests in `CataclysmForgeAffixNodesTests.cpp`
check the figures; these check what a test of figures cannot see:

  1. the five nodes are built in slice 14 with their rows, and no row waits on "crafting" any more;
  2. the one item craft sequence applies the nodes to the changed item before its residue is written and it is put back,
     and says what they did after what the material nodes kept;
  3. only the dismantle turns them off;
  4. the reroll's and the Deterministic Affix's draws take the rolls, and the shift's does not;
  5. every priced question says the chances after the tree's costs, and only the reroll and the addition say the rolls.
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

FIVE = {"Precision Crafting": [("AffixRollTopQuarterChance", 0.03)],
        "Master's Workshop": [("AffixRollFloor", 0.02)],
        "Quality Materials": [("AffixRollMaxedChance", 0.10)],
        "Apprentice Smith": [("AffixTierRaisedChance", 0.01)],
        "Rough Draft": [("AffixAddedChance", 0.05)]}

#: G4: what each row that waited on "crafting" waits on now.
SPLIT = {"crafting station": {"Expanded Station (Jeweler)", "Expanded Station (Smith)", "Expanded Station (Enchantress)",
                              "Assembly Line", "The Hidden Stash"},
         "double craft": {"Explorer Synergy"},
         "exalted item": {"Flawless Production", "Residue Mastery"},
         "item tier": {"Grand Forge"},
         "owner's question": {"Ancient Techniques", "Refining Techniques", "Residue Protocols",
                              "The Alchemist's Transmutation"},
         "held-over residue": {"Steady Hands"}}


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


def test_the_five_nodes_are_built_in_slice_fourteen_and_no_row_waits_on_crafting() -> None:
    for name, effects in FIVE.items():
        assert gen.EMPIRE_TREE_BUILT.get(name) == 14, f"{name} is not built in slice 14"
        assert name not in gen.EMPIRE_TREE_REFUSED, f"{name} is still refused"
        assert gen.EMPIRE_TREE_EFFECTS.get(name) == effects, f"{name}'s effects are not {effects}"
        for effect in effects:
            assert effect[0] in gen.EMPIRE_TREE_EFFECT_KINDS
    assert "crafting" not in gen.EMPIRE_TREE_SYSTEMS, "the one crafting reason is still a system"
    assert "crafting" not in gen.EMPIRE_TREE_REFUSED.values(), "a row still waits on crafting"
    for system, names in SPLIT.items():
        assert {name for name, reason in gen.EMPIRE_TREE_REFUSED.items() if reason == system} == names, system
        assert "(issue #2741)" in gen.EMPIRE_TREE_SYSTEMS[system], f"{system!r} does not name issue #2741"


def test_the_sequence_applies_the_nodes_to_the_changed_item_before_it_is_put_back() -> None:
    sequence = flat(body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft("))
    changed = sequence.find("if (!ChangeTheItem(Before, Changed, *Run, Refusal))")
    applied = sequence.find("UCataclysmForge::AffixNodesOn(Changed, Before, AffixNodesFor(WorldContext), "
                            "Run->WalkSeedMask, Run->Day(),")
    gated = sequence.find("const FCataclysmForgeAffixNodesDone TreeAffixes = Step.bTreeAffixNodes ?")
    residue = sequence.find("Changed.Residue = Step.ResidueAfter;")
    put = sequence.find("if (!PutTheItem(Changed))")
    assert -1 < changed < gated < applied < residue < put, (
        "the affix nodes are not applied, behind the step's switch, to the changed item after the craft's own change and "
        "before its residue is written and it is put back")
    assert "Run->DifficultyTier)" in sequence[applied:residue], "Rough Draft's tier does not read the run's difficulty"
    kept = sequence.find("FCataclysmPlayLoopLog::ForgeMaterialsKeptLine(")
    said = sequence.find("if (TreeAffixes.Any()) { FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeAffixNodesLine("
                         " Name, UCataclysmForge::AffixNodesDoneText(TreeAffixes, AffixTable)));")
    assert -1 < kept < said, "what the affix nodes did is not said, only when they did anything, after what was kept"


def test_only_the_dismantle_turns_the_nodes_off() -> None:
    craft = code(CRAFT_CPP)
    off = [m.start() for m in re.finditer(r"\bbTreeAffixNodes = false;", craft)]
    assert len(off) == 1, f"{len(off)} crafts turn the affix nodes off; only the dismantle should"
    dismantle = craft.find("ECataclysmForgeResult UCataclysmForgeCraft::Dismantle(")
    following = craft.find("\nECataclysmForgeResult UCataclysmForgeCraft::", dismantle + 1)
    assert -1 < dismantle < off[0] < (following if following != -1 else len(craft)), "it is not the dismantle that does"
    header = flat(code(SOURCE / "Player" / "CataclysmForgeCraft.h"))
    assert "bool bTreeAffixNodes = true;" in header, "an item craft does not take the affix nodes unless it says so"


def test_the_reroll_and_the_addition_draw_with_the_rolls_and_the_shift_does_not() -> None:
    forge = code(FORGE_CPP)
    reroll = flat(body(forge, "float UCataclysmForge::RerolledAffixRoll("))
    assert "return Rolls.Shape(Stream.FRand(), Seed);" in reroll
    addition = flat(body(forge, "FCataclysmForgeAffixChange UCataclysmForge::DeterministicAffixOf("))
    assert "Out.Roll = Rolls.Shape(Stream.FRand(), Seed);" in addition
    assert "Rolls" not in flat(body(forge, "FCataclysmForgeAffixChange UCataclysmForge::AffixShiftOf(")), (
        "the shift keeps its roll (F1 Q12); it must not take the rolls")
    shape = flat(body(forge, "float FCataclysmForgeRolls::Shape("))
    assert "Low = FMath::Max(Low, UCataclysmForge::TopQuarterFloor);" in shape and "return Low + (1.0f - Low) * Draw;" in shape
    craft = code(CRAFT_CPP)
    assert flat("UCataclysmForge::RerolledAffixRoll(Before, Plan.Affix, Run.WalkSeedMask, Run.Day(), "
                "RollsFor(WorldContext))") in flat(craft)
    plan = flat(body(craft, "FCataclysmForgeAffixChange UCataclysmForgeCraft::AddAffixPlanFor("))
    assert plan.rstrip("} ").endswith("PricingFor(WorldContext, Target), RollsFor(WorldContext));"), plan[-160:]
    header = flat(code(FORGE_H))
    assert "static constexpr float TopQuarterFloor = 0.75f;" in header
    for salt in ("PrecisionCraftingSalt", "RoughDraftSalt", "ApprenticeSmithSalt", "QualityMaterialsSalt"):
        assert re.search(rf"static constexpr int32 {salt} = 0x[0-9A-F]{{8}};", header), salt


def test_every_priced_question_says_the_chances_and_only_the_two_draws_say_the_rolls() -> None:
    widget = code(WIDGET_CPP)
    questions = re.findall(r"Message = UCataclysmForge::(\w+Question)\((.*?);", widget, flags=re.S)
    priced = [(name, flat(statement)) for name, statement in questions
              if "UCataclysmForgeCraft::KeptTextFor(" in statement]
    assert len(priced) == 8, [name for name, _ in priced]
    draws = {"AffixWorkQuestion": "Plan.Craft == ECataclysmForgeAffixCraft::RerollValue",
             "AffixChangeQuestion": "!Plan.bShift"}
    for name, text in priced:
        costs = max(text.find("UCataclysmForge::CostsText(Effect, Plan.ResidueBefore)"),
                    text.find("UCataclysmForge::CleanseCostsText(Plan)"))
        nodes = text.find(f"UCataclysmForgeCraft::AffixNodesTextFor( Context(), {draws.get(name, 'false')})"
                          if name == "AffixWorkQuestion"
                          else f"UCataclysmForgeCraft::AffixNodesTextFor(Context(), {draws.get(name, 'false')})")
        assert -1 < costs < nodes, f"{name} does not say the affix nodes after the tree's costs, as it should"
    text = flat(body(code(CRAFT_CPP), "FString UCataclysmForgeCraft::AffixNodesTextFor("))
    assert "(bDrawsARoll ? UCataclysmForge::RollsText(Nodes.Rolls) : FString()) + UCataclysmForge::AffixNodesChanceText(Nodes)" \
        in text
