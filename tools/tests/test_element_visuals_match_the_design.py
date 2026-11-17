"""The shipped effect palette table is the one section XIII of the design
document states.

WHY THIS EXISTS. Issue #549. The eight damage types' effect colours were settled
by the project owner and written into section XIII of docs/Cataclysm_GDD_v2.md,
and nothing in the game could read them. game/Data/ElementVisuals.csv is that
palette in the form Unreal imports. Two copies of eight colour pairs is exactly
the arrangement that drifts, so this file compares them.

WHAT DRIFT LOOKS LIKE WITHOUT THIS FILE. Somebody changes Void's primary in the
design document because it reads too blue next to Death, and does not touch the
workbook. The document and the game now disagree, both look internally
consistent, and the first symptom is an artist matching a Niagara system to a
colour the game does not use.

HOW THE COMPARISON IS DONE, and this is the part worth understanding. The design
document states sRGB hex, because that is what a colour picker shows. The CSV
carries linear floats, because that is what an FLinearColor is and what a
material multiplies. So the two cannot be compared as written. This file converts
the CSV's linear values BACK to sRGB hex and compares the hex.

    THE ROUND TRIP IS EXACT, and `test_the_hex_conversion_round_trips_exactly`
    proves it rather than assuming it: all 256 byte values survive the trip out
    to linear and back, including through the six decimal places the CSV
    actually carries. So this is an equality comparison and not a tolerance.

    IT IS DELIBERATELY THE INVERSE FUNCTION AND NOT THE GENERATOR'S OWN. Calling
    tools/generate_datatables.py's `linear_colour` to work out what the CSV
    should contain would agree with that function whatever it did, including
    getting the conversion backwards. Going the other way is an independent
    check, and it makes a failure read in the design document's own notation:
    "Demonic primary is #FF7A2F, the design says #FF7A2E" says what to do.

WHAT IS NOT ASSERTED HERE. Whether any particular colour is a good one. That the
palette stays readable -- a primary bright enough to clear 3:1 against the
brightest floor the world allows, a secondary dark enough to anchor one -- is
checked against the same document by
tools/tests/test_effect_palette_stays_readable.py. Between the two, the rule
reaches the shipped table without the CIE lightness formula being written down
twice.

That other file also parses this table out of section XIII, for its own purpose.
Two parsers of one markdown table can drift, so both assert they found eight
rows before comparing anything. If a third reader ever appears, move the parser
into a module under tools/ rather than writing it again.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
GENERATED_CSV = REPO_ROOT / "game" / "Data" / "ElementVisuals.csv"
TAG_CONFIG = REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini"

SECTION_HEADING = "### **The effect palette, which is not the environment palette**"
SECTION_END = "### **The one rule that generates the rest**"

#: The prefix every damage type's tag carries.
ELEMENT_TAG_PREFIX = "Element."

#: How many damage types there are. Written here so a parser that quietly
#: matched fewer fails instead of comparing a shorter list to itself.
DAMAGE_TYPE_COUNT = 8

#: What the three material scales read today. All eight rows are neutral: the
#: colours are settled and the material axes that turn a colour into a look are
#: not, so 1.0 leaves the Niagara system's own authored value alone. This is
#: pinned rather than left alone so that tuning one becomes a deliberate edit
#: here as well as in the workbook.
NEUTRAL_SCALE = 1.0
SCALE_COLUMNS = ("EmissiveMultiplier", "SpawnRateScale", "VelocityScale")


def linear_to_srgb_hex(linear: tuple[float, float, float]) -> str:
    """Three linear channels as the sRGB hex the design document writes."""
    def channel(value: float) -> int:
        encoded = (value * 12.92 if value <= 0.0031308
                   else 1.055 * value ** (1 / 2.4) - 0.055)
        return round(encoded * 255)

    return "#" + "".join(f"{channel(v):02X}" for v in linear)


def parse_linear_colour(text: str, where: str) -> tuple[float, float, float]:
    """Read the `(R=...,G=...,B=...,A=...)` text Unreal imports into a colour."""
    found = re.fullmatch(
        r"\(R=([-\d.]+),G=([-\d.]+),B=([-\d.]+),A=([-\d.]+)\)", text.strip())
    assert found, (
        f"{where} is {text!r}, which is not the text Unreal imports into an "
        "FLinearColor. It must read (R=...,G=...,B=...,A=...). A cell Unreal "
        "cannot parse imports as the struct's C++ default with no error."
    )
    alpha = float(found.group(4))
    assert alpha == 1.0, (
        f"{where} has an alpha of {alpha}. An effect colour is fully opaque; "
        "how much of it shows is the Niagara system's business, not the "
        "palette's."
    )
    return tuple(float(found.group(i)) for i in (1, 2, 3))


@pytest.fixture(scope="module")
def designed() -> dict[str, tuple[str, str]]:
    """Damage type to (primary, secondary) sRGB hex, read out of section XIII."""
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")

    text = DESIGN_DOCUMENT.read_text(encoding="utf-8")
    start = text.index(SECTION_HEADING)
    section = text[start:text.index(SECTION_END, start)]

    found: dict[str, tuple[str, str]] = {}
    for line in section.splitlines():
        if not line.startswith("| ") or line.startswith("| :"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 3:
            continue
        colours = re.findall(r"#[0-9A-Fa-f]{6}", f"{cells[1]} {cells[2]}")
        if len(colours) == 2:
            found[cells[0]] = (colours[0].upper(), colours[1].upper())
    return found


@pytest.fixture(scope="module")
def generated() -> dict[str, dict[str, str]]:
    """The generated table, keyed by row name."""
    if not GENERATED_CSV.is_file():
        pytest.skip(f"{GENERATED_CSV.name} is not present. Run "
                    "`python tools/generate_datatables.py`.")
    with GENERATED_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


# --------------------------------------------------------------------------
# the guards on this file's own machinery
# --------------------------------------------------------------------------

def test_the_hex_conversion_round_trips_exactly() -> None:
    """Without this, the comparisons below are a tolerance pretending to be an
    equality, and nobody reading them would know.

    Every one of the 256 values a channel can hold is converted out to linear
    the way the generator does it, written to the six decimal places the CSV
    carries, and read back. All 256 must return the byte they started as.
    """
    def to_linear(byte: int) -> float:
        c = byte / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    lost = []
    for byte in range(256):
        through_the_csv = float(f"{to_linear(byte):.6f}")
        back = linear_to_srgb_hex((through_the_csv,) * 3)
        if back != f"#{byte:02X}{byte:02X}{byte:02X}":
            lost.append((byte, back))

    assert not lost, (
        "These channel values do not survive the trip to linear and back, so "
        f"the colour comparisons in this file are not exact: {lost[:8]}"
    )


def test_the_design_document_table_was_actually_parsed(designed) -> None:
    """Every other test compares against what the parser produced. If it
    produced nothing they all pass having compared nothing at all."""
    assert len(designed) == DAMAGE_TYPE_COUNT, (
        f"parsed {len(designed)} row(s) out of the effect palette table in "
        f"section XIII of {DESIGN_DOCUMENT.name}: {sorted(designed)}. There are "
        f"{DAMAGE_TYPE_COUNT} damage types, so the parser in this file has "
        "stopped matching the table's shape. Fix it rather than lowering this "
        "number: every comparison below would otherwise pass while checking "
        "less than it claims."
    )


# --------------------------------------------------------------------------
# the comparison this file exists for
# --------------------------------------------------------------------------

def test_the_generated_table_has_one_row_per_damage_type(designed,
                                                         generated) -> None:
    missing = sorted(set(designed) - set(generated))
    extra = sorted(set(generated) - set(designed))

    assert not missing, (
        f"These damage types are in the effect palette table in section XIII "
        f"of {DESIGN_DOCUMENT.name} and have no row in {GENERATED_CSV.name}: "
        f"{missing}. A damage type with no row takes whatever colour its "
        "Niagara system was authored with, and nothing reports it."
    )
    assert not extra, (
        f"{GENERATED_CSV.name} has row(s) {extra} that the design document's "
        "effect palette table does not name."
    )


def test_every_colour_matches_the_design_document(designed, generated) -> None:
    """The comparison the file exists for. A colour changed in one place and
    not the other fails here, in the design document's own notation."""
    wrong = []
    for damage_type, (primary, secondary) in sorted(designed.items()):
        row = generated[damage_type]
        for column, expected in (("PrimaryColour", primary),
                                 ("SecondaryColour", secondary)):
            where = f"{GENERATED_CSV.name} {damage_type}.{column}"
            actual = linear_to_srgb_hex(parse_linear_colour(row[column], where))
            if actual != expected:
                wrong.append(f"{damage_type} {column} is {actual}, the design "
                             f"document says {expected}")

    assert not wrong, (
        f"{GENERATED_CSV.name} and section XIII of {DESIGN_DOCUMENT.name} "
        "disagree about the effect palette. The design document is the "
        "decision; the workbook sheet 'Element Visuals' is what generates the "
        "table. Change the workbook and run "
        "`python tools/generate_datatables.py`.\n  " + "\n  ".join(wrong)
    )


def test_every_row_names_a_tag_the_engine_will_know(generated) -> None:
    """The generator checks these against the workbook's Tags sheet. This
    checks them against the file the engine actually loads, which is what
    decides whether a lookup finds anything at runtime."""
    if not TAG_CONFIG.is_file():
        pytest.skip(f"{TAG_CONFIG.name} is not present")
    config = TAG_CONFIG.read_text(encoding="utf-8")

    problems = []
    for name, row in sorted(generated.items()):
        tag = row["ElementTag"]
        if tag != f"{ELEMENT_TAG_PREFIX}{name}":
            problems.append(
                f"row {name} carries the tag {tag!r}. The row key is the tag's "
                f"leaf, so it must be {ELEMENT_TAG_PREFIX}{name}.")
        if f'Tag="{tag}"' not in config:
            problems.append(
                f"row {name} names {tag!r}, which {TAG_CONFIG.name} does not "
                "declare. The engine resolves an undeclared tag to nothing and "
                "reports nothing.")

    assert not problems, "\n".join(problems)


def test_the_material_scales_are_neutral(generated) -> None:
    """All three are 1.0 on all eight rows today, because the colours are
    settled and the axes that turn a colour into a look are not.

    Pinned rather than ignored so that the first real tuning pass is a
    deliberate edit here as well as in the workbook, and so a column silently
    importing as zero -- which reads as an effect that spawns nothing, never
    moves, or renders black -- fails instead.
    """
    wrong = [
        f"{name}.{column} is {row[column]}"
        for name, row in sorted(generated.items())
        for column in SCALE_COLUMNS
        if float(row[column]) != NEUTRAL_SCALE
    ]
    assert not wrong, (
        f"These material scales are no longer the neutral {NEUTRAL_SCALE}. If "
        "that is deliberate tuning, update this test and say in the pull "
        "request what the new figures are for.\n  " + "\n  ".join(wrong)
    )
