"""The five crafting material colours, and the properties they were chosen for.

WHY THIS EXISTS. A drop on the ground is shown as its name, drawn in a colour.
Crafting materials drop at twice the gear rate and no colour was designed for any
of them until 2026-08-19, so nothing could put a material's name on the floor.
Issue #717.

WHAT IS GUARDED, AND IT IS NOT THE FIVE VALUES. Any of them may be retuned. What
may not quietly stop being true is why they were chosen:

- **They are their own hue family, away from the gear ramp.** A material's name
  and a gear item's name lie on the same dungeon floor, so a material sharing a
  hue with a rarity would be two things a player could not separate at a glance.
- **They brighten at every rung**, so the ladder reads with no colour vision at
  all. That is the same reason the names carry a border thickness.
- **The design document and the generated table say the same thing**, because the
  workbook is authoritative and `game/Data/MaterialTiers.csv` is generated from
  it.

WHY THE ARITHMETIC IS DONE HERE RATHER THAN READ OFF A LIST. Hue and luminance
are computed from the values themselves, so a colour changed in the workbook is
checked against the property rather than against a copy of the old answer. A test
that listed the five hexes would pass any change that kept the list in step and
would say nothing about whether the palette still worked.
"""

from __future__ import annotations

import colorsys
import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
MATERIAL_CSV = REPO_ROOT / "game" / "Data" / "MaterialTiers.csv"

#: The eight gear rarity colours, read out of the design document rather than
#: repeated here, so this cannot drift from the ramp it is measured against.
GEAR_TABLE = re.compile(
    r"^\| (?P<rarity>\w+) \| \w+ \| `#(?P<hex>[0-9A-Fa-f]{6})` \|$",
    re.MULTILINE)

MATERIAL_TABLE = re.compile(
    r"^\| (?P<tier>\d) \| (?P<name>[\w ]+?) \| `#(?P<hex>[0-9A-Fa-f]{6})` \|$",
    re.MULTILINE)

#: How far a material hue must stay from every gear hue, in degrees. Twenty is
#: wide enough that two colours read as different families rather than as two
#: shades of one, and far below the 36 the chosen palette actually achieves, so
#: this is a floor rather than a restatement.
MINIMUM_HUE_GAP = 20.0


def channels(hex_text: str) -> tuple[float, float, float]:
    return tuple(int(hex_text[i:i + 2], 16) / 255 for i in (0, 2, 4))


def hue_degrees(hex_text: str) -> float:
    return colorsys.rgb_to_hsv(*channels(hex_text))[0] * 360.0


def saturation(hex_text: str) -> float:
    return colorsys.rgb_to_hsv(*channels(hex_text))[1]


def relative_luminance(hex_text: str) -> float:
    """The WCAG measure, which is what "brighter" means to an eye."""
    def linear(channel: float) -> float:
        return (channel / 12.92 if channel <= 0.04045
                else ((channel + 0.055) / 1.055) ** 2.4)

    red, green, blue = (linear(c) for c in channels(hex_text))
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def hue_gap(one: str, other: str) -> float:
    """Degrees between two hues the short way round the wheel."""
    straight = abs(hue_degrees(one) - hue_degrees(other))
    return min(straight, 360.0 - straight)


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def material_colours(document) -> dict[int, tuple[str, str]]:
    """Tier number to its name and hex, from the design document."""
    block = re.search(
        r"### \*\*The crafting material ladder\*\*(?P<body>.*?)\n### ",
        document, re.DOTALL)
    assert block, (
        "docs/Cataclysm_GDD_v2.md has no 'The crafting material ladder' "
        "section. That is where the five material colours are stated, decided "
        "on 2026-08-19 under issue #717.")

    found = {int(m.group("tier")): (m.group("name").strip(), m.group("hex"))
             for m in MATERIAL_TABLE.finditer(block.group("body"))}
    assert found, "the material ladder section has no colour table"
    return found


@pytest.fixture(scope="module")
def gear_colours(document) -> dict[str, str]:
    """Rarity to hex, from the design document's own table.

    SCOPED TO THE GEAR SECTION. The material table below it has the same three
    columns, and a tier number matches the same pattern a rarity name does, so
    an unscoped search reported tier 1 as a rarity called "1" and then found it
    zero degrees from itself. That was the first run of this file.
    """
    block = re.search(
        r"\*\*The eight rarity colours, in tier order:\*\*(?P<body>.*?)\n\n\*\*",
        document, re.DOTALL)
    assert block, (
        "docs/Cataclysm_GDD_v2.md no longer introduces the rarity colours with "
        "'The eight rarity colours, in tier order:'. That heading is what "
        "separates them from the material ladder below.")

    found = {m.group("rarity"): m.group("hex")
             for m in GEAR_TABLE.finditer(block.group("body"))}
    assert len(found) == 8, (
        f"the design document's rarity colour table gave {len(found)} rows "
        f"and there are eight rarities: {sorted(found)}")
    return found


def test_every_material_tier_has_a_colour(material_colours) -> None:
    """Five tiers, numbered 1 to 5 with no gap. A tier with no colour is one
    whose name could not be drawn."""
    assert sorted(material_colours) == [1, 2, 3, 4, 5], (
        f"the material ladder lists tiers {sorted(material_colours)}")


def test_no_two_material_tiers_share_a_colour(material_colours) -> None:
    seen: dict[str, str] = {}
    for tier, (name, hex_text) in sorted(material_colours.items()):
        assert hex_text not in seen, (
            f"tier {tier} ({name}) and {seen[hex_text]} are both #{hex_text}, "
            f"so a player could not tell them apart on the floor")
        seen[hex_text] = name


def test_no_material_colour_sits_on_a_gear_rarity_hue(material_colours,
                                                      gear_colours) -> None:
    """The reason the material ladder is its own family rather than five of the
    gear colours: both are drawn as names on the same dungeon floor."""
    for tier, (name, hex_text) in sorted(material_colours.items()):
        if saturation(hex_text) < 0.15:
            pytest.fail(
                f"tier {tier} ({name}) is #{hex_text}, which is nearly grey. "
                f"The gear ramp already has a grey and a white, and a "
                f"desaturated material would sit on top of them.")

        for rarity, gear_hex in gear_colours.items():
            if saturation(gear_hex) < 0.15:
                # Grey and white have no meaningful hue to compare against; the
                # saturation check above is what separates a material from them.
                continue

            gap = hue_gap(hex_text, gear_hex)
            assert gap >= MINIMUM_HUE_GAP, (
                f"tier {tier} ({name}) is #{hex_text} and {rarity} gear is "
                f"#{gear_hex}, {gap:.1f} degrees apart. A material and a gear "
                f"item lying next to each other would read as the same kind of "
                f"thing.")


def test_the_material_ladder_brightens_at_every_rung(material_colours) -> None:
    """So the ladder reads with no colour vision at all.

    This is the property that survives colour blindness, and it is the reason a
    five-cyan palette is acceptable at all: the hues are close together and the
    brightnesses are not.
    """
    ladder = [material_colours[tier] for tier in sorted(material_colours)]

    for lower, higher in zip(ladder[:-1], ladder[1:], strict=True):
        assert relative_luminance(higher[1]) > relative_luminance(lower[1]), (
            f"{higher[0]} (#{higher[1]}, luminance "
            f"{relative_luminance(higher[1]):.3f}) is not brighter than "
            f"{lower[0]} (#{lower[1]}, luminance "
            f"{relative_luminance(lower[1]):.3f}). The ladder has to rise or a "
            f"player who cannot see the hue cannot read the tier.")


def test_the_generated_table_matches_the_design_document(
        material_colours) -> None:
    """The workbook is authoritative and the CSV is generated from it, so the
    document and the table have to agree about every tier."""
    if not MATERIAL_CSV.is_file():
        pytest.skip("game/Data/MaterialTiers.csv has not been generated")

    def to_linear(channel: float) -> float:
        return (channel / 12.92 if channel <= 0.04045
                else ((channel + 0.055) / 1.055) ** 2.4)

    rows = list(csv.DictReader(MATERIAL_CSV.read_text(encoding="utf-8")
                               .splitlines()))
    assert rows, "MaterialTiers.csv has no rows"

    for row in rows:
        tier = int(row["Tier"])
        assert tier in material_colours, (
            f"MaterialTiers.csv has tier {tier} and the design document does "
            f"not list it")

        name, hex_text = material_colours[tier]
        assert row["TierName"] == name, (
            f"tier {tier} is {row['TierName']!r} in MaterialTiers.csv and "
            f"{name!r} in the design document")

        wanted = [to_linear(c) for c in channels(hex_text)]
        written = [float(v) for v in
                   re.findall(r"[RGB]=([0-9.]+)", row["Colour"])]
        assert len(written) == 3, (
            f"tier {tier}'s colour in MaterialTiers.csv is {row['Colour']!r}, "
            f"which has no R, G and B to read")

        for channel, (got, want) in enumerate(zip(written, wanted, strict=True)):
            assert abs(got - want) < 0.001, (
                f"tier {tier} ({name}) channel {'RGB'[channel]} is {got} in "
                f"MaterialTiers.csv and #{hex_text} converts to {want:.6f}. "
                f"Run `python tools/generate_datatables.py`.")
