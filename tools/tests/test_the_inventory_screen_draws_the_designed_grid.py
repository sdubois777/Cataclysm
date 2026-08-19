"""The inventory screen draws the grid the design fixes, with its second channel.

WHY THIS IS CHECKED FROM PYTHON. Issue #731 drew the carried inventory on the
canvas heads-up display. Continuous integration compiles no C++ at all, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmInventoryScreenTests.cpp`
never run on a pull request. Reading the source as text does. That is the same
arrangement `tools/tests/test_carried_inventory_is_forty_eight_slots.py` uses for
the capacity, and its header says why.

WHAT IS CHECKED, AND EACH IS A THING THAT COULD DRIFT SILENTLY.

**The grid's shape is read from the store, not copied.** The design fixes four
rows of twelve. `UCataclysmInventoryComponent` already holds those two numbers,
and a second copy in the screen would be a second answer that could disagree
after a change to the first.

**The frame's thickness is the drop names' ladder, not a second one.** The
Interface Colour section of `docs/Cataclysm_GDD_v2.md` requires the frame to
differ by shape as well as by colour, and gives one pixel a rung. A ladder
written again in the screen could drift from the one on the floor, and the two
would then say different things about the same item.

**The requirement itself is still in the design document.** If that sentence is
ever rewritten, the reason the thickness exists stops holding and this should be
revisited rather than quietly still passing.

**The key reaches the screen.** The controller binds ToggleInventory and the
draw runs.

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
HUD_CPP = SOURCE / "Interface" / "CataclysmHUD.cpp"
CONTROLLER_CPP = SOURCE / "Player" / "CataclysmPlayerController.cpp"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. The "
            f"inventory screen is issue #731; if a file was renamed, rename it "
            f"here too.")
    return path.read_text(encoding="utf-8")


def flat(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


def test_the_grid_shape_is_read_from_the_store() -> None:
    """Twelve columns and four rows come from UCataclysmInventoryComponent."""
    text = read(SCREEN_CPP)

    for constant in ("Columns", "Rows"):
        assert f"UCataclysmInventoryComponent::{constant}" in text, (
            f"game/Source/Cataclysm/Interface/CataclysmInventoryScreen.cpp does "
            f"not read UCataclysmInventoryComponent::{constant}. The design "
            f"fixes the carried grid at four rows of twelve and the store "
            f"already holds both numbers; a second copy here is a second answer "
            f"that can disagree with it.")


def test_the_grid_writes_no_second_copy_of_its_shape() -> None:
    """No bare 12 or 4 standing in for the grid's shape.

    THE NUMBERS ARE LOOKED FOR ONLY WHERE THEY WOULD BE A SHAPE. A literal 4.0f
    in a layout is ordinary arithmetic; `= 12` or `= 4` assigned to something is
    what a copied grid dimension looks like.
    """
    text = read(SCREEN_CPP)

    offenders = [
        line.strip()
        for line in text.splitlines()
        if re.search(r"(Columns|Rows)\s*=\s*(4|12)\b", line)
    ]
    assert not offenders, (
        "CataclysmInventoryScreen.cpp assigns the grid's shape as a literal "
        "rather than reading it from UCataclysmInventoryComponent:\n  "
        + "\n  ".join(offenders))


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


def test_the_screen_is_drawn_and_the_key_is_bound() -> None:
    """The two ends of the chain: a key toggles it and the draw runs.

    Neither can be tested any other way on a pull request. AHUD::PostRender
    checks FApp::CanEverRender() and the automation command passes -nullrhi, so
    DrawHUD does not run under test at all.
    """
    hud = read(HUD_CPP)
    controller = read(CONTROLLER_CPP)

    assert "DrawInventory();" in hud, (
        "CataclysmHUD.cpp never calls DrawInventory, so the carried inventory "
        "is never drawn however the key is bound.")

    assert "Names::ToggleInventory" in controller, (
        "CataclysmPlayerController.cpp does not bind the ToggleInventory "
        "action, so nothing opens or closes the carried inventory.")
    assert "ToggleInventory();" in controller, (
        "CataclysmPlayerController.cpp never calls ACataclysmHUD::"
        "ToggleInventory, so the key is bound to a handler that does nothing.")


def test_a_click_on_the_panel_is_not_a_move_order() -> None:
    """The bug the screen would otherwise introduce.

    The left mouse button orders a move and the cursor ray passes through
    anything drawn on the canvas, so without this a click on a grid cell sends
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

    assert "InventoryCoversPoint" in controller, (
        "CataclysmPlayerController.cpp no longer asks the heads-up display "
        "whether the inventory panel covers a point, so its move-order guard "
        "cannot know where the panel is.")


def test_every_judgement_the_screen_makes_is_outside_the_draw() -> None:
    """The screen's decisions are statics, so they can be tested at all.

    THE LIST IS THE POINT. Each of these is a judgement that can be wrong, and
    each one moved into ACataclysmHUD::DrawInventory would become untestable
    without anything reporting a loss of coverage: the automation command runs
    with -nullrhi and DrawHUD never runs.
    """
    header = read(SCREEN_HEADER)

    for judgement in ("CellSizeFor", "PanelRectFor", "CellRectFor",
                      "PanelCoversPoint", "LabelLinesFor", "LabelFor",
                      "QuantityTextFor", "ColourFor", "BorderThicknessFor",
                      "HeaderTextFor", "LabelScaleFor"):
        assert re.search(rf"\b{judgement}\s*\(", header), (
            f"UCataclysmInventoryScreen no longer declares {judgement}. Every "
            f"judgement the inventory screen makes lives outside the draw so "
            f"that it can be tested; one moved into DrawHUD is one nothing can "
            f"check, and the automation run would not say so.")
