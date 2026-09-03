"""What tier an affix can reach, and what stops it reaching higher.

WHY THIS EXISTS. Issue #129. Affixes have seven tiers and T7 is worth seven times
T1, and nothing said which of them a drop could reach. Without a gate a tier 1
dungeon drops a T7 affix and the seven-tier curve does nothing for progression.

THE RULE, as the project owner settled it on 2026-08-05 in issue #241:

    a DROP rolls up to min(7, difficulty tier + 1), uniformly from T1
    CRAFTING has no tier gate; cost is the only limit
    a player may equip an affix above their own difficulty tier

The plus one is what gives a drop something the forge has not already reached,
and it is why a dungeon is worth running for quality rather than only quantity.
"""

from __future__ import annotations

import random
from collections import Counter

import pytest

from cataclysm_sim import affixes as af


# --------------------------------------------------------------------------
# The gate itself
# --------------------------------------------------------------------------

def test_a_drop_rolls_up_to_one_tier_above_the_difficulty_tier():
    assert [af.max_affix_tier_on_a_drop(t) for t in range(1, 9)] == \
        [2, 3, 4, 5, 6, 7, 7, 7]


def test_crafting_has_no_tier_gate_at_any_difficulty_tier():
    """Stated by the project owner, issue #241: a player may craft an affix as
    high as they can afford, wherever they are. Cost is the limit, not a rule."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        assert af.max_affix_tier_by_crafting(tier) == af.AFFIX_TIERS[-1]


def test_the_best_reachable_tier_is_the_crafting_one_because_it_is_uncapped():
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        assert af.max_affix_tier(tier) == af.AFFIX_TIERS[-1]


def test_a_drop_can_beat_what_the_player_could_otherwise_have_reached():
    """The whole point of the plus one. Below the top three difficulty tiers a
    drop can carry an affix one tier above the difficulty it dropped at."""
    for tier in range(1, 6):
        assert af.max_affix_tier_on_a_drop(tier) > tier


def test_the_top_affix_tier_is_reachable():
    """A gate that never reaches T7 makes the top tier unreachable and the
    Potency Crystal pointless."""
    reached = {af.max_affix_tier(t) for t in range(1, af.DIFFICULTY_TIERS + 1)}
    assert af.AFFIX_TIERS[-1] in reached


def test_the_lowest_difficulty_tier_can_still_drop_the_lowest_tier():
    """The cap is the ceiling of the draw, not the whole of it. T1 stays in the
    pool at every difficulty tier."""
    rng = random.Random(2)
    assert 1 in {af.roll_affix_tier(1, rng) for _ in range(200)}


def test_the_cap_never_falls_as_the_difficulty_tier_rises():
    caps = [af.max_affix_tier_on_a_drop(t)
            for t in range(1, af.DIFFICULTY_TIERS + 1)]
    assert caps == sorted(caps)


def test_every_cap_is_a_real_affix_tier():
    """A cap outside the tier list would index outside TIER_FRACTIONS and raise
    on the first drop rather than at import."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        assert af.max_affix_tier_on_a_drop(tier) in af.AFFIX_TIERS


def test_the_top_tier_serves_the_last_three_difficulty_tiers():
    """Eight difficulty tiers against seven affix tiers, and the plus one
    spends the difference at the top: tiers 6, 7 and 8 all reach T7 on a drop.
    That is where it costs least, because gear rarity, upgrade level and sockets
    are all still climbing through those tiers."""
    caps = [af.max_affix_tier_on_a_drop(t)
            for t in range(1, af.DIFFICULTY_TIERS + 1)]
    repeated = [tier for tier, count in Counter(caps).items() if count > 1]
    assert repeated == [af.AFFIX_TIERS[-1]]
    assert Counter(caps)[af.AFFIX_TIERS[-1]] == 3


def test_a_difficulty_tier_outside_the_range_is_rejected():
    with pytest.raises(ValueError):
        af.max_affix_tier_on_a_drop(0)
    with pytest.raises(ValueError):
        af.max_affix_tier_on_a_drop(af.DIFFICULTY_TIERS + 1)


def test_the_drop_gate_is_the_weapon_damage_type_gate_shifted_by_one():
    """Both are `min(a fixed ceiling, something that rises with the difficulty
    tier)`. The affix gate adds one to the tier first; the weapon gate does not.
    Keeping them recognisably the same shape is deliberate."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        assert af.max_affix_tier_on_a_drop(tier) == min(
            af.AFFIX_TIERS[-1], tier + af.DROP_TIERS_ABOVE_DIFFICULTY)
        assert af.max_damage_types(1, tier) == min(
            af.DAMAGE_TYPES_ON_ONE_HANDED, tier)


# --------------------------------------------------------------------------
# What a drop actually rolls
# --------------------------------------------------------------------------

def test_a_drop_never_exceeds_the_cap():
    rng = random.Random(20260805)
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        cap = af.max_affix_tier_on_a_drop(tier)
        for _ in range(500):
            assert 1 <= af.roll_affix_tier(tier, rng) <= cap


def test_every_tier_at_or_below_the_cap_can_still_roll():
    """A hard cap that also removed the low tiers would make a drop at a tier
    predictable, and the materials that reroll and perfect a value would have
    nothing to do."""
    rng = random.Random(11)
    seen = {af.roll_affix_tier(8, rng) for _ in range(2000)}
    assert seen == set(range(1, af.max_affix_tier_on_a_drop(8) + 1))


def test_a_tier_one_drop_can_only_be_tier_one_or_two():
    rng = random.Random(3)
    assert {af.roll_affix_tier(1, rng) for _ in range(200)} == {1, 2}


def test_each_tier_is_half_as_likely_as_the_one_below():
    """The shape the project owner set on 2026-08-18, replacing a uniform draw.

    Stated as the ratio rather than as the seven shares, because the ratio is
    the decision and the shares follow from it.
    """
    weights = af.AFFIX_TIER_DROP_WEIGHT
    for lower in af.AFFIX_TIERS[:-1]:
        assert weights[lower] / weights[lower + 1] == pytest.approx(2.0)


def test_a_top_tier_affix_is_one_in_a_hundred_and_twenty_seven():
    """At difficulty tier 8, where all seven tiers are reachable."""
    share = af.affix_tier_distribution(8)[7]
    assert 1 / share == pytest.approx(127, rel=0.001)


def test_half_of_every_affix_that_drops_is_the_lowest_tier():
    """Which is the point of the shape: a drop is raw material for crafting,
    and crafting is what raises an affix to T7."""
    assert af.affix_tier_distribution(8)[1] == pytest.approx(0.5039, abs=0.001)


def test_rolling_agrees_with_the_exact_distribution():
    """Two separate pieces of code must describe the same thing: the arithmetic
    in `affix_tier_distribution` and the draw `roll_affix_tier` actually makes.

    70,000 draws puts the standard error of any share below 0.2%, so half a
    percentage point cannot fail by chance.
    """
    rng = random.Random(99)
    draws = 70000
    counts = Counter(af.roll_affix_tier(8, rng) for _ in range(draws))
    expected = af.affix_tier_distribution(8)

    for affix_tier in af.AFFIX_TIERS:
        seen = counts[affix_tier] / draws
        assert seen == pytest.approx(expected[affix_tier], abs=0.005), (
            f"T{affix_tier} came up {seen:.3%} against a predicted "
            f"{expected[affix_tier]:.3%}")


def test_nothing_above_the_cap_can_roll():
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        spread = af.affix_tier_distribution(tier)
        cap = af.max_affix_tier_on_a_drop(tier)
        for affix_tier in range(cap + 1, af.AFFIX_TIERS[-1] + 1):
            assert spread[affix_tier] == 0.0


def test_the_average_drop_is_low_and_crafting_closes_the_rest():
    """What the weights are worth in practice. A raw tier 8 drop averages just
    under T2 of a possible T7, so almost all of an affix's value is crafted.

    THIS REPLACED A TEST EXPECTING THE MIDDLE TIER, which was true while the
    draw was uniform.
    """
    rng = random.Random(5)
    rolls = [af.roll_affix_tier(8, rng) for _ in range(20000)]
    assert 1.8 < sum(rolls) / len(rolls) < 2.1


def test_a_deeper_drop_is_better_on_average():
    """The whole point of the gate. Stated as a strict increase up to the point
    the affix tiers run out, because ties everywhere would pass a test named
    'better' while the gate did nothing."""
    rng = random.Random(41)
    means = []
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        rolls = [af.roll_affix_tier(tier, rng) for _ in range(5000)]
        means.append(sum(rolls) / len(rolls))
    for lower, higher in zip(means[:5], means[1:6], strict=True):
        assert higher > lower, means


def test_a_roll_at_a_difficulty_tier_outside_the_range_is_rejected():
    with pytest.raises(ValueError):
        af.roll_affix_tier(9, random.Random(1))


# --------------------------------------------------------------------------
# The gate and the value curve together
# --------------------------------------------------------------------------

def test_a_capped_affix_is_worth_its_share_of_the_top_value():
    """The gate and the tier curve have to agree.

    THE SHARE CHANGED ON 2026-09-03. It was four sevenths, because the ladder
    was `tier / 7`. The ladder is geometric now and spans 3.0 rather than 7.0,
    so a tier 4 affix is worth `3 ** ((4 - 7) / 6)` of a tier 7 one, which is
    about 0.577. Issue #1179 says why both ladders were eased.

    Read from TIER_FRACTIONS rather than written out, so this states that the
    gate and the curve agree rather than restating the curve's own formula and
    being able to drift from it.
    """
    cap = af.max_affix_tier_on_a_drop(3)
    assert cap == 4
    assert af.FLAT_HEALTH.value_at(cap) == pytest.approx(
        af.FLAT_HEALTH.value_at(7) * af.TIER_FRACTIONS[cap])
    assert af.TIER_FRACTIONS[cap] == pytest.approx(0.5774, abs=0.0005)


def test_the_cap_at_every_difficulty_tier_produces_a_computable_value():
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        value = af.FLAT_HEALTH.value_at(af.max_affix_tier_on_a_drop(tier))
        assert value > 0
