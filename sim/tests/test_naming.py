"""Tests for what a dropped item is called, in `sim/cataclysm_sim/naming.py`.

WHAT THESE GUARD:

1. The format the project owner asked for, on a real rolled item rather than a
   hand-built one.

2. That the name is stable. The same item must be called the same thing every
   time it is looked at, which needs a tie-break the obvious implementation does
   not have.

3. That an item with no suffix affix is named without a dangling "of".

4. That the module's import-time checks can actually fail.
"""

from __future__ import annotations

import random

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import loot, naming


def an_item(slot="Chest", tier=8, magic_find=200.0, seed=1):
    return loot.roll_item(slot, tier, magic_find, random.Random(seed))


# --------------------------------------------------------------------------
# The format
# --------------------------------------------------------------------------

def test_a_name_reads_rarity_then_base_then_the_word():
    rng = random.Random(2026)
    for _ in range(300):
        item = loot.roll_item("Chest", tier=8, magic_find=200.0, rng=rng)
        name = naming.name_of(item)

        assert name.startswith(f"{item.rarity} {item.base.name}"), (
            f"{name!r} does not begin with its rarity and base")

        if naming.strongest_suffix(item) is None:
            assert name == f"{item.rarity} {item.base.name}"
        else:
            assert " of " in name


def test_an_item_with_no_suffix_affix_has_no_dangling_of():
    """An Everyday piece carries one affix and it may be a prefix.

    THE CASE THAT WOULD LOOK BROKEN IN PLAY. A name ending in "of" with nothing
    after it, or a bare "of None", is what the obvious implementation produces.
    """
    rng = random.Random(55)
    seen = False
    for _ in range(3000):
        item = loot.roll_item("Boots", tier=1, magic_find=0.0, rng=rng)
        if naming.strongest_suffix(item) is not None:
            continue
        seen = True
        name = naming.name_of(item)
        assert not name.endswith(" of")
        assert "None" not in name
        assert name == f"{item.rarity} {item.base.name}"

    assert seen, "no item without a suffix affix was rolled, so nothing was checked"


def test_a_cataclysmic_item_is_named_without_a_word():
    """It carries four enchantments and no regular affixes at all, so there is
    no suffix to name it after.

    A MAGIC FIND NO CHARACTER COULD HAVE, on purpose. A Cataclysmic drop is one
    in 25,531 at difficulty tier 8, so rolling until one appears would take a
    hundred thousand items and several seconds. Magic find multiplies the top
    rung's chance until it saturates at certainty, which is the real code path
    rather than a hand-built item.
    """
    rng = random.Random(4242)
    seen = False
    for _ in range(200):
        item = loot.roll_item("Relic", tier=8, magic_find=5_000_000.0, rng=rng)
        if item.rarity != "Cataclysmic":
            continue
        seen = True
        assert naming.name_of(item) == f"Cataclysmic {item.base.name}"
    assert seen, "no Cataclysmic item was rolled, so nothing was checked"


# --------------------------------------------------------------------------
# Which affix the name comes from
# --------------------------------------------------------------------------

def test_the_word_comes_from_the_highest_tier_suffix():
    # NO MAGIC FIND, AND A MIDDLING TIER. Magic find pushes drops toward
    # Cataclysmic, which carries four enchantments and NO regular affixes, so a
    # lucky character rolls fewer items with two suffixes rather than more.
    rng = random.Random(9)
    checked = 0
    for _ in range(1500):
        item = loot.roll_item("Ring", tier=4, magic_find=0.0, rng=rng)
        suffixes = [r for r in item.affixes
                    if naming.word_for(r.affix.name) is not None]
        if len(suffixes) < 2:
            continue
        checked += 1
        chosen = naming.strongest_suffix(item)
        assert chosen.tier == max(r.tier for r in suffixes)
    assert checked > 20, (
        f"only {checked} items carried two suffix affixes, so the comparison "
        "barely ran")


def test_the_same_item_is_always_called_the_same_thing():
    """THE TIE-BREAK IS WHY THIS EXISTS. Affixes are stored in the order they
    were drawn, and two can share a tier and a roll. Without a final tie-break on
    position the name would depend on how the list was iterated."""
    item = an_item(seed=77)
    assert len({naming.name_of(item) for _ in range(50)}) == 1


def test_a_prefix_affix_never_supplies_the_word():
    rng = random.Random(303)
    for _ in range(300):
        item = loot.roll_item("Chest", tier=8, magic_find=200.0, rng=rng)
        chosen = naming.strongest_suffix(item)
        if chosen is None:
            continue
        assert chosen.affix.position == "suffix"


def test_a_prefix_affix_has_no_word_at_all():
    assert naming.word_for("Flat maximum health") is None


# --------------------------------------------------------------------------
# The word table
# --------------------------------------------------------------------------

def test_every_suffix_affix_in_the_pool_has_a_word():
    """Derived from the pool rather than counted, so a new affix is caught."""
    for slot in af.GEAR_SLOTS:
        for affix in af.everything_for(slot, "suffix"):
            assert naming.word_for(affix.name) is not None, (
                f"{affix.name} can roll on {slot} and has no name word")


def test_no_two_affixes_share_a_word():
    """A player reading "of Warding" should find one thing when they look it up."""
    words = list(naming.AFFIX_NAME_WORD.values())
    assert len(words) == len(set(words))


def test_no_word_is_empty_or_padded():
    for affix_name, word in naming.AFFIX_NAME_WORD.items():
        assert word == word.strip() and word, f"{affix_name} has the word {word!r}"


# --------------------------------------------------------------------------
# That the module's own import-time checks can fail
# --------------------------------------------------------------------------

def test_that_the_missing_word_check_actually_fires():
    original = naming.AFFIX_NAME_WORD
    try:
        naming.AFFIX_NAME_WORD = {"Chance to burn": "Cinders"}
        with pytest.raises(AssertionError, match="no name word"):
            naming._check_every_suffix_affix_has_a_word()
    finally:
        naming.AFFIX_NAME_WORD = original


def test_that_the_prefix_word_check_actually_fires():
    original = naming.AFFIX_NAME_WORD
    try:
        naming.AFFIX_NAME_WORD = dict(original)
        naming.AFFIX_NAME_WORD["Flat maximum health"] = "the Boar"
        with pytest.raises(AssertionError, match="PREFIX affixes carry"):
            naming._check_no_prefix_affix_has_a_word()
    finally:
        naming.AFFIX_NAME_WORD = original


def test_that_the_unknown_affix_check_actually_fires():
    original = naming.AFFIX_NAME_WORD
    try:
        naming.AFFIX_NAME_WORD = dict(original)
        naming.AFFIX_NAME_WORD["Chance to explode"] = "the Bang"
        with pytest.raises(AssertionError, match="not an affix"):
            naming._check_the_table_names_nothing_that_is_not_an_affix()
    finally:
        naming.AFFIX_NAME_WORD = original


def test_that_the_shared_word_check_actually_fires():
    original = naming.AFFIX_NAME_WORD
    try:
        naming.AFFIX_NAME_WORD = dict(original, **{"Chance to burn": "Venom"})
        with pytest.raises(AssertionError, match="is used by"):
            naming._check_no_two_affixes_share_a_word()
    finally:
        naming.AFFIX_NAME_WORD = original
