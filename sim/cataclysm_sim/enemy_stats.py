"""A full stat block for every enemy, derived from its Enemy Score.

WHAT THIS IS FOR. `scoring.py` gives every enemy a Power Score, and that score is
authoritative and verified. It is a power RATING: nothing in it says how much
health an enemy has, how hard it hits, how often, or what it resists. Issue #97.

I checked the separate DungeonSimulator repository, where the scoring model
lives, across its whole history. It has never contained a damage or health number
in any commit, so there was nothing to port.

HOW THIS IS BUILT, AND WHY IT CHANGED. An earlier version solved these numbers
backwards from player-side targets: how many hits a player should survive, how
many hits a player needs. That put the player first and left the enemy as a
consequence, and it kept producing conflicts -- most recently, an increased
damage affix the project owner wanted was eight times what the derived damage
target could absorb.

The order is now the other way round, at the project owner's direction. Enemy
stats are set on their own terms and player gear will be fitted to them. So
nothing here is constrained by a player number, and the player-side figures at
the bottom are REPORTED rather than asserted.

ONE FORMULA, SIX RARITIES. Every rarity is the same formula with one input: how
far above Common it sits. That is what makes this a base class per rarity rather
than six hand-written rows that drift apart the moment one is edited.

    score-scaled stats   health, damage, armor
                         value = score x factor, and the factor grows with rarity

    rarity traits        attack interval, resistance, penetration, critical
                         strike chance and multiplier, movement speed
                         these do not scale with score at all: they are what
                         KIND of thing the enemy is, not how big it is

A Cataclysm Boss is therefore not a large Common enemy. It has proportionally
more health and damage, and it also attacks differently: slower, more likely to
crit, harder to hurt through armour, and carrying penetration that punishes a
player sitting exactly on the resistance cap.

WHAT THIS DOES NOT COVER. Individual enemies. The design names seven Demonic
enemies -- the Imp swarms, the Brute is heavily armoured and slow, the Corrupted
Sentinel does not move at all. Those are modifiers on top of a rarity base, and
they belong with the enemy design work in issues #29 and #39. This is the base
class each of them starts from.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import scoring

# --------------------------------------------------------------------------
# The rarity ladder
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


# --------------------------------------------------------------------------
# Score-scaled stats: how BIG the enemy is
# --------------------------------------------------------------------------

#: Health as a fraction of score at Common, multiplied per step of rarity.
#: 1.85 per step takes a Cataclysm Boss to roughly 21 times a Common enemy's
#: health, which is what makes a boss fight last rather than a boss simply hit
#: harder.
HEALTH_AT_COMMON = 0.50
HEALTH_PER_STEP = 1.85

#: Damage grows far more slowly than health. A boss that hit 21 times as hard as
#: a common enemy AND had 21 times the health would not be a fight.
DAMAGE_AT_COMMON = 0.11
DAMAGE_PER_STEP = 1.21

#: Armour, so that the player's armour penetration has something to work on. A
#: Common enemy has none: swarm fodder should die to anything.
ARMOR_PER_STEP = 0.15

# --------------------------------------------------------------------------
# Rarity traits: what KIND of thing the enemy is
# --------------------------------------------------------------------------

#: Seconds between attacks. Rarer enemies wind up more slowly, so their damage
#: per second rises much less steeply than their damage per hit. Without this a
#: boss is a common enemy with more health.
INTERVAL_AT_COMMON = 1.5
INTERVAL_PER_STEP = 1.18

#: Percentage resistance to every damage type, per step. A Common enemy resists
#: nothing.
RESISTANCE_PER_STEP = 10.0

#: Percentage points of the player's resistance ignored, per step.
#:
#: This is the reason over-capping resistance is worth anything. A player sitting
#: exactly on the 70% cap drops to 45% against a Cataclysm Boss; one who has
#: over-capped to 95% stays at the cap. The design says resistance is reduced by
#: enemy penetration scaling and never says by how much. This is that number.
PENETRATION_PER_STEP = 5.0

#: Critical strikes. Rarer enemies crit more often and harder, which is what
#: makes a boss hit occasionally feel like a spike rather than a metronome.
CRIT_CHANCE_AT_COMMON = 5.0
CRIT_CHANCE_PER_STEP = 3.0
CRIT_MULTIPLIER_AT_COMMON = 150.0
CRIT_MULTIPLIER_PER_STEP = 25.0

#: Metres per second, on the same scale as the character sheet. Rarer enemies are
#: slower, so a player can create distance from a boss and not from a swarm.
#: Individual enemies override this: the design's Imp is fast and its Corrupted
#: Sentinel is stationary.
MOVE_SPEED_AT_COMMON = 4.5
MOVE_SPEED_PER_STEP = -0.3


@dataclass(frozen=True)
class EnemyStats:
    """One enemy's complete stat block."""

    rarity: str
    score: float

    health: float
    damage_per_hit: float
    armor: float

    attack_interval: float
    resistance: float
    penetration: float
    crit_chance: float
    crit_multiplier: float
    move_speed: float

    @property
    def damage_per_second(self) -> float:
        return self.damage_per_hit / self.attack_interval

    @property
    def average_damage_per_hit(self) -> float:
        """Including critical strikes, over many hits."""
        chance = self.crit_chance / 100.0
        multiplier = self.crit_multiplier / 100.0
        return self.damage_per_hit * (1.0 - chance + chance * multiplier)


def stats_for(rarity: str, score: float) -> EnemyStats:
    """The whole stat block for an enemy of this rarity and score."""
    n = rarity_step(rarity)
    score = max(0.0, score)

    return EnemyStats(
        rarity=rarity,
        score=score,
        health=max(1.0, score * HEALTH_AT_COMMON * HEALTH_PER_STEP ** n),
        damage_per_hit=score * DAMAGE_AT_COMMON * DAMAGE_PER_STEP ** n,
        armor=score * ARMOR_PER_STEP * n,
        attack_interval=INTERVAL_AT_COMMON * INTERVAL_PER_STEP ** n,
        resistance=RESISTANCE_PER_STEP * n,
        penetration=PENETRATION_PER_STEP * n,
        crit_chance=CRIT_CHANCE_AT_COMMON + CRIT_CHANCE_PER_STEP * n,
        crit_multiplier=CRIT_MULTIPLIER_AT_COMMON + CRIT_MULTIPLIER_PER_STEP * n,
        move_speed=max(0.0, MOVE_SPEED_AT_COMMON + MOVE_SPEED_PER_STEP * n),
    )


def stats_on_floor(rarity: str, tier: int, dungeon_type: str = "Basic",
                   subtype: str = "None", total_floors: int = 50,
                   floor: int | None = None,
                   modifier_score: float = 0.0) -> EnemyStats:
    """The stat block for an enemy standing on a particular dungeon floor."""
    floor = total_floors if floor is None else floor
    scores = scoring.enemy_scores(tier, dungeon_type, subtype, total_floors,
                                  floor, modifier_score)
    return stats_for(rarity, scores[rarity])


# --------------------------------------------------------------------------
# Reported, not asserted: what this implies for a player
# --------------------------------------------------------------------------

def hits_to_kill_player(enemy: EnemyStats, player_effective_health: float,
                        mitigation_fraction: float = 0.0) -> float:
    """How many of this enemy's hits a player survives, counting criticals."""
    per_hit = enemy.average_damage_per_hit * (1.0 - mitigation_fraction)
    return player_effective_health / max(per_hit, 1e-9)


def player_damage_to_kill_in(enemy: EnemyStats, hits: float) -> float:
    """The damage per hit a player needs to kill this enemy in so many hits.

    This is the number the player's gear has to produce, and it is now an OUTPUT
    of the enemy design rather than an input to it.
    """
    return enemy.health / max(hits, 1e-9)


if __name__ == "__main__":
    print("Enemy stat blocks, derived from Enemy Score. Issue #97.")
    print()
    print("One formula, six rarities. The only input is how far above Common a")
    print("rarity sits. Score-scaled stats say how big an enemy is; rarity")
    print("traits say what kind of thing it is.")
    print()

    TIER = 8
    print(f"Tier {TIER}, last floor of a 50-floor Cataclysm dungeon:")
    print()
    print(f"    {'rarity':<15} {'score':>6} {'health':>9} {'hit':>7} {'armor':>7} "
          f"{'every':>6} {'resist':>7} {'pen':>5} {'crit':>6} {'speed':>6}")
    print("    " + "-" * 88)
    for rarity in RARITY_ORDER:
        e = stats_on_floor(rarity, TIER, "Cataclysm")
        print(f"    {rarity:<15} {e.score:>6,.0f} {e.health:>9,.0f} "
              f"{e.damage_per_hit:>7,.0f} {e.armor:>7,.0f} "
              f"{e.attack_interval:>5.1f}s {e.resistance:>6.0f}% "
              f"{e.penetration:>4.0f}% {e.crit_chance:>5.0f}% "
              f"{e.move_speed:>5.1f}")
    print()

    common = stats_on_floor("Common", TIER, "Cataclysm")
    boss = stats_on_floor("Cataclysm Boss", TIER, "Cataclysm")
    print(f"    A Cataclysm Boss has {boss.health / common.health:.0f}x a Common "
          f"enemy's health but hits only {boss.damage_per_hit / common.damage_per_hit:.1f}x "
          "as hard,")
    print(f"    and {boss.damage_per_second / common.damage_per_second:.1f}x as hard per second because it winds up "
          "more slowly.")
    print("    That is what makes it a longer fight rather than a bigger number.")
    print()

    print("    Penetration is why over-capping resistance is worth anything:")
    for rarity in ("Common", "Boss", "Cataclysm Boss"):
        e = stats_on_floor(rarity, TIER, "Cataclysm")
        at_cap = 70.0 - e.penetration
        over = min(70.0, 95.0 - e.penetration)
        print(f"      against a {rarity:<15} a player at the 70% cap keeps "
              f"{at_cap:>4.0f}%, one over-capped to 95% keeps {over:>4.0f}%")
    print()

    print("=" * 72)
    print("What this implies for the player. REPORTED, NOT ASSERTED.")
    print("The enemy side is set on its own terms; gear will be fitted to it.")
    print()
    from .character import Attributes, Character
    from .classes import DEMONIC_CLASSES

    rav = Character(DEMONIC_CLASSES["Ravager"], level=100,
                    attributes=Attributes(vitality=100))
    pool = rav.stat("max_health") + rav.stat("max_energy_shield")
    print(f"    A level 100 Ravager with no gear has {pool:,.0f} effective health.")
    print()
    # A single player damage number has to serve every rarity, so the binding
    # constraint is whichever rarity demands the most. Comparing each rarity
    # against its own hit target would make the easy ones look trivial and hide
    # which one actually sets the requirement.
    BOSS_FIGHT_HITS = 30.0
    boss_target = player_damage_to_kill_in(boss, BOSS_FIGHT_HITS)
    print(f"    A Cataclysm Boss should take about {BOSS_FIGHT_HITS:.0f} hits to kill, which")
    print(f"    needs {boss_target:,.0f} damage per hit. That one number is what gear has")
    print("    to deliver, and this is what it does to everything else:")
    print()
    print(f"    {'rarity':<15} {'hits to kill it':>16} {'hits it survives you':>21}")
    print("    " + "-" * 55)
    for rarity in RARITY_ORDER:
        e = stats_on_floor(rarity, TIER, "Cataclysm")
        print(f"    {rarity:<15} {hits_to_kill_player(e, pool):>16.1f} "
              f"{e.health / boss_target:>21.1f}")
    print()
    print("    So a character geared to fight the boss kills common enemies in")
    print(f"    {common.health / boss_target:.1f} hits, which is the range the project owner set for")
    print("    them without that having constrained anything here.")
    print()
    print("    Those player figures assume NO GEAR and no mitigation, so they")
    print("    are a floor. Gear closes the difference, and what gear has to")
    print("    supply is now an output of this rather than an input to it.")
