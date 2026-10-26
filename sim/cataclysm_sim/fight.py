"""The Corrupted Stalker against the tier's reference character, set beside a Boss on the same floor.

WHAT IT IS FOR. The game's Corrupted Stalker (issue #1308) carries a player's numbers: its entry's Ravager class line
at the tier's reference level, its eighteen pieces at the highest affix tier a drop carries and the reference
character's gear level, and thirteen passive points. Its health and attack damage are then multiplied by placeholders, 1.2 rising evenly to 5.0 for health and 1.0
for damage, ruled by the coordinating session on 2026-10-25 as layer 1's. This model brings the replacements: per
tier, the health multiplier that makes the reference character take as long to kill the Stalker as to kill a Boss on
the same floor, and the damage multiplier that makes the Stalker kill the reference character as fast as that Boss
does.

THE BOSS IS THE SIMULATION'S, ruled the same day: `enemy_stats.stats_on_floor("Boss", tier, "Basic", "None", 50,
floor=25)`, the middle floor of a fifty-floor Basic dungeon. A reading recorded with that ruling: the game's own
creatures are not scaled by tier, so a Boss in the game is the same at tier 1 and tier 8, and this model's Boss is the
design's.

ITS POTIONS ARE COUNTED, as ruled: it holds the player's four slots of thirty charges, ten a drink, each drink healing
35% of its maximum health over three seconds, one at a time. A fight as long as the Boss's leaves time for
`min(12, seconds // 3)` drinks, each adding 35% of its maximum health to what the player must deal. A JUDGEMENT: every
drink is taken as poured in full, which is the most the potions can add.

WHAT IT CANNOT KNOW. The game builds the Stalker through `UCataclysmPlayerClassStats::ApplyTo` and this through
`Character`; the two are pinned to the same class line and affixes elsewhere, but its built health and attack damage
here are not the game's measured ones. The game test
`Cataclysm.DungeonModifierEffects.WhatACorruptedStalkerIsBuiltWithAtEachTier` prints those, to be compared. Each affix
is taken at a roll of one half, the mean of the game's draw. Critical strikes are left out on both sides.

Pure standard library, as the whole simulation is. Run it: `python -m cataclysm_sim.fight` from `sim/`.
"""

from __future__ import annotations

import math
from collections import Counter
from dataclasses import dataclass

from . import affixes as af
from . import damage as dmg
from . import enemy_stats as es
from . import player_damage as pd
from . import player_power as pp
from . import reference_build as rb
from .character import DAMAGE_TYPES, Attributes, Character, Gear
from .classes import DEMONIC_CLASSES

#: The floor the Boss stands on: the middle of a fifty-floor Basic dungeon, ruled 2026-10-25.
BOSS_DUNGEON = ("Basic", "None", 50, 25)

#: The Stalker's seconds between two blows: `CorruptedStalkerSecondsBetweenSkillUses` in the game.
STALKER_SECONDS_BETWEEN_BLOWS = 0.75

#: The player's potion rules, as `UCataclysmPotions` states them.
POTION_SLOTS = 4
POTION_MAX_CHARGES = 30
POTION_CHARGES_PER_DRINK = 10
POTION_HEAL_SHARE = 0.35
POTION_HEAL_SECONDS = 3.0

#: The Demonic entry's pieces: base, and its two affixes. `ACataclysmCorruptedStalkerCharacter`'s `DemonicEntry`.
DEMONIC_PIECES: tuple[tuple[str, tuple[af.StatAffix, af.StatAffix]], ...] = (
    ("Helm", (af.FLAT_HEALTH, af.INCREASED_ARMOR)),
    ("Cuirass", (af.FLAT_HEALTH, af.INCREASED_ARMOR)),
    ("Pauldrons", (af.FLAT_HEALTH, af.FLAT_ARMOR)),
    ("Gauntlets", (af.FLAT_DAMAGE, af.INCREASED_DAMAGE)),
    ("Greaves", (af.FLAT_HEALTH, af.FLAT_ARMOR)),
    ("Sabatons", (af.FLAT_HEALTH, af.FLAT_ARMOR)),
    ("Girdle", (af.FLAT_HEALTH, af.INCREASED_ARMOR)),
    ("Amulet", (af.FLAT_DAMAGE, af.INCREASED_DAMAGE)),
    ("Idol", (af.FLAT_DAMAGE, af.INCREASED_DAMAGE)),
) + (("Band", (af.FLAT_HEALTH, af.INCREASED_HEALTH)),) * 8 + (
    ("Greataxe", (af.FLAT_DAMAGE, af.INCREASED_DAMAGE)),
)

#: Thick Hide in full, twelve points: +1% increased armour and +0.5% increased maximum health a point.
THICK_HIDE = {"armor": 0.12, "max_health": 0.06}

#: The pieces whose affixes are attack damage, which belongs to the weapon and is counted by `player_damage`.
DAMAGE_PIECES = sum(1 for _, pair in DEMONIC_PIECES if af.FLAT_DAMAGE in pair)

#: The placeholders now in the game, for comparison.
PLACEHOLDER_HEALTH = (1.2, 5.0)
PLACEHOLDER_DAMAGE = 1.0


def placeholder_health_multiplier(tier: int) -> float:
    """The game's `CorruptedStalkerHealthMultiplier`: 1.2 at tier 1, rising evenly to 5.0 at tier 8."""
    low, high = PLACEHOLDER_HEALTH
    return low + (high - low) * (min(max(tier, 1), 8) - 1) / 7.0


def _defender(hero: Character, tier: int) -> dmg.Defender:
    return dmg.Defender(
        health=hero.stat("max_health"),
        energy_shield=hero.stat("max_energy_shield"),
        armor=hero.stat("armor"),
        evasion=hero.stat("evasion"),
        block_chance=hero.stat("block_chance"),
        damage_reduction=hero.stat("damage_reduction"),
        resistances={d: hero.stat(f"resistance_{d.lower()}") for d in DAMAGE_TYPES},
        crowd_control_resistance=hero.stat("crowd_control_resistance"),
        tier=tier,
    )


def reference_player(tier: int) -> Character:
    """The tier's reference character: the reference build at the tier's level, affix tier and gear level.

    Its 100 attribute points are spread as the reference build's 60 vitality and 40 constitution, in proportion to
    the level, so a level-12 character spends twelve.
    """
    level = pp.reference_character(tier).level
    attributes = Attributes(vitality=round(60 * level / 100), constitution=round(40 * level / 100))
    return Character(DEMONIC_CLASSES[rb.CLASS_NAME], level=level, attributes=attributes,
                     gear=rb.gear(affix_tier=pd.affix_tier_at(tier), gear_level=pd.gear_level_at(tier)))


def stalker_gear(tier: int) -> Gear:
    """The Demonic entry's pieces at the highest affix tier a drop carries, roll one half, at the reference
    character's gear level (`player_damage.gear_level_at`), as the game's `PiecesAt` has carried them since
    2026-10-25. Until then this built them at gear level nought while `fight` took the Stalker's hit from
    `player_damage.damage_per_hit`, which is at the reference gear level, so its health and damage columns were at
    different gear levels."""
    flat: Counter[str] = Counter()
    increased: Counter[str] = Counter(THICK_HIDE)
    affix_tier = af.max_affix_tier_on_a_drop(tier)
    gear_level = pd.gear_level_at(tier)
    for base_name, pair in DEMONIC_PIECES:
        base = af.base_named(base_name)
        for implicit in base.implicits:
            value = implicit.value_at(gear_level, two_handed=base.value_multiplier != 1.0)
            (flat if implicit.kind == "flat" else increased)[implicit.stat] += (
                value if implicit.kind == "flat" else value / 100.0)
        for affix in pair:
            value = affix.value_at(affix_tier, roll=0.5, gear_level=gear_level)
            if affix.kind == "flat":
                flat[affix.stat] += value
            else:
                increased[affix.stat] += value / 100.0
    flat.pop("attack_damage", None)
    increased.pop("attack_damage", None)
    return Gear(flat=dict(flat), increased=dict(increased),
                weapon_base={"attack_speed": af.attack_speed_of(af.base_named("Greataxe"))})


def stalker(tier: int) -> Character:
    """The Stalker as the simulation builds it: the Ravager line at the reference level, no attribute points."""
    return Character(DEMONIC_CLASSES[rb.CLASS_NAME], level=pp.reference_character(tier).level,
                     attributes=Attributes(), gear=stalker_gear(tier))


def _landed(hit: float, damage_type: str, against: dmg.Defender) -> float:
    """The share of a hit of this size and type that reaches the defender's health."""
    attacker = dmg.Attacker(damage=hit, damage_type=damage_type)
    return dmg.average_damage_taken(attacker, against) / hit if hit > 0 else 0.0


@dataclass(frozen=True)
class StalkerFight:
    tier: int
    boss_seconds_to_kill: float
    boss_seconds_to_kill_the_player: float
    stalker_built_health: float
    stalker_hit: float
    drinks: int
    health_multiplier: float
    damage_multiplier: float
    player_dps_on_stalker: float

    def stalker_seconds_to_kill(self) -> float:
        """How long the reference character takes to kill the Stalker at its health multiplier, drinks included."""
        healed = POTION_HEAL_SHARE * self.drinks
        return self.stalker_built_health * self.health_multiplier * (1.0 + healed) / self.player_dps_on_stalker


def fight(tier: int) -> StalkerFight:
    """The multipliers that set the Stalker beside the Boss at a tier."""
    player = reference_player(tier)
    player_defender = _defender(player, tier)
    player_dps = pd.damage_per_second(tier)

    boss = es.stats_on_floor("Boss", tier, *BOSS_DUNGEON[:3], floor=BOSS_DUNGEON[3])
    boss_seconds = boss.effective_health / (player_dps * boss.damage_taken_fraction())
    boss_hit = boss.average_damage_per_hit
    boss_dps_on_player = boss_hit * _landed(boss_hit, boss.damage_type, player_defender) / boss.attack_interval
    boss_kills_in = player_defender.health / boss_dps_on_player

    hunter = stalker(tier)
    hunter_defender = _defender(hunter, tier)
    built_health = hunter.stat("max_health")
    player_hit = pd.damage_per_hit(tier)
    player_dps_on_stalker = player_dps * _landed(player_hit, "Demonic", hunter_defender)
    drinks = min(POTION_SLOTS * POTION_MAX_CHARGES // POTION_CHARGES_PER_DRINK,
                 int(boss_seconds // POTION_HEAL_SECONDS))
    health_multiplier = boss_seconds * player_dps_on_stalker / (built_health * (1.0 + POTION_HEAL_SHARE * drinks))

    hit = pd.damage_per_hit(tier, ("Greataxe",), flat_affixes=DAMAGE_PIECES, increased_affixes=DAMAGE_PIECES)
    landed = _landed(hit, "Demonic", player_defender)
    damage_multiplier = boss_dps_on_player * STALKER_SECONDS_BETWEEN_BLOWS / (hit * landed)
    return StalkerFight(tier, boss_seconds, boss_kills_in, built_health, hit, drinks, health_multiplier,
                        damage_multiplier, player_dps_on_stalker)


def table() -> list[StalkerFight]:
    return [fight(tier) for tier in range(1, 9)]


def report() -> str:
    lines = ["tier  boss TTK s  boss kills s  stalker health  stalker hit  drinks  health x (now)  damage x (now)"]
    for row in table():
        lines.append(f"{row.tier:>4}  {row.boss_seconds_to_kill:>10.1f}  {row.boss_seconds_to_kill_the_player:>12.1f}"
                     f"  {row.stalker_built_health:>14.0f}  {row.stalker_hit:>11.1f}  {row.drinks:>6}"
                     f"  {row.health_multiplier:>6.2f} ({placeholder_health_multiplier(row.tier):.2f})"
                     f"  {row.damage_multiplier:>6.2f} ({PLACEHOLDER_DAMAGE:.2f})")
    return "\n".join(lines)


if __name__ == "__main__":
    print(report())
    if not all(math.isfinite(r.health_multiplier) and math.isfinite(r.damage_multiplier) for r in table()):
        raise SystemExit("a multiplier is not finite")
