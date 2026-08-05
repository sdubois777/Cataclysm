"""What tier an affix can reach, and what stops it reaching higher.

WHY THIS EXISTS. Issue #129. Affixes have seven tiers and T7 is worth seven times
T1, and nothing said which of them a drop could reach. Without a gate a tier 1
dungeon drops a T7 affix and the seven-tier curve does nothing for progression.

THE RULE. `affix tier <= min(7, difficulty tier)`, applied to the drop and to
crafting alike. Below the cap a drop rolls uniformly from T1 up to it.

The three things the issue left open are each pinned below:

    what the gate is            the difficulty tier, the design's own gate
    hard cap or weighted range  a hard cap on the maximum, everything below
                                it still in the pool
    any tiers drop-only         none; the cap already does that job
"""

from __future__ import annotations

import random
from collections import Counter

import pytest

from cataclysm_sim import affixes as af


# --------------------------------------------------------------------------
# The gate itself
# --------------------------------------------------------------------------

def test_the_cap_is_the_difficulty_tier_until_the_affix_tiers_run_out():
    assert [af.max_affix_tier(t) for t in range(1, 9)] == \
        [1, 2, 3, 4, 5, 6, 7, 7]


def test_the_top_affix_tier_is_reachable():
    """A gate that never reaches T7 makes the top tier unreachable and the
    Potency Crystal pointless."""
    reached = {af.max_affix_tier(t) for t in range(1, af.DIFFICULTY_TIERS + 1)}
    assert af.AFFIX_TIERS[-1] in reached


def test_the_lowest_difficulty_tier_can_still_drop_something():
    assert af.max_affix_tier(1) == af.AFFIX_TIERS[0]


def test_the_cap_never_falls_as_the_difficulty_tier_rises():
    caps = [af.max_affix_tier(t) for t in range(1, af.DIFFICULTY_TIERS + 1)]
    assert caps == sorted(caps)


def test_every_cap_is_a_real_affix_tier():
    """A cap outside the tier list would index outside TIER_FRACTIONS and raise
    on the first drop rather than at import."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        assert af.max_affix_tier(tier) in af.AFFIX_TIERS


def test_the_doubled_tier_is_at_the_top_and_not_the_bottom():
    """Eight difficulty tiers against seven affix tiers, so exactly one affix
    tier serves two difficulty tiers. Which one is a design decision: the top,
    because late progression still has gear rarity, upgrade level and sockets
    climbing while early progression has less."""
    caps = [af.max_affix_tier(t) for t in range(1, af.DIFFICULTY_TIERS + 1)]
    repeated = [tier for tier, count in Counter(caps).items() if count > 1]
    assert repeated == [af.AFFIX_TIERS[-1]]


def test_a_difficulty_tier_outside_the_range_is_rejected():
    with pytest.raises(ValueError):
        af.max_affix_tier(0)
    with pytest.raises(ValueError):
        af.max_affix_tier(af.DIFFICULTY_TIERS + 1)


def test_the_gate_is_the_same_shape_as_the_weapon_damage_type_gate():
    """Both are `min(a fixed ceiling, the difficulty tier)`. Using one shape for
    both is the point: it is a fourth use of an existing mechanism rather than a
    new one."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        assert af.max_affix_tier(tier) == min(af.AFFIX_TIERS[-1], tier)
        assert af.max_damage_types(1, tier) == min(
            af.DAMAGE_TYPES_ON_ONE_HANDED, tier)


# --------------------------------------------------------------------------
# What a drop actually rolls
# --------------------------------------------------------------------------

def test_a_drop_never_exceeds_the_cap():
    rng = random.Random(20260805)
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        cap = af.max_affix_tier(tier)
        for _ in range(500):
            assert 1 <= af.roll_affix_tier(tier, rng) <= cap


def test_every_tier_at_or_below_the_cap_can_still_roll():
    """A hard cap that also removed the low tiers would make a drop at a tier
    predictable, and the materials that reroll and perfect a value would have
    nothing to do."""
    rng = random.Random(11)
    seen = {af.roll_affix_tier(8, rng) for _ in range(2000)}
    assert seen == set(range(1, af.max_affix_tier(8) + 1))


def test_a_tier_one_drop_can_only_be_tier_one():
    rng = random.Random(3)
    assert {af.roll_affix_tier(1, rng) for _ in range(200)} == {1}


def test_the_draw_is_uniform_across_the_allowed_tiers():
    """Uniform is the simplest rule that keeps every tier below the cap in the
    pool, and it invents no constant. If this is ever tuned to lean high, this
    test is the one to change, and the decision belongs in docs/DECISIONS.md."""
    rng = random.Random(99)
    counts = Counter(af.roll_affix_tier(8, rng) for _ in range(70000))
    expected = 70000 / af.max_affix_tier(8)
    for tier, count in counts.items():
        assert abs(count - expected) < expected * 0.1, (
            f"T{tier} came up {count} times against an expected {expected:.0f}")


def test_the_average_drop_at_the_deepest_tier_is_the_middle_tier():
    """What the gate is worth in practice: a raw tier 8 drop is worth about four
    sevenths of a perfect one, and crafting closes the rest."""
    rng = random.Random(5)
    rolls = [af.roll_affix_tier(8, rng) for _ in range(20000)]
    expected = (af.max_affix_tier(8) + 1) / 2
    assert abs(sum(rolls) / len(rolls) - expected) < 0.1


def test_a_deeper_drop_is_better_on_average():
    """The whole point of the gate. Stated as a strict increase up to the point
    the affix tiers run out, because ties everywhere would pass a test named
    'better' while the gate did nothing."""
    rng = random.Random(41)
    means = []
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        rolls = [af.roll_affix_tier(tier, rng) for _ in range(5000)]
        means.append(sum(rolls) / len(rolls))
    for lower, higher in zip(means[:6], means[1:7], strict=True):
        assert higher > lower, means


def test_a_roll_at_a_difficulty_tier_outside_the_range_is_rejected():
    with pytest.raises(ValueError):
        af.roll_affix_tier(9, random.Random(1))


# --------------------------------------------------------------------------
# The gate and the value curve together
# --------------------------------------------------------------------------

def test_a_capped_affix_is_worth_its_share_of_the_top_value():
    """The gate and the linear tier curve have to agree: a tier 3 drop of the
    flat health affix is worth three sevenths of a T7 one, not some other
    number."""
    cap = af.max_affix_tier(3)
    assert af.FLAT_HEALTH.value_at(cap) == pytest.approx(
        af.FLAT_HEALTH.value_at(7) * 3 / 7)


def test_the_cap_at_every_difficulty_tier_produces_a_computable_value():
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        value = af.FLAT_HEALTH.value_at(af.max_affix_tier(tier))
        assert value > 0
