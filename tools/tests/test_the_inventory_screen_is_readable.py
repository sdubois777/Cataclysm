"""Every colour the inventory screen draws is readable against its own panel.

WHY THIS EXISTS. Issue #734. The carried inventory drew each cell's label in that
item's own rarity colour, and measuring it found two faults rather than the one
that had been assumed:

- **Ascendant gear, `#A335EE`, measured 3.95:1 against the panel**, below the
  4.5:1 WCAG 2.1 asks for ordinary text. One of the thirteen label colours failed
  outright.
- **An empty cell's frame, `#3A4149`, measured 1.86:1**, below the 3:1 WCAG asks
  for the boundary of a user interface component. That is why the three empty
  rows read as one flat rectangle rather than as 36 places an item could go.

The screen now draws every piece of text in one ink and lets the frame carry the
colour, and the empty cell's frame was raised. This file is what stops either
sliding back, and it checks the whole palette rather than the two that were
wrong, because the next colour to fail will be a different one.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so a check
written in the engine would never run on a pull request. This reads the colours
out of `game/Source/Cataclysm/Interface/CataclysmInventoryScreen.cpp` as text and
out of the two generated tables as data, which is the same arrangement
`tools/tests/test_carried_inventory_is_forty_eight_slots.py` uses and its header
says why.

WHAT THE THRESHOLDS ARE. WCAG 2.1 success criterion 1.4.3 asks 4.5:1 for
ordinary text and 3:1 for large text; 1.4.11 asks 3:1 for the visual boundary of
a user interface component. The design document commits to a colourblind-friendly
palette and to accessibility generally, so these are the published figures the
commitment answers to rather than numbers chosen here.

WHAT THIS DOES NOT CHECK. Whether the screen looks good, which no test can see.
Whether one rarity can be told from the next, which is a difference between two
colours rather than between a colour and the panel, and which the frame's
thickness answers instead.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCREEN_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
              / "CataclysmInventoryScreen.cpp")
GEAR_RARITY = REPO_ROOT / "game" / "Data" / "GearRarity.csv"
MATERIAL_TIERS = REPO_ROOT / "game" / "Data" / "MaterialTiers.csv"

#: WCAG 2.1 success criterion 1.4.3, ordinary text.
TEXT_CONTRAST = 4.5

#: WCAG 2.1 success criterion 1.4.11, a user interface component's boundary.
COMPONENT_CONTRAST = 3.0

#: `const TCHAR* UCataclysmInventoryScreen::PanelHex = TEXT("0A0F12");`
NAMED_COLOUR = re.compile(
    r"const\s+TCHAR\*\s+UCataclysmInventoryScreen::(\w+)\s*=\s*"
    r"TEXT\(\s*\"([0-9A-Fa-f]{6})\"\s*\)\s*;")


def linear(component: float) -> float:
    """One sRGB channel as linear light. WCAG 2.1 relative luminance."""
    return (component / 12.92 if component <= 0.03928
            else ((component + 0.055) / 1.055) ** 2.4)


def luminance(hex_colour: str) -> float:
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b)


def contrast(a: str, b: str) -> float:
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def srgb_hex(linear_value: float) -> str:
    """One linear channel back to a two-digit sRGB hex pair."""
    if linear_value <= 0.0031308:
        encoded = linear_value * 12.92
    else:
        encoded = 1.055 * (linear_value ** (1 / 2.4)) - 0.055
    return f"{max(0, min(255, round(encoded * 255))):02X}"


@pytest.fixture(scope="module")
def named() -> dict[str, str]:
    """The screen's own colours, by the name the C++ gives each one."""
    if not SCREEN_CPP.is_file():
        pytest.fail(
            "game/Source/Cataclysm/Interface/CataclysmInventoryScreen.cpp does "
            "not exist. The inventory screen is issue #731; if the file was "
            "renamed, rename it here too.")

    found = dict(NAMED_COLOUR.findall(SCREEN_CPP.read_text(encoding="utf-8")))
    assert found, (
        "No 'const TCHAR* UCataclysmInventoryScreen::Name = TEXT(\"RRGGBB\");' "
        "lines were found. If the colours changed shape, update NAMED_COLOUR "
        "in this test.")
    return {name: value.upper() for name, value in found.items()}


def colours_from(path: pathlib.Path, key_column: str) -> dict[str, str]:
    """A generated table's Colour column, converted from linear back to sRGB.

    The tables store what the engine uses, which is linear light, because
    `tools/generate_datatables.py` converts the sRGB written in the design
    workbook on the way in. Contrast is defined on sRGB, so it is converted back
    here rather than the workbook being read, which would need openpyxl and
    would not check what the engine actually loads.
    """
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")

    found: dict[str, str] = {}
    with open(path, encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            parts = dict(
                piece.split("=", 1)
                for piece in row.get("Colour", "").strip('"()').split(",")
                if "=" in piece
            )
            if not {"R", "G", "B"} <= parts.keys():
                continue
            found[row[key_column]] = "".join(
                srgb_hex(float(parts[channel])) for channel in ("R", "G", "B"))
    assert found, f"{path.name} has no readable Colour column."
    return found


def test_the_ink_is_readable_on_the_panel(named) -> None:
    """The one colour every piece of text on this screen is drawn in.

    THE WHOLE REASON THE LABELS STOPPED CARRYING THE RARITY. Thirteen text
    colours meant thirteen contrast ratios to keep above the threshold and one
    of them was already under it. One ink is one ratio.
    """
    for name in ("InkHex", "PanelHex"):
        assert name in named, (
            f"UCataclysmInventoryScreen no longer defines {name}. Every piece "
            f"of text on the inventory screen is drawn in the ink, on the "
            f"panel; if either was renamed, rename it here too.")

    ratio = contrast(named["InkHex"], named["PanelHex"])
    assert ratio >= TEXT_CONTRAST, (
        f"The inventory screen's text colour #{named['InkHex']} measures "
        f"{ratio:.2f}:1 against its panel #{named['PanelHex']}. WCAG 2.1 asks "
        f"{TEXT_CONTRAST}:1 for ordinary text, and the design document commits "
        f"to accessibility. Issue #734.")


def test_an_empty_cell_can_be_seen_as_a_cell(named) -> None:
    """The frame around a slot holding nothing.

    WHAT GOES WRONG BELOW THE THRESHOLD. At 1.86:1, which is what it was before
    issue #734, the three empty rows read as one flat rectangle rather than as
    36 places an item could go, which is the whole reason empty cells are drawn.
    """
    ratio = contrast(named["EmptyCellHex"], named["PanelHex"])
    assert ratio >= COMPONENT_CONTRAST, (
        f"An empty cell's frame #{named['EmptyCellHex']} measures {ratio:.2f}:1 "
        f"against the panel #{named['PanelHex']}. WCAG 2.1 asks "
        f"{COMPONENT_CONTRAST}:1 for the boundary of a user interface "
        f"component, and below it the empty slots stop reading as slots. "
        f"Issue #734.")


def test_every_frame_colour_can_be_seen_against_the_panel(named) -> None:
    """All thirteen rungs of the two palettes, not only the two that were wrong.

    A FRAME IS WHAT CARRIES THE RARITY NOW, so a rung too dim to see against the
    panel loses the item's rarity entirely rather than merely being hard to
    read. Checked over the generated tables so that a colour changed in the
    design workbook is checked here without this file being edited.
    """
    panel = named["PanelHex"]

    palettes = {
        "gear rarity": colours_from(GEAR_RARITY, "Rarity"),
        "material tier": colours_from(MATERIAL_TIERS, "TierName"),
    }

    faint = []
    for palette, colours in palettes.items():
        for rung, hexed in colours.items():
            ratio = contrast(hexed, panel)
            if ratio < COMPONENT_CONTRAST:
                faint.append(f"{palette} {rung} #{hexed} at {ratio:.2f}:1")

    assert not faint, (
        f"These frame colours are too faint against the inventory panel "
        f"#{panel}. WCAG 2.1 asks {COMPONENT_CONTRAST}:1 for the boundary of a "
        f"user interface component, and a frame is what carries an item's "
        f"rarity on this screen:\n  " + "\n  ".join(faint))


def test_the_panel_edge_is_quieter_than_the_cells_inside_it(named) -> None:
    """A boundary that competes with the grid is worse than none.

    THE EDGE IS DECORATION AND THE CELLS ARE INFORMATION. The edge only has to
    say where the screen stops; an empty cell has to say that a slot is there,
    which is why that one is held to the WCAG figure and this one is not.
    """
    edge = contrast(named["PanelEdgeHex"], named["PanelHex"])
    cell = contrast(named["EmptyCellHex"], named["PanelHex"])

    assert edge < cell, (
        f"The panel's edge #{named['PanelEdgeHex']} at {edge:.2f}:1 is as loud "
        f"as or louder than an empty cell's frame #{named['EmptyCellHex']} at "
        f"{cell:.2f}:1 against the panel. The edge is decoration and the cells "
        f"are what the player reads.")

    assert edge > 1.0, (
        f"The panel's edge #{named['PanelEdgeHex']} is the same colour as the "
        f"panel, so there is no edge.")
