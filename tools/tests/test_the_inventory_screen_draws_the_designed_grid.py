"""The inventory screen draws the grid the design fixes, with its second channel.

WHY THIS IS CHECKED FROM PYTHON. Issue #731 drew the carried inventory and issue
#735 moved it from the canvas heads-up display into a widget. Continuous
integration compiles no C++ at all, so the automation tests in
`game/Source/Cataclysm/Tests/CataclysmInventoryScreenTests.cpp` never run on a
pull request. Reading the source as text does. That is the same arrangement
`tools/tests/test_carried_inventory_is_forty_eight_slots.py` uses for the
capacity, and its header says why.

WHAT IS CHECKED, AND EACH IS A THING THAT COULD DRIFT SILENTLY.

**The grid's shape is read from the store, not copied.** The design fixes four
rows of twelve, `UCataclysmInventoryComponent` already holds those two numbers,
and a second copy would be a second answer.

**The frame's thickness is the drop names' ladder, not a second one.** The
Interface Colour section of `docs/Cataclysm_GDD_v2.md` requires the frame to
differ by shape as well as by colour, and gives one pixel a rung. A ladder
written again here could drift from the one used on the dungeon floor.

**The requirement itself is still in the design document.** If that sentence is
rewritten, the reason the thickness exists stops holding and this file should be
revisited rather than quietly still passing.

**The widget ships no content asset and builds its own tree.** That was the
project owner's choice on 2026-08-19 over a generated widget Blueprint, so that
every size and colour stays readable in a diff.

**The tree is built in the one place that works.** `NativeConstruct` runs after
Slate has already taken the root, so a tree built there never appears at all, and
nothing reports it.

**A click on the panel is still not a move order.** That guard works and the
project owner has played it; the port changed only what answers the question.

WHAT IS NOT CHECKED HERE. Anything about how it looks, which no test can see;
whether the panel is legible; and whether a real key press arrives. Those were
checked by playing.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
SCREEN_CPP = SOURCE / "Interface" / "CataclysmInventoryScreen.cpp"
SCREEN_HEADER = SOURCE / "Interface" / "CataclysmInventoryScreen.h"
WIDGET_CPP = SOURCE / "Interface" / "CataclysmInventoryWidget.cpp"
WIDGET_HEADER = SOURCE / "Interface" / "CataclysmInventoryWidget.h"
CONTROLLER_CPP = SOURCE / "Player" / "CataclysmPlayerController.cpp"
BUILD_RULES = SOURCE / "Cataclysm.Build.cs"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. The "
            f"inventory screen is issue #731 and its widget is issue #735; if a "
            f"file was renamed, rename it here too.")
    return path.read_text(encoding="utf-8")


def flat(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


def test_the_grid_shape_is_read_from_the_store() -> None:
    """Twelve columns and four rows come from UCataclysmInventoryComponent."""
    for path in (SCREEN_CPP, WIDGET_CPP):
        text = read(path)
        name = path.relative_to(REPO_ROOT).as_posix()

        assert "UCataclysmInventoryComponent::" in text, (
            f"{name} does not read anything from UCataclysmInventoryComponent. "
            f"The design fixes the carried grid at four rows of twelve and the "
            f"store already holds both numbers; a copy is a second answer that "
            f"can disagree with it.")

    assert "UCataclysmInventoryComponent::Columns" in read(SCREEN_CPP)
    assert "UCataclysmInventoryComponent::Rows" in read(SCREEN_CPP)
    assert "UCataclysmInventoryComponent::Columns" in read(WIDGET_CPP), (
        "CataclysmInventoryWidget.cpp does not read "
        "UCataclysmInventoryComponent::Columns, so the row and column a cell "
        "goes into is worked out from a number written here instead.")
    assert "UCataclysmInventoryComponent::SlotCount" in read(WIDGET_CPP), (
        "CataclysmInventoryWidget.cpp does not build "
        "UCataclysmInventoryComponent::SlotCount cells.")


def test_the_grid_writes_no_second_copy_of_its_shape() -> None:
    """No bare 12 or 4 standing in for the grid's shape.

    THE NUMBERS ARE LOOKED FOR ONLY WHERE THEY WOULD BE A SHAPE. A literal 4.0f
    in a layout is ordinary arithmetic; `= 12` or `= 4` assigned to something is
    what a copied grid dimension looks like.
    """
    for path in (SCREEN_CPP, WIDGET_CPP):
        offenders = [
            line.strip()
            for line in read(path).splitlines()
            if re.search(r"(Columns|Rows|SlotCount)\s*=\s*(4|12|48)\b", line)
        ]
        assert not offenders, (
            f"{path.relative_to(REPO_ROOT).as_posix()} assigns the grid's shape "
            f"as a literal rather than reading it from "
            f"UCataclysmInventoryComponent:\n  " + "\n  ".join(offenders))


def test_the_frame_thickness_is_the_drop_names_ladder() -> None:
    text = read(SCREEN_CPP)

    # THE OPEN PARENTHESIS IS REQUIRED, or the first name below also matches
    # every call to the second: NameBorderThicknessFor is a prefix of
    # NameBorderThicknessForMaterialTier, so the plain substring test passed a
    # build where the gear ladder had been replaced by a hand-written one. That
    # is the same trap the drop-name tests hit and it was found here by breaking
    # the guard on purpose.
    for function in ("NameBorderThicknessFor(",
                     "NameBorderThicknessForMaterialTier("):
        assert f"UCataclysmDropPickup::{function}" in text, (
            f"CataclysmInventoryScreen.cpp does not call "
            f"UCataclysmDropPickup::{function}. The border thickness on an "
            f"inventory frame and on a drop's name on the floor are the same "
            f"ladder for the same item, and writing it twice lets the two "
            f"drift. Issue #718 put the ladder there.")


def test_the_frame_colours_come_from_the_two_palettes() -> None:
    """The gear rarity ramp and the material tier ramp, both from their tables.

    A colour written into the screen as a hex value would be a third palette
    that the design document knows nothing about.
    """
    text = read(SCREEN_CPP)

    assert "UCataclysmDropSpawner::ColourFor" in text, (
        "CataclysmInventoryScreen.cpp does not call "
        "UCataclysmDropSpawner::ColourFor, so an inventory frame does not take "
        "its colour from the gear rarity table.")
    assert "UCataclysmDropRoll::MaterialColourFor" in text, (
        "CataclysmInventoryScreen.cpp does not call "
        "UCataclysmDropRoll::MaterialColourFor, so a crafting material's frame "
        "does not take its colour from the material tier table.")


def test_the_design_still_requires_a_second_channel_on_a_frame() -> None:
    """The sentence the thickness answers to.

    If this is ever rewritten, the reason an inventory frame carries a thickness
    at all stops holding, and this file should be revisited rather than quietly
    still passing.
    """
    text = flat(GDD)

    assert ("the frame and the drop marker must differ by shape or motion as "
            "well as by colour") in text, (
        "The Interface Colour section of docs/Cataclysm_GDD_v2.md no longer "
        "requires a frame to differ by shape or motion as well as by colour. "
        "That requirement is why an inventory frame's thickness is its rarity.")

    assert "inventory frames" in text, (
        "docs/Cataclysm_GDD_v2.md no longer names inventory frames as a "
        "surface the gear rarity colours appear on. That sentence is why the "
        "screen colours a frame at all.")


def test_the_module_depends_on_all_three_widget_modules() -> None:
    """UMG alone does not compile, and the failure is confusing when it happens.

    UMG lists Slate and SlateCore as private dependencies of itself, so neither
    propagates, while UMG's own public headers include SlateCore headers. Adding
    only UMG produces an error inside an engine header, which reads like an
    engine fault rather than a missing dependency.
    """
    text = read(BUILD_RULES)

    for module in ("UMG", "Slate", "SlateCore"):
        assert f'"{module}"' in text, (
            f"game/Source/Cataclysm/Cataclysm.Build.cs does not depend on "
            f"{module}. The inventory screen is a UUserWidget and needs all "
            f"three; see the comment beside them for why UMG alone is not "
            f"enough. Issue #735.")


def test_the_widget_builds_its_own_tree_and_ships_no_asset() -> None:
    """No widget Blueprint, and no image asset behind the frames.

    The project owner chose a C++ tree over a generated asset on 2026-08-19, so
    that every size and colour stays readable in a diff. A tree that started
    loading assets would give that up without anything saying so.
    """
    text = read(WIDGET_CPP)

    assert "WidgetTree->ConstructWidget" in text, (
        "CataclysmInventoryWidget.cpp does not construct any widget into its "
        "own WidgetTree, so it is no longer a screen built in C++.")

    # THE ASSIGNMENT, NOT THE NAME. Looking for `WidgetTree->RootWidget`
    # anywhere passed a build where the assignment had been deleted, because
    # RebuildWidget still reads the same member to decide whether to build.
    # Found by breaking this guard on purpose.
    assert "WidgetTree->RootWidget = " in text, (
        "CataclysmInventoryWidget.cpp never assigns WidgetTree->RootWidget. "
        "UUserWidget::RebuildWidget falls back to an empty SSpacer when it is "
        "unset, so the screen would open and show nothing at all.")

    # A HOLLOW FRAME WITH NO IMAGE. FSlateColorBrush fills a rectangle with a
    # colour and no texture, which is what lets a frame of any thickness be
    # drawn as one filled rectangle inside another.
    assert "FSlateColorBrush" in text, (
        "CataclysmInventoryWidget.cpp no longer uses FSlateColorBrush. That is "
        "what fills a rectangle with a colour and no texture; anything else "
        "needs an image asset, which this screen deliberately does not have.")

    for loader in ("FObjectFinder", "LoadObject<UTexture",
                   "SetBrushFromTexture", "SetBrushFromMaterial",
                   "SetBrushFromAsset"):
        assert loader not in text, (
            f"CataclysmInventoryWidget.cpp uses {loader}, so the screen now "
            f"needs a content asset. The project owner chose a C++ tree with no "
            f"asset on 2026-08-19; if that changed, this test should change "
            f"with it rather than be deleted.")


def test_the_tree_is_built_where_it_actually_appears() -> None:
    """In RebuildWidget, and not in NativeConstruct.

    NativeConstruct runs after Slate has already taken the widget's root, so a
    tree built there never appears and nothing reports it. That trap is recorded
    on `game/Source/Cataclysm/Interface/CataclysmCombatOverlay.h` and it is the
    reason the canvas was chosen in the first place.
    """
    text = read(WIDGET_CPP)

    assert "::RebuildWidget()" in text, (
        "CataclysmInventoryWidget.cpp does not override RebuildWidget, which is "
        "the only place a C++ widget tree can be built and still appear.")

    assert "NativeConstruct" not in text, (
        "CataclysmInventoryWidget.cpp mentions NativeConstruct. That runs after "
        "Slate has taken the root, so a tree built there never appears on "
        "screen and nothing says so. Build it in RebuildWidget.")


def test_nothing_in_the_screen_is_hit_testable() -> None:
    """The premise the click guard rests on.

    If Slate could consume the click instead, that would be a reasonable answer
    too, but the two mechanisms disagree about which one is running and neither
    can be tested here. The guard the project owner has played is the one kept.
    """
    text = read(WIDGET_CPP)

    assert "ESlateVisibility::HitTestInvisible" in text, (
        "CataclysmInventoryWidget.cpp does not set its visibility to "
        "HitTestInvisible. Some of the screen would then consume mouse events "
        "and some would not, and which is which decides whether a click reaches "
        "the controller's guard at all. Issue #735.")


def test_a_click_on_the_panel_is_not_a_move_order() -> None:
    """The bug the screen would otherwise introduce.

    The left mouse button orders a move and the cursor trace passes behind
    anything drawn over the world, so without this a click on a grid cell sends
    the character walking to whatever floor lies behind the panel.
    """
    controller = read(CONTROLLER_CPP)

    # THE CALL, NOT THE NAME. Looking only for `CursorIsOverInterface` anywhere
    # in the file passed a build where the press handler had stopped calling it,
    # because the function's own definition still carried the name. Found by
    # breaking this guard on purpose.
    assert "bPressBeganOnInterface = CursorIsOverInterface();" in controller, (
        "ACataclysmPlayerController::Input_MoveToCursorStarted no longer asks "
        "whether the cursor is over an open screen. Without that, clicking a "
        "grid cell orders the character to walk to the floor behind the panel. "
        "Issue #731.")

    # AND THE OTHER TWO HANDLERS HAVE TO HONOUR WHAT IT DECIDED, or the press is
    # ignored and the release still issues the order.
    for handler in ("Input_MoveToCursorHeld", "Input_MoveToCursorReleased"):
        body = controller.split(f"::{handler}()", 1)
        assert len(body) == 2, (
            f"CataclysmPlayerController.cpp has no {handler}. If it was "
            f"renamed, rename it here too.")
        assert "bPressBeganOnInterface" in body[1].split("\n}\n", 1)[0], (
            f"{handler} does not consult bPressBeganOnInterface, so a press "
            f"that began on the inventory panel still moves the character.")

    assert "CursorIsOverPanel" in controller, (
        "CataclysmPlayerController.cpp no longer asks the inventory widget "
        "whether the cursor is over its panel, so its move-order guard cannot "
        "know where the panel is.")

    widget = read(WIDGET_CPP)
    assert "IsUnderLocation" in widget and "ScreenToWidgetAbsolute" in widget, (
        "CataclysmInventoryWidget.cpp no longer converts the mouse position "
        "into Slate's coordinates and tests it against the panel's geometry. "
        "The mouse is reported in viewport pixels and a widget's geometry is "
        "absolute, and the two differ by the interface's scale.")


def test_the_screen_is_opened_and_closed_by_the_key() -> None:
    """The two ends of the chain: a key toggles it and it reaches the viewport."""
    controller = read(CONTROLLER_CPP)

    assert "Names::ToggleInventory" in controller, (
        "CataclysmPlayerController.cpp does not bind the ToggleInventory "
        "action, so nothing opens or closes the carried inventory.")

    assert "AddToViewport" in controller, (
        "CataclysmPlayerController.cpp never adds the inventory screen to the "
        "viewport, so the key is bound to a handler that draws nothing.")
    assert "RemoveFromParent" in controller, (
        "CataclysmPlayerController.cpp never removes the inventory screen from "
        "the viewport, so the key opens it and cannot close it again.")


def test_every_judgement_the_screen_makes_is_outside_the_widget() -> None:
    """The screen's decisions are statics, so they can be tested at all.

    THE LIST IS THE POINT. Each of these is a judgement that can be wrong, and
    each one moved into the widget would become untestable without anything
    reporting a loss of coverage: the automation command runs with -nullrhi and
    nothing that reaches the screen can be watched.

    IT IS SHORTER THAN IT WAS BEFORE THE PORT, and that is the port working
    rather than cover being lost. Where the panel sat, where each cell sat, how a
    label broke into lines and whether a point was over the panel were arithmetic
    the canvas needed; Slate does all four, so there is nothing left to judge.
    """
    header = read(SCREEN_HEADER)

    for judgement in ("CellSizeFor", "LabelFontSizeFor", "LabelFor",
                      "QuantityTextFor", "ColourFor", "BorderThicknessFor",
                      "HeaderTextFor"):
        assert re.search(rf"\b{judgement}\s*\(", header), (
            f"UCataclysmInventoryScreen no longer declares {judgement}. Every "
            f"judgement the inventory screen makes lives outside the widget so "
            f"that it can be tested; one moved into the widget is one nothing "
            f"can check, and the automation run would not say so.")

    widget = read(WIDGET_HEADER) + read(WIDGET_CPP)
    assert "UCataclysmInventoryScreen" in widget, (
        "CataclysmInventoryWidget no longer refers to "
        "UCataclysmInventoryScreen at all, so it is deciding for itself what "
        "each cell says and how big it is.")
