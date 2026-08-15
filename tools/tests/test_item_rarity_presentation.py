"""An item's rarity changes its colour, never its model. Issue #537.

WHY THIS EXISTS. The design document said nothing at all about how rarity is
shown. That absence is what let issue #17, the 3D asset bake-off, open with "24
classes of gear, 15 weapon types across eight rarity tiers" and treat the eight
tiers as a multiplier on the model count. They are not: there are 55 item bases
and 55 gear models, because the base determines the geometry and rarity does not
touch it.

WHY THE ANSWER FOLLOWS FROM THE DOCUMENT RATHER THAN FROM TASTE. Two sentences
already in the Item Rarities section decide it between them:

    "Rarity is not a property an item carries. It is a label for what fills its
    four slots."
    "Adding an affix promotes the piece."

Rarity is computed from an item's contents and changes at the crafting bench, so
a model that followed rarity would change shape in the player's hands. Those two
sentences are asserted here as well as the new rule, because the new rule rests
on them: if either is ever rewritten, the reasoning recorded in
`docs/DECISIONS.md` stops holding and this file should be revisited rather than
quietly still passing.

WHAT IS NOT CHECKED HERE. Which eight colours the ramp uses. They are not
assigned yet, and section XIII states the two constraints on assigning them
rather than the answer.

THE MODEL COUNT IS READ FROM THE DATA, not pinned to 55 here. Adding an item base
should not break a test; the document claiming a count that disagrees with
`game/Data/ItemBases.csv` should.
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
ITEM_BASES = REPO_ROOT / "game" / "Data" / "ItemBases.csv"


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def item_bases() -> list[dict]:
    if not ITEM_BASES.is_file():
        pytest.skip("ItemBases.csv is not present")
    with open(ITEM_BASES, encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def test_it_says_rarity_never_changes_the_model(document):
    """The decision itself. Issue #537."""
    assert "Rarity never changes the model" in document, (
        "the design document does not say whether an item's rarity changes its "
        "3D model or only its colour. It changes the colour, frame and drop "
        "effect only. Without that sentence the eight rarity tiers read as a "
        "multiplier on the model count, which is how issue #17's estimate went "
        "wrong. Issue #537.")


def test_it_says_the_item_base_is_what_decides_the_geometry(document):
    """The other half. "Rarity does not" leaves the reader asking what does."""
    assert "geometry comes from its item base" in document, (
        "the design document says rarity does not decide an item's model "
        "without saying what does. The item base does. Issue #537.")


def test_it_names_the_named_set_exception(document):
    """A set has an identity of its own, so it may carry bespoke art.

    Stated where sets are defined as well as where rarity is, because the cost
    is per set and should be counted when a set is written.
    """
    assert "Named sets are the one exception" in document, (
        "the design document states the rarity rule without naming the one "
        "exception. A named set may carry bespoke geometry. Issue #537.")

    assert "only itemisation layer that buys bespoke geometry" in document, (
        "the Set Enchantments section does not say that a set is what buys "
        "bespoke models, so the art cost of a set is invisible where sets are "
        "defined. Issue #537.")


def test_the_stated_model_count_matches_the_item_bases(document, item_bases):
    """The count is the thing #17 got wrong, so it is read from the data.

    THE FAILURE THIS EXISTS FOR is somebody adding item bases and leaving the
    document claiming the old number, which is exactly how "15 weapon types"
    survived. It fails when the data moves and the sentence does not.
    """
    bases = len(item_bases)
    weapons = len({row["WeaponType"] for row in item_bases
                   if row["WeaponType"].strip()})

    assert f"the number of gear\nmodels this project has to produce is {bases}" in document, (
        f"game/Data/ItemBases.csv holds {bases} item bases, and the design "
        f"document does not say that is the gear model count. Issue #537.")

    assert f"{bases} item bases, {weapons} of them weapon" in document, (
        f"the design document's item base and weapon type counts do not match "
        f"the data: {bases} bases and {weapons} weapon types in "
        f"game/Data/ItemBases.csv. Issue #537.")


def test_the_two_sentences_the_decision_rests_on_are_still_there(document):
    """If either is rewritten, the reasoning in docs/DECISIONS.md stops holding.

    Rarity being computed from slot contents, and being promoted by adding an
    affix, are what make a rarity-driven model change shape at the bench. A
    version of this design where rarity is stored on the item would deserve the
    question asked again rather than this file still passing.
    """
    for sentence in ("Rarity is not a property an item carries",
                     "Adding an affix promotes the piece"):
        assert sentence in document, (
            f"the design document no longer says {sentence!r}. The rule that "
            f"rarity never changes an item's model was decided because rarity "
            f"is computed and mutable; if that changed, issue #537 should be "
            f"asked again rather than left answered.")


def test_the_rarity_ramp_may_not_reuse_the_damage_type_hues(document):
    """The constraint on whoever assigns the eight colours. Issue #537.

    The effect palette's eight hues already mean "this damage is Fire" wherever
    they appear. A rarity ramp sharing them makes a drop's colour ambiguous
    between what the item is and what it does.
    """
    assert "must not reuse the eight damage-type hues" in document, (
        "section XIII does not say that the item rarity colours have to be "
        "separable from the damage-type palette. Issue #537.")

    assert "Colour cannot be the only channel" in document, (
        "section XIII does not carry the second channel requirement for the "
        "rarity ramp, so a player who cannot separate two hues cannot separate "
        "two rarities. Issue #537.")
