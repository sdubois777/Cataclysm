"""Turning an Enemy Score into health, damage per hit, and how often it hits.

WHAT THIS IS FOR. `scoring.py` gives every enemy a Power Score, and that score is
authoritative and verified. It is a power RATING. Nothing anywhere in the project
said how much damage an enemy deals or how much health it has, so nothing could
actually be played or balanced. Issue #97.

I checked the separate DungeonSimulator repository, which is where the scoring
model lives, across its whole history. It has never contained a damage or health
number in any commit. There was nothing to port.

THE TARGETS THIS IS SOLVED FROM, set by the project owner:

    A common enemy of equal score takes 8 to 10 non-critical hits to kill the
    player. The player kills that enemy in 1 to 3 non-critical hits.

THE ANCHOR THAT MADE IT SOLVABLE. Those targets fix ratios, not numbers: the
second one is enemy health divided by player damage, and player damage per hit
did not exist either. What closed it is that a mid-durability character's
effective health turns out to track the Power Score anchors within a few percent
at every tier -- 434 against 385 at tier 1, 6,330 against 6,327 at tier 8, for
the Ravager. That was not designed; the class stat lines in `classes.py` and the
Power Score formula in `player_power.py` were calibrated separately and against
different things.

So player effective health is approximately the player's Power Score, and
everything else can be expressed as a fraction of a score.

WHY THE FACTORS VARY BY RARITY. Rarity should change the SHAPE of a fight, not
only its size. Under a single pair of factors, a boss is a common enemy with more
health -- it hits for the same amount at the same rate and simply takes longer to
remove, which is a damage sponge rather than a different fight. So common enemies
hit often for little and die fast, and bosses hit rarely for a great deal and
take a long time to kill.

That argument stands on its own. It is worth noting what it deliberately does NOT
rest on: a Bulwark passive tree keystone triggers on absorbing "a single hit
exceeding 5,000 flat damage", which is the only absolute damage figure anywhere
in the project. It is tempting to treat that as an anchor. It is not one. The
project owner's direction is that passive tree content can change and the engine
takes precedence, so a tree node is evidence of intent at most, and the numbers
here are not fitted to it.

WHAT THESE COMPARISONS ASSUME, AND WHERE THAT BREAKS DOWN. Every player figure
below is a character with NO GEAR AT ALL, because flat health and mitigation from
gear have no values anywhere yet. That is fine against common enemies, where the
targets are met. It is misleading against bosses: a gearless character at tier 8
is not a real tier 8 character, and the boss rows show it being killed in very
few hits. Read those rows as "this is what gear has to close", not as a balance
result.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import scoring

# --------------------------------------------------------------------------
# The player side
# --------------------------------------------------------------------------

#: A player's damage per non-critical hit, as a fraction of their Power Score.
#:
#: Solved from the target of 1 to 3 hits to kill a common enemy, together with
#: the common health factor below: 0.5 / 0.25 is 2 hits, the middle of the range.
#:
#: Sanity check against the one other source of player damage figures: the
#: Bulwark tree grants 50, 100, 200 and 500 flat damage per passive point. At
#: tier 8 this puts a base hit at about 1,580, so those nodes are meaningful
#: additions rather than the whole of a character's damage, which is right.
PLAYER_DAMAGE_FACTOR = 0.25


def player_damage_per_hit(player_power: float) -> float:
    return player_power * PLAYER_DAMAGE_FACTOR


# --------------------------------------------------------------------------
# The enemy side
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class RarityProfile:
    """How one rarity converts its score into a fight.

    `health` and `damage` are fractions of the enemy's own score.
    `attack_interval` is seconds between its attacks.
    """

    health: float
    damage: float
    attack_interval: float


#: Common is SOLVED from the two targets. Everything above it is proposed.
#:
#: The rarer profiles are deliberately less extreme than a first pass produced.
#: Pushing boss damage high enough to make a gearless character die in one hit
#: says nothing useful, because a gearless character is not a real tier 8
#: character. These are set so that even with no gear at all a boss takes several
#: hits to kill the player, which leaves gear room to make the fight longer
#: rather than room to make it survivable at all.
#:
#: Rarity names match `scoring.RARITY_WEIGHTS`, which is the authoritative list.
#: Note it has Herald and Cataclysm Boss and no Rare; the design document's list
#: is the superseded one. See issue #30.
RARITY_PROFILES: dict[str, RarityProfile] = {
    "Common":         RarityProfile(health=0.50, damage=0.11, attack_interval=1.5),
    "Elite":          RarityProfile(health=1.20, damage=0.14, attack_interval=1.8),
    "Legendary":      RarityProfile(health=2.50, damage=0.17, attack_interval=2.0),
    "Herald":         RarityProfile(health=4.00, damage=0.20, attack_interval=2.5),
    "Boss":           RarityProfile(health=7.00, damage=0.24, attack_interval=3.0),
    "Cataclysm Boss": RarityProfile(health=10.0, damage=0.28, attack_interval=3.5),
}


def profile_for(rarity: str) -> RarityProfile:
    if rarity not in RARITY_PROFILES:
        raise ValueError(
            f"unknown rarity {rarity!r}; expected one of "
            f"{sorted(RARITY_PROFILES)}")
    return RARITY_PROFILES[rarity]


def enemy_health(score: float, rarity: str) -> float:
    return max(1.0, score * profile_for(rarity).health)


def enemy_damage_per_hit(score: float, rarity: str) -> float:
    return max(0.0, score * profile_for(rarity).damage)


def enemy_damage_per_second(score: float, rarity: str) -> float:
    p = profile_for(rarity)
    return score * p.damage / p.attack_interval


def hits_to_kill_enemy(score: float, rarity: str, player_power: float) -> float:
    """How many non-critical player hits an enemy of this score survives."""
    damage = player_damage_per_hit(player_power)
    return enemy_health(score, rarity) / max(damage, 1e-9)


def hits_to_kill_player(score: float, rarity: str,
                        player_effective_health: float,
                        mitigation_fraction: float = 0.0) -> float:
    """How many of this enemy's hits the player survives.

    `mitigation_fraction` is everything the damage calculation removes, from 0
    for a character with no armor, resistance or shield up to just under 1.
    """
    per_hit = enemy_damage_per_hit(score, rarity) * (1.0 - mitigation_fraction)
    return player_effective_health / max(per_hit, 1e-9)


if __name__ == "__main__":
    from .character import Attributes, Character
    from .classes import DEMONIC_CLASSES
    from .player_power import reference_character

    print("Enemy health and damage, derived from Enemy Score. Issue #97.")
    print()
    print("Targets set by the project owner:")
    print("  a common enemy of equal score kills the player in 8-10 hits")
    print("  the player kills that enemy in 1-3 hits")
    print()

    print("Check against the Ravager, which is the middle of the three classes")
    print("for durability, on the last floor of a 50-floor Basic dungeon:")
    print()
    print(f"    {'tier':>5} {'enemy':>7} {'its hp':>8} {'its hit':>8} "
          f"{'player hp':>10} {'player hit':>11} {'hits to':>8} {'hits to':>8}")
    print(f"    {'':>5} {'score':>7} {'':>8} {'':>8} {'':>10} {'':>11} "
          f"{'kill it':>8} {'kill you':>8}")
    print("    " + "-" * 76)
    for tier in (1, 4, 8):
        ref = reference_character(tier)
        c = Character(DEMONIC_CLASSES["Ravager"], level=ref.level,
                      attributes=Attributes(vitality=ref.level))
        pool = c.stat("max_health") + c.stat("max_energy_shield")
        score = scoring.enemy_scores(tier, "Basic", "None", 50, 50)["Common"]
        power = scoring.PLAYER_MAX_SCORES[tier]
        print(f"    {tier:>5} {score:>7,} {enemy_health(score, 'Common'):>8,.0f} "
              f"{enemy_damage_per_hit(score, 'Common'):>8,.0f} {pool:>10,.0f} "
              f"{player_damage_per_hit(power):>11,.0f} "
              f"{hits_to_kill_enemy(score, 'Common', power):>8.1f} "
              f"{hits_to_kill_player(score, 'Common', pool):>8.1f}")
    print()

    print("Rarity changes the shape of a fight, not only its size. Tier 8,")
    print("Cataclysm dungeon, last floor, against a Ravager:")
    print()
    ref = reference_character(8)
    rav = Character(DEMONIC_CLASSES["Ravager"], level=100,
                    attributes=Attributes(vitality=100))
    pool8 = rav.stat("max_health") + rav.stat("max_energy_shield")
    scores8 = scoring.enemy_scores(8, "Cataclysm", "None", 50, 50)
    print(f"    {'rarity':<15} {'score':>7} {'its hp':>9} {'its hit':>8} "
          f"{'every':>7} {'hits to':>8} {'hits to':>8}")
    print(f"    {'':<15} {'':>7} {'':>9} {'':>8} {'':>7} {'kill it':>8} "
          f"{'kill you':>8}")
    print("    " + "-" * 70)
    for rarity in RARITY_PROFILES:
        s = scores8[rarity]
        p = profile_for(rarity)
        print(f"    {rarity:<15} {s:>7,} {enemy_health(s, rarity):>9,.0f} "
              f"{enemy_damage_per_hit(s, rarity):>8,.0f} "
              f"{p.attack_interval:>6.1f}s "
              f"{hits_to_kill_enemy(s, rarity, 6327):>8.0f} "
              f"{hits_to_kill_player(s, rarity, pool8):>8.1f}")
    print()
    biggest = enemy_damage_per_hit(scores8["Cataclysm Boss"], "Cataclysm Boss")
    print(f"    The largest single hit in the game is {biggest:,.0f}.")
    print()
    print("    Every player figure above is a character with NO GEAR, because")
    print("    gear has no health or mitigation values anywhere yet. Against")
    print("    common enemies the targets are met and that is fine. The boss")
    print("    rows are what gear has to close, not a balance result.")
