"""What a drop is: which of the eight rarities it rolls.

A dungeon produces items and nothing decided what they were. This is the first
part of issue #44.

WHAT THIS DOES NOT DECIDE. Which item base drops, which affixes it rolls, how
many items a kill or a floor produces, and what upgrade level a drop arrives at
beyond the floor its own rarity forces. Those are the later parts of #44.

THE ROLL IS A CASCADE FROM THE RAREST DOWN, which is what the genre does. Path of
Exile "rolls for rare, then magic, and any remaining items will drop normal", and
Diablo 2 checks hierarchically with a fallback when the quality it rolled is not
available. Magic find multiplies the chance at each step rather than changing the
outcome, which is what Path of Exile states plainly: +100% increased item rarity
gives "twice as many magic items, twice as many rares and twice as many uniques".

THE RARITY IS ROLLED FIRST AND THE CONTENTS FOLLOW FROM IT. Asked by the project
owner on 2026-08-18: "Gear drops, rolls for rarity, then picks however many
affixes/enchantments based on that?" That is right, and it is also the only
workable order. `affixes.rarity_of` accepts eight combinations of enchantment and
affix counts and raises for every other, so rolling the two counts independently
would produce something that is not a rarity most of the time. Rolling the rarity
and reading `affixes.RARITY_COMPOSITION` for its contents cannot.

It does not conflict with rarity being computed rather than stored. An ITEM still
carries no rarity field; the generator uses the label as a step and stores only
the contents, and `affixes.rarity_of` recovers the same label from them.

THE CAP IS ONE RARITY ABOVE THE DIFFICULTY TIER, WHICH IS NOT A NEW MECHANISM. `docs/Cataclysm_GDD_v2.md` says the difficulty tier is the design's
own gate three times over -- gear and gem rarity equal it, the best upgrade stone
that can drop is capped by it, and a weapon rolls damage types up to it -- and
`affixes.max_affix_tier_on_a_drop` is the fourth, gating affix tiers at tier plus
one. This is the fifth use of the same shape.

The cap RAISES the ceiling rather than lifting the floor, which is the point of
it, quoted from the affix version: every tier at or below the cap "stays in the
pool, so a deep drop is better on average without being predictable, which is
what makes a drop worth reading". Path of Exile gates modifier tiers on item
level and Last Epoch on area level, and both expand which tiers are available
rather than removing the low ones.

How the rarities inside that cap are weighted against each other is a separate
question, and it is the one tunable described below. They are all equal today,
which is what makes the distribution flat.

THERE IS EXACTLY ONE TUNABLE HERE AND IT LIVES IN THE WORKBOOK. The Gear Rarity
sheet of `docs/All_Things_Cataclysm.xlsx` gives each rarity a drop weight, and
`RARITY_DROP_WEIGHT` below mirrors it the way every other sheet is mirrored into
this package. All eight weights are 1 today, which is what makes the distribution
flat.

**FLAT IS GENEROUS AND IS THE FIRST NUMBER TO REVISIT.** At difficulty tier 8 with
no magic find at all it makes one drop in eight Cataclysmic, which is far above
what the genre does with its top rarities. It is shipped anyway, for two reasons.
It is the shape the design already applies to affix tiers rather than a curve
invented here, and the lever that really controls how much good gear a player sees
is how many items drop, which is not built yet -- so tuning the split between
rarities before the quantity exists would be tuning half a system. Changing it is
a column in the workbook and no code change. Issue #81 and the project owner's
standing rule both say balance numbers wait until the systems around them can be
played.

WHAT IS DELIBERATELY NOT BUILT. Diminishing returns on magic find. Diablo 2 has
them, per rarity and weaker the rarer the tier -- uniques (MF*250)/(MF+250), sets
(MF*500)/(MF+500), rares (MF*600)/(MF+600), with ordinary magic items not
diminished at all -- but every one of those is a chosen constant, and this module
has none. Saturating at 1 is the only ceiling, and whether that is enough is a
question for play.

Sources: Drop rate, PoE Wiki, https://www.poewiki.net/wiki/Drop_rate ; Rarity,
PoE Wiki, https://www.poewiki.net/wiki/Rarity ; Magic find diminishing returns,
Diablo Wiki, https://diablo2.diablowiki.net/Magic_find_diminishing_returns .
"""

from __future__ import annotations

import dataclasses

from . import affixes as af
from . import player_power

#: How far above the difficulty tier's own rarity a drop may roll.
#:
#: THE SAME ONE-ABOVE THE AFFIX TIER GATE USES, and for the reason the project
#: owner gave for that one in issue #241: with the cap sitting exactly on the
#: tier, the best thing a dungeon can produce is something the player can already
#: make, so the only reason to run one is quantity.
DROP_RARITIES_ABOVE_DIFFICULTY = 1

#: How heavily each rarity is weighted on a drop, mirroring the Drop Weight
#: column of the Gear Rarity sheet in `docs/All_Things_Cataclysm.xlsx`.
#:
#: THE WORKBOOK IS AUTHORITATIVE, as it is for every other stored value in this
#: package; `tools/tests/test_loot_sheet_matches_the_model.py` fails when the two
#: disagree, and the fix is to change this table rather than the sheet.
#:
#: WHAT A WEIGHT MEANS. Its share of every reachable rarity's weight.
#:
#: THE LADDER FALLS IN TWO SEGMENTS, SPLIT WHERE THE DESIGN SPLITS IT. The four
#: lower rarities carry only regular affixes; the four upper ones carry
#: enchantments and are gated on upgrade level. That is a real boundary rather
#: than another rung, so each half falls at its own rate with a step between them:
#:
#:     the four ordinary rarities   each 2.5 times rarer than the one below
#:     the enchantment boundary     a step of 8
#:     the four enchanted rarities  each 5 times rarer than the one below
#:
#: Which gives 15625, 6250, 2500, 1000 | 125, 25, 5, 1, summing to 25,531. So a
#: Cataclysmic drop is one in 25,531 with no magic find at difficulty tier 8.
#:
#: WHY NOT ONE RATIO FOR THE WHOLE LADDER, which was the simpler proposal. A
#: single ratio cannot make the top rare without dragging Masterful down with it:
#: aimed at one Cataclysmic in 20,000 it puts Masterful at one in 82, against one
#: in 26 here. **Masterful has to stay common**, because it is the top of the
#: ordinary ladder and `docs/Cataclysm_GDD_v2.md` fits its affix values against "a
#: full set of Masterful gear" -- and because crafting promotes a piece upward
#: from there, so it is the supply line rather than the destination.
#:
#: WHICH IS THE POINT THE WHOLE SHAPE RESTS ON: A DROP IS RAW MATERIAL. The design
#: says adding an affix promotes a piece and applying an enchantment promotes it
#: further, all the way to Cataclysmic. So crafting is the intended route to the
#: top of the ladder and a dropped Cataclysmic is a windfall, not the expected
#: way to own one. That is what makes one in 25,531 right rather than punishing.
#:
#: THE FLAT WEIGHTS THIS REPLACED were every rarity at 1, shipped as a first value
#: and flagged at the time as too generous -- one drop in eight was Cataclysmic.
#: The project owner set the shape on 2026-08-18 after rejecting one in 255 as
#: still too generous.
RARITY_DROP_WEIGHT: dict[str, float] = {
    "Everyday":    15625.0,
    "Quality":      6250.0,
    "Superb":       2500.0,
    "Masterful":    1000.0,
    "Legendary":     125.0,
    "Mythical":       25.0,
    "Ascendant":       5.0,
    "Cataclysmic":     1.0,
}

#: The residue at which crafting an item starts costing real in-game days.
#:
#: Stated in section VI of `docs/Cataclysm_GDD_v2.md`, whose table reads "100+ |
#: Critical Time Penalty kicks in -- crafting costs real in-game days", and whose
#: formula puts the days at `CR / 100` rounded down.
#:
#: IT IS NOT A CEILING ON WHAT A DROP MAY CARRY, and treating it as one was wrong
#: twice before the project owner corrected it. Every rarity above Quality can
#: arrive past it, which is what makes a good item expensive to improve.
CRITICAL_RESIDUE = 100.0

#: The residue an item of each rarity carries when it drops, as a band it rolls
#: inside. Mirrors the two Residue On Drop columns of the Gear Rarity sheet.
#:
#: A DROPPED ITEM CARRYING RESIDUE IS A CHANGE THE PROJECT OWNER MADE ON
#: 2026-08-18, and `docs/Cataclysm_GDD_v2.md` was rewritten to match. Before it,
#: the design said residue came from crafting alone -- "every modification made to
#: an item adds Cataclysmic Residue" -- so a freshly dropped item had none.
#:
#: RESIDUE IS A COST AND NEVER A BENEFIT. The design is emphatic: "Worn Residue
#: grants nothing. It is not a resource and it does not make the character
#: stronger." So a better item is more expensive to improve and brings its wearer
#: nearer to being hunted by a corrupted copy of itself. It does not make the item
#: better, and that is the trade.
#:
#: THE TOP BAND IS THE PROJECT OWNER'S, stated on 2026-08-18: a Cataclysmic drop
#: carries between 300 and 500. Two earlier proposals, topping out at 50 and then
#: at 100, were both rejected as too safe. The rest of the ladder is that band
#: scaled by the rarity's position, so every rarity has a band of its own and even
#: the weakest drop carries some.
#:
#: WHAT 300 TO 500 ACTUALLY COSTS, by the design's own two formulas -- gold
#: multiplier `(CR / 50) + 1` and craft days `CR / 100` rounded down. A freshly
#: dropped Cataclysmic piece costs seven to eleven times the gold to craft and
#: three to five real in-game days per craft. That is heavy on purpose.
#:
#: WHY 100 IS NOT THE CEILING, though it is where crafting starts costing days.
#: The project owner's words on 2026-08-18: "The actual break point of residue is
#: the point where it triggers the corrupted dopple mechanic. Which we haven't
#: really decided yet." The design says the same -- the Consumption Threshold is
#: "a single fixed number, to be tuned" -- so there is no number to check these
#: against, and this module does not pretend otherwise. Issue #697.
RARITY_RESIDUE_BAND: dict[str, tuple[float, float]] = {
    "Everyday":    ( 38.0,  62.0),
    "Quality":     ( 75.0, 125.0),
    "Superb":      (112.0, 188.0),
    "Masterful":   (150.0, 250.0),
    "Legendary":   (188.0, 312.0),
    "Mythical":    (225.0, 375.0),
    "Ascendant":   (262.0, 438.0),
    "Cataclysmic": (300.0, 500.0),
}

#: The upgrade level a piece must be before it can be a given rarity.
#:
#: Stated in the rarity table of `docs/Cataclysm_GDD_v2.md` section VI and
#: implemented nowhere until now: Legendary needs +4, Mythical +6, Ascendant +8
#: and Cataclysmic +10. The lower four rarities have no gate.
#:
#: THIS IS A FLOOR ON A DROP, NOT A FILTER. A drop that rolls Legendary arrives at
#: +4 or better rather than being downgraded, which is what lets magic find do its
#: job at a low difficulty tier. Rolling the rarity first is what makes that
#: possible; deciding the upgrade level first would have fixed it before the
#: rarity was known.
RARITY_GEAR_LEVEL_GATE: dict[str, int] = {
    "Everyday":    0,
    "Quality":     0,
    "Superb":      0,
    "Masterful":   0,
    "Legendary":   4,
    "Mythical":    6,
    "Ascendant":   8,
    "Cataclysmic": 10,
}


def rarity_index(rarity: str) -> int:
    """Where a rarity sits on the ladder. 1 is Everyday and 8 is Cataclysmic.

    One-based so it lines up with the difficulty tier directly: the design says
    gear rarity equals the difficulty tier, and there are eight of each.
    """
    if rarity not in af.RARITIES:
        raise ValueError(f"{rarity!r} is not a rarity; expected one of "
                         f"{list(af.RARITIES)}")
    return af.RARITIES.index(rarity) + 1


def rarity_at_index(index: int) -> str:
    """The rarity at a position on the ladder, 1 to 8."""
    if not 1 <= index <= len(af.RARITIES):
        raise ValueError(f"index {index} is outside 1 to {len(af.RARITIES)}")
    return af.RARITIES[index - 1]


def best_rarity_on_a_drop(tier: int) -> str:
    """The highest rarity a drop may roll at a difficulty tier.

    Gear rarity equals the difficulty tier, plus the one-above that makes a drop
    worth reading, capped at Cataclysmic. So tiers 7 and 8 both reach
    Cataclysmic, the same way affix tiers 6, 7 and 8 all reach T7.
    """
    if not 1 <= tier <= af.DIFFICULTY_TIERS:
        raise ValueError(f"tier {tier} is outside 1 to {af.DIFFICULTY_TIERS}")
    return rarity_at_index(
        min(len(af.RARITIES), tier + DROP_RARITIES_ABOVE_DIFFICULTY))


def gear_level_gate(rarity: str) -> int:
    """The upgrade level a piece must reach before it can be this rarity."""
    if rarity not in RARITY_GEAR_LEVEL_GATE:
        raise ValueError(f"{rarity!r} is not a rarity; expected one of "
                         f"{list(af.RARITIES)}")
    return RARITY_GEAR_LEVEL_GATE[rarity]


def residue_band(rarity: str) -> tuple[float, float]:
    """The lowest and highest residue a drop of this rarity can carry."""
    if rarity not in RARITY_RESIDUE_BAND:
        raise ValueError(f"{rarity!r} is not a rarity; expected one of "
                         f"{list(af.RARITIES)}")
    return RARITY_RESIDUE_BAND[rarity]


def roll_residue(rarity: str, rng) -> float:
    """The residue one drop of this rarity arrives with.

    Uniform inside the band and a whole number of points, because residue is
    counted in points everywhere else: the craft day penalty is `CR / 100`
    rounded down and the gold multiplier is `(CR / 50) + 1`.

    THE BANDS OVERLAP BETWEEN NEIGHBOURING RARITIES, which is deliberate. A lucky
    Superb piece arrives cheaper to improve than an unlucky Masterful one.
    Residue is a cost, so that is a real trade rather than a strict ladder, and
    it is the same shape the affix tier bands already use.
    """
    lowest, highest = residue_band(rarity)
    return float(rng.randint(int(lowest), int(highest)))


def rarity_step_chance(index: int, magic_find: float = 0.0) -> float:
    """The chance the cascade stops at this rung, given that it reached it.

    THE RUNG'S WEIGHT AS A SHARE OF EVERYTHING AT OR BELOW IT. That is what turns
    a table of weights into a cascade, and the arithmetic is worth stating because
    it is not obvious: stopping at rung R with chance w[R]/S[R], where S[R] is the
    weight of rungs 1 to R, leaves S[R-1]/S[R] to carry on. Multiply those down
    from the top and every rung ends up with w[R]/S[N] -- its own share of the
    whole reachable ladder, which is what a weight is supposed to mean.

    With every weight equal this is one over the rung's position, and the
    distribution is flat.

    MAGIC FIND MULTIPLIES IT, which is Path of Exile's stated behaviour: +100%
    increased item rarity gives twice as many of every rarity above the floor.
    Saturating at 1 is the only ceiling; see the module docstring for the
    diminishing returns that are deliberately not built.
    """
    if index < 1:
        raise ValueError(f"index {index} is below 1")
    if magic_find < 0.0:
        raise ValueError(
            f"magic find is {magic_find}; it is an added percentage with a "
            "baseline of zero and cannot be negative")

    at_or_below = sum(RARITY_DROP_WEIGHT[rarity_at_index(rung)]
                      for rung in range(1, index + 1))
    if at_or_below <= 0.0:
        raise ValueError(
            f"every rarity up to {rarity_at_index(index)} has a drop weight of "
            "zero, so there is nothing for the cascade to choose between")

    share = RARITY_DROP_WEIGHT[rarity_at_index(index)] / at_or_below
    return min(1.0, share * (1.0 + magic_find / 100.0))


def rarity_distribution(tier: int, magic_find: float = 0.0) -> dict[str, float]:
    """What fraction of drops at this tier is each rarity.

    EXACT RATHER THAN SAMPLED, so a test can check the shape without running ten
    thousand rolls and then arguing about noise. `roll_rarity` draws from exactly
    this distribution, and a test compares the two.

    Every rarity is a key, including the ones this tier cannot reach, which carry
    zero. A caller asking about a rarity out of reach should get 0.0 rather than
    a KeyError.
    """
    best = rarity_index(best_rarity_on_a_drop(tier))

    out = {rarity: 0.0 for rarity in af.RARITIES}
    left = 1.0
    for index in range(best, 1, -1):
        chance = rarity_step_chance(index, magic_find)
        out[rarity_at_index(index)] = left * chance
        left *= 1.0 - chance

    # WHATEVER FELL THROUGH EVERY RUNG IS THE FLOOR, which is what makes this a
    # cascade rather than a table of weights that has to sum to one by hand.
    out[af.RARITIES[0]] = left
    return out


def roll_rarity(tier: int, magic_find: float, rng) -> str:
    """The rarity one drop rolls. `rng` is a `random.Random`.

    Walks the rungs from the rarest down and stops at the first that succeeds,
    which is the cascade itself rather than a lookup into `rarity_distribution`.
    Written that way on purpose: the test that the two agree is what proves the
    exact distribution above describes what actually happens.
    """
    best = rarity_index(best_rarity_on_a_drop(tier))

    for index in range(best, 1, -1):
        if rng.random() < rarity_step_chance(index, magic_find):
            return rarity_at_index(index)
    return af.RARITIES[0]


#: The most sockets a piece in each gear slot can have. Mirrors the Item Sockets
#: sheet of `docs/All_Things_Cataclysm.xlsx`, which restates the socket table in
#: section VI of `docs/Cataclysm_GDD_v2.md`.
#:
#: A WEAPON IS NOT HERE because its maximum depends on how many hands it takes;
#: see MAX_SOCKETS_BY_WEAPON_HANDS.
#:
#: POTION SLOTS ARE NOT HERE either. They carry one socket each and there are
#: four, but they are consumables rather than gear -- the design says they
#: "contribute through their sockets only" -- and nothing rolls one as a drop.
#: They are the difference between the 41 sockets this table describes and the 45
#: the design states across all equipment.
MAX_SOCKETS_BY_SLOT: dict[str, int] = {
    "Head":      2,
    "Chest":     6,
    "Shoulders": 2,
    "Gloves":    2,
    "Pants":     4,
    "Boots":     2,
    "Belt":      4,
    "Ring":      1,
    "Necklace":  1,
    "Relic":     4,
}

#: The most sockets a weapon can have, by how many hands it takes.
#:
#: TWO ONE-HANDED WEAPONS MATCH A TWO-HANDER, which is the design's own rule and
#: not a coincidence of these two numbers: "Every loadout gives the same maximum
#: gem sockets and the same Power Score."
MAX_SOCKETS_BY_WEAPON_HANDS: dict[int, int] = {1: 3, 2: 6}

#: How many rings are worn at once. Only used to check the totals add up.
RINGS_WORN = 8

#: Potion slots, one socket each. Only used to check the totals add up.
POTION_SOCKETS = 4


def max_sockets_for(base) -> int:
    """The most sockets this item base can have.

    From the base rather than from the slot alone, because a weapon's maximum
    depends on how many hands it takes.
    """
    if base.slot == "Weapon":
        hands = getattr(base, "hands", None)
        if hands not in MAX_SOCKETS_BY_WEAPON_HANDS:
            raise ValueError(
                f"{base.name} is a weapon taking {hands} hands, and only "
                f"{sorted(MAX_SOCKETS_BY_WEAPON_HANDS)} have a socket maximum")
        return MAX_SOCKETS_BY_WEAPON_HANDS[hands]

    if base.slot not in MAX_SOCKETS_BY_SLOT:
        raise ValueError(
            f"{base.name} is in slot {base.slot!r}, which has no socket maximum")
    return MAX_SOCKETS_BY_SLOT[base.slot]


def roll_sockets(base, rng) -> int:
    """How many sockets one drop of this base arrives with.

    UNIFORM FROM NONE UP TO THE BASE'S MAXIMUM, chosen by the project owner on
    2026-08-18: "all items should be able to drop with 0-n sockets where n is
    their maximum number of sockets". Two alternatives were put to them and
    declined -- capping the roll by the difficulty tier the way Diablo 2 and Path
    of Exile cap it by item level, and weighting the roll toward fewer sockets.

    SO A SOCKET COUNT CARRIES NO PROGRESSION. A tier 1 Chest can drop with all
    six. That is the shape asked for, and it is the one place the drop rules do
    not gate on the difficulty tier.

    A DROP WITH NO SOCKETS IS NOT A RUINED ITEM. `game/Data/CraftingMaterials.csv`
    carries an Add Socket craft -- Shattered Core, 15 residue, 3 days -- and that
    craft only has something to do because drops arrive below their maximum. It
    is the design's own evidence that sockets were always meant to roll.
    """
    return rng.randint(0, max_sockets_for(base))



# --------------------------------------------------------------------------
# Rolling a whole item
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class RolledAffix:
    """One affix as it landed on one item.

    Mirrors `FCataclysmRolledAffix` in
    `game/Source/Cataclysm/Items/CataclysmItem.h`: what belongs to the item is
    which affix it got, at what tier, where in that tier's band it landed, and --
    for a resistance family only -- which damage types it covers.

    THE VALUE IS NOT STORED, in either place, and for the same reason. It depends
    on the piece's upgrade level and on whether the piece is a two-handed weapon,
    and both of those change after the item exists. `affixes.affix_value` works
    it out on demand.
    """

    affix: object
    tier: int
    roll: float
    covers: tuple[str, ...] = ()


@dataclasses.dataclass(frozen=True)
class RolledItem:
    """One item as it dropped.

    Mirrors `FCataclysmItem`, including what it deliberately leaves out. Rarity
    is not a field: it is a label for the contents, and `rarity` below computes
    it rather than reading it back.
    """

    base: object
    gear_level: int
    affixes: tuple[RolledAffix, ...]
    enchantments: int

    #: Cataclysmic Residue already on the piece when it dropped. A cost, never a
    #: benefit: it raises what crafting this item will charge in gold and days,
    #: and it counts toward the Worn Residue that can get a character consumed.
    residue: float = 0.0

    #: How many sockets the piece dropped with, from none up to its base's
    #: maximum. Gems are not modelled, so these are empty; what fills one is
    #: issue #46.
    sockets: int = 0

    @property
    def rarity(self) -> str:
        """What this item IS, from what fills its slots.

        Computed rather than stored, which is the whole point: adding an affix at
        the crafting bench promotes the piece without anything having to write a
        new rarity onto it.
        """
        return af.rarity_of(self.enchantments, len(self.affixes))


@dataclasses.dataclass(frozen=True)
class _Candidate:
    """An affix together with the damage types a resistance roll would cover.

    WHY THIS EXISTS. `affixes.groups_of` refuses to read a resistance family
    without being told which damage types it landed on, because the family says
    how MANY it covers and the item says which. So the damage types have to be
    chosen BEFORE the draw, not after: two families that both landed on Fire
    occupy the same group and may not sit on one item, and the draw is what
    enforces that.
    """

    affix: object
    covers: tuple[str, ...]


def _groups_of_candidate(candidate: _Candidate) -> frozenset[str]:
    return af.groups_of(candidate.affix, candidate.covers)


def _candidates_for(slot: str, position: str, rng) -> list[_Candidate]:
    """Every affix that could roll here, each with its damage types already picked.

    A resistance family gets `breadth` distinct damage types drawn at random. A
    family covering all eight has no choice to make and gets them all, which is
    also what `affixes.groups_of` expects.
    """
    out = []
    for affix in af.everything_for(slot, position):
        covers: tuple[str, ...] = ()
        if isinstance(affix, af.AffixFamily):
            covers = tuple(rng.sample(list(af.DAMAGE_TYPES), affix.breadth))
        out.append(_Candidate(affix=affix, covers=covers))
    return out


def split_for_a_drop(slots: int, rng) -> tuple[int, int]:
    """How one drop's affix slots divide into prefixes and suffixes.

    `affixes.prefix_suffix_split` returns the even shape and says so explicitly:
    "a drop may legitimately go the other way with three -- one prefix and two
    suffixes -- so this returns the shape rather than a rule". A drop is where
    that other way has to actually happen.

    SO AN ODD COUNT PICKS A SIDE AT RANDOM. Without this every three-affix item
    in the game would carry two prefixes and one suffix, which is a bias nobody
    chose and which the affix pool is not built around -- there are 31 prefixes
    and 54 suffixes.
    """
    prefixes, suffixes = af.prefix_suffix_split(slots)
    if prefixes != suffixes and rng.random() < 0.5:
        prefixes, suffixes = suffixes, prefixes
    return prefixes, suffixes


def roll_item(slot: str, tier: int, magic_find: float, rng) -> RolledItem:
    """Roll one whole item for a gear slot at a difficulty tier.

    THE SLOT IS AN ARGUMENT RATHER THAN A ROLL, on purpose. Which slot a drop is
    for depends on what dropped it and how many items it produced, and neither of
    those is designed yet. Inventing a slot distribution here would be a number
    nobody asked for, sitting where it is hard to find later.

    THE ORDER IS RARITY, THEN CONTENTS, which the project owner chose on
    2026-08-18 and `docs/DECISIONS.md` records. The rarity says how many
    enchantments and how many regular affixes fill the four slots; the affixes are
    then drawn to that count.

    THE UPGRADE LEVEL IS THE FLOOR ITS RARITY FORCES, and nothing more. A
    Legendary drops at +4 because it could not be a Legendary below that. Whether
    a drop should also roll ABOVE its floor depends on the upgrade stone system,
    which the design describes and nothing models, so it is left alone rather than
    guessed at.
    """
    rarity = roll_rarity(tier, magic_find, rng)
    base = rng.choice(af.bases_for(slot))

    prefixes, suffixes = split_for_a_drop(af.affix_slots_for(rarity), rng)

    drawn: list[_Candidate] = []
    for position, count in (("prefix", prefixes), ("suffix", suffixes)):
        if count == 0:
            continue
        drawn.extend(af.draw_without_repeating_a_group(
            _candidates_for(slot, position, rng), count, rng,
            group_of=_groups_of_candidate))

    rolled = tuple(
        RolledAffix(affix=candidate.affix,
                    tier=af.roll_affix_tier(tier, rng),
                    roll=rng.random(),
                    covers=candidate.covers)
        for candidate in drawn)

    return RolledItem(base=base,
                      gear_level=gear_level_gate(rarity),
                      affixes=rolled,
                      enchantments=af.enchantments_for(rarity),
                      residue=roll_residue(rarity, rng),
                      sockets=roll_sockets(base, rng))


# --------------------------------------------------------------------------
# Checks that run on import, the same way affixes.py does it.
# --------------------------------------------------------------------------

def _check_every_rarity_has_a_gear_level_gate() -> None:
    missing = set(af.RARITIES) - set(RARITY_GEAR_LEVEL_GATE)
    extra = set(RARITY_GEAR_LEVEL_GATE) - set(af.RARITIES)
    if missing or extra:
        raise AssertionError(
            "the gear level gate table does not match the rarity ladder: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_the_gates_only_ever_rise() -> None:
    """A rarer piece never requires a lower upgrade level than a weaker one."""
    gates = [RARITY_GEAR_LEVEL_GATE[rarity] for rarity in af.RARITIES]
    for lower, higher in zip(gates, gates[1:], strict=False):
        if higher < lower:
            raise AssertionError(
                f"the gear level gates fall somewhere along the ladder: {gates}")


def _check_no_gate_is_out_of_reach() -> None:
    """Nothing requires an upgrade level a piece cannot be brought to."""
    for rarity, gate in RARITY_GEAR_LEVEL_GATE.items():
        if not 0 <= gate <= af.MAX_GEAR_LEVEL:
            raise AssertionError(
                f"{rarity} requires gear level {gate}, which is outside 0 to "
                f"{af.MAX_GEAR_LEVEL}")


def _check_every_rarity_has_a_drop_weight() -> None:
    missing = set(af.RARITIES) - set(RARITY_DROP_WEIGHT)
    extra = set(RARITY_DROP_WEIGHT) - set(af.RARITIES)
    if missing or extra:
        raise AssertionError(
            "the drop weight table does not match the rarity ladder: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_no_drop_weight_is_negative() -> None:
    for rarity, weight in RARITY_DROP_WEIGHT.items():
        if weight < 0.0:
            raise AssertionError(
                f"{rarity} has a drop weight of {weight}; a weight is a share "
                "and cannot be negative")


def _check_the_distribution_matches_the_weights() -> None:
    """Each rarity's share of drops is its weight's share of the reachable ones.

    This is the claim the whole module rests on -- that the cascade in
    `rarity_step_chance` really does turn a table of weights into the
    distribution those weights describe. It is not obvious from the arithmetic,
    and getting it wrong would bias the drops in a way nobody chose and nothing
    else would notice.
    """
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        spread = rarity_distribution(tier)
        reachable = rarity_index(best_rarity_on_a_drop(tier))
        total = sum(RARITY_DROP_WEIGHT[rarity_at_index(rung)]
                    for rung in range(1, reachable + 1))

        for index in range(1, reachable + 1):
            rarity = rarity_at_index(index)
            expected = RARITY_DROP_WEIGHT[rarity] / total
            if abs(spread[rarity] - expected) > 1e-9:
                raise AssertionError(
                    f"at tier {tier}, {rarity} is {spread[rarity]:.6f} of "
                    f"drops against the {expected:.6f} its weight asks for")


def _check_nothing_drops_above_the_tier_cap() -> None:
    """Not even with a magic find nothing could reach."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        spread = rarity_distribution(tier, magic_find=1000.0)
        reachable = rarity_index(best_rarity_on_a_drop(tier))

        for index in range(reachable + 1, len(af.RARITIES) + 1):
            if spread[rarity_at_index(index)] != 0.0:
                raise AssertionError(
                    f"tier {tier} can drop {rarity_at_index(index)}, which is "
                    f"above its cap of {best_rarity_on_a_drop(tier)}")


def _check_a_distribution_always_sums_to_one() -> None:
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for magic_find in (0.0, 50.0, 100.0, 400.0, 1000.0):
            total = sum(rarity_distribution(tier, magic_find).values())
            if abs(total - 1.0) > 1e-9:
                raise AssertionError(
                    f"at tier {tier} with {magic_find}% magic find the "
                    f"distribution sums to {total}, not 1")


def _check_every_gear_slot_has_a_socket_maximum() -> None:
    """Every slot a drop can be for, so nothing rolls sockets it has no cap on."""
    covered = set(MAX_SOCKETS_BY_SLOT) | {"Weapon"}
    missing = set(af.GEAR_SLOTS) - covered
    extra = set(MAX_SOCKETS_BY_SLOT) - set(af.GEAR_SLOTS)
    if missing or extra:
        raise AssertionError(
            "the socket maximum table does not match the gear slots: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_two_one_handed_weapons_match_a_two_hander() -> None:
    """The design's own rule, and the reason a one-hander has three rather than
    some other number: "Every loadout gives the same maximum gem sockets"."""
    one = MAX_SOCKETS_BY_WEAPON_HANDS[1] * 2
    two = MAX_SOCKETS_BY_WEAPON_HANDS[2]
    if one != two:
        raise AssertionError(
            f"two one-handed weapons carry {one} sockets against a two-hander's "
            f"{two}, so one loadout is worth more sockets than the other")


def _check_the_socket_maxima_add_up_to_the_design_total() -> None:
    """41 across the gear, plus four potion slots, is the 45 the design states.

    THE CHECK THAT MAKES THIS TABLE MORE THAN A COPY. Every number in it is
    restated from the design document, and a restated number can be mistyped. The
    total is stated separately in the same document and in
    `player_power.TOTAL_SOCKETS`, so it catches a single wrong entry.
    """
    worn = sum(most * (RINGS_WORN if slot == "Ring" else 1)
               for slot, most in MAX_SOCKETS_BY_SLOT.items())
    worn += MAX_SOCKETS_BY_WEAPON_HANDS[2]

    total = worn + POTION_SOCKETS
    if total != player_power.TOTAL_SOCKETS:
        raise AssertionError(
            f"the socket maxima add up to {total} across all equipment, and the "
            f"design says {player_power.TOTAL_SOCKETS}")


def _check_every_rarity_has_a_residue_band() -> None:
    missing = set(af.RARITIES) - set(RARITY_RESIDUE_BAND)
    extra = set(RARITY_RESIDUE_BAND) - set(af.RARITIES)
    if missing or extra:
        raise AssertionError(
            "the residue band table does not match the rarity ladder: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_every_band_runs_upward_from_above_zero() -> None:
    for rarity, (lowest, highest) in RARITY_RESIDUE_BAND.items():
        if not 0.0 < lowest <= highest:
            raise AssertionError(
                f"{rarity} has a residue band of {lowest} to {highest}, which "
                "is not a band running upward from above zero")


def _check_no_residue_band_falls_as_rarity_rises() -> None:
    """A better item is never cheaper to improve at either end of its band."""
    bands = [RARITY_RESIDUE_BAND[rarity] for rarity in af.RARITIES]
    for lower, higher in zip(bands, bands[1:], strict=False):
        if higher[0] < lower[0] or higher[1] < lower[1]:
            raise AssertionError(
                f"the residue bands fall somewhere along the ladder: {bands}")


_check_every_rarity_has_a_gear_level_gate()
_check_the_gates_only_ever_rise()
_check_no_gate_is_out_of_reach()
_check_every_rarity_has_a_residue_band()
_check_every_band_runs_upward_from_above_zero()
_check_no_residue_band_falls_as_rarity_rises()
_check_every_gear_slot_has_a_socket_maximum()
_check_two_one_handed_weapons_match_a_two_hander()
_check_the_socket_maxima_add_up_to_the_design_total()
_check_every_rarity_has_a_drop_weight()
_check_no_drop_weight_is_negative()
_check_the_distribution_matches_the_weights()
_check_nothing_drops_above_the_tier_cap()
_check_a_distribution_always_sums_to_one()
