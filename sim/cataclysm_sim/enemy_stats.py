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
Brute's stomp stun, the Gatekeeper's phases and the Abyssal Warden's positional
weak points are behaviour, not statistics. They belong with the enemy design work
in issues #29 and #39. This is the stat block each of them stands on.
`enemy_abilities.py` is where that behaviour goes as it is designed, one enemy at
a time; the Imp is the only one filled in so far (#348).

ONE FIELD HERE IS A BODY MEASUREMENT RATHER THAN A COMBAT STATISTIC.
`body_radius` says how wide the creature is, and it exists because how many of a
swarm can stand around one player at once is a geometry question, not a combat
one. It lives here because it is fixed per archetype exactly like movement speed
and attack interval are.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import scoring
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
            attack_interval=2.8, crit_multiplier=200.0, move_speed=2.5,
            # Thick hide on top of the armour, which is its main defence.
            resistance=15.0,
            # 180 rather than every other enemy's 480, because "can be
            # outmanoeuvred" has to be a number. A player circling at the
            # Brute's own reach turns at 223 degrees per second even in the
            # slowest Demonic class, so anything under that can be got behind
            # by every build. Issue #351.
            turn_rate_degrees=180.0,
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


def _check_no_enemy_can_become_immune() -> None:
    """Resistance is one number now, so nothing else caps it.

    The player's own resistance caps at 70% and armour at 75%, and the design
    states plainly that no combination of defensive layers reaches immunity.
    The same has to hold for enemies, or a player's damage could be reduced to
    nothing by a value nobody noticed was too large.
    """
    for kind in ARCHETYPES.values():
        assert kind.resistance < 70.0, (
            f"{kind.name} resists {kind.resistance}% of all damage, at or above "
            "the 70% the design caps resistance at")


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


_check_every_archetype_deals_a_real_damage_type()
_check_no_enemy_can_become_immune()
_check_every_body_has_a_width()
_check_every_creature_can_turn()


def archetype(name: str) -> Archetype:
    if name not in ARCHETYPES:
        raise ValueError(
            f"unknown archetype {name!r}; expected one of {sorted(ARCHETYPES)}")
    return ARCHETYPES[name]


# --------------------------------------------------------------------------
# The stat block
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class EnemyStats:
    """One enemy's complete stat block."""

    archetype: Archetype
    rarity: str
    score: float

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

    def damage_taken_fraction(self, penetration: float = 0.0) -> float:
        """Share of a player's hit that gets through this enemy's resistance.

        Player resistance penetration is subtracted before anything else, the
        same way it is on the player's own side of the calculation.
        """
        return 1.0 - max(0.0, self.resistance - penetration) / 100.0


def stats_for(rarity: str, score: float,
              kind: Archetype | str = BASELINE) -> EnemyStats:
    """The whole stat block for one enemy.

    `rarity` sets how big it is, `kind` sets what it is, `score` is what
    `scoring.py` says the encounter is worth.
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
    return stats_for(rarity, scores[rarity], kind)


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
                             penetration: float = 0.0) -> float:
    """The damage per hit a player needs to kill this enemy in so many hits.

    This is the number gear has to produce, and it is an OUTPUT of the enemy
    design rather than an input to it. The enemy's resistance is counted, and
    the player's own resistance penetration reduces it.

    It used to take a damage type, because enemies resisted the eight types
    separately. They do not any more: player damage is adaptive, so one enemy
    resistance figure applies whatever the player is wielding.
    """
    through = enemy.damage_taken_fraction(penetration)
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
    print(f"    {'enemy':<28} {'its hits you survive':>21} {'per hit to kill it in 30':>26}")
    print("    " + "-" * 78)
    for name, rarity in AT:
        e = stats_on_floor(rarity, TIER, "Cataclysm", kind=name)
        print(f"    {e.name:<28} {hits_to_kill_player(e, pool):>21.1f} "
              f"{player_damage_to_kill_in(e, 30.0):>26,.0f}")
    print()
    print("    The Gatekeeper one-shots a gearless character several times over,")
    print("    which is what the project owner asked for. What gear has to")
    print("    supply is the last column, and it is an output of this file")
    print("    rather than an input to it.")
