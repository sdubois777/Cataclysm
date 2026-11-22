"""The forge's gem combine: two of a kind and rarity make one of the next, no day, capped by the difficulty tier.

WHY THIS EXISTS. Issue #46, slice G6. The coordinating session ruled on 2026-11-21 that two gems combine into one of
the next rarity as stones do (the design's "Gems are upgraded by combining lower-tier gems", and issue #46's "same as
gear"). The owner ruled the same day that it works as gear levelling: "I don't think I want to cap what players can
level their stuff to", so there is no cap by the difficulty tier, and it costs what the stone combine costs, which is
no day, no residue and no gold. `Cataclysm.GemCombine.`
(`game/Source/Cataclysm/Tests/CataclysmGemCombineTests.cpp`) tests what the craft does; this checks its shape:

  1. a combine takes the stone combine's own count, read from it;
  2. no tier and no ceiling enter the rule or the craft;
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


def test_a_gem_combine_takes_the_stone_combines_count_read_from_it() -> None:
    assert "static constexpr int32 GemsPerCombine = StonesPerCombine;" in code(FORGE_H), (
        "the gem combine's count is no longer read from the stone combine's, so the two can drift apart")
    assert constant("StonesPerCombine") == 2, "the stone combine no longer takes two"


def test_no_tier_and_no_ceiling_enter_the_rule_or_the_craft() -> None:
    rule = squeeze(body(code(FORGE_CPP), "FCataclysmForgeGemCombine UCataclysmForge::GemCombineOf(", FORGE_CPP.name))
    craft = squeeze(body(code(CRAFT_CPP), "bool UCataclysmForgeCraft::CombineGems(", CRAFT_CPP.name))
    for where, text in (("GemCombineOf", rule), ("CombineGems", craft)):
        for word in ("DifficultyTier", "Ceiling", "HighestUnpenalisedRarity"):
            assert word not in text, (
                f"{where} reads {word}; the owner ruled on 2026-11-21 that combining is not capped by the tier")


def test_the_craft_passes_no_day_logs_saves_and_puts_back() -> None:
    craft = squeeze(body(code(CRAFT_CPP), "bool UCataclysmForgeCraft::CombineGems(", CRAFT_CPP.name))
    assert craft.find("OutReason = RefusalNow(WorldContext);") != -1, "CombineGems does not refuse during a walk"
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
