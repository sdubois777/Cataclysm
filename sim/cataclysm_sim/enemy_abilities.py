"""What each enemy DOES, in the same vocabulary a player skill is written in.

WHAT THIS IS FOR. `enemy_stats.py` gives every enemy health, damage, armour,
attack interval, movement speed, evasion and resistance. None of that says what
an enemy does with its turn. Issue #29 is the epic that asks, and it is split one
enemy at a time: #348 the Imp, #349 the Succubus, #350 the Hellhound, #351 the
Brute, #352 the Corrupted Sentinel, #353 the Abyssal Warden, #354 the Gatekeeper.

**Only the Imp, the Succubus, the Hellhound and the Brute are filled in.** The
other three are open issues. An archetype with no entry here has no designed abilities yet,
and asking for one raises rather than returning an empty list, so a missing
design cannot be mistaken for a finished one.

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
#:
#: THIS IS A COPY. `SHAPE_PARAMS` in `tools/generate_datatables.py` is the same
#: table and is what validates the player skill sheet. The two are checked
#: against each other by `tools/tests/test_enemy_abilities.py`, because a copy
#: in this project has silently drifted before.
SHAPE_PARAMS: dict[str, tuple[str, ...]] = {
    "Strike": ("Radius", "Angle", "MaxTargets", "Duration", "Interval",
               "Knockback"),
    "Projectile": ("Range", "Radius", "Pierce", "Returns", "Speed"),
    "SelfBuff": ("Duration", "Radius", "IncreasePerBurning"),
    "Movement": ("Mode", "Range", "Radius"),
    "Summon": ("Range", "Radius", "Count", "MaxActive", "Duration", "Interval"),
    "Aura": ("Radius", "Duration", "Interval"),
    "Debuff": ("Range", "Radius", "MaxTargets", "Duration"),
}

#: Riders any shape may carry, from the same section of the design document.
#: Also a copy of `SHAPE_RIDERS` in `tools/generate_datatables.py`.
RIDERS = ("GroundRadius", "GroundDuration", "GroundHitsAllies", "Burn",
          "Effect", "StunSeconds", "FinalHitPercent", "HealthCostPercent")

#: How long a target is immune to being stunned again, from the anti-stun-lock
#: rule in section VI of `docs/Cataclysm_GDD_v2.md`. Any ability that stuns has
#: to sit at least this far apart, whatever slot it is in, or it spends its uses
#: on a target that cannot be stunned.
STUN_IMMUNITY_WINDOW = 5.0

#: The longest stun any designed player skill grants, Shield Bash's. An
#: enemy ability that held the player still for longer than the player's own
#: best hold would be the failure the anti-stun-lock section is written
#: against. The four skills that stun run 0.75, 0.75, 1.0 and 1.5 seconds.
LONGEST_DESIGNED_STUN = 1.5

#: The three values a Movement shape's `Mode` may take.
MOVEMENT_MODES = ("Leap", "Charge", "Blink")

#: The seven slots in `game/Data/SkillSlots.csv`. An enemy ability declares one
#: for the same reason a player skill does: the slot says what kind of thing it
#: is and, for everything except Basic, what its cooldown band is. Only Basic
#: runs on the archetype's attack interval; only Aura is held on with no
#: cooldown at all.
SLOTS = ("Basic", "Heavy", "Special", "Support", "Aura", "Ultimate", "Movement")

#: The four shapes the Attack Telegraphs subsection of the design document gives
#: a ground marker to. An ability in one of the other three -- SelfBuff, Summon,
#: Debuff -- has no marker to draw, so it is read off the caster's animation and
#: answered by interrupting rather than by walking out of it.
TELEGRAPHED_SHAPES = ("Strike", "Projectile", "Aura", "Movement")

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

    #: Which of the seven slots in `game/Data/SkillSlots.csv` it behaves like.
    #: Basic runs on the archetype's attack interval; Aura is held on while the
    #: creature lives; everything else runs on `cooldown`.
    slot: str
    params: dict[str, float | str] = field(default_factory=dict)

    #: Seconds before it may be used again. Zero for a Basic attack, which runs
    #: on the archetype's attack interval, and for an Aura, which is held on.
    cooldown: float = 0.0

    #: What it does, in one line. This is the design, not flavour text.
    note: str = ""

    @property
    def is_basic_attack(self) -> bool:
        return self.slot == "Basic"

    @property
    def is_held_on(self) -> bool:
        """An Aura is on for as long as the creature is alive. Killing it is
        what turns the effect off, which is the whole point of a support enemy.
        """
        return self.slot == "Aura"

    def cycle_seconds(self, kind: Archetype) -> float:
        """The interval a telegraph for this ability is measured against.

        The design document's rule: an ability on a cooldown is telegraphed
        against its own cooldown, and a basic attack against the archetype's
        attack interval. An ability that is held on has no cycle, so it returns
        zero and is never telegraphed.
        """
        if self.is_basic_attack:
            return kind.attack_interval
        return 0.0 if self.is_held_on else self.cooldown


def largest_telegraphed_radius(seconds: float) -> float:
    """The biggest marker whose wind-up fits inside half of `seconds`.

    The Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md` states the
    wind-up as 0.4 + Radius / 3.5 seconds and requires it to fit inside half the
    cycle. Rearranged, that is what this returns. It can be negative, which means
    the cycle is too short for any marker at all.
    """
    return WALK_OUT_SPEED * (seconds / 2.0 - REACTION_ALLOWANCE)


def is_telegraphed(ability: Ability, kind: Archetype | str) -> bool:
    """Whether this ability gets a ground marker, by the document's own rules.

    Three conditions, all from the Attack Telegraphs subsection.

    Its marker table covers four of the seven shapes, so an ability in one of
    the other three has no marker to draw at all and is read off the caster
    instead.

    The ability's OWN marker has to be at least a metre across. "A marker
    smaller than 1 metre is not a telegraph. It is smaller than the creature
    standing in it, so there is nowhere to walk." That applies to the marker an
    ability actually draws, not only to the largest one its cycle would allow.
    The Brute is the case that shows the difference: its 2.8 second attack
    interval puts it in the telegraph table's Yes column, and its ordinary slam
    still gets no marker because a slam reaches 0.9 metres.

    And the wind-up for that marker has to fit inside half the cycle.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    if ability.shape not in TELEGRAPHED_SHAPES:
        return False
    if float(ability.params.get("Radius", 0.0)) < SMALLEST_USEFUL_MARKER_METRES:
        return False
    return (largest_telegraphed_radius(ability.cycle_seconds(kind))
            >= SMALLEST_USEFUL_MARKER_METRES)


#: The largest marker an ability may have if it is escaped with a Movement skill
#: rather than by walking, and the cooldown that tier requires. Both from the
#: Attack Telegraphs subsection: 8 metres is the shortest Movement-shape skill
#: range and 5 seconds is the Movement slot's cooldown.
MOVEMENT_ESCAPE_CAP_METRES = 8.0
MOVEMENT_ESCAPE_MINIMUM_COOLDOWN = 5.0


def fits_its_cycle(ability: Ability, kind: Archetype | str) -> bool:
    """Whether a telegraphed ability's marker is small enough for its cycle.

    Eight metres is an absolute ceiling. The design document says of it that
    "anything above 8 metres cannot be escaped by any means the player has,
    which makes it a damage event rather than a telegraph", and that is a
    statement about the player's reach rather than about any one tier.

    Under that ceiling there are two tiers and passing either is enough. A
    marker the player walks out of has to fit inside half the cycle. A larger
    one is legal only on a cooldown of at least 5 seconds, because that is what
    the Movement slot can answer.

    An ability that is not telegraphed has no marker and so no limit.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    if not is_telegraphed(ability, kind):
        return True
    radius = float(ability.params.get("Radius", 0.0))
    if radius > MOVEMENT_ESCAPE_CAP_METRES:
        return False
    if radius <= largest_telegraphed_radius(ability.cycle_seconds(kind)):
        return True
    return ability.cooldown >= MOVEMENT_ESCAPE_MINIMUM_COOLDOWN


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

    # 8 metres, which is the shortest Movement-shape skill range in
    # game/Data/WeaponSkills.csv -- the Sword's charge and the Axe's leap. The
    # Attack Telegraphs subsection already uses that figure as the furthest a
    # player can be made to travel, and a ranged enemy standing beyond it could
    # not be closed on by every build. It is also the distance the Succubus
    # holds at, because it neither advances nor retreats. Issue #349.
    "Succubus": 8.0,

    # Contact and no further: 0.42 for the player's body plus its own 0.48. A
    # Hellhound is more than half again as wide as an Imp, so only five fit
    # around one player where twenty Imps do. Its threat is the charge rather
    # than the mass. Issue #350.
    "Hellhound": 0.90,

    # The same contact reach, and for the same reason. The Brute's threat is
    # the stomp and its weakness is that it can be got behind, neither of which
    # is about reach. Issue #351.
    "Brute": 0.90,
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
            slot="Basic",
            params={"Radius": 1.32, "Angle": 90, "MaxTargets": 1},
            note="A claw swipe at whatever it is standing next to. Its radius "
                 "is its attack reach, so the front two ranks of a pack both "
                 "connect.",
        ),
    ),
    "Succubus": (
        Ability(
            name="Soulfire",
            shape="Projectile",
            slot="Basic",
            # Radius 3.15 is the largest marker a 2.6 second attack interval
            # allows, so its wind-up is exactly half the interval. The Succubus
            # takes all of it because "slow but powerful" has to mean something
            # the player can see. Speed 1200 is the slowest player projectile in
            # game/Data/WeaponSkills.csv, Magma Quake's; a slow bolt is a
            # readable one, and the marker is on the ground before it flies.
            params={"Range": 8, "Radius": 3.15, "Speed": 1200},
            note="A slow bolt of demonic fire, marked on the ground for 1.3 "
                 "seconds first. The largest telegraph any ordinary Demonic "
                 "enemy produces except the Brute's.",
        ),
        Ability(
            name="Wither the Living",
            shape="Debuff",
            slot="Support",
            # Withered Touch is chosen from game/Data/StatusEffects.csv rather
            # than invented, and it is one of the debuffs the enemy modifier
            # pool has NOT already claimed. 5 seconds is the duration both of
            # the enemy-applied debuffs in that table that state one use.
            params={"Range": 8, "MaxTargets": 1, "Duration": 5,
                    "Effect": "Withered Touch"},
            # Twice the duration, so the player has as long without it as with
            # it. 10 seconds is also the top of the Support slot's cooldown band
            # in game/Data/SkillSlots.csv, which is the slot curses live in.
            cooldown=10.0,
            note="Reduces the player's damage and energy shield for 5 seconds. "
                 "No ground marker: the telegraph table draws four shapes and "
                 "Debuff is not one, so this is read off the caster and "
                 "answered by interrupting it.",
        ),
        Ability(
            name="Dominion",
            shape="Aura",
            slot="Aura",
            # No Duration, so it is a toggle held on while the creature lives --
            # the design document's own rule for the Aura shape. Radius is the
            # Succubus's own attack range, because that is how far from the
            # fight it stands, so a smaller one would buff nothing at the moment
            # it matters and a larger one would buff a fight it is not in.
            params={"Radius": 8, "Effect": "Commander"},
            note="Every allied enemy within 8 metres gains 20% increased "
                 "stats, for as long as the Succubus is alive. Killing it "
                 "first is the correct play, and this is what makes that true.",
        ),
    ),
    "Hellhound": (
        Ability(
            name="Maul",
            shape="Strike",
            slot="Basic",
            # Contact reach, so exactly one rank of five can bite at once.
            params={"Radius": 0.9, "Angle": 90, "MaxTargets": 1, "Burn": 1},
            note="A bite at whatever it is standing against. Not telegraphed: "
                 "a 1.1 second attack interval allows a 0.5 metre marker, "
                 "which is smaller than the animal standing in it.",
        ),
        Ability(
            name="Hellrush",
            shape="Movement",
            slot="Movement",
            # Radius 1.5 is the narrowest corridor any player Charge-mode skill
            # uses, Flamedart's, so the marker is a lane to step out of rather
            # than a wall. Range 10 is what three of the four player charges
            # use, and it is more than the 6.2 metres the Hellhound could cover
            # by simply walking during the 0.83 second wind-up -- which is the
            # test a charge has to pass to be worth having at all.
            #
            # The trail is the same three riders Flamedart carries, plus the one
            # thing that is new in the whole slice: GroundHitsAllies.
            params={"Mode": "Charge", "Range": 10, "Radius": 1.5, "Burn": 1,
                    "GroundRadius": 1.5, "GroundDuration": 4,
                    "GroundHitsAllies": 1},
            # The Movement slot's typical cooldown in game/Data/SkillSlots.csv.
            cooldown=5.0,
            note="Charges in a straight line fixed when the wind-up starts, "
                 "burning everything it passes and leaving that lane on fire "
                 "for 4 seconds. The fire burns other enemies and the "
                 "Hellhound itself.",
        ),
    ),
    "Brute": (
        Ability(
            name="Slam",
            shape="Strike",
            slot="Basic",
            # 0.9 metres is below the one metre floor for a marker, so this gets
            # no telegraph even though the Brute's 2.8 second attack interval
            # puts it in the telegraph table's Yes column. That column says how
            # big a marker it COULD draw, not that everything it does draws one.
            params={"Radius": 0.9, "Angle": 90, "MaxTargets": 1},
            note="A swing at whatever is in front of it. It does not stun: an "
                 "ordinary Brute hit lands at exactly 10% of the reference "
                 "build's effective health, which is exactly the stun damage "
                 "threshold, so a stun on it would be a coin flip.",
        ),
        Ability(
            name="Stomp",
            shape="Strike",
            slot="Heavy",
            # Radius 3.5 gives a wind-up of 0.4 + 3.5 / 3.5 = 1.4 seconds.
            #
            # IT IS SIZED AGAINST ITS OWN COOLDOWN, NOT THE ATTACK INTERVAL,
            # which is the general rule for any ability that has a cooldown:
            # see the telegraph section of docs/Cataclysm_GDD_v2.md. The 5
            # second cooldown allows 7.35 metres and this deliberately takes
            # half of it, because the Brute gets the walk-out kind of telegraph
            # and a marker that large on a creature this slow is unmissable
            # rather than dodgeable.
            #
            # THIS COMMENT USED TO SAY THE RADIUS WAS THE LARGEST THE ATTACK
            # INTERVAL ALLOWED, and that the 1.4 second wind-up was exactly half
            # of the then 2.8 second interval. Both were true and neither was
            # the reason: the stomp never ran on the attack interval. When the
            # interval moved to 1.6 on 2026-08-07, because 2.8 played as too
            # slow to be a threat, nothing here needed to change.
            #
            # Angle 360 because a stomp is a ring at its feet, and that is what
            # stops the answer to a Brute being "stand behind it and ignore the
            # marker" once its turn rate is halved.
            #
            # 1.5 seconds of stun is the longest any designed player skill
            # grants -- Shield Bash's. An enemy's hold should not be longer than
            # the best one the player has.
            params={"Radius": 3.5, "Angle": 360, "StunSeconds": 1.5},
            # The stun immunity window, NOT the Heavy slot's cooldown. The whole
            # Heavy band in game/Data/SkillSlots.csv is 1 to 4 seconds, which is
            # inside the 5 second window, so a Brute stomping on the Heavy
            # cadence would spend most of its stomps on a target that cannot be
            # stunned.
            cooldown=STUN_IMMUNITY_WINDOW,
            note="A ring at its feet, marked for 1.4 seconds, stunning for 1.5. "
                 "At the Heavy slot's 250% it lands at 25% of the reference "
                 "build's effective health, which clears the 10% stun damage "
                 "threshold two and a half times over.",
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


def _check_every_ability_declares_a_real_slot() -> None:
    """A slot outside the seven has no cooldown band and no meaning."""
    for name, entries in ABILITIES.items():
        for ability in entries:
            assert ability.slot in SLOTS, (
                f"{name}'s {ability.name} is in slot {ability.slot!r}, which is "
                f"not one of the seven: {list(SLOTS)}")


def _check_every_designed_enemy_has_exactly_one_basic_attack() -> None:
    """Two basic attacks means nothing decides which one runs on the interval,
    and none means the creature stands there."""
    for name, entries in ABILITIES.items():
        basics = [a for a in entries if a.is_basic_attack]
        assert len(basics) == 1, (
            f"{name} has {len(basics)} abilities in the Basic slot. Exactly one "
            "is its basic attack, which runs on its attack interval.")


def _check_only_the_held_on_and_basic_abilities_lack_a_cooldown() -> None:
    """Everything else would fire every time the brain looked at it.

    A Basic attack is paced by the archetype's attack interval and an Aura is
    held on, so those two are the only ones a zero means something for.
    """
    for name, entries in ABILITIES.items():
        for ability in entries:
            paced = ability.is_basic_attack or ability.is_held_on
            assert paced or ability.cooldown > 0.0, (
                f"{name}'s {ability.name} is in the {ability.slot} slot with no "
                "cooldown, so nothing paces it. Only Basic and Aura may leave "
                "it at zero.")
            assert not paced or ability.cooldown == 0.0, (
                f"{name}'s {ability.name} is in the {ability.slot} slot and "
                f"also carries a cooldown of {ability.cooldown}. A Basic attack "
                "is paced by the attack interval and an Aura is held on, so "
                "neither reads a cooldown.")


def _check_every_movement_ability_names_a_real_mode() -> None:
    """Mode is the one Movement parameter whose value is a name. A typo would be
    read as no mode at all."""
    for name, entries in ABILITIES.items():
        for ability in entries:
            if ability.shape != "Movement":
                continue
            mode = ability.params.get("Mode")
            assert mode in MOVEMENT_MODES, (
                f"{name}'s {ability.name} is a Movement in mode {mode!r}, "
                f"which is not one of {list(MOVEMENT_MODES)}")


def _check_every_telegraphed_marker_fits_its_cycle() -> None:
    """A marker too big for its cycle cannot be escaped, which by the design
    document's own words makes it a damage event rather than a telegraph."""
    for name, entries in ABILITIES.items():
        kind = archetype(name)
        for ability in entries:
            assert fits_its_cycle(ability, kind), (
                f"{name}'s {ability.name} draws a "
                f"{ability.params.get('Radius')} m marker on a "
                f"{ability.cycle_seconds(kind)} s cycle, which allows at most "
                f"{largest_telegraphed_radius(ability.cycle_seconds(kind)):.2f}"
                " m. Nothing larger can be walked out of, and the Movement "
                "skill tier needs a cooldown of at least "
                f"{MOVEMENT_ESCAPE_MINIMUM_COOLDOWN} s and a radius of at most "
                f"{MOVEMENT_ESCAPE_CAP_METRES} m.")


def _check_every_stun_is_spaced_by_the_immunity_window() -> None:
    """An ability that stuns more often than every 5 seconds wastes its uses.

    The anti-stun-lock rule makes a target immune for 5 seconds after a stun,
    so a second stun inside that window lands on something that cannot be
    stunned. This is a slot-independent constraint: the Heavy slot's whole
    cooldown band sits inside the window.
    """
    for name, entries in ABILITIES.items():
        for ability in entries:
            if "StunSeconds" not in ability.params:
                continue
            assert ability.cooldown >= STUN_IMMUNITY_WINDOW, (
                f"{name}'s {ability.name} stuns and comes round every "
                f"{ability.cooldown} s, inside the {STUN_IMMUNITY_WINDOW} s "
                "stun immunity window, so most of its uses would stun nothing.")


def _check_no_stun_outlasts_the_longest_the_player_has() -> None:
    """1.5 seconds is Shield Bash's, the longest any designed skill grants. An
    enemy holding the player still for longer than the player's own best hold
    is the failure the anti-stun-lock section is written against."""
    for name, entries in ABILITIES.items():
        for ability in entries:
            seconds = float(ability.params.get("StunSeconds", 0.0))
            assert seconds <= LONGEST_DESIGNED_STUN, (
                f"{name}'s {ability.name} stuns for {seconds} s, longer than "
                f"the {LONGEST_DESIGNED_STUN} s of the longest stun any "
                "designed player skill grants.")


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
_check_every_ability_declares_a_real_slot()
_check_every_designed_enemy_has_exactly_one_basic_attack()
_check_only_the_held_on_and_basic_abilities_lack_a_cooldown()
_check_every_movement_ability_names_a_real_mode()
_check_every_telegraphed_marker_fits_its_cycle()
_check_every_stun_is_spaced_by_the_immunity_window()
_check_no_stun_outlasts_the_longest_the_player_has()
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
            if ability.is_basic_attack:
                gate = "attack interval"
            elif ability.is_held_on:
                gate = "held on while it lives"
            else:
                gate = "own cooldown"
            print(f"    {ability.name:<18} {ability.slot:<8} "
                  f"{ability.shape:<11} {ability.params}")
            print(f"    {'':18} every {cycle:.1f}s ({gate}), largest marker it "
                  f"could telegraph {largest_telegraphed_radius(cycle):.2f} m, "
                  f"telegraphed: {is_telegraphed(ability, kind)}")
        print()

    print("How many can reach one player at once:")
    print()
    print(f"    {'enemy':<10} {'body':>6} {'reach':>7} {'rank':>5} "
          f"{'distance':>9} {'fits':>5} {'total':>6}")
    print("    " + "-" * 54)
    for name in PACK_SIZE:
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
