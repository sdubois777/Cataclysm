"""The forge's gem combine: two of a kind and rarity make one of the next, no day, capped by the difficulty tier.

WHY THIS EXISTS. Issue #46, slice G6. The coordinating session ruled on 2026-11-21 that two gems combine into one of
the next rarity as stones do (the design's "Gems are upgraded by combining lower-tier gems", and issue #46's "same as
gear"), that it costs no day because it changes no item, and that combining is capped by the difficulty tier: the
gems' drop rate is one share at every tier, so a low tier drops far more than its ceiling needs, and how high a gem's
rarity can be pushed is where its power comes from. `Cataclysm.GemCombine.`
(`game/Source/Cataclysm/Tests/CataclysmGemCombineTests.cpp`) tests what the craft does; this checks its shape:

  1. a combine takes two, as a stone combine does;
  2. the cap is the highest rarity that drops at full weight at the tier, read from the tier being played;
  3. the craft passes no day, adds no residue, refuses during a walk, says its PLAY LOOP line, saves, and puts the two
     back when the made gem has nowhere to go;
  4. the screen does it on one press, as a stone combine.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
WIDGET_CPP = SOURCE / "Interface" / "CataclysmForgeWidget.cpp"


def code(path: pathlib.Path) -> str:
    """The file with each `//` comment taken off its line, so a mention in a comment cannot satisfy a check."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text)


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


def constant(name: str) -> int:
    found = re.search(rf"static constexpr int32 {name} = (\d+);", code(FORGE_H))
    assert found, f"{FORGE_H.name} declares no number {name}"
    return int(found.group(1))


def test_a_gem_combine_takes_two_as_a_stone_combine_does() -> None:
    assert constant("GemsPerCombine") == constant("StonesPerCombine") == 2, (
        "two gems make one of the next rarity, as two stones make one of the next level ('same as gear')")


def test_the_cap_is_the_tiers_full_weight_rarity_and_is_checked() -> None:
    ceiling = squeeze(body(code(FORGE_CPP), "ECataclysmRarity UCataclysmForge::GemCombineCeiling(", FORGE_CPP.name))
    assert "return UCataclysmDropRoll::HighestUnpenalisedRarity(DifficultyTier);" in ceiling, (
        "the gem combine's cap is no longer the highest rarity that drops at full weight at the tier")
    rule = squeeze(body(code(FORGE_CPP), "FCataclysmForgeGemCombine UCataclysmForge::GemCombineOf(", FORGE_CPP.name))
    assert "Out.Ceiling = GemCombineCeiling(DifficultyTier);" in rule, "GemCombineOf does not ask the cap"
    assert "if (static_cast<uint8>(Out.Into) > static_cast<uint8>(Out.Ceiling))" in rule, (
        "GemCombineOf does not refuse a rarity above the cap")


def test_the_craft_passes_no_day_logs_saves_and_puts_back() -> None:
    craft = squeeze(body(code(CRAFT_CPP), "bool UCataclysmForgeCraft::CombineGems(", CRAFT_CPP.name))
    assert craft.find("OutReason = RefusalNow(WorldContext);") != -1, "CombineGems does not refuse during a walk"
    assert "ACataclysmGameMode::DifficultyTierIn(WorldContext)" in craft, (
        "CombineGems does not read the tier being played, so the cap would not follow it")
    for absent in ("AdvanceDays(", "Residue"):
        assert absent not in craft, f"CombineGems uses {absent}; a gem combine changes no item and costs no day"
    assert "FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeGemCombineLine(" in craft, (
        "CombineGems says no PLAY LOOP line")
    assert "UCataclysmSaveWriter::NoteTriggerIn(ForgeWorldOf(WorldContext), ECataclysmSaveTrigger::ForgeCrafted);" in craft, (
        "CombineGems does not save")
    failed = craft.find("if (Inventory->AddGems(Gem, Plan.Into, 1) == INDEX_NONE) {")
    assert failed != -1 and "Inventory->AddGems(Gem, Plan.From, Plan.GemsNeeded);" in craft[failed:failed + 160], (
        "CombineGems does not put the two gems back when the made one has nowhere to go")


def test_the_screen_combines_gems_on_one_press() -> None:
    clicked = squeeze(body(code(WIDGET_CPP), "void UCataclysmForgeWidget::HandleActionClicked(", WIDGET_CPP.name))
    branch = clicked[clicked.find("GemCombineActionForButton("):]
    assert clicked.find("GemCombineActionForButton(") != -1, "the screen has no gem combine"
    branch = branch[:branch.find("return;")]
    assert "UCataclysmForgeCraft::CombineGems(" in branch and "PendingSocketAction = " not in branch, (
        "a gem combine is not done on its first press, as a stone combine is")
