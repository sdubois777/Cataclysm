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

A HIT BELONGS TO A LOADOUT, NOT TO A WEAPON. The first version of this module
took one weapon name and composed a hit from it. That produced a false finding
-- that no weapon reaches the damage target -- because the target's weapon term
of about 90 is what a PAIR of one-handers supplies, and no single weapon was
ever meant to supply it. Issue #610 was filed on that false finding and closed.
Everything here takes a loadout.

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
# What a character may be holding
# --------------------------------------------------------------------------

#: The base that is an offhand rather than a weapon.
#:
#: STATED BY THE PROJECT OWNER on 2026-08-15: a Shield is an offhand, and a
#: one-handed weapon with a Shield is a legal loadout. `affixes.py` still models
#: it as a `WeaponBase` with one hand, and `docs/Cataclysm_GDD_v2.md` still lists
#: it among the one-handed weapon types and says there are no offhand items.
#: Reclassifying it everywhere touches Power Score's piece count, the affix slot
#: count and the attack speed average, so it is its own change and its own issue.
#:
#: What this module does in the meantime is narrow and safe: a Shield contributes
#: no attack damage, because it has no attack damage implicit, and it is left out
#: of the attack rate average, because a shield is not swung.
OFFHAND = "Shield"

#: The loadout the damage target describes: two one-handed weapons.
#:
#: STATED BY THE PROJECT OWNER on 2026-08-15, choosing between three candidates.
#: The arithmetic agrees from two directions. `affixes.reference_weapon_base`
#: solves the pipeline backwards and reports the weapon term must supply about
#: 90; the two strongest legal pairs bracket it, an Axe with an Axe at 92 and an
#: Axe with a Sword at 86, while no single weapon is near it. And reading it as a
#: pair puts a Greatsword at 1.33 times the target, which is exactly the
#: two-handed advantage `docs/Cataclysm_GDD_v2.md` line 2382 already states.
#:
#: THIS PAIR IS THE DESIGN DOCUMENT'S OWN EXAMPLE. Line 2378: "Two one-handed
#: weapons **sum** their base damage, so an Axe and a Sword give 86 against a
#: Greatsword's stated 78."
REFERENCE_LOADOUT: tuple[str, ...] = ("Axe", "Sword")

#: How many of each kind of damage affix sit on a weapon rather than elsewhere.
#:
#: ZERO BY DEFAULT, BECAUSE ZERO IS WHAT THE TARGET WAS FITTED WITH. The values
#: of `FLAT_DAMAGE` and `INCREASED_DAMAGE` were solved against six of each at
#: their plain one-handed value, which is 108 flat and 750% increased, with no
#: piece named. `gap_against_target` compares against that fit, so composing with
#: a different assumption would compare two different builds and then report the
#: difference between them as a defect. That is how issue #610 was filed.
#:
#: PLACEMENT IS REAL AND IS STILL AVAILABLE. A two-handed weapon doubles the
#: affixes sitting on it, so where an affix sits changes what it is worth, and
#: that is the gap `reference_build.py` records against itself. Pass a non-zero
#: count to model it.
#:
#: IT DOES NOT CHANGE THE RATIO BETWEEN LOADOUTS. `analyse_two_handed_multiplier`
#: places one damage affix of each kind on a two-hander and two on a dual
#: wielder, and both then contribute the same 126 flat and 875% increased,
#: because one doubled affix is worth exactly two undoubled ones. Placement moves
#: every loadout together.
FLAT_DAMAGE_AFFIXES_ON_THE_WEAPON = 0
INCREASED_DAMAGE_AFFIXES_ON_THE_WEAPON = 0


def normalise_loadout(loadout: str | tuple[str, ...] | list[str]) -> tuple[str, ...]:
    """Accept a single name or a sequence, and check the result is legal.

    THE FOUR LEGAL SHAPES, as the project owner stated them on 2026-08-15:
    one two-handed weapon; two one-handed weapons; one one-handed weapon on its
    own; one one-handed weapon with a Shield in the offhand.

    A two-handed weapon uses both hands, so it may not be paired with anything.
    """
    names = (loadout,) if isinstance(loadout, str) else tuple(loadout)
    if not names:
        raise ValueError("a loadout holds at least one item")
    if len(names) > 2:
        raise ValueError(
            f"a character has two hands and this loadout holds {len(names)} "
            f"items: {list(names)}")

    bases = [af.base_named(n) for n in names]
    for base, name in zip(bases, names, strict=True):
        if not isinstance(base, af.WeaponBase):
            raise ValueError(f"{name} is not something a hand can hold")

    hands = sum(b.hands for b in bases)
    if hands > 2:
        raise ValueError(
            f"{list(names)} needs {hands} hands and a character has two")
    # Asked before the position check below, because a loadout of nothing but
    # offhands is wrong in a more fundamental way than one holding them in the
    # wrong order, and the more fundamental complaint is the more useful one.
    if all(n == OFFHAND for n in names):
        raise ValueError(
            f"this loadout holds no weapon, only {OFFHAND}, so it deals no "
            "attack damage at all")
    if len(names) == 2 and OFFHAND in names[:1]:
        raise ValueError(
            f"{OFFHAND} is an offhand, so it goes in the second position")
    return names


def weapons_in(loadout: tuple[str, ...]) -> tuple[str, ...]:
    """The names in a loadout that are weapons rather than the offhand."""
    return tuple(n for n in loadout if n != OFFHAND)


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


def weapon_base_damage(loadout: str | tuple[str, ...],
                       gear_level: int = af.MAX_GEAR_LEVEL) -> float:
    """The flat attack damage a loadout supplies.

    TWO ONE-HANDED WEAPONS SUM, which `docs/Cataclysm_GDD_v2.md` line 2378
    states: "Two one-handed weapons **sum** their base damage, so an Axe and a
    Sword give 86 against a Greatsword's stated 78." Attack speed averages
    instead, at line 2401, and that difference is what stops summed damage
    becoming a strict advantage.

    Asked of each base rather than of each implicit, so a two-handed weapon's
    doubling is applied where the rule lives.
    """
    total = 0.0
    for name in normalise_loadout(loadout):
        base = af.base_named(name)
        two_handed = base.value_multiplier != 1.0
        total += sum(i.value_at(gear_level, two_handed=two_handed)
                     for i in base.implicits
                     if i.stat == "attack_damage" and i.kind == "flat")
    return total


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
    loadout: tuple[str, ...]
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
              loadout: str | tuple[str, ...] = REFERENCE_LOADOUT,
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

    names = normalise_loadout(loadout)
    at = affix_tier_at(tier)
    level = gear_level_at(tier)

    # Only a two-handed weapon doubles the affixes sitting on it. A pair of
    # one-handers holds twice as many weapon affix slots at single value, which
    # `docs/Cataclysm_GDD_v2.md` line 2370 says is deliberately the same worth.
    two_handed = any(af.base_named(n).value_multiplier != 1.0 for n in names)

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
        loadout=names,
        affix_tier=at,
        gear_level=level,
        weapon_damage=weapon_base_damage(names, level),
        flat_from_affixes=flat,
        increased=increased,
        more=more,
        skill_percent=SKILL_SLOTS[skill_slot].typical_damage,
    )


def damage_per_hit(tier: int,
                   loadout: str | tuple[str, ...] = REFERENCE_LOADOUT,
                   flat_affixes: int = af.REFERENCE_FLAT_DAMAGE_AFFIXES,
                   increased_affixes: int = af.REFERENCE_INCREASED_DAMAGE_AFFIXES,
                   skill_slot: str = BASIC_ATTACK_SLOT,
                   more: float = 1.0) -> float:
    """One non-critical hit from the reference character at a difficulty tier."""
    return breakdown(tier, loadout, flat_affixes, increased_affixes,
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


def attack_rate(loadout: str | tuple[str, ...] = REFERENCE_LOADOUT) -> float:
    """Attacks per second a loadout supplies, before any attack speed affix.

    THE AVERAGE OF THE WEAPONS HELD, not the sum and not the slower.
    `docs/Cataclysm_GDD_v2.md` line 2401 states it and says why: it is what stops
    summed base damage becoming a strict advantage, because a dual wielder deals
    more per swing than either weapon alone without also swinging at the faster
    weapon's rate. Read through `affixes.attack_speed_of`, which owns the rule.

    The offhand is left out, because a shield is not swung.
    """
    names = weapons_in(normalise_loadout(loadout))
    if not names:
        raise ValueError("this loadout holds no weapon, so it has no rate")
    return af.attack_speed_of(*[af.base_named(n) for n in names])


def damage_per_second(tier: int,
                      loadout: str | tuple[str, ...] = REFERENCE_LOADOUT,
                      **kwargs: object) -> float:
    """Hit damage times the loadout's swing rate, before any attack speed affix."""
    return damage_per_hit(tier, loadout, **kwargs) * attack_rate(loadout)


def gap_against_target(tier: int,
                       loadout: str | tuple[str, ...] = REFERENCE_LOADOUT,
                       **kwargs: object) -> float:
    """Composed damage divided by what `affixes.damage_target` says is needed.

    One means the two agree. Above one is a loadout that overshoots the content.

    THE TARGET DESCRIBES THE DUAL WIELDER, stated by the project owner on
    2026-08-15. So this reads about 1.0 for two one-handers and about 1.33 for a
    two-hander, and that second figure is not an error: it is the two-handed
    advantage `docs/Cataclysm_GDD_v2.md` line 2382 states.

    THE TARGET APPLIES NO MITIGATION and is wrong for it, which is issue #511 and
    is stated in `damage_target`'s own docstring. Comparing against it is still
    the right check, because every offensive number in `affixes.py` is fitted to
    that same figure.
    """
    return damage_per_hit(tier, loadout, **kwargs) / af.damage_target(tier)


# --------------------------------------------------------------------------
# Checks that run at import
# --------------------------------------------------------------------------

def _check_the_reference_loadout_is_legal_and_armed() -> None:
    names = normalise_loadout(REFERENCE_LOADOUT)
    if weapon_base_damage(names, af.MAX_GEAR_LEVEL) <= 0:
        raise ValueError(
            f"the reference loadout {list(names)} supplies no attack damage, so "
            "it cannot be the loadout a damage figure is composed from")


def _check_the_reference_loadout_reaches_the_target_it_is_declared_to_describe() -> None:
    """The declaration and the arithmetic are both in this file, so compare them.

    The project owner declared on 2026-08-15 that the damage target describes a
    dual wielder. That is a claim this module can check rather than repeat: if
    the reference loadout stops landing near `damage_target`, either a weapon
    base moved or the affix values did, and the declaration has quietly stopped
    being true.

    FIVE PER CENT, because pair sums are whole numbers and the required weapon
    term is 90.03, so nothing lands on it exactly. An Axe with a Sword supplies
    86 and an Axe with an Axe supplies 92, which bracket it at about four per
    cent either side.
    """
    gap = gap_against_target(af.DIFFICULTY_TIERS)
    if abs(gap - 1.0) > 0.05:
        raise ValueError(
            f"the reference loadout {list(REFERENCE_LOADOUT)} deals "
            f"{damage_per_hit(af.DIFFICULTY_TIERS):,.0f} at difficulty tier "
            f"{af.DIFFICULTY_TIERS} against a target of "
            f"{af.damage_target(af.DIFFICULTY_TIERS):,.0f}, which is {gap:.2f}x. "
            "The target is declared to describe this loadout, so they have "
            "drifted apart.")


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


_check_the_reference_loadout_is_legal_and_armed()
_check_the_reference_loadout_reaches_the_target_it_is_declared_to_describe()
_check_the_weapon_cannot_hold_more_damage_affixes_than_it_has_prefixes()
_check_both_damage_affixes_are_prefixes_a_weapon_can_roll()


if __name__ == "__main__":
    def _label(names: tuple[str, ...]) -> str:
        return " + ".join(names)

    print("What the reference character deals per hit. Issue #336.")
    print()
    print(f"  The damage target describes {_label(REFERENCE_LOADOUT)}, two "
          f"one-handed weapons.")
    print(f"  {af.REFERENCE_FLAT_DAMAGE_AFFIXES} flat damage affixes and "
          f"{af.REFERENCE_INCREASED_DAMAGE_AFFIXES} increased, none placed on a "
          f"weapon, matching how the affix values were fitted.")
    print()
    print("  The reference loadout across the eight difficulty tiers:")
    print()
    print(f"    {'tier':<5} {'affix':>6} {'gear':>5} {'weapon':>7} {'flat':>6} "
          f"{'incr':>7} {'per hit':>9} {'target':>8} {'gap':>7}")
    print("    " + "-" * 70)
    for tier in range(1, 9):
        b = breakdown(tier)
        print(f"    T{tier:<4} {b.affix_tier:>6} +{b.gear_level:<4} "
              f"{b.weapon_damage:>7,.0f} {b.flat_from_affixes:>6,.0f} "
              f"{b.increased * 100:>6,.0f}% {b.per_hit:>9,.0f} "
              f"{af.damage_target(tier):>8,.0f} {gap_against_target(tier):>6.2f}x")
    print()

    ONE_HANDERS = [b.name for b in af.ITEM_BASES
                   if isinstance(b, af.WeaponBase) and b.hands == 1
                   and b.name != OFFHAND]
    TWO_HANDERS = [b.name for b in af.ITEM_BASES
                   if isinstance(b, af.WeaponBase) and b.hands == 2]

    LOADOUTS: list[tuple[str, ...]] = (
        [("Axe",), ("Dagger",)]
        + [("Axe", OFFHAND)]
        + [("Axe", "Sword"), ("Axe", "Axe"), ("Dagger", "Dagger")]
        + [(n,) for n in TWO_HANDERS])

    print("  Every shape of loadout at difficulty tier 8, one basic attack:")
    print()
    print(f"    {'loadout':<24} {'kind':<12} {'base':>6} {'per hit':>9} "
          f"{'gap':>7} {'per second':>11}")
    print("    " + "-" * 74)
    for names in LOADOUTS:
        weapons = weapons_in(names)
        if len(names) == 1 and af.base_named(names[0]).hands == 2:
            kind = "two-handed"
        elif OFFHAND in names:
            kind = "one + offhand"
        elif len(weapons) == 2:
            kind = "dual wield"
        else:
            kind = "one-handed"
        print(f"    {_label(names):<24} {kind:<12} "
              f"{weapon_base_damage(names):>6,.0f} "
              f"{damage_per_hit(8, names):>9,.0f} "
              f"{gap_against_target(8, names):>6.2f}x "
              f"{damage_per_second(8, names):>11,.0f}")
    print()
    two = damage_per_hit(8, ("Greatsword",))
    dual = damage_per_hit(8, REFERENCE_LOADOUT)
    print(f"  A Greatsword deals {two / dual:.2f}x what the reference pair deals "
          f"per hit.")
    print("  The design document states 1.33x, and that is the two-handed")
    print("  multiplier working rather than a loadout beating the target.")
