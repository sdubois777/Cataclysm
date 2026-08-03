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
its own progression.
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
#: Front-loaded rather than linear: an affix reaches half its final value by T3,
#: so a mid-tier roll is useful rather than filler, and the last two tiers are
#: worth chasing without being the only ones that matter.
TIER_FRACTIONS: dict[int, float] = {
    1: 0.20, 2: 0.35, 3: 0.50, 4: 0.65, 5: 0.80, 6: 0.90, 7: 1.00,
}

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

    def value_at(self, tier: int) -> float:
        """Percentage granted to each covered resistance, at an affix tier."""
        if tier not in TIER_FRACTIONS:
            raise ValueError(f"affix tier {tier} outside {sorted(TIER_FRACTIONS)}")
        return self.top_value * TIER_FRACTIONS[tier]

    def total_coverage(self, tier: int) -> float:
        """Percentage points granted across everything it covers.

        Broader families grant less per type and more in total, which is what
        makes the choice a real one rather than a strict ordering.
        """
        return self.value_at(tier) * self.breadth

    def useful_coverage(self, tier: int, active_cataclysms: int) -> float:
        """Coverage that is not wasted.

        An all-resistance affix covers eight damage types. If only two are
        attacking, six of those are worth nothing, and this is the whole reason
        the efficient family changes as a run goes on.
        """
        used = min(self.breadth, max(0, active_cataclysms))
        return self.value_at(tier) * used


#: Proposed families. Per-type value falls as breadth rises, which the project
#: owner specified; total coverage rises, which is what stops the narrow family
#: being strictly better.
SINGLE_RESISTANCE = AffixFamily("Single resistance", breadth=1, top_value=20.0)
HYBRID_RESISTANCE = AffixFamily("Two resistances", breadth=2, top_value=14.0)
ALL_RESISTANCE = AffixFamily("All resistances", breadth=8, top_value=6.0)

RESISTANCE_FAMILIES = (SINGLE_RESISTANCE, HYBRID_RESISTANCE, ALL_RESISTANCE)


def slots_to_cap(family: AffixFamily, tier: int, active_cataclysms: int,
                 affix_tier: int = 7) -> float:
    """Affix slots needed to cap every active resistance using one family."""
    needed = RESISTANCE_CAP * max(1, active_cataclysms)
    per_slot = family.useful_coverage(affix_tier, active_cataclysms)
    if per_slot <= 0:
        return float("inf")
    return needed / per_slot


def best_family(active_cataclysms: int, affix_tier: int = 7) -> AffixFamily:
    """Which family caps every active resistance in the fewest slots."""
    return min(RESISTANCE_FAMILIES,
               key=lambda f: slots_to_cap(f, active_cataclysms, active_cataclysms,
                                          affix_tier))


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
    print("Values per covered resistance, by affix tier:")
    print()
    header = f"    {'family':<20} " + "".join(f"{'T' + str(t):>7}" for t in AFFIX_TIERS)
    print(header + f"{'coverage':>11}")
    print("    " + "-" * (len(header) + 7))
    for f in RESISTANCE_FAMILIES:
        row = f"    {f.name:<20} " + "".join(f"{f.value_at(t):>6.1f}%" for t in AFFIX_TIERS)
        print(row + f"{f.total_coverage(7):>10.0f}")
    print()
    print("    'coverage' is percentage points across everything the affix")
    print("    covers, at T7. Narrower families give more per type; broader")
    print("    ones give more in total. That is what makes it a choice.")
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
    print("    The efficient family changes as the run goes on, which is the")
    print("    point of having three. A single-resistance affix is the best use")
    print("    of a slot when one Cataclysm is active and nearly worthless when")
    print("    eight are; an all-resistance affix is the reverse.")
