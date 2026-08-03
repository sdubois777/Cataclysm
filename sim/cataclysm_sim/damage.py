"""How one incoming hit becomes lost health.

WHAT THIS IS FOR. `docs/Cataclysm_GDD_v2.md` names every defensive stat and never
says how any of them combine. The consequence is concrete: the Ritualist's 832
energy shield and the Ravager's 371 armor are both declared, replicated and
completely inert, because nothing reads them. Issue #93.

This module is a proposal for the order and the formulas, written so the numbers
can be looked at rather than argued about.

WHAT IS ALREADY DECIDED AND IS NOT UP FOR DEBATE HERE:

    Evasion avoids an attack completely, but only a direct attack. Area damage
    lands regardless. Soft cap 60%, exceedable.

    Block is a chance, and a block removes 50% of the hit rather than preventing
    it. It applies to area damage as well as direct attacks. No cap.

    Resistances cap at 70% and may be over-capped by affixes.

Everything else below is proposed.

WHAT THIS DELIBERATELY DOES NOT DO. It takes the incoming damage as a parameter.
There are still no enemy damage numbers anywhere in the project, so this answers
"of a hit of X, how much reaches health" and not "how big is a hit". Those are
separate problems and the second one is not blocked by this.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# Armor
# --------------------------------------------------------------------------

#: Armor is converted to a percentage by `armor / (armor + K)`, where K rises
#: with the difficulty tier.
#:
#: Why this shape rather than subtracting armor from damage: it never reaches
#: 100%, so no amount of armor is immunity, and it has natural diminishing
#: returns, so the first points of armor matter most.
#:
#: Why K rises with tier: at a fixed K, armor earned early would keep its value
#: forever and gear would stop mattering. At 800 per tier, the Ravager's 371 base
#: armor is worth 32% at tier 1 and 5% at tier 8, so the class identity holds
#: early and gear has to carry it later.
ARMOR_CONSTANT_PER_TIER = 800.0

#: Even with the asymptote, armor alone should not approach immunity.
ARMOR_REDUCTION_CAP = 75.0

# --------------------------------------------------------------------------
# Resistance
# --------------------------------------------------------------------------

RESISTANCE_CAP = 70.0

#: Negative resistance means taking extra damage, which several enchantments
#: inflict deliberately. This bounds how bad it can get.
RESISTANCE_FLOOR = -100.0

# --------------------------------------------------------------------------
# Block
# --------------------------------------------------------------------------

BLOCK_DAMAGE_REDUCTION = 50.0

# --------------------------------------------------------------------------
# Weapon sub-types
# --------------------------------------------------------------------------

#: From the Weapon Sub-Types table: piercing ignores 20% of enemy armor,
#: slashing does 10% more damage versus HP, blunt 10% more versus armor, and
#: magic 10% more versus shields.
PIERCING_ARMOR_IGNORED = 20.0
SUBTYPE_BONUS = 10.0

WEAPON_SUBTYPES = ("Piercing", "Slashing", "Blunt", "Magic", "None")


@dataclass(frozen=True)
class Attacker:
    """What the incoming hit is."""

    damage: float
    #: Which of the eight resistances applies.
    damage_type: str = "Demonic"
    #: Percentage points subtracted from the defender's resistance.
    penetration: float = 0.0
    subtype: str = "None"
    #: Area damage cannot be evaded. It can still be blocked.
    is_area: bool = False

    def __post_init__(self) -> None:
        if self.subtype not in WEAPON_SUBTYPES:
            raise ValueError(f"unknown weapon sub-type {self.subtype!r}")
        if self.damage < 0:
            raise ValueError("damage cannot be negative")


@dataclass(frozen=True)
class Defender:
    """The stats a hit is resolved against. Names match the character sheet."""

    health: float
    energy_shield: float = 0.0
    armor: float = 0.0
    evasion: float = 0.0
    block_chance: float = 0.0
    damage_reduction: float = 0.0
    resistances: dict[str, float] = field(default_factory=dict)
    tier: int = 1

    def resistance_to(self, damage_type: str) -> float:
        return self.resistances.get(damage_type, 0.0)


@dataclass(frozen=True)
class Resolution:
    """What happened to one hit, step by step, so it can be inspected."""

    evaded: bool
    blocked: bool
    incoming: float
    after_block: float
    after_armor: float
    after_resistance: float
    after_reduction: float
    absorbed_by_shield: float
    dealt_to_health: float

    @property
    def total_mitigated(self) -> float:
        return self.incoming - self.dealt_to_health


def armor_reduction(armor: float, tier: int) -> float:
    """Armor as a percentage of damage removed."""
    if armor <= 0:
        return 0.0
    k = ARMOR_CONSTANT_PER_TIER * max(1, tier)
    return min(ARMOR_REDUCTION_CAP, 100.0 * armor / (armor + k))


def effective_resistance(resistance: float, penetration: float) -> float:
    """Resistance after penetration, then capped.

    THE ORDER HERE IS THE MOST LOAD-BEARING CHOICE IN THIS MODULE. Penetration
    is subtracted BEFORE the cap is applied, which is the only thing that makes
    over-capping worth anything: a defender at 100 resistance facing 30
    penetration still sits at the 70 cap, where one at exactly 70 drops to 40.

    Capping first would make every point above 70 worthless and would contradict
    the design's own statement that over-capping is possible via affixes.
    """
    return max(RESISTANCE_FLOOR,
               min(RESISTANCE_CAP, resistance - penetration))


def resolve(attacker: Attacker, defender: Defender,
            rng: random.Random | None = None,
            force_evade: bool | None = None,
            force_block: bool | None = None) -> Resolution:
    """Run one hit through the whole order.

    `force_evade` and `force_block` exist so tests can pin the two random rolls
    and check the arithmetic rather than the dice.
    """
    rng = rng or random.Random()

    def zero(evaded: bool) -> Resolution:
        return Resolution(evaded=evaded, blocked=False,
                          incoming=attacker.damage, after_block=0.0,
                          after_armor=0.0, after_resistance=0.0,
                          after_reduction=0.0, absorbed_by_shield=0.0,
                          dealt_to_health=0.0)

    # 1. Evasion. Direct attacks only; area damage lands regardless.
    if not attacker.is_area:
        evaded = (force_evade if force_evade is not None
                  else rng.uniform(0, 100) < defender.evasion)
        if evaded:
            return zero(evaded=True)

    # 2. Block. Applies to area damage too. Removes half the hit, not all of it.
    blocked = (force_block if force_block is not None
               else rng.uniform(0, 100) < defender.block_chance)
    damage = attacker.damage
    if blocked:
        damage *= 1.0 - BLOCK_DAMAGE_REDUCTION / 100.0
    after_block = damage

    # 3. Armor. Piercing ignores a share of it before the conversion.
    armor = defender.armor
    if attacker.subtype == "Piercing":
        armor *= 1.0 - PIERCING_ARMOR_IGNORED / 100.0
    damage *= 1.0 - armor_reduction(armor, defender.tier) / 100.0
    # Blunt is "10% more damage versus armor", read as a bonus that only applies
    # when there is armor to beat. See the module note on the overlap with
    # piercing.
    if attacker.subtype == "Blunt" and defender.armor > 0:
        damage *= 1.0 + SUBTYPE_BONUS / 100.0
    after_armor = damage

    # 4. Resistance, penetrated first and capped second.
    resist = effective_resistance(
        defender.resistance_to(attacker.damage_type), attacker.penetration)
    damage *= 1.0 - resist / 100.0
    after_resistance = damage

    # 5. Flat damage reduction.
    damage *= 1.0 - defender.damage_reduction / 100.0
    after_reduction = damage

    # 6. Energy shield absorbs before health. Magic weapons are better against
    # the shield, slashing weapons better against what gets past it.
    shield_damage = damage * (1.0 + SUBTYPE_BONUS / 100.0
                              if attacker.subtype == "Magic" else 1.0)
    absorbed = min(defender.energy_shield, shield_damage)
    # Convert what the shield actually stopped back into raw damage, so a magic
    # bonus does not destroy more raw damage than the hit contained.
    consumed = absorbed / (1.0 + SUBTYPE_BONUS / 100.0
                           if attacker.subtype == "Magic" else 1.0)
    to_health = max(0.0, damage - consumed)
    if attacker.subtype == "Slashing":
        to_health *= 1.0 + SUBTYPE_BONUS / 100.0

    return Resolution(
        evaded=False, blocked=blocked, incoming=attacker.damage,
        after_block=after_block, after_armor=after_armor,
        after_resistance=after_resistance, after_reduction=after_reduction,
        absorbed_by_shield=absorbed, dealt_to_health=min(to_health, defender.health),
    )


def hits_to_kill(attacker: Attacker, defender: Defender,
                 max_hits: int = 10_000) -> float:
    """How many hits it takes to empty the shield and then the health.

    Applied one hit at a time, with the shield depleting as it absorbs. Dividing
    the combined pool by the damage of a single hit would be wrong: it assumes
    the shield absorbs its whole value again on every hit, when a shield is
    spent once. That mistake makes a shielded character look several times
    tougher than it is.

    Regeneration is deliberately not modelled here. Between hits a character
    does regain shield, but how much depends on the interval between hits, and
    there are no attack speed numbers to supply it.
    """
    shield = defender.energy_shield
    health = defender.health
    for hit in range(1, max_hits + 1):
        state = Defender(
            health=health, energy_shield=shield, armor=defender.armor,
            evasion=defender.evasion, block_chance=defender.block_chance,
            damage_reduction=defender.damage_reduction,
            resistances=defender.resistances, tier=defender.tier)
        taken = average_damage_taken(attacker, state)
        # Work out how much of the shield that average hit consumed.
        blocked = resolve(attacker, state, force_evade=False, force_block=True)
        unblocked = resolve(attacker, state, force_evade=False, force_block=False)
        bc = min(100.0, defender.block_chance) / 100.0
        absorbed = bc * blocked.absorbed_by_shield + (1 - bc) * unblocked.absorbed_by_shield
        if not attacker.is_area:
            absorbed *= 1.0 - min(100.0, defender.evasion) / 100.0

        if taken <= 0 and absorbed <= 0:
            return float("inf")
        shield = max(0.0, shield - absorbed)
        health -= taken
        if health <= 0:
            return float(hit)
    return float("inf")


def average_damage_taken(attacker: Attacker, defender: Defender) -> float:
    """Expected damage to health, averaging over the evasion and block rolls.

    Useful for comparing builds without sampling thousands of hits.
    """
    evade_chance = 0.0 if attacker.is_area else min(100.0, defender.evasion) / 100.0
    block_chance = min(100.0, defender.block_chance) / 100.0

    blocked = resolve(attacker, defender, force_evade=False, force_block=True)
    unblocked = resolve(attacker, defender, force_evade=False, force_block=False)
    hit = (block_chance * blocked.dealt_to_health
           + (1.0 - block_chance) * unblocked.dealt_to_health)
    return (1.0 - evade_chance) * hit


if __name__ == "__main__":
    from .character import Attributes, Character
    from .classes import DEMONIC_CLASSES

    print("Proposed damage calculation, issue #93")
    print()
    print("  Order: evasion, block, armor, resistance, flat reduction,")
    print("  energy shield, health.")
    print()
    print("  Armor as a percentage, by tier. K rises 800 per tier, so armor")
    print("  earned early does not keep its value forever:")
    print()
    print(f"    {'armor':>7} " + "".join(f"{'T' + str(t):>7}" for t in (1, 4, 8)))
    for armor in (100, 371, 1000, 3000, 8000):
        row = f"    {armor:>7} "
        for tier in (1, 4, 8):
            row += f"{armor_reduction(armor, tier):>6.1f}%"
        print(row)
    print()

    print("  Why penetration is applied BEFORE the 70% cap. Two defenders,")
    print("  one at the cap and one over-capped, against 30 penetration:")
    print()
    for raw in (70.0, 100.0):
        capped_first = min(RESISTANCE_CAP, raw) - 30.0
        pen_first = effective_resistance(raw, 30.0)
        print(f"    {raw:>5.0f} resistance -> cap first: {capped_first:>5.1f}%   "
              f"penetrate first: {pen_first:>5.1f}%  <- proposed")
    print()
    print("    Capping first makes every point above 70 worthless, which")
    print("    contradicts the design allowing affixes to over-cap.")
    print()

    print("  A hit of 1,000 Demonic damage against each Demonic class at")
    print("  level 100, no gear, every point in Vitality, tier 8:")
    print()
    print(f"    {'class':<11} {'health':>8} {'shield':>7} {'armor':>7} "
          f"{'taken':>7} {'hits to kill':>13}")
    for name, definition in DEMONIC_CLASSES.items():
        c = Character(definition, level=100, attributes=Attributes(vitality=100))
        d = Defender(
            health=c.stat("max_health"),
            energy_shield=c.stat("max_energy_shield"),
            armor=c.stat("armor"),
            evasion=c.stat("evasion"),
            block_chance=c.stat("block_chance"),
            damage_reduction=c.stat("damage_reduction"),
            resistances={"Demonic": c.stat("resistance_demonic")},
            tier=8,
        )
        attack = Attacker(damage=1000.0)
        taken = average_damage_taken(attack, d)
        print(f"    {name:<11} {d.health:>8,.0f} {d.energy_shield:>7,.0f} "
              f"{d.armor:>7,.0f} {taken:>7,.0f} "
              f"{hits_to_kill(attack, d):>13.0f}")
    print()
    print("  Hits to kill applies hits one at a time with the shield depleting.")
    print("  Dividing the combined pool by one hit's damage would be wrong: it")
    print("  assumes the shield absorbs its whole value again on every hit, and")
    print("  it made the Ritualist look three times tougher than it is.")
    print()
    print("  Energy shield and armor both do something here, which they do not")
    print("  in the game today, because nothing reads them.")
