"""A full stat block for every enemy, from its rarity and its archetype.

WHAT THIS IS FOR. `scoring.py` gives every enemy a Power Score, and that score is
authoritative and verified. It is a power RATING: nothing in it says how much
health an enemy has, how hard it hits, how often, or what it resists. Issue #97.

I checked the separate DungeonSimulator repository, where the scoring model
lives, across its whole history. It has never contained a damage or health number
in any commit, so there was nothing to port.

TWO LAYERS, AND THEY OWN DIFFERENT THINGS.

    RARITY scales magnitude, and nothing else.
        health, damage, armor, energy shield
        A Legendary Imp is a bigger Imp. It is not a different creature: it does
        not start critting more, resisting more, or moving differently.

    ARCHETYPE supplies the profile, and it does not change with rarity.
        attack interval, critical strike chance and multiplier, movement speed,
        evasion, energy shield as a fraction of health, resistances, and the
        multipliers that say how big this kind of thing is relative to average
        The Imp is fast whether it is Common or Legendary. The Corrupted
        Sentinel never moves. The Brute is always heavily armoured.

AN ENEMY HAS ONE RESISTANCE, APPLIED TO ALL INCOMING DAMAGE. It is not eight
figures, one per damage type.

A version of this file gave every enemy a profile across the eight damage types,
resisting some and taking extra from others. The project owner removed it, and
the reason is that player damage is ADAPTIVE: a weapon deals one damage number
rather than eight separate pools, because a weapon carrying eight damage types
would be unworkable to calculate. Once player damage adapts, an enemy's per-type
profile stops changing any outcome, so it is authoring work that buys nothing.

The player still has all eight resistances DEFENSIVELY. That is unchanged and
unrelated: eight Cataclysms attack the player, so the player needs eight
resistances. It is only the enemy side that collapses to one number.

An earlier version of this file put attack interval, criticals, movement and
resistance on the RARITY, which said a Cataclysm Boss winds up more slowly than
a Common enemy purely because it is rarer. That is a statement about what kind of
creature it is, so it belongs to the archetype. The project owner pointed this
out and it is now the other way round.

WHY THERE IS NO ENEMY PENETRATION HERE. An earlier version gave each rarity a
penetration figure so that over-capping resistance would be worth something.
`combat.py` already does that job, through Overwhelm: an enemy above the player's
Power Score strips the player's mitigation in proportion to the gap, and because
`scoring.RARITY_WEIGHTS` already spaces the rarities apart in score, that
produces a rarity ladder by itself. Measured at tier 8 against a player at the
tier's maximum score, Overwhelm strips 8.9% from a Common enemy and 21.4% from a
Cataclysm Boss. The hard-coded figures were a second copy of the same mechanic at
roughly double the size.

Overwhelm is the better of the two for two reasons. It responds to the player's
own power, so out-gearing the content shrinks it, where a fixed per-rarity number
punishes forever. And it strips ALL mitigation rather than only resistance, so an
armour or block or evasion build cannot sidestep it.

Over-capping resistance keeps its purpose under Overwhelm, and gets a cleaner
one: resistance above the 70% cap is exactly the headroom that Overwhelm eats
into. The player's own offensive Penetration stat is untouched by any of this.

ONE THING HERE IS FITTED TO THE PLAYER: ENEMY DAMAGE. Issue #108. Everything
else is set on the enemy's own terms and the player follows, which is the
ordering the project owner asked for. Damage cannot be, because it only means
something against what a character can survive, and that is four multiplying
mitigation layers deep. `reference_build.py` assembles a real geared character
and `tests/test_survivability.py` measures the two damage constants against it.

WHAT THIS DOES NOT COVER. Enemy abilities. The Hellhound's fire trail, the
Brute's stomp stun, the Gatekeeper's phases and the Abyssal Warden's ring are
behaviour, not statistics. They belong with the enemy design work in issues #29
and #39. This is the stat block each of them stands on. `enemy_abilities.py` is
where that behaviour goes as it is designed, one enemy at a time; six of the
seven are filled in and only the Gatekeeper (#354) is left.

THIS PARAGRAPH USED TO NAME THE ABYSSAL WARDEN'S POSITIONAL WEAK POINTS. The
project owner ruled them out on 2026-08-09 -- "we don't do positional weak
points. That's too tedious in a diablo like arpg" -- and nothing in the project
ever implemented damage that varies by where a creature is hit. What that
creature does instead is designed in `enemy_abilities.py` under issue #353.

ONE FIELD HERE IS A BODY MEASUREMENT RATHER THAN A COMBAT STATISTIC.
`body_radius` says how wide the creature is, and it exists because how many of a
swarm can stand around one player at once is a geometry question, not a combat
one. It lives here because it is fixed per archetype exactly like movement speed
and attack interval are.

EVERY DEFENSIVE LAYER LEAVES THIS FILE THROUGH `defender_for`, AND THAT IS THE
POINT OF IT. Until issue #481 there was no route at all: an enemy's armour,
evasion and energy shield were computed here, exported to
`game/Data/EnemyArchetypes.csv`, written onto engine attributes, checked for
sanity by the tests, and then consumed by no arithmetic anywhere, because nothing
in `sim/` ever built a `damage.Defender` from an `EnemyStats`. Only resistance
was applied, so "damage needed to kill" understated by 48% against the Abyssal
Warden and by 56% against the Gatekeeper.

`defender_for` is that route. `EnemyStats.damage_taken_fraction` resolves a probe
hit through `damage.resolve` against it, so it runs the same eight steps in the
same order as the player's side and as `UCataclysmDamageCalculation::Resolve`
in the engine. A defensive layer added to enemies later -- block chance and flat
damage reduction are issue #488 -- is wired in `defender_for` alone and every
figure downstream picks it up. Adding it to `EnemyStats` and not to
`defender_for` puts it back on the dead path.

THERE IS A CEILING ON WHAT THOSE LAYERS MAY STOP TOGETHER, and it is a rule
about the combination rather than about any one of them. `ENEMY_MITIGATION_CEILING`
below is 89% of a hit, which is what a geared player stops, and
`_check_no_enemy_can_become_immune` holds every archetype under it. The per-field
caps cannot do that job: armour caps at 75% and resistance at 70%, so those two
alone reach 92.5% stopped with neither over its own limit. Until issue #483 the
check inspected `resistance` and nothing else, under a docstring saying it
checked the combination.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

from . import damage, scoring
from .character import DAMAGE_TYPES

# --------------------------------------------------------------------------
# The rarity ladder: magnitude only
# --------------------------------------------------------------------------

#: In order, and matching `scoring.RARITY_WEIGHTS`, which is the authoritative
#: list. Note it has Herald and Cataclysm Boss and no Rare; the design document's
#: list is the superseded one. See issue #30.
RARITY_ORDER = ("Common", "Elite", "Legendary", "Herald", "Boss",
                "Cataclysm Boss")


def rarity_step(rarity: str) -> int:
    """How far above Common a rarity sits. Common is 0, Cataclysm Boss is 5."""
    if rarity not in RARITY_ORDER:
        raise ValueError(
            f"unknown rarity {rarity!r}; expected one of {list(RARITY_ORDER)}")
    return RARITY_ORDER.index(rarity)


#: The first rung of the ladder that is a boss for the anti-stun-lock rule "a
#: boss cannot be stunned at all", section VI of the design document. Boss and
#: Cataclysm Boss are above the line; Herald -- the Abyssal Warden's reference
#: rarity, a mini-boss -- is deliberately below it.
FIRST_BOSS_RARITY = "Boss"


def is_boss_rarity(rarity: str) -> bool:
    """Whether the anti-stun-lock boss rule applies at this rarity.

    DERIVED FROM RARITY, NOT DECLARED PER ENEMY, and that is the decision
    rather than a convenience: the enemy generator assigns each enemy a rarity
    from the pool weights anyway, so boss-ness follows from what it already
    sets and there is no second flag to forget. Decided by the project owner on
    2026-08-10; `docs/DECISIONS.md` records it. The engine copy is
    `ACataclysmEnemyCharacter::IsBoss`, whose `FirstBossRarityStep` a test in
    `tools/tests/test_enemy_tables_match_the_model.py` pins to this ladder.
    `damage.py` consumes the answer through `Defender.is_boss`.
    """
    return rarity_step(rarity) >= rarity_step(FIRST_BOSS_RARITY)


#: Health as a fraction of score for an average Common enemy, multiplied per step
#: of rarity. 1.85 per step takes a Cataclysm Boss to roughly 21 times a Common
#: enemy's health, which is what makes a boss fight last rather than a boss
#: simply hit harder.
HEALTH_AT_COMMON = 0.50
HEALTH_PER_STEP = 1.85

#: Damage. These two are the ONE PLACE in this file fitted to the player rather
#: than set on the enemy's own terms, and that is deliberate. Issue #108.
#:
#: WHY THIS ONE IS DIFFERENT. Enemy health is set freely and the damage a player
#: needs follows from it, which is the ordering the project owner asked for.
#: Enemy damage has no such freedom: it only means anything against what a player
#: can survive, and a player's survivability is four mitigation layers deep. A
#: geared character at tier 8 takes about a tenth of what is thrown at them --
#: 53% off from armour, 70% resistance, 28% block chance, 16% flat reduction --
#: so a figure chosen without reference to that is a figure chosen against
#: nothing.
#:
#: WHAT THEY WERE, AND WHY THEY MOVED. 0.09 and 1.55. Measured against the
#: reference build in `reference_build.py`, that left an average Common enemy
#: needing 176 hits to kill a geared character where the project owner had asked
#: for 8 to 10, while the Cataclysm Boss needed 8. Trash and elites did nothing
#: at all.
#:
#: 0.65 and 1.40 were solved for two targets against that same build: an average
#: Common enemy is a threat in a pack rather than alone, and the Gatekeeper kills
#: in a couple of hits. The growth per step falls from 1.55 to 1.40 because
#: raising the floor without lowering the slope would have made the boss a
#: guaranteed one-shot.
#:
#: The rarest enemies are MORE frightening than before, not less, even though the
#: growth rate fell: what a player feels is hits survived, and the Gatekeeper went
#: from 8 to about 2 while a Common enemy went from 176 to about 25.
#:
#: `sim/tests/test_survivability.py` measures all of this, so these two numbers
#: cannot drift from the mitigation they were fitted against.
DAMAGE_AT_COMMON = 0.65
DAMAGE_PER_STEP = 1.40

#: Armour. Unlike the two above, an average enemy carries a little of this and a
#: Common enemy is no longer automatically unarmoured -- a Common Brute is
#: described in the design as heavily armoured, and that is the archetype's call
#: to make, not the rarity's. An archetype with no armour sets its share to zero.
ARMOR_AT_COMMON = 0.10
ARMOR_PER_STEP = 1.35


# --------------------------------------------------------------------------
# Archetypes: what KIND of thing the enemy is
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Archetype:
    """One kind of enemy. Rarity scales this; it does not reshape it."""

    name: str
    role: str

    #: Which Cataclysm this enemy belongs to. Also the damage type it deals: the
    #: design gives each Cataclysm one damage type and its enemies use it.
    cataclysm: str = "Demonic"

    # How big this kind of thing is, relative to an average enemy of its rarity.
    health_share: float = 1.0
    damage_share: float = 1.0
    armor_share: float = 1.0

    # The profile. Fixed: the same at every rarity.
    attack_interval: float = 1.5      # seconds between attacks
    crit_chance: float = 5.0          # percent
    crit_multiplier: float = 150.0    # percent
    move_speed: float = 4.5           # metres per second
    evasion: float = 0.0              # percent, direct attacks only

    #: How fast it moves once it has noticed the player, in metres per second.
    #: Zero means it uses `move_speed` in both states, which is what every
    #: enemy but the Brute does.
    #:
    #: A SECOND SPEED IS ORDINARY, not an invention for one creature. Diablo II
    #: gives every monster both a `Velocity` and a `Runvelocity` column in
    #: `monstats.txt`. A creature that patrols at one pace and commits at
    #: another is the normal shape; one pace for both is the special case.
    #:
    #: IT ALSO DECIDES WHETHER THE PLAYER CAN DISENGAGE, which is why it is a
    #: design figure and not an engine tuning value. Player movement is 3.5,
    #: 4.0 and 4.6 metres per second across the three Demonic classes, so a
    #: chase speed above 4.6 means no class can break away and the fight is
    #: mandatory once noticed.
    chase_speed: float = 0.0
    energy_shield_fraction: float = 0.0   # of this enemy's health

    #: How wide the creature's body is, as a radius in metres. It decides how
    #: many of this kind can stand around one player at once, which is the whole
    #: of the Imp's design: see `enemy_abilities.ring_capacity`.
    #:
    #: The default is 0.48 because that is `EnemyCapsuleRadius` in
    #: `game/Source/Cataclysm/Character/CataclysmEnemyCharacter.cpp`, which every
    #: enemy uses today. An archetype that has not had its own figure decided
    #: keeps that one, so nothing changes for the six enemies whose design issues
    #: are still open (#349 to #354).
    body_radius: float = 0.48

    #: How fast it can turn on the spot, in degrees per second. It is what
    #: decides whether a creature can be got behind, which is the whole of what
    #: the design means by "can be outmanoeuvred".
    #:
    #: The default is 480 because that is the `RotationRate` yaw every enemy is
    #: constructed with in
    #: `game/Source/Cataclysm/Character/CataclysmEnemyCharacter.cpp`. An
    #: archetype that has not had its own figure decided keeps that one.
    turn_rate_degrees: float = 480.0

    #: Percent of all incoming damage resisted, whatever its type. One figure,
    #: not eight: see the note at the top of this file. A negative value would
    #: mean the creature takes extra damage from everything, which is legal and
    #: currently unused.
    resistance: float = 0.0

    @property
    def damage_type(self) -> str:
        """What this enemy deals. The same as its Cataclysm, by design.

        This still matters, because it says which of the PLAYER's eight
        resistances applies to being hit by it.
        """
        return self.cataclysm


#: The abstract average enemy. Not a creature anyone fights: it exists so the
#: rarity ladder can be read on its own, with every archetype multiplier at 1 and
#: no thematic resistances. Every table in this file that shows "the rarity base
#: class" is showing this archetype at each rarity.
BASELINE = Archetype(name="Baseline", role="The average enemy, for reading the "
                                           "rarity ladder on its own")

#: The seven Demonic Cataclysm enemies the design document names for the vertical
#: slice. Their roles are quoted from it; the numbers are this file's.
ARCHETYPES: dict[str, Archetype] = {
    a.name: a for a in (
        BASELINE,
        Archetype(
            name="Imp",
            role="Fast, swarming melee. Weak individually",
            health_share=0.35, damage_share=0.45, armor_share=0.0,
            attack_interval=0.9, move_speed=6.5, evasion=25.0,
            # None at all. Swarm fodder should die to whatever the player has.
            resistance=0.0,
            # The smallest body in the slice, and the same as the lesser imp
            # minion's capsule in game/Source/Cataclysm/AbilitySystem/
            # CataclysmMinion.cpp, because it is the same creature. At 0.30 m
            # exactly twenty fit within the Imp's reach, which is the pack size
            # the design document already commits to. Issue #348.
            body_radius=0.30,
        ),
        Archetype(
            name="Succubus",
            role="Ranged caster. Slow but powerful attacks",
            health_share=0.60, damage_share=1.60, armor_share=0.20,
            attack_interval=2.6, crit_chance=10.0, crit_multiplier=200.0,
            move_speed=3.5, evasion=10.0, energy_shield_fraction=0.50,
            # Little of its own. What keeps it alive is the shield.
            resistance=10.0,
        ),
        Archetype(
            name="Hellhound",
            role="Aggressive charger that leaves fire trails",
            health_share=0.75, damage_share=0.95, armor_share=0.30,
            attack_interval=1.1, crit_chance=15.0, crit_multiplier=175.0,
            move_speed=7.5, evasion=20.0,
            # A beast. Tougher than an Imp, and it relies on speed rather than
            # on soaking hits.
            resistance=10.0,
        ),
        Archetype(
            name="Brute",
            role="Heavily armored slow melee. Can be outmaneuvered",
            health_share=2.20, damage_share=1.75, armor_share=3.00,
            # 1.2 SECONDS BETWEEN SWINGS, SET BY PLAYING IT on 2026-08-09. It
            # was 1.6, and 2.8 before that. The project owner found the swing
            # slow at 1.6 and settled 1.2 at the same time as the two ability
            # cooldowns below, because the three are one question: how many
            # ordinary swings fall between abilities.
            #
            # IT NO LONGER CLEARS THE SWING ANIMATION BY MUCH, and that is the
            # hard bound. Attack_Biped_Melee_A is 1.0000 seconds long, measured
            # in the editor on 2026-08-09, and nothing rate-scales it:
            # ACataclysmBruteCharacter::PlayAttackAnimation passes no window, so
            # the clip plays at its authored speed. At 1.2 there is a fifth of a
            # second between one swing ending and the next starting. Below 1.0
            # they would overlap.
            #
            # THE BRUTE NOW SWINGS FASTER THAN A GENERIC ENEMY, AND THAT
            # PROPERTY WAS GIVEN UP DELIBERATELY. ACataclysmEnemyCharacter
            # carries 1.5 seconds as its class default, so at 1.6 the Brute was
            # slower than anything unconfigured and at 1.2 it is faster.
            # "Heavily armored slow melee. Can be outmaneuvered" survives it:
            # that reading is carried by the 180 degree turn rate against every
            # other enemy's 480, and by the 2.5 m/s patrol speed. Swing speed
            # was a third supporting property and is now spent.
            attack_interval=1.2, crit_multiplier=200.0, move_speed=2.5,
            # Thick hide on top of the armour, which is its main defence.
            resistance=15.0,
            # 180 rather than every other enemy's 480, because "can be
            # outmanoeuvred" has to be a number. A player circling at the
            # Brute's own reach turns at 223 degrees per second even in the
            # slowest Demonic class, so anything under that can be got behind
            # by every build. Issue #351.
            turn_rate_degrees=180.0,
            # It lumbers at 2.5 while it has seen nothing and commits at 5.0
            # once it has. Set by the project owner on 2026-08-07 by playing it,
            # after a Brute that chased at its patrol speed was reported as
            # barely a threat.
            #
            # IT IS NOW FASTER THAN THE PLAYER, AND THAT IS NOT WHAT WAS
            # JUDGED. When 5.0 was chosen, ACataclysmPlayerCharacter never
            # assigned MaxWalkSpeed and the player ran at Unreal's default of
            # 6.0 metres per second, so 5.0 left a 1.0 margin in the player's
            # favour: they could break away, but only by committing to it,
            # which is what playing it was reported to feel like. Issue #391
            # fixed that on 2026-08-08 and the player now walks at the 4.0 the
            # class stat data gives, so the same 5.0 is a 1.0 margin AGAINST
            # them and this creature cannot be walked away from at all.
            #
            # SCALING IT DOWN IS NOT ENOUGH. ABP_Brute chooses the four-legged
            # chase gait above 3.75 metres per second, so the usable window
            # against a 4.0 player is 3.75 to 4.0; and the Ritualist is
            # designed at 3.5, which no chase speed that triggers that gait can
            # be escaped by. That is a design question rather than arithmetic
            # and it is issue #417.
            #
            # "CAN BE OUTMANOEUVRED" HOLDS EITHER WAY, through the turn rate
            # rather than through footspeed. A player circling at the Brute's
            # reach sweeps 223 degrees per second and it turns at 180, so it can
            # be got behind by every build exactly as before.
            chase_speed=5.0,
        ),
        Archetype(
            name="Corrupted Sentinel",
            role="Stationary ranged. Forces the player to stay mobile",
            health_share=1.30, damage_share=1.10, armor_share=2.20,
            attack_interval=2.0, move_speed=0.0, energy_shield_fraction=0.35,
            # A construct rather than a living thing, so it is hard to hurt by
            # any means. It cannot retreat, so it has to be able to take hits.
            resistance=20.0,
        ),
        Archetype(
            name="Abyssal Warden",
            role="Massive stone and lava demon. High damage resistance",
            health_share=3.50, damage_share=1.90, armor_share=3.50,
            attack_interval=2.4, crit_chance=10.0, crit_multiplier=200.0,
            move_speed=2.8,
            # The highest in the vertical slice, because the design describes
            # this one and only this one as having high damage resistance.
            resistance=35.0,
        ),
        Archetype(
            name="Gatekeeper",
            role="Multi-phase towering demon",
            health_share=5.00, damage_share=2.10, armor_share=2.50,
            attack_interval=3.0, crit_chance=15.0, crit_multiplier=250.0,
            move_speed=3.0,
            # High, but below the Abyssal Warden, which is the one the design
            # singles out for resistance. This one's threat is its phases.
            resistance=30.0,
        ),
    )
}


def _check_every_archetype_deals_a_real_damage_type() -> None:
    """An enemy's own damage type says which of the PLAYER's eight resistances
    applies when it hits them, so a typo would silently bypass all of them."""
    for kind in ARCHETYPES.values():
        assert kind.cataclysm in DAMAGE_TYPES, (
            f"{kind.name} belongs to Cataclysm {kind.cataclysm!r}, which is not "
            f"one of the eight damage types: {list(DAMAGE_TYPES)}")


def _check_no_enemy_resists_more_than_the_cap_allows() -> None:
    """A resistance at or above the cap is a figure that does not mean what it
    says.

    `damage.effective_resistance` caps every resistance at
    `damage.RESISTANCE_CAP`, so an archetype declaring 95% would behave exactly
    as one declaring 70% and the extra 25 points would be a number in a table
    that changes no outcome anywhere.

    THIS IS A DIFFERENT RULE FROM THE CEILING ON THE COMBINATION further down,
    and neither implies the other. This one is about one field being read as
    smaller than it is written; that one is about how much every layer stops
    together. An archetype with no armour and 69% resistance passes this and
    passes that; one at 60% resistance and heavy armour passes this and fails
    that. Until issue #483 only this one existed, under a docstring that claimed
    to be the other.
    """
    for kind in ARCHETYPES.values():
        assert kind.resistance < damage.RESISTANCE_CAP, (
            f"{kind.name} resists {kind.resistance}% of all damage, at or above "
            f"the {damage.RESISTANCE_CAP:.0f}% the design caps resistance at, so "
            "everything above the cap would change nothing")


def _check_every_creature_can_turn() -> None:
    """A turn rate of zero would mean a creature that can never face anything,
    which is a bug rather than a design and would make every melee enemy
    harmless by walking half a step."""
    for kind in ARCHETYPES.values():
        assert kind.turn_rate_degrees > 0.0, (
            f"{kind.name} turns at {kind.turn_rate_degrees} degrees per second, "
            "so it can never face what it is fighting")


def _check_every_body_has_a_width() -> None:
    """A body radius of zero would let unlimited enemies stand on one point.

    `enemy_abilities.ring_capacity` divides by it, and it is what caps how many
    of a swarm can reach the player at once. Zero or a negative value removes
    that cap silently rather than raising anywhere useful.
    """
    for kind in ARCHETYPES.values():
        assert kind.body_radius > 0.0, (
            f"{kind.name} has a body radius of {kind.body_radius}, so any "
            "number of them could stand on the same point")


def archetype(name: str) -> Archetype:
    if name not in ARCHETYPES:
        raise ValueError(
            f"unknown archetype {name!r}; expected one of {sorted(ARCHETYPES)}")
    return ARCHETYPES[name]


# --------------------------------------------------------------------------
# The stat block
# --------------------------------------------------------------------------

#: The hit `damage_taken_fraction` resolves to read the mitigation multiplier off,
#: and the health it resolves against.
#:
#: THE HEALTH IS ENORMOUS ON PURPOSE. `damage.resolve` reports what one hit
#: actually dealt, so it clamps the answer to the health remaining. That clamp is
#: correct there and would silently cap the multiplier here, which is reading a
#: ratio rather than killing anything. A pool far above the probe cannot clamp.
_PROBE_DAMAGE = 1000.0
_PROBE_HEALTH = 1e12


@dataclass(frozen=True)
class EnemyStats:
    """One enemy's complete stat block."""

    archetype: Archetype
    rarity: str
    score: float

    #: The difficulty tier this enemy stands at. CARRIED RATHER THAN USED to set
    #: any magnitude here: the score already contains the tier, so nothing in the
    #: block below is computed from it.
    #:
    #: IT IS HERE BECAUSE ARMOUR HAS NO VALUE WITHOUT IT. `damage.armor_reduction`
    #: divides by `800 x tier`, so the same armour figure means very different
    #: things at different tiers -- the Abyssal Warden's 5,954 removes 75.00% of a
    #: hit at tier 1 and 48.19% at tier 8. Asking the caller to supply a tier at
    #: the moment of use instead would allow a block built at tier 8 to be judged
    #: at tier 1 with nothing to notice, and would be wrong by a factor of two.
    #: There is one tier per stat block and it cannot disagree with itself.
    tier: int

    # Scaled by rarity.
    health: float
    damage_per_hit: float
    armor: float
    energy_shield: float

    # Taken unchanged from the archetype.
    attack_interval: float
    crit_chance: float
    crit_multiplier: float
    move_speed: float
    evasion: float
    resistance: float

    @property
    def name(self) -> str:
        if self.archetype is BASELINE:
            return self.rarity
        return f"{self.rarity} {self.archetype.name}"

    @property
    def effective_health(self) -> float:
        """Health plus shield. What the player's damage has to chew through."""
        return self.health + self.energy_shield

    @property
    def damage_per_second(self) -> float:
        return self.damage_per_hit / self.attack_interval

    @property
    def average_damage_per_hit(self) -> float:
        """Including critical strikes, over many hits."""
        chance = self.crit_chance / 100.0
        multiplier = self.crit_multiplier / 100.0
        return self.damage_per_hit * (1.0 - chance + chance * multiplier)

    @property
    def damage_type(self) -> str:
        """What this enemy deals, which says which player resistance applies."""
        return self.archetype.cataclysm

    def defender_for(self) -> damage.Defender:
        """This enemy in the shape `damage.resolve` resolves hits against.

        THE ONLY ROUTE FROM THIS FILE INTO THE DAMAGE MODEL. See the note at the
        top of the file: before issue #481 there was none, so armour, evasion and
        the energy shield were computed and never applied to anything.

        ONE RESISTANCE BECOMES EIGHT IDENTICAL ENTRIES. `damage.Defender` holds a
        resistance per damage type because the player needs eight; an enemy has
        one figure applied to all incoming damage, so the same number goes in
        every slot. That is the mapping, not a loss of information.

        BLOCK CHANCE AND FLAT DAMAGE REDUCTION ARE LEFT AT ZERO, deliberately and
        not by omission: enemies have neither today. Issue #488 proposes giving
        them both, and this is the single line it has to change.
        """
        return damage.Defender(
            health=self.health,
            energy_shield=self.energy_shield,
            armor=self.armor,
            evasion=self.evasion,
            resistances={kind: self.resistance for kind in DAMAGE_TYPES},
            tier=self.tier,
            is_boss=is_boss_rarity(self.rarity),
        )

    def damage_taken_fraction(self, penetration: float = 0.0,
                              armor_penetration: float = 0.0,
                              is_area: bool = False) -> float:
        """Share of a player's hit that reaches this enemy's health and shield.

        Every mitigation layer the enemy has, in the order `damage.resolve`
        applies them, which is the order the engine applies them in too. Before
        issue #481 this applied resistance alone and nothing else.

        THE SHIELD IS NOT COUNTED HERE, and that is not an oversight. An energy
        shield sits after all of these layers and is part of the pool a player
        chews through rather than a per-hit reduction, so it belongs to
        `effective_health` and counting it in both places would count it twice.

        EVASION IS COUNTED, AS ITS EXPECTATION. It is a per-hit avoidance roll
        and not a proportional reduction, so folding it into a single fraction
        means averaging over the roll, which is what `damage.average_damage_taken`
        already does on the player's side. Two reasons for including it. The
        figure this feeds answers "damage per swing needed to kill in so many
        swings", and a player counts swings: an Imp that avoids a quarter of them
        genuinely takes a third more damage per swing to kill on schedule. And
        evasion is the Imp's and the Hellhound's designed defence -- leaving it
        out would make swarm fodder that dodges identical to swarm fodder that
        does not.

        `is_area` is how a caller says the hit cannot be evaded. The design gives
        evasion to direct attacks only and lets area damage land regardless, so
        an area attack against the same enemy gets a larger share through.

        PENETRATION IS CLAMPED AT THE ENEMY'S OWN RESISTANCE, and this file no
        longer does the clamping itself. It used to, because
        `damage.effective_resistance` let penetration overshoot into negative
        resistance and turn over-stacking into a damage multiplier, which the
        design document forbids. Issue #482 fixed that at the shared definition,
        so the clamp here would now be a second copy of a rule that has one, and
        it was removed. The behaviour is unchanged: over-stacked penetration
        stops at zero resistance either way.
        """
        against = replace(self.defender_for(),
                          health=_PROBE_HEALTH, energy_shield=0.0)
        hit = damage.Attacker(
            damage=_PROBE_DAMAGE,
            damage_type=self.damage_type,
            penetration=penetration,
            armor_penetration=armor_penetration,
            is_area=is_area,
        )
        return damage.average_damage_taken(hit, against) / _PROBE_DAMAGE


def stats_for(rarity: str, score: float,
              kind: Archetype | str = BASELINE, *, tier: int) -> EnemyStats:
    """The whole stat block for one enemy.

    `rarity` sets how big it is, `kind` sets what it is, `score` is what
    `scoring.py` says the encounter is worth, and `tier` is the difficulty tier
    it stands at.

    `tier` CHANGES NOTHING THIS FUNCTION COMPUTES, and it has no default anyway.
    See the field's own note on `EnemyStats`: armour is worth twice as much at
    tier 1 as at tier 8, so a defaulted tier would be a wrong answer that looks
    like a right one. Requiring it means every caller states which tier its
    figures are about.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    n = rarity_step(rarity)
    score = max(0.0, score)

    health = max(1.0, score * HEALTH_AT_COMMON * HEALTH_PER_STEP ** n
                 * kind.health_share)

    return EnemyStats(
        archetype=kind,
        rarity=rarity,
        score=score,
        tier=tier,
        health=health,
        damage_per_hit=(score * DAMAGE_AT_COMMON * DAMAGE_PER_STEP ** n
                        * kind.damage_share),
        armor=(score * ARMOR_AT_COMMON * ARMOR_PER_STEP ** n
               * kind.armor_share),
        energy_shield=health * kind.energy_shield_fraction,
        attack_interval=kind.attack_interval,
        crit_chance=kind.crit_chance,
        crit_multiplier=kind.crit_multiplier,
        move_speed=kind.move_speed,
        evasion=kind.evasion,
        resistance=kind.resistance,
    )


def stats_on_floor(rarity: str, tier: int, dungeon_type: str = "Basic",
                   subtype: str = "None", total_floors: int = 50,
                   floor: int | None = None,
                   modifier_score: float = 0.0,
                   kind: Archetype | str = BASELINE) -> EnemyStats:
    """The stat block for an enemy standing on a particular dungeon floor."""
    floor = total_floors if floor is None else floor
    scores = scoring.enemy_scores(tier, dungeon_type, subtype, total_floors,
                                  floor, modifier_score)
    return stats_for(rarity, scores[rarity], kind, tier=tier)


# --------------------------------------------------------------------------
# The ceiling on what an enemy may stop
# --------------------------------------------------------------------------

#: The most of a hit any enemy may stop with every defensive layer it has, as a
#: percentage. The design document states the RULE and no number, so this is the
#: number, and it is one the project already had rather than a new one.
#:
#: THE RULE IS "NO ENEMY STOPS MORE THAN THE PLAYER DOES".
#: `docs/Cataclysm_GDD_v2.md`, "How Long a Geared Character Survives", totals the
#: reference geared character's four layers at 89.9% of a hit stopped: 53.3% from
#: armour, 70% resistance, 14.0% from a 28% block chance because a block removes
#: half a hit rather than all of it, and 15.9% flat reduction.
#: `reference_build.damage_taken_fraction(8)` measures the same character at
#: 89.87%.
#:
#: THIS FILE CANNOT COMPUTE THAT FIGURE, so it is stated here and pinned by a
#: test. `reference_build` imports `affixes` and `affixes` imports this module,
#: so importing it here would be a cycle. See the note at the top of
#: `reference_build.py`, which chose that direction deliberately.
#: `sim/tests/test_survivability.py` is where both modules can be imported at
#: once, and it holds this constant below what the reference character stops and
#: within two points of it, so it can neither become unreachable nor quietly
#: stop meaning what it says.
#:
#: WHY THE PLAYER'S TIER 8 FIGURE. Their own total falls as the tier rises,
#: because armour is divided by 800 x tier: they stop 94.58% at tier 1 and 89.87%
#: at tier 8, which is the last tier in the game. Taking their weakest is what
#: makes the rule hold at every tier rather than only at the one it was measured
#: at.
#:
#: WHY A WHOLE PERCENT BELOW IT rather than 89.87 exactly. The design document
#: publishes the player's figure rounded to 89.9%, so a ceiling at the measured
#: value would leave a gap an enemy could sit in and stop more than the player
#: while still matching the published number. It also stops this constant
#: churning every time an affix is tuned.
#:
#: IT BINDS BEFORE THE PER-LAYER CAPS DO, and that is the whole point of it.
#: Armour caps at 75% and resistance at 70%, so those two alone reach 92.5%
#: stopped with neither one over its own cap. "No combination of these layers
#: reaches immunity" is a statement about the combination, and until issue #483
#: nothing checked the combination.
ENEMY_MITIGATION_CEILING = 89.0

#: An armour figure large enough to sit at `damage.ARMOR_REDUCTION_CAP` at any
#: tier. Not a stat block anyone fights: it is how `most_damage_stopped` reads an
#: upper bound instead of a sample.
_SATURATING_ARMOR = 1e9


def most_damage_stopped(kind: Archetype | str, rarity: str) -> float:
    """The largest share of a hit this creature could ever stop, as a percentage.

    AN UPPER BOUND RATHER THAN A MEASUREMENT, and it has to be one. An enemy's
    armour is a share of its Power Score, and a score has no maximum: a deeper
    floor, a higher tier and every dungeon modifier all add to it. So there is no
    largest real armour figure to check.

    There is a largest EFFECT, though, and that is what this reads. Armour is the
    only defensive layer that grows with the score; every other one is fixed per
    archetype. `damage.armor_reduction` rises with armour and stops at
    `damage.ARMOR_REDUCTION_CAP`, and no later step in the order can undo a
    larger reduction at an earlier one. So this archetype at the armour cap stops
    at least as much as the same archetype at any score it could really have.

    An archetype whose `armor_share` is zero never gets armour at all, however
    large its score. The Imp is the one, and its bound is read with no armour
    rather than with the cap.

    IT GOES THROUGH `damage_taken_fraction` LIKE EVERY OTHER FIGURE HERE, rather
    than multiplying the layers out locally. A second copy of the mitigation
    order would not see a layer added to `defender_for` later, which is exactly
    the failure the note at the top of this file is about.
    """
    kind = archetype(kind) if isinstance(kind, str) else kind
    saturated = replace(
        stats_for(rarity, 0.0, kind, tier=1),
        armor=_SATURATING_ARMOR if kind.armor_share > 0.0 else 0.0)
    return 100.0 * (1.0 - saturated.damage_taken_fraction())


def _check_no_enemy_can_become_immune() -> None:
    """No creature's defensive layers stop more of a hit than the player's do.

    THE CHECK IS ON THE COMBINATION, not on any one field, because the rule it
    enforces is about the combination. `docs/Cataclysm_GDD_v2.md`: "No
    combination of these layers reaches immunity. Each has either a cap or a
    curve that cannot reach zero damage." Every field here is inside its own cap
    and armour and resistance alone still reach 92.5% stopped, so a per-field
    check cannot enforce that sentence however many fields it inspects.

    UNTIL ISSUE #483 THIS FUNCTION CHECKED `resistance` AND NOTHING ELSE, under a
    docstring that said it checked the combination. The three other layers an
    enemy already had -- armour, evasion and the energy shield -- were invisible
    to it.

    IT SWEEPS RARITY AS WELL AS ARCHETYPE. Rarity changes no mitigation today, so
    every rarity gives the same answer for one archetype. It is swept anyway
    because `defender_for` already reads rarity for `is_boss`, and a layer that
    varied by rarity would otherwise be checked at one rarity only.
    """
    for kind in ARCHETYPES.values():
        for rarity in RARITY_ORDER:
            stopped = most_damage_stopped(kind, rarity)
            assert stopped < ENEMY_MITIGATION_CEILING, (
                f"{kind.name} at {rarity} rarity could stop {stopped:.2f}% of a "
                f"hit with every defensive layer it has, at or above the "
                f"{ENEMY_MITIGATION_CEILING:.0f}% ceiling, which is what the "
                "reference geared character stops")


# All five run here rather than beside their own definitions, because the
# immunity check resolves a probe hit through a whole stat block and so cannot
# run until `stats_for` above it exists. Splitting them would leave four checks
# firing at one point in the file and the fifth at another, for no reason a
# reader could see.
_check_every_archetype_deals_a_real_damage_type()
_check_no_enemy_resists_more_than_the_cap_allows()
_check_every_body_has_a_width()
_check_every_creature_can_turn()
_check_no_enemy_can_become_immune()


# --------------------------------------------------------------------------
# Reported, not asserted: what this implies for a player
# --------------------------------------------------------------------------
#
# The enemy side is set on its own terms and gear will be fitted to it, so these
# two functions answer questions and enforce nothing. An earlier version of the
# test file asserted player survival targets directly, which is what kept
# producing conflicts with the gear work.

def hits_to_kill_player(enemy: EnemyStats, player_effective_health: float,
                        mitigation_fraction: float = 0.0) -> float:
    """How many of this enemy's hits a player survives, counting criticals."""
    per_hit = enemy.average_damage_per_hit * (1.0 - mitigation_fraction)
    return player_effective_health / max(per_hit, 1e-9)


def player_damage_to_kill_in(enemy: EnemyStats, hits: float,
                             penetration: float = 0.0,
                             armor_penetration: float = 0.0,
                             is_area: bool = False) -> float:
    """The damage per hit a player needs to kill this enemy in so many hits.

    This is the number gear has to produce, and it is an OUTPUT of the enemy
    design rather than an input to it. EVERY defensive layer the enemy has is
    counted, and the player's resistance penetration and armour penetration each
    cut into the layer they are for.

    `hits` MEANS SWINGS ATTEMPTED, NOT BLOWS LANDED, because evasion is counted.
    Against an enemy that avoids nothing the two are the same thing.

    IT USED TO COUNT RESISTANCE AND NOTHING ELSE, which understated it by 48%
    against the Abyssal Warden and by 56% against the Gatekeeper -- the two most
    armoured creatures in the vertical slice. Issue #481.

    It used to take a damage type, because enemies resisted the eight types
    separately. They do not any more: player damage is adaptive, so one enemy
    resistance figure applies whatever the player is wielding.
    """
    through = enemy.damage_taken_fraction(penetration, armor_penetration,
                                          is_area)
    return enemy.effective_health / (max(hits, 1e-9) * max(through, 1e-9))


if __name__ == "__main__":
    TIER = 8
    print("Enemy stat blocks. Issue #97.")
    print()
    print("Rarity scales magnitude. Archetype supplies the profile.")
    print()

    print(f"The rarity ladder on its own, tier {TIER}, last floor of a 50-floor")
    print("Cataclysm dungeon. This is the baseline archetype, every multiplier")
    print("at 1, so it shows what rarity alone does:")
    print()
    print(f"    {'rarity':<15} {'score':>6} {'health':>9} {'hit':>8} {'armor':>7}")
    print("    " + "-" * 50)
    for rarity in RARITY_ORDER:
        e = stats_on_floor(rarity, TIER, "Cataclysm")
        print(f"    {rarity:<15} {e.score:>6,.0f} {e.health:>9,.0f} "
              f"{e.damage_per_hit:>8,.0f} {e.armor:>7,.0f}")
    print()

    common = stats_on_floor("Common", TIER, "Cataclysm")
    cb = stats_on_floor("Cataclysm Boss", TIER, "Cataclysm")
    print(f"    A Cataclysm Boss has {cb.health / common.health:.0f}x a Common enemy's health "
          f"and hits {cb.damage_per_hit / common.damage_per_hit:.1f}x as hard.")
    print("    Health still grows faster, so the rarest things are long fights")
    print("    AND dangerous, rather than only one or the other.")
    print()

    print("=" * 78)
    print("The seven Demonic Cataclysm enemies, each at the rarity it is")
    print(f"normally met at, tier {TIER}:")
    print()
    AT = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
          ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
          ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))
    print(f"    {'enemy':<28} {'health':>9} {'shield':>8} {'hit':>9} "
          f"{'every':>6} {'armor':>8} {'resist':>7} {'speed':>6} {'evade':>6}")
    print("    " + "-" * 94)
    for name, rarity in AT:
        e = stats_on_floor(rarity, TIER, "Cataclysm", kind=name)
        print(f"    {e.name:<28} {e.health:>9,.0f} {e.energy_shield:>8,.0f} "
              f"{e.damage_per_hit:>9,.0f} {e.attack_interval:>5.1f}s "
              f"{e.armor:>8,.0f} {e.resistance:>6.0f}% {e.move_speed:>6.1f} "
              f"{e.evasion:>5.0f}%")
    print()
    print("    Same rarity, different creature: an Elite Succubus and an Elite")
    print("    Brute share a score and share nothing else.")
    print()

    print("    Resistance is ONE figure applied to all incoming damage, not one")
    print("    per damage type. Player damage is adaptive -- a weapon deals one")
    print("    number rather than eight pools -- so a per-type profile would")
    print("    change no outcome. The player still has all eight defensively,")
    print("    because eight Cataclysms attack them.")
    print()
    print("    That one figure is what the player's resistance penetration")
    print("    works on, against the hardest thing in the vertical slice:")
    print()
    warden = stats_on_floor("Herald", TIER, "Cataclysm", kind="Abyssal Warden")
    for pen in (0.0, 10.0, 20.0, 35.0):
        through = warden.damage_taken_fraction(pen)
        print(f"      {pen:>4.0f} penetration -> {through:>5.0%} of the player's "
              f"hit lands, {player_damage_to_kill_in(warden, 30.0, pen):>7,.0f} "
              "needed to kill it in 30")
    print()
    print("    Resistance is not the only layer, and until issue #481 it was")
    print("    the only one anything read. Every layer this creature has, in")
    print("    the order a hit meets them:")
    print()
    bare = warden.defender_for()
    for label, effect in (
        (f"armour {warden.armor:,.0f} at tier {TIER}",
         f"removes {damage.armor_reduction(warden.armor, TIER):.1f}% of the hit"),
        (f"resistance {warden.resistance:.0f}%",
         f"removes {warden.resistance:.1f}% of what is left"),
        (f"evasion {warden.evasion:.0f}%",
         f"avoids {warden.evasion:.1f}% of direct attacks entirely"),
        (f"block {bare.block_chance:.0f}%, "
         f"flat reduction {bare.damage_reduction:.0f}%",
         "enemies have neither yet, issue #488"),
    ):
        print(f"      {label:<34} {effect}")
    print(f"      {'TOGETHER':<34} {warden.damage_taken_fraction():.1%} of the "
          "hit reaches its health and shield")
    print()

    print("=" * 78)
    print("The ceiling on the combination, issue #483. A creature's armour is a")
    print("share of its Power Score and a score has no maximum, so the column")
    print("below is each archetype at the 75% armour cap: the most it could ever")
    print("stop, not what it stops at any particular tier and rarity.")
    print()
    print(f"    {'archetype':<20} {'stops at most':>14} {'headroom':>10}")
    print("    " + "-" * 46)
    for name in ARCHETYPES:
        most = most_damage_stopped(name, RARITY_ORDER[-1])
        print(f"    {name:<20} {most:>13.2f}% "
              f"{ENEMY_MITIGATION_CEILING - most:>9.2f}")
    print()
    print(f"    The ceiling is {ENEMY_MITIGATION_CEILING:.0f}%, which is what the")
    print("    reference geared character stops at tier 8. No enemy may stop")
    print("    more than the player does.")
    print()
    print("    Armour caps at 75% and resistance at 70%, so those two alone")
    print("    reach 92.5% stopped with neither one over its own cap. That is")
    print("    why the check is on the combination and not on any one field.")
    print()

    print("    Armour alone is why the figure below nearly doubled. Reading")
    print("    resistance and nothing else said 3,929 where the answer is "
          f"{player_damage_to_kill_in(warden, 30.0):,.0f}.")
    print()

    print("=" * 78)
    print("What this implies for the player. REPORTED, NOT ASSERTED.")
    print()
    from .character import Attributes, Character
    from .classes import DEMONIC_CLASSES

    rav = Character(DEMONIC_CLASSES["Ravager"], level=100,
                    attributes=Attributes(vitality=100))
    pool = rav.stat("max_health") + rav.stat("max_energy_shield")
    print(f"    A level 100 Ravager with no gear has {pool:,.0f} effective health,")
    print("    and no gear is not a real state at tier 8. It is a floor.")
    print()
    print(f"    {'enemy':<28} {'its hits you survive':>21} {'lands for':>11} "
          f"{'per hit to kill it in 30':>26}")
    print("    " + "-" * 90)
    for name, rarity in AT:
        e = stats_on_floor(rarity, TIER, "Cataclysm", kind=name)
        print(f"    {e.name:<28} {hits_to_kill_player(e, pool):>21.1f} "
              f"{e.damage_taken_fraction():>10.1%} "
              f"{player_damage_to_kill_in(e, 30.0):>26,.0f}")
    print()
    print("    The Gatekeeper one-shots a gearless character several times over,")
    print("    which is what the project owner asked for. What gear has to")
    print("    supply is the last column, and it is an output of this file")
    print("    rather than an input to it.")
    print()
    print("    The middle column is every mitigation layer the creature has,")
    print("    multiplied together. Until issue #481 it read resistance alone,")
    print("    so the last column was too low everywhere and worst against the")
    print("    two most armoured creatures: the Abyssal Warden by 48% and the")
    print("    Gatekeeper by 56%.")
