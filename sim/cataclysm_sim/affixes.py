"""Affix families and their values, starting with resistance.

WHAT THIS IS FOR. Issue #79. The regular affix pool does not exist: 961
enchantments are designed and not one ordinary affix. Gear therefore grants no
stats, which is why the Power Score model can assume gear supplies half a
character's power while nothing in the game delivers any of it.

This starts with the resistance families, because those are the ones the two
anchors on #79 actually constrain, and because they are the hardest to get right.

WHY RESISTANCE IS THE HARD CASE. A difficulty tier is a run, and each tier adds a
Cataclysm: at tier 2 the player fights two, at tier 8 all eight at once. So the
number of resistances that matter grows with the tier, from one to all eight, and
the requirement at tier 8 is 560 percentage points rather than 70.

THE THREE FAMILIES, proposed by the project owner:

    single    one resistance,    highest value per type
    hybrid    two resistances,   middle value per type
    all       all eight,         lowest value per type

The point of three families rather than one is that the efficient choice should
change as the run goes on. A single-resistance affix is the best use of a slot
when only one Cataclysm is active and wasted when eight are; an all-resistance
affix is the reverse. `crossover_table()` below shows where each one wins, and
that progression is the reason the structure is worth having at all.

TIER VALUES. The crafting system levels an affix up to T7 -- see the Potency
Crystal in `game/Data/CraftingMaterials.csv` -- so every family needs seven
values. One shared curve produces them from a family's top value, so that the
whole affix pool stays consistent as it grows rather than each family inventing
its own progression. The curve is linear, and the reason is a design pressure
rather than a convenience; see TIER_FRACTIONS.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import damage, enemy_stats, player_power
from .character import (ALL_STATS, ATTRIBUTE_NAMES, RESISTANCE_STATS,
                        SKILL_SLOTS)

#: The eight damage types, so the eight resistances.
DAMAGE_TYPES = ("War", "Demonic", "Death", "Pestilence",
                "Famine", "Celestial", "Chaos", "Void")

#: Stats an affix may grant that are not on the character sheet.
#:
#: `character.py` says plainly that attack damage and attack range live off the
#: sheet, because they belong to the equipped weapon rather than to the
#: character. Damage affixes still have to name attack damage, so it is allowed
#: here and nowhere else. Every other name an affix uses must be a real stat, or
#: an affix could silently grant something nothing reads.
#:
#: THE THREE MINION STATS ARE OFF THE SHEET FOR THE SAME REASON, added with issue
#: #337. They belong to the minion rather than to the character, exactly as
#: attack damage belongs to the weapon. Putting them on the character sheet would
#: be worse than useless: the sheet sums every source that names a stat, so a
#: summoner holding no minions would carry a visible number meaning nothing.
#: `game/Data/MinionScaling.csv` scopes ATTRIBUTE scaling by tag, because Spirit
#: and Agility raise different families. An affix needs no tag, because increased
#: minion damage reaches every minion by definition; a narrower affix would name
#: a narrower stat rather than carry a tag.
OFF_SHEET_STATS = frozenset({"attack_damage", "minion_damage", "minion_health",
                             "minion_attack_speed"})

#: AN ATTRIBUTE IS NOT A STAT, and an affix may still name one.
#:
#: `character.py` keeps the eight attributes in `ATTRIBUTE_EFFECTS`, apart from
#: the stat groups, because an attribute does not hold a value of its own — it
#: turns each point into increases on the two or three stats it drives. So an
#: attribute name fails the "is this a real stat" test while being read by more
#: of the model than most stats are.
#:
#: They are allowed here by name rather than by widening the test, so the guard
#: still refuses anything nothing reads, which is the whole reason it exists.
AFFIXABLE_STATS = (frozenset(ALL_STATS) | OFF_SHEET_STATS
                   | frozenset(ATTRIBUTE_NAMES))

#: Affixes level from T1 to T7. Seven tiers, from the crafting material that
#: raises them.
AFFIX_TIERS = (1, 2, 3, 4, 5, 6, 7)

#: What share of its top-tier value an affix has at each tier.
#:
#: LINEAR, and deliberately so. Every step up is worth the same as every other,
#: which means the value of one more upgrade never falls off.
#:
#: This is a pressure point rather than a neutral choice. The game's central
#: tension is that a day spent at the forge is a day not spent defending the
#: empire, so the decision to upgrade gear rather than run a dungeon has to stay
#: uncomfortable for the whole run. A front-loaded curve, which an earlier
#: version used, hands over most of an affix's value in the first few tiers and
#: makes the later ones easy to skip -- which relieves exactly the pressure the
#: design wants to keep applying.
#:
#: The cost side already curves: gear upgrade levels cost 2^N - 1 stones, so
#: diminishing returns arrive through rising cost rather than through falling
#: value.
TIER_FRACTIONS: dict[int, float] = {tier: tier / 7.0 for tier in range(1, 8)}

#: EVERY AFFIX TIER IS A RANGE, NOT A SINGLE VALUE. A T5 single-resistance affix
#: rolls somewhere between 11.4% and 13.1%, and where it lands is the difference
#: between a good item and one worth rerolling.
#:
#: This is not decoration. Two crafting materials in
#: `game/Data/CraftingMaterials.csv` do nothing at all without it:
#:
#:     Corrupted Mote   "Affix Reroll Currency. Used for Reroll Affix Value."
#:     Primal Spark     "The Perfection Material. Used to perfect the rolls on
#:                       gear."
#:
#: Perfecting a roll is meaningless if a tier has one value, and rerolling a
#: value is meaningless if the reroll cannot change it. Without ranges, an
#: Extremely Rare crafting material is dead content.
#:
#: How far a band reaches below its top, as a fraction of THE AFFIX'S OWN VALUE
#: at that tier. A T7 affix rolls between 75% and 100% of its stated maximum.
#:
#: An earlier version measured this against the gap between tiers instead, which
#: made rolls almost worthless: with seven tiers spanning zero to the maximum,
#: each gap is a seventh of the affix, so the band could never be more than a few
#: percent of its value. A perfect set of resistance affixes saved about one slot
#: out of 72, which is not a difference anyone would craft for.
#:
#: BANDS NOW OVERLAP BETWEEN ADJACENT TIERS, AND THAT IS THE POINT. A perfect T6
#: roll can beat a poor T7 one. With seven tiers there is no way to have both
#: non-overlapping bands and rolls that change a build: a band large enough to
#: matter is necessarily larger than the gap between tiers. Given the choice, a
#: roll that matters is worth more than a clean ordering, because it is what
#: makes a drop worth looking at and what the reroll and perfect crafting actions
#: exist to act on.
#:
#: The overlap is bounded to ONE tier and that is provable rather than tuned. A
#: tier's floor is 0.75 of its own fraction, so tier n is undercut by tier n-1
#: only when n > 4, and by tier n-2 only when n > 8, which cannot happen with
#: seven tiers. So a perfect roll can beat the tier above and never the one above
#: that.
ROLL_BAND_FRACTION = 0.25

#: The character sheet's resistance cap. Reaching it on every active damage type
#: is what a build is trying to do.
#:
#: Re-exported from `damage.py`, which owns it because that is where it is
#: applied, rather than written out again here. Issue #228: it used to be a
#: separate literal in this file, in `player_power.py` and in `character.py`,
#: and nothing compared the four.
RESISTANCE_CAP = damage.RESISTANCE_CAP

#: A character with one hand free -- holding a two-handed weapon, or a single
#: one-handed weapon -- has 18 gear pieces with up to 4 affix slots each, and
#: those slots are shared with enchantments. Filling the offhand adds a piece;
#: see GEAR_PIECES_WITH_AN_OFFHAND below.
GEAR_PIECES = 18
AFFIX_SLOTS_PER_PIECE = 4
TOTAL_AFFIX_SLOTS = GEAR_PIECES * AFFIX_SLOTS_PER_PIECE

#: A filled offhand is a nineteenth piece, so 76 affix slots.
#:
#: Settled by the project owner 2026-08-03 for a second weapon: it is a real
#: piece with its own four affix slots, and it is not exempt.
#:
#: NAMED FOR THE OFFHAND -- MEANING THE SECOND HAND, NOT A CATEGORY OF ITEM --
#: rather than for dual wielding, which is what these two used to be called. A
#: Shield is one of the one-handed weapons and simply grants no attack damage, so
#: a character holding a weapon and a Shield fills the second hand and gets this
#: nineteenth piece exactly as two ordinary one-handers do. "Dual wielding" reads
#: as two damage-dealing weapons, and this is about the hand being occupied at
#: all.
GEAR_PIECES_WITH_AN_OFFHAND = GEAR_PIECES + 1
TOTAL_AFFIX_SLOTS_WITH_AN_OFFHAND = (GEAR_PIECES_WITH_AN_OFFHAND
                                     * AFFIX_SLOTS_PER_PIECE)

#: What a two-handed weapon multiplies its own implicits AND its own affixes by.
#:
#: The project owner's decision, 2026-08-03: "yes dual wielding is a thing. This
#: is compensated for by 2h affixes having more value than 1h affixes."
#:
#: THE VALUE IS DERIVED, NOT CHOSEN. Two one-handed weapons hold eight affix
#: slots against a two-hander's four, so 2.0 is the figure that makes the two
#: loadouts worth the same in affixes. That is not a preference: section VII of
#: the design document already requires it, stating that two one-handed weapons
#: count as one equipped piece for Power Score so that dual wielding is not worth
#: free power. The rating model deliberately scores the two loadouts the same, so
#: whichever side had the larger affix budget would carry power its rating does
#: not count. `_check_the_two_loadouts_have_equal_affix_value` asserts the
#: equality rather than trusting the arithmetic.
#:
#: IT APPLIES TO IMPLICITS AS WELL AS AFFIXES, and that is the part that came
#: from research rather than from measurement. Last Epoch balances two-handed
#: weapons by giving them an inherent bonus to their affixes *and* their implicit
#: stats. A weapon's base damage is an implicit here, so one multiplier covers
#: both, and no weapon base damage number needs changing.
#:
#: Without the implicit half the two-hander is strictly worse. Two one-handed
#: bases sum to more than any two-handed base -- an Axe and a Sword give 86
#: against a Greatsword's 78 -- so with only the affix half it loses on damage
#: while also holding one fewer damage type. Reaching the SAME edge through the
#: affix half alone needs a multiplier of 3.40, which hands the two-hander 13.6
#: affix slots-worth on the weapon against the dual wielder's 8: the same free
#: power section VII forbids, pointed the other way.
#:
#: That figure used to read "near 2.75, which hands the two-hander three affix
#: slots". It was never computed from anything. It is now solved by
#: `solve_affix_only_for_the_same_edge` in
#: `sim/analyse_two_handed_multiplier.py`, and checked against this comment by
#: `sim/tests/test_analysis_scripts.py`. Issue #319.
#:
#: What it produces, measured in `sim/analyse_two_handed_multiplier.py`: the
#: two-hander deals 1.29x per hit and about 1.22x per second, the dual wielder
#: holds a fourth damage type and a wider spread of affixes, and the affix
#: budgets are exactly equal.
#:
#: THOSE TWO RATIOS FELL FROM 1.33 AND 1.26 WITH ISSUE #511, and the multiplier
#: itself did not move. The flat damage affix rose from 18 to 22 there, so the
#: affixes supply more of the base bracket and the weapon's doubled implicit is a
#: smaller share of it. The edge is still the implicit half doing the work.
TWO_HANDED_MULTIPLIER = 2.0


def two_handed_multiplier(hands: int) -> float:
    """What a weapon of this many hands multiplies its own values by."""
    if hands not in (1, 2):
        raise ValueError(f"a weapon has 1 or 2 hands, not {hands}")
    return TWO_HANDED_MULTIPLIER if hands == 2 else 1.0

#: The eight item rarities, weakest first. Index + 1 is the rarity number the
#: Power Score model uses, so Everyday is 1 and Cataclysmic is 8.
#:
#: Spelled as `player_power.RARITIES` spells them, which is also how the Gems
#: sheet spells its eight columns. The design document writes "Mythic" for the
#: sixth; the data writes "Mythical". That disagreement is filed rather than
#: quietly resolved here.
RARITIES: tuple[str, ...] = ("Everyday", "Quality", "Superb", "Masterful",
                             "Legendary", "Mythical", "Ascendant", "Cataclysmic")

#: What each rarity IS: how many of its four slots hold an enchantment, and how
#: many hold a regular affix.
#:
#: RARITY IS NOT A PROPERTY AN ITEM CARRIES. It is a label computed from what
#: fills the slots, stated by the project owner 2026-08-03: an item that drops
#: with an enchantment is a Legendary; one that drops with three regular affixes
#: is a Superb. So `rarity_of` is the definition and this table is its inverse.
#:
#: Two consequences follow, and both are the design working rather than an
#: accident.
#:
#: ADDING AN AFFIX PROMOTES THE PIECE. An Everyday item with an affix added
#: becomes a Quality item. Applying an enchantment to a Masterful item makes it
#: Legendary, and the enchantment takes an affix's slot, which is the trade the
#: design describes -- "players must choose between stacking powerful
#: enchantments or filling slots with standard affixes".
#:
#: A CATACLYSMIC ITEM HAS NO REGULAR AFFIXES. All four of its slots hold
#: enchantments. So the 72-slot affix budget is what eighteen MASTERFUL pieces
#: reach, not eighteen Cataclysmic ones. The Expected Character by Tier table in
#: the design document still says a tier 8 character is fully Cataclysmic, which
#: no longer describes the character every affix value was fitted against; that
#: is issue #125, and the project owner's reading is that a top build is a mix
#: of regular affixes and enchantments rather than all of either.
#:
#: The genre puts affix count on rarity the same way. Path of Exile gives a
#: normal item no affixes, a magic item at most one prefix and one suffix, and a
#: rare item three to six; Diablo 4 gives a magic item one or two and a rare item
#: three. What is particular here is that enchantments share the same four slots,
#: so the top four rarities trade affixes away rather than adding capacity.
RARITY_COMPOSITION: dict[str, tuple[int, int]] = {
    #             enchantments, regular affixes
    "Everyday":    (0, 1),
    "Quality":     (0, 2),
    "Superb":      (0, 3),
    "Masterful":   (0, 4),
    "Legendary":   (1, 3),
    "Mythical":    (2, 2),
    "Ascendant":   (3, 1),
    "Cataclysmic": (4, 0),
}

#: The four rarities reachable by adding regular affixes, weakest first. Index
#: + 1 is the affix count, so one affix is Everyday and four is Masterful.
CRAFTABLE_RARITIES: tuple[str, ...] = RARITIES[:4]

#: The lowest rarity carrying an enchantment. Below it a piece has none.
FIRST_ENCHANTABLE_RARITY = RARITIES[4]


def affix_slots_for(rarity: str) -> int:
    """How many REGULAR affixes a piece of this rarity carries.

    Four at Masterful and falling from there, because the top four rarities
    spend slots on enchantments instead. A Cataclysmic piece carries none.
    """
    return _composition(rarity)[1]


def enchantments_for(rarity: str) -> int:
    """How many of the four slots hold an enchantment."""
    return _composition(rarity)[0]


def _composition(rarity: str) -> tuple[int, int]:
    if rarity not in RARITY_COMPOSITION:
        raise ValueError(f"{rarity!r} is not a rarity; expected one of "
                         f"{list(RARITIES)}")
    return RARITY_COMPOSITION[rarity]


def rarity_of(enchantments: int, affixes: int) -> str:
    """What a piece carrying this many enchantments and affixes IS.

    THE DEFINITION, not a lookup. Stated by the project owner 2026-08-03: an
    item that drops with an enchantment is a Legendary; one that drops with
    three regular affixes is a Superb. Rarity is a label for the contents rather
    than a property the item carries, which is why adding an affix promotes the
    piece and applying an enchantment promotes it further.
    """
    if enchantments < 0 or affixes < 0:
        raise ValueError(
            f"{enchantments} enchantments and {affixes} affixes; neither can "
            "be negative")
    total = enchantments + affixes
    if not 1 <= total <= AFFIX_SLOTS_PER_PIECE:
        raise ValueError(
            f"{enchantments} enchantments and {affixes} affixes fill {total} "
            f"slots; a piece has 1 to {AFFIX_SLOTS_PER_PIECE} filled")

    for rarity, composition in RARITY_COMPOSITION.items():
        if composition == (enchantments, affixes):
            return rarity
    raise ValueError(
        f"{enchantments} enchantments and {affixes} affixes is not a rarity. "
        f"Below {FIRST_ENCHANTABLE_RARITY} a piece fills only as many slots as "
        "it has affixes; from there it fills all four.")


def rarity_for_affix_count(count: int) -> str:
    """The rarity a piece with this many affixes and NO enchantments is.

    The crafting path: adding an affix promotes the piece, so an Everyday item
    with an affix added becomes a Quality one. It stops at Masterful, because a
    Masterful piece already fills all four slots and there is no fifth. Reaching
    Legendary means applying an enchantment, which takes an affix's slot rather
    than adding one.
    """
    if not 1 <= count <= AFFIX_SLOTS_PER_PIECE:
        raise ValueError(
            f"{count} affixes is outside 1-{AFFIX_SLOTS_PER_PIECE}; a piece "
            "with none is not an item and there is no fifth slot")
    return rarity_of(0, count)


def prefix_suffix_split(slots: int) -> tuple[int, int]:
    """How a slot count divides into prefixes and suffixes, at its most even.

    Two of each remain the caps. One slot is a prefix, two are one of each, and
    three are two prefixes and one suffix. A drop may legitimately go the other
    way with three -- one prefix and two suffixes -- so this returns the shape
    rather than a rule; what it fixes is that neither side exceeds its cap and
    that the two sum to the slot count.
    """
    if not 0 <= slots <= AFFIX_SLOTS_PER_PIECE:
        raise ValueError(f"{slots} slots is outside 0-{AFFIX_SLOTS_PER_PIECE}")
    prefixes = min(PREFIXES_PER_PIECE, (slots + 1) // 2)
    return prefixes, slots - prefixes


#: An affix is a prefix or a suffix, and the two draw from separate pools.
#:
#: WHAT THIS BUYS. Without the split, four slots means four of whatever is
#: strongest, and one item can carry a whole build. With it, an item's four slots
#: are two of each, so every piece has to give up something. That is the trade
#: that makes reading a drop interesting rather than arithmetic.
#:
#: All three games surveyed use it: Path of Exile, Last Epoch and Torchlight
#: Infinite all split prefixes from suffixes with mutually exclusive pools.
#:
#: WHICH GOES WHERE. Prefixes carry magnitude -- how big a character's numbers
#: are. Suffixes carry rates and qualifiers -- how often, how fast, how much
#: gets through. That is the convention in all three, and it groups the stats
#: that compete with each other so a choice inside one group is a real one.
PREFIX = "prefix"
SUFFIX = "suffix"
AFFIX_POSITIONS = (PREFIX, SUFFIX)

#: Two of each per piece, which is what makes four slots a trade rather than a
#: total. The same split Last Epoch uses.
PREFIXES_PER_PIECE = 2
SUFFIXES_PER_PIECE = 2

#: The equipped pieces and how many of each, from the Item Slots list in the
#: design document. Potion slots are consumables rather than gear: they hold
#: gems and carry no affixes, which is why these sum to 18 and not 22.
GEAR_SLOTS: dict[str, int] = {
    "Head": 1, "Chest": 1, "Shoulders": 1, "Gloves": 1, "Pants": 1,
    "Boots": 1, "Belt": 1, "Ring": 8, "Necklace": 1, "Relic": 1, "Weapon": 1,
}

#: Which slots each kind of affix can appear on.
#:
#: WHY RESTRICT AT ALL. Without it every slot is interchangeable and gearing has
#: no puzzle in it: a player fills all 72 slots with whatever is strongest and
#: never has to trade one thing for another. Restrictions are what create the
#: familiar problem of needing resistance from a helmet because the weapon
#: cannot provide it.
#:
#: This is not a new mechanism. `game/Config/Tags/CataclysmTags.ini` already
#: generates 14 Item.Slot tags and three enchantments already restrict
#: themselves with them.
#:
#: Rings are deliberately in every list. There are eight of them, so they are
#: the flexible slots a build uses to fix whatever it is short of, which is what
#: makes them worth chasing.
#: Shoulders were missing from the defensive list until the pool was built out.
#: It was an oversight rather than a decision: shoulders are armour, and without
#: them the slot could roll nothing but resistance and energy shield, leaving it
#: unable to fill its own four affix slots. The import-time check
#: `_check_every_slot_can_fill_all_four_of_its_affixes` is what found it.
OFFENSIVE_SLOTS = frozenset({"Weapon", "Ring", "Relic", "Necklace", "Gloves"})
DEFENSIVE_SLOTS = frozenset({"Head", "Chest", "Shoulders", "Belt", "Pants",
                             "Boots", "Ring"})
#: Everything except the weapon. Armour and jewellery defend; a weapon does not.
RESISTANCE_SLOTS = frozenset(GEAR_SLOTS) - {"Weapon"}


#: How much one gear upgrade level adds to every affix on that piece, as a
#: fraction of the affix's unupgraded value. A +10 piece is about 3.52 times a
#: +0 one. It is not a round number because it is DERIVED from the tier anchors
#: rather than chosen.
#:
#: NOT A SECOND NUMBER. It is read from `player_power.WEIGHTS`, where it is
#: already used for exactly this: `Cataclysm_GDD_v2.md` says gear upgrade level
#: MULTIPLIES gear rarity rather than adding to it, and the Power Score model
#: implements that as `rarity * (1 + factor * level)`. Duplicating the constant
#: here would let the two drift, which has happened before elsewhere in this
#: project.
#:
#: EVERY affix on the piece, which the project owner chose on 2026-08-03 after
#: being shown the measurement below.
#:
#: A KNOWN IMBALANCE, ACCEPTED AND RECORDED. Gear level multiplies both brackets
#: of the pipeline at once, so its effect on final damage is roughly squared,
#: while Power Score counts it once. Measured with everything else held at tier 8
#: maximum, going from +0 to +10 multiplies damage by 9.58 and Power Score by
#: only 1.56, so hits-to-kill a Common enemy falls from 19.1 to 2.0. Gear level
#: is therefore over-rewarded relative to what a character is rated at.
#:
#: The project owner's decision was to proceed anyway and tune against real play:
#: "We'll figure out how to make it work, for now let's just continue forward.
#: Numbers and stuff can be changed once we have a working prototype and can see
#: how it plays." The alternatives measured at the time were applying it to the
#: flat bracket only, which lands at 1.64 against a target of 1.56, or cutting
#: the factor to 0.0268, which makes a +10 piece only 1.27 times a +0 one.
GEAR_LEVEL_FACTOR = player_power.WEIGHTS["upgrade_factor"]
MAX_GEAR_LEVEL = player_power.MAX_UPGRADE


def gear_level_multiplier(gear_level: int) -> float:
    """What a piece at this upgrade level does to every affix it carries."""
    if not 0 <= gear_level <= MAX_GEAR_LEVEL:
        raise ValueError(
            f"gear level {gear_level} outside 0-{MAX_GEAR_LEVEL}")
    return 1.0 + GEAR_LEVEL_FACTOR * gear_level


def slots_available_to(allowed: frozenset[str]) -> int:
    """How many affix slots a family restricted to these pieces can occupy."""
    return sum(count * AFFIX_SLOTS_PER_PIECE
               for slot, count in GEAR_SLOTS.items() if slot in allowed)


def tier_band(top_value: float, tier: int) -> tuple[float, float]:
    """The lowest and highest an affix can roll at a tier.

    Shared by every affix family so the whole pool uses one curve and one band
    width. A family that computed its own would drift from the rest the moment
    either constant changed.
    """
    if tier not in TIER_FRACTIONS:
        raise ValueError(f"affix tier {tier} outside {sorted(TIER_FRACTIONS)}")
    high = top_value * TIER_FRACTIONS[tier]
    return (high * (1.0 - ROLL_BAND_FRACTION), high)


def roll_within(low: float, high: float, roll: float) -> float:
    """Place a roll inside a band. Clamped, so a bad caller cannot leave it."""
    return low + (high - low) * min(max(roll, 0.0), 1.0)


def affix_value(top_value: float, tier: int, roll: float = 1.0,
                gear_level: int = MAX_GEAR_LEVEL) -> float:
    """One affix's value: its tier band, its roll, and the piece's upgrade level.

    Defaults to a fully upgraded piece, because the questions this module answers
    are about what a finished character reaches. The stated top values are
    therefore the +10 figures; a +0 piece gives 1/3.525 of them.
    """
    low, high = tier_band(top_value, tier)
    at_zero = roll_within(low, high, roll) / gear_level_multiplier(MAX_GEAR_LEVEL)
    return at_zero * gear_level_multiplier(gear_level)


@dataclass(frozen=True)
class AffixFamily:
    """One family of affixes: what it grants, to how many things, and how much.

    `breadth` is how many resistances a single roll covers. `top_value` is the
    percentage it grants to each of them at T7.
    """

    name: str
    breadth: int
    top_value: float
    #: Resistance is a suffix in every game surveyed, and it fits the rule: it
    #: says how much of a hit gets through rather than how big a number is.
    position: str = SUFFIX

    def range_at(self, tier: int) -> tuple[float, float]:
        """The lowest and highest this affix can roll at a tier.

        The top of a tier's band is its share of the family's top value, so a
        perfect T7 roll is exactly the family's stated maximum. The band reaches
        down by a fraction of that value, not of the gap to the tier below, which
        is what makes the roll worth caring about. See ROLL_BAND_FRACTION.
        """
        return tier_band(self.top_value, tier)

    def value_at(self, tier: int, roll: float = 1.0,
                 gear_level: int = MAX_GEAR_LEVEL) -> float:
        """Percentage granted to each covered resistance.

        `roll` places the result inside the tier's band: 0.0 is the worst
        possible roll, 1.0 the best. `gear_level` is the piece's upgrade level,
        which multiplies the result. Both default to the best case, because the
        questions this module answers -- how many slots to reach the resistance
        cap -- are about what a finished, crafted character can achieve.
        """
        return affix_value(self.top_value, tier, roll, gear_level)

    def average_at(self, tier: int) -> float:
        """The middle of the band. What an uncrafted drop is worth on average."""
        return self.value_at(tier, roll=0.5)

    def total_coverage(self, tier: int, roll: float = 1.0,
                       gear_level: int = MAX_GEAR_LEVEL) -> float:
        """Percentage points granted across everything it covers.

        Broader families grant less per type and more in total, which is what
        makes the choice a real one rather than a strict ordering.
        """
        return self.value_at(tier, roll, gear_level) * self.breadth

    def useful_coverage(self, tier: int, active_cataclysms: int,
                        roll: float = 1.0,
                        gear_level: int = MAX_GEAR_LEVEL) -> float:
        """Coverage that is not wasted.

        An all-resistance affix covers eight damage types. If only two are
        attacking, six of those are worth nothing, and this is the whole reason
        the efficient family changes as a run goes on.
        """
        used = min(self.breadth, max(0, active_cataclysms))
        return self.value_at(tier, roll, gear_level) * used


#: Proposed families. Per-type value falls as breadth rises, which the project
#: owner specified; total coverage rises, which is what stops the narrow family
#: being strictly better.
SINGLE_RESISTANCE = AffixFamily("Single resistance", breadth=1, top_value=20.0)
HYBRID_RESISTANCE = AffixFamily("Two resistances", breadth=2, top_value=14.0)
ALL_RESISTANCE = AffixFamily("All resistances", breadth=8, top_value=6.0)

RESISTANCE_FAMILIES = (SINGLE_RESISTANCE, HYBRID_RESISTANCE, ALL_RESISTANCE)


# --------------------------------------------------------------------------
# Health and damage, which have no breadth but do have two kinds
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class StatAffix:
    """An affix granting one ordinary stat, either flat or as an increase.

    Resistance needed three families because it has a breadth axis -- one type,
    two, or all eight. Health and damage have no such axis. What they have
    instead is the two ends of the stat pipeline:

        Final = (class base + per-level + FLAT from gear) * (1 + INCREASES)

    A flat affix enters the first bracket and an increased affix the second, and
    which is worth more depends on how large the first bracket already is. That
    produces a crossover of the same shape as the resistance one, and for the
    same reason: neither kind is strictly better, so the choice is real.
    """

    name: str
    stat: str
    kind: str          # "flat" or "increased"
    top_value: float
    allowed_slots: frozenset[str] = frozenset(GEAR_SLOTS)
    position: str = PREFIX

    def __post_init__(self) -> None:
        if self.kind not in ("flat", "increased"):
            raise ValueError(
                f"{self.name}: kind must be 'flat' or 'increased', "
                f"got {self.kind!r}")
        if self.position not in AFFIX_POSITIONS:
            raise ValueError(
                f"{self.name}: position must be one of {list(AFFIX_POSITIONS)}, "
                f"got {self.position!r}")
        if self.stat not in AFFIXABLE_STATS:
            raise ValueError(
                f"{self.name} grants {self.stat!r}, which is neither on the "
                "character sheet nor one of the off-sheet stats an affix may "
                f"name: {sorted(OFF_SHEET_STATS)}")
        unknown = set(self.allowed_slots) - set(GEAR_SLOTS)
        if unknown:
            raise ValueError(
                f"{self.name} allows slots that do not exist: {sorted(unknown)}")
        if not self.allowed_slots:
            raise ValueError(f"{self.name} can appear on no slot at all")

    def slots_available(self) -> int:
        return slots_available_to(self.allowed_slots)

    def range_at(self, tier: int) -> tuple[float, float]:
        return tier_band(self.top_value, tier)

    def value_at(self, tier: int, roll: float = 1.0,
                 gear_level: int = MAX_GEAR_LEVEL,
                 two_handed: bool = False) -> float:
        """This affix's value on a piece.

        `two_handed` applies TWO_HANDED_MULTIPLIER, which is what balances four
        affix slots on a two-handed weapon against eight across two one-handed
        ones. It is only ever true for an affix rolled on a two-handed weapon.
        """
        value = affix_value(self.top_value, tier, roll, gear_level)
        return value * (TWO_HANDED_MULTIPLIER if two_handed else 1.0)

    def added_value(self, base_before_increases: float, existing_increases: float,
                    tier: int = 7, roll: float = 1.0) -> float:
        """How much final stat one more of this affix actually adds.

        This is the only fair way to compare the two kinds, because each is
        worth more in the presence of the other. A flat affix is multiplied by
        every increase already on the character; an increased affix multiplies
        every flat point already there.
        """
        v = self.value_at(tier, roll)
        if self.kind == "flat":
            return v * (1.0 + existing_increases)
        return base_before_increases * (v / 100.0)


#: Health. A flat affix adds points before scaling; an increased one scales
#: everything already there.
#:
#: The two are set so the crossover lands mid-build rather than at either end. A
#: level 100 Ravager has 2,110 base health, and with every attribute point in
#: Vitality carries +200% increases before any gear. Under those conditions the
#: two kinds are worth the same at about 3,000 points of base, which a character
#: reaches after roughly seven flat affixes. Before that flat wins, after it
#: increased does.
FLAT_HEALTH = StatAffix("Flat maximum health", "max_health", "flat", 120.0,
                        DEFENSIVE_SLOTS, PREFIX)
INCREASED_HEALTH = StatAffix("Increased maximum health", "max_health",
                             "increased", 12.0, DEFENSIVE_SLOTS, PREFIX)

#: How many non-critical hits an average Common enemy should take to kill. The
#: project owner set the range 1 to 3; this is the middle of it. It is the only
#: player-facing number in this file that is chosen rather than derived, and it
#: is what `damage_target()` turns into a damage figure.
#:
#: A Common enemy is the right thing to anchor on rather than a boss, because the
#: spread between the two is 117 times and no single hits-to-kill figure can suit
#: both. Trash is what the player fights almost all of the time.
HITS_TO_KILL_A_COMMON_ENEMY = 2.0

#: The build the damage values are fitted against: how many affix slots a
#: character is assumed to spend on each kind of damage affix at tier 8.
#:
#: NOT A CAP. There are 48 offensive affix slots and nothing stops a player using
#: more, in which case they exceed the target, which is what heavy investment is
#: for. This is the ordinary case the numbers are tuned around, and it is stated
#: here rather than buried in a calculation because every damage figure in this
#: file moves if it changes.
REFERENCE_FLAT_DAMAGE_AFFIXES = 6
REFERENCE_INCREASED_DAMAGE_AFFIXES = 6


def damage_target(tier: int = 8) -> float:
    """Damage per non-critical hit a player needs at a difficulty tier.

    An OUTPUT of the enemy design, not an input to it. `enemy_stats.py` sets
    enemy health on its own terms; this reads it back and asks what has to get
    through it. An earlier version of this file went the other way round, taking
    a player damage figure derived from player-side targets, and that is what
    made the project owner's 125% increased damage affix impossible to fit.

    Measured against the baseline archetype, which is the average enemy of a
    rarity rather than any particular creature.

    IT GOES THROUGH EVERY MITIGATION LAYER THE CREATURE HAS, since issue #511.
    Until then it divided health by hits and applied nothing, so it answered
    "how much health has to be removed" rather than "how much damage has to be
    dealt to remove it". The average Common enemy carries 673 armour at tier 8,
    which stops 9.52% of a hit, so the figure was 10.5% low: 1,683 where the
    answer is 1,860.

    THE ERROR WAS SMALL HERE AND LARGE ELSEWHERE, and that is not luck. The
    average Common enemy carries the least armour of anything in the slice --
    `ARMOR_AT_COMMON` is 0.10 and the baseline archetype's `armor_share` is 1.00
    -- so this is the mildest case of the same mistake issue #481 fixed in
    `player_damage_to_kill_in`, which understated by 48% against the Abyssal
    Warden.

    IT ANCHORS EVERY OFFENSIVE NUMBER IN THIS FILE. `damage_for_slot` scales it
    per skill slot, `weapon_base_damage_needed` solves the damage pipeline
    backwards from it, and `reference_weapon_base` feeds the crossover argument
    that sets `FLAT_DAMAGE`. All of them rose by the same 10.5%, which is why
    `FLAT_DAMAGE` and `INCREASED_DAMAGE` did not have to move: the target and the
    weapon base moved together, so the crossover sits in the same place in a
    build. `tests/test_affixes.py` re-checks that rather than assuming it.
    """
    common = enemy_stats.stats_on_floor("Common", tier, "Cataclysm")
    return enemy_stats.player_damage_to_kill_in(
        common, HITS_TO_KILL_A_COMMON_ENEMY)


#: Damage. Increased damage is 125% at T7, set by the project owner.
#:
#: THE 125% IS WHAT FORCES FLAT DAMAGE TO BE SMALL. A character with eight
#: increased damage affixes is already multiplying by 11, so the bracket those
#: multiply has to be around 150 at tier 8 to land on `damage_target()`. Flat
#: damage was 60, which meant three affixes alone filled the whole bracket and
#: the weapon had nothing left to contribute.
#:
#: 22 IS DERIVED, NOT PICKED, AND TWO SEPARATE THINGS PIN IT.
#:
#: FIRST, THE WEAPON TERM HAS TO BE A WEAPON A PLAYER CAN HOLD.
#: `reference_weapon_base` solves the pipeline backwards from `damage_target()`
#: and says what the weapon and skill together must supply once the reference
#: six-and-six build's affixes are counted. The reference loadout is two
#: one-handed weapons -- an Axe and a Sword, the design document's own example --
#: which supply 86 between them. At 22 the pipeline asks for 86.86, so the pair
#: lands on it within one per cent.
#:
#: IT WAS 18 UNTIL ISSUE #511, AND THAT ONLY WORKED BECAUSE THE TARGET WAS 10.5%
#: LOW. `damage_target` applied no enemy mitigation, so it asked for 1,683 where
#: the answer is 1,860. At the corrected target and a flat value of 18 the
#: pipeline asks the weapon for 110.86, and no legal pair of one-handers reaches
#: it -- the strongest, an Axe with an Axe, supplies 92. The choice was between
#: moving this number and rewriting the weapon damage table, and this is the one
#: the project derives rather than publishes.
#:
#: SECOND, THE CHOICE BETWEEN THE TWO KINDS HAS TO BE REAL, which is the entire
#: point of having both. `crossover_base` with eight increased affixes in place
#: puts them at equal value at 194 points of base, and a build starting from the
#: pair's 86 crosses that after three flat affixes. So flat wins early and
#: increased wins later, and a character actually takes some of each.
#:
#: Both neighbouring values fail the second test. At 60 the crossover is 528 and
#: flat still wins after six flat affixes; at 12 it is 106, below where a real
#: build starts, so increased wins from the first affix. Either way one of the
#: two kinds is dead content.
#:
#: Flat damage being far smaller than flat health is not an inconsistency. The
#: two stats have different multiplier scales by design: 125% per damage affix
#: against 12% per health affix.
FLAT_DAMAGE = StatAffix("Flat damage", "attack_damage", "flat", 22.0,
                        OFFENSIVE_SLOTS, PREFIX)
INCREASED_DAMAGE = StatAffix("Increased damage", "attack_damage",
                             "increased", 125.0, OFFENSIVE_SLOTS, PREFIX)

#: The minion affixes. Issue #337.
#:
#: EACH MIRRORS AN EXISTING AFFIX EXACTLY, so no new power enters the game. A
#: minion build gives up the weapon damage affix, which is worth nothing to it,
#: and takes the minion one at the same value on the same slots. That is the
#: whole design: a swap, not an addition.
#:
#: THE DAMAGE ONE IS 125 AND THE HEALTH ONE IS 12, WHICH IS NOT AN INCONSISTENCY.
#: The increased affixes do not share one standard value -- damage and spell
#: damage are 125, defensive and attribute affixes are 12, attack speed is 15.
#: A minion damage affix at 12 would be a tenth of its weapon counterpart and the
#: archetype would not function.
#:
#: MINION HEALTH SITS ON DEFENSIVE SLOTS DELIBERATELY. It makes a summoner spend
#: the slots that would have kept them alive on keeping the army alive, which is
#: the real cost of the archetype and should be a visible trade rather than a
#: hidden one.
INCREASED_MINION_DAMAGE = StatAffix("Increased minion damage", "minion_damage",
                                    "increased", 125.0,
                                    INCREASED_DAMAGE.allowed_slots, PREFIX)
INCREASED_MINION_HEALTH = StatAffix("Increased minion health", "minion_health",
                                    "increased", 12.0,
                                    INCREASED_HEALTH.allowed_slots, PREFIX)

HEALTH_AFFIXES = (FLAT_HEALTH, INCREASED_HEALTH)
DAMAGE_AFFIXES = (FLAT_DAMAGE, INCREASED_DAMAGE)


# --------------------------------------------------------------------------
# The rest of the pool
# --------------------------------------------------------------------------
#
# WHY THESE STATS AND NOT OTHERS. Every stat on the character sheet that a piece
# of equipment could plausibly grant.
#
# NO AFFIX HERE GRANTS A PRIMARY ATTRIBUTE, AND THAT IS NOW A GAP RATHER THAN A
# RULE. This comment used to say the absence was deliberate, because the design
# gave attribute points from levelling and from the Maw and never from gear. The
# project owner reversed that on 2026-08-04: attributes must be slottable on
# gear. Nothing has been added yet -- flat or increased, prefix or suffix, which
# slots, and whether a hybrid grants two are still open, and an attribute affix
# has to be priced against what the Maw already gives rather than against the
# hundred points a character earns by reaching level 100. Issue #204 carries it.
#
# `AFFIXABLE_STATS` does not admit an attribute name today, so adding one means
# widening that set as well; `StatAffix.__post_init__` rejects anything outside
# it.
#
# HOW THE VALUES WERE SET. Not one formula, because the stats are not on one
# scale. Three anchors, and each affix below says which it used:
#
#   against the class base   for stats a class already has, the top value is
#                            about 6% of what a level 100 character carries, the
#                            ratio flat health already used: 120 against 2,110
#   against the requirement  for stats whose class base is near zero but whose
#                            endgame requirement is large, armour above all
#   by convention            for percentages with no base at all, where the only
#                            sensible anchor is how many slots should reach a
#                            useful figure
#
# All of these are top-tier values on a fully upgraded piece, and all of them are
# expected to move once the game is playable.

# -- Prefixes: how big a character's numbers are --------------------------

MANA_SLOTS = frozenset({"Head", "Chest", "Belt", "Ring", "Necklace", "Relic"})
SHIELD_SLOTS = frozenset({"Head", "Chest", "Shoulders", "Ring", "Relic"})
EVASION_SLOTS = frozenset({"Head", "Chest", "Shoulders", "Gloves", "Pants",
                           "Boots", "Ring"})

#: Against the class base: a level 100 character carries 644 mana, and 6% is 38.
FLAT_MANA = StatAffix("Flat maximum mana", "max_mana", "flat", 38.0,
                      MANA_SLOTS, PREFIX)
INCREASED_MANA = StatAffix("Increased maximum mana", "max_mana", "increased",
                           12.0, MANA_SLOTS, PREFIX)

#: Against the class base: the Ritualist, the only class with a shield, carries
#: 832, and 6% is 50. Restricted to the pieces a caster's protection sits on.
FLAT_ENERGY_SHIELD = StatAffix("Flat maximum energy shield", "max_energy_shield",
                               "flat", 50.0, SHIELD_SLOTS, PREFIX)
INCREASED_ENERGY_SHIELD = StatAffix("Increased maximum energy shield",
                                    "max_energy_shield", "increased", 12.0,
                                    SHIELD_SLOTS, PREFIX)

#: AGAINST THE REQUIREMENT, not the class base, and this is the one place the
#: two disagree enough to matter. A Ravager has 371 armour, but the armour curve
#: divides by 800 times the difficulty tier, so 6,400 armour is worth half
#: damage taken at tier 8 and 371 is worth 5%. Six percent of the class base
#: would be 22 per affix, which fifteen slots could never turn into anything.
#: At 250 a defensive set reaches a figure that matters, which is exactly what
#: the design means when it says armour earned early does not keep its value and
#: gear has to carry it.
FLAT_ARMOR = StatAffix("Flat armor", "armor", "flat", 250.0,
                       DEFENSIVE_SLOTS, PREFIX)
INCREASED_ARMOR = StatAffix("Increased armor", "armor", "increased", 12.0,
                            DEFENSIVE_SLOTS, PREFIX)

#: By convention: evasion is a percentage with a soft cap of 60 and no class
#: has any, so gear supplies all of it. At 4 points a piece, fifteen slots reach
#: the cap, which is a real investment rather than an incidental one.
FLAT_EVASION = StatAffix("Flat evasion", "evasion", "flat", 4.0,
                         EVASION_SLOTS, PREFIX)
INCREASED_EVASION = StatAffix("Increased evasion", "evasion", "increased", 12.0,
                              EVASION_SLOTS, PREFIX)

#: Parallel to increased damage, and for the same reason. The Ritualist carries
#: 158 spell damage, so six of these produce a figure in the same range as the
#: damage target. There is no flat spell damage: the class supplies that base,
#: where attack damage comes from the weapon.
INCREASED_SPELL_DAMAGE = StatAffix("Increased spell damage", "spell_damage",
                                   "increased", 125.0, OFFENSIVE_SLOTS, PREFIX)

#: Against the class base: 100 to 150 depending on class, and 6% is 7.
FLAT_CLASS_RESOURCE = StatAffix("Flat maximum class resource", "class_resource",
                                "flat", 7.0,
                                frozenset({"Belt", "Ring", "Relic", "Necklace"}),
                                PREFIX)

# -- Suffixes: how often, how fast, how much gets through -----------------

RECOVERY_SLOTS = frozenset({"Chest", "Belt", "Boots", "Ring", "Necklace"})
UTILITY_SLOTS = frozenset({"Boots", "Belt", "Ring", "Necklace", "Relic"})

#: Against the class base: 15.85 health and 10.9 mana regeneration at level 100,
#: and 6% of each. Energy shield regeneration is anchored on the Ritualist's 21.8.
FLAT_HEALTH_REGEN = StatAffix("Flat health regeneration", "health_regen",
                              "flat", 0.95, RECOVERY_SLOTS, SUFFIX)
INCREASED_HEALTH_REGEN = StatAffix("Increased health regeneration",
                                   "health_regen", "increased", 12.0,
                                   RECOVERY_SLOTS, SUFFIX)
FLAT_MANA_REGEN = StatAffix("Flat mana regeneration", "mana_regen", "flat",
                            0.65, RECOVERY_SLOTS, SUFFIX)
INCREASED_MANA_REGEN = StatAffix("Increased mana regeneration", "mana_regen",
                                 "increased", 12.0, RECOVERY_SLOTS, SUFFIX)
INCREASED_ENERGY_SHIELD_REGEN = StatAffix(
    "Increased energy shield regeneration", "energy_shield_regen", "increased",
    12.0, SHIELD_SLOTS, SUFFIX)

#: LEECH IS A PERCENTAGE OF THE DAMAGE ACTUALLY DEALT, paid out over 3 seconds
#: and not counting overkill. `Cataclysm_GDD_v2.md` has the rule under "Leech".
#:
#: By convention: the Ravager's 3% is the only leech any class has, and leech
#: compounds with every point of damage a character stacks, so it stays small.
FLAT_LIFE_LEECH = StatAffix("Flat life leech", "life_leech", "flat", 0.5,
                            OFFENSIVE_SLOTS, SUFFIX)

#: Mana and energy shield leech, added for issue #214. The design already relied
#: on both: the Energy Leech class drains enemy mana to refill its own pool, and
#: leech is named as a recovery stat group and as a suffix category. Only life
#: leech existed, so neither class could be geared for.
#:
#: ALL THREE HAVE THE SAME TOP VALUE, AND THAT IS A JUDGEMENT RATHER THAN A
#: DERIVATION. Leech is a percentage of the same damage number in every case, so
#: the same percentage returns the same absolute amount; what differs is only
#: which pool it fills, and choosing between the pools is the player's decision
#: rather than something the numbers should make for them.
#:
#: Path of Exile prices mana leech below life leech, and the reason does not
#: carry over: its mana pools are an order of magnitude smaller than its life
#: pools. Here they are comparable for the class that wants each. At level 100
#: the Ritualist, the caster, has 1,278 mana against 1,060 health and 832 energy
#: shield, so a mana leech priced below life leech would be worth less for no
#: reason the character sheet supports.
#:
#: ON THE SAME SLOTS AS LIFE LEECH. Leech comes from hitting, so it belongs where
#: a hit comes from, which is what OFFENSIVE_SLOTS is.
FLAT_MANA_LEECH = StatAffix("Flat mana leech", "mana_leech", "flat", 0.5,
                            OFFENSIVE_SLOTS, SUFFIX)

#: Energy shield is leechable even though the Vampire class cannot use one. That
#: is a restriction on one class rather than on the stat: the Ritualist is the
#: class with an energy shield and it is the one this is for.
FLAT_ENERGY_SHIELD_LEECH = StatAffix("Flat energy shield leech",
                                     "energy_shield_leech", "flat", 0.5,
                                     OFFENSIVE_SLOTS, SUFFIX)

#: The three together, so a rule about leech can be written once.
LEECH_AFFIXES: tuple[StatAffix, ...] = (FLAT_LIFE_LEECH, FLAT_MANA_LEECH,
                                        FLAT_ENERGY_SHIELD_LEECH)

#: How long a leeched amount takes to arrive, in seconds. Not instant, which is
#: what stops a character that is winning being unkillable. Last Epoch's figure;
#: see the "Leech" section of `Cataclysm_GDD_v2.md` for why that shape was taken
#: over Path of Exile's per-instance and total rate caps.
LEECH_PAYOUT_SECONDS = 3.0

#: By convention. Block removes half a hit and needs no cap, so a full defensive
#: investment reaching a high figure is legal by design rather than a mistake.
FLAT_BLOCK_CHANCE = StatAffix("Flat block chance", "block_chance", "flat", 5.0,
                              DEFENSIVE_SLOTS, SUFFIX)

#: By convention, and deliberately the smallest defensive affix in the pool.
#: Damage reduction is a flat percentage off everything, with no curve and no
#: cap of its own, so it is the one defensive stat that would run away.
FLAT_DAMAGE_REDUCTION = StatAffix("Flat damage reduction", "damage_reduction",
                                  "flat", 2.0, DEFENSIVE_SLOTS, SUFFIX)

#: Against the class base: the Masochist carries 158, and 6% is 9.5.
FLAT_RETALIATION = StatAffix("Flat retaliation", "retaliation", "flat", 9.5,
                             DEFENSIVE_SLOTS, SUFFIX)

#: By convention: at 5 points a piece, twenty slots reach immunity, which the
#: design already allows -- a character at 100% cannot be stunned at all.
FLAT_CROWD_CONTROL_RESISTANCE = StatAffix(
    "Flat crowd control resistance", "crowd_control_resistance", "flat", 5.0,
    DEFENSIVE_SLOTS, SUFFIX)

#: By convention. Critical strike chance has a hard cap of 100 and its base
#: comes from the skill, so gear supplies the climb toward that cap.
FLAT_CRIT_CHANCE = StatAffix("Flat critical strike chance", "crit_chance",
                             "flat", 5.0, OFFENSIVE_SLOTS, SUFFIX)
INCREASED_CRIT_CHANCE = StatAffix("Increased critical strike chance",
                                  "crit_chance", "increased", 25.0,
                                  OFFENSIVE_SLOTS, SUFFIX)

#: Against the class base: every class starts at 150, and this is percentage
#: points added to it rather than a multiplier on it.
FLAT_CRIT_MULTIPLIER = StatAffix("Flat critical strike multiplier",
                                 "crit_multiplier", "flat", 20.0,
                                 OFFENSIVE_SLOTS, SUFFIX)

#: Increased only: attack speed's base is the weapon's, so an affix scales what
#: the weapon gives rather than creating a rate from nothing.
INCREASED_ATTACK_SPEED = StatAffix("Increased attack speed", "attack_speed",
                                   "increased", 15.0, OFFENSIVE_SLOTS, SUFFIX)

#: The minion mirror, at the same value on the same slots. Issue #337. It scales
#: the interval `game/Data/MinionTypes.csv` gives each type, in the same way the
#: affix above scales the rate the weapon gives.
INCREASED_MINION_ATTACK_SPEED = StatAffix(
    "Increased minion attack speed", "minion_attack_speed", "increased", 15.0,
    INCREASED_ATTACK_SPEED.allowed_slots, SUFFIX)

#: Against the class base: baselines at 100%, being a percentage of whatever
#: the skill itself does, so 12% is the same ratio the other increases use.
INCREASED_AREA_OF_EFFECT = StatAffix("Increased area of effect",
                                     "area_of_effect", "increased", 12.0,
                                     OFFENSIVE_SLOTS, SUFFIX)


# -- The three damage over time levers ------------------------------------
#
# TICK RATE IS DAMAGE, NOT DELIVERY. Decided by the project owner on 2026-08-04,
# issue #220. A damage over time effect deals a fixed amount per tick, so ticking
# twice as often deals twice the total. Damage per tick, tick rate and duration
# are three separate scalable metrics and all three multiply the same output.
#
# ONE SET OF LEVERS FOR EVERY DAMAGE OVER TIME EFFECT, NOT ONE SET PER AILMENT.
# Six of the ten ailment affixes apply a damage over time effect -- bleed,
# poison, disease, void splinter, necrosis and burn -- and skills apply burn
# outright on top of that. Three levers each would be eighteen affixes for one
# build archetype, against the eight the whole damage-against-a-type family
# costs. It also matches the lever that already existed: there has only ever been
# one damage over time frequency stat, shared by every effect, and splitting the
# other two per ailment while leaving that one shared would be incoherent.
# Issue #205.

#: How many of the three levers a damage over time build has to buy. Named
#: because the pricing below divides by it.
DOT_LEVERS = 3


def dot_lever_top_value() -> float:
    """Top-tier value for each of the three damage over time levers.

    DERIVED, NOT PICKED, and derived from the direct-hit build it has to be fair
    against. `REFERENCE_INCREASED_DAMAGE_AFFIXES` slots spent on
    `INCREASED_DAMAGE` multiply a direct-hit build's damage by 8.5. The same
    number of slots spread evenly over the three levers has to reach the same
    figure, so with `s` slots on each lever:

        (1 + s x v) ** 3 = 1 + REFERENCE_INCREASED_DAMAGE_AFFIXES x 1.25

    WHY IT HAS TO BE SOLVED RATHER THAN COPIED FROM ANOTHER AFFIX. The three
    levers multiply each other, so setting any one of them against an existing
    affix sets the wrong number: three affixes at 125% each would be 8.5 x 8.5 x
    8.5, not 8.5. This is the whole reason issue #258 was blocked on issue #205
    rather than being a one-line edit.

    THE COMPARISON HOLDS THE SLOT COUNT FIXED AND NOTHING ELSE. It says a damage
    over time build and a direct-hit build that each spend six offensive affix
    slots on raising their own damage end up in the same place. It does NOT hold
    at other slot counts, and cannot: an additive bracket and a product of three
    brackets only cross once. Below six slots the damage over time build is
    behind and above it ahead, reaching about three times a direct-hit build at
    eighteen slots. That shape is deliberate -- the design document says the
    three levers multiply and that this is why they are separate stats -- but the
    size of the gap at heavy investment has not been played. Issue #264 carries
    it, with four alternative ways to close it.
    """
    target = 1.0 + REFERENCE_INCREASED_DAMAGE_AFFIXES * INCREASED_DAMAGE.top_value / 100.0
    slots_per_lever = REFERENCE_INCREASED_DAMAGE_AFFIXES / DOT_LEVERS
    return (target ** (1.0 / DOT_LEVERS) - 1.0) / slots_per_lever * 100.0


#: The shipped figure, rounded to a whole percentage from `dot_lever_top_value()`
#: because every other top value in this pool is a whole number and a player
#: reads these on an item. The exact solve is 52.04%; rounding down leaves a
#: damage over time build at 8.49 against a direct-hit build's 8.50, which is
#: 0.1% short and errs in the safer direction, since these levers compound.
#:
#: `test_affixes.py` checks the rounding against the solve, so this cannot drift
#: away from its derivation without a test failing.
DOT_LEVER_TOP_VALUE = 52.0

#: Damage per tick. The lever that did not exist at all before issue #205, and
#: the reason a build could apply nine different ailments and never make one of
#: them hurt more.
INCREASED_DOT_DAMAGE = StatAffix("Increased damage over time", "dot_damage",
                                 "increased", DOT_LEVER_TOP_VALUE,
                                 OFFENSIVE_SLOTS, SUFFIX)

#: Ticks per second. WAS 12.0 AND THAT WAS WRONG, set to match increased armour
#: and increased maximum health on the unexamined assumption that ticking faster
#: only changed when damage arrived. Issue #258.
INCREASED_DOT_FREQUENCY = StatAffix("Increased damage over time frequency",
                                    "dot_frequency", "increased",
                                    DOT_LEVER_TOP_VALUE, OFFENSIVE_SLOTS,
                                    SUFFIX)

#: How long the effect runs. The third lever, and the other one issue #205 found
#: missing.
#:
#: NOT THE SAME THING AS AILMENT MAGNITUDE. Magnitude comes from chance to apply
#: above 100% and, for an effect with a capped strength such as Cripple, turns
#: into duration only once that cap is reached -- see `ailment_application` and
#: the design document's table of what magnitude scales. This affix lengthens
#: every damage over time effect directly, whatever its chance to apply is.
INCREASED_DOT_DURATION = StatAffix("Increased damage over time duration",
                                   "dot_duration", "increased",
                                   DOT_LEVER_TOP_VALUE, OFFENSIVE_SLOTS,
                                   SUFFIX)

#: The three in the order the design document lists them: damage per tick, tick
#: rate, duration.
DOT_LEVER_AFFIXES: tuple[StatAffix, ...] = (INCREASED_DOT_DAMAGE,
                                            INCREASED_DOT_FREQUENCY,
                                            INCREASED_DOT_DURATION)

#: Against the requirement: enemy resistance runs from 0 to 35 and penetration
#: beyond it grants nothing, so a handful of these covers the hardest target in
#: the vertical slice and more is wasted.
FLAT_PENETRATION = StatAffix("Flat penetration", "penetration", "flat", 4.0,
                             OFFENSIVE_SLOTS, SUFFIX)

#: By convention, and deliberately small. Movement speed has a base of about 4
#: metres per second, no cap, and affects everything a player does, so 8% is
#: half the ratio the other increases use.
INCREASED_MOVEMENT_SPEED = StatAffix("Increased movement speed",
                                     "movement_speed", "increased", 8.0,
                                     frozenset({"Boots", "Belt", "Ring"}),
                                     SUFFIX)

#: By convention. Cooldown reduction divides rather than subtracting, so no
#: quantity of it reaches zero and it needs no cap.
INCREASED_COOLDOWN_REDUCTION = StatAffix("Increased cooldown reduction",
                                         "cooldown_reduction", "increased",
                                         12.0, UTILITY_SLOTS, SUFFIX)

#: By convention. Both affect what drops rather than what a character can do, so
#: they compete with combat stats for the same slots, which is the trade.
FLAT_MAGIC_FIND = StatAffix("Flat magic find", "magic_find", "flat", 10.0,
                            UTILITY_SLOTS, SUFFIX)
INCREASED_LOOT_QUANTITY = StatAffix("Increased loot quantity", "loot_quantity",
                                    "increased", 8.0, UTILITY_SLOTS, SUFFIX)


# -- Suffixes: the eight primary attributes -------------------------------

#: PERCENTAGE ONLY, NEVER FLAT, AND THAT IS THE POINT. Gear grants a percentage
#: increase to an attribute the character already has, not extra points. So the
#: affix is worth little to a character spread thin and a great deal to one
#: specialised, which makes it reward a decision the player already made rather
#: than handing out the same value to everyone.
#:
#: WHERE THE 12% COMES FROM. It is the house figure every other "increased"
#: affix uses, and the arithmetic lands in a sensible place. An attribute grants
#: 2% of its main stat per point, so +12% of an attribute held at V points is
#: worth 0.12 x V x 2% of that stat. That equals a dedicated 12% single-stat
#: affix at exactly V = 50, which is heavy specialisation out of the 100 points a
#: character gets from levels. Below 50 it is worse than the dedicated affix;
#: above it, better. It also drives the attribute's second stat for free, which
#: is why it sits on suffixes where the dedicated prefixes do not compete.
#:
#: NO HYBRIDS, and no flat version. Both decided by the project owner.
ATTRIBUTE_AFFIX_TOP_VALUE = 12.0

#: WHICH SLOTS EACH ONE ROLLS ON IS DERIVED, NOT CHOSEN. An attribute affix rolls
#: wherever the stats that attribute drives already roll, taken from
#: `game/Data/Attributes.csv` and the pool above. That is what keeps weapons
#: offensive without needing a new rule: Ferocity drives critical strike and
#: Efficacy drives area of effect, both of which already roll on a weapon, while
#: Vitality drives health and Constitution drives armour, which do not.
AGILITY_SLOTS = frozenset({"Belt", "Boots", "Chest", "Gloves", "Head", "Pants",
                           "Ring", "Shoulders"})
CONSTITUTION_SLOTS = frozenset({"Belt", "Boots", "Chest", "Head", "Pants",
                                "Ring", "Shoulders"})
VITALITY_SLOTS = frozenset({"Belt", "Boots", "Chest", "Head", "Necklace",
                            "Pants", "Ring", "Shoulders"})
MIND_SLOTS = frozenset({"Belt", "Boots", "Chest", "Head", "Necklace", "Relic",
                        "Ring"})
SPIRIT_SLOTS = frozenset({"Chest", "Head", "Relic", "Ring", "Shoulders"})
EFFICACY_SLOTS = frozenset({"Belt", "Boots", "Gloves", "Necklace", "Relic",
                            "Ring", "Weapon"})
LUCK_SLOTS = frozenset({"Belt", "Boots", "Necklace", "Relic", "Ring"})

INCREASED_AGILITY = StatAffix("Increased agility", "agility", "increased",
                              ATTRIBUTE_AFFIX_TOP_VALUE, AGILITY_SLOTS, SUFFIX)
INCREASED_FEROCITY = StatAffix("Increased ferocity", "ferocity", "increased",
                               ATTRIBUTE_AFFIX_TOP_VALUE, OFFENSIVE_SLOTS,
                               SUFFIX)
INCREASED_CONSTITUTION = StatAffix("Increased constitution", "constitution",
                                   "increased", ATTRIBUTE_AFFIX_TOP_VALUE,
                                   CONSTITUTION_SLOTS, SUFFIX)
INCREASED_VITALITY = StatAffix("Increased vitality", "vitality", "increased",
                               ATTRIBUTE_AFFIX_TOP_VALUE, VITALITY_SLOTS,
                               SUFFIX)
INCREASED_MIND = StatAffix("Increased mind", "mind", "increased",
                           ATTRIBUTE_AFFIX_TOP_VALUE, MIND_SLOTS, SUFFIX)
INCREASED_SPIRIT = StatAffix("Increased spirit", "spirit", "increased",
                             ATTRIBUTE_AFFIX_TOP_VALUE, SPIRIT_SLOTS, SUFFIX)
INCREASED_EFFICACY = StatAffix("Increased efficacy", "efficacy", "increased",
                               ATTRIBUTE_AFFIX_TOP_VALUE, EFFICACY_SLOTS,
                               SUFFIX)
INCREASED_LUCK = StatAffix("Increased luck", "luck", "increased",
                           ATTRIBUTE_AFFIX_TOP_VALUE, LUCK_SLOTS, SUFFIX)

#: The eight, in the order `game/Data/Attributes.csv` lists them.
ATTRIBUTE_AFFIXES: tuple[StatAffix, ...] = (
    INCREASED_AGILITY, INCREASED_FEROCITY, INCREASED_CONSTITUTION,
    INCREASED_VITALITY, INCREASED_MIND, INCREASED_SPIRIT, INCREASED_EFFICACY,
    INCREASED_LUCK,
)

#: The stat name each one grants, which is the attribute's own name. Kept as a
#: set so the checks below can ask "is this an attribute affix" without matching
#: on the display name.
ATTRIBUTE_STATS = frozenset(a.stat for a in ATTRIBUTE_AFFIXES)


# -- Prefixes: increased damage against one type of enemy ------------------

#: 400%, against the generic "Increased damage" affix's 125%. Issue #213.
#:
#: WHAT MAKES IT CONDITIONAL. An enemy has a damage type of its own, which is
#: its Cataclysm's; the design document says so in the enemy section. This affix
#: applies only when the target's type matches. It reads the TARGET, so how many
#: damage types the player's weapon carries is irrelevant to it. That is the
#: difference between this and the weapon-side version, which was rejected and
#: closed as #206 because a weapon carries several types at once and the player
#: could not concentrate to fix the dilution.
#:
#: WHY IT MUST BE LARGER THAN THE GENERIC ONE. The project owner stated it: a
#: conditional affix that pays the same as an unconditional one is a strictly
#: worse roll, and nobody would ever swap gear for it.
#:
#: WHERE 400 COMES FROM. Not invented -- it is the ratio this project already
#: pays for narrowing a modifier from all eight damage types to one. The
#: resistance families give 20 per type at breadth 1 and 6 per type at breadth 8,
#: so narrowing is worth 20/6, about 3.33 times. The generic damage affix is the
#: breadth 8 case because it applies whatever the target is. 125 x 3.33 is 416.7;
#: 400 is the round number next to it and the design document quotes round
#: numbers for the resistance ladder too.
#:
#: WHAT 400 PRODUCES. The generic affix is worth 125 whatever is in front of the
#: player. A type-specific affix is worth 400 against its own type and nothing
#: against the other seven, so across C active Cataclysms it averages 400/C. The
#: two are equal at C = 3.2. The type-specific affix is therefore the better use
#: of a prefix for the first three Cataclysms of a campaign and the generic one
#: from four onward, which is the same crossover shape the resistance ladder has
#: at 3.33 and is where the reason to change equipment comes from.
#:
#: A BALANCE RISK WORTH STATING RATHER THAN HIDING. Resistance is capped at 70%
#: so its 3.33 ratio cannot run away. Damage is not capped. Four prefix rolls of
#: this affix give +1600% increases against one type where four generic rolls
#: give +500% against everything, and in a one-Cataclysm run that is a 3.2 times
#: swing on the largest damage bucket. The ratio is defensible and the magnitude
#: is a tuning question for real play. Recorded in docs/DECISIONS.md and filed as
#: its own issue rather than argued to a conclusion here.
DAMAGE_VS_TOP_VALUE = 400.0

#: SAME SLOTS AS THE GENERIC DAMAGE AFFIX, and same position, so the two compete
#: for one prefix slot on one piece. That competition IS the choice; putting them
#: in different positions would let a player take both and remove it.
DAMAGE_VS_AFFIXES: tuple[StatAffix, ...] = tuple(
    StatAffix(f"Increased damage against {damage_type} enemies",
              f"damage_vs_{damage_type.lower()}", "increased",
              DAMAGE_VS_TOP_VALUE, OFFENSIVE_SLOTS, PREFIX)
    for damage_type in DAMAGE_TYPES
)

#: The stat name each one grants. Kept as a set so the checks below can ask "is
#: this a damage-against-a-type affix" without matching on the display name.
DAMAGE_VS_STATS = frozenset(a.stat for a in DAMAGE_VS_AFFIXES)


#: Every stat affix in the pool. Resistance families are separate, because they
#: have a breadth axis the others do not.
AFFIX_POOL: tuple[StatAffix, ...] = (
    FLAT_HEALTH, INCREASED_HEALTH,
    FLAT_MANA, INCREASED_MANA,
    FLAT_ENERGY_SHIELD, INCREASED_ENERGY_SHIELD,
    FLAT_ARMOR, INCREASED_ARMOR,
    FLAT_EVASION, INCREASED_EVASION,
    FLAT_DAMAGE, INCREASED_DAMAGE,
    INCREASED_SPELL_DAMAGE,
    INCREASED_MINION_DAMAGE, INCREASED_MINION_HEALTH,
    INCREASED_MINION_ATTACK_SPEED,
    FLAT_CLASS_RESOURCE,
    FLAT_HEALTH_REGEN, INCREASED_HEALTH_REGEN,
    FLAT_MANA_REGEN, INCREASED_MANA_REGEN,
    INCREASED_ENERGY_SHIELD_REGEN,
    FLAT_LIFE_LEECH, FLAT_MANA_LEECH, FLAT_ENERGY_SHIELD_LEECH,
    FLAT_BLOCK_CHANCE,
    FLAT_DAMAGE_REDUCTION,
    FLAT_RETALIATION,
    FLAT_CROWD_CONTROL_RESISTANCE,
    FLAT_CRIT_CHANCE, INCREASED_CRIT_CHANCE,
    FLAT_CRIT_MULTIPLIER,
    INCREASED_ATTACK_SPEED,
    INCREASED_AREA_OF_EFFECT,
    FLAT_PENETRATION,
    INCREASED_MOVEMENT_SPEED,
    INCREASED_COOLDOWN_REDUCTION,
    FLAT_MAGIC_FIND,
    INCREASED_LOOT_QUANTITY,
) + DOT_LEVER_AFFIXES + ATTRIBUTE_AFFIXES + DAMAGE_VS_AFFIXES


def pool_for(slot: str, position: str | None = None) -> tuple[StatAffix, ...]:
    """Which stat affixes can roll on a slot, optionally in one position only."""
    if slot not in GEAR_SLOTS:
        raise ValueError(f"unknown gear slot {slot!r}; "
                         f"expected one of {sorted(GEAR_SLOTS)}")
    if position is not None and position not in AFFIX_POSITIONS:
        raise ValueError(f"unknown position {position!r}")
    return tuple(a for a in AFFIX_POOL
                 if slot in a.allowed_slots
                 and (position is None or a.position == position))


# --------------------------------------------------------------------------
# Implicits: what a slot gives before anything rolls
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Implicit:
    """A stat fixed to an item's base, above the rolled affixes.

    IT DOES NOT ROLL AND IT CANNOT BE CHANGED. That is the whole point: it is
    what a slot IS rather than what a particular drop happened to get, so
    choosing which slot to put a build's weight on is a decision made before any
    loot is involved.

    Requested by the project owner 2026-08-03, and it fills a real gap:
    `Cataclysm_GDD_v2.md` lists item slots and rarities but gives no slot any
    inherent stat, so a Chest and a Belt currently differ only in how many gem
    sockets they hold.

    Gear upgrade level multiplies an implicit the same way it multiplies an
    affix, so the stated values are the fully upgraded ones.
    """

    stat: str
    kind: str
    value: float

    def __post_init__(self) -> None:
        if self.kind not in ("flat", "increased"):
            raise ValueError(
                f"implicit {self.stat}: kind must be 'flat' or 'increased', "
                f"got {self.kind!r}")
        if self.stat not in AFFIXABLE_STATS:
            raise ValueError(
                f"implicit grants {self.stat!r}, which is neither on the "
                "character sheet nor an off-sheet stat")

    def value_at(self, gear_level: int = MAX_GEAR_LEVEL,
                 two_handed: bool = False) -> float:
        """This implicit's value at an upgrade level.

        `two_handed` applies TWO_HANDED_MULTIPLIER. Pass a weapon base's
        `value_multiplier` rather than deciding here, so the rule lives in one
        place. An armour base is never two-handed.
        """
        at_zero = self.value / gear_level_multiplier(MAX_GEAR_LEVEL)
        multiplier = TWO_HANDED_MULTIPLIER if two_handed else 1.0
        return at_zero * gear_level_multiplier(gear_level) * multiplier


# --------------------------------------------------------------------------
# Item bases: a slot is a category, and the bases inside it differ
# --------------------------------------------------------------------------
#
# THE IMPLICIT BELONGS TO THE BASE, NOT THE SLOT. Corrected by the project owner
# 2026-08-03: every category of gear has several bases, and each base has its own
# implicit. A chest is not one item with one inherent stat -- it is a choice
# between a chest built for armour, one built for evasion, one built for health
# regeneration, and so on.
#
# That is where most of the interest in gearing lives. A player who wants evasion
# is not waiting for an evasion affix to roll; they are looking for an evasion
# base, and every base they pick is a defensive layer they are committing to
# before any affix is involved.

#: The weapon sub-types, from the Weapon Sub-Types table in the design document.
#: Each carries a baseline combat property: Piercing ignores 20% of enemy armour,
#: Slashing deals 10% more damage against health, Blunt has a 10% chance to stun
#: for 0.75 seconds, and Magic strips 10% more energy shield.
WEAPON_SUB_TYPES = ("Piercing", "Slashing", "Blunt", "Magic")

#: The two shapes a basic attack may take, because the design document says the
#: basic attack is the weapon's own swing: a Strike for a melee weapon, a
#: Projectile for a ranged one. Issue #524.
#:
#: THIS IS A COPY. `BASIC_ATTACK_SHAPES` in `tools/generate_datatables.py` is the
#: same set and refuses the same cells at generation time.
#: `test_the_two_basic_attack_shape_vocabularies_agree` compares them, the same
#: way the two copies of the full shape vocabulary are compared.
BASIC_ATTACK_SHAPES = frozenset({"Strike", "Projectile"})


@dataclass(frozen=True)
class ItemBase:
    """One item base within a slot, and what it inherently grants.

    `implicits` do not roll and cannot be changed. Two bases in the same slot are
    the same size and take the same affixes; what separates them is this.
    """

    name: str
    slot: str
    implicits: tuple[Implicit, ...]

    def __post_init__(self) -> None:
        if self.slot not in GEAR_SLOTS:
            raise ValueError(
                f"{self.name} is in slot {self.slot!r}, which does not exist")
        if not 1 <= len(self.implicits) <= 3:
            raise ValueError(
                f"{self.name} has {len(self.implicits)} implicits; a base "
                "carries one to three")

    @property
    def value_multiplier(self) -> float:
        """What this base multiplies its own implicits and affixes by.

        One for everything except a two-handed weapon, which overrides it. Held
        on the base rather than checked at each call site so that adding a piece
        of gear cannot forget the rule.
        """
        return 1.0

    def implicit_values(self, gear_level: int = MAX_GEAR_LEVEL
                        ) -> dict[str, float]:
        two_handed = self.value_multiplier != 1.0
        return {i.stat: i.value_at(gear_level, two_handed=two_handed)
                for i in self.implicits}

    def affix_value(self, affix: "StatAffix", tier: int = 7, roll: float = 1.0,
                    gear_level: int = MAX_GEAR_LEVEL) -> float:
        """What one affix is worth when it is rolled on THIS base.

        The only place that should be asked. Reading `affix.value_at` directly
        gives the one-handed figure and silently loses the two-handed bonus.
        """
        return affix.value_at(tier, roll, gear_level,
                              two_handed=self.value_multiplier != 1.0)


@dataclass(frozen=True)
class WeaponBase(ItemBase):
    """A weapon base, which carries more than an armour base does.

    On top of its implicit stats a weapon has a physical sub-type and a number of
    damage type slots, both of which the design already establishes and neither
    of which any other item has.

    WHICH damage types fill those slots is not a property of the base. Section IV
    says loot is biased toward the Cataclysm being fought, so the types are
    decided when the item drops. The base says only how many it can hold.
    """

    hands: int = 1
    sub_type: str = "Slashing"
    weapon_type: str = "Sword"

    #: Attacks per second before any increase. This is the base the design
    #: document says the equipped weapon supplies, and without it every increased
    #: attack speed affix in the game multiplies zero and is worth nothing.
    #: Issue #120.
    #:
    #: NOT AN IMPLICIT, AND THAT IS THE POINT. `value_multiplier` doubles every
    #: implicit on a two-handed weapon, which is correct for damage and would be
    #: nonsense here: a Greatsword would swing twice as fast as a Sword. Path of
    #: Exile and Last Epoch both treat a weapon's rate as an intrinsic property
    #: listed apart from its modifiers, and Last Epoch's own formula is skill
    #: rate times weapon rate times one plus increases. So it sits beside the
    #: implicits rather than among them, and nothing scales it but increases.
    #:
    #: The numbers are ordered inversely to each weapon's flat attack damage and
    #: average to ONE_HANDED_RATE and TWO_HANDED_RATE in
    #: sim/analyse_two_handed_multiplier.py, which is what the two-handed
    #: multiplier of 2.0 was derived against. Changing one without the other
    #: moves a multiplier that is already shipped.
    attack_speed: float = 0.0

    #: THE MOST damage types this base can ever hold, not how many it holds. The
    #: count on a particular weapon is rolled when it drops, from 1 up to
    #: `max_damage_types(hands, tier)`, which also caps it by difficulty tier.
    #:
    #: The name ends in `_on_base` to keep it apart from the module-level
    #: `max_damage_types(hands, tier)` function, which returns the effective cap
    #: at a difficulty tier rather than this base's own limit. The workbook calls
    #: the column `Max Damage Types` and `game/Data/ItemBases.csv` calls it
    #: `MaxDamageTypes`; see issue #218 for the rename.
    max_damage_types_on_base: int = 4

    #: The basic attack's shape and its parameters, written the way the Item
    #: Bases sheet writes them and carried through to `game/Data/ItemBases.csv`
    #: as `BasicShape` and `BasicShapeParams`.
    #:
    #: IT LIVES ON THE WEAPON RATHER THAN IN THE SKILL MATRIX because it does not
    #: vary by damage type. Every other slot names a designed skill per weapon
    #: AND damage type; the basic attack is weapon damage itself, so there are 13
    #: of these rather than 75 near-identical matrix rows. Decided 2026-08-15 on
    #: issue #524, and it keeps the design document's statement that the matrix
    #: holds one skill per non-basic slot true. Path of Exile stores melee reach
    #: the same way, as a property of the weapon base type rather than of the
    #: skill.
    #:
    #: EMPTY ON A WEAPON THAT ARMS NOBODY, which is the Shield. Issue #619.
    basic_shape: str = ""
    basic_shape_params: str = ""

    @property
    def arms_the_holder(self) -> bool:
        """Whether this weapon supplies attack damage, so a hit can be composed.

        Read off the implicits rather than off the name, which is what
        `player_damage.armed_weapons_in` also does, so a second weapon like the
        Shield needs no edit here.

        THE TWO ARE NOT IDENTICAL. That function sums any `attack_damage`
        implicit; this one requires the kind to be `flat`. They agree on today's
        data because no weapon grants increased attack damage as an implicit.
        This is the stricter of the two, because a weapon granting only an
        increase supplies no damage for the increase to act on.
        """
        return any(i.stat == "attack_damage" and i.kind == "flat"
                   for i in self.implicits)

    @property
    def value_multiplier(self) -> float:
        """A two-hander is worth double per implicit and per affix.

        Four affix slots at double value equals eight at single value, which is
        what a dual wielder holds across two weapons. See TWO_HANDED_MULTIPLIER.
        """
        return two_handed_multiplier(self.hands)

    def __post_init__(self) -> None:
        super().__post_init__()
        if self.sub_type not in WEAPON_SUB_TYPES:
            raise ValueError(
                f"{self.name} has sub-type {self.sub_type!r}; expected one of "
                f"{list(WEAPON_SUB_TYPES)}")
        if self.hands not in (1, 2):
            raise ValueError(f"{self.name} is {self.hands}-handed")
        if self.attack_speed <= 0.0:
            raise ValueError(
                f"{self.name} has an attack speed of {self.attack_speed}. Every "
                "weapon needs one above zero, because the weapon is where that "
                "base comes from and an increase to zero is worth nothing. "
                "See issue #120.")
        if not 1 <= self.max_damage_types_on_base <= len(DAMAGE_TYPES):
            raise ValueError(
                f"{self.name} holds at most {self.max_damage_types_on_base} "
                f"damage types; there are only {len(DAMAGE_TYPES)}")
        if self.arms_the_holder and not self.basic_shape:
            raise ValueError(
                f"{self.name} supplies attack damage and has no basic attack "
                "shape, so a character holding it has nothing between its "
                "cooldowns. See issue #524.")
        if self.basic_shape and not self.arms_the_holder:
            raise ValueError(
                f"{self.name} supplies no attack damage but states a basic "
                "attack, which would deal 100% of nothing. See issue #619.")
        if self.basic_shape and self.basic_shape not in BASIC_ATTACK_SHAPES:
            raise ValueError(
                f"{self.name} has a basic attack shaped {self.basic_shape!r}; "
                f"expected one of {sorted(BASIC_ATTACK_SHAPES)}")
        if self.basic_shape_params and not self.basic_shape:
            raise ValueError(
                f"{self.name} has basic attack parameters but no shape")


#: THE MOST damage types a weapon of each kind can ever hold, not how many it
#: holds. A one-hander tops out at four and a two-hander at eight.
#:
#: HOW MANY A PARTICULAR WEAPON HOLDS IS ROLLED WHEN IT DROPS, and the difficulty
#: tier caps that roll as well. The count is drawn from 1 up to
#: `max_damage_types(hands, tier)`, which is the lower of the two limits. So a
#: two-hander cannot roll five damage types until tier 5, and a one-hander never
#: rolls more than four however deep the player goes.
#:
#: DUAL WIELDING IS STILL THE ROUTE TO MULTICLASSING, and the tier cap is what
#: makes that true rather than the raw maximums, which tie at eight. Two
#: one-handers reach the full eight types at tier 4; a two-hander cannot reach
#: eight until tier 8. Dual wielding therefore leads at every tier from 1 to 7
#: and ties only at the last one, with the gap widest at tier 4. Every damage
#: type present unlocks that type's three class trees.
DAMAGE_TYPES_ON_ONE_HANDED = 4
DAMAGE_TYPES_ON_TWO_HANDED = 8

#: The number of difficulty tiers, which is also the highest damage type count a
#: two-hander can reach, because the tier caps the roll.
DIFFICULTY_TIERS = 8


#: How far above the difficulty tier a DROP may roll. Stated by the project
#: owner on 2026-08-05, issue #241.
#:
#: The one-above is what makes a drop worth reading. With the cap sitting exactly
#: on the tier, every affix a player found was one they could already make, so
#: the only reason to run a dungeon was quantity. One tier above means the best
#: thing a dungeon can produce is something the forge cannot yet reach.
DROP_TIERS_ABOVE_DIFFICULTY = 1


def max_affix_tier_on_a_drop(tier: int) -> int:
    """The highest affix tier a DROP may roll at a difficulty tier.

    Issue #129, revised by the project owner on 2026-08-05 in issue #241.
    Affixes have seven tiers and tier 7 is worth seven times tier 1, and nothing
    said which of them a drop could roll. Without a gate a tier 1 dungeon drops
    a T7 affix and the seven-tier curve does nothing for progression at all.

    THE GATE IS THE DIFFICULTY TIER PLUS ONE. The difficulty tier is the design's
    own gate three times over: gear and gem rarity equal it, the best upgrade
    stone that can drop is capped by it, and a weapon rolls damage types up to
    it. The plus one is the project owner's, and it is what gives a drop
    something the forge cannot yet make.

    THERE ARE EIGHT DIFFICULTY TIERS AND SEVEN AFFIX TIERS. With the plus one,
    the cap reaches T7 at difficulty tier 6 and stays there for tiers 6, 7 and 8.

    IT DOES NOT CAP CRAFTING. See `max_affix_tier_by_crafting`.
    """
    if not 1 <= tier <= DIFFICULTY_TIERS:
        raise ValueError(f"tier {tier} is outside 1 to {DIFFICULTY_TIERS}")
    return min(AFFIX_TIERS[-1], tier + DROP_TIERS_ABOVE_DIFFICULTY)


def max_affix_tier_by_crafting(tier: int) -> int:
    """The highest affix tier CRAFTING may reach at a difficulty tier: all of them.

    STATED BY THE PROJECT OWNER, issue #241, 2026-08-05: a player may craft an
    affix as high as they can afford, at any difficulty tier. What stops a tier 1
    player owning a set of T7 affixes is cost, not a rule -- the Potency Crystal
    raises one tier at a time, `game/Data/CraftingMaterials.csv` prices the
    deterministic affix craft at one day per tier of affix, and the empire tree's
    crafting investment is what brings that within reach.

    Takes `tier` and ignores it on purpose. A caller asking "how high can this be
    crafted here" gets an answer with the same shape as the drop cap, and the
    day this stops being flat there is one place to change.
    """
    if not 1 <= tier <= DIFFICULTY_TIERS:
        raise ValueError(f"tier {tier} is outside 1 to {DIFFICULTY_TIERS}")
    return AFFIX_TIERS[-1]


def max_affix_tier(tier: int) -> int:
    """The highest affix tier reachable at a difficulty tier, by any route.

    Which is the crafting ceiling, because crafting is uncapped. Kept as a name
    because callers asking "what is the best possible here" should not have to
    know which route wins.
    """
    return max(max_affix_tier_on_a_drop(tier), max_affix_tier_by_crafting(tier))


#: How heavily each affix tier is weighted on a drop, mirroring the Drop Weight
#: column of the Affix Tiers sheet in `docs/All_Things_Cataclysm.xlsx`.
#:
#: EACH TIER IS HALF AS LIKELY AS THE ONE BELOW IT, set by the project owner on
#: 2026-08-18. Which makes a T7 affix one in 127 at difficulty tier 8, where all
#: seven are reachable, and half of every affix that drops a T1.
#:
#: WHAT THIS REPLACED, AND WHY IT WAS WRONG. The draw was uniform, so nearly 15%
#: of affixes at difficulty tier 8 came out at T7. Uniform was chosen as the rule
#: that "invents no constant", and the test guarding it said in its own docstring
#: that it was the one to change if this were ever tuned to lean high.
#:
#: THE GENRE WEIGHTS THESE, and the argument for uniform read across from a
#: different question. Path of Exile is cited above for WHICH tiers a drop can
#: reach, and it expands that range with item level; how LIKELY each one is, is a
#: separate matter, and there the higher tiers are rarer rather than equal.
#:
#: AND CRAFTING IS THE ROUTE TO T7, WHICH IS WHY A RARE DROP IS NOT PUNISHING.
#: `max_affix_tier_by_crafting` has no gate at all -- "an affix can be raised as
#: high as the player can afford" -- so a drop is the raw material and the
#: Potency Crystal is how a build reaches the top. The same reasoning set the gear
#: rarity drop weights; `docs/DECISIONS.md` records both.
AFFIX_TIER_DROP_WEIGHT: dict[int, float] = {
    1: 64.0,
    2: 32.0,
    3: 16.0,
    4:  8.0,
    5:  4.0,
    6:  2.0,
    7:  1.0,
}


def affix_tier_weights_up_to(cap: int) -> list[float]:
    """The weight of each tier from T1 up to `cap`, in order."""
    if cap not in AFFIX_TIERS:
        raise ValueError(f"T{cap} is not an affix tier; expected one of "
                         f"{list(AFFIX_TIERS)}")
    return [AFFIX_TIER_DROP_WEIGHT[t] for t in range(1, cap + 1)]


def affix_tier_distribution(tier: int) -> dict[int, float]:
    """What fraction of affixes dropping at this difficulty tier is each tier.

    Exact rather than sampled, so a test can check the shape without rolling
    seventy thousand affixes and then arguing about noise. Tiers above the cap
    are present and carry zero.
    """
    cap = max_affix_tier_on_a_drop(tier)
    weights = affix_tier_weights_up_to(cap)
    total = sum(weights)

    out = dict.fromkeys(AFFIX_TIERS, 0.0)
    for affix_tier, weight in zip(range(1, cap + 1), weights, strict=True):
        out[affix_tier] = weight / total
    return out


def roll_affix_tier(tier: int, rng) -> int:
    """The tier one affix rolls at on a drop at this difficulty tier.

    WHICH TIERS ARE REACHABLE is `max_affix_tier_on_a_drop(tier)`, the difficulty
    tier plus one capped at T7. Every tier at or below that stays in the pool, so
    a deep drop is better on average without being predictable, which is what
    makes a drop worth reading.

    That part is what the genre does. Path of Exile gates modifier tiers on item
    level, and item level expands which tiers are available rather than removing
    the low ones, so a high item level gives better potential and guarantees
    nothing. Last Epoch gates the same way on area level. It is also the shape
    this design already uses one section away: a weapon rolls from one damage
    type up to the lower of its own limit and the tier it dropped on.

    HOW LIKELY EACH REACHABLE TIER IS comes from AFFIX_TIER_DROP_WEIGHT, and each
    is half as likely as the one below. This was uniform until 2026-08-18; see
    that table for why it changed and what it means.
    """
    cap = max_affix_tier_on_a_drop(tier)
    weights = affix_tier_weights_up_to(cap)
    return rng.choices(range(1, cap + 1), weights=weights, k=1)[0]


def max_damage_types(hands: int, tier: int) -> int:
    """The most damage types a weapon of this kind can roll at this tier.

    The lower of the weapon's own limit and the difficulty tier. A one-hander is
    capped at four whatever the tier; a two-hander is capped by the tier until
    tier 8, where it reaches its own limit of eight.
    """
    if hands not in (1, 2):
        raise ValueError(f"a weapon is one- or two-handed, not {hands}")
    if not 1 <= tier <= DIFFICULTY_TIERS:
        raise ValueError(
            f"tier {tier} is outside 1 to {DIFFICULTY_TIERS}")
    own_limit = (DAMAGE_TYPES_ON_ONE_HANDED if hands == 1
                 else DAMAGE_TYPES_ON_TWO_HANDED)
    return min(own_limit, tier)


#: The basic attack each weapon type composes from its own swing, keyed on the
#: weapon type so a base picks its own up. Mirrors the Basic Shape and Basic
#: Shape Params columns of the Item Bases sheet, and
#: `test_affix_sheets_match_the_model.py` compares the two.
#:
#: MELEE REACH IS `0.9 + the weapon's length past the fist`, on a 0.3 m grid. The
#: 0.9 is not chosen: it is this project's own contact distance, the 0.42 m
#: player capsule from `CataclysmPlayerCharacter.cpp` plus a 0.48 m baseline enemy
#: body, and it is the Radius of Maul, Slam and Sunder -- three of the seven
#: designed enemy basic attacks, and the three shortest. Path of Exile computes
#: melee reach the same way, as weapon range plus the character's own hitbox
#: radius.
#:
#: THE ARC IS THAT WEAPON'S DESIGNED HEAVY ARC, CARRIED OVER UNCHANGED, because
#: the arc is the animation and the reach is the power. Every angle below is the
#: weapon's own Heavy angle from `game/Data/WeaponSkills.csv`.
#:
#: THAT LANDS ON EXACTLY 0.6 x THE DESIGNED HEAVY RADIUS for all ten weapons that
#: have a designed Heavy to check against -- eight Strikes and the Staff and Wand
#: Projectiles -- verified to three decimals by two derivations that were not
#: fitted to each other. So every one of those basics covers 36% of its Heavy's
#: area, since 0.6 squared is 0.36 and the angle is untouched.
#:
#: THREE ARE JUDGEMENTS WITH NOTHING TO CHECK AGAINST. The Spear, the Crossbow
#: and the two-handed Crossbow have no designed Heavy of any shape anywhere in
#: the matrix, so 3.3, 10 and 12 could not be cross-checked. Issue #524.
#:
#: NO RIDERS, ON ANY OF THEM. A basic attack is 100% weapon damage and nothing
#: else, which is what makes it the anchor every other slot is measured against.
#:
#: THE ENEMY BASIC ATTACKS ARE THE NEAREST PRECEDENT AND THEY ARE NOT UNANIMOUS,
#: so this is a choice rather than a convention read off the content. Of the seven
#: designed enemy basic attacks in `enemy_abilities.py`, four state `MaxTargets=1`
#: -- Rend, Maul, Slam and Sunder -- while Soulfire, Siege Bolt and Dread Cleave
#: state no cap at all. One of the seven does carry a rider: the Hellhound's Maul
#: sets what it hits alight. None carries a stun or leaves a patch of ground.
#:
#: A single target and no riders is the stricter reading, taken because the player
#: basic attack has a job no enemy one has: it is the 100% figure every other slot
#: is a percentage of, so a rider on it would move all six of the others.
BASIC_ATTACKS: dict[str, tuple[str, str]] = {
    "Dagger":      ("Strike",     "Radius=1.5; Angle=60; MaxTargets=1"),
    "Fist":        ("Strike",     "Radius=1.5; Angle=60; MaxTargets=1"),
    "Sword":       ("Strike",     "Radius=1.8; Angle=90; MaxTargets=1"),
    "Axe":         ("Strike",     "Radius=1.8; Angle=100; MaxTargets=1"),
    "Warhammer":   ("Strike",     "Radius=2.1; Angle=80; MaxTargets=1"),
    "Greataxe":    ("Strike",     "Radius=2.4; Angle=120; MaxTargets=1"),
    "Greatsword":  ("Strike",     "Radius=2.7; Angle=140; MaxTargets=1"),
    "Whip":        ("Strike",     "Radius=3; Angle=45; MaxTargets=1"),
    "Spear":       ("Strike",     "Radius=3.3; Angle=40; MaxTargets=1"),
    "Staff":       ("Projectile", "Range=7.2; Radius=0.9; Pierce=0; Speed=2000"),
    "Wand":        ("Projectile", "Range=8.4; Radius=0.6; Pierce=0; Speed=2600"),
    "Crossbow":    ("Projectile", "Range=10; Radius=0.4; Pierce=0; Speed=2400"),
    "2H Crossbow": ("Projectile", "Range=12; Radius=0.9; Pierce=0; Speed=1800"),
    # The Shield is deliberately absent. It is a one-handed weapon that grants no
    # attack damage, so there is no hit to compose from it. Issue #619.
}


def _weapon(name: str, weapon_type: str, hands: int, sub_type: str,
            attack_speed: float, *implicits: Implicit) -> WeaponBase:
    basic_shape, basic_shape_params = BASIC_ATTACKS.get(weapon_type, ("", ""))
    return WeaponBase(
        name=name, slot="Weapon", implicits=implicits, hands=hands,
        sub_type=sub_type, weapon_type=weapon_type, attack_speed=attack_speed,
        basic_shape=basic_shape, basic_shape_params=basic_shape_params,
        max_damage_types_on_base=(DAMAGE_TYPES_ON_ONE_HANDED if hands == 1
                                  else DAMAGE_TYPES_ON_TWO_HANDED))


#: Every base in the game, grouped by slot. Names follow the ordinary conventions
#: of the genre, because a base name has to say what the item is at a glance.
#:
#: Each slot's bases split along the axis that matters for that slot: armour
#: pieces along the three defensive layers the design has -- armour, evasion,
#: energy shield -- plus health or recovery; jewellery along what a build is
#: short of; weapons along the fourteen weapon types the design lists.
ITEM_BASES: tuple[ItemBase, ...] = (
    # -- Head ------------------------------------------------------------
    ItemBase("Helm", "Head", (Implicit("armor", "flat", 200.0),)),
    ItemBase("Hood", "Head", (Implicit("evasion", "flat", 4.0),)),
    ItemBase("Circlet", "Head", (Implicit("max_energy_shield", "flat", 55.0),)),
    ItemBase("Visage", "Head", (Implicit("max_health", "flat", 70.0),
                                Implicit("crowd_control_resistance", "flat", 4.0))),
    # -- Chest -----------------------------------------------------------
    ItemBase("Cuirass", "Chest", (Implicit("armor", "flat", 440.0),)),
    ItemBase("Jerkin", "Chest", (Implicit("evasion", "flat", 8.0),)),
    ItemBase("Vestment", "Chest", (Implicit("max_energy_shield", "flat", 120.0),)),
    ItemBase("Hauberk", "Chest", (Implicit("max_health", "flat", 180.0),)),
    ItemBase("Carapace", "Chest", (Implicit("armor", "flat", 220.0),
                                   Implicit("max_health", "flat", 90.0))),
    # -- Shoulders -------------------------------------------------------
    ItemBase("Pauldrons", "Shoulders", (Implicit("armor", "flat", 165.0),)),
    ItemBase("Mantle", "Shoulders", (Implicit("evasion", "flat", 3.5),)),
    ItemBase("Epaulets", "Shoulders",
             (Implicit("health_regen", "flat", 1.1),)),
    ItemBase("Spaulders", "Shoulders", (Implicit("retaliation", "flat", 11.0),)),
    # -- Gloves ----------------------------------------------------------
    ItemBase("Gauntlets", "Gloves", (Implicit("armor", "flat", 130.0),)),
    ItemBase("Grips", "Gloves", (Implicit("attack_speed", "increased", 9.0),)),
    ItemBase("Handwraps", "Gloves", (Implicit("crit_chance", "flat", 5.0),)),
    ItemBase("Vambraces", "Gloves", (Implicit("attack_damage", "flat", 12.0),)),
    # -- Pants -----------------------------------------------------------
    ItemBase("Greaves", "Pants", (Implicit("armor", "flat", 250.0),)),
    ItemBase("Leggings", "Pants", (Implicit("evasion", "flat", 5.0),)),
    ItemBase("Kilt", "Pants", (Implicit("max_health", "flat", 130.0),)),
    ItemBase("Trousers", "Pants", (Implicit("max_energy_shield", "flat", 65.0),)),
    # -- Boots -----------------------------------------------------------
    ItemBase("Sabatons", "Boots", (Implicit("armor", "flat", 145.0),
                                   Implicit("movement_speed", "increased", 5.0))),
    ItemBase("Treads", "Boots", (Implicit("movement_speed", "increased", 12.0),)),
    ItemBase("Striders", "Boots", (Implicit("evasion", "flat", 3.0),
                                   Implicit("movement_speed", "increased", 8.0))),
    ItemBase("Sollerets", "Boots", (Implicit("max_health", "flat", 80.0),
                                    Implicit("movement_speed", "increased", 6.0))),
    # -- Belt ------------------------------------------------------------
    ItemBase("Girdle", "Belt", (Implicit("max_health", "flat", 130.0),)),
    ItemBase("Sash", "Belt", (Implicit("max_mana", "flat", 60.0),)),
    ItemBase("Cord", "Belt", (Implicit("health_regen", "flat", 1.3),)),
    ItemBase("Cinch", "Belt", (Implicit("armor", "flat", 150.0),)),
    # -- Ring ------------------------------------------------------------
    ItemBase("Band", "Ring", (Implicit("attack_damage", "flat", 10.0),)),
    ItemBase("Signet", "Ring", (Implicit("crit_multiplier", "flat", 16.0),)),
    ItemBase("Loop", "Ring", (Implicit("max_health", "flat", 60.0),)),
    ItemBase("Circle", "Ring", (Implicit("max_mana", "flat", 30.0),
                                Implicit("mana_regen", "flat", 0.5))),
    # -- Necklace --------------------------------------------------------
    ItemBase("Amulet", "Necklace", (Implicit("max_mana", "flat", 60.0),)),
    ItemBase("Pendant", "Necklace", (Implicit("crit_chance", "flat", 6.0),)),
    ItemBase("Torc", "Necklace", (Implicit("max_health", "flat", 95.0),)),
    ItemBase("Locket", "Necklace",
             (Implicit("max_energy_shield", "flat", 55.0),)),
    # -- Relic -----------------------------------------------------------
    ItemBase("Idol", "Relic", (Implicit("crit_multiplier", "flat", 28.0),)),
    ItemBase("Fetish", "Relic", (Implicit("area_of_effect", "increased", 10.0),)),
    ItemBase("Reliquary", "Relic",
             (Implicit("cooldown_reduction", "increased", 10.0),)),
    ItemBase("Effigy", "Relic", (Implicit("dot_frequency", "increased", 10.0),)),
    # -- Weapon, one-handed ----------------------------------------------
    _weapon("Sword", "Sword", 1, "Slashing", 1.30,
            Implicit("attack_damage", "flat", 40.0),
            Implicit("attack_speed", "increased", 5.0)),
    _weapon("Dagger", "Dagger", 1, "Piercing", 1.50,
            Implicit("attack_damage", "flat", 26.0),
            Implicit("crit_chance", "flat", 8.0)),
    _weapon("Axe", "Axe", 1, "Slashing", 1.25,
            Implicit("attack_damage", "flat", 46.0)),
    _weapon("Fist", "Fist", 1, "Blunt", 1.45,
            Implicit("attack_damage", "flat", 30.0),
            Implicit("attack_speed", "increased", 10.0)),
    # The flat damage comes first here for the same reason it does on every other
    # weapon: it is what a skill takes its percentage of. A Wand without it deals
    # nothing at all, because every skill deals a percent of weapon damage and a
    # percent of zero is zero -- including spells, which have no separate path.
    # The spell damage increase is a second implicit, not a replacement for the
    # first. Issue #146.
    _weapon("Wand", "Wand", 1, "Magic", 1.35,
            Implicit("attack_damage", "flat", 38.0),
            Implicit("spell_damage", "increased", 18.0)),
    _weapon("Whip", "Whip", 1, "Slashing", 1.40,
            Implicit("attack_damage", "flat", 32.0),
            Implicit("area_of_effect", "increased", 12.0)),
    # A shield is a one-handed WEAPON in this design, not an offhand: section V
    # lists it among the one-handed weapon types and states there are no offhand
    # items. It is the one weapon whose implicit is defensive, and that is what
    # the base IS rather than something a drop happened to roll.
    _weapon("Shield", "Shield", 1, "Blunt", 1.20,
            Implicit("block_chance", "flat", 12.0),
            Implicit("armor", "flat", 260.0)),
    _weapon("Crossbow", "Crossbow", 1, "Piercing", 1.35,
            Implicit("attack_damage", "flat", 38.0),
            Implicit("crit_multiplier", "flat", 20.0)),
    # -- Weapon, two-handed ----------------------------------------------
    _weapon("Greatsword", "Greatsword", 2, "Slashing", 1.25,
            Implicit("attack_damage", "flat", 78.0)),
    _weapon("Greataxe", "Greataxe", 2, "Slashing", 1.28,
            Implicit("attack_damage", "flat", 72.0),
            Implicit("crit_multiplier", "flat", 22.0)),
    _weapon("Spear", "Spear", 2, "Piercing", 1.35,
            Implicit("attack_damage", "flat", 64.0),
            Implicit("penetration", "flat", 6.0)),
    _weapon("Staff", "Staff", 2, "Magic", 1.30,
            Implicit("attack_damage", "flat", 66.0),
            Implicit("spell_damage", "increased", 32.0)),
    _weapon("Two-Handed Crossbow", "2H Crossbow", 2, "Piercing", 1.30,
            Implicit("attack_damage", "flat", 66.0),
            Implicit("crit_chance", "flat", 7.0)),
    _weapon("Warhammer", "Warhammer", 2, "Blunt", 1.20,
            Implicit("attack_damage", "flat", 84.0)),
)

BASES_BY_SLOT: dict[str, tuple[ItemBase, ...]] = {
    slot: tuple(b for b in ITEM_BASES if b.slot == slot) for slot in GEAR_SLOTS
}

WEAPON_BASES: tuple[WeaponBase, ...] = tuple(
    b for b in ITEM_BASES if isinstance(b, WeaponBase))


def attack_speed_of(*weapons: WeaponBase) -> float:
    """The attacks per second a loadout supplies, before any increase.

    One weapon supplies its own rate. Two weapons AVERAGE, which is the design
    decision recorded in docs/DECISIONS.md and what both Last Epoch and Path of
    Exile do -- Path of Exile reaches the average by alternating hands. It is
    what stops summed damage becoming a strict advantage: a dual wielder deals
    more per swing than either weapon alone but does not also swing at the
    faster weapon's rate.

    Not multiplied by anything. The two-handed multiplier applies to a weapon's
    implicits and affixes, and a rate is neither.
    """
    if not weapons:
        raise ValueError("a loadout has at least one weapon")
    if len(weapons) > 2:
        raise ValueError(f"a character holds one or two weapons, not {len(weapons)}")
    if len(weapons) == 2 and any(w.hands == 2 for w in weapons):
        raise ValueError(
            "a two-handed weapon fills both hands, so it cannot be paired")
    return sum(w.attack_speed for w in weapons) / len(weapons)


def bases_for(slot: str) -> tuple[ItemBase, ...]:
    if slot not in GEAR_SLOTS:
        raise ValueError(f"unknown gear slot {slot!r}; "
                         f"expected one of {sorted(GEAR_SLOTS)}")
    return BASES_BY_SLOT[slot]


def base_named(name: str) -> ItemBase:
    for base in ITEM_BASES:
        if base.name == name:
            return base
    raise ValueError(f"no item base named {name!r}")


# --------------------------------------------------------------------------
# Ailment affixes: chance to apply the effects the gems already grant
# --------------------------------------------------------------------------
#
# WHERE THESE CAME FROM. `game/Data/Gems.csv` already designs ten gems that
# apply an effect on hit, and `game/Data/StatusEffects.csv` defines what most of
# them do. The project owner asked for the same effects to be reachable as
# affixes, on weapons above all.
#
# WHY AN AFFIX AND A GEM BOTH. A gem is a socket and a socket is a commitment;
# an affix is a roll. Having both means a build that wants an ailment can chase
# it two ways, and one that wants it badly can do both. The gem stays the
# stronger source: an Of Rending gem reaches 150% chance at Cataclysmic against
# this affix's 15% at T7, so a socket is still where an ailment build lives.
#
# NOT ON ARMOUR. These only make sense where a hit comes from. The project owner
# named weapons, with necklace and relic as maybes; rings are in because rings
# take everything, being the flexible slot.

AILMENT_SLOTS = frozenset({"Weapon", "Necklace", "Relic", "Ring"})


@dataclass(frozen=True)
class AilmentAffix:
    """A chance to apply a named effect on hit.

    Not a stat affix: it grants no number on the character sheet. What it grants
    is a chance, and the effect it applies is defined in
    `game/Data/StatusEffects.csv` rather than here.
    """

    name: str
    ailment: str
    #: Percent chance to apply, at T7 on a fully upgraded piece.
    top_chance: float
    allowed_slots: frozenset[str] = AILMENT_SLOTS
    position: str = SUFFIX
    #: Which gem in `game/Data/Gems.csv` already applies the same effect.
    gem: str = ""

    def __post_init__(self) -> None:
        if self.position not in AFFIX_POSITIONS:
            raise ValueError(f"{self.name}: unknown position {self.position!r}")
        unknown = set(self.allowed_slots) - set(GEAR_SLOTS)
        if unknown:
            raise ValueError(
                f"{self.name} allows slots that do not exist: {sorted(unknown)}")

    def range_at(self, tier: int) -> tuple[float, float]:
        return tier_band(self.top_chance, tier)

    def chance_at(self, tier: int, roll: float = 1.0,
                  gear_level: int = MAX_GEAR_LEVEL) -> float:
        return affix_value(self.top_chance, tier, roll, gear_level)

    def slots_available(self) -> int:
        return slots_available_to(self.allowed_slots)


#: The six gems that apply damage over time, and the three that apply a
#: weakening effect. Values track the gem's own starting chance, five points
#: above it every time: the gem that applies poison starts at 20% and its affix
#: is 25, the gem that applies disease starts at 15% and its affix is 20, and the
#: seven gems that start at 10% all have a 15 affix.
BLEED = AilmentAffix("Chance to bleed", "Bleed", 15.0, gem="Of Rending")
POISON = AilmentAffix("Chance to poison", "Poison", 25.0, gem="Of The Viper")
DISEASE = AilmentAffix("Chance to disease", "Disease", 20.0, gem="Of Rot")
VOID_SPLINTER = AilmentAffix("Chance to apply void splinter", "Void Splinter",
                             15.0, gem="Of The Abyss")
MADNESS = AilmentAffix("Chance to madden", "Madness", 15.0, gem="Of Madness")
NECROSIS = AilmentAffix("Chance to necrose", "Necrosis", 15.0, gem="Of Wasting")
CRIPPLE = AilmentAffix("Chance to cripple", "Cripple", 15.0, gem="Of Maiming")
WEAKEN = AilmentAffix("Chance to weaken", "Weaken", 15.0, gem="Of Withering")
SHRED = AilmentAffix("Chance to shred", "Shred", 15.0, gem="Of Shredding")

#: Added for issue #152. Burn was the only player-applicable effect with neither
#: a gem nor an affix, so a Demonic character's burn chance from gear was always
#: zero and its magnitude could never rise above the base, while every one of the
#: sixteen designed Demonic skills applies it. Matched to bleed rather than to
#: poison because bleed is War's signature damage over time and burn is Demonic's:
#: the two sit in the same place in their damage types and should cost the same.
BURN = AilmentAffix("Chance to burn", "Burn", 15.0, gem="Of Embers")

#: Added for issue #298, answered by the project owner on 2026-08-16: an affix
#: does grant a chance to stun, and the Blunt weapon sub-type's 10% is part of
#: the same pool rather than a separate mechanic.
#:
#: 15%, THE SAME AS THE OTHER NINE, AND THE ISSUE ASKED WHY IT WOULD BE ANYTHING
#: ELSE. A chance-to-apply affix that did not match the shape of the four
#: weakening ones would need a reason, and there is none: it rolls on the same
#: four slots, in the same suffix position, at the same value.
#:
#: NO GEM, unlike every other entry here. Nothing in `game/Data/Gems.csv` applies
#: stun, so an affix is the only way to buy chance beyond what a Blunt weapon
#: gives free. That is also why the duration cap is hard to reach: eleven pieces
#: of gear carrying one each reach 165%, which is 1.65 times the base duration,
#: and the cap needs 400%.
#:
#: LAST EPOCH SELLS THE SAME STAT, which is what settled that the affix should
#: exist at all. Its "Increased Stun Chance" suffix runs 15% to 25% at its first
#: tier and 131% to 170% at its seventh. Path of Exile argues the other way and
#: has no such stat: stun there is decided by damage against a threshold, so
#: there is nothing to buy. This design already chose the Last Epoch shape by
#: giving Blunt a flat chance, so an affix scaling it is consistent rather than
#: new.
STUN = AilmentAffix("Chance to stun", "Stun", 15.0)

AILMENT_AFFIXES: tuple[AilmentAffix, ...] = (
    BLEED, POISON, DISEASE, VOID_SPLINTER, NECROSIS, BURN, MADNESS, CRIPPLE,
    WEAKEN, SHRED, STUN,
)

#: The effects among those that are damage over time rather than a weakening.
#: Taken from `game/Data/StatusEffects.csv`, where each is listed with EffectKind
#: "DoT". Damage over time matters separately because the design says it bypasses
#: energy shield and holds it empty, which is what makes it the answer to shield
#: stacking rather than a stat check.
DAMAGE_OVER_TIME_AILMENTS = frozenset({"Bleed", "Poison", "Disease",
                                       "Void Splinter", "Necrosis", "Burn"})


def ailments_for(slot: str) -> tuple[AilmentAffix, ...]:
    if slot not in GEAR_SLOTS:
        raise ValueError(f"unknown gear slot {slot!r}; "
                         f"expected one of {sorted(GEAR_SLOTS)}")
    return tuple(a for a in AILMENT_AFFIXES if slot in a.allowed_slots)


# --------------------------------------------------------------------------
# What happens above 100% chance to apply
# --------------------------------------------------------------------------

#: An enemy carries at most one stack of any effect the player applies.
#:
#: Stated by the project owner 2026-08-03. It is what makes chance above 100%
#: mean something instead of being wasted, and it keeps a screen full of enemies
#: readable: one enemy has bleeding or it does not.
MAX_STACKS_ON_AN_ENEMY = 1

#: Chance to apply caps here. Everything past it becomes magnitude.
AILMENT_CHANCE_CAP = 100.0


def ailment_application(total_chance: float) -> tuple[float, float]:
    """Chance to apply an effect, and the multiplier on its magnitude.

    Stated by the project owner 2026-08-03:

        DoT chance caps at 100%, anything beyond 100% applies to the magnitude
        of the DoT's effect. So you can only ever have 1 stack of something on
        an enemy, however if you have 800% chance to apply it, it gets a 700%
        multiplier.

    So 800% chance applies the effect every hit at eight times its magnitude,
    which is a 700% increase over the one time it would otherwise be worth.

    WHY IT MATTERS. Ailment chance comes from two sources that both scale hard:
    affixes here and gems in `game/Data/Gems.csv`, where the gem applying bleed
    reaches 150% chance on its own at Cataclysmic rarity. Without this rule a
    build stacking both would hit a ceiling and every point past it would be
    dead, which would make an ailment build stop progressing at exactly the
    point it should be coming together.

    `total_chance` is the sum across every source: affixes, gems, keystones and
    enchantments alike.
    """
    if total_chance < 0.0:
        raise ValueError(f"a chance to apply of {total_chance}% is not a chance")
    applied = min(AILMENT_CHANCE_CAP, total_chance)
    magnitude = max(1.0, total_chance / AILMENT_CHANCE_CAP)
    return applied, magnitude


# --------------------------------------------------------------------------
# Hybrid affixes: two stats on one roll, less of each
# --------------------------------------------------------------------------
#
# The same trade the resistance families already make, applied to the rest of the
# pool. A hybrid is worth more in total than either single affix and less of
# either stat, so it wins a slot when a build needs both and loses when it needs
# one badly.
#
# THE REDUCTION IS DERIVED, NOT PICKED. It is the ratio the project owner already
# set between the two-resistance affix and the single-resistance one, 14 against
# 20. Reading it off those two rather than writing 0.7 here means the whole pool
# moves together if that ratio ever changes.

HYBRID_FRACTION = HYBRID_RESISTANCE.top_value / SINGLE_RESISTANCE.top_value


@dataclass(frozen=True)
class HybridAffix:
    """One roll granting two stats, each at `HYBRID_FRACTION` of its own affix.

    Defined in terms of the single affixes it combines rather than by copying
    their numbers, so it cannot drift from them.
    """

    name: str
    parts: tuple[StatAffix, StatAffix]
    allowed_slots: frozenset[str] = frozenset()
    position: str = PREFIX

    def __post_init__(self) -> None:
        first, second = self.parts
        if first.position != second.position:
            raise ValueError(
                f"{self.name} combines a {first.position} with a "
                f"{second.position}; a hybrid has to sit in one pool")
        if first.stat == second.stat:
            raise ValueError(f"{self.name} combines a stat with itself")
        if self.position != first.position:
            raise ValueError(
                f"{self.name} is a {self.position} but its parts are "
                f"{first.position}es")
        unknown = set(self.allowed_slots) - set(GEAR_SLOTS)
        if unknown:
            raise ValueError(
                f"{self.name} allows slots that do not exist: {sorted(unknown)}")
        if not self.allowed_slots:
            raise ValueError(f"{self.name} can appear on no slot at all")
        for part in self.parts:
            outside = set(self.allowed_slots) - set(part.allowed_slots)
            if outside:
                raise ValueError(
                    f"{self.name} can roll on {sorted(outside)}, where "
                    f"{part.name} cannot")

    def value_of(self, part: StatAffix, tier: int = 7, roll: float = 1.0,
                 gear_level: int = MAX_GEAR_LEVEL) -> float:
        if part not in self.parts:
            raise ValueError(f"{part.name} is not part of {self.name}")
        return part.value_at(tier, roll, gear_level) * HYBRID_FRACTION

    def values_at(self, tier: int = 7, roll: float = 1.0,
                  gear_level: int = MAX_GEAR_LEVEL) -> dict[str, float]:
        return {p.stat: self.value_of(p, tier, roll, gear_level)
                for p in self.parts}


def _hybrid(name: str, first: StatAffix, second: StatAffix) -> HybridAffix:
    """Slots are the intersection of the two parts', so a hybrid can never reach
    a slot one of its halves could not."""
    return HybridAffix(
        name=name, parts=(first, second),
        allowed_slots=first.allowed_slots & second.allowed_slots,
        position=first.position)


HYBRID_AFFIXES: tuple[HybridAffix, ...] = (
    # Prefixes: pairs of defensive layers, so a hybrid is a build committing to
    # two at once rather than going deep on one.
    _hybrid("Health and armor", FLAT_HEALTH, FLAT_ARMOR),
    _hybrid("Health and energy shield", FLAT_HEALTH, FLAT_ENERGY_SHIELD),
    _hybrid("Armor and evasion", FLAT_ARMOR, FLAT_EVASION),
    _hybrid("Evasion and energy shield", FLAT_EVASION, FLAT_ENERGY_SHIELD),
    _hybrid("Mana and energy shield", FLAT_MANA, FLAT_ENERGY_SHIELD),
    _hybrid("Increased health and armor", INCREASED_HEALTH, INCREASED_ARMOR),
    # ONE MINION HYBRID, AND ITS SLOT LIST IS DERIVED RATHER THAN CHOSEN. A
    # hybrid can only appear where both halves roll, and Ring is the only slot
    # shared by the offensive slots minion damage uses and the defensive ones
    # minion health uses. Issue #337.
    _hybrid("Minion damage and minion health",
            INCREASED_MINION_DAMAGE, INCREASED_MINION_HEALTH),
    # Suffixes: pairs that a single build wants together, which is what makes
    # giving up 30% of each worth doing.
    _hybrid("Attack speed and critical strike chance",
            INCREASED_ATTACK_SPEED, INCREASED_CRIT_CHANCE),
    _hybrid("Critical strike chance and multiplier",
            FLAT_CRIT_CHANCE, FLAT_CRIT_MULTIPLIER),
    _hybrid("Health and mana regeneration",
            FLAT_HEALTH_REGEN, FLAT_MANA_REGEN),
    _hybrid("Penetration and critical strike multiplier",
            FLAT_PENETRATION, FLAT_CRIT_MULTIPLIER),
    _hybrid("Block chance and crowd control resistance",
            FLAT_BLOCK_CHANCE, FLAT_CROWD_CONTROL_RESISTANCE),
    _hybrid("Magic find and loot quantity",
            FLAT_MAGIC_FIND, INCREASED_LOOT_QUANTITY),
)


def hybrids_for(slot: str, position: str | None = None
                ) -> tuple[HybridAffix, ...]:
    if slot not in GEAR_SLOTS:
        raise ValueError(f"unknown gear slot {slot!r}; "
                         f"expected one of {sorted(GEAR_SLOTS)}")
    return tuple(h for h in HYBRID_AFFIXES
                 if slot in h.allowed_slots
                 and (position is None or h.position == position))


# --------------------------------------------------------------------------
# Affix groups: what stops one item rolling the same thing twice
# --------------------------------------------------------------------------
#
# Issue #128. Slot restrictions stop a weapon rolling health and a chest plate
# rolling damage, but nothing stopped a four-affix Masterful item rolling "Flat
# maximum health" four times over. Every affix in the pool was restricted against
# the slot and against nothing else, least of all itself.
#
# THE RULE. An affix occupies one GROUP for every stat it grants, named by the
# stat and the kind together. An item holds at most one affix from any group.
#
# WHERE THE SHAPE COMES FROM. Path of Exile calls this a mod group and it is the
# only mechanism that makes modifiers on one item mutually exclusive: a group is
# a string identifier shared by one or more modifiers, and only one modifier from
# a group may exist on an item at a time. Path of Exile 2 keeps the same rule and
# the same name. What differs here is that the group is DERIVED from what the
# affix grants rather than typed onto each modifier by hand, so a new affix
# cannot be added without a group and two affixes granting the same stat in the
# same kind cannot be given different groups by mistake.
#
# WHAT THE RULE DECIDES, in the three cases the issue raised:
#
#   Flat and increased are different groups, so one piece may carry both "Flat
#   maximum health" and "Increased maximum health". That is deliberate: the
#   design says neither kind is strictly better and that is the reason for having
#   both, so an item carrying one of each is the design working rather than a
#   duplicate slipping through.
#
#   A hybrid occupies the group of EACH of its parts, so "Health and armor"
#   cannot share a piece with "Flat maximum health" or with "Flat armor". A
#   hybrid grants 70% of each part, so allowing it beside its own part is the
#   same stat twice on one piece, which is what the rule exists to stop. Path of
#   Exile 2 goes the other way and gives a hybrid its own group; this project has
#   two prefix slots per piece against that game's three, so the same allowance
#   concentrates far more here.
#
#   Resistance is grouped BY DAMAGE TYPE, not by family. A single-resistance roll
#   occupies only the group of the type it rolled, so two of them covering
#   different types may share a piece; an all-resistance roll occupies all eight
#   groups and so excludes every other resistance affix. That falls out of the
#   rule rather than being a separate one, because the eight resistances are
#   eight stats on the character sheet.
#
# Prefixes and suffixes cannot share a group at all, because a stat that appears
# as a prefix never appears as a suffix. `_check_the_two_positions_are_separate_
# pools` already holds that, so the two pools are group-disjoint for free.

def stat_group(stat: str, kind: str) -> str:
    """The group key for granting one stat in one kind."""
    return f"{stat}.{kind}"


def ailment_group(ailment: str) -> str:
    """The group key for a chance to apply one named effect.

    An ailment affix grants no stat, so it cannot use `stat_group`. What it
    grants is a chance at one named effect, and two rolls of the same chance on
    one piece is the same duplicate the rule exists to stop.
    """
    return f"ailment.{ailment}"


def resistance_group(damage_type: str) -> str:
    """The group key for resistance to one damage type."""
    if damage_type not in DAMAGE_TYPES:
        raise ValueError(f"unknown damage type {damage_type!r}; "
                         f"expected one of {list(DAMAGE_TYPES)}")
    return stat_group(f"resistance_{damage_type.lower()}", "flat")


def groups_of(affix, covers: tuple[str, ...] = ()) -> frozenset[str]:
    """Every group `affix` occupies on an item.

    `covers` is which damage types a resistance roll landed on, which is decided
    when the item drops rather than by the family. It is required for a
    resistance family and must be empty for everything else.
    """
    if isinstance(affix, AffixFamily):
        if len(set(covers)) != affix.breadth:
            raise ValueError(
                f"{affix.name} covers {affix.breadth} damage type"
                f"{'s' if affix.breadth > 1 else ''}; "
                f"got {list(covers)}")
        return frozenset(resistance_group(t) for t in covers)
    if covers:
        raise ValueError(
            f"{affix.name} is not a resistance family, so it covers no damage "
            f"types of its own; got {list(covers)}")
    if isinstance(affix, StatAffix):
        return frozenset({stat_group(affix.stat, affix.kind)})
    if isinstance(affix, HybridAffix):
        return frozenset(stat_group(p.stat, p.kind) for p in affix.parts)
    if isinstance(affix, AilmentAffix):
        return frozenset({ailment_group(affix.ailment)})
    raise TypeError(f"{affix!r} is not an affix this pool knows about")


def may_join(affix, taken: frozenset[str] | set[str],
             covers: tuple[str, ...] = ()) -> bool:
    """Whether `affix` may be added to an item already holding `taken` groups."""
    return not (groups_of(affix, covers) & set(taken))


def draw_without_repeating_a_group(candidates, count: int, rng,
                                   group_of=groups_of) -> tuple:
    """Draw `count` affixes at random, never two from one group.

    This is the drop roll's inner step: draw without replacement, and treat a
    whole group as drawn once any of its members is. `rng` is a `random.Random`.
    `group_of` is how to read an affix's groups, so a caller that has already
    decided which damage types a resistance roll covers can pass its own.

    Raises `ValueError` if the candidates cannot supply `count` distinct groups,
    which is a fault in the pool rather than an unlucky roll.
    """
    if count < 0:
        raise ValueError(f"cannot draw {count} affixes")
    order = list(candidates)
    rng.shuffle(order)
    taken: set[str] = set()
    drawn: list = []
    for candidate in order:
        if len(drawn) == count:
            break
        groups = group_of(candidate)
        if groups & taken:
            continue
        taken |= groups
        drawn.append(candidate)
    if len(drawn) < count:
        raise ValueError(
            f"asked for {count} affixes but the {len(order)} candidates supply "
            f"only {len(drawn)} distinct groups")
    return tuple(drawn)


def everything_for(slot: str, position: str) -> tuple:
    """Every affix a drop could roll on one slot in one position.

    `pool_for` returns the stat affixes alone. A drop rolls from the hybrids, the
    ailment affixes and the resistance families as well, and the group rule has
    to hold across all four kinds rather than within each one.
    """
    if slot not in GEAR_SLOTS:
        raise ValueError(f"unknown gear slot {slot!r}; "
                         f"expected one of {sorted(GEAR_SLOTS)}")
    if position not in AFFIX_POSITIONS:
        raise ValueError(f"unknown position {position!r}; "
                         f"expected one of {list(AFFIX_POSITIONS)}")
    out: list = list(pool_for(slot, position))
    out.extend(hybrids_for(slot, position))
    if position == SUFFIX:
        out.extend(a for a in ailments_for(slot))
        if slot in RESISTANCE_SLOTS:
            out.extend(RESISTANCE_FAMILIES)
    return tuple(out)


def distinct_groups_for(slot: str, position: str) -> int:
    """How many affixes one piece could hold in a position before the group rule
    stops it. A resistance family counts once per damage type it can reach."""
    groups: set[str] = set()
    for affix in everything_for(slot, position):
        if isinstance(affix, AffixFamily):
            groups |= {resistance_group(t) for t in DAMAGE_TYPES}
        else:
            groups |= groups_of(affix)
    return len(groups)


def total_pool_size() -> int:
    """Everything a drop could roll, counting each resistance family once."""
    return (len(AFFIX_POOL) + len(RESISTANCE_FAMILIES) + len(AILMENT_AFFIXES)
            + len(HYBRID_AFFIXES))


# --------------------------------------------------------------------------
# What has to stay true as the pool grows
# --------------------------------------------------------------------------

def _check_every_affix_tier_has_a_drop_weight() -> None:
    missing = set(AFFIX_TIERS) - set(AFFIX_TIER_DROP_WEIGHT)
    extra = set(AFFIX_TIER_DROP_WEIGHT) - set(AFFIX_TIERS)
    if missing or extra:
        raise AssertionError(
            "the affix tier drop weights do not match the seven tiers: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_a_higher_affix_tier_is_never_more_likely() -> None:
    """A better roll must not be commoner than a worse one at any step."""
    weights = [AFFIX_TIER_DROP_WEIGHT[t] for t in AFFIX_TIERS]
    for lower, higher in zip(weights, weights[1:], strict=False):
        if higher > lower:
            raise AssertionError(
                f"the affix tier weights rise somewhere along the ladder: "
                f"{weights}")
        if higher <= 0.0:
            raise AssertionError(
                f"an affix tier has a weight of {higher}, so it can never roll")


def _check_every_slot_can_fill_all_four_of_its_affixes() -> None:
    """A slot with fewer prefixes than it has prefix slots would roll duplicates
    or blanks. Checked per position, because the split makes the two separate
    problems: a slot could have plenty of suffixes and no prefixes at all."""
    for slot in GEAR_SLOTS:
        for position, needed in ((PREFIX, PREFIXES_PER_PIECE),
                                 (SUFFIX, SUFFIXES_PER_PIECE)):
            available = len(pool_for(slot, position))
            if available < needed:
                raise ValueError(
                    f"{slot} has {available} {position}es available but "
                    f"{needed} {position} slots to fill")


def _check_the_affix_tier_gate_covers_every_difficulty_tier() -> None:
    """The DROP gate has to reach the top and never exceed it, at both ends.

    A gate that never reaches T7 makes the top tier undroppable. A gate that
    exceeds the affix tiers would index outside TIER_FRACTIONS and raise on the
    first drop.

    Checks the drop cap rather than `max_affix_tier`, because crafting is
    uncapped by the project owner's decision on issue #241 and so has nothing
    left to check.
    """
    reached = [max_affix_tier_on_a_drop(t)
               for t in range(1, DIFFICULTY_TIERS + 1)]
    # Checked FIRST so a gate that runs off the end of the tier list is named
    # for what it is. Checking the top value first would report a gate reaching
    # T8 as "T7 can never be reached", which is both wrong and unhelpful.
    for tier, cap in enumerate(reached, 1):
        if cap not in AFFIX_TIERS:
            raise ValueError(
                f"difficulty tier {tier} allows affix tier {cap}, which is not "
                f"one of {list(AFFIX_TIERS)}")
    if max(reached) != AFFIX_TIERS[-1]:
        raise ValueError(
            f"the affix tier gate tops out at T{max(reached)}, so T"
            f"{AFFIX_TIERS[-1]} can never be reached and the Potency Crystal "
            "has nothing to raise an affix to")
    # AT LEAST T1, not exactly T1. The cap is the ceiling of the draw, and the
    # draw runs from T1 up to it, so a cap of T2 at difficulty tier 1 still
    # drops T1 items. Requiring equality here is what the rule said before the
    # project owner raised the cap to the tier plus one on issue #241.
    if min(reached) < AFFIX_TIERS[0]:
        raise ValueError(
            f"the affix tier gate tops out at T{min(reached)} somewhere, which "
            f"is below T{AFFIX_TIERS[0]}, so nothing can drop at all there")
    if reached != sorted(reached):
        raise ValueError(
            f"the affix tier gate falls as the difficulty tier rises: {reached}")


def _check_every_slot_can_fill_its_affixes_without_repeating_a_group() -> None:
    """The check above counts affixes; this one counts GROUPS.

    A slot could have four prefixes available and only one group between them,
    in which case the group rule from issue #128 leaves the second prefix slot
    unfillable. Counting affixes cannot see that, so it is counted separately.
    """
    for slot in GEAR_SLOTS:
        for position, needed in ((PREFIX, PREFIXES_PER_PIECE),
                                 (SUFFIX, SUFFIXES_PER_PIECE)):
            available = distinct_groups_for(slot, position)
            if available < needed:
                raise ValueError(
                    f"{slot} offers {available} distinct {position} group"
                    f"{'s' if available != 1 else ''} but has {needed} "
                    f"{position} slots to fill, so one of them could not be "
                    "filled without repeating a group")


def _check_the_two_positions_are_separate_pools() -> None:
    """A stat appearing in both would let one item carry four of it, which is
    exactly what the split exists to prevent."""
    by_stat: dict[str, set[str]] = {}
    for affix in AFFIX_POOL:
        by_stat.setdefault(affix.stat, set()).add(affix.position)
    straddling = {s: sorted(p) for s, p in by_stat.items() if len(p) > 1}
    if straddling:
        raise ValueError(
            f"stats appearing as both a prefix and a suffix: {straddling}")


def _check_no_two_affixes_are_the_same_thing() -> None:
    seen: dict[tuple[str, str], str] = {}
    for affix in AFFIX_POOL:
        key = (affix.stat, affix.kind)
        if key in seen:
            raise ValueError(
                f"{affix.name} and {seen[key]} both grant {affix.kind} "
                f"{affix.stat}")
        seen[key] = affix.name


def _check_every_slot_offers_a_real_choice_of_base() -> None:
    """One base in a slot is not a choice. The point of bases is that picking one
    commits a character to a defensive layer or an offensive property before any
    affix is involved, and that only exists if there is something to pick."""
    for slot in GEAR_SLOTS:
        bases = bases_for(slot)
        if len(bases) < 3:
            raise ValueError(
                f"{slot} has {len(bases)} bases; a slot needs at least three "
                "for the choice to mean anything")
        names = [b.name for b in bases]
        if len(names) != len(set(names)):
            raise ValueError(f"{slot} has two bases with the same name")


def _check_bases_in_a_slot_are_actually_different() -> None:
    """Two bases granting the same thing are one base written twice."""
    for slot in GEAR_SLOTS:
        seen: dict[tuple, str] = {}
        for base in bases_for(slot):
            key = tuple(sorted((i.stat, i.kind, i.value)
                               for i in base.implicits))
            if key in seen:
                raise ValueError(
                    f"{base.name} and {seen[key]} grant the same implicits")
            seen[key] = base.name


def _check_every_base_grants_a_stat_something_reads() -> None:
    for base in ITEM_BASES:
        for implicit in base.implicits:
            if implicit.stat not in AFFIXABLE_STATS:
                raise ValueError(
                    f"{base.name} grants {implicit.stat!r}, which is neither on "
                    "the character sheet nor an off-sheet stat")


def _check_the_weapon_types_match_the_design() -> None:
    """`Cataclysm_GDD_v2.md` section V lists eight one-handed and six two-handed
    weapon types. A base for a weapon the design does not have, or a design
    weapon with no base, would both be wrong."""
    designed_one_handed = {"Sword", "Dagger", "Axe", "Fist", "Wand", "Whip",
                           "Shield", "Crossbow"}
    designed_two_handed = {"Greatsword", "Greataxe", "Spear", "Staff",
                           "2H Crossbow", "Warhammer"}
    for hands, designed in ((1, designed_one_handed), (2, designed_two_handed)):
        present = {b.weapon_type for b in WEAPON_BASES if b.hands == hands}
        if present != designed:
            raise ValueError(
                f"{hands}-handed weapon bases do not match the design: "
                f"missing {sorted(designed - present)}, "
                f"unexpected {sorted(present - designed)}")


def _check_dual_wielding_carries_more_damage_types_than_a_two_hander() -> None:
    """The design says dual wielding is the primary route to multiclassing
    because it is how a player carries more damage types at once.

    COMPARING THE RAW MAXIMUMS NO LONGER PROVES THIS, and that is why this check
    walks the tiers instead. Two one-handers top out at eight types and so does a
    two-hander, so the maximums tie. What keeps the design's sentence true is the
    tier cap: a one-hander reaches its limit of four at tier 4, so a dual wielder
    has all eight from tier 4 onward, while a two-hander gains one type per tier
    and does not reach eight until tier 8.

    So the claim to hold is: at every tier a dual wielder carries at least as many
    damage types as a two-hander, and strictly more below the last tier.

    ONE CONDITION, NOT TWO. An earlier version of this check tested "is the dual
    wielder behind" separately from "have they tied early". The first of those can
    never fire: a two-hander gains exactly one type per tier, so it cannot
    overtake a dual wielder without passing through equality first, and the tie
    test always catches it. A branch that cannot fire is not a check.
    """
    for tier in range(1, DIFFICULTY_TIERS + 1):
        dual = max_damage_types(1, tier) * 2
        two_handed = max_damage_types(2, tier)
        last_tier = tier == DIFFICULTY_TIERS
        held = dual >= two_handed if last_tier else dual > two_handed
        if not held:
            raise ValueError(
                f"at tier {tier} two one-handers hold {dual} damage types and a "
                f"two-hander holds {two_handed}. A dual wielder must lead at "
                f"every tier below {DIFFICULTY_TIERS} and may only be caught at "
                f"{DIFFICULTY_TIERS} itself, or dual wielding is not the route "
                "to multiclassing the design says")


def _check_no_weapon_rolls_a_defensive_affix() -> None:
    """Armour and jewellery defend; a weapon does not.

    AFFIXES ONLY. A shield's block chance and armour are implicits, and the
    design lists Shield among the one-handed weapon types with no offhand slot
    to put it in. What a base IS may be defensive; what a drop happened to roll
    on a weapon may not.
    """
    defensive = {"max_health", "max_energy_shield", "armor", "evasion",
                 "block_chance", "damage_reduction"} | set(RESISTANCE_STATS)
    for affix in pool_for("Weapon"):
        if affix.stat in defensive:
            raise ValueError(
                f"{affix.name} can roll on a weapon, and it defends")


def _check_only_the_shield_defends_among_weapon_bases() -> None:
    """So the exemption above stays one named exception rather than a hole."""
    defensive = {"max_health", "max_energy_shield", "armor", "evasion",
                 "block_chance", "damage_reduction"} | set(RESISTANCE_STATS)
    for base in WEAPON_BASES:
        if base.weapon_type == "Shield":
            continue
        granted = {i.stat for i in base.implicits}
        if granted & defensive:
            raise ValueError(
                f"the {base.name} base grants {sorted(granted & defensive)}, "
                "and only the Shield may defend among weapons")


def _check_every_weapon_but_the_shield_supplies_damage() -> None:
    """A weapon with no flat attack damage makes every skill deal nothing.

    Every skill deals a percent of weapon damage -- see
    `character.Skill.weapon_damage_percent` -- and spells have no separate path.
    Weapon damage comes from this implicit and nowhere else, so a base without
    one is a weapon a character can hold and deal exactly zero with, whatever
    their stats say.

    The Wand and the Staff shipped that way. Both gave only INCREASED spell
    damage, which multiplies a damage number they did not supply. Issue #146.

    The Shield is the one exemption, and for the same reason it is exempt from
    the defensive check above: it is not there to hit anything.
    """
    for base in WEAPON_BASES:
        if base.weapon_type == "Shield":
            continue
        supplies_damage = any(i.stat == "attack_damage" and i.kind == "flat"
                              for i in base.implicits)
        if not supplies_damage:
            raise ValueError(
                f"the {base.name} base supplies no flat attack damage, so every "
                "skill used with it deals a percent of zero. Only the Shield may "
                "do that.")


def _check_every_gem_applied_effect_is_reachable_as_an_affix() -> None:
    """`game/Data/Gems.csv` designs ten gems that apply an effect on hit. The
    project owner asked for the same effects to be reachable as affixes, so a
    gem effect with no affix would be one the request missed.

    This set is written out rather than read from the CSV, so it does not
    protect against a gem being added with no affix; issue #152 was exactly that
    and went unnoticed because burn was in neither place.

    ONE DIRECTION ONLY, SINCE 2026-08-16. This compared the two sets for
    equality, which also required every AFFIX to have a gem -- and that was never
    a stated rule, only a fact about the ten that existed. The chance to stun
    added for issue #298 has no gem, deliberately: nothing in `Gems.csv` applies
    stun, which is what makes the affix the only way to buy the chance. Requiring
    the reverse would have meant inventing a gem to satisfy a check.
    """
    from_gems = {"Void Splinter", "Poison", "Bleed", "Madness", "Disease",
                 "Necrosis", "Burn", "Cripple", "Weaken", "Shred"}
    covered = {a.ailment for a in AILMENT_AFFIXES}
    missing = from_gems - covered
    if missing:
        raise ValueError(
            f"gem effects with no affix: {sorted(missing)}. Every effect a gem "
            f"applies has to be reachable as an affix too.")


def _check_ailments_only_appear_where_a_hit_comes_from() -> None:
    for affix in AILMENT_AFFIXES:
        if "Weapon" not in affix.allowed_slots:
            raise ValueError(f"{affix.name} cannot appear on a weapon")
        armour = set(affix.allowed_slots) & {
            "Head", "Chest", "Shoulders", "Gloves", "Pants", "Boots", "Belt"}
        if armour:
            raise ValueError(
                f"{affix.name} can roll on {sorted(armour)}, which no hit comes "
                "from")


def _check_every_rarity_has_a_composition() -> None:
    """The eight rarities, what fills their slots, and the shape of the curve.

    A rarity missing from the table would raise on every lookup, which is loud.
    What this really guards is the SHAPE: enchantments must climb as rarity
    rises, regular affixes must not climb once enchantments start taking slots,
    and no rarity may fill more slots than a piece has.
    """
    missing = set(RARITIES) - set(RARITY_COMPOSITION)
    extra = set(RARITY_COMPOSITION) - set(RARITIES)
    if missing or extra:
        raise ValueError(
            f"the rarity composition table does not cover the rarities: "
            f"missing {sorted(missing)}, unknown {sorted(extra)}")

    enchantments = [enchantments_for(r) for r in RARITIES]
    if enchantments != sorted(enchantments):
        raise ValueError(f"enchantments fall as rarity rises: {enchantments}")

    for rarity in RARITIES:
        filled = enchantments_for(rarity) + affix_slots_for(rarity)
        if filled > AFFIX_SLOTS_PER_PIECE:
            raise ValueError(
                f"{rarity} fills {filled} slots but a piece has "
                f"{AFFIX_SLOTS_PER_PIECE}")
        if filled < 1:
            raise ValueError(f"{rarity} fills no slots at all")

    # Below the first enchantable rarity a piece fills only as many slots as it
    # has affixes; from there it fills all four. Anything else would leave a
    # rarity that no combination of contents can produce.
    for rarity in CRAFTABLE_RARITIES:
        if enchantments_for(rarity) != 0:
            raise ValueError(f"{rarity} carries an enchantment, so it is not "
                             "reachable by adding affixes")
    for rarity in RARITIES[len(CRAFTABLE_RARITIES):]:
        filled = enchantments_for(rarity) + affix_slots_for(rarity)
        if filled != AFFIX_SLOTS_PER_PIECE:
            raise ValueError(
                f"{rarity} fills {filled} of {AFFIX_SLOTS_PER_PIECE} slots; "
                "an enchantment takes an affix's slot rather than adding one")


def _check_the_affix_budget_is_the_best_rarity_on_every_piece() -> None:
    """72 is what eighteen MASTERFUL pieces reach, not eighteen Cataclysmic ones.

    Every affix value in the pool was fitted against 72 regular affix slots. A
    Cataclysmic piece has none, because all four of its slots hold enchantments,
    so the budget belongs to the best rarity that spends nothing on them. Stating
    it here means the two cannot part company.
    """
    best = max(affix_slots_for(r) for r in RARITIES)
    if GEAR_PIECES * best != TOTAL_AFFIX_SLOTS:
        raise ValueError(
            f"{GEAR_PIECES} pieces at {best} regular affixes is "
            f"{GEAR_PIECES * best}, but the affix budget is {TOTAL_AFFIX_SLOTS}")

    richest = [r for r in RARITIES if affix_slots_for(r) == best]
    if CRAFTABLE_RARITIES[-1] not in richest:
        raise ValueError(
            f"{CRAFTABLE_RARITIES[-1]} does not carry the most regular affixes; "
            f"{richest} do")


def _check_rarity_is_decided_by_what_fills_the_slots() -> None:
    """`rarity_of` and the composition table must be inverses.

    Rarity is a label for the contents rather than a property the item carries,
    so every rarity must be produced by exactly one combination and every legal
    combination must name a rarity. If they disagreed, adding an affix to an
    Everyday item could produce something that is not a Quality item, and the
    promotion rule would silently stop working.
    """
    seen: dict[tuple[int, int], str] = {}
    for rarity, composition in RARITY_COMPOSITION.items():
        if composition in seen:
            raise ValueError(
                f"{rarity} and {seen[composition]} are both {composition[0]} "
                f"enchantments and {composition[1]} affixes")
        seen[composition] = rarity
        if rarity_of(*composition) != rarity:
            raise ValueError(
                f"{composition} names {rarity_of(*composition)}, not {rarity}")

    for count in range(1, AFFIX_SLOTS_PER_PIECE + 1):
        rarity = rarity_for_affix_count(count)
        if affix_slots_for(rarity) != count or enchantments_for(rarity) != 0:
            raise ValueError(
                f"{count} affixes and no enchantment makes a {rarity}, which "
                f"carries {affix_slots_for(rarity)} affixes and "
                f"{enchantments_for(rarity)} enchantments")


def _check_a_split_never_exceeds_either_cap() -> None:
    """Neither side of a prefix and suffix split may pass its own cap, and the
    two must always add back to the slot count."""
    for slots in range(AFFIX_SLOTS_PER_PIECE + 1):
        prefixes, suffixes = prefix_suffix_split(slots)
        if prefixes + suffixes != slots:
            raise ValueError(f"{slots} slots split into {prefixes}+{suffixes}")
        if prefixes > PREFIXES_PER_PIECE or suffixes > SUFFIXES_PER_PIECE:
            raise ValueError(
                f"{slots} slots split into {prefixes} prefixes and {suffixes} "
                f"suffixes, past the caps of {PREFIXES_PER_PIECE} and "
                f"{SUFFIXES_PER_PIECE}")


def _check_the_two_loadouts_have_equal_affix_value() -> None:
    """A two-hander and two one-handers must be worth the same in affixes.

    This is the reason TWO_HANDED_MULTIPLIER is 2.0 rather than anything else,
    and section VII of the design document requires it: two one-handed weapons
    count as one equipped piece for Power Score so that dual wielding is not
    worth free power. The rating model scores both loadouts the same, so
    whichever side had the larger affix budget would carry power its rating does
    not count.

    Asserted rather than trusted, because the equality only holds while a weapon
    has exactly AFFIX_SLOTS_PER_PIECE slots and a dual wielder exactly two
    weapons. Change either and this fails, which is the point.
    """
    two_handed = AFFIX_SLOTS_PER_PIECE * TWO_HANDED_MULTIPLIER
    dual_wield = AFFIX_SLOTS_PER_PIECE * 2
    if abs(two_handed - dual_wield) > 1e-9:
        raise ValueError(
            f"a two-handed weapon is worth {two_handed} one-handed affix slots "
            f"and two one-handed weapons are worth {dual_wield}. Dual wielding "
            "must not be worth free power; see section VII.")

    pieces = GEAR_PIECES_WITH_AN_OFFHAND - GEAR_PIECES
    if pieces != 1:
        raise ValueError(
            f"filling the offhand adds {pieces} pieces; it should add exactly "
            "the one held item, whether that is a second weapon or a Shield")


def _check_only_a_two_handed_weapon_multiplies_its_values() -> None:
    """Nothing but a two-handed weapon may be worth more than face value.

    An armour base or a one-handed weapon quietly gaining a multiplier would
    break the equality above without any of the counts changing, so it would not
    be caught by the check before this one.
    """
    for base in ITEM_BASES:
        expected = (TWO_HANDED_MULTIPLIER
                    if isinstance(base, WeaponBase) and base.hands == 2
                    else 1.0)
        if abs(base.value_multiplier - expected) > 1e-9:
            raise ValueError(
                f"{base.name} multiplies its values by "
                f"{base.value_multiplier}, expected {expected}")


_check_every_affix_tier_has_a_drop_weight()
_check_a_higher_affix_tier_is_never_more_likely()
_check_every_slot_can_fill_all_four_of_its_affixes()
_check_the_affix_tier_gate_covers_every_difficulty_tier()
_check_every_slot_can_fill_its_affixes_without_repeating_a_group()
_check_the_two_positions_are_separate_pools()
_check_no_two_affixes_are_the_same_thing()
_check_every_slot_offers_a_real_choice_of_base()
_check_bases_in_a_slot_are_actually_different()
_check_every_base_grants_a_stat_something_reads()
_check_the_weapon_types_match_the_design()
_check_dual_wielding_carries_more_damage_types_than_a_two_hander()
_check_no_weapon_rolls_a_defensive_affix()
_check_only_the_shield_defends_among_weapon_bases()
_check_every_weapon_but_the_shield_supplies_damage()
_check_every_gem_applied_effect_is_reachable_as_an_affix()
_check_ailments_only_appear_where_a_hit_comes_from()
_check_the_two_loadouts_have_equal_affix_value()
_check_only_a_two_handed_weapon_multiplies_its_values()
_check_every_rarity_has_a_composition()
_check_the_affix_budget_is_the_best_rarity_on_every_piece()
_check_a_split_never_exceeds_either_cap()
_check_rarity_is_decided_by_what_fills_the_slots()


def better_kind(pair: tuple[StatAffix, StatAffix], base_before_increases: float,
                existing_increases: float, tier: int = 7) -> StatAffix:
    """Which of a flat and increased pair adds more, given what is already there."""
    return max(pair, key=lambda a: a.added_value(
        base_before_increases, existing_increases, tier))


def crossover_base(pair: tuple[StatAffix, StatAffix], existing_increases: float,
                   tier: int = 7) -> float:
    """The base value at which the two kinds are worth the same.

    Below it the flat affix adds more; above it the increased one does.
    """
    flat = next(a for a in pair if a.kind == "flat")
    increased = next(a for a in pair if a.kind == "increased")
    per_cent = increased.value_at(tier) / 100.0
    if per_cent <= 0:
        return float("inf")
    return flat.value_at(tier) * (1.0 + existing_increases) / per_cent


def weapon_base_damage_needed(target_damage: float, flat_slots: int,
                              increased_slots: int, tier: int = 7,
                              roll: float = 1.0) -> float:
    """What the base bracket must contain for a build to reach a damage target.

    Solves the pipeline backwards. There are no weapon damage numbers anywhere in
    the project, so this is how the affix values imply one rather than the other
    way round.

    THIS IS THE WEAPON, and it used to be the weapon and the skill together
    because no skill had a damage multiplier anywhere in the project. Issue #107
    settled that: `character.SKILL_SLOTS` gives each of the seven slots one, and
    the basic attack is exactly 100% of weapon damage. Since `damage_target()` is
    what an ordinary hit has to do, and an ordinary hit is a basic attack, this
    figure is the weapon alone.

    `damage_for_slot` below gives what the other six slots do with it.

    A NEGATIVE RESULT IS INFORMATION, NOT A FAULT. It means the affixes alone
    already exceed the target, so that build overshoots the content, which is
    what heavy investment is supposed to do.
    """

    flat = FLAT_DAMAGE.value_at(tier, roll) * flat_slots
    increases = INCREASED_DAMAGE.value_at(tier, roll) / 100.0 * increased_slots
    return target_damage / (1.0 + increases) - flat


def damage_for_slot(slot: str, tier: int = 8) -> float:
    """What one use of a skill in this slot deals at a difficulty tier.

    `damage_target()` is what an ordinary hit has to do, and an ordinary hit is
    the basic attack, so every other slot is that figure times its share of
    weapon damage.
    """
    if slot not in SKILL_SLOTS:
        raise ValueError(
            f"unknown skill slot {slot!r}; expected one of {list(SKILL_SLOTS)}")
    return damage_target(tier) * SKILL_SLOTS[slot].typical_damage / 100.0


def reference_weapon_base(difficulty_tier: int = 8) -> float:
    """What a weapon and skill together supply at the reference build.

    A fixed quantity that a player then adds affixes on top of, which is why the
    flat-versus-increased crossover is walked from here rather than from the
    whole base a target implies.
    """
    return weapon_base_damage_needed(damage_target(difficulty_tier),
                                     REFERENCE_FLAT_DAMAGE_AFFIXES,
                                     REFERENCE_INCREASED_DAMAGE_AFFIXES)


def slots_to_cap(family: AffixFamily, tier: int, active_cataclysms: int,
                 affix_tier: int = 7, roll: float = 1.0) -> float:
    """Affix slots needed to cap every active resistance using one family.

    Defaults to perfect rolls, so this is the floor. An average-rolled set needs
    more slots, which is the gap the Primal Spark and the Corrupted Mote exist
    to close.
    """
    needed = RESISTANCE_CAP * max(1, active_cataclysms)
    per_slot = family.useful_coverage(affix_tier, active_cataclysms, roll)
    if per_slot <= 0:
        return float("inf")
    return needed / per_slot


def best_family(active_cataclysms: int, affix_tier: int = 7,
                roll: float = 1.0) -> AffixFamily:
    """Which family caps every active resistance in the fewest slots."""
    return min(RESISTANCE_FAMILIES,
               key=lambda f: slots_to_cap(f, active_cataclysms, active_cataclysms,
                                          affix_tier, roll))


def crossover_table(affix_tier: int = 7) -> list[dict[str, object]]:
    """Slots to cap all active resistances, per family, at each difficulty tier.

    A difficulty tier is a run and each tier adds a Cataclysm, so the tier is
    also the number of active damage types.
    """
    rows = []
    for tier in range(1, 9):
        row: dict[str, object] = {"tier": tier, "active": tier}
        for family in RESISTANCE_FAMILIES:
            row[family.name] = slots_to_cap(family, tier, tier, affix_tier)
        row["best"] = best_family(tier, affix_tier).name
        rows.append(row)
    return rows


if __name__ == "__main__":
    print("Resistance affix families. Issue #79.")
    print()
    print("Every tier is a RANGE. Where a roll lands inside it is the difference")
    print("between a good item and one worth rerolling, and it is what the")
    print("Corrupted Mote and the Primal Spark exist to change.")
    print()
    for f in RESISTANCE_FAMILIES:
        print(f"  {f.name} (covers {f.breadth}):")
        for t in AFFIX_TIERS:
            low, high = f.range_at(t)
            print(f"      T{t}   {low:>5.1f}% to {high:>5.1f}%")
        print()

    t6_high = SINGLE_RESISTANCE.range_at(6)[1]
    t7_low = SINGLE_RESISTANCE.range_at(7)[0]
    print("  Bands OVERLAP between adjacent tiers, and that is deliberate. A")
    print(f"  perfect T6 single-resistance roll is {t6_high:.1f}% and the worst T7")
    print(f"  roll is {t7_low:.1f}%, so a perfect lower-tier item can beat a poor")
    print("  higher-tier one. With seven tiers there is no way to have both")
    print("  non-overlapping bands and rolls large enough to change a build.")
    print("  The overlap reaches exactly one tier and never two.")
    print()
    print("  Total coverage at a perfect T7 roll:")
    for f in RESISTANCE_FAMILIES:
        print(f"      {f.name:<20} {f.total_coverage(7):>5.0f} points across "
              f"{f.breadth} resistance{'s' if f.breadth > 1 else ''}")
    print()

    print("Slots needed to cap every ACTIVE resistance, using T7 affixes.")
    print("A difficulty tier is a run and each tier adds a Cataclysm, so the")
    print("tier is also how many damage types are attacking.")
    print()
    print(f"    {'tier':>5} {'active':>7} " +
          "".join(f"{f.name:>20}" for f in RESISTANCE_FAMILIES) + "   best")
    print("    " + "-" * 90)
    for row in crossover_table():
        line = f"    {row['tier']:>5} {row['active']:>7} "
        for f in RESISTANCE_FAMILIES:
            line += f"{row[f.name]:>19.1f} "
        print(line + f"  {row['best']}")
    print()
    print(f"    Out of {TOTAL_AFFIX_SLOTS} affix slots on a full set of gear.")
    print()

    perfect = slots_to_cap(ALL_RESISTANCE, 8, 8, roll=1.0)
    average = slots_to_cap(ALL_RESISTANCE, 8, 8, roll=0.5)
    worst = slots_to_cap(ALL_RESISTANCE, 8, 8, roll=0.0)
    print("    Those figures assume PERFECT rolls. What the roll is worth, at")
    print("    tier 8 with all-resistance affixes:")
    print(f"      every roll perfect   {perfect:>5.1f} slots")
    print(f"      every roll average   {average:>5.1f} slots")
    print(f"      every roll minimum   {worst:>5.1f} slots")
    print(f"    So crafting is worth {worst - perfect:.1f} slots of gear, which is")
    print("    what the Primal Spark and the Corrupted Mote are for.")
    print()
    print("    The efficient family changes as the run goes on, which is the")
    print("    point of having three. A single-resistance affix is the best use")
    print("    of a slot when one Cataclysm is active and nearly worthless when")
    print("    eight are; an all-resistance affix is the reverse.")
    print()

    # ---------------------------------------------------------------
    from .character import Attributes, Character
    from .classes import DEMONIC_CLASSES

    print("=" * 72)
    print("Health and damage affixes")
    print()
    print("These have no breadth axis. What they have is the two ends of the")
    print("stat pipeline: a flat affix enters the base, an increased affix")
    print("scales it. Which is worth more depends on how much base is there.")
    print()
    for pair in (HEALTH_AFFIXES, DAMAGE_AFFIXES):
        for a in pair:
            low, high = a.range_at(7)
            unit = "" if a.kind == "flat" else "%"
            print(f"    {a.name:<28} T7 {low:>6.1f}{unit} to {high:>6.1f}{unit}")
    print()

    rav = Character(DEMONIC_CLASSES["Ravager"], level=100,
                    attributes=Attributes(vitality=100))
    base = DEMONIC_CLASSES["Ravager"].base_at("max_health", 100)
    vitality_increases = 100 * 0.02
    over = crossover_base(HEALTH_AFFIXES, vitality_increases)
    print(f"    A level 100 Ravager has {base:,.0f} base health and, with every")
    print(f"    point in Vitality, {vitality_increases:.0%} of increases before gear.")
    print(f"    Its health is {rav.stat('max_health'):,.0f}.")
    print()
    print(f"    Flat and increased health are worth the same at {over:,.0f} base,")
    flat_slots_to_reach = (over - base) / FLAT_HEALTH.value_at(7)
    print(f"    which is about {flat_slots_to_reach:.0f} flat affixes away. So flat wins early")
    print("    in a build and increased wins after it.")
    print()
    geared_increases = vitality_increases + 13 * INCREASED_HEALTH.value_at(7) / 100
    geared_over = crossover_base(HEALTH_AFFIXES, geared_increases)
    geared_base = base + 13 * FLAT_HEALTH.value_at(7)
    print(f"    With 13 increased health affixes as well, increases reach "
          f"{geared_increases:.0%}")
    print(f"    and the crossover moves out to {geared_over:,.0f} base, against a base of")
    print(f"    {geared_base:,.0f}. Every increase already on a character pushes the")
    print("    crossover further away, so flat stays worth taking for longer")
    print("    than a first look suggests.")
    print()

    print("    The damage target is READ OFF THE ENEMY STATS, not chosen. An")
    print("    average Common enemy at tier 8 has "
          f"{enemy_stats.stats_on_floor('Common', 8, 'Cataclysm').effective_health:,.0f} effective")
    print(f"    health, and should take {HITS_TO_KILL_A_COMMON_ENEMY:.0f} non-critical hits to kill, so a")
    target = damage_target(8)
    print(f"    player needs {target:,.0f} damage per hit. Everything else follows:")
    print()
    AT = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
          ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
          ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))
    for name, rarity in AT:
        e = enemy_stats.stats_on_floor(rarity, 8, "Cataclysm", kind=name)
        hits = enemy_stats.player_damage_to_kill_in(e, 1.0) / target
        print(f"      {e.name:<28} {hits:>7.1f} hits")
    print()
    print("    Those are non-critical. A geared character critting raises its")
    print("    average hit well above the target, so the real counts are lower.")
    print()

    print("    Solving the pipeline backwards gives what the weapon and skill")
    print("    together have to supply, which is a number the project does not")
    print("    have anywhere:")
    print()
    print(f"      {'flat':>5} {'increased':>10} {'multiplier':>11} "
          f"{'base needed':>12} {'from flat':>10} {'from weapon':>12}")
    print("      " + "-" * 66)
    for flat_slots, inc_slots in ((0, 0), (4, 4), (6, 6), (6, 8), (8, 8),
                                  (8, 10), (10, 12)):
        need = weapon_base_damage_needed(target, flat_slots, inc_slots)
        mult = 1.0 + INCREASED_DAMAGE.value_at(7) / 100.0 * inc_slots
        from_flat = FLAT_DAMAGE.value_at(7) * flat_slots
        note = "  <- overshoots" if need <= 0 else ""
        print(f"      {flat_slots:>5} {inc_slots:>10} {mult:>10.1f}x "
              f"{need + from_flat:>12,.0f} {from_flat:>10,.0f} "
              f"{need:>12,.0f}{note}")
    print()
    weapon = reference_weapon_base(8)
    print(f"    The reference build is {REFERENCE_FLAT_DAMAGE_AFFIXES} flat and "
          f"{REFERENCE_INCREASED_DAMAGE_AFFIXES} increased, which needs a weapon")
    print(f"    and skill supplying {weapon:,.0f} against {FLAT_DAMAGE.value_at(7) * REFERENCE_FLAT_DAMAGE_AFFIXES:,.0f} from the flat affixes, so the")
    print("    two contribute about equally. Past eight and ten slots the affixes")
    print("    alone exceed the target, which is what heavy investment is for")
    print("    rather than a fault in the numbers.")
    print()
    print("    Adding flat damage affixes on top of that weapon, one at a time,")
    print("    with the reference build's increases already in place:")
    print()
    increases = (INCREASED_DAMAGE.value_at(7) / 100.0
                 * REFERENCE_INCREASED_DAMAGE_AFFIXES)
    for flat_n in range(0, 7):
        base = weapon + FLAT_DAMAGE.value_at(7) * flat_n
        winner = better_kind(DAMAGE_AFFIXES, base, increases)
        print(f"      {flat_n} flat affixes, base {base:>6,.0f}   "
              f"the better next pick is {winner.kind}")
    print()
    print("    So a character takes a few flat damage affixes and then switches.")
    print("    Both kinds get used, which is the whole reason for having two.")
    print()

    print("Slot restrictions. Affixes do not go anywhere.")
    print()
    print(f"    {'family':<24} {'pieces':>7} {'slots':>7}   where")
    print("    " + "-" * 72)
    for label, allowed in (("Damage", OFFENSIVE_SLOTS),
                           ("Health", DEFENSIVE_SLOTS),
                           ("Resistance", RESISTANCE_SLOTS)):
        pieces = sum(c for s, c in GEAR_SLOTS.items() if s in allowed)
        print(f"    {label:<24} {pieces:>7} {slots_available_to(allowed):>7}   "
              f"{', '.join(sorted(allowed))}")
    print()
    print(f"    Out of {GEAR_PIECES} pieces and {TOTAL_AFFIX_SLOTS} slots in total.")
    print("    Rings are in every list on purpose. There are eight of them, so")
    print("    they are the flexible slots a build uses to fix whatever it is")
    print("    short of, which is what makes them worth chasing.")
    print()

    print("=" * 72)
    print("The affix pool")
    print()
    print("Every piece has four affix slots, and they are two prefixes and two")
    print("suffixes drawn from separate pools. Without that split an item's four")
    print("slots are four of whatever is strongest and one piece can carry a")
    print("whole build. With it, every piece gives something up.")
    print()
    for position in AFFIX_POSITIONS:
        group = [a for a in AFFIX_POOL if a.position == position]
        label = ("Prefixes -- how big a character's numbers are"
                 if position == PREFIX
                 else "Suffixes -- how often, how fast, how much gets through")
        print(f"  {label} ({len(group)})")
        print(f"    {'affix':<38} {'T7 at +10':>10} {'slots':>6}   where")
        print("    " + "-" * 84)
        for a in sorted(group, key=lambda x: (x.stat, x.kind)):
            unit = "" if a.kind == "flat" else "%"
            where = ("everywhere" if len(a.allowed_slots) == len(GEAR_SLOTS)
                     else ", ".join(sorted(a.allowed_slots)))
            print(f"    {a.name:<38} {a.value_at(7):>9,.2f}{unit} "
                  f"{a.slots_available():>6}   {where}")
        print()
    print(f"  Plus the three resistance families above, which are {SUFFIX}es.")
    print()

    print("  Hybrid affixes: two stats on one roll, each at "
          f"{HYBRID_FRACTION:.0%} of the single")
    print("  affix's value. That ratio is read off the two-resistance affix")
    print("  against the single-resistance one rather than written twice.")
    print()
    for hybrid in HYBRID_AFFIXES:
        parts = ", ".join(f"{v:,.1f} {s}" for s, v in hybrid.values_at().items())
        print(f"    {hybrid.name:<44} {hybrid.position:<7} {parts}")
    print()

    print("  ONE AFFIX PER GROUP. Issue #128. An affix belongs to a group for")
    print("  every stat it grants, named by the stat and the kind together, and")
    print("  one piece holds at most one affix from any group. So a hybrid")
    print("  cannot sit beside either of its own halves, flat and increased of")
    print("  one stat can sit together, and two single-resistance rolls can if")
    print("  they cover different damage types.")
    print()
    print(f"    {'slot':<12} {'prefix groups':>14} {'suffix groups':>14}"
          f"   (needs {PREFIXES_PER_PIECE} and {SUFFIXES_PER_PIECE})")
    print("    " + "-" * 60)
    for slot in GEAR_SLOTS:
        print(f"    {slot:<12} {distinct_groups_for(slot, PREFIX):>14} "
              f"{distinct_groups_for(slot, SUFFIX):>14}")
    print()
    print("    Every slot has room to spare, so the rule constrains which")
    print("    combinations appear rather than whether a piece can be filled.")
    print()

    print("  Ailment affixes, applying the effects the gems already grant:")
    print()
    print(f"    {'affix':<34} {'T7':>6} {'kind':<20} same effect as")
    print("    " + "-" * 84)
    for a in AILMENT_AFFIXES:
        kind = ("damage over time" if a.ailment in DAMAGE_OVER_TIME_AILMENTS
                else "weakening effect")
        print(f"    {a.name:<34} {a.chance_at(7):>5.0f}% {kind:<20} {a.gem}")
    print()
    print("    On weapons, necklaces, relics and rings only: an ailment affix")
    print("    only makes sense where a hit comes from. The gem stays the")
    print("    stronger source, so a socket is still where an ailment build")
    print("    lives.")
    print()

    print("=" * 72)
    print("Item bases")
    print()
    print("The implicit belongs to the BASE, not the slot. Every category of")
    print("gear has several bases, and picking one commits a character to a")
    print("defensive layer or an offensive property before any affix is")
    print("involved. That is where most of the interest in gearing lives.")
    print()
    for slot in GEAR_SLOTS:
        if slot == "Weapon":
            continue
        print(f"  {slot}  ({len(pool_for(slot, PREFIX))} prefixes, "
              f"{len(pool_for(slot, SUFFIX))} suffixes available)")
        for base in bases_for(slot):
            marks = ", ".join(
                f"{i.value:,.1f} {i.stat}" if i.kind == "flat"
                else f"{i.value:,.0f}% increased {i.stat}"
                for i in base.implicits)
            print(f"    {base.name:<14} {marks}")
        print()

    print("  Weapon")
    print(f"    {'base':<20} {'hands':>5} {'sub-type':<9} {'types':>5}  implicit")
    print("    " + "-" * 84)
    for base in WEAPON_BASES:
        marks = ", ".join(
            f"{i.value:,.0f} {i.stat}" if i.kind == "flat"
            else f"{i.value:,.0f}% increased {i.stat}"
            for i in base.implicits)
        print(f"    {base.name:<20} {base.hands:>5} {base.sub_type:<9} "
              f"{base.max_damage_types_on_base:>5}  {marks}")
    print()
    print("    A weapon carries a physical sub-type and a limit on how many")
    print("    damage types it can hold, neither of which any other item has.")
    print("    The column above is that LIMIT, not a count. How many a weapon")
    print("    actually holds is rolled when it drops, from 1 up to the lower of")
    print("    that limit and the difficulty tier. WHICH types fill them is also")
    print("    decided on drop, biased toward the Cataclysm being fought.")
    print()
    print("    Dual wielding stays the route to multiclassing, and the tier cap")
    print("    is what makes that true: the raw limits tie at eight.")
    print()
    print("      tier   two one-handers   one two-hander")
    for tier in range(1, DIFFICULTY_TIERS + 1):
        dual = max_damage_types(1, tier) * 2
        two_handed = max_damage_types(2, tier)
        lead = dual - two_handed
        print(f"      {tier:>4}   {dual:>15}   {two_handed:>14}   "
              f"{'+' + str(lead) if lead else 'tie'}")
    print()
    print("    A dual wielder leads at every tier from 1 to 7 and ties only at 8,")
    print("    where the two-hander finally catches up, while staying ahead on")
    print("    raw damage throughout.")
    print()
    print("    The Shield is the one weapon whose base defends. The design lists")
    print("    it among the one-handed weapon types and says there are no")
    print("    offhand items, so it is a weapon with nowhere else to be.")
    print()

    print(f"  {len(ITEM_BASES)} bases across {len(GEAR_SLOTS)} slots. "
          f"{total_pool_size()} things a drop could roll:")
    print(f"    {len(AFFIX_POOL)} stat affixes, {len(RESISTANCE_FAMILIES)} resistance families, "
          f"{len(HYBRID_AFFIXES)} hybrids, {len(AILMENT_AFFIXES)} ailments.")
    print("  No affix grants a primary attribute yet. The project owner")
    print("  reversed the rule that forbade it on 2026-08-04; issue #204.")
