"""What the reference character actually DEALS per hit.

WHAT THIS IS FOR. `affixes.damage_target()` says what a player NEEDS at a
difficulty tier: it reads enemy health and divides by how many hits an enemy
should take. Nothing said what a player actually HAS. Those are different
questions and until now only the first had an answer, so no number in the
project could be set as a share of real player damage. Issue #336 needs one:
minion damage cannot be a share of the player's hit until the player's hit is a
number.

WHY IT IS A SEPARATE MODULE AND NOT PART OF `player_power.py`. It was asked for
there, and it cannot go there. `affixes.py` line 40 already imports
`player_power` -- it reads `GEAR_LEVEL_FACTOR` off `player_power.WEIGHTS` -- so
`player_power` importing `affixes` back is a circular import. The composition
needs weapon bases and affix values, and both live in `affixes.py`.

This is the same reason `reference_build.py` exists as a third module rather
than living inside `enemy_stats.py`, and it is resolved the same way: a module
that imports both, so the direction of the existing dependency is untouched.

`player_power.py` also holds the wrong shape of character for this. Its
`Character` carries a rarity and an upgrade level per piece and nothing else,
because Power Score does not read a weapon or an affix. Damage reads both.

WHAT IT DELIBERATELY DOES NOT MODEL.

* Gems. `game/Data/Gems.csv` grants ailment chances -- poison, bleed, void
  splinter -- and not one entry in it is a damage multiplier. There is no gem
  damage figure to read, so inventing one would be a number with no source.
  `more` is a parameter instead, defaulting to 1.0.
* Criticals. The target this is checked against is stated in non-critical hits,
  so `damage_per_hit` is a non-critical hit. `average_damage_per_hit` adds
  criticals and takes the two figures as arguments rather than deriving a
  critical progression that nothing states.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import affixes as af
from . import player_power
from .character import BASIC_ATTACK_SLOT, SKILL_SLOTS

# --------------------------------------------------------------------------
# The build being measured
# --------------------------------------------------------------------------

#: The weapon the reference build equips. `reference_build.BASES` ends in a
#: Greatsword, and this is the same character seen from the offensive side.
REFERENCE_WEAPON = "Greatsword"

#: How many of each kind of damage affix sit on the weapon itself.
#:
#: IT MATTERS WHICH PIECE CARRIES THEM, which is the gap `reference_build.py`
#: records in its own docstring and leaves open: a two-handed weapon doubles the
#: value of the affixes on it, so an affix moved onto the weapon is worth twice
#: as much. `reference_build` sums affixes across all eighteen pieces without
#: saying which piece holds which, and says plainly that this understates damage
#: and would have to be fixed before that build is used to measure damage. This
#: is that fix.
#:
#: ONE OF EACH IS WHAT A WEAPON HOLDS. A piece has two prefix slots, both damage
#: affixes are prefixes, and the weapon prefix pool for an attack build is flat
#: damage and increased damage. So the weapon carries one of each and the other
#: pieces carry the rest. `analyse_two_handed_multiplier.py` already assumes
#: exactly this split, and the two agreeing is the point.
FLAT_DAMAGE_AFFIXES_ON_THE_WEAPON = 1
INCREASED_DAMAGE_AFFIXES_ON_THE_WEAPON = 1


def affix_tier_at(tier: int) -> int:
    """The affix tier a character progressing on drops carries at a difficulty tier.

    Read off `affixes.max_affix_tier_on_a_drop`, which the project owner set in
    issue #241: a drop may roll one tier above the difficulty tier, capped at
    the seventh. Crafting is not capped, so a player who spends enough is above
    this; this is the ordinary case, matching how `reference_character` treats
    level and gear.
    """
    return af.max_affix_tier_on_a_drop(tier)


def gear_level_at(tier: int) -> int:
    """The gear upgrade level the reference character carries at a difficulty tier.

    Taken from `player_power.reference_character` rather than restated, so the
    character being scored and the character being measured for damage cannot
    drift apart.
    """
    return player_power.reference_character(tier).gear[0].upgrade


def weapon_base_damage(weapon: str, gear_level: int) -> float:
    """The flat attack damage a weapon base supplies at an upgrade level.

    Asked of the base rather than of each implicit, so a two-handed weapon's
    doubling is applied where the rule lives. A base with no attack damage
    implicit -- the Shield is the only one -- supplies none, which is correct
    rather than an error.
    """
    base = af.base_named(weapon)
    two_handed = base.value_multiplier != 1.0
    return sum(i.value_at(gear_level, two_handed=two_handed)
               for i in base.implicits
               if i.stat == "attack_damage" and i.kind == "flat")


# --------------------------------------------------------------------------
# The composition
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Breakdown:
    """One hit, with every bracket of the pipeline kept separately.

    Held apart rather than returned as one number because the interesting
    question is almost never the total: it is which bracket moved.
    """

    tier: int
    weapon: str
    affix_tier: int
    gear_level: int
    weapon_damage: float
    flat_from_affixes: float
    increased: float
    more: float
    skill_percent: float

    @property
    def base_bracket(self) -> float:
        """Weapon plus every flat damage affix. What a player reads as damage."""
        return self.weapon_damage + self.flat_from_affixes

    @property
    def per_hit(self) -> float:
        """One non-critical hit, through the three-bucket pipeline.

            (base + flat) x (1 + increases) x more x the skill's share
        """
        return (self.base_bracket * (1.0 + self.increased) * self.more
                * self.skill_percent / 100.0)


def breakdown(tier: int,
              weapon: str = REFERENCE_WEAPON,
              flat_affixes: int = af.REFERENCE_FLAT_DAMAGE_AFFIXES,
              increased_affixes: int = af.REFERENCE_INCREASED_DAMAGE_AFFIXES,
              skill_slot: str = BASIC_ATTACK_SLOT,
              more: float = 1.0) -> Breakdown:
    """Compose one hit for the reference character at a difficulty tier.

    The affix counts default to the same two constants `affixes.py` fits its
    damage values against, so the bottom-up figure and the top-down target
    describe the same spending. Passing different counts is what asks the
    "what if this build invested more" question.
    """
    if not 1 <= tier <= af.DIFFICULTY_TIERS:
        raise ValueError(f"tier {tier} is outside 1 to {af.DIFFICULTY_TIERS}")
    if skill_slot not in SKILL_SLOTS:
        raise ValueError(
            f"unknown skill slot {skill_slot!r}; expected one of "
            f"{list(SKILL_SLOTS)}")
    if flat_affixes < 0 or increased_affixes < 0:
        raise ValueError("affix counts cannot be negative")

    at = affix_tier_at(tier)
    level = gear_level_at(tier)
    base = af.base_named(weapon)
    two_handed = base.value_multiplier != 1.0

    on_weapon_flat = min(FLAT_DAMAGE_AFFIXES_ON_THE_WEAPON, flat_affixes)
    on_weapon_increased = min(INCREASED_DAMAGE_AFFIXES_ON_THE_WEAPON,
                              increased_affixes)

    flat = (af.FLAT_DAMAGE.value_at(at, gear_level=level)
            * (flat_affixes - on_weapon_flat)
            + af.FLAT_DAMAGE.value_at(at, gear_level=level,
                                      two_handed=two_handed) * on_weapon_flat)

    increased = (af.INCREASED_DAMAGE.value_at(at, gear_level=level)
                 * (increased_affixes - on_weapon_increased)
                 + af.INCREASED_DAMAGE.value_at(at, gear_level=level,
                                                two_handed=two_handed)
                 * on_weapon_increased) / 100.0

    return Breakdown(
        tier=tier,
        weapon=weapon,
        affix_tier=at,
        gear_level=level,
        weapon_damage=weapon_base_damage(weapon, level),
        flat_from_affixes=flat,
        increased=increased,
        more=more,
        skill_percent=SKILL_SLOTS[skill_slot].typical_damage,
    )


def damage_per_hit(tier: int,
                   weapon: str = REFERENCE_WEAPON,
                   flat_affixes: int = af.REFERENCE_FLAT_DAMAGE_AFFIXES,
                   increased_affixes: int = af.REFERENCE_INCREASED_DAMAGE_AFFIXES,
                   skill_slot: str = BASIC_ATTACK_SLOT,
                   more: float = 1.0) -> float:
    """One non-critical hit from the reference character at a difficulty tier."""
    return breakdown(tier, weapon, flat_affixes, increased_affixes,
                     skill_slot, more).per_hit


def average_damage_per_hit(tier: int,
                           crit_chance: float,
                           crit_multiplier: float,
                           **kwargs: object) -> float:
    """Including critical strikes, over many hits.

    The same form `enemy_stats.EnemyStats.average_damage_per_hit` uses, so the
    player and the enemies average criticals the same way. Both figures are
    arguments because nothing in the design states a critical progression per
    difficulty tier; `reference_build.character()` supplies them at tier 8.
    """
    chance = crit_chance / 100.0
    multiplier = crit_multiplier / 100.0
    return damage_per_hit(tier, **kwargs) * (1.0 - chance + chance * multiplier)


def damage_per_second(tier: int, weapon: str = REFERENCE_WEAPON,
                      **kwargs: object) -> float:
    """Hit damage times the weapon's swing rate, before any attack speed affix.

    The rate is read off the equipped weapon, which is where `character.py` puts
    its base, so changing the weapon changes both halves of this.
    """
    base = af.base_named(weapon)
    if not isinstance(base, af.WeaponBase):
        raise ValueError(f"{weapon} is not a weapon, so it has no attack speed")
    return damage_per_hit(tier, weapon, **kwargs) * base.attack_speed


def gap_against_target(tier: int, weapon: str = REFERENCE_WEAPON,
                       **kwargs: object) -> float:
    """Composed damage divided by what `affixes.damage_target` says is needed.

    One means the two agree. Above one is a build that overshoots the content.

    THE TARGET APPLIES NO MITIGATION and is wrong for it, which is issue #511
    and is stated in `damage_target`'s own docstring. Comparing against it is
    still the right check, because every offensive number in `affixes.py` is
    fitted to that same figure; a comparison against a different one would say
    nothing about whether the pool is consistent with itself.
    """
    return damage_per_hit(tier, weapon, **kwargs) / af.damage_target(tier)


# --------------------------------------------------------------------------
# Checks that run at import
# --------------------------------------------------------------------------

def _check_the_reference_weapon_exists_and_is_a_weapon() -> None:
    base = af.base_named(REFERENCE_WEAPON)
    if not isinstance(base, af.WeaponBase):
        raise ValueError(f"{REFERENCE_WEAPON} is not a weapon base")
    if weapon_base_damage(REFERENCE_WEAPON, af.MAX_GEAR_LEVEL) <= 0:
        raise ValueError(
            f"{REFERENCE_WEAPON} supplies no attack damage, so it cannot be "
            "the weapon a damage figure is composed from")


def _check_the_weapon_cannot_hold_more_damage_affixes_than_it_has_prefixes() -> None:
    """Both damage affixes are prefixes, and a piece has two prefix slots."""
    on_weapon = (FLAT_DAMAGE_AFFIXES_ON_THE_WEAPON
                 + INCREASED_DAMAGE_AFFIXES_ON_THE_WEAPON)
    if on_weapon > af.PREFIXES_PER_PIECE:
        raise ValueError(
            f"{on_weapon} damage affixes are placed on the weapon and a piece "
            f"holds {af.PREFIXES_PER_PIECE} prefixes")


def _check_both_damage_affixes_are_prefixes_a_weapon_can_roll() -> None:
    for affix in (af.FLAT_DAMAGE, af.INCREASED_DAMAGE):
        if affix.position != af.PREFIX:
            raise ValueError(
                f"{affix.name} is placed in a prefix slot and is not a prefix")
        if "Weapon" not in affix.allowed_slots:
            raise ValueError(
                f"{affix.name} is placed on the weapon and cannot roll there")


_check_the_reference_weapon_exists_and_is_a_weapon()
_check_the_weapon_cannot_hold_more_damage_affixes_than_it_has_prefixes()
_check_both_damage_affixes_are_prefixes_a_weapon_can_roll()


if __name__ == "__main__":
    print("What the reference character deals per hit. Issue #336.")
    print()
    print(f"  Weapon {REFERENCE_WEAPON}, "
          f"{af.REFERENCE_FLAT_DAMAGE_AFFIXES} flat damage affixes and "
          f"{af.REFERENCE_INCREASED_DAMAGE_AFFIXES} increased, one of each on "
          f"the weapon.")
    print()
    print("  Composed from the gear, against what the enemies require:")
    print()
    print(f"    {'tier':<5} {'affix':>6} {'gear':>5} {'weapon':>8} {'flat':>7} "
          f"{'incr':>7} {'per hit':>9} {'target':>8} {'gap':>6}")
    print("    " + "-" * 72)
    for tier in range(1, 9):
        b = breakdown(tier)
        print(f"    T{tier:<4} {b.affix_tier:>6} +{b.gear_level:<4} "
              f"{b.weapon_damage:>8,.0f} {b.flat_from_affixes:>7,.0f} "
              f"{b.increased * 100:>6,.0f}% {b.per_hit:>9,.0f} "
              f"{af.damage_target(tier):>8,.0f} {gap_against_target(tier):>5.2f}x")
    print()

    print("  Every weapon at difficulty tier 8, one basic attack:")
    print()
    print(f"    {'weapon':<22} {'hands':>5} {'base':>7} {'per hit':>9} "
          f"{'gap':>6} {'per second':>11}")
    print("    " + "-" * 66)
    weapons = [b for b in af.ITEM_BASES
               if isinstance(b, af.WeaponBase)
               and weapon_base_damage(b.name, af.MAX_GEAR_LEVEL) > 0]
    for base in sorted(weapons, key=lambda b: damage_per_hit(8, b.name)):
        print(f"    {base.name:<22} {base.hands:>5} "
              f"{weapon_base_damage(base.name, af.MAX_GEAR_LEVEL):>7,.0f} "
              f"{damage_per_hit(8, base.name):>9,.0f} "
              f"{gap_against_target(8, base.name):>5.2f}x "
              f"{damage_per_second(8, base.name):>11,.0f}")
    print()
    print(f"  The target is {af.damage_target(8):,.0f}. No single weapon sits on it,")
    print("  and the spread between the weakest and strongest is what a weapon")
    print("  choice is worth.")
