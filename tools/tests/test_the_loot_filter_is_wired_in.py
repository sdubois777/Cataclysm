"""The loot filter is what the floor's names are drawn through, and Alt is what shows what it hides.

WHY THIS EXISTS. Issue #2846, slice LF1. `UCataclysmLootFilter` decides which drops are named and how, and the
automation tests `Cataclysm.LootFilter.` hold every rule. But the heads-up display draws inside a draw call, which
never runs under -nullrhi (#650), and no test presses a key. So nothing that runs can see that the display asks the
filter, or that Alt sets the flag it reads. This reads the source for those calls.

WHAT IS CHECKED:

  1. `ACataclysmHUD::DrawDropNames` passes the names in range through `UCataclysmLootFilter::VisibleDrops`, with the
     game instance's active filter, the character's context and the controller's reveal flag; it measures and draws
     each name at its verdict's scale, and draws the border in the drop's own colour, never the restyle's (the owner's
     ruling O2 keeps the border the rarity marker, #718);
  2. the controller binds the reveal action's start to one handler and its end and cancel to the other, and the two
     set and clear the flag `IsRevealingHiddenDrops` reads;
  3. the game instance starts with the owner's default, "Hide worse than worn";
  4. the reveal action is on Left Alt in both control schemes.
"""

from __future__ import annotations

import ast
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`, whitespace collapsed."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return flat(text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


def test_the_display_draws_the_names_through_the_filter() -> None:
    draw = body(code(SOURCE / "Interface" / "CataclysmHUD.cpp"), "void ACataclysmHUD::DrawDropNames(")
    assert "UCataclysmLootFilter::VisibleDrops(InRange, UCataclysmGameInstance::ActiveLootFilterIn(this), " \
           "UCataclysmLootFilter::ContextOf(LocalPawn), bReveal," in draw
    assert "Controller->IsRevealingHiddenDrops()" in draw
    assert "MeasureTextCentred(Drop->DisplayName, Screen.X, Screen.Y, Verdict.Scale)" in draw
    assert "Tag.Min.X + Inset, Tag.Min.Y + Inset, Verdict.Scale)" in draw
    assert "FLinearColor Border = Drop->NameColour;" in draw
    assert "DrawBorder(Tag, static_cast<float>(Thickness), Border);" in draw
    # THE NAMES ARE TAKEN FROM WHAT THE FILTER LEFT, not from the drops in range: the loop reads `Nearby`, which
    # VisibleDrops fills.
    assert "ACataclysmDroppedItem* Drop = Nearby[Found];" in draw
    assert "UCataclysmDropPickup::DropsToName(World, LocalPawn->GetActorLocation(), InRange);" in draw


def test_alt_held_and_let_go_sets_and_clears_the_reveal_flag() -> None:
    controller = code(SOURCE / "Player" / "CataclysmPlayerController.cpp")
    setup = body(controller, "void ACataclysmPlayerController::SetupInputComponent(")
    for event, handler in (("Started", "Input_RevealDropsStarted"), ("Completed", "Input_RevealDropsReleased"),
                           ("Canceled", "Input_RevealDropsReleased")):
        assert (f"BindNativeAction(Config, Names::RevealDrops, ETriggerEvent::{event}, this, "
                f"&ACataclysmPlayerController::{handler});") in setup, event
    assert "bRevealHiddenDrops = true;" in body(controller, "void ACataclysmPlayerController::Input_RevealDropsStarted(")
    assert "bRevealHiddenDrops = false;" in body(controller,
                                                 "void ACataclysmPlayerController::Input_RevealDropsReleased(")
    header = flat(code(SOURCE / "Player" / "CataclysmPlayerController.h"))
    assert "bool IsRevealingHiddenDrops() const { return bRevealHiddenDrops; }" in header


def test_a_new_game_starts_with_the_owners_default() -> None:
    header = flat(code(SOURCE / "Player" / "CataclysmGameInstance.h"))
    assert "FCataclysmLootFilter ActiveLootFilter = UCataclysmLootFilter::HideWorseThanWorn();" in header
    filter_code = code(SOURCE / "Items" / "CataclysmLootFilter.cpp")
    assert 'UCataclysmLootFilter::DefaultFilterName = TEXT("Hide worse than worn");' in flat(filter_code)
    assert "Filter.Name = DefaultFilterName;" in body(filter_code, "FCataclysmLootFilter UCataclysmLootFilter::HideWorseThanWorn(")


def test_the_reveal_action_is_on_left_alt_in_both_schemes() -> None:
    tree = ast.parse((REPO_ROOT / "tools" / "generate_input_assets.py").read_text(encoding="utf-8"))
    lists = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            if node.targets[0].id in ("MOUSE_MAPPINGS", "KEYBOARD_MAPPINGS"):
                lists[node.targets[0].id] = ast.literal_eval(node.value)
    assert set(lists) == {"MOUSE_MAPPINGS", "KEYBOARD_MAPPINGS"}
    for name, mappings in lists.items():
        assert ("IA_RevealDrops", "LeftAlt", []) in mappings, name
