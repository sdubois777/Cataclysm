"""What a dropped item is called.

A rolled item knows its base, its rarity and its affixes, and none of that is a
name a player could read. This turns one into `Superb Robes of Warding`.

THE FORMAT WAS SET BY THE PROJECT OWNER on 2026-08-18, issue #695: "Quality Item
Type of Interesting word that fits the item. For example, Everyday Short Sword of
Malice or Mythic Robes of The Night."

    <rarity>  <base name>  of <word>

THE WORD COMES FROM THE ITEM'S OWN AFFIXES, not from a flavour list. Also the
project owner's choice, from three candidates. It is what Diablo 2 and Path of
Exile do for magic items -- "Warrior's Sword of Fire" is named for what is on it
-- and it means the name tells a player something true rather than being
decoration. A flavour list keyed to the item type was the alternative, and it was
rejected because two very different pieces of Robes could then share a name.

Path of Exile does use a flavour pool, for RARE items, whose names are two
invented words unrelated to what they rolled. That is the shape not taken here.

WHEN AN ITEM HAS NO SUFFIX AFFIX THE NAME STOPS AFTER THE BASE. An Everyday piece
carries one affix and it may be a prefix, so `Everyday Short Sword` is a whole
name. Also the project owner's choice, and it is what Diablo 2 does: a magic item
with only a prefix has no "of" part. The missing words are themselves a signal
that the item is thin.

THE RARITY IS THE FIRST WORD, WHICH IS THE ONE PLACE THIS DEPARTS FROM THE GENRE.
Diablo 2 and Path of Exile put a word from the item's PREFIX affix there. Here the
prefix position is the rarity, so a prefix affix contributes nothing to the name
and only the 54 suffix affixes carry a word.

WHERE THE WORDS LIVE. The Name Word column of the Affixes sheet in
`docs/All_Things_Cataclysm.xlsx`, mirrored into `AFFIX_NAME_WORD` below the way
every other stored value is mirrored into this package.
`tools/tests/test_affix_name_words_match_the_sheet.py` fails when the two
disagree, and the workbook is authoritative.
"""

from __future__ import annotations

from . import affixes as af

#: The word each suffix affix contributes to an item's name.
#:
#: THE WORKBOOK IS AUTHORITATIVE. When the test comparing the two fails, the fix
#: is to change this table rather than the sheet.
#:
#: A PREFIX AFFIX IS ABSENT RATHER THAN EMPTY. The format has no place for one, so
#: a prefix appearing here would be a word that can never be used, and a lookup
#: that found one would be a defect rather than a name.
AFFIX_NAME_WORD: dict[str, str] = {
    # Resistances
    "Single resistance": "Warding",
    "Two resistances": "Twin Wards",
    "All resistances": "the Bulwark",

    # Ailments, named for what they inflict
    "Chance to bleed": "Rending",
    "Chance to burn": "Cinders",
    "Chance to cripple": "Lameness",
    "Chance to disease": "Contagion",
    "Chance to madden": "Madness",
    "Chance to necrose": "Rot",
    "Chance to poison": "Venom",
    "Chance to shred": "Sundering",
    "Chance to stun": "Concussion",
    "Chance to weaken": "Enfeebling",
    "Chance to apply void splinter": "the Void",

    # Attacking
    "Flat critical strike chance": "Precision",
    "Increased critical strike chance": "the Keen Edge",
    "Flat critical strike multiplier": "Brutality",
    "Increased attack speed": "Alacrity",
    "Flat penetration": "Piercing",
    "Increased area of effect": "Reach",
    "Increased efficacy": "the Adept",

    # Damage over time
    "Increased damage over time": "Affliction",
    "Increased damage over time duration": "Lingering",
    "Increased damage over time frequency": "Quickening",

    # Defending
    "Flat block chance": "the Shield",
    "Flat damage reduction": "the Aegis",
    "Flat crowd control resistance": "Steadfastness",
    "Flat retaliation": "Reprisal",

    # Sustaining
    "Flat health regeneration": "Mending",
    "Increased health regeneration": "Recovery",
    "Flat mana regeneration": "Replenishment",
    "Increased mana regeneration": "the Wellspring",
    "Increased energy shield regeneration": "the Barrier",
    "Flat life leech": "the Leech",
    "Flat mana leech": "Drawing",
    "Flat energy shield leech": "Siphoning",

    # Moving
    "Increased movement speed": "Swiftness",
    "Flat cooldown reduction": "Haste",

    # Attributes
    "Increased agility": "the Fox",
    "Increased constitution": "the Bear",
    "Increased ferocity": "the Wolf",
    "Increased mind": "the Sage",
    "Increased spirit": "the Seer",
    "Increased vitality": "the Ox",
    "Increased luck": "the Gambler",

    # Finding things
    "Flat magic find": "Fortune",
    "Increased loot quantity": "Plenty",

    # Minions
    "Increased minion attack speed": "the Master",

    # Hybrids, named for the pairing rather than for either half
    "Attack speed and critical strike chance": "the Duelist",
    "Critical strike chance and multiplier": "the Assassin",
    "Penetration and critical strike multiplier": "the Executioner",
    "Block chance and crowd control resistance": "the Sentinel",
    "Health and mana regeneration": "Renewal",
    "Magic find and loot quantity": "the Magpie",
}


def word_for(affix_name: str) -> str | None:
    """The name word a suffix affix contributes, or None for a prefix."""
    return AFFIX_NAME_WORD.get(affix_name)


def strongest_suffix(item) -> object | None:
    """The affix on `item` whose word the name uses, or None if it has no suffix.

    STRONGEST MEANS HIGHEST TIER, THEN HIGHEST ROLL, THEN FIRST ON THE ITEM. All
    three are needed and the third is the one that matters most: without a final
    tie-break the same item could be called two different things on two runs,
    because the affixes are stored in the order they were drawn and two of them
    can share a tier and a roll.

    TIER BEFORE ROLL, because a tier is worth a seventh of the affix's top value
    and a roll is worth at most a quarter of one tier. A perfect T6 can beat a
    poor T7 in value, but the name follows the tier, which is the number a player
    reads off the item.
    """
    suffixes = [rolled for rolled in item.affixes
                if word_for(rolled.affix.name) is not None]
    if not suffixes:
        return None

    best = suffixes[0]
    for rolled in suffixes[1:]:
        if (rolled.tier, rolled.roll) > (best.tier, best.roll):
            best = rolled
    return best


def name_of(item) -> str:
    """What a rolled item is called.

    `<rarity> <base name>`, and `of <word>` when the item carries a suffix affix.
    """
    name = f"{item.rarity} {item.base.name}"

    strongest = strongest_suffix(item)
    if strongest is None:
        return name
    return f"{name} of {word_for(strongest.affix.name)}"


# --------------------------------------------------------------------------
# Checks that run on import, the same way affixes.py does it.
# --------------------------------------------------------------------------

def _suffix_affix_names() -> set[str]:
    names: set[str] = set()
    for slot in af.GEAR_SLOTS:
        names.update(affix.name for affix in af.everything_for(slot, "suffix"))
    return names


def _prefix_affix_names() -> set[str]:
    names: set[str] = set()
    for slot in af.GEAR_SLOTS:
        names.update(affix.name for affix in af.everything_for(slot, "prefix"))
    return names


def _check_every_suffix_affix_has_a_word() -> None:
    """Or an item could roll a suffix and have nothing to be called after it."""
    missing = _suffix_affix_names() - set(AFFIX_NAME_WORD)
    if missing:
        raise AssertionError(
            f"these suffix affixes have no name word: {sorted(missing)}. An item "
            "rolling one would have no word to take its name from.")


def _check_no_prefix_affix_has_a_word() -> None:
    """The format has no place for one, so a word here could never be used."""
    stray = _prefix_affix_names() & set(AFFIX_NAME_WORD)
    if stray:
        raise AssertionError(
            f"these PREFIX affixes carry a name word: {sorted(stray)}. The first "
            "word of an item's name is its rarity, so a prefix contributes "
            "nothing and this word can never appear.")


def _check_the_table_names_nothing_that_is_not_an_affix() -> None:
    known = _suffix_affix_names() | _prefix_affix_names()
    unknown = set(AFFIX_NAME_WORD) - known
    if unknown:
        raise AssertionError(
            f"these name words are keyed on something that is not an affix: "
            f"{sorted(unknown)}. A renamed affix leaves one of these behind.")


def _check_no_two_affixes_share_a_word() -> None:
    """Two affixes with one word would make the name say less than it looks like.

    A player reading "of Warding" should be able to look it up and find one
    thing.
    """
    seen: dict[str, str] = {}
    clashes: list[str] = []
    for affix_name, word in AFFIX_NAME_WORD.items():
        if word in seen:
            clashes.append(f"{word!r} is used by {seen[word]!r} and {affix_name!r}")
        seen[word] = affix_name
    if clashes:
        raise AssertionError("; ".join(clashes))


_check_every_suffix_affix_has_a_word()
_check_no_prefix_affix_has_a_word()
_check_the_table_names_nothing_that_is_not_an_affix()
_check_no_two_affixes_share_a_word()
