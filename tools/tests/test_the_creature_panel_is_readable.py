"""Every colour the creature panel draws stays readable over live gameplay.

WHY THIS EXISTS. Issue #740. The panel describing the creature under the cursor
sits at the top of the screen while the fight is still happening, so unlike the
inventory screen it is read against the world rather than instead of it. That
makes the measurement different in one specific way: the panel is not opaque, so
what sits behind the text is the panel colour composited over whatever the player
is standing on.

WHAT IT IS COMPOSITED OVER, AND WHY THAT NUMBER IS NOT INVENTED HERE. The design
document states the rule that generates the whole readability guarantee: **"A
world surface may not exceed 30% brightness."** So the worst backdrop the panel
can ever have is a surface at exactly that cap, and measuring against it covers
every environment in the game without checking eight of them one at a time. That
is the same derivation `tools/tests/test_effect_palette_stays_readable.py` uses,
and brightness means CIE L* there and here.

WHAT THE MEASUREMENT FOUND, which is worth stating because it decided the design:
**the panel's own fill cannot be seen against a bright floor at all.** A
near-black panel over a surface at the cap measures about 1.9:1 whatever its
opacity, because the panel has to stay near-black to keep the text readable. So
the panel is a shape only because of its edge, and the edge is the one colour
here that has to clear 3:1 against both sides of itself -- the panel within, and
the brightest floor without.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so a
check written in the engine would never run on a pull request. This reads the
colours out of `game/Source/Cataclysm/Interface/CataclysmCreaturePanel.cpp` as
text, which is the same arrangement
`tools/tests/test_the_inventory_screen_is_readable.py` uses.

WHAT THIS DOES NOT CHECK. Whether the panel looks good, which no test can see,
and whether it is in the right place, which only playing settles.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
INTERFACE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
PANEL_CPP = INTERFACE / "CataclysmCreaturePanel.cpp"
PANEL_H = INTERFACE / "CataclysmCreaturePanel.h"
OVERLAY_CPP = INTERFACE / "CataclysmCombatOverlay.cpp"
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: WCAG 2.1 success criterion 1.4.3, ordinary text.
TEXT_CONTRAST = 4.5

#: WCAG 2.1 success criterion 1.4.11, a user interface component's boundary,
#: and 1.4.11 again for a graphical object such as the filled part of a bar.
COMPONENT_CONTRAST = 3.0

#: The brightest a world surface may be, as CIE L*. Stated in the design
#: document under "The one rule that generates the rest"; repeated here because
#: it is what every figure below is measured against.
WORLD_BRIGHTNESS_CAP = 30.0

#: `const TCHAR* UCataclysmCreaturePanel::PanelHex = TEXT("0A0F12");`
NAMED_COLOUR = re.compile(
    r"const\s+TCHAR\*\s+UCataclysm(?:CreaturePanel|CombatOverlay)::(\w+)\s*=\s*"
    r"TEXT\(\s*\"([0-9A-Fa-f]{6})\"\s*\)\s*;")

#: `static constexpr float PanelOpacity = 0.90f;`
PANEL_OPACITY = re.compile(
    r"static\s+constexpr\s+float\s+PanelOpacity\s*=\s*([\d.]+)f\s*;")


def to_linear(component: float) -> float:
    """One sRGB channel as linear light. WCAG 2.1 relative luminance."""
    return (component / 12.92 if component <= 0.03928
            else ((component + 0.055) / 1.055) ** 2.4)


def luminance(hex_colour: str) -> float:
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    return 0.2126 * to_linear(r) + 0.7152 * to_linear(g) + 0.0722 * to_linear(b)


def contrast(a: float, b: float) -> float:
    """Between two relative luminances rather than two colours, because one side
    of every comparison here is a composite rather than a colour anybody wrote."""
    return (max(a, b) + 0.05) / (min(a, b) + 0.05)


def brightest_world() -> float:
    """The relative luminance of a surface at the design's brightness cap.

    The cap is stated as a percentage of CIE L*, which is perceived lightness,
    so it is converted rather than used directly: L* 30 is about 6.2% luminance,
    not 30%.
    """
    return ((WORLD_BRIGHTNESS_CAP + 16) / 116) ** 3


@pytest.fixture(scope="module")
def named() -> dict[str, str]:
    """The panel's colours, and the health fill it borrows, by C++ name."""
    for path in (PANEL_CPP, OVERLAY_CPP):
        if not path.is_file():
            pytest.fail(
                f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. The "
                f"creature panel is issue #740; if a file was renamed, rename "
                f"it here too.")

    found: dict[str, str] = {}
    for path in (PANEL_CPP, OVERLAY_CPP):
        found.update(dict(
            NAMED_COLOUR.findall(path.read_text(encoding="utf-8"))))

    assert found, (
        "No 'const TCHAR* UCataclysmCreaturePanel::Name = TEXT(\"RRGGBB\");' "
        "lines were found. If the colours changed shape, update NAMED_COLOUR "
        "in this test.")
    return {name: value.upper() for name, value in found.items()}


@pytest.fixture(scope="module")
def opacity() -> float:
    """How much of the world the panel hides, read out of the header."""
    if not PANEL_H.is_file():
        pytest.fail("CataclysmCreaturePanel.h does not exist.")

    found = PANEL_OPACITY.search(PANEL_H.read_text(encoding="utf-8"))
    assert found, (
        "UCataclysmCreaturePanel has no PanelOpacity. It is how much of the "
        "world the panel hides, and every figure in this file depends on it.")

    value = float(found.group(1))
    assert 0.0 < value <= 1.0, (
        f"PanelOpacity is {value}, which is not a share between 0 and 1.")
    return value


@pytest.fixture(scope="module")
def behind(named, opacity) -> float:
    """What actually sits behind the panel's text, in the worst case.

    THE PANEL COMPOSITED OVER THE BRIGHTEST FLOOR THE DESIGN ALLOWS. Blended in
    linear light, which is where relative luminance is defined and where a
    physically correct blend happens.
    """
    assert "PanelHex" in named, (
        "UCataclysmCreaturePanel no longer defines PanelHex.")
    return (opacity * luminance(named["PanelHex"])
            + (1.0 - opacity) * brightest_world())


def test_the_ink_is_readable_on_the_panel(named, behind) -> None:
    """The one colour every piece of text on the panel is drawn in.

    ONE INK IS ONE RATIO TO KEEP ABOVE THE THRESHOLD. That is the answer issue
    #734 reached for the inventory screen after thirteen text colours turned out
    to include one that failed outright.
    """
    assert "InkHex" in named, (
        "UCataclysmCreaturePanel no longer defines InkHex. Every piece of text "
        "on the panel is drawn in it; if it was renamed, rename it here too.")

    ratio = contrast(luminance(named["InkHex"]), behind)
    assert ratio >= TEXT_CONTRAST, (
        f"The creature panel's text colour #{named['InkHex']} measures "
        f"{ratio:.2f}:1 against its own panel over the brightest world surface "
        f"the design allows. WCAG 2.1 asks {TEXT_CONTRAST}:1 for ordinary text, "
        f"and the design document commits to accessibility. Issue #740.")


def test_the_edge_can_be_seen_against_both_sides_of_itself(named, behind) -> None:
    """The edge is the only thing that makes the panel a shape.

    WHY BOTH SIDES. The panel's fill is near-black so the text stays readable,
    and a near-black panel against a floor at the design's brightness cap
    measures about 1.9:1 -- see the test below, which states that outright. So
    the boundary is carried entirely by the edge, and an edge that reads against
    the panel but vanishes against a pale stone floor would leave the panel with
    no visible extent exactly where it was needed.
    """
    assert "EdgeHex" in named, (
        "UCataclysmCreaturePanel no longer defines EdgeHex.")

    edge = luminance(named["EdgeHex"])

    inside = contrast(edge, behind)
    assert inside >= COMPONENT_CONTRAST, (
        f"The creature panel's edge #{named['EdgeHex']} measures {inside:.2f}:1 "
        f"against the panel it surrounds. WCAG 2.1 asks {COMPONENT_CONTRAST}:1 "
        f"for the boundary of a user interface component. Issue #740.")

    outside = contrast(edge, brightest_world())
    assert outside >= COMPONENT_CONTRAST, (
        f"The creature panel's edge #{named['EdgeHex']} measures "
        f"{outside:.2f}:1 against the brightest world surface the design allows "
        f"({WORLD_BRIGHTNESS_CAP}% brightness). Below the threshold the panel "
        f"has no visible boundary on a pale floor, and its own fill cannot "
        f"supply one. Issue #740.")


def test_the_filled_part_of_the_health_bar_reads_against_the_panel(
        named, behind) -> None:
    """The bar has an outline and no track, and this is why.

    THERE IS NO GREY THAT WORKS AS A TRACK. To clear 3:1 against this panel a
    track has to reach about 14% relative luminance, and the health fill is
    already at 14.3%, so the filled part and the empty part would measure about
    1:1 against each other and read as one flat block. The bar is drawn instead
    as red against the panel itself, which is the pair measured here, inside an
    outline that shows how far the bar extends.
    """
    assert "HealthFillHex" in named, (
        "UCataclysmCombatOverlay no longer defines HealthFillHex, which is the "
        "colour the panel's health bar is filled with.")

    ratio = contrast(luminance(named["HealthFillHex"]), behind)
    assert ratio >= COMPONENT_CONTRAST, (
        f"The health bar's fill #{named['HealthFillHex']} measures "
        f"{ratio:.2f}:1 against the panel behind it. WCAG 2.1 asks "
        f"{COMPONENT_CONTRAST}:1 for a graphical object, and below it the "
        f"filled part of the bar cannot be told from the empty part. Issue "
        f"#740.")


def test_the_panel_fill_alone_cannot_carry_the_boundary(named, behind) -> None:
    """The measurement that decided the design, kept so it cannot be forgotten.

    THIS IS NOT A FAILURE. It is a fact about a near-black panel over a world
    the design caps at 30% brightness, and it is written down as a test so that
    somebody who later deletes the edge as decoration finds out here rather than
    in play. If this ever stops holding -- because the panel colour was made
    much lighter, or the world cap was lowered -- then the edge's job has
    changed and the reasoning on UCataclysmCreaturePanel::EdgeHex needs
    rewriting rather than the number here being nudged.
    """
    ratio = contrast(behind, brightest_world())
    assert ratio < COMPONENT_CONTRAST, (
        f"The creature panel's fill now measures {ratio:.2f}:1 against the "
        f"brightest world surface the design allows, which is above the "
        f"{COMPONENT_CONTRAST}:1 threshold. That is better than when this was "
        f"written, but it means the reasoning on "
        f"UCataclysmCreaturePanel::EdgeHex -- that the edge is the only thing "
        f"making the panel a shape -- is now wrong. Rewrite it rather than "
        f"changing this number.")


def test_the_world_brightness_cap_is_still_the_design_document_s(
) -> None:
    """Every figure above is measured against a number stated in docs/.

    IF THE CAP MOVES, EVERY MEASUREMENT HERE MOVES WITH IT. A floor built
    brighter than the cap does not look slightly wrong; it silently changes what
    "readable" means for this panel and for every effect in the game.
    """
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip("Cataclysm_GDD_v2.md is not present")

    text = " ".join(DESIGN_DOCUMENT.read_text(encoding="utf-8").split())
    expected = f"A world surface may not exceed {WORLD_BRIGHTNESS_CAP:.0f}% brightness"

    assert expected in text, (
        f"docs/Cataclysm_GDD_v2.md no longer states {expected!r}. That cap is "
        f"what every contrast figure in this file is measured against; if it "
        f"changed, change WORLD_BRIGHTNESS_CAP here and re-measure the panel's "
        f"colours rather than leaving them.")
