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
                 resistances, movement speed
    the weapon   what the equipped weapon is: attack speed, and off this sheet,
                 attack range and attack damage
    the skill    what the ability being used is: critical strike chance, area of
                 effect, damage-over-time frequency, and off this sheet, the base
                 cooldown, projectile count, charges and duration

Critical strike chance is the example the project owner gave: each skill carries
its own base, and gear and attributes scale that. It is not a class number.

A class still customises weapon-supplied and skill-supplied stats, but with an
increase joining the same sum gear and attributes use, never by replacing the
weapon's or the skill's own value.

THE DEFAULT LINE AND OVERRIDES. There are 33 class-supplied stats, each needing a
level 1 base and a per-level gain. Across 24 classes that would be 1,584 numbers.
So every class inherits DEFAULT_STAT_LINE and overrides only the stats that
express its identity. A class may override any stat; the default is a starting
point, not a floor.

WHAT THIS MODULE DOES NOT DO. It sets no per-class values. DEFAULT_STAT_LINE is
deliberately the only stat line here. Ravager, Ritualist and Masochist are issue
#77, and their values need reviewing on their own terms rather than arriving as a
side effect of building the structure.
"""

from __future__ import annotations

from dataclasses import dataclass, field

MAX_LEVEL = 100

#: One resistance per damage type, in the order the design document lists them.
DAMAGE_TYPES = ("War", "Demonic", "Death", "Pestilence",
                "Famine", "Celestial", "Chaos", "Void")

RESISTANCE_STATS = tuple(f"resistance_{d.lower()}" for d in DAMAGE_TYPES)

#: The character sheet, grouped the way `game/Config/Tags/CataclysmTags.ini`
#: groups its Stat.* tags.
STAT_GROUPS: dict[str, tuple[str, ...]] = {
    "Resource": ("max_health", "max_mana", "max_energy_shield", "class_resource"),
    "Recovery": ("health_regen", "mana_regen", "energy_shield_regen", "life_leech"),
    "Defense": ("armor", "evasion", "block_chance", "damage_reduction",
                "retaliation", "crowd_control_resistance") + RESISTANCE_STATS,
    "Offense": ("crit_chance", "crit_multiplier", "attack_speed",
                "area_of_effect", "dot_frequency", "penetration", "spell_damage"),
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
    "area_of_effect": "skill",
    "dot_frequency": "skill",
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

#: Stats that are counted as a percentage and cannot exceed their cap no matter
#: what. Only hard caps appear here; soft caps are exceedable by design and so
#: are deliberately not clamped. See the Stat Calculation section of the design
#: document.
HARD_CAPS: dict[str, float] = {"crit_chance": 100.0}

#: Recorded so the soft caps are not lost, but NOT applied. Affixes may exceed
#: resistances' 70% and evasion's 60%.
SOFT_CAPS: dict[str, float] = dict(
    {"evasion": 60.0}, **{s: 70.0 for s in RESISTANCE_STATS})

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
    # Defence
    "armor": Scaling(),
    "evasion": Scaling(),
    "block_chance": Scaling(),
    "damage_reduction": Scaling(),
    "retaliation": Scaling(),
    "crowd_control_resistance": Scaling(),
    **{s: Scaling() for s in RESISTANCE_STATS},
    # Offence. Critical strike chance, area of effect and damage-over-time
    # frequency have no class entry that matters: their base comes from the
    # skill. The zeroes here are placeholders the pipeline never reads.
    "crit_chance": Scaling(),
    "crit_multiplier": Scaling(base=150.0),
    "attack_speed": Scaling(),
    "area_of_effect": Scaling(),
    "dot_frequency": Scaling(),
    "penetration": Scaling(),
    "spell_damage": Scaling(),
    # Utility. Movement speed is in metres per second, following the project
    # owner's example of a tank at about 3.
    "movement_speed": Scaling(base=4.0),
    "cooldown_reduction": Scaling(),
    "magic_find": Scaling(),
    "loot_quantity": Scaling(),
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


@dataclass(frozen=True)
class Gear:
    """What equipment contributes, by where it enters the pipeline.

    `flat` is added to the class base before scaling. `increased` joins the same
    sum the attribute points do. `weapon_base` supplies the base for the stats
    the weapon owns rather than the class.
    """

    flat: dict[str, float] = field(default_factory=dict)
    increased: dict[str, float] = field(default_factory=dict)
    weapon_base: dict[str, float] = field(default_factory=dict)

    def __post_init__(self) -> None:
        for label, table in (("flat", self.flat), ("increased", self.increased),
                             ("weapon_base", self.weapon_base)):
            unknown = set(table) - set(ALL_STATS)
            if unknown:
                raise ValueError(
                    f"gear {label} names stats that are not on the character "
                    f"sheet: {sorted(unknown)}")


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
    #: Seconds before increases are applied.
    cooldown: float = 0.0

    def __post_init__(self) -> None:
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


@dataclass(frozen=True)
class Character:
    definition: ClassDefinition
    level: int
    attributes: Attributes = field(default_factory=Attributes)
    gear: Gear = field(default_factory=Gear)
    skill: Skill = field(default_factory=Skill)

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
        return (self.attributes.increases_for(stat)
                + self.gear.increased.get(stat, 0.0))

    def stat(self, stat: str) -> float:
        """The final value, through the pipeline in the design document."""
        if stat not in DEFAULT_STAT_LINE:
            raise KeyError(f"{stat} is not on the character sheet")
        base, inc = self.base(stat), self.increases(stat)

        if stat in RATE_STATS:
            # An increase shortens an interval. Dividing means the interval can
            # never reach zero, which is why cooldown reduction needs no cap.
            value = base / (1.0 + inc) if base else 0.0
        else:
            value = base * (1.0 + inc)

        cap = HARD_CAPS.get(stat)
        return min(value, cap) if cap is not None else value

    def cooldown_of(self, base_cooldown: float) -> float:
        """A skill's cooldown after this character's reduction.

        The skill supplies the base. Displayed reduction is
        increases / (1 + increases), so a character shown at 25% turns a
        4 second skill into a 3 second one.
        """
        inc = self.increases("cooldown_reduction")
        return base_cooldown / (1.0 + inc)

    def displayed_cooldown_reduction(self) -> float:
        inc = self.increases("cooldown_reduction")
        return 100.0 * inc / (1.0 + inc)

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
