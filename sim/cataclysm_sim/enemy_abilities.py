"""What each enemy DOES, in the same vocabulary a player skill is written in.

WHAT THIS IS FOR. `enemy_stats.py` gives every enemy health, damage, armour,
attack interval, movement speed, evasion and resistance. None of that says what
an enemy does with its turn. Issue #29 is the epic that asks, and it is split one
enemy at a time: #348 the Imp, #349 the Succubus, #350 the Hellhound, #351 the
Brute, #352 the Corrupted Sentinel, #353 the Abyssal Warden, #354 the Gatekeeper.

**All seven are filled in: the Imp, the Succubus, the Hellhound, the Brute, the
Corrupted Sentinel, the Abyssal Warden and the Gatekeeper.** An archetype with no
entry here has no designed abilities yet,
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

#: The eight shapes from `docs/Cataclysm_GDD_v2.md` section V, spelled the way
#: the `Shape` column of `game/Data/WeaponSkills.csv` spells them.
#:
#: `Deployable` was the eighth, added with issue #338. No enemy ability uses it,
#: and it is listed here anyway because this tuple has to hold every shape the
#: skill data uses: a test compares the two and fails when they drift apart.
SHAPES = ("Strike", "Projectile", "SelfBuff", "Movement", "Summon", "Deployable",
          "Aura", "Debuff")

#: Which parameter names each shape reads. Same names and same meanings as the
#: player skill rows use. A key outside its shape's list is a typo that would be
#: read as nothing by whatever executes the shape.
#:
#: THIS IS A COPY. `SHAPE_PARAMS` in `tools/generate_datatables.py` is the same
#: table and is what validates the player skill sheet. The two are checked
#: against each other by `tools/tests/test_enemy_abilities.py`, because a copy
#: in this project has silently drifted before.
#:
#: A PROJECTILE STATES `Speed` OR `Arc`, NOT BOTH. `Speed` is centimetres per
#: second and describes something travelling flat, which is every one of the 398
#: player projectile rows. `Arc` describes a LOB following real projectile motion
#: onto the point it was aimed at: steady speed across the ground and a descent
#: that accelerates. It is how high the shot rises above the straight line to
#: where it lands, as a fraction of the distance thrown, so the shape holds at
#: every range.
#:
#: A LOB HAS NO SINGLE SPEED TO STATE, because it is slowest at the top of its
#: arc and fastest as it lands. Issue #465 established that and stated a FLIGHT
#: TIME instead. Issue #474 replaced the flight time with this, because gravity
#: and a fixed time together fix the whole vertical part of the trajectory
#: whatever the distance, so every short lob became a near-vertical mortar. The
#: flight time is now derived: a parabola sags `g * t * t / 8` below its own
#: chord, so an arc of `Arc * range` is in the air for
#: `sqrt(8 * Arc * range / g)`.
SHAPE_PARAMS: dict[str, tuple[str, ...]] = {
    "Strike": ("Radius", "Angle", "MaxTargets", "Duration", "Interval"),
    "Projectile": ("Range", "Radius", "Pierce", "Returns", "Speed", "Arc"),
    "SelfBuff": ("Duration", "Radius", "IncreasePerBurning"),
    "Movement": ("Mode", "Range", "Radius"),
    "Summon": ("Range", "Radius", "Count", "MaxActive", "Duration", "Interval",
               "Minions"),
    "Deployable": ("Range", "Radius", "Count", "MaxActive", "Duration",
                   "Interval", "Minions", "HealthPercent"),
    "Aura": ("Radius", "Duration", "Interval"),
    "Debuff": ("Range", "Radius", "MaxTargets", "Duration"),
}

#: Riders any shape may carry, from the same section of the design document.
#: Also a copy of `SHAPE_RIDERS` in `tools/generate_datatables.py`.
#:
#: `Knockback` MOVED HERE FROM `Strike` on 2026-08-15, issue #626. Displacement
#: is not specific to one kind of skill: a strike, a leap, a charge and an enemy
#: slam can all shove. While it was a Strike parameter, Shockwave Leap knocked
#: back in its prose and could not say so in its data, and two of the three enemy
#: abilities that will displace the player are charges, which are Movement.
#: `GroundHitsAllies` IS IN THE VOCABULARY AND NO ABILITY USES IT. The
#: Hellhound's trail and the Gatekeeper's Soulfall both carried it until
#: 2026-08-20, when the project owner set the rule that a creature does not
#: burn itself or its own side. The word is kept rather than deleted, by the
#: owner's choice, so that the option is on the record as considered and
#: rejected rather than never thought of.
#: `test_nothing_burns_its_own_side` refuses an ability that sets it.
RIDERS = ("GroundRadius", "GroundDuration", "GroundPercent", "GroundHitsAllies",
          "Burn", "Effect", "StunSeconds", "FinalHitPercent",
          "HealthCostPercent", "Knockback")

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

    #: The first phase this ability is available from. 1 for every ability of
    #: every single-phase enemy, which is six of the seven.
    #:
    #: WHAT A PHASE IS ALLOWED TO OWN, AND IT IS ONLY THIS. Research across ten
    #: bosses in Path of Exile 1 and 2 and Last Epoch (recorded with issue #354
    #: in docs/DECISIONS.md) found not one that gains damage, armour, attack
    #: speed or crit at a phase transition. Escalation is done by adding a named
    #: ability or using an existing one more often. So a phase selects WHICH
    #: abilities are in the rotation and nothing else, and the two-layer rule --
    #: rarity scales magnitude, archetype sets behaviour -- survives a
    #: multi-phase boss untouched. An ability available from phase N stays
    #: available in every later phase; phases add, they do not take away.
    phase: int = 1

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
    The Brute used to be the case that showed the difference, and no longer is.
    Its attack interval has been shortened twice by play testing -- 2.8 to 1.6
    on 2026-08-07, and 1.6 to 1.2 on 2026-08-09 -- and the marker its cycle
    allows shrank with it each time: 3.50 metres, then 1.40, and now 0.70. At
    0.70 the interval is below the one metre floor on its own, so the Brute's
    ordinary slam now fails BOTH conditions rather than only the one about its
    own radius. The distinction this paragraph draws is still real; the Brute is
    simply no longer an example of it.

    And the wind-up for that marker has to fit inside half the cycle.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    if ability.shape not in TELEGRAPHED_SHAPES:
        return False
    if float(ability.params.get("Radius", 0.0)) < SMALLEST_USEFUL_MARKER_METRES:
        return False
    return (largest_telegraphed_radius(ability.cycle_seconds(kind))
            >= SMALLEST_USEFUL_MARKER_METRES)


#: The longest any telegraph may warn for, in seconds.
#:
#: THIS IS THE ONE NUMBER IN THE TELEGRAPH RULES THAT NOTHING DERIVES, and it is
#: a judgement rather than a calculation. It was chosen on 2026-08-09 and
#: `docs/DECISIONS.md` records why: 2.0 seconds is already the longest telegraph
#: in the game, so adopting it as the ceiling changed no existing ability, and no
#: shipped game in the genre publishes a telegraph duration to check it against.
#:
#: WHAT IT IS FOR. Without a ceiling the wind-up formula gives the player back
#: exactly as much ground as a bigger radius takes away, so the escape margin is
#: 2.3 metres at EVERY radius and a bigger marker is not harder to escape. That
#: property is stated in the Attack Telegraphs subsection of
#: `docs/Cataclysm_GDD_v2.md` and it is what made "make the ring bigger" a
#: request the rules could not satisfy. Above the radius at which the wind-up
#: reaches this ceiling the warning stops growing while the ground to cross keeps
#: growing, so the margin falls by one metre per metre of radius. That is what
#: makes radius mean difficulty.
MAXIMUM_WIND_UP_SECONDS = 2.0


def wind_up_seconds(radius: float) -> float:
    """How long a marker of this radius is on the ground before its attack lands.

    The Attack Telegraphs subsection of `docs/Cataclysm_GDD_v2.md` states the
    wind-up as 0.4 + Radius / 3.5 seconds, held to a ceiling of
    `MAXIMUM_WIND_UP_SECONDS`.
    """
    return min(REACTION_ALLOWANCE + radius / WALK_OUT_SPEED,
               MAXIMUM_WIND_UP_SECONDS)


def telegraph_cap_metres(kind: Archetype | str) -> float:
    """The largest marker this creature may draw, in metres.

    DERIVED, NOT CHOSEN. It is the radius at which the slowest class still has
    exactly the reaction allowance and not a moment more. During a wind-up of
    `MAXIMUM_WIND_UP_SECONDS` that class covers `WALK_OUT_SPEED` times it; a
    player standing at contact has to cross the radius less the contact
    distance; and requiring the difference to leave `REACTION_ALLOWANCE`
    seconds of walking spare rearranges to this.

    SO "EVERY CLASS CAN CLEAR EVERY TELEGRAPH WITH THE STATED REACTION
    ALLOWANCE" IS A PROPERTY OF THE RULES rather than something checked ability
    by ability. Above this radius the slowest class cannot both react and walk
    clear, which is the line the design document draws when it says an area that
    cannot be escaped "is a damage event rather than a telegraph".

    PER CREATURE, BECAUSE CONTACT IS PER CREATURE. A player stands at their own
    radius plus the creature's, so a wider creature holds its target further out
    and that target has less ground to cross. Every enemy that can telegraph
    today has the same 0.48 m body, so this is 6.50 m for all of them.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    contact = PLAYER_BODY_RADIUS + kind.body_radius
    return (WALK_OUT_SPEED * (MAXIMUM_WIND_UP_SECONDS - REACTION_ALLOWANCE)
            + contact)


def fits_its_cycle(ability: Ability, kind: Archetype | str) -> bool:
    """Whether an ability's radius is legal.

    TWO CONDITIONS, AND THEY ARE ASKED OF DIFFERENT SETS OF ABILITIES. The
    radius must be within `telegraph_cap_metres`, which is asked of almost
    everything; and the wind-up for that radius must fit inside half the cycle,
    which is asked only of an ability that draws a marker.

    THE CAP DOES NOT DEPEND ON A MARKER BEING DRAWN, since issue #500. The two
    conditions answer different questions. The half-cycle test asks "can the
    player clear this during the wind-up", which means nothing without a wind-up.
    The cap asks "can the player cross this at all", which does not care whether
    anything was drawn -- an untelegraphed nine metre ring is strictly worse than
    a telegraphed one, because it is unavoidable AND unannounced. Until #500 the
    cap sat after the telegraph test and so was skipped by every untelegraphed
    ability, which is exactly the dangerous set: an ability escapes telegraphing
    by being FAST, and a fast, huge, unannounced area is the thing the cap exists
    to forbid. No designed ability exploited it -- the six untelegraphed ones run
    0.90 to 2.00 metres against a 6.50 metre cap -- so this was a guard that did
    not guard rather than a live defect.

    AN ABILITY HELD ON FOR AS LONG AS THE CREATURE LIVES IS EXEMPT, BY DECISION.
    That is the Aura slot, and the Succubus's Dominion is the only one designed:
    an 8.00 metre field granting Commander to allies, which is over the cap. The
    cap is about a moment. It asks whether the player can be clear by the time an
    attack lands, and a field that is simply on has no moment it lands -- the
    player may walk out of it at any point, and the design's stated counter is
    killing the caster, which ends it instantly. Dominion's radius is also not a
    free number: the design document derives it from the Succubus's own 8 metre
    attack range, because a smaller field would buff nothing at the moment it
    matters.

    An Aura on a COOLDOWN is not exempt, and the distinction is deliberate. It
    fires at a moment like anything else, so it has a wind-up, draws a marker,
    and is capped. Nothing designed does this today.

    WHAT WENT AND WHY. Until 2026-08-09 there was a second tier for markers
    escaped with a Movement skill rather than by walking, with its own wind-up
    formula and a 5 second minimum cooldown. Issue #487 recorded that the tier
    was unreachable: the walk-out limit grows at 1.75 metres per second of
    cooldown while its 8 metre cap did not grow at all, so above a 5.36 second
    cooldown every legal radius was a walk-out radius, and no designed ability
    was ever in it. Measuring it also showed it would not have done its job: its
    escape margin was 13.7 metres at every radius against the walk-out tier's
    2.3, so it was between identical and twice as forgiving, never harder.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    radius = float(ability.params.get("Radius", 0.0))

    if not ability.is_held_on and radius > telegraph_cap_metres(kind):
        return False

    if not is_telegraphed(ability, kind):
        return True

    return (wind_up_seconds(radius)
            <= ability.cycle_seconds(kind) / 2.0)


def is_lobbed(ability: Ability) -> bool:
    """Whether this ability follows a real trajectory onto a marked circle.

    A Projectile states `Speed` or `Arc` and not both, so carrying `Arc` is
    what makes it a lob. See `SHAPE_PARAMS` above for why the two are
    exclusive.
    """
    return ability.shape == "Projectile" and "Arc" in ability.params


def lob_minimum_range(ability: Ability, kind: Archetype | str) -> float:
    """The nearest a lobbed attack may be aimed, in metres.

    THE RULE, from `docs/DECISIONS.md` under the 2026-08-09 heading "A lobbed
    attack will not be thrown at something standing against the creature": an
    attack that marks a circle must not mark the ground its own caster is
    standing on. Below `marked radius + caster body radius` the creature is
    inside the area it is about to hit, which makes the attack a melee attack
    wearing a thrown attack's telegraph.

    IT APPLIES TO A LOB AND NOT TO A FLAT SHOT. A flat Projectile's marker is a
    LANE running from the caster out to its range, so the caster stands at the
    lane's origin rather than inside a circle. That is why the Corrupted
    Sentinel's bolt has no minimum range and its mortar does.

    THE BRUTE'S 2.58 METRES IS THIS FUNCTION'S ANSWER FOR RIP AND TOSS, and the
    C++ carries it as `RockThrowMinimumRangeCm = 258.0f` in
    `game/Source/Cataclysm/Character/CataclysmBruteCharacter.h` with a
    `static_assert` beside its use. `tools/tests/test_the_rock_throw_minimum_range.py`
    holds that constant to this function, so the rule has one definition rather
    than a C++ copy and a Python copy that can drift apart.

    Raises for an ability that is not lobbed, because a number returned for a
    flat shot would be a limit somebody could apply by mistake.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    if not is_lobbed(ability):
        raise ValueError(
            f"{ability.name} is a {ability.shape} with no Arc, so it is not "
            "lobbed and has no minimum range. Only an attack that marks a "
            "circle can mark the ground its own caster stands on.")
    return float(ability.params["Radius"]) + kind.body_radius


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
    `ACataclysmEnemyController::Think` compares `FVector::Dist2D` between the
    two actors' locations against `AttackReachCm`.

    ON THE FLOOR PLANE, WHICH IS NOT A DETAIL. Capsule centres do not sit at the
    same height: a player's capsule half-height is 96 cm and a Brute's is 110.
    A 3D distance would charge a creature for a height difference nobody chose,
    and at contact that is 91.08 cm against a 90 cm reach. Issue #373.

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

    # 14 metres, WHICH IS THE LONGEST RANGE ANY PLAYER ATTACK REACHES.
    # Emberbolt on the wand and Hellbrand on the greatsword both state Range=14
    # in game/Data/WeaponSkills.csv, and nothing states more. Two Debuff and
    # Summon rows reach 15, and neither is an attack.
    #
    # IT GETS ALL OF IT BECAUSE REACH IS THE ONLY TOOL IT HAS. Its move_speed
    # is 0.0 at every rarity, so it cannot close a gap and cannot retreat from
    # one. At the Succubus's 8 metres any ranged build could stand at 9 and
    # kill it for nothing, which is the free kill issue #352 asks this design
    # to prevent. This is also the Brute's own rule generalised: there is no
    # distance at which an enemy is aware of the player and can do nothing.
    #
    # ITS NOTICE RADIUS MUST BE AT LEAST THIS, and no enemy has a designed
    # notice radius yet -- the Brute's 1000 cm was set by playing. Issue #383.
    # A notice radius below 14 metres would make this reach unreachable.
    # Issue #352.
    "Corrupted Sentinel": 14.0,

    # Contact and no further: 0.42 for the player's body plus its own 0.48. The
    # same figure the Brute and the Hellhound use, and for the same reason --
    # this creature's threat is the ring at its feet and its weakness is that it
    # cannot catch anybody, neither of which is about reach. Issue #353.
    "Abyssal Warden": 0.90,

    # 2.0 metres, which is Dread Cleave's own radius: this creature's ordinary
    # attack is a telegraphed cone rather than a contact swing, so its reach IS
    # the cone's reach. The engine comparison is centre to centre, and the cone
    # is measured from the creature's centre too, so the two agree by
    # construction. Issue #354.
    "Gatekeeper": 2.0,
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
            # The trail is the same three riders Flamedart carries.
            #
            # **IT NO LONGER BURNS THE HELLHOUND OR ITS ALLIES**, and it did
            # until 2026-08-20. The project owner set a general rule that
            # day: a creature does not burn itself or its own side. So the
            # `GroundHitsAllies` rider is gone from here and from the
            # Gatekeeper's Soulfall, and no designed ability carries it. See
            # `docs/DECISIONS.md` for what the reversal costs and why it was
            # accepted.
            params={"Mode": "Charge", "Range": 10, "Radius": 1.5, "Burn": 1,
                    "GroundRadius": 1.5, "GroundDuration": 4,
                    # 25% per second for 4 seconds, so standing in the trail
                    # for its whole life costs one bite. That was already the
                    # design's number and it was prose only; issue #361 made it
                    # a rider, and the general rule is 100 / GroundDuration.
                    "GroundPercent": 25,
                    # 4 METRES BECAUSE IT IS A CHARGE THAT DOES NOT STUN, the
                    # same figure and the same reason as the Abyssal Warden's
                    # Stampede. Issue #625 chose the three distances; the design
                    # names the band as the player's own 3 and 4 metres, with
                    # Path of Exile's default of 4 units beside them.
                    #
                    # NOT IMPLEMENTED, AND IT IS THE ONLY ONE OF THE THREE THAT
                    # IS NOT. The Hellhound has no C++ class -- only the Brute
                    # and the Abyssal Warden are built -- so there is nothing to
                    # attach a shove to. The number is decided and recorded here
                    # so that building the creature does not have to decide it
                    # again. Issue #39 builds the seven slice enemies.
                    "Knockback": 4},
            # The Movement slot's typical cooldown in game/Data/SkillSlots.csv.
            cooldown=5.0,
            note="Charges in a straight line fixed when the wind-up starts, "
                 "burning everything it passes and leaving that lane on fire "
                 "for 4 seconds. The fire burns the player and nobody on the "
                 "Hellhound's own side, including the Hellhound.",
        ),
    ),
    "Brute": (
        Ability(
            name="Slam",
            shape="Strike",
            slot="Basic",
            # NO TELEGRAPH, FOR TWO INDEPENDENT REASONS SINCE 2026-08-09. Its
            # 0.9 metre reach is below the one metre floor for a marker, and the
            # Brute's 1.2 second attack interval now allows only 0.70 metres,
            # which is below that floor as well. Either one alone would settle
            # it. Until the interval moved from 1.6 to 1.2 only the first did:
            # 1.6 allowed 1.40 metres, so the cycle would have permitted a
            # marker and the slam's own radius was what refused it.
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
            #
            # 3 METRES OF SHOVE, THE LOW END OF THE BAND, BECAUSE THIS ALSO
            # STUNS. Issue #625 chose the three distances the design left open.
            # The band is the player's own two numeric knockbacks -- Molten
            # Crush's 3 metres and Searing Hook's 4 -- with Path of Exile's
            # default of 4 units beside them. Of the three enemy abilities that
            # displace, this is the only one that also holds the player still,
            # and being moved while unable to act is already the harshest thing
            # in the slice. The other two are charges and take the 4.
            #
            # Molten Crush is the right comparison rather than Searing Hook: it
            # is the player's own slam that shoves outward from a point, which
            # is what this is, where Searing Hook pulls along a line.
            params={"Radius": 3.5, "Angle": 360, "StunSeconds": 1.5,
                    "Knockback": 3},
            # 8 SECONDS, SET BY PLAYING IT on 2026-08-09, up from the 5 second
            # stun immunity window it used to sit exactly on.
            #
            # THE FLOOR IS STILL THE STUN IMMUNITY WINDOW, and it is a floor
            # rather than the figure. The whole Heavy band in
            # game/Data/SkillSlots.csv is 1 to 4 seconds, which is inside the 5
            # second window, so a Brute stomping on the Heavy cadence would
            # spend most of its stomps on a target that cannot be stunned. 8
            # clears that by 3.
            #
            # WHAT IT IS REALLY SETTING is how many ordinary swings fall between
            # abilities, which is roughly the cooldown divided by the attack
            # interval. At 8 seconds against the 1.2 second interval that is
            # about six swings between one stomp and the next.
            cooldown=8.0,
            note="A ring at its feet, marked for 1.4 seconds, stunning for 1.5. "
                 "At the Heavy slot's 250% it lands at 25% of the reference "
                 "build's effective health, which clears the 10% stun damage "
                 "threshold two and a half times over.",
        ),
        Ability(
            name="Rip and Toss",
            shape="Projectile",
            slot="Special",
            # WHAT IT IS FOR. The Brute has nothing to do about a player who
            # stands off, and the design principle is that abilities should mean
            # different things rather than all mean "damage". This one means
            # "standing outside my reach is not free". It is the only reason the
            # creature is not answered by walking backwards.
            #
            # RANGE 10 IS ITS NOTICE RADIUS. The rule is that there is no
            # distance at which the Brute is aware of you and can do nothing.
            # The notice radius is 1000 cm in
            # game/Source/Cataclysm/Character/CataclysmBruteCharacter.h and was
            # set by playing; if it moves, this should move with it.
            #
            # RADIUS 2.1 IS SET BY THE ANIMATION, which is the only hard
            # constraint available. A telegraph has to be long enough to play
            # the attack's wind-up animation, the way the Stomp's 1.4 second
            # wind-up covers the 0.83 second Ability_GroundSmash_Start.
            # Ability_RipNToss_Toss is 0.87 seconds, so the wind-up must be at
            # least that, and 0.4 + Radius / 3.5 >= 0.87 needs a radius of at
            # least 1.65 m. At 2.1 the wind-up is exactly 1.0 second, clearing
            # the animation by 0.13 -- a similar margin to the Stomp's.
            #
            # It is also well under the Stomp's 3.5 m, so the two markers read
            # as different sizes, and well under the 7.35 m its own cooldown
            # would allow.
            #
            # ARC 0.25, AND NOT A SPEED AT ALL. A lob follows real projectile
            # motion, so it has no one speed to state: it is slowest at the top
            # of its arc and fastest as it lands. What is stated is how high it
            # rises above the straight line to where it lands, as a fraction of
            # the distance thrown, and gravity supplies the rest.
            #
            # A REAL TRAJECTORY RATHER THAN A CHOSEN NUMBER. A projectile
            # launched at 45 degrees, the angle that throws an object furthest,
            # reaches an apex of one quarter of its range.
            #
            # THIS ROW HAS CARRIED THREE DIFFERENT ANSWERS IN ONE DAY, and the
            # middle one is the instructive failure. Speed=600 until issue #465,
            # which showed that holding a speed constant along the path is not
            # projectile motion at all. Flight=1.4 seconds from #465, on the
            # argument that a telegraphed attack should take the same readable
            # moment at every range. Arc=0.25 from issue #474, because gravity
            # and a fixed time together fix the whole VERTICAL part of the
            # trajectory independently of the distance: the rock left the hand
            # at 570 cm/s straight up and rose 166 cm above it whether it was
            # travelling two metres or ten, and every short throw was a
            # near-vertical mortar.
            #
            # WHAT WAS GIVEN UP WITH THE FIXED TIME. The player's window to move
            # is the wind-up plus the flight, and it is no longer constant:
            # about 1.6 seconds at short range against 2.4 at maximum range. The
            # marker still promises the place and the wind-up is unchanged, and
            # at short range the player is inside the Brute's melee reach with
            # other things to react to.
            #
            # THE FLIGHT TIME IS DERIVED FROM IT. A parabola sags g*t*t/8 below
            # its own chord, so an arc of 0.25 * range is in the air for
            # sqrt(8 * 0.25 * range / 980) seconds: 1.43 at ten metres, 0.78 at
            # three. The ten metre figure is within rounding of the 1.4 this row
            # stated between #465 and #474, so the longest throw is unchanged
            # and only the short ones moved.
            #
            # THE SPEED CEILING STILL HOLDS. The rock must not outrun the
            # Succubus's Soulfire at 1200 cm/s, the slowest projectile any
            # player skill uses. It is fastest as it lands, and at the full ten
            # metre throw it arrives at 1073.
            #
            # AND IT IS BOUNDED ABOVE BY THE SPEED CEILING. The rock must not
            # outrun the Succubus's Soulfire at 1200, the slowest projectile any
            # player skill uses in game/Data/WeaponSkills.csv. It lands fastest,
            # having fallen longest: at 1.4 seconds a ten metre throw arrives at
            # 1121 cm/s, under the ceiling. At 1.0 it would arrive at 1244 and
            # break it, so the flight cannot be shortened much below 1.4.
            #
            # PIERCE 0 BECAUSE IT IS ONE ROCK. It stops at what it hits.
            #
            # AND IT IS LOBBED, WHICH IS WHY PIERCE 0 IS NOT ONLY A DETAIL. From
            # 2026-08-09 the rock rises in an arc and comes down on the marked
            # circle rather than travelling flat down a marked lane. A telegraph
            # that marks a place has to deliver to that place; the two were
            # different promises before. See docs/DECISIONS.md, which records why
            # the genre settles this rather than taste, and issue #459.
            #
            # THE MARKED AREA IS THE SAME 2.1 m RADIUS, so this row is unchanged
            # in what it says the attack covers. Only its shape moved: a circle
            # where it lands rather than a lane along the way.
            params={"Range": 10, "Radius": 2.1, "Pierce": 0, "Arc": 0.25},
            # 12 SECONDS, SET BY PLAYING IT on 2026-08-09, up from 5.
            #
            # THE FLOOR IS THE APPROACH TIME, and it is a floor rather than the
            # figure. The Brute crosses its own 10 m throwing range in 2 seconds
            # at its 5 m/s chase speed, so a cooldown under that would let it
            # throw twice per approach and it would read as a ranged enemy
            # rather than a bruiser with a rock. 12 clears that by a wide
            # margin: the throw is now something that happens once as it comes
            # in, not a rhythm.
            #
            # LONGER THAN THE STOMP'S 8, WHICH IS THE POINT OF HAVING TWO. The
            # stomp is what a Brute does to somebody standing next to it and the
            # throw is what it does to somebody who will not come close, so the
            # one that answers the common case comes round more often.
            cooldown=12.0,
            note="It tears a rock out of the ground and lobs it, marked as a "
                 "circle 2.1 metres across where it will land, for 1 second "
                 "first. At the Special slot's 150% it is worth half a Stomp. "
                 "Added 2026-08-07 when the project owner settled the Brute at "
                 "three abilities -- this, the Slam and the Stomp -- on the "
                 "grounds that it is a basic mob and that rarities and "
                 "modifiers are where extra abilities belong. Became an arc "
                 "onto a landing circle on 2026-08-09, from a flat throw down "
                 "a marked lane, and on the same day became real projectile "
                 "motion measured by a fixed 1.4 second flight rather than a "
                 "speed.",
        ),
    ),
    "Corrupted Sentinel": (
        Ability(
            name="Siege Bolt",
            shape="Projectile",
            slot="Basic",
            # RADIUS 2.1 IS THE LARGEST A 2.0 SECOND INTERVAL ALLOWS, so its
            # wind-up is 0.4 + 2.1 / 3.5 = exactly 1.0 second, which is exactly
            # half the interval. That is the same place the Succubus sits and
            # for the same reason: a creature whose whole role is forcing the
            # player to move should mark as much ground as the rule permits.
            #
            # RANGE 14 IS ITS ATTACK REACH. See ATTACK_REACH above: the longest
            # range any player attack reaches, taken because reach is the only
            # tool a creature that cannot move has.
            #
            # SPEED 1400 FALLS OUT OF THE OTHER TWO RATHER THAN BEING CHOSEN.
            # The bolt should land before the next one is marked, or the
            # creature has a shot in the air and a marker on the ground at once
            # and neither means anything. The wind-up takes 1.0 second of the
            # 2.0 second interval, so the flight has the other 1.0. Fourteen
            # metres in one second is 1400 cm/s, and 1400 is one of the ten
            # speeds game/Data/WeaponSkills.csv uses -- Blood Pyre's.
            #
            # SO THE RHYTHM IS EXACTLY TWO SECONDS WITH NOTHING IDLE IN IT: one
            # second of marker, one second of flight, and the next marker
            # appears as the shot lands. That is "forces the player to stay
            # mobile" written as a number rather than as an adjective.
            #
            # IT IS FASTER THAN THE SUCCUBUS'S 1200 AND THAT IS DELIBERATE. The
            # 1200 ceiling in Rip and Toss's comment above is reasoning about a
            # LOB, which is fastest as it lands; this is a flat bolt and is not
            # bound by it. 1400 is still the second slowest of the ten speeds
            # in the player skill table, and what makes this shot readable is
            # the second of ground marker in front of it rather than its speed.
            #
            # PIERCE 0 BECAUSE IT IS ONE BOLT. It stops at what it hits.
            #
            # NO MINIMUM RANGE, AND IT NEEDS NONE. The Brute's rule -- an
            # attack must not mark the ground its own caster stands on -- was
            # written for a LOB marking a circle, where the caster ends up
            # inside the area it is about to hit. A Projectile's marker is a
            # LANE running from the caster out to Range, so the caster is at
            # the lane's origin, which is where a shooter stands. A melee
            # player standing against this creature is standing in that lane
            # and has to step out of it every two seconds like everybody else.
            # That is its answer to being stood on, and it needs no new
            # mechanic. Issue #352.
            params={"Range": 14, "Radius": 2.1, "Pierce": 0, "Speed": 1400},
            note="A bolt down a marked lane 2.1 metres to either side, on the "
                 "ground for 1 second first. It does not lead the player: the "
                 "area is fixed when the wind-up starts, which is the rule for "
                 "every telegraph. Geometry blocks it, so cover works.",
        ),
        Ability(
            name="Brimstone Mortar",
            shape="Projectile",
            slot="Special",
            # WHAT IT IS FOR, AND IT IS THE ONLY THING THAT CAN DO IT. A
            # creature that shoots only in straight lines and cannot walk is
            # answered by one pillar. Issue #352 asks what it does about a
            # player behind cover, and a lobbed shell that comes down from
            # above is the answer. This is the second reason the Sentinel is
            # not simply a Succubus that stands still.
            #
            # ARC 0.25 AND NOT A SPEED, exactly as Rip and Toss above: a
            # projectile launched at 45 degrees, the angle that throws an
            # object furthest, reaches an apex of one quarter of its range. The
            # flight time is derived from it, sqrt(8 * Arc * range / g), which
            # is 1.69 seconds at the full fourteen metres.
            #
            # IT RESPECTS THE SPEED CEILING RIP AND TOSS IS BOUND BY. A lob is
            # fastest as it lands, and at fourteen metres this one arrives at
            # 1171 cm/s, under the Succubus's Soulfire at 1200.
            #
            # RADIUS 3.0 IS A JUDGEMENT, and it is bounded rather than free. It
            # has to be larger than the bolt's 2.1 so the two markers read as
            # different sizes, which is the mistake the Brute's two markers
            # were sized to avoid. It has to be under the 8 metre cap that
            # would make it cost a Movement skill, and under the 12.6 metres
            # its own cooldown allows. Inside that window 3.0 is chosen because
            # it is the second largest Radius any player Projectile uses, Blood
            # Pyre's, so an enemy shell the size of the player's own heavy shot
            # reads as heavy without exceeding anything they have seen.
            #
            # ITS WIND-UP IS 0.4 + 3.0 / 3.5 = 1.26 SECONDS, well inside the
            # half of its cooldown the rule allows.
            #
            # THE BRUTE'S MINIMUM RANGE RULE DOES APPLY TO THIS ONE, because it
            # is lobbed and marks a circle: below marked radius plus body
            # radius the creature is inside the area it is about to hit. That
            # is 3.0 + 0.48 = 3.48 metres. The bolt has no such limit; see the
            # note on it above.
            #
            # RANGE 14, THE SAME AS THE BOLT. A shorter-ranged answer to cover
            # would answer only the cover nearby, and the cover a stationary
            # creature most needs to answer is the cover a player retreats to.
            params={"Range": 14, "Radius": 3.0, "Pierce": 0, "Arc": 0.25},
            # 8 SECONDS. Inside the Special slot's 3 to 10 second band in
            # game/Data/SkillSlots.csv, which is the slot for traps,
            # deployables and grenades.
            #
            # WHAT IT IS REALLY SETTING is how many ordinary shots fall between
            # shells, which is the cooldown divided by the attack interval.
            # Eight against 2.0 is exactly four bolts between one shell and the
            # next -- the same measure the Brute's two cooldowns were settled
            # by playing against.
            cooldown=8.0,
            note="A shell lobbed over cover onto a marked circle 3 metres "
                 "across, on the ground for 1.26 seconds first. It is what a "
                 "creature that cannot walk does about a player who has "
                 "stopped moving behind something.",
        ),
    ),
    "Abyssal Warden": (
        Ability(
            name="Sunder",
            shape="Strike",
            slot="Basic",
            # NOT TELEGRAPHED, and unlike the Brute it is the attack's OWN reach
            # that refuses the marker rather than the cycle. A 2.4 second attack
            # interval allows 2.80 metres, which is comfortably over the one
            # metre floor; the 0.9 metre swing is under it. So this creature can
            # telegraph and simply does not need to for its ordinary swing.
            params={"Radius": 0.9, "Angle": 90, "MaxTargets": 1},
            note="A swing at whatever it is standing against. Its interval is "
                 "long enough for two authored swings: PrimaryAttack_LA and "
                 "PrimaryAttack_RA are 1.1333 seconds each and 2.2667 fits "
                 "inside 2.4 with a tenth of a second to spare, measured "
                 "2026-08-09. It is the only one of the seven that can do "
                 "that, and it is presentation rather than a second ability.",
        ),
        Ability(
            name="Stampede",
            shape="Movement",
            slot="Movement",
            # WHY IT NEEDS ONE AT ALL. This is a MELEE enemy that cannot catch
            # anybody -- the Succubus cannot either and does not need to,
            # because it reaches 8 metres, and being unable to close only
            # matters for a creature that has to. The Gatekeeper shares the
            # problem and answers it differently, with a mortar; two enemies
            # with the same answer would be the same enemy at two sizes.
            #
            # It moves at 2.8 metres per second and its
            # chase speed is 0.0, against player classes at 3.5, 4.0 and 4.6.
            # Without this a player walks backwards and it never fights, which
            # is the rule the Brute's rock throw already states from the other
            # side: there must be no distance at which an enemy is aware of the
            # player and can do nothing.
            #
            # MODE Charge, WHICH REPEATS THE HELLHOUND'S, AND THAT WAS DECIDED
            # AGAINST THE ART RATHER THAN ASSUMED. A Leap was proposed first,
            # because the Imp's section notes that a leap clears a ring of
            # bodies where a charge meets it, and this is the slowest melee
            # creature in the slice. Measuring the pack on 2026-08-09 settled it
            # the other way: `Stampede` is a single 0.700 second clip that fits
            # inside this ability's 0.83 second wind-up at its authored speed,
            # while a leap has to be stitched from five -- Jump_Start 0.333,
            # Jump_Up 0.333, Jump_Loop 1.333, Jump_Fall 1.333, Jump_Land 0.900 --
            # which the shipped one-clip-at-a-time playback path cannot do
            # without the animation Blueprint work in issue #387. `Bound` looked
            # like the leap from its name and is 0.0333 seconds, a single pose.
            #
            # RANGE 8 METRES, the shortest Movement-shape skill range in
            # game/Data/WeaponSkills.csv. The design document already uses that
            # figure as the furthest a player can be made to travel. The
            # shortest is right for the slowest creature, and it still passes
            # the test the Hellhound's charge sets for whether a gap-closer is
            # worth having: during the 0.83 second wind-up this creature could
            # walk 2.32 metres at its own speed, and 8 is more than three times
            # that.
            #
            # RADIUS 1.5, the narrowest corridor any player Charge-mode skill
            # uses, so the marker is a lane to step out of rather than a wall.
            # The same rule the Hellhound's charge is sized by, producing the
            # same answer. It also keeps this ability clearly secondary to the
            # 5.6 metre ring below, which is what the creature is really about.
            #
            # 4 METRES OF SHOVE, THE TOP OF THE BAND, BECAUSE IT DOES NOT STUN.
            # Issue #625 chose the three distances. The band is the player's own
            # two numeric knockbacks -- Molten Crush's 3 metres and Searing
            # Hook's 4 -- with Path of Exile's default of 4 units beside them.
            # The Brute's Stomp takes the 3 because it also holds the player
            # still; nothing about this charge denies the player anything once
            # it has passed, so it takes the 4.
            #
            # IT LEAVES THE LANE, AND DIAGONALLY RATHER THAN STRAIGHT OUT.
            # The shove is away from the creature and a charge meets its target
            # at the LEADING edge of a 1.5 metre lane, so contact happens about
            # 1.3 metres short of somebody standing 0.75 metres off the centre
            # line and the push carries them forward as well as out. Measured in
            # the engine on 2026-08-16: 334 cm along against 219 cm across. They
            # still finish outside the lane, which is the requirement.
            params={"Mode": "Charge", "Range": 8, "Radius": 1.5,
                    "Knockback": 4},
            # The Movement slot's cooldown in game/Data/SkillSlots.csv.
            cooldown=5.0,
            note="A charge in a straight line fixed when the wind-up starts, "
                 "marked as a lane 1.5 metres to either side for 0.83 seconds. "
                 "It leaves nothing behind: the burning lane is the "
                 "Hellhound's, and this creature's job is to arrive.",
        ),
        Ability(
            name="Molten Roar",
            shape="Strike",
            slot="Ultimate",
            # THE LARGEST MARKER IN THE GAME. The Brute's stomp is 3.5 metres
            # and the Succubus's bolt 3.15.
            #
            # RADIUS 6.5 IS THE CAP, AND SITTING AT IT IS THE POINT. It is
            # `telegraph_cap_metres` for a 0.48 m body: the largest marker at
            # which the slowest class still has exactly the 0.4 second reaction
            # allowance and not a moment more. Nothing in the game may be
            # larger, so this is the hardest ring the rules permit.
            #
            # IT WAS 5.6 UNTIL 2026-08-09, and it was raised because the project
            # owner played it and said it was too easy to escape. Raising it
            # only means something because the wind-up is now capped at 2.0
            # seconds. Before that cap the wind-up grew with the radius and
            # handed back exactly as much ground as the bigger ring took away,
            # so the escape margin was 2.3 metres at EVERY radius and a bigger
            # marker was not a harder one. Issues #487 and #496.
            #
            # WHAT THE CHANGE BOUGHT, for the slowest class: the margin falls
            # from 2.30 m to 1.40 m and the spare time from 0.657 s to 0.400 s,
            # a 39% cut in both. The warning stays 2.0 seconds, so the 1.4
            # second `Ultimate_Roar` clip still fits inside it unchanged.
            #
            # THE GEOMETRY IS NOW EXHAUSTED. If this still reads as too easy,
            # the answer is not more radius -- 6.5 is the ceiling -- but what
            # the attack leaves behind. It currently leaves nothing.
            #
            # ANGLE 360 because it is a ring at its feet, and because a cone on
            # a creature that turns at the ordinary 480 degrees per second would
            # simply be aimed.
            #
            # NO STUN. The Brute's stomp is the thing in this slice that stuns,
            # and a second creature holding the player still would spend most of
            # its uses inside the 5 second immunity window anyway.
            params={"Radius": 6.5, "Angle": 360},
            # 12 SECONDS, AND IT IS DERIVED RATHER THAN CHOSEN. docs/DECISIONS.md
            # records that a Herald Abyssal Warden kills the reference geared
            # character in 5 hits and 12.0 seconds. A cooldown longer than that
            # could come round zero times in a fight the player is losing. It is
            # also the bottom of the Ultimate slot's 12 to 40 second band in
            # game/Data/SkillSlots.csv, and exactly five basic attacks apart.
            #
            # THE ULTIMATE SLOT IS THE ONLY ONE OF THE SEVEN NO ENEMY HAS USED.
            # At its 400% this lands at about four of this creature's ordinary
            # hits, which is four fifths of what the reference geared character
            # survives. That is the right weight for something that warns for
            # two seconds and is avoided completely by walking out.
            cooldown=12.0,
            note="It roars and the ground erupts in a ring 6.5 metres across, "
                 "marked for 2 seconds first. The largest telegraph in the "
                 "game and the first thing in it to use the Ultimate slot. "
                 "Ultimate_Roar is 1.4000 seconds, measured 2026-08-09, so the "
                 "wind-up holds the whole clip at its authored speed.",
        ),
    ),
    "Gatekeeper": (
        Ability(
            name="Dread Cleave",
            shape="Strike",
            slot="Basic",
            # THE ONLY TELEGRAPHED BASIC ATTACK THAT IS A MELEE SWING, and the
            # whole fight rests on it being telegraphed at all. This creature
            # kills the reference geared character in 2 hits and 6.0 seconds
            # -- the table in docs/Cataclysm_GDD_v2.md -- so its ordinary
            # swing cannot be an untelegraphed contact hit the way the Abyssal
            # Warden's, the Brute's, the Hellhound's and the Imp's are. A 2.0
            # metre cone warns for 0.97 seconds, which its 3.0 second interval
            # holds with half a cycle to spare (1.5 allowed).
            #
            # THREE OF THE SEVEN BASICS ARE TELEGRAPHED, NOT ONE. The
            # Corrupted Sentinel's Siege Bolt and the Succubus's Soulfire are
            # the other two, and both are projectiles rather than swings.
            # `is_telegraphed` computes it from the numbers rather than from
            # this comment, and `tools/tests/test_enemy_telegraphs.py` holds
            # the design document's prose to what it computes. This comment
            # and that document both said "the only" until 2026-08-21, which
            # is issue #763.
            #
            # RADIUS 2.0 IS A JUDGEMENT, bounded twice. Above the 1 metre
            # marker floor, or nothing is drawn and a 2-hit kill arrives
            # unannounced. Under the 3.85 metres its own interval allows, so
            # the basic swing stays visibly smaller than everything on a
            # cooldown. NO MaxTargets, WHICH IT SHARES WITH THE OTHER TWO
            # TELEGRAPHED BASICS AND WITH NOTHING ELSE: those three are
            # exactly the ordinary attacks that mark an area, and an area
            # attack hits what is standing in it. The four contact swings
            # state MaxTargets=1. This comment said "unlike the other six"
            # until 2026-08-21; issue #763.
            params={"Radius": 2.0, "Angle": 120},
            note="A hammer sweep across a 120 degree arc, 2 metres out, marked "
                 "for 0.97 seconds first. The only telegraphed basic attack "
                 "that is a melee swing; the Corrupted Sentinel's Siege Bolt "
                 "and the Succubus's Soulfire are the other two telegraphed "
                 "basics and both are projectiles. It is telegraphed because "
                 "two of these kill the reference geared character. Sevarog's "
                 "three swing chains at their slow speeds (1.70 s) fit inside "
                 "the 3.0 second interval.",
        ),
        Ability(
            name="Soulfall",
            shape="Projectile",
            slot="Special",
            # WHY IT NEEDS ONE. It moves at 3.0 metres per second with no
            # chase speed, against classes at 3.5, 4.0 and 4.6: like the
            # Abyssal Warden it can never catch anybody. The Warden's answer
            # is a charge; giving the boss the same answer would make it a
            # bigger Warden. Its answer is the Corrupted Sentinel's instead --
            # a lobbed mortar that makes standing off more dangerous than
            # closing -- plus what the mortar leaves behind.
            #
            # THE GROUND IS THE ARENA CHANGING, which is what the research
            # (recorded with #354 in docs/DECISIONS.md) found real bosses do:
            # persistence, not replacement. GroundDuration equals the cooldown,
            # so in steady state one patch of burning ground is always down and
            # the arena shrinks by exactly one patch per cycle until the old
            # one expires.
            #
            # **IT BURNS THE PLAYER AND NOBODY ELSE.** It carried
            # `GroundHitsAllies=1` until 2026-08-20, so the summoned Imps of
            # phase 2 burned in it and kiting them through the fire was
            # counterplay. The project owner then set a general rule that a
            # creature does not burn itself or its own side, which removes
            # that. **PHASE 2 IS CHEAPER FOR THE BOSS THAN IT WAS**, because
            # the summons no longer have a cost attached; `docs/DECISIONS.md`
            # records that as the price of the rule and says what would be
            # tuned first if phase 2 turns out too strong.
            #
            # RANGE 14 AND ARC 0.25 ARE THE SENTINEL'S MORTAR FIGURES, reused
            # rather than invented: both abilities exist to answer a player who
            # stands off, and the Sentinel's were settled first. Radius 3.0 the
            # same. Wind-up 0.4 + 3.0 / 3.5 = 1.26 s inside a 4.0 s half-cycle.
            # THE FIVE RIDERS BELOW WERE MISSING UNTIL 2026-08-20, and the
            # paragraph above described them as though they were here. The
            # Gatekeeper's row in docs/Cataclysm_GDD_v2.md stated all five
            # the whole time, and that document is authoritative, so this
            # was data lost from the model rather than a design question.
            # Nothing here is invented: GroundDuration is the cooldown,
            # GroundRadius is the burst radius, GroundPercent is the
            # project's rule of 100 / GroundDuration. Issue #774, and the
            # guard that could not see it is fixed in the same change.
            # `GroundHitsAllies` was among the five restored and was removed
            # again the same day by the rule above.
            params={"Range": 14, "Radius": 3.0, "Pierce": 0, "Arc": 0.25,
                    "Burn": 1,
                    "GroundRadius": 3.0, "GroundDuration": 10,
                    # 10% a second for 10 seconds, so standing in a patch
                    # for its whole life costs one full hit. The general
                    # rule is 100 / GroundDuration, the same arithmetic the
                    # Hellhound's 25 over 4 seconds comes from.
                    "GroundPercent": 10},
            # The top of the Special slot's 3-to-10 band in
            # game/Data/SkillSlots.csv, because the burning ground it leaves
            # must not accumulate faster than one patch per expiry.
            cooldown=10.0,
            note="A lobbed gout of soulfire that bursts 3 metres wide where it "
                 "lands and leaves burning ground the same size for 10 "
                 "seconds, which burns the player and nobody on the "
                 "Gatekeeper's own side. "
                 "Marked at the landing circle for 1.26 seconds. It is what a "
                 "boss that cannot walk anybody down does about a player who "
                 "stands off, and it shrinks the arena one patch at a time.",
        ),
        Ability(
            name="Call the Damned",
            shape="Summon",
            slot="Special",
            # PHASE 2. The first use of the Summon shape by any enemy, and the
            # genre-standard second-phase addition: the fight stops being a
            # duel and the burning ground starts mattering twice over, because
            # the Imps chase the player through it and burn in it.
            #
            # IMPS, NOT A NEW CREATURE. A boss is built from the vocabulary
            # the other six establish, and the Imp is the swarm. Count 3 per
            # use against MaxActive 6: two uses of headroom, and killing adds
            # is worthwhile because the cap means dead Imps are replaced only
            # on the next cast.
            phase=2,
            params={"Range": 4, "Radius": 2, "Count": 3, "MaxActive": 6},
            cooldown=10.0,
            note="It drives its hammer down and 3 Imps claw out of the ground "
                 "within 4 metres of it, to a cap of 6 alive at once. Not "
                 "telegraphed -- a Summon draws no marker -- and answered by "
                 "killing the Imps, who burn in Soulfall's ground like "
                 "anything else.",
        ),
        Ability(
            name="Soul Harvest",
            shape="Strike",
            slot="Ultimate",
            # PHASE 3, AND THE PROMISE THE DESIGN DOCUMENT MAKES -- "phases can
            # stack area attacks" -- KEPT: in the last third of the fight the
            # floor holds burning patches, the Imps are loose, the cleave keeps
            # coming, and this ring lands on top.
            #
            # RADIUS 6.5 IS THE CAP, the same ring as the Abyssal Warden's
            # Molten Roar and for the same reason: the largest marker the
            # rules permit, at which the slowest class has exactly the 0.4
            # second reaction allowance. A boss finale should be the hardest
            # legal telegraph, and 6.5 is what "hardest legal" is.
            #
            # AT THE ULTIMATE SLOT'S 400% IT KILLS FROM FULL HEALTH. Four of
            # this creature's ordinary hits, and the reference geared character
            # survives two. That is stated as designed rather than discovered:
            # the genre's rule for a long-telegraph boss ultimate is that
            # standing in it is death, and the answer is the 2.0 second
            # warning, not surviving the hit.
            phase=3,
            params={"Radius": 6.5, "Angle": 360},
            # Inside the Ultimate slot's 12-to-40 band. 20 rather than the
            # Warden's 12 because this creature's kill time is 6.0 seconds:
            # a shorter cooldown would put a second ring inside almost every
            # fight the player is already losing.
            cooldown=20.0,
            note="It plants the hammer and the ground erupts in a ring 6.5 "
                 "metres across, marked for 2 seconds first. The same largest-"
                 "legal ring as the Abyssal Warden's, at 400%: standing in it "
                 "is death from full health, and the warning is the answer. "
                 "Sevarog's ultimate is authored as targeting (0.87 s), a hold "
                 "loop (2.23 s) and the swing (2.63 s), which is exactly the "
                 "start-hold-release a 2 second wind-up needs.",
        ),
    ),
}

#: Where each multi-phase enemy's phases begin, as fractions of maximum health,
#: highest first. An enemy absent from this table has one phase.
#:
#: HEALTH THRESHOLDS AND NOTHING ELSE. The research recorded with issue #354 in
#: docs/DECISIONS.md found no shipped ARPG boss whose phases are triggered by a
#: timer, and mechanic completion only ever EXITS a transition. Path of Exile
#: uses quarters; Last Epoch's Aberroth runs uneven bands with a long opening.
#:
#: 0.60 AND 0.30 ARE A JUDGEMENT, bounded by the shape the research settled: a
#: long first band (40%) to teach the base kit, then two shorter ones (30% each)
#: that each add exactly one thing. The genre figures they sit between are
#: community-derived, not developer-published, so there is nothing published to
#: copy exactly.
PHASE_TRANSITIONS: dict[str, tuple[float, ...]] = {
    "Gatekeeper": (0.60, 0.30),
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
            "is split one enemy at a time, and the ones that are done are "
            f"{sorted(ABILITIES)}. The Gatekeeper is #354.")
    return ABILITIES[name]


def _check_every_ability_uses_a_real_shape() -> None:
    """A shape name outside the eight is a shape nothing will execute."""
    for name, entries in ABILITIES.items():
        for ability in entries:
            assert ability.shape in SHAPES, (
                f"{name}'s {ability.name} has shape {ability.shape!r}, which is "
                f"not one of the eight: {list(SHAPES)}")


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


def _check_every_ability_radius_is_legal() -> None:
    """An area too big to cross cannot be escaped, which by the design
    document's own words makes it a damage event rather than a telegraph.

    THIS COVERS EVERY ABILITY, not only the ones that draw a marker, since issue
    #500. `fits_its_cycle` says which of its two conditions each ability is
    subject to."""
    for name, entries in ABILITIES.items():
        kind = archetype(name)
        for ability in entries:
            radius = float(ability.params.get("Radius", 0.0))
            assert fits_its_cycle(ability, kind), (
                f"{name}'s {ability.name} states a {radius} m radius. The cap "
                f"is {telegraph_cap_metres(kind):.2f} m, above which the "
                "slowest class cannot both react and walk clear, which the "
                "design document calls a damage event rather than a telegraph. "
                f"Its wind-up would be {wind_up_seconds(radius):.2f} s, on a "
                f"{ability.cycle_seconds(kind)} s cycle which allows a wind-up "
                f"of {ability.cycle_seconds(kind) / 2.0:.2f} s.")


def _check_every_phase_is_reachable_and_starts_at_one() -> None:
    """A phase nothing transitions into is dead design.

    Three rules. Every ability's phase is at least 1. An enemy whose abilities
    name a phase above 1 must have transitions in PHASE_TRANSITIONS, and the
    highest phase named must be exactly one more than the number of transitions
    -- N transitions make N+1 phases, and a phase beyond that can never begin.
    And phase 1 must not be empty, because the fight has to open with something.
    """
    for name, entries in ABILITIES.items():
        phases = [ability.phase for ability in entries]
        transitions = PHASE_TRANSITIONS.get(name, ())

        assert min(phases) == 1, (
            f"{name}'s first phase has no abilities: the lowest phase named is "
            f"{min(phases)}. The fight has to open with something.")

        assert max(phases) == len(transitions) + 1, (
            f"{name} names phase {max(phases)} and has {len(transitions)} "
            f"transitions in PHASE_TRANSITIONS, which make "
            f"{len(transitions) + 1} phases. A phase beyond that can never "
            "begin, and a transition into a phase that adds nothing is dead "
            "design.")

    for name, thresholds in PHASE_TRANSITIONS.items():
        assert name in ABILITIES, (
            f"PHASE_TRANSITIONS names {name}, which has no designed abilities.")
        assert all(0.0 < t < 1.0 for t in thresholds) and all(
            a > b for a, b in zip(thresholds, thresholds[1:],
                                  strict=False)), (
            f"{name}'s phase transitions {thresholds} must fall strictly "
            "between 1 and 0 and strictly descend: each is the health fraction "
            "a later phase begins at.")


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


def _check_every_projectile_states_a_speed_or_an_arc_but_not_both() -> None:
    """The two describe different motion and only one can be true at a time.

    `Speed` is centimetres per second along a flat path. `Arc` is a lob
    following real projectile motion, whose speed changes throughout the
    flight. An ability carrying both states two trajectories, and whichever the
    engine reads first silently wins. An ability carrying neither is a
    projectile with no way to work out when it arrives.

    The docstring on `SHAPE_PARAMS` has said the two are exclusive since issue
    #474 and nothing checked it. The Corrupted Sentinel is the first enemy with
    one of each, which is what made the gap worth closing.
    """
    for name, entries in ABILITIES.items():
        for ability in entries:
            if ability.shape != "Projectile":
                continue
            stated = [key for key in ("Speed", "Arc") if key in ability.params]
            assert len(stated) == 1, (
                f"{name}'s {ability.name} is a Projectile stating {stated}. It "
                "must state exactly one of Speed and Arc: Speed is a flat path "
                "at a constant rate, Arc is a lob under gravity whose speed "
                "changes throughout, and they describe different motion.")


def _check_no_lobbed_attack_marks_the_ground_its_caster_stands_on() -> None:
    """Its minimum range must be further than the creature's own body.

    The rule is from `docs/DECISIONS.md` under the 2026-08-09 heading "A lobbed
    attack will not be thrown at something standing against the creature", and
    `lob_minimum_range` computes it. What this asserts is the thing that made
    the Brute's original minimum useless: a minimum at or below the distance at
    which the two bodies touch can never refuse anything.
    """
    for name, entries in ABILITIES.items():
        kind = archetype(name)
        contact = PLAYER_BODY_RADIUS + kind.body_radius
        for ability in entries:
            if not is_lobbed(ability):
                continue
            minimum = lob_minimum_range(ability, kind)
            assert minimum > contact, (
                f"{name}'s {ability.name} is lobbed with a minimum range of "
                f"{minimum:.2f} m, and the player's body touches this "
                f"creature's at {contact:.2f} m. A minimum at or inside "
                "contact distance refuses nothing, which is the state issue "
                "#475 was filed about on the Brute.")


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
_check_every_ability_radius_is_legal()
_check_every_phase_is_reachable_and_starts_at_one()
_check_every_stun_is_spaced_by_the_immunity_window()
_check_no_stun_outlasts_the_longest_the_player_has()
_check_every_projectile_states_a_speed_or_an_arc_but_not_both()
_check_no_lobbed_attack_marks_the_ground_its_caster_stands_on()
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
