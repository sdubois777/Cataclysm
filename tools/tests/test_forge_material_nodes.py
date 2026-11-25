"""The empire tree's three forge material nodes, issue #47 slice F6b, checked in the source as text.

Ruled by the coordinating session on 2026-11-22 (Q11 to Q17, in the F6 plan) and on 2026-11-25 (N1 to N5): Material
Conservation keeps a forge item craft's tier 1 and 2 materials, Resource Management its tier 3 to 5 ones, Tempting
Fate every material on an item whose residue before the craft is above 500; each drawn once a craft from the item and
the day, independently; the combines never keep; a gem is not a crafting material; the cleanse counts; the craft takes
everything and puts the kept materials back once it can no longer fail, before its days pass; the question names what
will be kept, and a PLAY LOOP line what was. The Unreal tests in `CataclysmForgeMaterialNodesTests.cpp` check the
figures; these check what a test of figures cannot see:

  1. the three nodes are built in slice 12 with their rows;
  2. the item craft sequence puts the kept materials back once, after its last failure path and before its days;
  3. the take itself still takes every material, so a refused or failed craft gives back what it took;
  4. the combines never draw the keeping;
  5. the three draws each have a salt of their own, and Tempting Fate reads the residue as MORE than 500;
  6. every priced question of the forge screen names what the tree will keep, from the materials its craft takes.
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

THREE = {"Material Conservation": [("LowTierMaterialKeptChance", 0.02)],
         "Resource Management": [("RareMaterialKeptChance", 0.01)],
         "Tempting Fate": [("HighResidueMaterialsReturnedChance", 0.15)]}


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


def test_the_three_nodes_are_built_in_slice_twelve_with_their_rows() -> None:
    for name, effects in THREE.items():
        assert gen.EMPIRE_TREE_BUILT.get(name) == 12, f"{name} is not built in slice 12"
        assert name not in gen.EMPIRE_TREE_REFUSED, f"{name} is still refused"
        assert gen.EMPIRE_TREE_EFFECTS.get(name) == effects, f"{name}'s effects are not {effects}"
        for effect in effects:
            assert effect[0] in gen.EMPIRE_TREE_EFFECT_KINDS


def test_the_sequence_keeps_once_after_its_last_failure_and_before_its_days() -> None:
    sequence = flat(body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft("))
    drawn = sequence.find("ForgeMaterialKeepingOf(WorldContext, Before, *Run)")
    kept = sequence.find("Keeping.KeptOf(Taken)")
    given = sequence.find("GiveMaterialsBack(Inventory, Kept);")
    assert -1 < drawn < kept < given, "the sequence does not draw the keeping and give back what it keeps"
    assert sequence.count("GiveMaterialsBack(Inventory, Kept);") == 1, "the kept materials are given back more than once"
    last_failure = sequence.rfind("return ECataclysmForgeResult::Refused;")
    days = sequence.find("Run->AdvanceDays(Step.Days)")
    assert last_failure < given < days, (
        "the kept materials are given back where the craft can still fail, or after its days (N1)")
    taken = sequence.find("TakeMaterials(Inventory, Step.Materials, Taken)")
    assert -1 < taken < drawn, "the keeping is drawn before the materials are taken"
    line = sequence.find("SayTheLine(Name, DayBefore, Run->Day());")
    said = sequence.find("FCataclysmPlayLoopLog::ForgeMaterialsKeptLine(Name, Keeping.KeptPartsOf(Kept))")
    assert -1 < line < said, "the kept line is not said after the craft's own line (N4)"
    assert "if (!Kept.IsEmpty())" in sequence, "the kept line is said when nothing was kept"


def test_the_take_still_takes_every_material() -> None:
    take = flat(body(code(CRAFT_CPP), "bool UCataclysmForgeCraft::TakeMaterials("))
    assert "Inventory->RemoveMaterial(Take.Material, Take.Count)" in take
    for name in ("Keeping", "KeptOf", "NodeFor", "KeepingOn"):
        assert name not in take, f"TakeMaterials decides the keeping ({name}), which N1 put after the craft"


def test_the_combines_never_draw_the_keeping() -> None:
    craft = code(CRAFT_CPP)
    for signature in ("bool UCataclysmForgeCraft::Combine(", "bool UCataclysmForgeCraft::CombineGems("):
        text = body(craft, signature)
        for name in ("Keeping", "KeptOf", "ForgeMaterialKeepingOf"):
            assert name not in text, f"{signature} draws the keeping ({name}): a kept stone makes stones from nothing (Q11)"


def test_the_draws_have_their_own_salts_and_fate_reads_more_than_five_hundred() -> None:
    header = flat(code(FORGE_H))
    salts = re.findall(r"static constexpr int32 (\w+Salt) = (0x[0-9A-Fa-f]+);", header)
    values = [value.upper() for _, value in salts]
    assert len(values) == len(set(values)), f"two draws of the forge share a salt: {salts}"
    for name in ("MaterialConservationSalt", "ResourceManagementSalt", "TemptingFateSalt"):
        assert name in dict(salts), f"no {name}"
    assert "static constexpr float TemptingFateResidueAbove = 500.0f;" in header
    assert "static constexpr int32 MaterialConservationHighestTier = 2;" in header
    keeping = flat(body(code(FORGE_CPP), "FCataclysmForgeKeeping UCataclysmForge::KeepingOn("))
    for chance, salt in (("LowTierChance", "MaterialConservationSalt"), ("RareChance", "ResourceManagementSalt"),
                         ("ReturnAllChance", "TemptingFateSalt")):
        assert f"Lands({chance}, {salt})" in keeping, f"{chance} is not drawn on {salt}"
    assert "Item.Residue > TemptingFateResidueAbove" in keeping, "Tempting Fate does not read 'above 500' as more (N5)"
    node = flat(body(code(FORGE_CPP), "FString FCataclysmForgeKeeping::NodeFor("))
    assert "Tier <= UCataclysmForge::MaterialConservationHighestTier" in node


def test_every_priced_question_names_what_the_tree_will_keep() -> None:
    widget = code(WIDGET_CPP)
    questions = re.findall(r"Message = UCataclysmForge::(\w+Question)\((.*?);", widget, flags=re.S)
    priced = [(name, statement) for name, statement in questions if name != "DismantleQuestion"]
    assert len(priced) == 8, [name for name, _ in priced]
    for name, statement in priced:
        text = flat(statement)
        if name == "UpgradeQuestion":
            wanted = ("UCataclysmForgeCraft::KeptTextFor(Context(), ChosenTarget, Plan.Stone, Plan.StonesNeeded, "
                      "Effect.Materials)")
        elif name == "CleanseQuestion":
            wanted = "UCataclysmForgeCraft::KeptTextFor(Context(), ChosenTarget, Plan.Material, 1, {})"
        else:
            wanted = "UCataclysmForgeCraft::KeptTextFor(Context(), ChosenTarget, Plan.Material, 1, Effect.Materials)"
        assert wanted in text, f"{name} does not name what the empire tree will keep of its own materials (N4)"
    dismantle = [flat(statement) for name, statement in questions if name == "DismantleQuestion"]
    assert dismantle and "KeptTextFor" not in dismantle[0], "the dismantle uses no material, so names none"
