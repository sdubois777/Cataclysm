"""How often a gem must drop for the design's reference character to be reachable, and what the shipped rate gives.

Issue #46, slice G5. The coordinating session ruled on 2026-11-21 that gem drop rates are measured in the simulation.
This is the measurement. `loot.GEM_DROPS_PER_GEAR_DROP` is the figure it chooses from, and `loot.ENEMY_GEM_DROPS`
the Enemy Drops sheet's Gem Drops column it gives.

    cd sim && python analyse_gem_drops.py

WHAT IS DERIVED, AND FROM WHAT:

  - the kills in one difficulty tier: `analyse_experience_curve.py`'s three inputs, 26 dungeons a campaign, the
    average dungeon's floors over `config.DUNGEON_SPECS`, and the creatures on a floor from the floor population's
    0.24 creatures per walkable cell. THEY ASSUME EVERY CREATURE IS KILLED, which nobody has measured (issue #925);
    a half-cleared floor halves the gems;
  - the enemy rarities a kill is, by `game/Data/EnemyRarities.csv`'s spawn weights, and the gear each drops
    (`loot.ENEMY_GEAR_DROPS`), which is what the gem rate is a share of;
  - a gem's rarity, by the gear's own spread (`loot.rarity_distribution`) with the enemy's magic find added;
  - what one gem is worth against the tier's own rarity: 2 to the power of the rungs between them, since two gems
    of a rarity combine into one of the next (ruled 2026-11-21). A gem one rung above counts as two, a gem one rung
    below as a half;
  - the gems the reference character fills: `player_power`'s gems filled per tier, 6 at tier 1 to 45 at tier 8.

WHAT IS A JUDGEMENT:

  - the TARGET: the rate is the least at which a full clear of a tier's dungeons drops enough to fill the reference
    character at that tier, because the design calls that character the tier's ceiling;
  - ANY GEM COUNTS: the Power Score reads only a gem's rarity, so the reference character is filled by any gem of the
    rarity. A build after six or seven particular gems out of 28 needs about four times the kills;
  - gems follow the gear column's shape across enemy rarities, as magic find does.
"""

from __future__ import annotations

import contextlib
import io

from cataclysm_sim import affixes as af
from cataclysm_sim import loot
from cataclysm_sim import player_power

with contextlib.redirect_stdout(io.StringIO()):
    import analyse_experience_curve as curve

RARITIES = list(af.RARITIES)
TIERS = range(1, 9)


def kills_in_a_tier() -> float:
    """Every creature in one campaign's dungeons: 26 dungeons x their floors x the creatures on a floor."""
    return curve.CAMPAIGN_DUNGEONS * curve.average_dungeon_floors() * curve.creatures_per_floor()


def kill_shares() -> dict[str, float]:
    """What share of kills each enemy rarity is, by spawn weight, over the rarities that drop anything."""
    weights = {rarity: weight for rarity, weight in curve.spawn_weights().items() if rarity in loot.ENEMY_GEAR_DROPS}
    total = sum(weights.values())
    return {rarity: weight / total for rarity, weight in weights.items()}


def gear_drops_per_kill() -> float:
    return sum(share * loot.expected_gear_drops(rarity) for rarity, share in kill_shares().items())


def worth_against_the_tier(tier: int) -> float:
    """What one gem drop is worth in gems of the tier's own rarity, averaged over the kills that drop it."""
    worth = 0.0
    weight = 0.0
    for rarity, share in kill_shares().items():
        dropped = share * loot.expected_gear_drops(rarity)
        spread = loot.rarity_distribution(tier, loot.magic_find_from(rarity))
        own = tier - 1
        worth += dropped * sum(chance * 2.0 ** (RARITIES.index(name) - own) for name, chance in spread.items())
        weight += dropped
    return worth / weight


def gems_filled(tier: int) -> int:
    return len(player_power.reference_character(tier).gems)


def needed_share(tier: int) -> float:
    """The gems for each gear drop that fill the reference character at this tier over a full clear of its dungeons."""
    drops_needed = gems_filled(tier) / worth_against_the_tier(tier)
    return drops_needed / (gear_drops_per_kill() * kills_in_a_tier())


def main() -> None:
    kills = kills_in_a_tier()
    print("WHAT ONE TIER IS\n")
    print(f"  Kills in a tier, every creature killed: {kills:,.0f}")
    print(f"  Gear drops a kill, by spawn weight:      {gear_drops_per_kill():.4f}")
    print(f"  Gear drops in a tier:                    {gear_drops_per_kill() * kills:,.0f}\n")
    print("WHAT EACH TIER NEEDS\n")
    print("  Tier | Its rarity  | Gems filled | One drop is worth | Drops needed | Gems a gear drop")
    print("  " + "-" * 84)
    for tier in TIERS:
        worth = worth_against_the_tier(tier)
        needed = gems_filled(tier) / worth
        print(f"  {tier:>4} | {RARITIES[tier - 1]:<11} | {gems_filled(tier):>11} | {worth:>17.5f} | {needed:>12,.0f} |"
              f" {needed_share(tier):>16.5f}")
    binding = max(TIERS, key=needed_share)
    print(f"\n  The most any tier needs: {needed_share(binding):.5f} gems a gear drop, at tier {binding}.")
    print(f"  Shipped: GEM_DROPS_PER_GEAR_DROP = {loot.GEM_DROPS_PER_GEAR_DROP}, that figure rounded up.\n")
    print("WHAT THE SHIPPED RATE GIVES\n")
    print("  Tier | Gems in a tier | In a dungeon | Filled at the tier's rarity, of those needed")
    print("  " + "-" * 76)
    floors = curve.average_dungeon_floors()
    for tier in TIERS:
        dropped = loot.GEM_DROPS_PER_GEAR_DROP * gear_drops_per_kill() * kills
        reach = dropped * worth_against_the_tier(tier)
        print(f"  {tier:>4} | {dropped:>14,.0f} | {dropped / curve.CAMPAIGN_DUNGEONS:>12.1f} |"
              f" {reach:>8.0f} of {gems_filled(tier):>2}")
    print(f"\n  A dungeon is {floors:.1f} floors, so the shipped rate is about "
          f"{loot.GEM_DROPS_PER_GEAR_DROP * gear_drops_per_kill() * curve.creatures_per_floor():.2f} gems a floor.")
    print("\n  Gem Drops by enemy rarity (the sheet's column):")
    for rarity, gems in loot.ENEMY_GEM_DROPS.items():
        print(f"    {rarity:<15} {gems}")


if __name__ == "__main__":
    main()
