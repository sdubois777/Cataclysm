"""Class base stats, per-level scaling, and the stat pipeline they feed.

WHAT THIS IS FOR. `docs/Cataclysm_GDD_v2.md` gives every attribute as a
percentage per point, and the Stat Calculation section says attributes scale
values rather than creating them. It never says what they scale. This module
supplies the missing half for one class, the Masochist, and implements the
pipeline so the numbers can be looked at rather than argued about.

THE PIPELINE, from the Stat Calculation section:

    Final Value = (class base + per-level scaling + flat from gear)
                  * (1 + sum of increases)

Attribute points and gear affixes worded "increased" all land in the same sum.
Only "more" and "less" multiply separately, and nothing here uses them.

WHERE THE SCALE COMES FROM. There are no absolute damage or health numbers in
the design documents, and `sim/cataclysm_sim/combat.py` is entirely relative --
it works in ratios of Power Score to Enemy Score and never computes a hit point.
Power Score does not read vitals either. So nothing in the project constrained
these values except one place: `docs/Bulwark_Class_Tree_Final.json`.

That tree is written against concrete Maximum HP thresholds -- 1,000, 5,000,
8,000, 10,000, 15,000, 20,000 and 25,000 -- with passive nodes granting 50, 200
and 500 flat HP per point, and a keystone that triggers on absorbing a single hit
above 5,000 damage. The Bulwark is the design's most defensive class, so 20,000
to 25,000 is the top of the health range at full investment, not the middle.

Everything below is chosen to land inside that scale. See BULWARK_* constants.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# The scale, read out of the one class tree that carries concrete numbers
# --------------------------------------------------------------------------

#: Maximum HP thresholds the Bulwark tree's nodes are written against.
BULWARK_HP_THRESHOLDS = (1_000, 5_000, 8_000, 10_000, 15_000, 20_000, 25_000)

#: Flat Maximum HP a single Bulwark passive point grants, by node.
BULWARK_FLAT_HP_PER_POINT = (50, 200, 500)

#: The Bulwark's deepest node requires this much Maximum HP to activate, so a
#: fully committed defensive character is expected to reach it.
BULWARK_ENDGAME_HP = 20_000

#: The tree's highest threshold of any kind. Treated as the ceiling a defensive
#: character reaches at full investment.
BULWARK_CEILING_HP = 25_000

#: Class resources run 0-100. Resolve, the only resource with designed values,
#: never exceeds 100 anywhere in the Bulwark tree.
CLASS_RESOURCE_MAX = 100

MAX_LEVEL = 100

# Attribute effects per point, from the attribute table in the design document.
VITALITY_HEALTH_PER_POINT = 0.02
VITALITY_HEALTH_REGEN_PER_POINT = 0.01
MIND_MANA_PER_POINT = 0.02
SPIRIT_SHIELD_PER_POINT = 0.02


# --------------------------------------------------------------------------
# Class base values
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class ClassStats:
    """One class's base values at level 1 and its gain per level.

    Per-level gain is linear. A curve would let a class be weak early and strong
    late, but nothing in the design asks for that, and a straight line is the
    thing to argue with first.
    """

    name: str
    base_health: float
    health_per_level: float
    base_mana: float = 0.0
    mana_per_level: float = 0.0
    base_energy_shield: float = 0.0
    energy_shield_per_level: float = 0.0
    #: Points of health restored per second before any increases.
    base_health_regen: float = 0.0
    health_regen_per_level: float = 0.0
    base_mana_regen: float = 0.0
    mana_regen_per_level: float = 0.0
    #: Whether abilities are paid for with health rather than mana.
    spends_health: bool = False

    def at_level(self, level: int) -> dict[str, float]:
        """Base values before gear and before any attribute scaling."""
        if not 1 <= level <= MAX_LEVEL:
            raise ValueError(f"level {level} outside 1-{MAX_LEVEL}")
        steps = level - 1
        return {
            "health": self.base_health + self.health_per_level * steps,
            "mana": self.base_mana + self.mana_per_level * steps,
            "energy_shield": (self.base_energy_shield
                              + self.energy_shield_per_level * steps),
            "health_regen": (self.base_health_regen
                             + self.health_regen_per_level * steps),
            "mana_regen": self.base_mana_regen + self.mana_regen_per_level * steps,
        }


# The Masochist: "Converts received damage into buffs and counterattacks. Uses
# HP instead of mana for abilities." (Demonic class table, design document.)
#
# Three consequences of that identity, each a proposal:
#
#   1. No mana at all, at any level. Not a small pool -- none. This makes Mind a
#      dead attribute for a Masochist, which is the point: the class trades an
#      entire attribute away and spends the points elsewhere. Precedent exists
#      in the design already, where the Vampire "cannot use energy shields".
#
#   2. Higher health regeneration than a class that spends mana would have,
#      because for this class health regeneration IS resource regeneration. A
#      Masochist that cannot regain health cannot cast.
#
#   3. No base energy shield. A shield absorbs damage before health, and this
#      class converts received damage into buffs, so a shield works against its
#      own identity. Energy shield remains reachable through gear, which is what
#      keeps Spirit meaningful for a player who deliberately builds for it.
#
# Health is set so that a level 100 Masochist has about 2,500 before gear and
# before attributes. With gear flat health of a similar size and 100 points in
# Vitality, that reaches roughly 15,000, and passive tree nodes carry it into the
# 20,000 to 25,000 band the Bulwark tree is written against.
MASOCHIST = ClassStats(
    name="Masochist",
    base_health=150.0,
    health_per_level=24.0,          # level 100: 150 + 24*99 = 2,526
    base_mana=0.0,
    mana_per_level=0.0,
    base_energy_shield=0.0,
    energy_shield_per_level=0.0,
    base_health_regen=1.0,
    health_regen_per_level=0.25,    # level 100: 1 + 0.25*99 = 25.75 per second
    base_mana_regen=0.0,
    mana_regen_per_level=0.0,
    spends_health=True,
)

CLASSES: dict[str, ClassStats] = {MASOCHIST.name: MASOCHIST}


# --------------------------------------------------------------------------
# A character, and the pipeline
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Attributes:
    """Points spent, one per level. Only the four that touch vitals are here."""

    vitality: int = 0
    mind: int = 0
    spirit: int = 0

    def total(self) -> int:
        return self.vitality + self.mind + self.spirit


@dataclass(frozen=True)
class Gear:
    """What equipment contributes, separated by where it enters the pipeline.

    Flat values are added to the class base before scaling. Increases join the
    same sum the attribute points do.
    """

    flat_health: float = 0.0
    flat_mana: float = 0.0
    flat_energy_shield: float = 0.0
    flat_health_regen: float = 0.0
    increased_health: float = 0.0
    increased_mana: float = 0.0
    increased_energy_shield: float = 0.0
    increased_health_regen: float = 0.0


@dataclass(frozen=True)
class Character:
    stats: ClassStats
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

    # -- the pipeline ------------------------------------------------------

    def max_health(self) -> float:
        base = self.stats.at_level(self.level)["health"] + self.gear.flat_health
        increases = (self.attributes.vitality * VITALITY_HEALTH_PER_POINT
                     + self.gear.increased_health)
        return base * (1 + increases)

    def max_mana(self) -> float:
        """Zero for a class that spends health, at every level and with any gear.

        A Masochist wearing +mana gear still has no mana pool. The class does not
        have the resource, so there is nothing for gear to add to.
        """
        if self.stats.spends_health:
            return 0.0
        base = self.stats.at_level(self.level)["mana"] + self.gear.flat_mana
        increases = (self.attributes.mind * MIND_MANA_PER_POINT
                     + self.gear.increased_mana)
        return base * (1 + increases)

    def max_energy_shield(self) -> float:
        base = (self.stats.at_level(self.level)["energy_shield"]
                + self.gear.flat_energy_shield)
        increases = (self.attributes.spirit * SPIRIT_SHIELD_PER_POINT
                     + self.gear.increased_energy_shield)
        return base * (1 + increases)

    def health_regen(self) -> float:
        base = (self.stats.at_level(self.level)["health_regen"]
                + self.gear.flat_health_regen)
        increases = (self.attributes.vitality * VITALITY_HEALTH_REGEN_PER_POINT
                     + self.gear.increased_health_regen)
        return base * (1 + increases)

    def effective_health_pool(self) -> float:
        """Health plus energy shield. What actually has to be chewed through."""
        return self.max_health() + self.max_energy_shield()


# --------------------------------------------------------------------------
# Reference builds, for checking the numbers land in the right band
# --------------------------------------------------------------------------

def naked(stats: ClassStats, level: int) -> Character:
    """No gear, no attribute points spent. The floor of the class."""
    return Character(stats=stats, level=level)


def geared(stats: ClassStats, level: int) -> Character:
    """A character who has spent every point on Vitality and wears gear with
    flat health roughly equal to what class and level supply.

    This is the upper bound of what base values and gear produce **before** the
    passive tree. The tree then carries it the rest of the way: the Bulwark's
    nodes alone grant 50, 200 and 500 flat HP per point and +4% per point.
    """
    class_health = stats.at_level(level)["health"]
    return Character(
        stats=stats,
        level=level,
        attributes=Attributes(vitality=level),
        gear=Gear(flat_health=class_health),
    )


def progression(stats: ClassStats) -> list[dict[str, float]]:
    """What the class looks like across the levels, both builds."""
    out = []
    for level in (1, 10, 25, 50, 75, 100):
        n, g = naked(stats, level), geared(stats, level)
        out.append({
            "level": level,
            "base_health": stats.at_level(level)["health"],
            "naked_health": n.max_health(),
            "geared_health": g.max_health(),
            "naked_regen": n.health_regen(),
            "geared_regen": g.health_regen(),
        })
    return out


if __name__ == "__main__":
    s = MASOCHIST
    print(f"{s.name} -- proposed base values")
    print()
    print(f"  base health {s.base_health:.0f} at level 1, "
          f"+{s.health_per_level:.0f} per level")
    print(f"  base health regen {s.base_health_regen:.2f}/s at level 1, "
          f"+{s.health_regen_per_level:.2f} per level")
    print("  mana: none at any level (spends health for abilities)")
    print("  energy shield: none from the class; gear only")
    print()
    print("  'naked' is no gear and no attribute points spent.")
    print("  'geared' is every point in Vitality plus gear flat health equal to")
    print("  what class and level supply. Neither includes the passive tree.")
    print()
    print("    level   class base    naked      geared    naked regen  geared regen")
    for row in progression(s):
        print(f"    {row['level']:>5}   {row['base_health']:>10,.0f} "
              f"{row['naked_health']:>10,.0f} {row['geared_health']:>11,.0f} "
              f"{row['naked_regen']:>12.1f} {row['geared_regen']:>13.1f}")
    print()

    end = geared(s, MAX_LEVEL)
    print("  A level 100 Masochist with gear and full Vitality, before the")
    print(f"  passive tree: {end.max_health():,.0f} health.")
    print()
    print("  Against the scale in the Bulwark class tree:")
    print(f"    thresholds the tree is written against: "
          f"{', '.join(f'{t:,}' for t in BULWARK_HP_THRESHOLDS)}")
    print(f"    deepest node requires: {BULWARK_ENDGAME_HP:,}")
    print(f"    highest threshold of any kind: {BULWARK_CEILING_HP:,}")
    gap = BULWARK_ENDGAME_HP - end.max_health()
    print(f"    the passive tree has to supply the remaining {gap:,.0f} "
          f"to reach {BULWARK_ENDGAME_HP:,}")
    print(f"    the Bulwark's own flat nodes give "
          f"{'/'.join(str(v) for v in BULWARK_FLAT_HP_PER_POINT)} HP per point, "
          f"so roughly {gap / max(BULWARK_FLAT_HP_PER_POINT):,.0f} points of the")
    print("    500-per-point node would close it on flat grants alone, before")
    print("    any of the tree's percentage nodes are counted")
