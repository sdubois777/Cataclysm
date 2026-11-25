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

IT IS A MASTERFUL, NO-ENCHANTMENT BUILD. That used to go without saying and no
longer does. Rarity is a label for what fills a piece's four slots: a Masterful
piece has four regular affixes and no enchantment, and a Cataclysmic one has four
enchantments and no regular affix at all. Spending 36 prefixes and 36 suffixes
across eighteen pieces therefore describes a full set of Masterful gear.

The design document still says a difficulty tier 8 character is fully
Cataclysmic, which describes a different character entirely -- one with 72
enchantments and no ordinary stats. That disagreement is issue #125, and the
project owner's reading is that a real top build is a mix of the two. It does not
affect what this module measures, because a mix has ordinary stats too and this
build is a concrete, statable one rather than a claim about what players do.

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

# --------------------------------------------------------------------------
# THE RE-FIT FOR ISSUE #2884 (ANALYSIS BRANCH), under the affix ranges of #2883
# --------------------------------------------------------------------------
#
# WHY IT HAD TO MOVE. The approved affix proposal (#2883) puts block chance on two kinds of piece (Shoulders, Belt),
# flat damage reduction on one (Pants) and penetration on one (the weapon), and its whole-number all-resistance affix
# is 40 at tier 7 on +10 gear, so capping all eight resistances at tier 8 (145) costs 3.6 affixes rather than 12.1.
# The build as written spent four affixes each on block, damage reduction and penetration, and twelve on all
# resistances; read literally it reached 140 block, 140 damage reduction and 480 resistance. The proposal's own words:
# "Re-fitting it, and then `enemy_stats.py`, is #2884's."
#
# EVERY CHOICE, each a judgement and each stated so it can be changed:
#   1. Every affix keeps its old slot count where the pieces it can now roll on allow it: min(old count, pieces).
#      All six prefixes keep theirs (8 flat health, 6 increased health, 6 flat armour, 4 increased armour, 6 flat
#      damage, 6 increased damage), as do increased crit chance, crit multiplier and attack speed (4 each).
#      Block 4 -> 2, damage reduction 4 -> 1, penetration 4 -> 1.
#   2. All-resistance affixes are the count that caps all eight resistances at tier 8, the old build's own rule
#      ("Twelve is roughly what capping all eight resistances at tier 8 costs"): ceil(3.625) = 4, 160 per type.
#   3. The build keeps its split between staying alive and killing things. The 13 freed DEFENSIVE suffix slots (2 of
#      block, 3 of damage reduction, 8 of resistance) go on the defensive suffixes that can still take them: the
#      build's own two attributes, Increased Vitality 8 and Increased Constitution 5, in the 60:40 split of its
#      attribute points. The 3 freed OFFENSIVE slots (penetration) go one each on increased crit chance, crit
#      multiplier and attack speed, the build's other offensive suffixes.
#   4. Every affix is placed on a named piece that can roll it, two prefixes and two suffixes a piece, no affix group
#      twice on one piece: `LAYOUT`, checked at import. `PREFIX_SPEND`, `SUFFIX_SPEND` and `RESISTANCE_AFFIXES` are
#      counted from it.
#   5. UNCHANGED, AND STILL A STATED LIMIT: the weapon's own affixes are not doubled for being on a two-handed
#      Greatsword, as before. Every affix it carries here is offensive (flat and increased damage, penetration and
#      attack speed), so no defensive figure depends on it.
# The alternatives measured beside this one, and what each does to the enemy refit, are in
# `sim/analyse_enemy_rebalance_2884.py` and the proposal document.

#: One base per piece, chosen the way a defensive character would choose them.
BASES: tuple[str, ...] = (
    "Helm", "Cuirass", "Pauldrons", "Gauntlets", "Greaves", "Sabatons", "Girdle",
) + ("Loop",) * 8 + ("Torc", "Idol", "Greatsword")

#: Every piece's two prefixes and two suffixes, in `BASES` order. "ALL_RESISTANCE" is the all-resistances family.
LAYOUT: tuple[tuple[str, tuple[str, str], tuple[str, str]], ...] = (
    ("Helm", ("FLAT_HEALTH", "INCREASED_HEALTH"), ("ALL_RESISTANCE", "INCREASED_VITALITY")),
    ("Cuirass", ("FLAT_HEALTH", "INCREASED_HEALTH"), ("ALL_RESISTANCE", "INCREASED_VITALITY")),
    ("Pauldrons", ("FLAT_HEALTH", "FLAT_ARMOR"), ("FLAT_BLOCK_CHANCE", "INCREASED_CONSTITUTION")),
    ("Gauntlets", ("FLAT_DAMAGE", "INCREASED_DAMAGE"), ("INCREASED_CRIT_CHANCE", "INCREASED_ATTACK_SPEED")),
    ("Greaves", ("FLAT_HEALTH", "INCREASED_ARMOR"), ("FLAT_DAMAGE_REDUCTION", "INCREASED_VITALITY")),
    ("Sabatons", ("FLAT_HEALTH", "FLAT_ARMOR"), ("ALL_RESISTANCE", "INCREASED_CONSTITUTION")),
    ("Girdle", ("FLAT_HEALTH", "INCREASED_ARMOR"), ("FLAT_BLOCK_CHANCE", "INCREASED_VITALITY")),
    ("Loop", ("FLAT_HEALTH", "INCREASED_HEALTH"), ("ALL_RESISTANCE", "INCREASED_VITALITY")),
    ("Loop", ("FLAT_HEALTH", "INCREASED_HEALTH"), ("INCREASED_VITALITY", "INCREASED_CONSTITUTION")),
    ("Loop", ("INCREASED_HEALTH", "FLAT_ARMOR"), ("INCREASED_VITALITY", "INCREASED_CONSTITUTION")),
    ("Loop", ("INCREASED_HEALTH", "FLAT_ARMOR"), ("INCREASED_VITALITY", "INCREASED_CONSTITUTION")),
    ("Loop", ("FLAT_ARMOR", "INCREASED_ARMOR"), ("INCREASED_CRIT_CHANCE", "FLAT_CRIT_MULTIPLIER")),
    ("Loop", ("FLAT_ARMOR", "INCREASED_ARMOR"), ("INCREASED_CRIT_CHANCE", "INCREASED_ATTACK_SPEED")),
    ("Loop", ("FLAT_DAMAGE", "INCREASED_DAMAGE"), ("FLAT_CRIT_MULTIPLIER", "INCREASED_ATTACK_SPEED")),
    ("Loop", ("FLAT_DAMAGE", "INCREASED_DAMAGE"), ("INCREASED_CRIT_CHANCE", "FLAT_CRIT_MULTIPLIER")),
    ("Torc", ("FLAT_DAMAGE", "INCREASED_DAMAGE"), ("FLAT_CRIT_MULTIPLIER", "INCREASED_ATTACK_SPEED")),
    ("Idol", ("FLAT_DAMAGE", "INCREASED_DAMAGE"), ("INCREASED_CRIT_CHANCE", "FLAT_CRIT_MULTIPLIER")),
    ("Greatsword", ("FLAT_DAMAGE", "INCREASED_DAMAGE"), ("FLAT_PENETRATION", "INCREASED_ATTACK_SPEED")),
)

#: The name of the all-resistances family in `LAYOUT`.
RESISTANCE_TOKEN = "ALL_RESISTANCE"

#: How the 36 prefix slots are spent, counted from `LAYOUT`. Split between staying alive and killing things, because a
#: character that spent everything on defence would flatter these numbers and one that spent nothing would make them
#: meaningless.
PREFIX_SPEND: dict[str, int] = dict(Counter(p for _, prefixes, _ in LAYOUT for p in prefixes))

#: How the 36 suffix slots are spent, alongside the resistance affixes below, counted from `LAYOUT`.
SUFFIX_SPEND: dict[str, int] = dict(Counter(s for _, _, suffixes in LAYOUT for s in suffixes
                                            if s != RESISTANCE_TOKEN))

#: All-resistance affixes, which are suffixes too: the count that caps all eight at tier 8 (choice 2 above).
RESISTANCE_AFFIXES = sum(1 for _, _, suffixes in LAYOUT for s in suffixes if s == RESISTANCE_TOKEN)

#: What the build spent before the re-fit, kept so the measurement can state the change. Not read by `gear`.
SPEND_BEFORE_2884 = {
    "prefix": {"FLAT_HEALTH": 8, "INCREASED_HEALTH": 6, "FLAT_ARMOR": 6, "INCREASED_ARMOR": 4, "FLAT_DAMAGE": 6,
               "INCREASED_DAMAGE": 6},
    "suffix": {"FLAT_BLOCK_CHANCE": 4, "FLAT_DAMAGE_REDUCTION": 4, "INCREASED_CRIT_CHANCE": 4,
               "FLAT_CRIT_MULTIPLIER": 4, "INCREASED_ATTACK_SPEED": 4, "FLAT_PENETRATION": 4},
    "resistance": 12,
}

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

    # The weapon supplies the attack speed base, which is the whole point of
    # issue #120: without it this build's four increased attack speed suffixes
    # multiply zero and are worth nothing. Read off the equipped weapon rather
    # than stated here, so changing the weapon changes the rate.
    weapons = [af.base_named(n) for n in BASES
               if isinstance(af.base_named(n), af.WeaponBase)]
    if not weapons:
        raise ValueError("the reference build equips no weapon, so it has no "
                         "attack speed base")

    return Gear(flat=dict(flat), increased=dict(increased),
                weapon_base={"attack_speed": af.attack_speed_of(*weapons)})


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


def _layout_affix(name: str):
    return af.ALL_RESISTANCE if name == RESISTANCE_TOKEN else _affix(name)


def _check_every_affix_sits_on_a_piece_that_can_roll_it() -> None:
    """ISSUE #2884. The build used to state totals only, so it could spend four block affixes when block now rolls on
    two kinds of piece. Every affix is now placed on a named piece, and this refuses one that piece cannot roll, a
    prefix in a suffix place or the other way round, and an affix group twice on one piece."""
    if tuple(base for base, _, _ in LAYOUT) != BASES:
        raise ValueError("LAYOUT does not name the pieces in BASES, in BASES order")
    for base_name, prefixes, suffixes in LAYOUT:
        slot = af.base_named(base_name).slot
        groups: list[str] = []
        for position, names in ((af.PREFIX, prefixes), (af.SUFFIX, suffixes)):
            for name in names:
                affix = _layout_affix(name)
                if affix.position != position:
                    raise ValueError(f"{name} sits in a {position} place on the {base_name} and is not a {position}")
                allowed = af.RESISTANCE_SLOTS if name == RESISTANCE_TOKEN else affix.allowed_slots
                if slot not in allowed:
                    raise ValueError(f"{name} sits on the {base_name} ({slot}) and cannot roll there")
                groups += (sorted(af.groups_of(affix, tuple(DAMAGE_TYPES))) if name == RESISTANCE_TOKEN
                           else sorted(af.groups_of(affix)))
        if len(groups) != len(set(groups)):
            raise ValueError(f"the {base_name} repeats an affix group: {sorted(groups)}")


_check_the_build_fits_in_the_slots_it_has()
_check_every_affix_is_in_the_position_it_was_filed_under()
_check_there_is_one_base_per_equipped_piece()
_check_every_affix_sits_on_a_piece_that_can_roll_it()


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
