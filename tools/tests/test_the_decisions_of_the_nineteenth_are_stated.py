"""Two design rules decided on 2026-08-19 that nothing else checks yet.

WHY THIS FILE EXISTS. The project owner settled three things after the first
session of playing with loot working. One of them — swapping the two lowest
rarity colours, issue #711 — changed data, and
`tools/tests/test_item_rarity_presentation.py` already checks it against the
design document and `tools/tests/test_generate_datatables.py` against the
generated table. The other two are rules with no code behind them yet:

- **A drop's name is bordered, and the border's thickness is its rarity**, which
  is issue #718 and is what satisfies the design's own requirement that the drop
  marker "must differ by shape or motion as well as by colour".
- **Crafting materials stack**, which is issue #717 and is what makes it possible
  to carry a Cataclysm Boss's 24 materials in a 48-slot inventory at all.

A rule stated in `docs/Cataclysm_GDD_v2.md` and implemented nowhere is the
easiest kind to lose: nothing fails when it is deleted, and the next person to
read the section sees no sign it was ever agreed. These tests fail if either
sentence goes away.

WHAT THEY DO NOT CHECK. That anything implements either rule. When the border is
built, the thickness per rarity should be pinned to this document the way
`tools/tests/test_carried_inventory_is_forty_eight_slots.py` pins the 48 slots to
it — read out of the C++ as text, because continuous integration compiles no C++.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"


@pytest.fixture(scope="module")
def document() -> str:
    """The design document as one line, so a sentence broken across a wrap still
    matches."""
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return " ".join(GDD.read_text(encoding="utf-8").split())


def test_a_drops_name_carries_a_border_thick_enough_to_read_its_rarity(
        document) -> None:
    """Issue #718. The Interface Colour section requires a second channel and
    this is what fills it."""
    assert ("The second channel is a border around the name, and its thickness "
            "is the rarity.") in document, (
        "docs/Cataclysm_GDD_v2.md no longer says the drop name's border "
        "thickness is its rarity. That sentence is what answers the design's own "
        "requirement that the drop marker differ by shape as well as by colour, "
        "which was stated and unmet until 2026-08-19. Issue #718.")

    assert "one pixel thick for Everyday" in document, (
        "the design no longer states where the border thickness starts.")


def test_the_requirement_the_border_answers_is_still_stated(document) -> None:
    """The rule and the thing it answers are separate sentences, and losing
    either one leaves the other looking arbitrary."""
    assert ("the frame and the drop marker must differ by shape or motion as "
            "well as by colour") in document, (
        "The Interface Colour section no longer requires the drop marker to "
        "differ by shape or motion. That requirement is the reason the border "
        "exists; without it the border reads as decoration.")


def test_crafting_materials_stack_and_gear_does_not(document) -> None:
    """Issue #717. Both halves matter: a stacking rule that quietly covered gear
    would merge two items that carry different affixes."""
    assert ("Crafting materials stack, and every material of one kind takes one "
            "slot however many are held.") in document, (
        "docs/Cataclysm_GDD_v2.md no longer says crafting materials stack. A "
        "Cataclysm Boss averages 24 materials and the carried inventory is 48 "
        "slots, so without stacking one kill could fill half the bag with dust. "
        "Decided by the project owner on 2026-08-19. Issue #717.")

    assert "Gear does not stack, and cannot." in document, (
        "docs/Cataclysm_GDD_v2.md no longer rules out stacking gear. Two items "
        "of the same base carry different affixes, upgrade levels, sockets and "
        "residue, so there is no sense in which they are the same thing.")


def test_the_one_item_one_slot_rule_survives_the_stacking_exception(
        document) -> None:
    """The stacking rule is an exception to a rule that has to still be there.

    `tools/tests/test_carried_inventory_is_forty_eight_slots.py` checks the 48
    and what may not increase it. This checks the sentence stacking is carved
    out of, which that file does not.
    """
    assert ("One item takes one slot whatever it is, the same rule the stash "
            "uses.") in document, (
        "The Storage section no longer says one item takes one slot. Crafting "
        "materials stacking is an exception to that rule, and an exception to a "
        "rule nobody states is just an unexplained behaviour.")
