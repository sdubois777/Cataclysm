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
#: How wide the band is, as a fraction of the gap between one tier and the next.
#: Below 1.0 the bands do not overlap, so ANY roll at a higher tier beats ANY
#: roll at a lower one. That keeps tier the primary axis and the roll the
#: secondary one: a lucky T4 must never beat an unlucky T5, or tiers stop
#: meaning anything.
ROLL_BAND_FRACTION = 0.60

#: The character sheet's resistance cap. Reaching it on every active damage type
#: is what a build is trying to do.
RESISTANCE_CAP = 70.0

#: A character has 18 gear pieces with up to 4 affix slots each, and those slots
#: are shared with enchantments.
GEAR_PIECES = 18
AFFIX_SLOTS_PER_PIECE = 4
TOTAL_AFFIX_SLOTS = GEAR_PIECES * AFFIX_SLOTS_PER_PIECE


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
        down by a fraction of the gap to the tier below, which leaves a gap
        between tiers so that any higher-tier roll beats any lower-tier one.
        """
        if tier not in TIER_FRACTIONS:
            raise ValueError(f"affix tier {tier} outside {sorted(TIER_FRACTIONS)}")
        high = self.top_value * TIER_FRACTIONS[tier]
        step = self.top_value / len(AFFIX_TIERS)
        return (max(0.0, high - step * ROLL_BAND_FRACTION), high)

    def value_at(self, tier: int, roll: float = 1.0) -> float:
        """Percentage granted to each covered resistance.

        `roll` places the result inside the tier's band: 0.0 is the worst
        possible roll, 1.0 the best. It defaults to a perfect roll, because the
        questions this module answers -- how many slots to reach the resistance
        cap -- are about what a finished, crafted character can achieve.
        """
        low, high = self.range_at(tier)
        return low + (high - low) * min(max(roll, 0.0), 1.0)

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
