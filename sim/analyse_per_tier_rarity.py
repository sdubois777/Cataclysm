"""What drops at each difficulty tier, and what the rejected alternative gave.

Issue #886. The project owner played on 2026-08-23 and reported that only
Everyday and Quality items ever drop. This prints the distribution the decision
produced, so the numbers in `docs/DECISIONS.md` can be reproduced rather than
taken on trust, and so a different endpoint can be tried before it is built.

    cd sim && python analyse_per_tier_rarity.py

WHAT THE THREE TABLES ARE.

    SHIPPED       what `loot.rarity_distribution` produces today, with the
                  ordinary segment's fall ratio running 2.5 at tier 1 down to
                  ORDINARY_FALL_AT_DEEPEST at tier 8, and the four enchanted
                  rarities pinned to the share the Gear Rarity sheet gives them.

    FLOATED       the alternative the project owner rejected on 2026-08-23: the
                  same flattening with the enchanted four left at their sheet
                  weights, so they rise as the ordinary segment shrinks. Kept
                  here because `drop_weight` says the pinning is what holds
                  Cataclysmic at one in 25,531, and this is what shows it.

    ENDPOINTS     difficulty tier 8 across a range of endpoints, which is the
                  table the endpoint was chosen from.

CHANGING THE ENDPOINT IS TWO LINES AND NOT A CHANGE HERE: move
`ORDINARY_FALL_AT_DEEPEST` in `sim/cataclysm_sim/loot.py` and
`OrdinaryFallAtDeepest` in `game/Source/Cataclysm/Items/CataclysmDropRoll.h`.
"""

from __future__ import annotations

from cataclysm_sim import affixes as af
from cataclysm_sim import loot

RARITIES = list(af.RARITIES)


def floated_weights(tier: int) -> dict[str, float]:
    """The rejected alternative: flatten the ordinary four, pin nothing.

    Built from the model's own ordinary weights so the two tables differ in one
    thing only. The enchanted four keep the sheet's figures untouched instead of
    following the ordinary segment down.
    """
    out = {}
    for index, rarity in enumerate(RARITIES, start=1):
        if index <= loot.ORDINARY_RARITIES:
            out[rarity] = loot.drop_weight(rarity, tier)
        else:
            out[rarity] = loot.RARITY_DROP_WEIGHT[rarity]
    return out


def distribution_with(tier: int, weights: dict[str, float]) -> dict[str, float]:
    """What the real cascade produces at this tier with these weights.

    The weights are swapped in rather than the arithmetic repeated here, so what
    is printed is what the model does and not a second implementation of it that
    could disagree.
    """
    kept = dict(loot.RARITY_DROP_WEIGHT)
    original = loot.ORDINARY_FALL_AT_DEEPEST
    try:
        loot.RARITY_DROP_WEIGHT.clear()
        loot.RARITY_DROP_WEIGHT.update(weights)
        # The weights passed in are already the ones for this tier, so the
        # per-tier adjustment must not be applied a second time on top.
        loot.ORDINARY_FALL_AT_DEEPEST = loot.ORDINARY_FALL_AT_TIER_ONE
        return loot.rarity_distribution(tier)
    finally:
        loot.ORDINARY_FALL_AT_DEEPEST = original
        loot.RARITY_DROP_WEIGHT.clear()
        loot.RARITY_DROP_WEIGHT.update(kept)


def one_in(share: float) -> str:
    return "never" if share <= 0.0 else f"1 in {round(1.0 / share):,}"


def header() -> str:
    return (f"{'tier':>4} {'ratio':>6} "
            + " ".join(f"{r[:9]:>9}" for r in RARITIES)
            + f" {'bottom 2':>9}")


def row(tier: int, share: dict[str, float]) -> str:
    cells = " ".join(f"{share[r] * 100:8.3f}%" for r in RARITIES)
    bottom = (share[RARITIES[0]] + share[RARITIES[1]]) * 100
    return (f"{tier:>4} {loot.ordinary_fall_at(tier):>6.2f} {cells} "
            f"{bottom:8.1f}%")


def main() -> None:
    tiers = range(1, af.DIFFICULTY_TIERS + 1)

    print("SHIPPED: the ordinary four flatten, the enchanted four are pinned")
    print("=" * 78)
    print(header())
    shipped = {tier: loot.rarity_distribution(tier) for tier in tiers}
    for tier in tiers:
        print(row(tier, shipped[tier]))
    print()
    for tier in tiers:
        print(f"  tier {tier}: Legendary {one_in(shipped[tier]['Legendary']):>12}"
              f"   Cataclysmic {one_in(shipped[tier]['Cataclysmic']):>12}")

    print()
    print("FLOATED: the same flattening with nothing pinned. REJECTED.")
    print("=" * 78)
    print(header())
    floated = {tier: distribution_with(tier, floated_weights(tier))
               for tier in tiers}
    for tier in tiers:
        print(row(tier, floated[tier]))
    print()
    for tier in tiers:
        print(f"  tier {tier}: Legendary {one_in(floated[tier]['Legendary']):>12}"
              f"   Cataclysmic {one_in(floated[tier]['Cataclysmic']):>12}")

    print()
    print("ENDPOINTS: difficulty tier 8 only, as the endpoint was chosen from")
    print("=" * 78)
    print(f"{'ratio':>6} " + " ".join(f"{r[:9]:>9}" for r in RARITIES)
          + f" {'bottom 2':>9}")

    original = loot.ORDINARY_FALL_AT_DEEPEST
    try:
        for endpoint in (2.5, 2.0, 1.5, 1.25, 1.0, 0.8, 0.6):
            loot.ORDINARY_FALL_AT_DEEPEST = endpoint
            share = loot.rarity_distribution(af.DIFFICULTY_TIERS)
            cells = " ".join(f"{share[r] * 100:8.3f}%" for r in RARITIES)
            bottom = (share[RARITIES[0]] + share[RARITIES[1]]) * 100
            marker = "  <- shipped" if endpoint == original else ""
            print(f"{endpoint:>6.2f} {cells} {bottom:8.1f}%{marker}")
    finally:
        loot.ORDINARY_FALL_AT_DEEPEST = original


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, matching
# `analyse_margin_tolerance.py` and the others. `sim/tests/test_analysis_scripts.py`
# runs these through `runpy.run_path`, which does NOT set `__name__` to
# `__main__`, so a guarded script runs and prints nothing and the test reports it
# as a script that produced no output.
main()
