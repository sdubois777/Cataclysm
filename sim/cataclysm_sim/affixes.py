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

from . import enemy_stats, player_power

#: The eight damage types, so the eight resistances.
DAMAGE_TYPES = ("War", "Demonic", "Death", "Pestilence",
                "Famine", "Celestial", "Chaos", "Void")

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
RESISTANCE_CAP = 70.0

#: A character has 18 gear pieces with up to 4 affix slots each, and those slots
#: are shared with enchantments.
GEAR_PIECES = 18
AFFIX_SLOTS_PER_PIECE = 4
TOTAL_AFFIX_SLOTS = GEAR_PIECES * AFFIX_SLOTS_PER_PIECE

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
OFFENSIVE_SLOTS = frozenset({"Weapon", "Ring", "Relic", "Necklace", "Gloves"})
DEFENSIVE_SLOTS = frozenset({"Head", "Chest", "Belt", "Pants", "Boots", "Ring"})
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

    def __post_init__(self) -> None:
        if self.kind not in ("flat", "increased"):
            raise ValueError(
                f"{self.name}: kind must be 'flat' or 'increased', "
                f"got {self.kind!r}")
        unknown = set(self.allowed_slots) - set(GEAR_SLOTS)
        if unknown:
            raise ValueError(
                f"{self.name} allows slots that do not exist: {sorted(unknown)}")

    def slots_available(self) -> int:
        return slots_available_to(self.allowed_slots)

    def range_at(self, tier: int) -> tuple[float, float]:
        return tier_band(self.top_value, tier)

    def value_at(self, tier: int, roll: float = 1.0,
                 gear_level: int = MAX_GEAR_LEVEL) -> float:
        return affix_value(self.top_value, tier, roll, gear_level)

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
                        DEFENSIVE_SLOTS)
INCREASED_HEALTH = StatAffix("Increased maximum health", "max_health",
                             "increased", 12.0, DEFENSIVE_SLOTS)

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
    """
    common = enemy_stats.stats_on_floor("Common", tier, "Cataclysm")
    return common.effective_health / HITS_TO_KILL_A_COMMON_ENEMY


#: Damage. Increased damage is 125% at T7, set by the project owner.
#:
#: THE 125% IS WHAT FORCES FLAT DAMAGE TO BE SMALL. A character with eight
#: increased damage affixes is already multiplying by 11, so the bracket those
#: multiply has to be around 150 at tier 8 to land on `damage_target()`. Flat
#: damage was 60, which meant three affixes alone filled the whole bracket and
#: the weapon had nothing left to contribute.
#:
#: 18 IS DERIVED, NOT PICKED. It is set so the choice between the two kinds is
#: real, which is the entire point of having both. `crossover_base` with eight
#: increased affixes in place puts them at equal value at 158 points of base, and
#: a build with a weapon supplying 81 crosses that after four or five flat
#: affixes. So flat wins early and increased wins later, and a character actually
#: takes some of each.
#:
#: Both neighbouring values fail that. At 60 the crossover is 528, which no build
#: reaches, so flat wins always. At 12 it is 106, below where a real build starts,
#: so increased wins always. Either way one of the two kinds is dead content.
#:
#: Flat damage being far smaller than flat health is not an inconsistency. The
#: two stats have different multiplier scales by design: 125% per damage affix
#: against 12% per health affix.
FLAT_DAMAGE = StatAffix("Flat damage", "attack_damage", "flat", 18.0,
                        OFFENSIVE_SLOTS)
INCREASED_DAMAGE = StatAffix("Increased damage", "attack_damage",
                             "increased", 125.0, OFFENSIVE_SLOTS)

HEALTH_AFFIXES = (FLAT_HEALTH, INCREASED_HEALTH)
DAMAGE_AFFIXES = (FLAT_DAMAGE, INCREASED_DAMAGE)


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

    "Base bracket" rather than "weapon" because a skill's own multiplier is not
    modelled anywhere yet. The design says skills come from weapon type paired
    with damage type and never says what any of them are worth, so this figure is
    the weapon and the skill together. See issue #107.

    A NEGATIVE RESULT IS INFORMATION, NOT A FAULT. It means the affixes alone
    already exceed the target, so that build overshoots the content, which is
    what heavy investment is supposed to do.
    """

    flat = FLAT_DAMAGE.value_at(tier, roll) * flat_slots
    increases = INCREASED_DAMAGE.value_at(tier, roll) / 100.0 * increased_slots
    return target_damage / (1.0 + increases) - flat


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
