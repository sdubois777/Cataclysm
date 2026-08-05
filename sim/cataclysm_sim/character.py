"""The character sheet: every stat a class supplies, and the pipeline that
turns class, level, gear and attributes into final values.

WHAT THIS IS. `docs/Cataclysm_GDD_v2.md` gives eight attributes as percentages
per point and says attributes scale values rather than creating them. It never
says what the full stat set is, nor where each stat's starting value comes from.
This module is that structure.

ATTRIBUTES ONLY EVER SCALE. Stated by the project owner 2026-08-02:

    Attributes exist to scale stats you get elsewhere. Somewhere other than
    attributes, every stat is given a base value. This doesn't mean every class
    needs a base above 0 for every stat, just that if you want to scale it using
    attributes, you need a base value first.

So there is exactly one way an attribute point acts: it adds to that stat's sum
of increases, and the sum multiplies the base. `movement_speed` on a class with a
base of 3 metres per second becomes 3 * (1 + increases). A stat with no base
gains nothing from its attribute, and that is the design working rather than
failing.

WHERE EACH BASE VALUE COMES FROM. Three places, recorded per stat in BASE_SOURCE:

    the class    the character's own numbers: vitals, recovery, defences,
                 resistances, movement speed, and the percentages that modify
                 what skills do, such as area of effect
    the weapon   what the equipped weapon is: attack speed, and off this sheet,
                 attack range and attack damage
    the skill    what the ability being used is: critical strike chance, and off
                 this sheet, the base cooldown, projectile count and duration

Critical strike chance is the example the project owner gave: each skill carries
its own base, and gear and attributes scale that. It is not a class number.

Area of effect is not the same case, even though it also concerns a skill. The
character holds a single area of effect percentage that applies to every skill
tagged for area of effect. Its baseline is 100%, not zero, because it is a
percentage of whatever the skill itself does.

INCREASES ARE SCOPED BY TAG. Stated by the project owner 2026-08-02:

    All of our skills have tags, so we know which enchantments and effects apply
    to which skills. If I'm using an AOE skill and I find equipment that
    increases my AOE, that increase should still apply to the skill. But it
    should be a global stat that applies to anything tagged with AOE. The player
    holds all of its own increases, and those increases apply to things with
    matching tags.

So a Modifier carries the tags it requires, a Skill carries the tags it has, and
an increase reaches a skill only when they match. Matching is hierarchical, as
the tag names imply: a modifier requiring `Type.AOE` applies to a skill tagged
`Type.AOE.PointBlank`. The design's tag list already contains `Scope.Global` for
modifiers that should apply to everything.

The Weapon Skills sheet already tags every skill this way, and the enchantment
tables already tag every enchantment.

THREE BUCKETS, NOT TWO.

    Final = (base + flat) * (1 + sum of increases) * more1 * more2 * ...

Everything in the INCREASED bucket adds together first and multiplies once, so
it has diminishing returns: the hundredth point of increase is worth far less
than the first. A MORE multiplier is not in that sum, so it is worth its full
value no matter how much of anything else is already there. A character at +800%
increased who adds another +60% increased gains 6.7%; the same character adding
a 60% more multiplier gains 60%.

That gap is what makes gearing a puzzle rather than a sum, and it is the standard
shape across the genre: Path of Exile and Last Epoch both call these "increased"
and "more", and Torchlight Infinite calls them non-additional and additional.

WHERE EACH BUCKET COMES FROM. Ordinary gear affixes are flat or increased and
never more. More multipliers come from gems, passive tree keystones and
enchantments, which MORE_SOURCES enforces. That keeps a rare drop readable and
gives the 961 designed enchantments a job they did not previously have.

THE DEFAULT LINE AND OVERRIDES. 33 of the 35 stats come from the class -- all but
attack speed, which comes from the weapon, and critical strike chance, which comes
from the skill. Each needs a level 1 base and a per-level gain, so across 24
classes that would be 1,584 numbers.
So every class inherits DEFAULT_STAT_LINE and overrides only the stats that
express its identity. A class may override any stat; the default is a starting
point, not a floor.

WHAT THIS MODULE DOES NOT DO. It sets no per-class values. DEFAULT_STAT_LINE is
deliberately the only stat line here. Ravager, Ritualist and Masochist are issue
#77, and their values need reviewing on their own terms rather than arriving as a
side effect of building the structure.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from . import damage

MAX_LEVEL = 100

#: One resistance per damage type, in the order the design document lists them.
DAMAGE_TYPES = ("War", "Demonic", "Death", "Pestilence",
                "Famine", "Celestial", "Chaos", "Void")

RESISTANCE_STATS = tuple(f"resistance_{d.lower()}" for d in DAMAGE_TYPES)

#: The offensive mirror of the eight resistances: increased damage dealt to an
#: enemy whose own damage type is that one. Each is a percentage that joins the
#: increases bracket, and it applies only against that type of enemy.
#:
#: WHY THERE ARE EIGHT AND NOT ONE. The design document says an enemy has a
#: damage type of its own, which is its Cataclysm's, and that a run starts with
#: one Cataclysm active and gains one more each time a Cataclysm is defeated. So
#: which of these eight is worth anything changes over a campaign, in exactly
#: the way the three resistance breadth families already do. Issue #213.
#:
#: THESE ARE ON THE SHEET, unlike `attack_damage`. The rule for keeping a stat
#: off the sheet is that it belongs to the equipped weapon rather than to the
#: character. This belongs to the character: it reads the target, not the
#: weapon, so how many damage types the weapon carries does not touch it.
DAMAGE_VS_STATS = tuple(f"damage_vs_{d.lower()}" for d in DAMAGE_TYPES)

#: The character sheet, grouped the way `game/Config/Tags/CataclysmTags.ini`
#: groups its Stat.* tags.
STAT_GROUPS: dict[str, tuple[str, ...]] = {
    "Resource": ("max_health", "max_mana", "max_energy_shield", "class_resource"),
    "Recovery": ("health_regen", "mana_regen", "energy_shield_regen",
                 "life_leech", "mana_leech", "energy_shield_leech"),
    "Defense": ("armor", "evasion", "block_chance", "damage_reduction",
                "retaliation", "crowd_control_resistance") + RESISTANCE_STATS,
    "Offense": ("crit_chance", "crit_multiplier", "attack_speed",
                "area_of_effect", "dot_damage", "dot_frequency",
                "dot_duration", "penetration",
                "spell_damage") + DAMAGE_VS_STATS,
    "Utility": ("movement_speed", "cooldown_reduction", "magic_find",
                "loot_quantity"),
}

ALL_STATS: tuple[str, ...] = tuple(s for g in STAT_GROUPS.values() for s in g)

#: Where each stat's base value comes from. Every stat on the sheet has exactly
#: one source. Anything not named here comes from the class.
#:
#: The weapon and skill entries are a proposal and are the part of this most
#: likely to be wrong. Critical strike chance is certain -- the project owner
#: stated it. Area of effect and damage-over-time frequency are placed with the
#: skill by the same reasoning: a skill has a radius and a tick rate, and a
#: character does not have those in the abstract.
_NON_CLASS_BASE: dict[str, str] = {
    "attack_speed": "weapon",
    "crit_chance": "skill",
}

BASE_SOURCE: dict[str, str] = {
    stat: _NON_CLASS_BASE.get(stat, "class") for stat in ALL_STATS
}

WEAPON_BASE_STATS = frozenset(
    s for s, src in BASE_SOURCE.items() if src == "weapon")
SKILL_BASE_STATS = frozenset(
    s for s, src in BASE_SOURCE.items() if src == "skill")
CLASS_BASE_STATS = frozenset(
    s for s, src in BASE_SOURCE.items() if src == "class")

#: The base critical strike chance every skill has unless it states its own, as
#: a percentage. Issue #120.
#:
#: 5% is Path of Exile's base for a plain melee weapon; its daggers and staves
#: sit at 6 to 6.5% and its wands at 7 to 8%, so a skill that wants to crit more
#: overrides upward from here rather than this being a floor for everything. It
#: is also the value this project already gives an ordinary enemy, in
#: `enemy_stats.EnemyStats.crit_chance`, so the player and the enemies start from
#: the same place.
DEFAULT_SKILL_CRIT_CHANCE = 5.0

#: Stats that are counted as a percentage and cannot exceed their cap no matter
#: what. Only hard caps appear here; soft caps are exceedable by design and so
#: are deliberately not clamped. See the Stat Calculation section of the design
#: document.
HARD_CAPS: dict[str, float] = {"crit_chance": 100.0}

#: Recorded so the soft caps are not lost, but NOT applied. Affixes may exceed
#: resistances' 70% and evasion's 60%.
#:
#: The resistance figure comes from `damage.py`, which owns it because that is
#: where it is applied, rather than being written out again here. Issue #228.
SOFT_CAPS: dict[str, float] = dict(
    {"evasion": 60.0},
    **{s: damage.RESISTANCE_CAP for s in RESISTANCE_STATS})

#: Stats that measure an interval, where an increase makes the interval shorter.
#: These divide rather than multiply: Final = Base / (1 + increases), which is
#: what stops a cooldown ever reaching zero.
#:
#: Damage-over-time frequency is deliberately NOT here. The design document says
#: it "uses the same form", and it does -- but the thing that form applies to is
#: the interval between ticks, and this sheet stores the frequency. Multiplying a
#: frequency by (1 + increases) and dividing an interval by (1 + increases) are
#: the same operation seen from either end. Putting the frequency in this set
#: would make Efficacy reduce the number of ticks, which is backwards.
RATE_STATS = frozenset({"cooldown_reduction"})


# --------------------------------------------------------------------------
# Which attribute scales which stat
# --------------------------------------------------------------------------

#: Straight from the attribute table. Each entry is the increase, as a fraction,
#: that one point of that attribute adds to that stat's sum of increases.
#:
#: Every one of these is an INCREASE, not a flat addition, because the design
#: document says attributes scale values rather than creating them. That has a
#: consequence worth stating: a class whose base for a stat is zero gets nothing
#: from the attribute that scales it. A class with no base evasion gains no
#: evasion from Agility. See the note in the module docstring for issue #77.
ATTRIBUTE_EFFECTS: dict[str, dict[str, float]] = {
    "agility":      {"movement_speed": 0.02, "evasion": 0.005},
    "ferocity":     {"crit_chance": 0.005, "crit_multiplier": 0.05},
    "constitution": {"armor": 0.02, "block_chance": 0.01},
    "vitality":     {"max_health": 0.02, "health_regen": 0.01},
    "mind":         {"max_mana": 0.02, "mana_regen": 0.01},
    "spirit":       {"max_energy_shield": 0.02, "energy_shield_regen": 0.01},
    "efficacy":     {"cooldown_reduction": 0.01, "area_of_effect": 0.02,
                     "dot_frequency": 0.01},
    "luck":         {"magic_find": 0.0001, "loot_quantity": 0.01},
}

ATTRIBUTE_NAMES: tuple[str, ...] = tuple(ATTRIBUTE_EFFECTS)


# --------------------------------------------------------------------------
# A stat's class-supplied value
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Scaling:
    """One stat's level 1 base and its gain per level.

    Per-level gain is linear. Whether it should stay linear is undecided and
    needs experimenting rather than a decision; keeping it in one place here is
    what makes the shape cheap to change later.
    """

    base: float = 0.0
    per_level: float = 0.0

    def at(self, level: int) -> float:
        if not 1 <= level <= MAX_LEVEL:
            raise ValueError(f"level {level} outside 1-{MAX_LEVEL}")
        return self.base + self.per_level * (level - 1)


#: What a class inherits before it overrides anything. Deliberately plain: a
#: character with no class identity at all. Real classes are issue #77.
#:
#: Zero here means "this stat does not exist for you until gear or passives give
#: it to you", which is the right default for the defensive and utility stats. A
#: class that wants Agility's evasion half to do anything must override evasion.
DEFAULT_STAT_LINE: dict[str, Scaling] = {
    # Resources
    "max_health": Scaling(base=100.0, per_level=15.0),
    "max_mana": Scaling(base=50.0, per_level=6.0),
    "max_energy_shield": Scaling(),
    "class_resource": Scaling(base=100.0),
    # Recovery
    "health_regen": Scaling(base=1.0, per_level=0.15),
    "mana_regen": Scaling(base=1.0, per_level=0.10),
    "energy_shield_regen": Scaling(),
    "life_leech": Scaling(),
    "mana_leech": Scaling(),
    "energy_shield_leech": Scaling(),
    # Defence
    "armor": Scaling(),
    "evasion": Scaling(),
    "block_chance": Scaling(),
    "damage_reduction": Scaling(),
    "retaliation": Scaling(),
    "crowd_control_resistance": Scaling(),
    **{s: Scaling() for s in RESISTANCE_STATS},
    # Offence. Critical strike chance has no class entry that matters: its base
    # comes from the skill. The zero here is a placeholder never read.
    "crit_chance": Scaling(),
    "crit_multiplier": Scaling(base=150.0),
    "attack_speed": Scaling(),
    # Area of effect and the three damage-over-time levers are percentages of
    # whatever the skill or the effect itself does, so their baseline is 100%,
    # not zero. A class that is naturally better at any of them starts above 100.
    #
    # THE THREE LEVERS ARE THE THREE NUMBERS A DAMAGE-OVER-TIME EFFECT HAS. The
    # design document says such an effect deals a fixed amount per tick, so its
    # total is damage per tick x ticks per second x seconds. Each of those is
    # scalable on its own and all three multiply. Issues #205 and #220.
    "area_of_effect": Scaling(base=100.0),
    "dot_damage": Scaling(base=100.0),
    "dot_frequency": Scaling(base=100.0),
    "dot_duration": Scaling(base=100.0),
    "penetration": Scaling(),
    "spell_damage": Scaling(),
    # Increased damage against one type of enemy. Zero for every class: no class
    # is born better against a Cataclysm, because which Cataclysms a run faces
    # is drawn at run start and the class is chosen before that.
    **{s: Scaling() for s in DAMAGE_VS_STATS},
    # Utility. Movement speed is in metres per second, following the project
    # owner's example of a tank at about 3.
    "movement_speed": Scaling(base=4.0),
    "cooldown_reduction": Scaling(),
    # Magic find is an added percentage, so zero is right: it has a FLAT source,
    # the "Flat magic find" affix, which is what Luck then scales.
    "magic_find": Scaling(),
    # Loot quantity is a percentage of whatever the dungeon would otherwise
    # drop, so its baseline is 100% rather than zero, for the same reason area
    # of effect and damage-over-time frequency have one. Issue #243. Every
    # source of loot quantity is an INCREASE -- the Luck attribute, the
    # "Increased loot quantity" affix, its hybrid, and several empire tree
    # nodes -- and not one of them is flat, so a base of zero left the stat
    # permanently at zero however much was spent on it.
    "loot_quantity": Scaling(base=100.0),
}


@dataclass(frozen=True)
class ClassDefinition:
    """A class is the default stat line plus whatever it overrides."""

    name: str
    overrides: dict[str, Scaling] = field(default_factory=dict)
    #: Set when the class pays for abilities with health rather than mana. It
    #: does NOT mean the class has no mana. The Masochist's design converts mana
    #: into health through a passive tree node; the pool exists until then.
    spends_health: bool = False

    def __post_init__(self) -> None:
        unknown = set(self.overrides) - set(ALL_STATS)
        if unknown:
            raise ValueError(
                f"{self.name} overrides stats that are not on the character "
                f"sheet: {sorted(unknown)}")

    def scaling(self, stat: str) -> Scaling:
        if stat not in DEFAULT_STAT_LINE:
            raise KeyError(f"{stat} is not on the character sheet")
        return self.overrides.get(stat, DEFAULT_STAT_LINE[stat])

    def base_at(self, stat: str, level: int) -> float:
        return self.scaling(stat).at(level)


#: The stat line with nothing overridden. Useful on its own for the 21 classes
#: that have no design yet.
GENERIC = ClassDefinition(name="Generic")


# --------------------------------------------------------------------------
# Gear, attributes, and the character
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Attributes:
    """Points spent. One point per level, spread across the eight attributes."""

    agility: int = 0
    ferocity: int = 0
    constitution: int = 0
    vitality: int = 0
    mind: int = 0
    spirit: int = 0
    efficacy: int = 0
    luck: int = 0

    def total(self) -> int:
        return sum(getattr(self, n) for n in ATTRIBUTE_NAMES)

    def increases_for(self, stat: str) -> float:
        """Sum of increases this allocation contributes to one stat."""
        return sum(getattr(self, attribute) * effects[stat]
                   for attribute, effects in ATTRIBUTE_EFFECTS.items()
                   if stat in effects)


#: A modifier requiring this tag applies to everything. The design's tag list
#: already carries it.
GLOBAL_SCOPE_TAG = "Scope.Global"


def tag_matches(required: str, tags: frozenset[str]) -> bool:
    """Whether a required tag is satisfied by a set of tags.

    Hierarchical, the way the design's tag names are built: a modifier requiring
    `Type.AOE` is satisfied by a skill tagged `Type.AOE.PointBlank`. A modifier
    requiring `Scope.Global` is satisfied by anything.
    """
    if required == GLOBAL_SCOPE_TAG:
        return True
    return any(t == required or t.startswith(required + ".") for t in tags)


@dataclass(frozen=True)
class Modifier:
    """One increase the character carries, and what it applies to.

    Stated by the project owner 2026-08-02:

        The player holds all of its own increases, and those increases apply to
        things with matching tags.

    So an item granting increased area of effect is not a property of any one
    skill. The character holds it, and it applies to every skill tagged for area
    of effect. `requires` empty means it applies to everything.
    """

    stat: str
    increase: float
    requires: frozenset[str] = frozenset()

    def __post_init__(self) -> None:
        if self.stat not in ALL_STATS:
            raise ValueError(
                f"modifier names {self.stat}, which is not on the character sheet")

    def applies_to(self, tags: frozenset[str]) -> bool:
        return all(tag_matches(r, tags) for r in self.requires)


#: Where a MORE multiplier is allowed to come from. Ordinary gear affixes are
#: deliberately absent: an affix enters the flat bracket or the increased bucket,
#: never this one.
#:
#: Stated by the project owner 2026-08-03, after looking at how Path of Exile,
#: Last Epoch and Torchlight Infinite split their damage calculations. Keeping
#: the multiplicative sources on gems, keystones and enchantments means a rare
#: drop stays readable, and it gives the 961 designed enchantments a job they did
#: not previously have.
MORE_SOURCES = frozenset({"gem", "keystone", "enchantment"})


@dataclass(frozen=True)
class More:
    """One multiplicative source. Each multiplies on its own.

    This is the bucket that makes gearing a puzzle rather than a sum. Everything
    in the increased bucket adds together first and then multiplies once, so the
    hundredth point of increase is worth far less than the first. A more
    multiplier is not in that sum, so it is worth its full value no matter how
    much of anything else is already there.

    A character at +800% increased who adds another +60% increased gains 6.7%. A
    character at +800% increased who adds a 60% more gains 60%. The question a
    player is answering is therefore "which independent multiplier am I missing",
    not "what is the biggest number on this item".

    `more` is a fraction: 0.20 is a 20% more multiplier, giving x1.20. Negative
    values are legal and are the "less" case.
    """

    source: str
    stat: str
    more: float
    requires: frozenset[str] = frozenset()

    def __post_init__(self) -> None:
        if self.source not in MORE_SOURCES:
            raise ValueError(
                f"a more multiplier from {self.source!r}; expected one of "
                f"{sorted(MORE_SOURCES)}. Ordinary gear affixes cannot grant "
                "one: they are flat or increased.")
        if self.stat not in ALL_STATS:
            raise ValueError(
                f"more multiplier names {self.stat}, which is not on the "
                "character sheet")
        if self.more <= -1.0:
            raise ValueError(
                f"{self.stat} more multiplier of {self.more} would zero or "
                "invert the stat; a less multiplier cannot reach -100%")

    @property
    def factor(self) -> float:
        return 1.0 + self.more

    def applies_to(self, tags: frozenset[str]) -> bool:
        return all(tag_matches(r, tags) for r in self.requires)


@dataclass(frozen=True)
class Gear:
    """What equipment contributes, by where it enters the pipeline.

    `flat` is added to the class base before scaling. `increased` joins the same
    sum the attribute points do and applies to everything. `modifiers` are
    increases scoped to a tag, applying only to skills that carry it.
    `weapon_base` supplies the base for the stats the weapon owns.
    """

    flat: dict[str, float] = field(default_factory=dict)
    increased: dict[str, float] = field(default_factory=dict)
    weapon_base: dict[str, float] = field(default_factory=dict)
    modifiers: tuple[Modifier, ...] = ()

    def __post_init__(self) -> None:
        for label, table in (("flat", self.flat), ("increased", self.increased),
                             ("weapon_base", self.weapon_base)):
            unknown = set(table) - set(ALL_STATS)
            if unknown:
                raise ValueError(
                    f"gear {label} names stats that are not on the character "
                    f"sheet: {sorted(unknown)}")


# --------------------------------------------------------------------------
# What a skill is worth, in weapon damage
# --------------------------------------------------------------------------
#
# ISSUE #107. The design says every weapon type paired with every damage type
# produces a set of skills, and never said what any of them was worth. So every
# weapon damage figure in this project was really weapon-and-skill together, and
# the two differed by however much a skill multiplied.
#
# THE CONCEPT WAS ALREADY IN THE DATA, unsystematically. Four of the 61 designed
# skills in `game/Data/WeaponSkills.csv` state a multiplier in prose:
#
#     Skull Splitter   "dealing 500% weapon damage to a single target"
#     Annihilator      "the final hit ... deals 300% weapon damage"
#     Bulwark          "bonus damage up to a cap of 200% weapon damage"
#     Haymaker         "they take an additional 100% weapon damage"
#
# So skills multiply weapon damage, the design already says so, and the Ultimate
# band below is set by the two Ultimates among them rather than invented.
#
# THE BASIC ATTACK IS 100% BY DEFINITION, and that is what makes this cost
# nothing. Every damage figure fitted so far -- the tier 8 target of 1,681, the
# affix values, what a weapon must supply -- was fitted to an ordinary hit, which
# is the basic attack. Anchoring the scale there leaves all of it standing and
# lets the other slots multiply from it.
#
# WEAPON DAMAGE MEANS THE WHOLE BASE BRACKET: the weapon plus flat added damage
# from gear. That is what a player reads "500% weapon damage" to mean, and it is
# why flat added damage affixes are worth taking at all.

BASIC_ATTACK_SLOT = "Basic"


@dataclass(frozen=True)
class SkillSlot:
    """One of the seven slots: what a skill in it is worth, and what it costs.

    A band rather than a single number, because skills in a slot vary: the design
    already has one Ultimate at 300% and another at 500%. `typical_damage` is
    what a skill in this slot deals when it does not state its own, and
    `typical_cooldown` is how long before it can be used again.

    COOLDOWN AND COST LIVE HERE RATHER THAN ON THE SKILL SHEET, for the same
    reason the damage multiplier does. No designed skill states either one, so a
    column on the sheet would be 77 copies of six values. A skill that wants to
    differ says so in its own description, which is exactly how Skull Splitter
    differs on damage.

    MANA COST IS A FLAT NUMBER OF MANA, the same number for every class. It is
    quoted at level 100, which is the level every other figure in this project
    is quoted at, and it scales down with character level. See MANA_COST_LEVEL
    and mana_cost_of.
    """

    name: str
    typical_damage: float
    lowest: float
    highest: float
    note: str
    #: Seconds before the skill can be used again, before any reduction. Zero
    #: means the slot has no cooldown at all rather than an instant one.
    typical_cooldown: float = 0.0
    cooldown_lowest: float = 0.0
    cooldown_highest: float = 0.0
    #: Mana one use costs at level 100. For the Aura this is per second while
    #: the toggle is on, matching how its damage is also per second.
    mana_cost: float = 0.0

    def __post_init__(self) -> None:
        if not self.lowest <= self.typical_damage <= self.highest:
            raise ValueError(
                f"{self.name}: typical {self.typical_damage} is outside its "
                f"band of {self.lowest} to {self.highest}")
        if not self.cooldown_lowest <= self.typical_cooldown <= self.cooldown_highest:
            raise ValueError(
                f"{self.name}: typical cooldown {self.typical_cooldown}s is "
                f"outside its band of {self.cooldown_lowest} to "
                f"{self.cooldown_highest}")
        if self.mana_cost < 0.0:
            raise ValueError(
                f"{self.name}: mana cost {self.mana_cost} is negative")


#: The seven slots from the design document's Skill Slots table, in the order it
#: lists them. Six are chosen by the player; the Basic Attack is automatic.
# --------------------------------------------------------------------------
# COOLDOWNS AND MANA COSTS. Issue #155.
#
# Neither existed anywhere. The design document defines a cooldown reduction
# formula, an attribute that scales it, an affix that grants it and 41
# enchantments that mention it -- and no skill supplied a base cooldown for any
# of that to divide. That is the same shape as issue #120, where attack speed
# and critical strike chance were scaled from a base of zero.
#
# WHERE THE BASE BELONGS WAS ALREADY SETTLED. The design document's stat source
# table says "The skill being used | Critical strike chance, and off this sheet,
# the base cooldown, projectile count and duration". So this is the design
# becoming real rather than a change to it.
#
# THE COOLDOWNS WERE SET BY THE PROJECT OWNER on 2026-08-04, who judged an
# earlier set anchored on Diablo 4 too long to play. Diablo 4 remains the only
# reference game that gates skills by cooldown per slot the way this design
# does, but its numbers assume a resource system this design does not use, so
# they set the shape and not the values. Movement kept its 5 seconds.
#
# MANA COST IS A FLAT NUMBER OF MANA, the same for every class, quoted at level
# 100 and scaled down with character level. Also the project owner's call: a cost
# expressed as a share of the player's own pool "just feels bad".
#
# WHY IT STILL SCALES WITH LEVEL rather than being one fixed number forever.
# Nothing in this project raises a skill's cost the way a gem level does in Path
# of Exile, and a mana pool runs from 40 at level 1 to 436 at level 100 for a
# Ravager. A cost that did not move would be crippling at level 1 and beneath
# notice at level 100. It rides the default mana progression, so the ratio a
# player experiences is the same at both ends, and the number on the tooltip is
# still a flat quantity of mana.
#
# IT IS THE SAME NUMBER FOR EVERY CLASS, which is what makes the Ritualist's
# large pool and large regeneration worth having: it buys more casts of the same
# skill rather than paying a proportionally larger price for each one. Every
# source of maximum mana -- the Mind attribute, two affixes and a hybrid -- is
# pure gain for the same reason.
#
# THE BASIC ATTACK RESTORES MANA ON HIT, AND THIS IS NOT A GENERATOR. What makes
# the Diablo 4 pattern tiresome, in its players' own words, is casting a weak
# skill roughly five times to afford one real one. Two things here prevent that.
# The basic attack is automatic, so there is no button to press and no rotation
# to perform; it is income for being in a fight. And the Heavy Attack, the
# primary damage button, is affordable from mana regeneration alone with no hits
# landing at all -- see the guard below. Mana on hit pays for everything else, so
# it is a supplement rather than the source. Path of Exile treats mana on hit as
# ordinary sustain for exactly this reason.
# --------------------------------------------------------------------------

#: Costs are quoted at this level, as every other figure in this project is.
MANA_COST_LEVEL = 100

#: Mana the automatic basic attack restores per hit, at level 100. Scales with
#: level alongside the costs.
BASIC_ATTACK_MANA_ON_HIT = 6.0

SKILL_SLOTS: dict[str, SkillSlot] = {
    s.name: s for s in (
        SkillSlot(BASIC_ATTACK_SLOT, 100.0, 100.0, 100.0,
                  "Automatic and free. It IS weapon damage, which is what makes "
                  "it the anchor every other slot is measured against.",
                  typical_cooldown=0.0, cooldown_lowest=0.0,
                  cooldown_highest=0.0, mana_cost=0.0),
        SkillSlot("Heavy", 250.0, 175.0, 350.0,
                  "The design calls it often the primary damage button, on a "
                  "moderate cooldown.",
                  typical_cooldown=1.5, cooldown_lowest=1.0,
                  cooldown_highest=4.0, mana_cost=15.0),
        SkillSlot("Special", 150.0, 100.0, 250.0,
                  "Traps, deployables, grenades, pets. The most varied slot, so "
                  "the widest band below its typical value.",
                  typical_cooldown=5.0, cooldown_lowest=3.0,
                  cooldown_highest=10.0, mana_cost=40.0),
        SkillSlot("Support", 0.0, 0.0, 100.0,
                  "Buffs, shields, stances, curses, banners. Usually no damage "
                  "at all, which is why its typical value is zero.",
                  typical_cooldown=4.0, cooldown_lowest=2.0,
                  cooldown_highest=10.0, mana_cost=25.0),
        SkillSlot("Aura", 25.0, 15.0, 40.0,
                  "Persistent and toggled, draining resource per second. This "
                  "is per second rather than per use.",
                  typical_cooldown=0.0, cooldown_lowest=0.0,
                  cooldown_highest=0.0, mana_cost=20.0),
        SkillSlot("Ultimate", 400.0, 300.0, 500.0,
                  "The band is the two designed Ultimates: Annihilator states "
                  "300% and Skull Splitter states 500%.",
                  typical_cooldown=20.0, cooldown_lowest=12.0,
                  cooldown_highest=40.0, mana_cost=150.0),
        SkillSlot("Movement", 100.0, 75.0, 150.0,
                  "Gap closers and escapes. The design says some also deal "
                  "damage, so a basic attack's worth is the right middle.",
                  typical_cooldown=5.0, cooldown_lowest=3.0,
                  cooldown_highest=10.0, mana_cost=20.0),
    )
}

#: The two slots that have no cooldown, and why each has none. The Basic Attack
#: is automatic, so its rate is the weapon's attack speed. The Aura is a toggle,
#: so there is nothing to wait for; it is paid for by draining mana instead.
SLOTS_WITHOUT_A_COOLDOWN = frozenset({BASIC_ATTACK_SLOT, "Aura"})

#: The six a player chooses between. The Basic Attack is not one of them: the
#: design says it is handled automatically.
CHOSEN_SKILL_SLOTS = tuple(s for s in SKILL_SLOTS if s != BASIC_ATTACK_SLOT)


def _check_the_slots_match_the_design_document() -> None:
    """`Cataclysm_GDD_v2.md` lists seven rows in its Skill Slots table and says
    a player has six slots, the Basic Attack being automatic. Both have to hold,
    or this table has drifted from the design it came from."""
    designed = {BASIC_ATTACK_SLOT, "Heavy", "Special", "Support", "Aura",
                "Ultimate", "Movement"}
    if set(SKILL_SLOTS) != designed:
        raise ValueError(
            f"skill slots have drifted from the design: "
            f"missing {sorted(designed - set(SKILL_SLOTS))}, "
            f"unexpected {sorted(set(SKILL_SLOTS) - designed)}")
    if len(CHOSEN_SKILL_SLOTS) != 6:
        raise ValueError(
            f"{len(CHOSEN_SKILL_SLOTS)} slots a player chooses between; the "
            "design says six")


def _check_the_basic_attack_is_the_anchor() -> None:
    """Every damage figure in this project was fitted to an ordinary hit. If the
    basic attack stopped being exactly 100% of weapon damage, all of them would
    silently mean something else."""
    basic = SKILL_SLOTS[BASIC_ATTACK_SLOT]
    if not basic.lowest == basic.typical_damage == basic.highest == 100.0:
        raise ValueError(
            f"the basic attack is {basic.typical_damage}% of weapon damage and "
            "must be exactly 100%, because it is what everything else is "
            "measured against")


def _check_only_the_toggle_and_the_automatic_slot_lack_a_cooldown() -> None:
    """A cooldown of zero has to be deliberate, because it is also what a
    forgotten one looks like.

    Issue #155 was exactly this failure: every slot read zero, an attribute and
    41 enchantments scaled a number that did not exist, and nothing reported it.
    Only the Basic Attack and the Aura may read zero, and both must, or the
    reason recorded in SLOTS_WITHOUT_A_COOLDOWN no longer matches the table.
    """
    free = {n for n, s in SKILL_SLOTS.items() if s.typical_cooldown == 0.0}
    if free != set(SLOTS_WITHOUT_A_COOLDOWN):
        raise ValueError(
            f"slots with no cooldown are {sorted(free)}; the design says only "
            f"{sorted(SLOTS_WITHOUT_A_COOLDOWN)} have none. The Basic Attack is "
            "automatic and the Aura is a toggle; every other slot waits.")


def _check_every_slot_a_player_chooses_costs_something() -> None:
    """A skill that costs nothing and waits for nothing is unlimited.

    The Basic Attack is free by design -- the design document calls it
    "automatic and free". Each of the six a player chooses has to be limited by
    a cooldown, a mana cost, or both, or there is no reason not to hold it down.
    """
    unlimited = [n for n in CHOSEN_SKILL_SLOTS
                 if SKILL_SLOTS[n].typical_cooldown == 0.0
                 and SKILL_SLOTS[n].mana_cost == 0.0]
    if unlimited:
        raise ValueError(
            f"{sorted(unlimited)} cost nothing and have no cooldown, so "
            "nothing limits how often they are used")


def _check_the_primary_damage_button_needs_no_basic_attacks() -> None:
    """This is what keeps mana on hit from becoming a generator.

    The complaint against the Diablo 4 pattern is having to cast a weak skill
    about five times to afford one real one. The rule that prevents it here is
    that the Heavy Attack, which the design calls often the primary damage
    button, is affordable from mana regeneration alone with no hits landing.
    Mana on hit then pays for the other slots and is a supplement rather than
    the source.

    Checked against the WORST case, the class with the smallest regeneration
    relative to its costs. Costs are the same for every class, so whichever
    class has the least mana regeneration is the binding one.
    """
    heavy = SKILL_SLOTS["Heavy"]
    if heavy.typical_cooldown <= 0.0:
        raise ValueError("the Heavy Attack has no cooldown to spend mana over")
    spend = heavy.mana_cost / heavy.typical_cooldown
    regen = DEFAULT_STAT_LINE["mana_regen"].at(MANA_COST_LEVEL)
    if spend > regen:
        raise ValueError(
            f"the Heavy Attack costs {spend:.1f} mana/s used on cooldown "
            f"against {regen:.1f}/s of default regeneration, so the primary "
            "damage button cannot be sustained without landing basic attacks. "
            "That is the generator pattern this design avoids.")


_check_the_slots_match_the_design_document()
_check_the_basic_attack_is_the_anchor()
_check_only_the_toggle_and_the_automatic_slot_lack_a_cooldown()
_check_every_slot_a_player_chooses_costs_something()
_check_the_primary_damage_button_needs_no_basic_attacks()


@dataclass(frozen=True)
class Skill:
    """The ability being used, which supplies the base for some stats.

    A character has no critical strike chance in the abstract. It has whatever
    the skill it is using provides, scaled by gear and attributes. Asking for a
    skill-based stat with no skill in hand gives zero, which is correct: there is
    nothing being used.
    """

    name: str = "None"
    base: dict[str, float] = field(default_factory=dict)
    #: Seconds before increases are applied. Left unset, the skill takes its
    #: slot's typical cooldown, in the same way it takes its slot's damage
    #: multiplier. No designed skill states one of its own yet.
    cooldown: float | None = None
    #: The skill's gameplay tags, as the Weapon Skills sheet already carries
    #: them, for example "Type.AOE.PointBlank" or "Item.Weapon.Dagger". These
    #: decide which of the character's tag-scoped modifiers reach this skill.
    tags: frozenset[str] = frozenset()
    #: Which of the seven slots this skill occupies. Decides its damage
    #: multiplier when the skill does not state one of its own. See SKILL_SLOTS.
    slot: str = BASIC_ATTACK_SLOT
    #: Percent of weapon damage this particular skill deals, when it differs
    #: from what its slot implies. Four designed skills already state one:
    #: Skull Splitter says 500% and Annihilator says 300%.
    damage_multiplier: float | None = None

    def __post_init__(self) -> None:
        # Every skill supplies a base critical strike chance unless it names its
        # own. Without one, every increased critical strike chance affix in the
        # game multiplies zero and is worth nothing, which is issue #120. The
        # design document says this base belongs to the skill rather than to the
        # weapon or the class, so a default here is what makes that true of every
        # skill rather than only of the 61 that are designed so far.
        #
        # NOT WHAT PATH OF EXILE DOES, deliberately. There, attacks take their
        # base critical strike chance from the weapon and only spells take it
        # from the skill. This project applies one rule to both, which is the
        # design document's stat source table and is simpler.
        if "crit_chance" not in self.base:
            object.__setattr__(
                self, "base", {**self.base, "crit_chance": DEFAULT_SKILL_CRIT_CHANCE})

        unknown = set(self.base) - set(ALL_STATS)
        if unknown:
            raise ValueError(
                f"skill {self.name} names stats that are not on the character "
                f"sheet: {sorted(unknown)}")
        not_skill_based = set(self.base) - SKILL_BASE_STATS
        if not_skill_based:
            raise ValueError(
                f"skill {self.name} supplies a base for "
                f"{sorted(not_skill_based)}, whose base does not come from the "
                "skill. See BASE_SOURCE.")
        if self.slot not in SKILL_SLOTS:
            raise ValueError(
                f"skill {self.name} is in slot {self.slot!r}; expected one of "
                f"{list(SKILL_SLOTS)}")
        if self.damage_multiplier is not None and self.damage_multiplier < 0:
            raise ValueError(
                f"skill {self.name} deals {self.damage_multiplier}% of weapon "
                "damage, which is less than none")
        if self.cooldown is not None and self.cooldown < 0:
            raise ValueError(
                f"skill {self.name} has a cooldown of {self.cooldown}s, which "
                "is less than none")

    def weapon_damage_percent(self) -> float:
        """Percent of weapon damage one use of this skill deals."""
        if self.damage_multiplier is not None:
            return self.damage_multiplier
        return SKILL_SLOTS[self.slot].typical_damage

    def base_cooldown(self) -> float:
        """Seconds before this skill can be used again, before any reduction.

        Its slot's typical unless the skill states its own, which is the same
        rule the damage multiplier follows.
        """
        if self.cooldown is not None:
            return self.cooldown
        return SKILL_SLOTS[self.slot].typical_cooldown


@dataclass(frozen=True)
class Character:
    definition: ClassDefinition
    level: int
    attributes: Attributes = field(default_factory=Attributes)
    gear: Gear = field(default_factory=Gear)
    skill: Skill = field(default_factory=Skill)
    #: Multiplicative sources, from gems, passive tree keystones and
    #: enchantments. Held on the character rather than on Gear because a
    #: keystone is not a piece of equipment, and because each one multiplies on
    #: its own rather than joining a sum the way gear increases do.
    more: tuple[More, ...] = ()

    def __post_init__(self) -> None:
        if not 1 <= self.level <= MAX_LEVEL:
            raise ValueError(f"level {self.level} outside 1-{MAX_LEVEL}")
        spent = self.attributes.total()
        if spent > self.level:
            raise ValueError(
                f"{spent} attribute points spent at level {self.level}; a "
                "character gains one point per level")

    def base(self, stat: str) -> float:
        """Before any scaling, from whichever source BASE_SOURCE names."""
        source = BASE_SOURCE[stat]
        if source == "weapon":
            start = self.gear.weapon_base.get(stat, 0.0)
        elif source == "skill":
            start = self.skill.base.get(stat, 0.0)
        else:
            start = self.definition.base_at(stat, self.level)
        return start + self.gear.flat.get(stat, 0.0)

    def increases(self, stat: str) -> float:
        """Every increase that reaches this stat for the skill in hand.

        Attribute points and unscoped gear increases always count. A tag-scoped
        modifier counts only when the skill being used carries a matching tag.
        """
        total = (self.attributes.increases_for(stat)
                 + self.gear.increased.get(stat, 0.0))
        for m in self.gear.modifiers:
            if m.stat == stat and m.applies_to(self.skill.tags):
                total += m.increase
        return total

    def more_multiplier(self, stat: str) -> float:
        """Every more multiplier that reaches this stat, multiplied together.

        Each source is its own factor. They are NOT summed first, which is the
        entire difference between this bucket and `increases`, and the reason
        stacking two independent 50% more multipliers gives 2.25x rather than
        2.0x.
        """
        product = 1.0
        for m in self.more:
            if m.stat == stat and m.applies_to(self.skill.tags):
                product *= m.factor
        return product

    def more_sources_for(self, stat: str) -> tuple[More, ...]:
        """The individual multipliers reaching a stat, for reporting."""
        return tuple(m for m in self.more
                     if m.stat == stat and m.applies_to(self.skill.tags))

    def stat(self, stat: str) -> float:
        """The final value, through the pipeline in the design document.

            Final = (base + flat) * (1 + sum of increases) * more1 * more2 * ...

        Three buckets, and which one a modifier lands in is what decides whether
        it has diminishing returns. Flat and increased come from gear affixes;
        more comes from gems, keystones and enchantments.
        """
        if stat not in DEFAULT_STAT_LINE:
            raise KeyError(f"{stat} is not on the character sheet")
        base, inc = self.base(stat), self.increases(stat)
        more = self.more_multiplier(stat)

        if stat in RATE_STATS:
            # An increase shortens an interval, so it divides. A more multiplier
            # divides for the same reason: both make the interval shorter, and
            # dividing means it can never reach zero, which is why cooldown
            # reduction needs no cap.
            value = base / ((1.0 + inc) * more) if base else 0.0
        else:
            value = base * (1.0 + inc) * more

        cap = HARD_CAPS.get(stat)
        return min(value, cap) if cap is not None else value

    def cooldown_of(self, base_cooldown: float) -> float:
        """A skill's cooldown after this character's reduction.

        The skill supplies the base. Displayed reduction is
        increases / (1 + increases), so a character shown at 25% turns a
        4 second skill into a 3 second one. A more multiplier divides as well,
        so it too can never bring a cooldown to zero.
        """
        return base_cooldown / self._cooldown_divisor()

    def skill_cooldown(self) -> float:
        """The cooldown of the skill in hand, after this character's reduction."""
        return self.cooldown_of(self.skill.base_cooldown())

    def base_max_mana(self) -> float:
        """Maximum mana from the class and level alone, before gear."""
        return self.definition.base_at("max_mana", self.level)

    def _cost_scale(self) -> float:
        """How much of a level 100 cost a character of this level pays.

        Costs ride the default mana progression, so the share of a pool one use
        takes is the same at level 1 as at level 100. The default line is used
        rather than this character's own class, which is what keeps the cost the
        same number for every class and makes a larger pool buy more casts.
        """
        reference = DEFAULT_STAT_LINE["max_mana"].at(MANA_COST_LEVEL)
        return DEFAULT_STAT_LINE["max_mana"].at(self.level) / reference

    def mana_cost_of(self, slot: str | None = None) -> float:
        """What one use of a skill in this slot costs, in mana.

        For the Aura this is the drain per second while the toggle is on, not a
        cost per use. Defaults to the slot of the skill in hand.
        """
        name = self.skill.slot if slot is None else slot
        if name not in SKILL_SLOTS:
            raise KeyError(f"{name} is not one of the seven slots")
        return SKILL_SLOTS[name].mana_cost * self._cost_scale()

    def mana_on_hit(self) -> float:
        """Mana the automatic basic attack restores each time it lands."""
        return BASIC_ATTACK_MANA_ON_HIT * self._cost_scale()

    def mana_income(self) -> float:
        """Mana per second while fighting: regeneration plus basic attacks.

        A character with no weapon has no attack speed and so earns nothing from
        hits, which is correct rather than a gap.
        """
        return (self.stat("mana_regen")
                + self.stat("attack_speed") * self.mana_on_hit())

    def seconds_of_aura(self) -> float:
        """How long the Aura runs from a full pool while standing still.

        Standing still, so regeneration is the only income. Issue #36 requires
        the aura to switch off when the resource runs out, and this is what says
        whether that is reachable. Infinite means it is not: for a class whose
        regeneration covers the drain the aura simply stays on, which is true of
        the Ritualist and is intended.
        """
        net = self.mana_cost_of("Aura") - self.stat("mana_regen")
        if net <= 0.0:
            return math.inf
        return self.stat("max_mana") / net

    def displayed_cooldown_reduction(self) -> float:
        divisor = self._cooldown_divisor()
        return 100.0 * (divisor - 1.0) / divisor

    def _cooldown_divisor(self) -> float:
        return ((1.0 + self.increases("cooldown_reduction"))
                * self.more_multiplier("cooldown_reduction"))

    def sheet(self) -> dict[str, float]:
        """Every stat on the character sheet, in display order."""
        return {s: self.stat(s) for s in ALL_STATS}


# --------------------------------------------------------------------------

def _check_stat_line_is_complete() -> None:
    missing = set(ALL_STATS) - set(DEFAULT_STAT_LINE)
    extra = set(DEFAULT_STAT_LINE) - set(ALL_STATS)
    if missing or extra:
        raise ValueError(
            f"DEFAULT_STAT_LINE does not match the character sheet. "
            f"missing={sorted(missing)} extra={sorted(extra)}")


def _check_every_stat_has_a_base_source() -> None:
    missing = set(ALL_STATS) - set(BASE_SOURCE)
    if missing:
        raise ValueError(
            f"stats with no recorded base source: {sorted(missing)}")
    bad = {s: v for s, v in BASE_SOURCE.items()
           if v not in ("class", "weapon", "skill")}
    if bad:
        raise ValueError(f"unknown base sources: {bad}")


def _check_attributes_only_scale() -> None:
    """Attributes may only ever be increases. There is no second kind."""
    for attribute, effects in ATTRIBUTE_EFFECTS.items():
        for stat, value in effects.items():
            if not isinstance(value, float):
                raise TypeError(
                    f"{attribute} -> {stat} is {type(value).__name__}; an "
                    "attribute effect is a plain increase fraction")
            if stat not in ALL_STATS:
                raise ValueError(
                    f"{attribute} scales {stat}, which is not on the sheet")


_check_stat_line_is_complete()
_check_every_stat_has_a_base_source()
_check_attributes_only_scale()


if __name__ == "__main__":
    print("The character sheet")
    print()
    total = 0
    for group, stats in STAT_GROUPS.items():
        print(f"  {group} ({len(stats)})")
        for s in stats:
            sc = DEFAULT_STAT_LINE[s]
            scaled_by = [a for a, e in ATTRIBUTE_EFFECTS.items() if s in e]
            notes = []
            if s in WEAPON_BASE_STATS:
                notes.append("base from weapon")
            if s in RATE_STATS:
                notes.append("divides")
            if s in HARD_CAPS:
                notes.append(f"hard cap {HARD_CAPS[s]:.0f}")
            if s in SOFT_CAPS:
                notes.append(f"soft cap {SOFT_CAPS[s]:.0f}")
            print(f"    {s:<26} base {sc.base:>7.2f}  /level {sc.per_level:>6.2f}"
                  f"  {'+'.join(scaled_by) or '-':<14} {', '.join(notes)}")
            total += 1
        print()
    print(f"  {total} stats. Across 24 classes that is {24 * total * 2:,} numbers,")
    print("  which is why every class inherits this line and overrides only what")
    print("  its identity requires.")
    print()

    print("  Attributes only ever scale. Every effect below is an increase")
    print("  multiplying a base that came from somewhere else.")
    print()
    print("  Each attribute at 100 points, against a probe base supplied by")
    print("  whichever source that stat uses, so the multiplier is visible.")
    print("  Hard-capped stats use a probe base of 10 so the cap does not")
    print("  distort the reading.")
    print()
    print(f"    {'attribute':<13} {'stat':<21} {'source':<7} {'base':>6} "
          f"{'at 100 pts':>11}  multiplier")
    print("    " + "-" * 70)
    for attribute, effects in ATTRIBUTE_EFFECTS.items():
        for stat in effects:
            src = BASE_SOURCE[stat]
            probe_base = 10.0 if stat in HARD_CAPS else 100.0
            probe = ClassDefinition(name="Probe",
                                    overrides={stat: Scaling(base=probe_base)})
            kwargs = {}
            if src == "weapon":
                kwargs["gear"] = Gear(weapon_base={stat: probe_base})
            elif src == "skill":
                kwargs["skill"] = Skill(name="Probe", base={stat: probe_base})
            full = Character(probe, level=100,
                             attributes=Attributes(**{attribute: 100}), **kwargs)
            value = full.stat(stat)
            note = (f"{full.displayed_cooldown_reduction():.0f}% shown"
                    if stat in RATE_STATS else f"{value / probe_base:.2f}x")
            print(f"    {attribute:<13} {stat:<21} {src:<7} {probe_base:>6.0f} "
                  f"{value:>11.2f}  {note}")
    print()

    print("  A level 100 character on the default line, 60 Vitality 40 Efficacy,")
    print("  using a skill with a 20% base critical strike chance:")
    c = Character(GENERIC, level=100,
                  attributes=Attributes(vitality=60, efficacy=40),
                  skill=Skill(name="Example", base={"crit_chance": 20.0},
                              cooldown=4.0))
    print(f"    max health           {c.stat('max_health'):>10,.0f}")
    print(f"    max mana             {c.stat('max_mana'):>10,.0f}")
    print(f"    health regen         {c.stat('health_regen'):>10,.2f}/s")
    print(f"    movement speed       {c.stat('movement_speed'):>10,.2f} m/s")
    print(f"    crit chance          {c.stat('crit_chance'):>10,.1f}%"
          "   <- from the skill, scaled")
    print(f"    that skill's cooldown{c.cooldown_of(c.skill.cooldown):>10,.2f}s"
          f"   (shown as {c.displayed_cooldown_reduction():.1f}% reduction)")
    print()
    print("  With no skill in hand, critical strike chance is "
          f"{Character(GENERIC, level=100).stat('crit_chance'):.0f}. A character")
    print("  has no critical strike chance in the abstract; it has whatever the")
    print("  skill it is using provides.")
    print()

    print("  Tag-scoped increases. One piece of equipment granting +40% area of")
    print("  effect, restricted to skills tagged Type.AOE, against two skills:")
    boots = Gear(modifiers=(Modifier("area_of_effect", 0.40,
                                     frozenset({"Type.AOE"})),))
    for skill in (Skill(name="Smoke Bomb", tags=frozenset(
                      {"Item.Weapon.Dagger", "Type.AOE.PointBlank"})),
                  Skill(name="Thrust", tags=frozenset(
                      {"Item.Weapon.Spear", "Type.Strike"}))):
        c = Character(GENERIC, level=100, gear=boots, skill=skill)
        tags = ", ".join(sorted(skill.tags))
        print(f"    {skill.name:<12} {c.stat('area_of_effect'):>6.0f}%   {tags}")
    print()
    print("    The first is tagged Type.AOE.PointBlank, which matches the")
    print("    requirement of Type.AOE, so the increase reaches it. The second")
    print("    is not tagged for area at all, so the same equipment does")
    print("    nothing for it. The character holds the increase either way.")
    print()

    print("=" * 72)
    print("  Three buckets: flat, increased, more.")
    print()
    print("    Final = (base + flat) * (1 + sum of increases) * more1 * more2...")
    print()
    print("  Flat and increased come from gear affixes. More multipliers come")
    print("  from gems, passive keystones and enchantments, and each one")
    print("  multiplies on its own rather than joining the sum.")
    print()
    probe_line = dict(DEFAULT_STAT_LINE)
    probe_line["max_health"] = Scaling(base=1000.0, per_level=0.0)
    probe_class = ClassDefinition(name="Probe", overrides=probe_line)

    print("  What one more 60% is worth, against what one more +60% increased")
    print("  is worth, to a character who already holds some increases:")
    print()
    print(f"    {'already held':>14} {'health':>9} {'+60% increased':>16} "
          f"{'a 60% more':>13}")
    print("    " + "-" * 56)
    for held in (0.0, 1.0, 3.0, 8.0):
        have = Character(probe_class, level=100,
                         gear=Gear(increased={"max_health": held}))
        more_inc = Character(probe_class, level=100,
                             gear=Gear(increased={"max_health": held + 0.60}))
        more_mult = Character(probe_class, level=100,
                              gear=Gear(increased={"max_health": held}),
                              more=(More("gem", "max_health", 0.60),))
        now = have.stat("max_health")
        print(f"    {held:>13.0%} {now:>9,.0f} "
              f"{more_inc.stat('max_health') / now - 1:>15.1%} "
              f"{more_mult.stat('max_health') / now - 1:>12.1%}")
    print()
    print("  The increased column shrinks as a character stacks more of it.")
    print("  The more column does not, and that is the whole difference. It is")
    print("  why the question a player answers is which independent multiplier")
    print("  they are missing, not which number on an item is biggest.")
    print()

    print("  More multipliers compound with each other rather than summing:")
    print()
    for count in range(0, 5):
        c = Character(probe_class, level=100,
                      more=tuple(More("gem", "max_health", 0.50)
                                 for _ in range(count)))
        summed = 1.0 + 0.50 * count
        print(f"    {count} sources of 50% more   multiplied {c.more_multiplier('max_health'):>5.2f}x"
              f"   if they were summed instead {summed:>5.2f}x")
