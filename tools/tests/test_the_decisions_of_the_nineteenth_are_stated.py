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

THE BORDER IS NOW BUILT, and the last test in this file pins the engine's
thickness to what the document says, read out of the C++ as text — the same
arrangement `tools/tests/test_carried_inventory_is_forty_eight_slots.py` uses,
and for the same reason: continuous integration compiles no C++, so an assertion
beside the constant would never run on a pull request.

WHAT IS STILL NOT CHECKED. That crafting materials stack anywhere, because
nothing implements them yet.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

sys.path.insert(0, str(REPO_ROOT / "sim"))


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


DROP_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
               / "CataclysmDroppedItem.h")


def constant(name: str) -> int:
    """The value of a `static constexpr int32 <name> = <number>;` line."""
    if not DROP_HEADER.is_file():
        pytest.fail(f"{DROP_HEADER.relative_to(REPO_ROOT)} does not exist")

    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        DROP_HEADER.read_text(encoding="utf-8"))
    if match is None:
        pytest.fail(
            f"CataclysmDroppedItem.h has no "
            f"'static constexpr int32 {name} = <number>;' line. If it was "
            f"renamed, rename it here too; if it was deleted, the border "
            f"thickness is unguarded and continuous integration compiles no C++ "
            f"to notice.")
    return int(match.group(1))


def test_the_engines_border_matches_what_the_design_says(document) -> None:
    """The thinnest border, and the thickest, against the design document.

    NEITHER NUMBER IS PINNED HERE. Both are read out of the document's own
    sentences and compared with the engine, so moving them together passes and
    moving one alone fails. The top of the ladder is derived from the number of
    rarities in the model rather than written down, so adding a rung fails here
    rather than leaving the new one sharing a thickness with the rung below.
    """
    from cataclysm_sim.affixes import RARITIES

    thinnest = re.search(r"(\w+) pixels? thick for Everyday", document)
    assert thinnest, (
        "docs/Cataclysm_GDD_v2.md no longer says how thick an Everyday drop's "
        "border is.")
    words = {"one": 1, "two": 2, "three": 3, "four": 4}
    assert thinnest.group(1).lower() in words, (
        f"unrecognised thickness word: {thinnest.group(1)!r}")
    designed_thinnest = words[thinnest.group(1).lower()]

    assert constant("ThinnestNameBorderPx") == designed_thinnest, (
        f"NameBorderThicknessFor starts at "
        f"{constant('ThinnestNameBorderPx')} in "
        f"game/Source/Cataclysm/Items/CataclysmDroppedItem.h and "
        f"docs/Cataclysm_GDD_v2.md says {designed_thinnest}.")

    # THE TOP OF THE LADDER, WHICH THE DOCUMENT STATES AS A SENTENCE.
    thickest = re.search(r"Cataclysmic sits inside (\w+)", document)
    assert thickest, (
        "docs/Cataclysm_GDD_v2.md no longer says how thick a Cataclysmic drop's "
        "border is. That sentence is what says the rungs are all different.")
    tops = {"six": 6, "seven": 7, "eight": 8, "nine": 9, "ten": 10}
    assert thickest.group(1).lower() in tops, (
        f"unrecognised thickness word: {thickest.group(1)!r}")

    engine_top = constant("ThinnestNameBorderPx") + len(RARITIES) - 1
    assert engine_top == tops[thickest.group(1).lower()], (
        f"The engine gives the top rarity a border of {engine_top} pixels and "
        f"the design says {tops[thickest.group(1).lower()]}. There are "
        f"{len(RARITIES)} rarities, so a ladder starting at "
        f"{constant('ThinnestNameBorderPx')} ends at {engine_top}.")
