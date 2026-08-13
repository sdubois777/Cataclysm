"""Every damage type has a stated visual theme in the design document.

WHY THIS EXISTS. Issue #19. The design document described the world's tone in
prose — grim, dark fantasy, despair and gloom — and said nothing about what any
individual Cataclysm looks like. The project owner gave the eight themes on
2026-08-12 and they are now a table in section XIII.

WHAT THIS GUARDS AGAINST. A ninth damage type being added to the Tags sheet, or
one of the eight being renamed, without the visual identity table following. That
table is the whole art direction for a Cataclysm's environment, so a damage type
missing from it has no art direction at all and nobody would notice until
somebody tried to build the environment.

WHERE THE LIST OF DAMAGE TYPES COMES FROM. The Tags sheet of
`docs/All_Things_Cataclysm.xlsx`, read through the `Element.` tag prefix, which is
the same source the skill matrix and the enchantment tables use. It is not
restated here, because a copy would be one more thing to drift.

WHAT IS NOT ASSERTED. The content of a theme. "Red, fire, lava" is a judgement by
the project owner and this file has no opinion on it. Only that every damage type
has one.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

SECTION_HEADING = "## **Visual Identity per Cataclysm**"


@pytest.fixture(scope="module")
def damage_types() -> list[str]:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")
    book = openpyxl.load_workbook(WORKBOOK, read_only=True)
    names = []
    for row in book["Tags"].iter_rows(values_only=True):
        tag = str(row[0] or "").strip()
        if tag.startswith("Element."):
            names.append(tag.split(".", 1)[1])
    return names


@pytest.fixture(scope="module")
def identity_section() -> str:
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")
    text = DESIGN_DOCUMENT.read_text(encoding="utf-8")
    start = text.index(SECTION_HEADING)
    end = text.index("## **Audio**", start)
    return text[start:end]


def test_there_are_eight_damage_types_to_check(damage_types):
    """A sheet that failed to parse would leave nothing to look for, and every
    check below would pass while proving nothing."""
    assert len(damage_types) == 8, (
        f"Expected 8 Element. tags in the Tags sheet, found {len(damage_types)}: "
        f"{damage_types}. If a damage type was added or removed deliberately, "
        "the visual identity table in the design document has to follow."
    )


def table_subjects(section: str) -> set[str]:
    """The first cell of every table row in the section.

    Checking the whole section text instead would not work: the open questions
    below the table name Death, Void, Famine and Pestilence in prose, so a
    damage type deleted from the table would still be found and this file would
    pass while testing nothing. That is not hypothetical — it is what the first
    version of this file did, caught by tools/prove_guard.py.
    """
    subjects = set()
    for line in section.splitlines():
        if not line.startswith("| ") or line.startswith("| :"):
            continue
        first = line.strip("|").split("|")[0].strip()
        if first and first != "Cataclysm":
            subjects.add(first)
    return subjects


def test_every_damage_type_has_a_visual_theme(damage_types, identity_section):
    subjects = table_subjects(identity_section)
    missing = [name for name in damage_types if name not in subjects]
    assert not missing, (
        "These damage types have no row in the Visual Identity per Cataclysm "
        f"table in section XIII of {DESIGN_DOCUMENT.name}: {missing}. A "
        "Cataclysm with no theme has no art direction for its environment.\n"
        f"Rows found: {sorted(subjects)}"
    )


def test_the_section_states_that_shape_carries_the_telegraph(identity_section):
    """The rule that survives texturing. Without it the table reads as though a
    telegraph should be tinted to its Cataclysm's theme, which is the thing the
    section exists to rule out."""
    assert "shape and its edge" in identity_section, (
        "The Visual Identity section no longer states that a telegraph is made "
        "visible by its shape and edge rather than its colour."
    )
    assert "emissive" in identity_section, (
        "The Visual Identity section no longer states that telegraphs are drawn "
        "with an unlit emissive material. Without that, their brightness is "
        "whatever the room's lighting produces and no brightness rule can hold."
    )


#: The telegraph's three ring colours, outermost first. Stated here as well as
#: in the design document on purpose: these are the values
#: game/Source/Cataclysm/AbilitySystem/CataclysmTelegraphMarker.cpp is built to,
#: so a silent edit to either side should fail loudly rather than leaving the
#: document and the material disagreeing.
TELEGRAPH_OUTLINE = "#0A0F12"
TELEGRAPH_RING = "#FF3020"
TELEGRAPH_INNER = "#FFD9CF"


def test_there_is_exactly_one_telegraph_colour(identity_section):
    """The project owner settled this on 2026-08-12: one colour for the whole
    game, not one per damage type, because the creature's own art already says
    what is attacking. The colour itself was settled on 2026-08-13 by looking at
    it, after cyan was tried first."""
    for colour, why in (
        (TELEGRAPH_RING, "the ring, which is the colour the marker reads as"),
        (TELEGRAPH_OUTLINE, "the outer ring, which carries Celestial and Chaos "
                            "where everything else is brighter than the ground"),
        (TELEGRAPH_INNER, "the inner line, without which the worst case is "
                          "2.47:1 and fails the 3:1 threshold"),
    ):
        assert colour in identity_section, (
            f"{colour} is not stated in the Visual Identity section. It is "
            f"{why}."
        )

    assert "one telegraph colour for the whole game" in identity_section, (
        "The Visual Identity section no longer states that there is a single "
        "telegraph colour. If this became per damage type again, the eight "
        "colours would each need checking against all eight environments."
    )


def test_the_fill_is_stated_as_see_through(identity_section):
    """The project owner asked on 2026-08-13 for the marker to stop reading as
    a solid plate. If the document stopped saying the fill is see-through,
    nothing would connect the C++ opacity to a design decision."""
    assert "35% opacity" in identity_section, (
        "The Visual Identity section no longer states the fill's opacity. "
        "ACataclysmTelegraphMarker::DesignedFillOpacity is 0.35 and this is "
        "the only place that says why."
    )
    assert "carries none of the readability" in identity_section, (
        "The Visual Identity section no longer explains that the fill carries "
        "none of the readability. That is the reason it is allowed to be "
        "see-through at all, and without it somebody will make the rings "
        "translucent too and quietly break every contrast figure above."
    )


def test_no_per_damage_type_telegraph_colour_crept_back(damage_types, identity_section):
    """A telegraph colour named after a damage type would mean the single-colour
    rule had quietly been reversed."""
    start = identity_section.index("one telegraph colour for the whole game")
    after = identity_section[start:]
    offenders = [
        name for name in damage_types
        if f"{name} telegraph" in after or f"telegraph is {name}" in after
    ]
    assert not offenders, (
        f"These damage types are given their own telegraph colour: {offenders}. "
        "There is one telegraph colour for the whole game."
    )
