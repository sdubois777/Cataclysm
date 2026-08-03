"""A geared character at a difficulty tier, assembled from the real affix pool.

WHAT THIS IS FOR. Issue #108. Enemy damage only means something against what a
player can actually survive, and until this existed there was no way to say what
that was. Every earlier figure assumed a flat "70% mitigation" rather than
computing one, and was wrong by a wide margin: a geared character has four
mitigation layers that multiply, and takes about a tenth of what is thrown at
them rather than three tenths.

WHY IT IS A MODULE AND NOT A SCRIPT. Two things need it. `enemy_stats.py` has two
constants fitted against it, and `test_survivability.py` measures them. A build
written twice would drift, and the point of this is that the enemy numbers cannot
drift from the mitigation they were fitted against.

WHY IT DOES NOT LIVE IN `enemy_stats.py`. That module must not import
`affixes.py`, because `affixes.py` already imports it -- the damage a player
needs is read off enemy health. Putting the reference build in a third module
that imports both keeps the direction of that dependency intact.

WHAT IT IS NOT. Not the only build, not the best build, and not a claim about
what players will do. It is one honestly-assembled character, stated in full, so
that a statement like "a Common enemy takes 25 hits to kill you" has something
concrete behind it. A build that spent its slots differently would get different
numbers, which is the point of having slots.

IT IS A TWO-HANDED BUILD, and it does not model which piece each affix sits on.
`BASES` ends in a Greatsword, so this is the 18-piece, 72-affix-slot loadout
rather than the dual wielder's 19 and 76. `PREFIX_SPEND` and `SUFFIX_SPEND` are
totals across all eighteen pieces and never say which piece carries which affix,
so the two-handed multiplier that a weapon applies to its own four affixes is
not applied here. That understates this character's damage and changes none of
its defences, because every affix a weapon can roll is offensive. Since what
this module exists to measure is survivability, and the enemy damage constants in
`enemy_stats.py` are fitted against that, the omission does not touch anything
those constants depend on. It would have to be fixed before this build is used
to measure damage.
"""

from __future__ import annotations

from collections import Counter

from . import affixes as af
from . import damage as dmg
from .character import DAMAGE_TYPES, Attributes, Character, Gear
from .classes import DEMONIC_CLASSES

#: How the 36 prefix slots are spent. Two on each of 18 pieces.
#:
#: Split between staying alive and killing things, because a character that spent
#: everything on defence would flatter these numbers and one that spent nothing
#: would make them meaningless.
PREFIX_SPEND: dict[str, int] = {
    "FLAT_HEALTH": 8,
    "INCREASED_HEALTH": 6,
    "FLAT_ARMOR": 6,
    "INCREASED_ARMOR": 4,
    "FLAT_DAMAGE": 6,
    "INCREASED_DAMAGE": 6,
}

#: How the 36 suffix slots are spent, alongside the resistance affixes below.
SUFFIX_SPEND: dict[str, int] = {
    "FLAT_BLOCK_CHANCE": 4,
    "FLAT_DAMAGE_REDUCTION": 4,
    "INCREASED_CRIT_CHANCE": 4,
    "FLAT_CRIT_MULTIPLIER": 4,
    "INCREASED_ATTACK_SPEED": 4,
    "FLAT_PENETRATION": 4,
}

#: All-resistance affixes, which are suffixes too. Twelve is roughly what capping
#: all eight resistances at tier 8 costs, which `affixes.slots_to_cap` reports.
RESISTANCE_AFFIXES = 12

#: One base per piece, chosen the way a defensive character would choose them.
BASES: tuple[str, ...] = (
    "Helm", "Cuirass", "Pauldrons", "Gauntlets", "Greaves", "Sabatons", "Girdle",
) + ("Loop",) * 8 + ("Torc", "Idol", "Greatsword")

#: 60 into Vitality for health, 40 into Constitution for armour and block.
ATTRIBUTES = Attributes(vitality=60, constitution=40)

CLASS_NAME = "Ravager"


def _affix(name: str) -> af.StatAffix:
    return getattr(af, name)


def spent_slots() -> tuple[int, int]:
    """Prefix and suffix slots this build uses."""
    return (sum(PREFIX_SPEND.values()),
            sum(SUFFIX_SPEND.values()) + RESISTANCE_AFFIXES)


def gear(affix_tier: int = 7, gear_level: int = af.MAX_GEAR_LEVEL) -> Gear:
    """Everything the equipment contributes: affixes and base implicits."""
    flat: Counter[str] = Counter()
    increased: Counter[str] = Counter()

    for name, count in {**PREFIX_SPEND, **SUFFIX_SPEND}.items():
        affix = _affix(name)
        value = affix.value_at(affix_tier, gear_level=gear_level) * count
        if affix.kind == "flat":
            flat[affix.stat] += value
        else:
            increased[affix.stat] += value / 100.0

    for base_name in BASES:
        base = af.base_named(base_name)
        # Asked of the BASE rather than of each implicit, because a two-handed
        # weapon doubles its own implicits and reading them directly loses that
        # silently. It changes no number here today -- the Greatsword's only
        # implicit is attack damage, which is popped below -- but it would the
        # moment a two-handed base gained anything else.
        for implicit in base.implicits:
            value = implicit.value_at(gear_level,
                                      two_handed=base.value_multiplier != 1.0)
            if implicit.kind == "flat":
                flat[implicit.stat] += value
            else:
                increased[implicit.stat] += value / 100.0

    per_type = af.ALL_RESISTANCE.value_at(affix_tier, gear_level=gear_level)
    for damage_type in DAMAGE_TYPES:
        flat[f"resistance_{damage_type.lower()}"] += per_type * RESISTANCE_AFFIXES

    # Attack damage is off the character sheet: it belongs to the weapon.
    flat.pop("attack_damage", None)
    increased.pop("attack_damage", None)
    return Gear(flat=dict(flat), increased=dict(increased))


def character(affix_tier: int = 7,
              gear_level: int = af.MAX_GEAR_LEVEL) -> Character:
    return Character(DEMONIC_CLASSES[CLASS_NAME], level=100,
                     attributes=ATTRIBUTES,
                     gear=gear(affix_tier, gear_level))


def defender(tier: int = 8, affix_tier: int = 7,
             gear_level: int = af.MAX_GEAR_LEVEL) -> dmg.Defender:
    """The same character, in the shape the damage model resolves hits against."""
    hero = character(affix_tier, gear_level)
    return dmg.Defender(
        health=hero.stat("max_health"),
        energy_shield=hero.stat("max_energy_shield"),
        armor=hero.stat("armor"),
        evasion=hero.stat("evasion"),
        block_chance=hero.stat("block_chance"),
        damage_reduction=hero.stat("damage_reduction"),
        resistances={d: hero.stat(f"resistance_{d.lower()}")
                     for d in DAMAGE_TYPES},
        crowd_control_resistance=hero.stat("crowd_control_resistance"),
        tier=tier,
    )


def damage_taken_fraction(tier: int = 8) -> float:
    """Share of an average hit that reaches health. About a tenth at tier 8."""
    against = defender(tier)
    probe = dmg.Attacker(damage=1000.0, damage_type="Demonic")
    return dmg.average_damage_taken(probe, against) / 1000.0


def _check_the_build_fits_in_the_slots_it_has() -> None:
    prefixes, suffixes = spent_slots()
    available = af.GEAR_PIECES * af.PREFIXES_PER_PIECE
    if prefixes > available:
        raise ValueError(
            f"the reference build spends {prefixes} prefix slots and a "
            f"character has {available}")
    if suffixes > available:
        raise ValueError(
            f"the reference build spends {suffixes} suffix slots and a "
            f"character has {available}")


def _check_every_affix_is_in_the_position_it_was_filed_under() -> None:
    for name in PREFIX_SPEND:
        if _affix(name).position != af.PREFIX:
            raise ValueError(f"{name} is spent as a prefix and is not one")
    for name in SUFFIX_SPEND:
        if _affix(name).position != af.SUFFIX:
            raise ValueError(f"{name} is spent as a suffix and is not one")


def _check_there_is_one_base_per_equipped_piece() -> None:
    if len(BASES) != af.GEAR_PIECES:
        raise ValueError(
            f"{len(BASES)} bases named for {af.GEAR_PIECES} equipped pieces")
    for base_name in BASES:
        af.base_named(base_name)


_check_the_build_fits_in_the_slots_it_has()
_check_every_affix_is_in_the_position_it_was_filed_under()
_check_there_is_one_base_per_equipped_piece()


if __name__ == "__main__":
    from . import enemy_stats as es

    TIER = 8
    hero = character()
    against = defender(TIER)
    prefixes, suffixes = spent_slots()
    available = af.GEAR_PIECES * af.PREFIXES_PER_PIECE

    print("The reference geared character. Issue #108.")
    print()
    print(f"  A level 100 {CLASS_NAME}, {ATTRIBUTES.vitality} Vitality and "
          f"{ATTRIBUTES.constitution} Constitution,")
    print(f"  spending {prefixes}/{available} prefix slots and "
          f"{suffixes}/{available} suffix slots at T7 on +10 gear.")
    print()
    print("  What it ends up with:")
    for stat in ("max_health", "armor", "block_chance", "damage_reduction",
                 "evasion", "crowd_control_resistance", "resistance_demonic"):
        print(f"    {stat:<26} {hero.stat(stat):>10,.1f}")
    print()
    print(f"    armour reduces             {dmg.armor_reduction(hero.stat('armor'), TIER):>9.1f}%")
    print(f"    resistance after the cap   {min(dmg.RESISTANCE_CAP, hero.stat('resistance_demonic')):>9.1f}%")
    print(f"    SO A HIT LANDS FOR         {damage_taken_fraction(TIER):>9.1%} of itself")
    print()
    print("  Four layers that multiply. That is why enemy damage set without")
    print("  reference to a real character was wrong by a factor of twenty.")
    print()

    print(f"  How long it survives at tier {TIER}:")
    print()
    print(f"    {'enemy':<28} {'its hit':>9} {'lands for':>10} {'hits':>7} {'seconds':>9}")
    print("    " + "-" * 68)
    AT = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
          ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
          ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))
    for name, rarity in AT:
        e = es.stats_on_floor(rarity, TIER, "Cataclysm", kind=name)
        attacker = dmg.Attacker(damage=e.average_damage_per_hit,
                                damage_type=e.damage_type)
        hits = dmg.hits_to_kill(attacker, against)
        print(f"    {e.name:<28} {e.average_damage_per_hit:>9,.0f} "
              f"{dmg.average_damage_taken(attacker, against):>10,.0f} "
              f"{hits:>7.1f} {hits * e.attack_interval:>9.1f}")
    print()

    print("  A Common enemy arrives in a pack, which is the whole point of one:")
    print()
    imp = es.stats_on_floor("Common", TIER, "Cataclysm", kind="Imp")
    per_hit = dmg.average_damage_taken(
        dmg.Attacker(damage=imp.average_damage_per_hit,
                     damage_type=imp.damage_type), against)
    for pack in (1, 5, 10, 20):
        per_second = per_hit * pack / imp.attack_interval
        print(f"    {pack:>2} Imps deal {per_second:>8,.0f} a second   "
              f"dead in {against.health / per_second:>6.1f} seconds")
