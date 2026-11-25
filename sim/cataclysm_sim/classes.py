"""The three Demonic classes, as stat lines.

WHAT THIS IS. `character.py` holds the structure: 33 stats, where each one's base
comes from, and the pipeline. This holds the content for the three classes the
vertical slice needs. Issue #77.

WHAT THE DESIGN GAVE ME. One sentence per class, from the Demonic table in
`docs/Cataclysm_GDD_v2.md`:

    Ravager    Frontline aggressor embodying raw demonic power. Brutal melee
               combat style with devastating strength.
    Ritualist  Summoner and manipulator of demonic forces. Commands demonic
               entities and can possess enemies to turn them against allies.
    Masochist  Converts received damage into buffs and counterattacks. Uses HP
               instead of mana for abilities.

Everything below beyond those sentences is a proposal.

HOW THE THREE FINISHED CLASSES DO IT. The three War trees that exist as data each
commit to three or four stats and ignore the rest, which is what makes them feel
different before a single point is spent:

    Bulwark    health above all, with tree thresholds up to 25,000, plus armor,
               block and retaliation
    Berserker  resource, damage, critical strikes and leech. Almost no armor and
               no evasion at all
    Saboteur   deployables and evasion. No armor, no crit, no leech

So a class is defined as much by what it refuses as by what it takes. Each class
below leaves most of the 33 stats at the default line deliberately.

CLASS RESOURCES ARE NOT DESIGNED HERE. Only the pool size is set. What a resource
does, how it builds and how it decays belongs with the passive trees in #63. The
names and behaviours in the comments are a starting suggestion for that work, not
a decision. Resolve, the one designed resource, runs 0 to 100, which is where the
default comes from.
"""

from __future__ import annotations

from .character import ClassDefinition, Scaling

# --------------------------------------------------------------------------
# Ravager
# --------------------------------------------------------------------------

# "Frontline aggressor embodying raw demonic power. Brutal melee combat style
# with devastating strength."
#
# The problem to solve is that the Berserker already occupies angry melee. It
# wins through critical strikes and leech, and it is deliberately fragile: its
# tree has three mentions of armor and none of evasion.
#
# So the Ravager is not a bigger Berserker. It is the one that cannot be stopped
# rather than the one that hits hardest: armor and flat damage reduction, enough
# leech to stay standing, faster than everyone else so it is always in contact,
# and no evasion or energy shield whatever. Where the Berserker spikes, the
# Ravager grinds.
#
# Suggested resource for #63: builds while dealing melee damage and decays the
# moment it is out of contact, so the class is punished for backing off. That is
# distinct from Fury, which builds on critical hits specifically, and from
# Resolve, which builds from being hit.
RAVAGER = ClassDefinition(
    name="Ravager",
    overrides={
        # Tougher than average but well short of a dedicated tank.
        "max_health": Scaling(base=130.0, per_level=20.0),      # L100: 2,110
        # The defining defensive pair. Armor scales with Constitution; flat
        # damage reduction does not, so it stays small and gear-led.
        "armor": Scaling(base=25.0, per_level=3.5),             # L100: 371
        "damage_reduction": Scaling(base=3.0, per_level=0.05),  # L100: 7.95%
        # Enough sustain to hold a front line without the Berserker's payoff.
        "life_leech": Scaling(base=1.0, per_level=0.02),        # L100: 2.98%
        # Fastest of the three. A frontline aggressor that cannot close is not
        # one, and this is the stat that makes it feel like pressure.
        "movement_speed": Scaling(base=4.6),
        # Stands its ground.
        "crowd_control_resistance": Scaling(base=5.0, per_level=0.15),
        # Melee, so a small mana pool. It has one; it just does not live on it.
        "max_mana": Scaling(base=40.0, per_level=4.0),          # L100: 436
    },
)

# --------------------------------------------------------------------------
# Ritualist
# --------------------------------------------------------------------------

# "Summoner and manipulator of demonic forces. Commands demonic entities and can
# possess enemies to turn them against allies."
#
# The Saboteur already covers deployables, but it deploys objects: traps and
# turrets that sit where they are put. The Ritualist commands things that were
# alive and in some cases still belong to the enemy.
#
# Mechanically it is the caster of the three, and the one class here that gets an
# energy shield. Frailest health, largest mana pool by a wide margin, the only
# meaningful mana regeneration, and slowest on foot. It survives at range and
# behind what it summons, not by being hard to hit -- no armor, no evasion.
#
# Suggested resource for #63: a pool that active demons RESERVE rather than
# spend, so the ceiling is how much the Ritualist can hold under control at once,
# and possessing an enemy costs more than summoning. That is a different shape
# from all three War resources, which build up and are spent.
RITUALIST = ClassDefinition(
    name="Ritualist",
    overrides={
        # Frailest of the three by a clear margin.
        "max_health": Scaling(base=70.0, per_level=10.0),       # L100: 1,060
        # The mana pool is the class. Roughly double the default.
        "max_mana": Scaling(base=90.0, per_level=12.0),         # L100: 1,278
        "mana_regen": Scaling(base=2.0, per_level=0.25),        # L100: 26.75/s
        # The only class here with an energy shield, which is what the project
        # owner's rule points at: give it to the ones that thematically warrant
        # it, such as casters.
        "max_energy_shield": Scaling(base=40.0, per_level=8.0),  # L100: 832
        # A FIFTH OF THE SHIELD ABOVE, so it refills in five seconds at
        # every level rather than in 38 at level 100. Issue #1237. It was
        # 2.0 and 0.20 a level, which is a rate chosen on its own rather
        # than against the pool it fills.
        "energy_shield_regen": Scaling(base=8.0, per_level=1.60),
        # Slowest. It should not want to be where the fighting is.
        "movement_speed": Scaling(base=3.5),
        # Commands groups rather than single targets.
        "area_of_effect": Scaling(base=110.0),
        # A caster's damage comes from spells, so it needs a base to scale.
        "spell_damage": Scaling(base=10.0, per_level=1.5),
        # A larger pool than the default, because the resource is held rather
        # than spent and the ceiling is the point.
        "class_resource": Scaling(base=150.0),
    },
)

# --------------------------------------------------------------------------
# Masochist
# --------------------------------------------------------------------------

# "Converts received damage into buffs and counterattacks. Uses HP instead of
# mana for abilities."
#
# The class keeps a normal mana pool. "Uses HP instead of mana" is delivered by a
# keystone or capstone in its passive tree that converts all mana into added
# health, per the project owner 2026-08-02. Until that node is taken, the
# Masochist is an ordinary mana user, and the conversion is a build choice rather
# than a starting condition. So `max_mana` is left at the default deliberately.
#
# The interesting tension is that this class wants to be hit, which makes the
# usual defences work against it. It has the largest health pool of the three and
# by far the largest regeneration, because for this class health regeneration is
# resource regeneration -- a Masochist that cannot regain health cannot act. It
# has retaliation, low armor, and deliberately zero evasion and zero energy
# shield: evading is missing out, and a shield absorbs the damage the class needs
# to convert.
#
# Suggested resource for #63: builds from damage taken rather than damage dealt,
# and is spent on counterattacks. That is the inverse of Fury and the inverse of
# the Ravager's suggestion.
MASOCHIST = ClassDefinition(
    name="Masochist",
    overrides={
        # Largest pool of the three. It is both the health bar and, after the
        # tree's conversion node, the resource.
        "max_health": Scaling(base=150.0, per_level=24.0),      # L100: 2,526
        # Roughly 1.5% of its own base health per second at level 100, which is
        # what lets health double as a resource.
        "health_regen": Scaling(base=3.0, per_level=0.35),      # L100: 37.65/s
        # The counterattack half of the class identity.
        #
        # A PERCENTAGE OF THE BLOW SINCE ISSUE #1227, NOT A FLAT AMOUNT, and
        # both constants are exactly a tenth of what they were. A flat 158.5
        # could not matter against enemies whose health runs from 3,238 to
        # 40,048 at difficulty tier 8: a build wearing the affix on every
        # piece reached 291 returned per hit, which is 137 hits taken to kill
        # one Boss. As a share of the damage taken it scales with the enemy
        # instead of against it.
        "retaliation": Scaling(base=1.0, per_level=0.15),        # L100: 15.85%
        # Some armor, but far less than the Ravager. It is not trying to reduce
        # what it takes, only to survive it.
        "armor": Scaling(base=5.0, per_level=0.5),              # L100: 54.5
        # Stands and takes it.
        "crowd_control_resistance": Scaling(base=10.0, per_level=0.2),
        # Evasion and energy shield are left at the default of zero on purpose,
        # not by omission. Both work against what the class does.
    },
)

DEMONIC_CLASSES: dict[str, ClassDefinition] = {
    c.name: c for c in (RAVAGER, RITUALIST, MASOCHIST)
}

# --------------------------------------------------------------------------
# The three Death classes, added 2026-11-24 with their passive trees
# --------------------------------------------------------------------------
#
# THE SAME METHOD AS THE DEMONIC THREE ABOVE: each commits to a handful of stats
# and leaves the rest at the default. One more rule, which the Demonic lines met
# by accident and which these meet on purpose: EVERY STAT THE CLASS'S OWN TREE
# RAISES BY AN "INCREASED" PERCENTAGE HAS A BASE HERE OR ON THE DEFAULT LINE.
# An increase multiplies a base, so a node increasing a stat its class has no
# base for is worth nothing to that class -- the fault issue #1105 found in the
# Masochist tree and `test_no_node_is_worth_nothing_to_its_own_class` guards.
#
# THE SHAPES ARE FROM SHIPPED GAMES, THE NUMBERS ARE JUDGEMENTS. Diablo II's
# Necromancer gains 1.5 life and 2 mana a level against the Barbarian's 2 and 1
# (the Basin Wiki's class pages); Path of Exile's Shadow starts with 57 life
# against the Marauder's 66 (poedb.tw's class pages). So a summoner trades health
# for mana, and an assassin starts with less health than a fighter.
# `docs/DECISIONS.md`, 2026-11-24, has the sources.

# "Siphons life essence from enemies, weakening foes while strengthening self."
#
# It fights inside its own 6 metre Siphon, so it stands among enemies: more
# health than the default, a little flat damage reduction and crowd control
# resistance (its tree's Pale Ward, Stolen Strength and Held Close increase
# both), and twice the default health regeneration, because "strengthening self"
# is what the class is. It refuses armor, evasion, energy shield and leech:
# what keeps it alive is taken from the dead and the Siphon, not worn.
SOUL_COLLECTOR = ClassDefinition(
    name="Soul Collector",
    overrides={
        # Between the default and the Ravager.
        "max_health": Scaling(base=115.0, per_level=17.0),      # L100: 1,798
        # Twice the default's rate at every level, short of the Masochist's.
        "health_regen": Scaling(base=2.0, per_level=0.25),      # L100: 26.75/s
        # Pale Ward and Stolen Strength increase it; the Ravager's is 3 + 0.05.
        "damage_reduction": Scaling(base=3.0, per_level=0.04),  # L100: 6.96%
        # Held Close increases it: "A stun stops your kills".
        "crowd_control_resistance": Scaling(base=3.0, per_level=0.08),  # L100: 10.92
        # Kills fill its bar, so it walks to the next pack a little faster.
        "movement_speed": Scaling(base=4.2),
    },
)

# "Summons and commands the restless dead."
#
# The summoner of the Death three, and not a second Ritualist: the Ritualist
# survives at range behind an energy shield, the Necromancer behind bone. So it
# takes armor, a little flat damage reduction and crowd control resistance (its
# Grave limb's Gravekeeper's Hide, Among the Dead, Mantle of Bone, Shielding
# Ranks, Death Ward and Steadfast Among Them increase all three), the summoner's
# larger mana pool, and less health than the default. It refuses evasion, energy
# shield and leech.
NECROMANCER = ClassDefinition(
    name="Necromancer",
    overrides={
        # Diablo II's Necromancer gains three quarters of the Barbarian's life a
        # level; this is between the Ritualist's 10 and the default's 15.
        "max_health": Scaling(base=85.0, per_level=12.0),       # L100: 1,273
        # Half again the default, short of the Ritualist's 90 + 12.
        "max_mana": Scaling(base=70.0, per_level=9.0),          # L100: 961
        "mana_regen": Scaling(base=1.5, per_level=0.18),        # L100: 19.32/s
        # Bone, not plate: well under the Ravager's 25 + 3.5.
        "armor": Scaling(base=15.0, per_level=2.0),             # L100: 213
        "damage_reduction": Scaling(base=2.0, per_level=0.03),  # L100: 4.97%
        "crowd_control_resistance": Scaling(base=5.0, per_level=0.1),  # L100: 14.9
        # It walks behind its army.
        "movement_speed": Scaling(base=3.8),
    },
)

# "High single-target damage and critical hits, low area damage" (the approved
# draft of 2026-11-24), moving from kill to kill.
#
# A larger critical strike multiplier than the default, which its Blade limb
# scales; a smaller area of effect, which is the draft's "low area damage"; less
# health than the default; and faster on foot. It refuses armor, energy shield,
# damage reduction and leech: it is not there when the blow lands.
#
# NO BASE OF EVASION, AND THAT IS DELIBERATE. No class has one -- the gem
# ruling of 2026-11-21 rests on it -- and the Shadow's tree supplies its own:
# Counterstroke grants flat evasion chance, and every other Evasion node is
# reached through it, so Nimble's and Moving Target's increases always have a
# base under them. The tree's summary designed it that way: "the limb grants
# flat evasion chance ... and does not rely on gear".
SHADOW = ClassDefinition(
    name="Shadow",
    overrides={
        # Path of Exile's Shadow starts with 86% of the Marauder's life.
        "max_health": Scaling(base=90.0, per_level=13.0),       # L100: 1,377
        # A tenth more than the default's 150.
        "crit_multiplier": Scaling(base=160.0),
        # A tenth less than the default's 100: "low area damage".
        "area_of_effect": Scaling(base=90.0),
        # Between the default and the Ravager.
        "movement_speed": Scaling(base=4.4),
    },
)

DEATH_CLASSES: dict[str, ClassDefinition] = {
    c.name: c for c in (SOUL_COLLECTOR, NECROMANCER, SHADOW)
}

#: Every class that has a stat line, which is every class `Class Stats` in the
#: design workbook names besides `Default`.
CLASSES: dict[str, ClassDefinition] = {**DEMONIC_CLASSES, **DEATH_CLASSES}


if __name__ == "__main__":
    from .character import Attributes, Character

    SHOWN = ("max_health", "max_mana", "max_energy_shield", "health_regen",
             "mana_regen", "armor", "evasion", "damage_reduction",
             "retaliation", "life_leech", "movement_speed", "spell_damage",
             "crowd_control_resistance", "class_resource")

    print("The three Demonic classes, at level 100, no gear, no points spent.")
    print("Only the stats any of the three overrides are shown; the other 19")
    print("are identical across all of them, at the default line.")
    print()
    names = list(DEMONIC_CLASSES)
    print(f"    {'stat':<26} " + "".join(f"{n:>12}" for n in names))
    print("    " + "-" * (26 + 12 * len(names)))
    for stat in SHOWN:
        row = f"    {stat:<26} "
        for name in names:
            c = Character(DEMONIC_CLASSES[name], level=100)
            row += f"{c.stat(stat):>12,.1f}"
        print(row)
    print()

    print("What each refuses, which is as much of the identity as what it takes:")
    for name, definition in DEMONIC_CLASSES.items():
        c = Character(definition, level=100)
        zero = [s for s in SHOWN if c.stat(s) == 0.0]
        print(f"    {name:<11} {', '.join(zero) if zero else '(nothing)'}")
    print()

    print("At level 100 with every point in Vitality and no gear. Effective")
    print("health is health plus energy shield, since both have to be removed:")
    for name, definition in DEMONIC_CLASSES.items():
        c = Character(definition, level=100,
                      attributes=Attributes(vitality=100))
        health = c.stat("max_health")
        shield = c.stat("max_energy_shield")
        print(f"    {name:<11} {health + shield:>9,.0f} effective health"
              f"   ({health:,.0f} health + {shield:,.0f} shield)")
    print()
    print("Vitality scales health but not energy shield, so the Ritualist gains")
    print("least from that particular allocation. Spirit is its equivalent.")
