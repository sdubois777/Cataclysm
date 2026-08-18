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

THE EIGHT COLOURS ARE CHECKED HERE TOO, since issue #602 assigned them on
2026-08-14: White, Grey, Green, Blue, Yellow, Orange, Purple, Red. Seven of them
sit close to one of the eight Cataclysm damage hues, which is deliberate and
safe, because the two palettes never appear on the same surface.

THE MODEL COUNT IS READ FROM THE DATA, not pinned to 55 here. Adding an item base
should not break a test; the document claiming a count that disagrees with
`game/Data/ItemBases.csv` should.
"""

from __future__ import annotations

import csv
import pathlib
import re

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


#: The eight rarity colours, in tier order, chosen by the project owner on
#: 2026-08-14. Issue #602.
RARITY_COLOURS = [
    ("Everyday", "White"),
    ("Quality", "Grey"),
    ("Superb", "Green"),
    ("Masterful", "Blue"),
    ("Legendary", "Yellow"),
    ("Mythical", "Orange"),
    ("Ascendant", "Purple"),
    ("Cataclysmic", "Red"),
]


def colour_table_tiers(document: str) -> list[str]:
    """The tier names section XIII's colour table actually lists, in order.

    READ OUT OF THE DOCUMENT rather than taken from RARITY_COLOURS above. The
    first version of the drift check below iterated over the Python list, so
    renaming a tier in the document could not fail it: it went on checking that
    the OLD name was in the Item Rarities table, which it still was. Proving the
    guard is what found that.
    """
    # THE HEADER MAY CARRY MORE COLUMNS THAN THESE TWO. It gained an sRGB
    # column when the colour VALUES were settled on 2026-08-18; the tier is
    # still the first cell, which is all this reads.
    block = re.search(
        r"^\| Rarity \| Colour \|[^\n]*\n\|[^\n]*\|\n((?:\|[^\n]*\|\n)+)",
        document, re.MULTILINE)
    if not block:
        return []
    return [line.split("|")[1].strip()
            for line in block.group(1).splitlines() if line.strip()]


def test_every_rarity_tier_has_a_colour(document):
    """Issue #602. Eight tiers, eight colours, in the same order both places."""
    for tier, colour in RARITY_COLOURS:
        assert f"| {tier} | {colour} |" in document, (
            f"section XIII does not give {tier} the colour {colour}. All eight "
            f"tiers need one. Issue #602.")


def test_the_colour_table_covers_the_same_tiers_as_the_rarity_table(document):
    """The two tables are in different sections and would drift apart silently.

    THE FAILURE THIS EXISTS FOR is adding a ninth rarity to the Item Rarities
    table, or renaming one, and leaving section XIII's colour table behind. That
    already happened once with this exact set of tiers: the sixth was called
    Mythic in one table and Mythical in four other places, which issue #603
    fixed.
    """
    named = colour_table_tiers(document)
    assert named, "section XIII no longer has a rarity colour table at all."

    for tier in named:
        assert re.search(rf"^\| {re.escape(tier)} \| \d+ \| \d+ \|", document,
                         re.MULTILINE), (
            f"{tier!r} is given a colour in section XIII but is not a row of the "
            f"Item Rarities table, which is the definitive list of tiers. Either "
            f"it was renamed in one place and not the other, or a tier was "
            f"invented. Issues #602 and #603.")

    assert named == [tier for tier, _ in RARITY_COLOURS], (
        f"the tiers in section XIII's colour table are {named}, and the eight "
        f"the project owner assigned colours to on 2026-08-14 are "
        f"{[tier for tier, _ in RARITY_COLOURS]}. Issue #602.")


def test_it_says_the_two_palettes_may_overlap_and_why(document):
    """The constraint written on 2026-08-14 was too strong and was replaced.

    WHAT THIS USED TO ASSERT. Until the colours were chosen, this file asserted
    that section XIII said the rarity ramp "must not reuse the eight damage-type
    hues". Seven of the eight colours the project owner then chose do sit close
    to a Cataclysm hue, and the project owner's answer was that this is not a
    problem.

    They were right, and the reason is worth keeping rather than the rule: the
    two palettes never share a SURFACE. Rarity colours are on item names,
    inventory frames and the marker over a drop; damage-type hues are on skill
    and damage effects. Nothing is both an item and an attack.
    """
    assert "The two palettes never share a surface" in document, (
        "section XIII gives the rarity colours without saying why overlapping "
        "the damage-type palette is safe. Seven of the eight do overlap, so a "
        "reader who knows the effect palette will think it is a mistake. "
        "Issue #602.")

    assert "Colour is still not the only channel" in document, (
        "section XIII does not carry the second channel requirement for the "
        "rarity ramp, so a player who cannot separate two hues cannot separate "
        "two rarities. Issues #537 and #602.")


def test_a_damage_taken_debuff_is_the_defenders_bucket(document):
    """Issue #600. "One bucket per stat" never said whose stat.

    THE FAILURE THIS EXISTS FOR is the sentence being dropped as redundant. It
    reads like a restatement of the bucket rule and it is not: it is the half
    the bucket rule leaves open, and the two readings differ by about a factor
    of ten on Thornwall, a Bulwark capstone option.
    """
    assert "is the target's bucket, not\nthe attacker's" in document, (
        "section IV states the damage pipeline without saying which side owns "
        "the bucket for a debuff that increases the damage a target takes. It "
        "is the defender's. Issue #600.")

    assert "They do not join the attacker's bucket" in document, (
        "section IV does not rule out the other reading, under which an "
        "invested attacker dilutes the debuff to a few percent. Issue #600.")


# --------------------------------------------------------------------------
# The colour VALUES, settled on 2026-08-18
# --------------------------------------------------------------------------

GEAR_RARITY_CSV = REPO_ROOT / "game" / "Data" / "GearRarity.csv"

#: `| Everyday | White | \`#FFFFFF\` |`
COLOUR_VALUE_ROW = re.compile(
    r"^\|\s*([A-Za-z]+)\s*\|\s*[A-Za-z]+\s*\|\s*`(#[0-9A-Fa-f]{6})`\s*\|$",
    re.MULTILINE)


def stated_values(document) -> dict:
    """Tier against the sRGB hex the design document states for it."""
    return dict(COLOUR_VALUE_ROW.findall(document))


def test_every_rarity_states_an_srgb_value(document):
    """The colour NAMES were assigned on 2026-08-14 and the VALUES on
    2026-08-18. A name with no value is a colour nobody can draw."""
    values = stated_values(document)
    assert list(values) == [tier for tier, _ in RARITY_COLOURS], (
        f"section XIII states sRGB values for {list(values)}, and the eight "
        f"tiers are {[tier for tier, _ in RARITY_COLOURS]}.")


def test_no_two_rarities_share_a_value(document):
    """A player reads a rarity off the floor by the colour of its name, so two
    rarities sharing one would be two things that cannot be told apart."""
    values = stated_values(document)
    assert len(set(values.values())) == len(values), (
        f"two rarities are given the same colour: {values}")


def test_the_stated_values_are_the_ones_the_game_loads(document):
    """The design document holds sRGB and `game/Data/GearRarity.csv` holds the
    linear conversion, so this converts and compares rather than matching text.

    WITHOUT THIS the document could state one colour and the game draw another,
    and the only way to notice would be to look at both.
    """
    if not GEAR_RARITY_CSV.is_file():
        pytest.skip("GearRarity.csv is not present")

    import csv as csv_module
    with GEAR_RARITY_CSV.open(encoding="utf-8-sig", newline="") as handle:
        loaded = {row["Rarity"]: row["Colour"]
                  for row in csv_module.DictReader(handle)}

    def to_linear(channel: int) -> float:
        share = channel / 255.0
        if share <= 0.04045:
            return share / 12.92
        return ((share + 0.055) / 1.055) ** 2.4

    wrong = []
    for tier, hex_value in stated_values(document).items():
        red, green, blue = (int(hex_value[i:i + 2], 16) for i in (1, 3, 5))
        expected = (f"(R={to_linear(red):.6f},G={to_linear(green):.6f},"
                    f"B={to_linear(blue):.6f},A=1.000000)")
        if loaded.get(tier) != expected:
            wrong.append(f"{tier}: document {hex_value} becomes {expected}, "
                         f"and the table holds {loaded.get(tier)}")
    assert not wrong, (
        "docs/Cataclysm_GDD_v2.md and game/Data/GearRarity.csv disagree about "
        "what colour a rarity is drawn in: " + "; ".join(wrong))
