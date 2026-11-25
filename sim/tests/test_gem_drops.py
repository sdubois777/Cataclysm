"""Gem drops: the shipped rate against the measurement it was chosen from. Issue #46, slice G5, and issue #2853.

`loot.GEM_DROPS_PER_GEAR_DROP` was chosen from `sim/analyse_gem_drops.py`: the least share of gear drops at which a
full clear of a tier's dungeons drops enough gems to fill the design's reference character at that tier, any gem of the
tier's rarity counting (a judgement, the coordinating session's of 2026-11-21 to measure here). Tier 8 sets it. Since
2026-11-24 a lower tier drops `loot.gem_drop_share(tier)` of it, three quarters of the tier above's (the owner's ruling,
"Milder: tier 1 gets 1/8 (Recommended)"; the three quarters a judgement). These hold the stored figures to that
measurement, so a change to any input -- the kills in a tier, the gear column, the rarity spread, the reference
character, the share -- that leaves a tier's gem rate short fails here rather than in play.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import random
import sys

import pytest

SIM = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM))

from cataclysm_sim import loot  # noqa: E402

with contextlib.redirect_stdout(io.StringIO()):
    import analyse_gem_drops as measured  # noqa: E402

#: The Gem Drops column before issue #2853, gems a kill at the baseline loot quantity. The owner ruled tier 8 keeps it.
GEMS_A_KILL_BEFORE_2853 = {"Common": 0.001088, "Elite": 0.0034, "Legendary": 0.0068, "Herald": 0.0136, "Boss": 0.034,
                           "Cataclysm Boss": 0.0816}


def shipped(tier: int) -> float:
    """The gems a gear drop carries at this tier, as shipped."""
    return loot.GEM_DROPS_PER_GEAR_DROP * loot.gem_drop_share(tier)


def test_the_shipped_rate_reaches_the_ceiling_at_every_tier() -> None:
    for tier in measured.TIERS:
        assert shipped(tier) >= measured.needed_share(tier), (
            f"tier {tier} needs {measured.needed_share(tier):.6f} gems a gear drop to fill the reference character "
            f"over a full clear; it ships {shipped(tier):.6f} ({loot.gem_drop_share(tier):.4f} of tier 8's)")


def test_the_shipped_rate_is_the_measured_one_rounded_up_and_no_more() -> None:
    most = max(measured.needed_share(tier) for tier in measured.TIERS)
    assert loot.GEM_DROPS_PER_GEAR_DROP <= most * 1.05, (
        f"the shipped {loot.GEM_DROPS_PER_GEAR_DROP} is over 5% above the measured {most:.5f}; it was chosen as the "
        "measurement rounded up, so a larger gap means an input moved")


def test_tier_eight_keeps_the_gem_rate_a_kill_had_before_the_gear_column_halved() -> None:
    """The owner's ruling of 2026-11-24: tier 8 keeps today's rate. The gear column halved, so the share of a gear drop
    doubled to 0.0136, and gems a kill at tier 8 are exactly what they were."""
    assert loot.ENEMY_GEM_DROPS == GEMS_A_KILL_BEFORE_2853
    assert loot.gem_drop_share(8) == 1.0
    for rarity, gems in GEMS_A_KILL_BEFORE_2853.items():
        assert loot.expected_gem_drops(rarity, 8) == pytest.approx(gems, abs=1e-12)
        assert loot.ENEMY_GEAR_DROPS[rarity] * loot.GEM_DROPS_PER_GEAR_DROP == pytest.approx(gems, abs=1e-12)


def test_tier_one_drops_about_an_eighth_and_each_tier_three_quarters_of_the_next() -> None:
    """"Milder: tier 1 gets 1/8 (Recommended)", the owner, 2026-11-24: about an eighth at tier 1, rising to the whole
    rate at tier 8, less steeply than halving per tier. Three quarters a step is the judgement."""
    assert loot.gem_drop_share(1) == 0.75 ** 7 == 2187 / 16384
    assert 1 / 8 <= loot.gem_drop_share(1) < 1 / 7, "tier 1's share is not about an eighth"
    for tier in range(1, 8):
        step = loot.gem_drop_share(tier + 1) / loot.gem_drop_share(tier)
        assert step == pytest.approx(4 / 3), (tier, step)
        assert step < 2.0, f"tier {tier} to {tier + 1} is as steep as halving or steeper"


@pytest.mark.parametrize("tier", [0, 9])
def test_a_tier_outside_one_to_eight_is_refused(tier: int) -> None:
    with pytest.raises(ValueError, match="outside 1 to 8"):
        loot.gem_drop_share(tier)


def test_a_kills_expected_gems_are_the_column_times_the_tiers_share_times_loot_quantity() -> None:
    for tier in measured.TIERS:
        for rarity, gems in loot.ENEMY_GEM_DROPS.items():
            assert loot.expected_gem_drops(rarity, tier, 300.0) == pytest.approx(
                gems * loot.gem_drop_share(tier) * 3.0, abs=1e-12)


def test_a_tier_one_kill_drops_its_tiers_share_and_no_more() -> None:
    """The count the roll actually draws, not only the expectation: 40,000 Boss kills at tier 1 and at tier 8, at ten
    times the loot quantity, 0.045 and 0.34 gems a kill expected, about 1,800 and 13,600 gems. The tier 1 count is
    0.133 of the tier 8 one, within 0.01 (about three standard deviations of that ratio)."""
    counts = {}
    for tier in (1, 8):
        rng = random.Random(2853 + tier)
        counts[tier] = sum(len(loot.roll_gem_drops_from_kill("Boss", tier, 0.0, 1000.0, rng)) for _ in range(40_000))
    assert counts[1] / counts[8] == pytest.approx(loot.gem_drop_share(1), abs=0.01), counts


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
    # What tells a tier apart is how many are Everyday: 45% at tier 1 for a Cataclysm Boss, 4% at tier 8. AT TEN TIMES
    # THE LOOT QUANTITY, since a tier 1 kill drops 0.75 ** 7 of the column (issue #2853): about 650 gems, one standard
    # deviation about 0.02, so 0.10 is more than four.
    rng = random.Random(46)
    rolled = [rarity for _ in range(6000) for rarity in loot.roll_gem_drops_from_kill("Cataclysm Boss", 1, 0.0, 1000.0,
                                                                                      rng)]
    assert len(rolled) > 300, (f"6,000 Cataclysm Boss kills at 0.0816 x 0.75 ** 7 x 10 each dropped only "
                               f"{len(rolled)} gems")
    expected = loot.rarity_distribution(1, loot.magic_find_from("Cataclysm Boss"))["Everyday"]
    share = rolled.count("Everyday") / len(rolled)
    assert abs(share - expected) < 0.10, (
        f"{share:.3f} of a tier 1 kill's gems are Everyday; the gear's spread there gives {expected:.3f}")
