"""The character sheet: every stat a class supplies, and the pipeline that
turns class, level, gear and attributes into final values.

WHAT THIS IS. `docs/Cataclysm_GDD_v2.md` gives eight attributes as percentages
per point and says attributes scale values rather than creating them. It never
says what the full stat set is, nor where each stat's starting value comes from.
This module is that structure.

WHERE EACH BASE VALUE COMES FROM. Decided by the project owner 2026-08-02:

    the class    everything on the character sheet except the two rows below
    the weapon   attack range, attack damage, base attack speed
    the skill    projectile count, charges, skill duration

A class still customises the weapon-supplied and skill-supplied stats, but with
an *increase* that joins the same sum gear and attributes use, rather than by
replacing the weapon's own value. So a class that should attack quickly carries
an attack speed increase applying to whatever weapon it holds. That is why
ATTACK_SPEED appears below as a class stat: the class supplies the increase, the
weapon supplies the base.

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

#: Stats whose base comes from the equipped weapon, not the class. The class
#: value for these is an increase applied to the weapon's base.
WEAPON_BASE_STATS = frozenset({"attack_speed"})

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

@dataclass(frozen=True)
class AttributeEffect:
    """What one point of an attribute does to one stat.

    `kind` is the whole of issue #86. An attribute point can act in one of two
    ways, and reading the design document's per-point values under the wrong one
    changes them by an order of magnitude:

    INCREASE -- a fraction added to the stat's sum of increases, which multiplies
    the base. Right for magnitudes: health, mana, armor, a multiplier. Two per
    cent per point means 100 points triples the stat.

    FLAT -- percentage points added to the base, before increases multiply it.
    Right for chances: critical strike chance, evasion, block chance, magic find.
    These have no meaningful base to multiply, and reading them as increases
    makes the attribute nearly worthless. Ferocity read as an increase moves
    critical strike chance from 5% to 7.5% across a character's entire budget.

    Flat contributions land in the base, so increases from gear scale them. That
    also means an attribute can give a stat its first value: a class with no base
    evasion still gains evasion from Agility.
    """

    value: float
    kind: str = "increase"

    def __post_init__(self) -> None:
        if self.kind not in ("increase", "flat"):
            raise ValueError(f"kind must be 'increase' or 'flat', got {self.kind!r}")


def _inc(v: float) -> AttributeEffect:
    return AttributeEffect(v, "increase")


def _flat(v: float) -> AttributeEffect:
    return AttributeEffect(v, "flat")


#: What one point of each attribute does. Proposed on issue #86; the design
#: document's original values are recorded in ORIGINAL_ATTRIBUTE_VALUES below so
#: the two can be compared.
ATTRIBUTE_EFFECTS: dict[str, dict[str, AttributeEffect]] = {
    # Movement speed is not a pool. At the design document's +2% per point, 100
    # points tripled it, which no top-down action game survives.
    "agility":      {"movement_speed": _inc(0.003), "evasion": _flat(0.3)},
    "ferocity":     {"crit_chance": _flat(0.3), "crit_multiplier": _inc(0.02)},
    "constitution": {"armor": _inc(0.02), "block_chance": _flat(0.3)},
    "vitality":     {"max_health": _inc(0.02), "health_regen": _inc(0.01)},
    "mind":         {"max_mana": _inc(0.02), "mana_regen": _inc(0.01)},
    "spirit":       {"max_energy_shield": _inc(0.02),
                     "energy_shield_regen": _inc(0.01)},
    "efficacy":     {"cooldown_reduction": _inc(0.01),
                     "area_of_effect": _inc(0.01), "dot_frequency": _inc(0.01)},
    "luck":         {"magic_find": _flat(0.5), "loot_quantity": _inc(0.01)},
}

#: The per-point values as written in `docs/Cataclysm_GDD_v2.md`, kept so the
#: proposal on #86 can be shown against what it replaces. Seven of seventeen
#: change; the ten vital, regeneration, armor and Efficacy entries do not.
ORIGINAL_ATTRIBUTE_VALUES: dict[str, float] = {
    "movement_speed": 0.02, "evasion": 0.005,
    "crit_chance": 0.005, "crit_multiplier": 0.05,
    "armor": 0.02, "block_chance": 0.01,
    "max_health": 0.02, "health_regen": 0.01,
    "max_mana": 0.02, "mana_regen": 0.01,
    "max_energy_shield": 0.02, "energy_shield_regen": 0.01,
    "cooldown_reduction": 0.01, "area_of_effect": 0.02, "dot_frequency": 0.01,
    "magic_find": 0.0001, "loot_quantity": 0.01,
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
    # Offence
    "crit_chance": Scaling(base=5.0),
    "crit_multiplier": Scaling(base=150.0),
    "attack_speed": Scaling(),
    "area_of_effect": Scaling(),
    "dot_frequency": Scaling(),
    "penetration": Scaling(),
    "spell_damage": Scaling(),
    # Utility
    "movement_speed": Scaling(base=100.0),
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

    def _contribution(self, stat: str, kind: str) -> float:
        return sum(getattr(self, attribute) * effects[stat].value
                   for attribute, effects in ATTRIBUTE_EFFECTS.items()
                   if stat in effects and effects[stat].kind == kind)

    def increases_for(self, stat: str) -> float:
        """Sum of increases this allocation contributes to one stat."""
        return self._contribution(stat, "increase")

    def flat_for(self, stat: str) -> float:
        """Percentage points this allocation adds to one stat's base.

        Lands in the base rather than beside the final value, so gear increases
        scale it, and so an attribute can give a stat its first value.
        """
        return self._contribution(stat, "flat")


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
class Character:
    definition: ClassDefinition
    level: int
    attributes: Attributes = field(default_factory=Attributes)
    gear: Gear = field(default_factory=Gear)

    def __post_init__(self) -> None:
        if not 1 <= self.level <= MAX_LEVEL:
            raise ValueError(f"level {self.level} outside 1-{MAX_LEVEL}")
        spent = self.attributes.total()
        if spent > self.level:
            raise ValueError(
                f"{spent} attribute points spent at level {self.level}; a "
                "character gains one point per level")

    def base(self, stat: str) -> float:
        """Before any scaling. Weapon-based stats take their base from the
        weapon; everything else takes it from the class and level.

        Flat contributions from gear and from attribute points both land here,
        so that increases scale them.
        """
        if stat in WEAPON_BASE_STATS:
            start = self.gear.weapon_base.get(stat, 0.0)
        else:
            start = self.definition.base_at(stat, self.level)
        return (start + self.gear.flat.get(stat, 0.0)
                + self.attributes.flat_for(stat))

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


_check_stat_line_is_complete()


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

    print("  Every attribute at 100 points, level 100, default stat line.")
    print("  'was' applies the design document's per-point value as an increase,")
    print("  which is what issue #86 reports as broken. 'now' applies the")
    print("  proposal: increases for magnitudes, flat percentage points for")
    print("  chances.")
    print()
    header = (f"    {'attribute':<13} {'stat':<21} {'at 0':>9} "
              f"{'was':>10} {'now':>10}  kind")
    print(header)
    print("    " + "-" * (len(header) - 4))
    for attribute, effects in ATTRIBUTE_EFFECTS.items():
        for stat, effect in effects.items():
            none = Character(GENERIC, level=100)
            full = Character(GENERIC, level=100,
                             attributes=Attributes(**{attribute: 100}))
            at_zero = none.stat(stat)
            now = full.stat(stat)
            # What the original value would have produced, read as an increase.
            was_inc = 100 * ORIGINAL_ATTRIBUTE_VALUES[stat]
            if stat in RATE_STATS:
                at_zero = 0.0
                now = full.displayed_cooldown_reduction()
                was = 100.0 * was_inc / (1.0 + was_inc)
            else:
                was = none.base(stat) * (1.0 + was_inc)
            changed = "" if effect.value == ORIGINAL_ATTRIBUTE_VALUES[stat] \
                and effect.kind == "increase" else "  <- changed"
            print(f"    {attribute:<13} {stat:<21} {at_zero:>9.2f} "
                  f"{was:>10.2f} {now:>10.2f}  {effect.kind}{changed}")
    print()
    pairs = [(a, s) for a, e in ATTRIBUTE_EFFECTS.items() for s in e]
    was_dead, now_dead = [], []
    for attribute, stat in pairs:
        none = Character(GENERIC, level=100)
        full = Character(GENERIC, level=100,
                         attributes=Attributes(**{attribute: 100}))
        if none.base(stat) == 0.0 and stat not in RATE_STATS:
            was_dead.append(stat)
        if full.stat(stat) == 0.0 and stat not in RATE_STATS:
            now_dead.append(stat)
    print(f"  {len(was_dead)} of the {len(pairs)} produced nothing at all under the")
    print("  old reading, because their base is zero and an increase has nothing")
    print(f"  to multiply. {len(was_dead) - len(now_dead)} of those are chances and now respond,")
    print("  because flat contributions land in the base.")
    print()
    print(f"  {len(now_dead)} still produce nothing, and correctly so: they are")
    print("  magnitudes, and no class has given them a base yet. That is #77.")
    print(f"    {', '.join(sorted(set(now_dead)))}")
