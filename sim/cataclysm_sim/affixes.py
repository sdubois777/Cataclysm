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

    def value_at(self, tier: int, roll: float = 1.0) -> float:
        """Percentage granted to each covered resistance.

        `roll` places the result inside the tier's band: 0.0 is the worst
        possible roll, 1.0 the best. It defaults to a perfect roll, because the
        questions this module answers -- how many slots to reach the resistance
        cap -- are about what a finished, crafted character can achieve.
        """
        low, high = self.range_at(tier)
        return roll_within(low, high, roll)

    def average_at(self, tier: int) -> float:
        """The middle of the band. What an uncrafted drop is worth on average."""
        return self.value_at(tier, roll=0.5)

    def total_coverage(self, tier: int, roll: float = 1.0) -> float:
        """Percentage points granted across everything it covers.

        Broader families grant less per type and more in total, which is what
        makes the choice a real one rather than a strict ordering.
        """
        return self.value_at(tier, roll) * self.breadth

    def useful_coverage(self, tier: int, active_cataclysms: int,
                        roll: float = 1.0) -> float:
        """Coverage that is not wasted.

        An all-resistance affix covers eight damage types. If only two are
        attacking, six of those are worth nothing, and this is the whole reason
        the efficient family changes as a run goes on.
        """
        used = min(self.breadth, max(0, active_cataclysms))
        return self.value_at(tier, roll) * used


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

    def __post_init__(self) -> None:
        if self.kind not in ("flat", "increased"):
            raise ValueError(
                f"{self.name}: kind must be 'flat' or 'increased', "
                f"got {self.kind!r}")

    def range_at(self, tier: int) -> tuple[float, float]:
        return tier_band(self.top_value, tier)

    def value_at(self, tier: int, roll: float = 1.0) -> float:
        low, high = self.range_at(tier)
        return roll_within(low, high, roll)

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
FLAT_HEALTH = StatAffix("Flat maximum health", "max_health", "flat", 120.0)
INCREASED_HEALTH = StatAffix("Increased maximum health", "max_health",
                             "increased", 12.0)

#: Damage. Same structure, and the numbers are pinned to a target rather than
#: chosen: `enemy_stats.PLAYER_DAMAGE_FACTOR` says a player hits for 25% of their
#: Power Score, which is 1,582 at tier 8, and that is what makes the "player
#: kills a common enemy in 1 to 3 hits" target hold. Gear has to deliver it.
FLAT_DAMAGE = StatAffix("Flat damage", "attack_damage", "flat", 60.0)
INCREASED_DAMAGE = StatAffix("Increased damage", "attack_damage",
                             "increased", 15.0)

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
    """What a weapon must contribute for a build to reach a damage target.

    Solves the pipeline backwards. There are no weapon damage numbers anywhere in
    the project, so this is how the affix values imply one rather than the other
    way round.
    """
    flat = FLAT_DAMAGE.value_at(tier, roll) * flat_slots
    increases = INCREASED_DAMAGE.value_at(tier, roll) / 100.0 * increased_slots
    return target_damage / (1.0 + increases) - flat


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

    print("  Bands do not overlap, so any roll at a higher tier beats any roll")
    print("  at a lower one. Tier stays the primary axis and the roll the")
    print("  secondary one.")
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
    from .enemy_stats import PLAYER_DAMAGE_FACTOR

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
    print(f"    Flat and increased health are worth the same at {over:,.0f} base.")
    flat_slots_to_reach = (over - base) / FLAT_HEALTH.value_at(7)
    print(f"    That is about {flat_slots_to_reach:.0f} flat affixes away, so flat")
    print("    wins early in a build and increased wins after it.")
    print()

    print("    Damage affixes are pinned to a target rather than chosen. A")
    print(f"    player hits for {PLAYER_DAMAGE_FACTOR:.0%} of their Power Score, which is")
    target = 6327 * PLAYER_DAMAGE_FACTOR
    print(f"    {target:,.0f} at tier 8. Solving the pipeline backwards gives what a")
    print("    weapon has to supply, which is a number the project does not have:")
    print()
    print(f"      {'flat slots':>11} {'increased slots':>16} {'weapon base needed':>20}")
    for flat_slots, inc_slots in ((4, 4), (6, 6), (8, 8), (12, 0), (0, 12)):
        need = weapon_base_damage_needed(target, flat_slots, inc_slots)
        print(f"      {flat_slots:>11} {inc_slots:>16} {need:>20,.0f}")
    print()
    print("    Spending nothing on damage affixes would need a weapon supplying")
    print(f"    the whole {target:,.0f}, and spending 12 slots on increases alone")
    print("    still needs a large one. That is the shape of the trade.")
