"""Gem drops: the shipped rate against the measurement it was chosen from. Issue #46, slice G5.

`loot.GEM_DROPS_PER_GEAR_DROP` was chosen from `sim/analyse_gem_drops.py`: the least share of gear drops at which a
full clear of a tier's dungeons drops enough gems to fill the design's reference character at that tier, any gem of the
tier's rarity counting (a judgement, the coordinating session's of 2026-11-21 to measure here). These hold the stored
figure to that measurement, so a change to any input -- the kills in a tier, the gear column, the rarity spread, the
reference character -- that leaves the gem rate short fails here rather than in play.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import random
import sys

SIM = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM))

from cataclysm_sim import loot  # noqa: E402

with contextlib.redirect_stdout(io.StringIO()):
    import analyse_gem_drops as measured  # noqa: E402


def test_the_shipped_rate_reaches_the_ceiling_at_every_tier() -> None:
    for tier in measured.TIERS:
        assert loot.GEM_DROPS_PER_GEAR_DROP >= measured.needed_share(tier), (
            f"tier {tier} needs {measured.needed_share(tier):.5f} gems a gear drop to fill the reference character "
            f"over a full clear; the shipped rate is {loot.GEM_DROPS_PER_GEAR_DROP}")


def test_the_shipped_rate_is_the_measured_one_rounded_up_and_no_more() -> None:
    most = max(measured.needed_share(tier) for tier in measured.TIERS)
    assert loot.GEM_DROPS_PER_GEAR_DROP <= most * 1.05, (
        f"the shipped {loot.GEM_DROPS_PER_GEAR_DROP} is over 5% above the measured {most:.5f}; it was chosen as the "
        "measurement rounded up, so a larger gap means an input moved")


def test_tier_eight_is_the_tier_that_sets_it() -> None:
    assert max(measured.TIERS, key=measured.needed_share) == 8, (
        "a tier other than 8 now needs the most gems for each gear drop; the reason recorded beside "
        "GEM_DROPS_PER_GEAR_DROP no longer holds")


def test_a_better_enemy_drops_more_gems_and_every_kill_can_drop_one() -> None:
    rates = list(loot.ENEMY_GEM_DROPS.values())
    assert all(rate > 0.0 for rate in rates)
    assert all(below < above for below, above in zip(rates[:-1], rates[1:], strict=True))


def test_a_kills_gems_are_rolled_at_the_gears_rarity_spread_at_the_tier() -> None:
    # The gear's spread lists all eight rarities, at a share of nought or more, so membership in it proves nothing.
    # What tells a tier apart is how many are Everyday: 45% at tier 1 for a Cataclysm Boss, 4% at tier 8. With about
    # 490 gems one standard deviation is about 0.022, so 0.10 is more than four.
    rng = random.Random(46)
    rolled = [rarity for _ in range(6000) for rarity in loot.roll_gem_drops_from_kill("Cataclysm Boss", 1, 0.0, 100.0,
                                                                                      rng)]
    assert len(rolled) > 300, f"6,000 Cataclysm Boss kills at 0.0816 each dropped only {len(rolled)} gems"
    expected = loot.rarity_distribution(1, loot.magic_find_from("Cataclysm Boss"))["Everyday"]
    share = rolled.count("Everyday") / len(rolled)
    assert abs(share - expected) < 0.10, (
        f"{share:.3f} of a tier 1 kill's gems are Everyday; the gear's spread there gives {expected:.3f}")
