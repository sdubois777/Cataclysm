"""The eight damage types' effect colours, checked against the rule that
generates them.

WHY THIS EXISTS. Issue #546. The design document fixed eight *environment*
themes and no effect palette, so there was nothing saying what colour a Demonic
skill's impact is as distinct from what a Demonic dungeon's floor is. Those are
not the same decision, and an effect coloured like its own damage type is at its
least readable in the environment that damage type generates.

THE RULE THIS FILE HOLDS, and it is two numbers rather than a table of pairs:

    a world surface may not exceed 30% brightness
    an effect's primary may not fall below 60%

Everything else follows. To clear 3:1 -- the accessibility threshold for a
graphical object that is not text -- against the brightest surface the world is
allowed to have, a colour must reach 60.5% brightness. So checking every effect
against every environment is unnecessary; checking the floor is enough.

THAT IS WHY THE ENVIRONMENT CAP IS LOAD-BEARING. A floor built brighter than 30%
does not look slightly wrong. It silently breaks the readability of every effect
in the game, and without this file nothing would report it.

WHAT IS ASSERTED HERE.

    all eight damage types have an effect palette row
    every primary clears the brightness floor the 3:1 threshold implies
    every secondary is genuinely dark, so it can anchor a bright environment
    no two primaries are confusable with each other
    the attack warning red is not confusable with any of them
    the two numbers the rule rests on are still stated in the document

WHAT IS NOT ASSERTED. That any particular colour is the right one. "Icy blue for
Death" is a judgement by the project owner and this file has no opinion on it.
Only that whatever is chosen stays readable and stays distinct.
"""

from __future__ import annotations

import math
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

SECTION_HEADING = "### **The effect palette, which is not the environment palette**"

#: The brightest a world surface may be, as a percentage. Stated in the design
#: document; repeated here because it is half of the rule this file exists for.
WORLD_BRIGHTNESS_CAP = 30.0

#: The accessibility threshold for a graphical object that is not text.
MINIMUM_CONTRAST = 3.0

#: Below this, two colours are confusable. A perceptual difference of 15 is the
#: usual "clearly a different colour" line.
MINIMUM_SEPARATION = 15.0

#: The attack warning marker's ring, from section XIII. An effect confusable with
#: it would make the player mistake damage for a warning.
TELEGRAPH_RING = "#FF3020"


def channel_to_linear(value: int) -> float:
    c = value / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def to_rgb(colour: str) -> tuple[int, int, int]:
    h = colour.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def luminance(colour: str) -> float:
    r, g, b = (channel_to_linear(c) for c in to_rgb(colour))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def brightness(colour: str) -> float:
    """Perceived lightness, 0 to 100. What a person would call 'how bright'."""
    y = luminance(colour)
    return 116 * (y ** (1 / 3)) - 16 if y > 0.008856 else 903.3 * y


def lab(colour: str) -> tuple[float, float, float]:
    r, g, b = (channel_to_linear(c) for c in to_rgb(colour))
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883

    def f(t: float) -> float:
        return t ** (1 / 3) if t > 0.008856 else (7.787 * t + 16 / 116)

    fx, fy, fz = f(x), f(y), f(z)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))


def separation(a: str, b: str) -> float:
    la, lb = lab(a), lab(b)
    return math.sqrt(sum((la[i] - lb[i]) ** 2 for i in range(3)))


def required_brightness(cap_percent: float) -> float:
    """How bright an effect must be to clear the contrast threshold against a
    surface at the cap. This is the whole derivation, in four lines."""
    cap_luminance = ((cap_percent + 16) / 116) ** 3
    needed = MINIMUM_CONTRAST * (cap_luminance + 0.05) - 0.05
    return 116 * (needed ** (1 / 3)) - 16


@pytest.fixture(scope="module")
def damage_types() -> list[str]:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")
    book = openpyxl.load_workbook(WORKBOOK, read_only=True)
    return [
        str(row[0]).strip().split(".", 1)[1]
        for row in book["Tags"].iter_rows(values_only=True)
        if str(row[0] or "").strip().startswith("Element.")
    ]


@pytest.fixture(scope="module")
def palette() -> dict[str, tuple[str, str]]:
    """Damage type to (primary, secondary), read out of the design document's
    own table rather than restated here."""
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")
    text = DESIGN_DOCUMENT.read_text(encoding="utf-8")
    start = text.index(SECTION_HEADING)
    end = text.index("### **What Chaos looks like**", start)
    section = text[start:end]

    found: dict[str, tuple[str, str]] = {}
    for line in section.splitlines():
        if not line.startswith("| ") or line.startswith("| :"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 3:
            continue
        colours = re.findall(r"#[0-9A-Fa-f]{6}", f"{cells[1]} {cells[2]}")
        if len(colours) == 2:
            found[cells[0]] = (colours[0], colours[1])
    return found


def test_every_damage_type_has_an_effect_palette(damage_types, palette):
    missing = [name for name in damage_types if name not in palette]
    assert not missing, (
        "These damage types have no row in the effect palette table in section "
        f"XIII of {DESIGN_DOCUMENT.name}: {missing}. Found rows for "
        f"{sorted(palette)}."
    )
    assert len(palette) == 8, (
        f"Expected 8 palette rows, parsed {len(palette)}: {sorted(palette)}. "
        "If the table's shape changed, fix this parser rather than deleting "
        "the check."
    )


def test_every_primary_clears_the_brightness_floor(palette):
    """The rule that generates the palette. A primary below this cannot reach
    3:1 against the brightest surface the world is allowed to have, whatever
    hue it is."""
    floor = required_brightness(WORLD_BRIGHTNESS_CAP)
    too_dark = [
        f"{name}: {primary} is {brightness(primary):.1f}%, needs {floor:.1f}%"
        for name, (primary, _) in palette.items()
        if brightness(primary) < floor
    ]
    assert not too_dark, (
        f"Against a world surface capped at {WORLD_BRIGHTNESS_CAP}% brightness, "
        f"an effect must reach {floor:.1f}% to clear {MINIMUM_CONTRAST}:1.\n"
        + "\n".join(too_dark)
    )


def test_every_secondary_is_dark_enough_to_anchor(palette):
    """The secondary's job is to stay readable where the primary matches the
    floor. It can only do that by being much darker than the world's cap."""
    not_dark = [
        f"{name}: {secondary} is {brightness(secondary):.1f}%"
        for name, (_, secondary) in palette.items()
        if brightness(secondary) > WORLD_BRIGHTNESS_CAP
    ]
    assert not not_dark, (
        "These secondaries are not darker than the world's brightness cap of "
        f"{WORLD_BRIGHTNESS_CAP}%, so they cannot anchor an effect against a "
        "bright floor:\n" + "\n".join(not_dark)
    )


def test_no_two_primaries_are_confusable(palette):
    names = sorted(palette)
    confusable = []
    for i, first in enumerate(names):
        for second in names[i + 1:]:
            gap = separation(palette[first][0], palette[second][0])
            if gap < MINIMUM_SEPARATION:
                confusable.append(f"{first} and {second} differ by only {gap:.1f}")
    assert not confusable, (
        f"Two effect colours below {MINIMUM_SEPARATION} apart are confusable in "
        "play:\n" + "\n".join(confusable)
    )


def test_no_effect_is_confusable_with_the_attack_warning(palette):
    """A player mistaking damage for a warning, or a warning for damage, is
    worse than either being hard to see."""
    clashes = [
        f"{name}: {primary} differs from the warning ring by only {gap:.1f}"
        for name, (primary, _) in palette.items()
        if (gap := separation(primary, TELEGRAPH_RING)) < MINIMUM_SEPARATION
    ]
    assert not clashes, (
        f"These effect colours are confusable with the attack warning ring "
        f"{TELEGRAPH_RING}:\n" + "\n".join(clashes)
    )


def test_the_two_numbers_the_rule_rests_on_are_stated(palette):
    """A palette with no stated rule is a list somebody will extend without
    checking. The document has to carry the reasoning, not only the values."""
    # WHITESPACE-INSENSITIVE, because the design document is hard-wrapped and a
    # sentence can span a line break. The first version of this check searched
    # the raw text and failed on a phrase that was present but wrapped, which is
    # a false alarm rather than a real drift.
    text = " ".join(DESIGN_DOCUMENT.read_text(encoding="utf-8").split())
    assert "may not exceed 30% brightness" in text, (
        "Section XIII no longer states the world's brightness cap. Without it "
        "the palette is eight arbitrary colours and nothing explains why a "
        "ninth would have to be bright."
    )
    assert "may not fall below 60%" in text, (
        "Section XIII no longer states the effect brightness floor."
    )
