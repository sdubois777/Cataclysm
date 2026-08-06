"""What each enemy DOES, in the same vocabulary a player skill is written in.

WHAT THIS IS FOR. `enemy_stats.py` gives every enemy health, damage, armour,
attack interval, movement speed, evasion and resistance. None of that says what
an enemy does with its turn. Issue #29 is the epic that asks, and it is split one
enemy at a time: #348 the Imp, #349 the Succubus, #350 the Hellhound, #351 the
Brute, #352 the Corrupted Sentinel, #353 the Abyssal Warden, #354 the Gatekeeper.

**Only the Imp is filled in.** The other six are open issues. An archetype with no
entry here has no designed abilities yet, and asking for one raises rather than
returning an empty list, so a missing design cannot be mistaken for a finished
one.

THE VOCABULARY IS THE PLAYER'S, DELIBERATELY. An ability is a `Shape` plus
`Params`, the same two columns `game/Data/WeaponSkills.csv` already carries for
player skills, in the same units: radii and ranges in metres, durations in
seconds. `docs/Cataclysm_GDD_v2.md` section V lists the seven shapes and section
X's Attack Telegraphs subsection already draws an enemy's ground marker from
those same numbers. Reusing them means an enemy ability needs no second executor
in the engine and no second authoring format.

WHERE THIS ENDS UP. Issue #355 publishes the archetype numbers as a table under
`game/Data/` that the engine can load. These rows go with them. Until that lands
this module is the single machine-readable copy, and
`tools/tests/test_enemy_abilities.py` checks the design document against it
rather than the other way round.

WHY THE RING MATHS IS HERE AND NOT IN THE DOCUMENT. `ring_capacity` and
`attackers_within_reach` are the whole of the Imp's pack design. They take the
body radius from `enemy_stats.py` and produce the number of attackers, so the
design document's pack figures are recomputed rather than restated. If somebody
changes a body radius, the count changes here and the document's table stops
matching, which is the failure this arrangement exists to cause.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from .enemy_stats import ARCHETYPES, Archetype, archetype

#: The seven shapes from `docs/Cataclysm_GDD_v2.md` section V, spelled the way
#: the `Shape` column of `game/Data/WeaponSkills.csv` spells them.
SHAPES = ("Strike", "Projectile", "SelfBuff", "Movement", "Summon", "Aura",
          "Debuff")

#: Which parameter names each shape reads. Same names and same meanings as the
#: player skill rows use. A key outside its shape's list is a typo that would be
#: read as nothing by whatever executes the shape.
SHAPE_PARAMS: dict[str, tuple[str, ...]] = {
    "Strike": ("Radius", "Angle", "MaxTargets", "Duration", "Interval",
               "Knockback"),
    "Projectile": ("Range", "Radius", "Pierce", "Returns", "Speed"),
    "SelfBuff": ("Duration", "Radius"),
    "Movement": ("Mode", "Range", "Radius"),
    "Summon": ("Range", "Radius", "Count", "MaxActive", "Duration", "Interval"),
    "Aura": ("Radius", "Duration", "Interval"),
    "Debuff": ("Range", "Radius", "MaxTargets", "Duration"),
}

#: Riders any shape may carry, from the same section of the design document.
RIDERS = ("GroundRadius", "GroundDuration", "Burn", "Effect",
          "FinalHitPercent", "HealthCostPercent")

#: The player's capsule radius in metres. `CapsuleRadius` is 42 centimetres in
#: `game/Source/Cataclysm/Character/CataclysmPlayerCharacter.cpp`. Every ring of
#: enemies is measured from the player's centre, and the innermost one starts at
#: the player's own edge, so this is the offset every ring sits on top of.
PLAYER_BODY_RADIUS = 0.42

#: A marker smaller than this is smaller than the creature standing in it, so
#: there is nowhere to walk. Stated in the Attack Telegraphs subsection of
#: `docs/Cataclysm_GDD_v2.md` and repeated here because `is_telegraphed` applies
#: it. `tools/tests/test_enemy_telegraphs.py` owns the same figure.
SMALLEST_USEFUL_MARKER_METRES = 1.0

#: The reaction allowance in the walk-out wind-up formula, and the slowest class
#: in metres per second. Both from the Attack Telegraphs subsection.
REACTION_ALLOWANCE = 0.4
WALK_OUT_SPEED = 3.5


@dataclass(frozen=True)
class Ability:
    """One thing an enemy can do.

    `shape` and `params` are exactly what a player skill row carries, so an
    ability can be executed by the same code and its telegraph marker drawn from
    its own numbers.
    """

    name: str
    shape: str
    params: dict[str, float | str] = field(default_factory=dict)

    #: Seconds before it may be used again. Zero means it is the enemy's basic
    #: attack and runs on the archetype's attack interval instead.
    cooldown: float = 0.0

    #: What it does, in one line. This is the design, not flavour text.
    note: str = ""

    @property
    def is_basic_attack(self) -> bool:
        return self.cooldown <= 0.0

    def cycle_seconds(self, kind: Archetype) -> float:
        """The interval a telegraph for this ability is measured against.

        The design document's rule: an ability on a cooldown is telegraphed
        against its own cooldown, and a basic attack against the archetype's
        attack interval.
        """
        return kind.attack_interval if self.is_basic_attack else self.cooldown


def largest_telegraphed_radius(seconds: float) -> float:
    """The biggest marker whose wind-up fits inside half of `seconds`.

    The Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md` states the
    wind-up as 0.4 + Radius / 3.5 seconds and requires it to fit inside half the
    cycle. Rearranged, that is what this returns. It can be negative, which means
    the cycle is too short for any marker at all.
    """
    return WALK_OUT_SPEED * (seconds / 2.0 - REACTION_ALLOWANCE)


def is_telegraphed(ability: Ability, kind: Archetype | str) -> bool:
    """Whether this ability gets a ground marker, by the document's own rule."""
    kind = archetype(kind) if isinstance(kind, str) else kind
    return (largest_telegraphed_radius(ability.cycle_seconds(kind))
            >= SMALLEST_USEFUL_MARKER_METRES)


# --------------------------------------------------------------------------
# How many of a swarm can reach one player at once
# --------------------------------------------------------------------------
#
# This is the Imp's design and it is pure geometry. Bodies cannot overlap, so
# they queue in rings around the player. The count that fits in a ring is set by
# how much of the circle each body covers, and the number of rings that can
# reach is set by the enemy's attack reach.

def ring_distance(kind: Archetype | str, ring: int) -> float:
    """Centre-to-centre distance from the player to the ring-th rank, in metres.

    Ring 0 is in contact with the player, so it sits one player radius plus one
    body radius out. Each ring beyond that is one body diameter further.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    if ring < 0:
        raise ValueError(f"ring must be 0 or more, not {ring}")
    return PLAYER_BODY_RADIUS + kind.body_radius * (1 + 2 * ring)


def ring_capacity(kind: Archetype | str, ring: int) -> int:
    """How many of this creature fit shoulder to shoulder in that ring.

    A body of radius r standing at distance D from the centre covers an angle of
    2 * arcsin(r / D), so the whole circle holds pi / arcsin(r / D) of them.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    distance = ring_distance(kind, ring)
    return math.floor(math.pi / math.asin(min(1.0, kind.body_radius / distance)))


def attackers_within_reach(kind: Archetype | str, reach: float) -> int:
    """How many can hit one player at once, given the enemy's attack reach.

    Reach is measured centre to centre, which is how the engine measures it:
    `ACataclysmEnemyController::Think` compares `FVector::Dist` between the two
    actors' locations against `AttackReachCm`.

    This is the cap on a swarm. There is no attack-token rule anywhere in this
    project and there is deliberately none: the design document states that ten
    Imps kill a geared character in 4.9 seconds and twenty in 2.4, and a token
    rule that let only two or three swing at a time would make both figures
    false.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    total = 0
    ring = 0
    while ring_distance(kind, ring) <= reach:
        total += ring_capacity(kind, ring)
        ring += 1
    return total


def reach_for_rings(kind: Archetype | str, rings: int) -> float:
    """The attack reach that lets exactly that many ranks hit, in metres."""
    return ring_distance(kind, rings - 1)


# --------------------------------------------------------------------------
# The designed enemies
# --------------------------------------------------------------------------

#: How far each enemy can hit from, centre to centre, in metres. Only enemies
#: with a designed ability list appear. The engine holds this as `MeleeReachCm`
#: on `ACataclysmEnemyCharacter`, in centimetres.
ATTACK_REACH: dict[str, float] = {
    # 1.32 metres is exactly the second rank's distance: 0.42 for the player's
    # own body, plus three Imp radii. Set there and not by eye, because it is
    # what makes the design document's pack figures true. One rank of Imps is 7
    # creatures, and 7 of them take 6.9 seconds to kill a geared character; the
    # document says ten take 4.9 and twenty take 2.4, so a reach that let only
    # the front rank swing would contradict its own numbers. Issue #348.
    "Imp": 1.32,
}

#: The pack an enemy arrives in. Only swarming enemies have one.
PACK_SIZE: dict[str, int] = {
    # Ten. The design document already names ten as the pack that kills a geared
    # character in 4.9 seconds, so this is its number rather than a new one. It
    # is three more than one full rank, which is what makes the second rank --
    # and therefore the reach above -- do something in an ordinary encounter.
    "Imp": 10,
}

ABILITIES: dict[str, tuple[Ability, ...]] = {
    "Imp": (
        Ability(
            name="Rend",
            shape="Strike",
            params={"Radius": 1.32, "Angle": 90, "MaxTargets": 1},
            cooldown=0.0,
            note="A claw swipe at whatever it is standing next to. Its radius "
                 "is its attack reach, so the front two ranks of a pack both "
                 "connect.",
        ),
    ),
}


def abilities(name: str) -> tuple[Ability, ...]:
    """Everything the named enemy can do.

    Raises for an enemy whose design issue is still open, rather than returning
    an empty tuple, so an undesigned enemy cannot be mistaken for one that
    deliberately does nothing.
    """
    if name not in ARCHETYPES:
        raise ValueError(
            f"unknown archetype {name!r}; expected one of {sorted(ARCHETYPES)}")
    if name not in ABILITIES:
        raise ValueError(
            f"{name} has no designed abilities yet. The enemy design epic #29 "
            "is split one enemy at a time and only the Imp (#348) is done.")
    return ABILITIES[name]


def _check_every_ability_uses_a_real_shape() -> None:
    """A shape name outside the seven is a shape nothing will execute."""
    for name, entries in ABILITIES.items():
        for ability in entries:
            assert ability.shape in SHAPES, (
                f"{name}'s {ability.name} has shape {ability.shape!r}, which is "
                f"not one of the seven: {list(SHAPES)}")


def _check_every_parameter_belongs_to_its_shape() -> None:
    """A key the shape does not read is silently ignored by whatever runs it."""
    for name, entries in ABILITIES.items():
        for ability in entries:
            allowed = set(SHAPE_PARAMS[ability.shape]) | set(RIDERS)
            unknown = sorted(set(ability.params) - allowed)
            assert not unknown, (
                f"{name}'s {ability.name} is a {ability.shape} carrying "
                f"{unknown}, which that shape does not read. It reads "
                f"{list(SHAPE_PARAMS[ability.shape])}, plus the riders "
                f"{list(RIDERS)}.")


def _check_every_designed_enemy_has_exactly_one_basic_attack() -> None:
    """Two basic attacks means nothing decides which one runs on the interval,
    and none means the creature stands there."""
    for name, entries in ABILITIES.items():
        basics = [a for a in entries if a.is_basic_attack]
        assert len(basics) == 1, (
            f"{name} has {len(basics)} abilities with no cooldown. Exactly one "
            "is its basic attack, which runs on its attack interval.")


def _check_reach_and_pack_are_only_set_for_designed_enemies() -> None:
    """A reach or a pack size for an enemy with no abilities is a half-design
    that would read as finished."""
    for table, label in ((ATTACK_REACH, "ATTACK_REACH"),
                         (PACK_SIZE, "PACK_SIZE")):
        extra = sorted(set(table) - set(ABILITIES))
        assert not extra, (
            f"{label} has entries for {extra}, which have no designed "
            "abilities. Design the enemy in ABILITIES or remove the entry.")


_check_every_ability_uses_a_real_shape()
_check_every_parameter_belongs_to_its_shape()
_check_every_designed_enemy_has_exactly_one_basic_attack()
_check_reach_and_pack_are_only_set_for_designed_enemies()


if __name__ == "__main__":
    import sys

    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    print("Designed enemy abilities. Issue #29, split per enemy.")
    print()
    for name, entries in ABILITIES.items():
        kind = archetype(name)
        print(f"{name} -- {kind.role}")
        for ability in entries:
            cycle = ability.cycle_seconds(kind)
            gate = ("attack interval" if ability.is_basic_attack
                    else "own cooldown")
            print(f"    {ability.name:<12} {ability.shape:<11} "
                  f"{ability.params}")
            print(f"    {'':12} every {cycle:.1f}s ({gate}), largest marker it "
                  f"could telegraph {largest_telegraphed_radius(cycle):.1f} m, "
                  f"telegraphed: {is_telegraphed(ability, kind)}")
        print()

    print("How many can reach one player at once:")
    print()
    print(f"    {'enemy':<10} {'body':>6} {'reach':>7} {'rank':>5} "
          f"{'distance':>9} {'fits':>5} {'total':>6}")
    print("    " + "-" * 54)
    for name in ABILITIES:
        kind = archetype(name)
        reach = ATTACK_REACH[name]
        total = 0
        ring = 0
        while ring_distance(kind, ring) <= reach:
            total += ring_capacity(kind, ring)
            print(f"    {name if ring == 0 else '':<10} "
                  f"{kind.body_radius if ring == 0 else '':>6} "
                  f"{reach if ring == 0 else '':>7} {ring:>5} "
                  f"{ring_distance(kind, ring):>8.2f}m "
                  f"{ring_capacity(kind, ring):>5} {total:>6}")
            ring += 1
        print(f"    {'':10} {'':6} {'':7} pack of {PACK_SIZE[name]}, "
              f"cap of {total}")
