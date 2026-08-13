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
