"""Tests for what a drop rolls as, in `sim/cataclysm_sim/loot.py`.

WHAT THESE GUARD, and each is something the module can get wrong silently:

1. That the exact distribution and the actual roll agree. `rarity_distribution`
   is arithmetic and `roll_rarity` walks the cascade; they are written separately
   on purpose, and a test that only checked one of them would prove nothing about
   what a player sees.

2. That the tier cap holds however much magic find is applied. A stat that could
   push a rarity past the cap would undo the design's own gate.

3. That magic find does what the genre says it does, rather than merely doing
   something.

4. That the import-time checks in the module can actually fail.
"""

from __future__ import annotations

import random

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import loot


# --------------------------------------------------------------------------
# The cap
# --------------------------------------------------------------------------

def test_the_cap_is_one_rarity_above_the_difficulty_tier():
    assert loot.best_rarity_on_a_drop(1) == "Quality"
    assert loot.best_rarity_on_a_drop(4) == "Legendary"
    assert loot.best_rarity_on_a_drop(6) == "Ascendant"


def test_the_cap_stops_at_cataclysmic_for_the_last_two_tiers():
    """Eight rarities and eight tiers, so the one-above spends the difference
    at the top -- the same way affix tiers 6, 7 and 8 all reach T7."""
    assert loot.best_rarity_on_a_drop(7) == "Cataclysmic"
    assert loot.best_rarity_on_a_drop(8) == "Cataclysmic"


def test_a_tier_outside_the_eight_is_refused():
    with pytest.raises(ValueError, match="outside 1 to 8"):
        loot.best_rarity_on_a_drop(0)
    with pytest.raises(ValueError, match="outside 1 to 8"):
        loot.best_rarity_on_a_drop(9)


def test_nothing_above_the_cap_can_drop_at_any_magic_find():
    """A magic find nothing could ever reach still cannot lift the ceiling.

    This is the assertion that would fail if magic find were ever applied to
    which rarities are reachable rather than to the roll among them.
    """
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        spread = loot.rarity_distribution(tier, magic_find=100_000.0)
        above = loot.rarity_index(loot.best_rarity_on_a_drop(tier)) + 1
        for index in range(above, len(af.RARITIES) + 1):
            rarity = loot.rarity_at_index(index)
            assert spread[rarity] == 0.0, (
                f"tier {tier} dropped {rarity}, above its cap of "
                f"{loot.best_rarity_on_a_drop(tier)}")


# --------------------------------------------------------------------------
# The distribution
# --------------------------------------------------------------------------

def test_a_distribution_covers_every_rarity_including_unreachable_ones():
    """A caller asking about a rarity out of reach gets zero, not a KeyError."""
    spread = loot.rarity_distribution(1)
    assert set(spread) == set(af.RARITIES)
    assert spread["Cataclysmic"] == 0.0


def test_the_flat_weights_shipped_today_give_a_flat_distribution():
    """Every weight is 1, so every reachable rarity is equally likely.

    Stated as a test rather than left implicit, because the flatness is the
    number most likely to be changed first and this is what will show it moving.
    """
    spread = loot.rarity_distribution(8)
    for rarity in af.RARITIES:
        assert spread[rarity] == pytest.approx(1 / 8, abs=1e-9)


def test_the_distribution_follows_the_weights_rather_than_the_ladder():
    """Change a weight and the share changes with it.

    A CHECK THAT COULD NOT FAIL OTHERWISE. Every weight shipped today is 1, so
    every test above passes just as well if the weights are ignored entirely and
    the cascade is hard-coded flat. This one makes one rarity three times as
    heavy and requires the outcome to move.
    """
    heavier = dict(loot.RARITY_DROP_WEIGHT)
    heavier["Superb"] = 3.0

    original = loot.RARITY_DROP_WEIGHT
    try:
        loot.RARITY_DROP_WEIGHT = heavier
        spread = loot.rarity_distribution(8)
    finally:
        loot.RARITY_DROP_WEIGHT = original

    # Seven rarities at weight 1 and one at 3 is ten shares.
    assert spread["Superb"] == pytest.approx(3 / 10, abs=1e-9)
    assert spread["Everyday"] == pytest.approx(1 / 10, abs=1e-9)
    assert sum(spread.values()) == pytest.approx(1.0, abs=1e-9)


def test_a_distribution_sums_to_one_at_every_tier_and_magic_find():
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for magic_find in (0.0, 25.0, 100.0, 700.0):
            total = sum(loot.rarity_distribution(tier, magic_find).values())
            assert total == pytest.approx(1.0, abs=1e-9)


# --------------------------------------------------------------------------
# Magic find
# --------------------------------------------------------------------------

def test_a_hundred_percent_magic_find_doubles_the_rarest_drop():
    """The genre's own statement, made concrete.

    Path of Exile: +100% increased item rarity gives "twice as many magic items,
    twice as many rares and twice as many uniques". At tier 8 the rarest is
    Cataclysmic at one drop in eight, so it becomes one in four.
    """
    plain = loot.rarity_distribution(8)
    doubled = loot.rarity_distribution(8, magic_find=100.0)

    assert doubled["Cataclysmic"] == pytest.approx(
        2 * plain["Cataclysmic"], abs=1e-9)


def test_magic_find_moves_weight_upward_and_takes_it_from_the_floor():
    plain = loot.rarity_distribution(8)
    lucky = loot.rarity_distribution(8, magic_find=200.0)

    assert lucky["Cataclysmic"] > plain["Cataclysmic"]
    assert lucky["Everyday"] < plain["Everyday"]


def test_negative_magic_find_is_refused():
    """It is an added percentage with a baseline of zero, so below zero is a
    caller passing something else -- a multiplier, or loot quantity."""
    with pytest.raises(ValueError, match="cannot be negative"):
        loot.rarity_step_chance(4, magic_find=-1.0)


# --------------------------------------------------------------------------
# The roll itself
# --------------------------------------------------------------------------

def test_rolling_agrees_with_the_exact_distribution():
    """The one that matters. Two separate pieces of code must describe the same
    thing: the arithmetic in `rarity_distribution` and the cascade actually
    walked by `roll_rarity`.

    A FIXED SEED AND A TOLERANCE FROM THE SAMPLE SIZE. 40,000 draws puts the
    standard error of any share below 0.25%, so 1.5 percentage points is six
    standard errors and this cannot fail by chance.
    """
    draws = 40_000
    rng = random.Random(20260818)

    counts = dict.fromkeys(af.RARITIES, 0)
    for _ in range(draws):
        counts[loot.roll_rarity(8, magic_find=150.0, rng=rng)] += 1

    expected = loot.rarity_distribution(8, magic_find=150.0)
    for rarity in af.RARITIES:
        seen = counts[rarity] / draws
        assert seen == pytest.approx(expected[rarity], abs=0.015), (
            f"{rarity} came up {seen:.1%} of the time against a predicted "
            f"{expected[rarity]:.1%}")


def test_rolling_never_returns_a_rarity_above_the_tier_cap():
    rng = random.Random(7)
    for tier in (1, 3, 5):
        cap = loot.rarity_index(loot.best_rarity_on_a_drop(tier))
        for _ in range(2_000):
            rolled = loot.roll_rarity(tier, magic_find=5_000.0, rng=rng)
            assert loot.rarity_index(rolled) <= cap


def test_the_same_seed_gives_the_same_drops():
    """Deterministic from its generator, so a failing drop can be reproduced."""
    first = [loot.roll_rarity(6, 100.0, random.Random(99)) for _ in range(5)]
    again = [loot.roll_rarity(6, 100.0, random.Random(99)) for _ in range(5)]
    assert first == again


# --------------------------------------------------------------------------
# The gear level gate
# --------------------------------------------------------------------------

def test_the_four_stated_gates_are_the_four_enchantable_rarities():
    assert loot.gear_level_gate("Masterful") == 0
    assert loot.gear_level_gate("Legendary") == 4
    assert loot.gear_level_gate("Mythical") == 6
    assert loot.gear_level_gate("Ascendant") == 8
    assert loot.gear_level_gate("Cataclysmic") == 10


def test_every_rarity_that_carries_an_enchantment_has_a_gate():
    """The gate exists because an enchantment does, so the two must line up.

    Derived from `affixes.RARITY_COMPOSITION` rather than listed again, so a
    change to which rarities carry enchantments cannot leave a gate behind.
    """
    for rarity in af.RARITIES:
        carries = af.enchantments_for(rarity) > 0
        gated = loot.gear_level_gate(rarity) > 0
        assert carries == gated, (
            f"{rarity} carries {af.enchantments_for(rarity)} enchantments and "
            f"has a gate of {loot.gear_level_gate(rarity)}")


def test_an_unknown_rarity_is_refused():
    with pytest.raises(ValueError, match="is not a rarity"):
        loot.gear_level_gate("Mythic")


# --------------------------------------------------------------------------
# That the module's own import-time checks can fail
# --------------------------------------------------------------------------

def test_that_the_weight_table_check_actually_fires():
    original = loot.RARITY_DROP_WEIGHT
    try:
        loot.RARITY_DROP_WEIGHT = {"Everyday": 1.0}
        with pytest.raises(AssertionError, match="does not match the rarity"):
            loot._check_every_rarity_has_a_drop_weight()
    finally:
        loot.RARITY_DROP_WEIGHT = original


def test_that_the_negative_weight_check_actually_fires():
    original = loot.RARITY_DROP_WEIGHT
    try:
        loot.RARITY_DROP_WEIGHT = dict(original, Superb=-1.0)
        with pytest.raises(AssertionError, match="cannot be negative"):
            loot._check_no_drop_weight_is_negative()
    finally:
        loot.RARITY_DROP_WEIGHT = original


def test_that_the_rising_gate_check_actually_fires():
    original = loot.RARITY_GEAR_LEVEL_GATE
    try:
        loot.RARITY_GEAR_LEVEL_GATE = dict(original, Cataclysmic=2)
        with pytest.raises(AssertionError, match="fall somewhere along"):
            loot._check_the_gates_only_ever_rise()
    finally:
        loot.RARITY_GEAR_LEVEL_GATE = original


def test_that_the_out_of_reach_gate_check_actually_fires():
    original = loot.RARITY_GEAR_LEVEL_GATE
    try:
        loot.RARITY_GEAR_LEVEL_GATE = dict(original, Cataclysmic=11)
        with pytest.raises(AssertionError, match="outside 0 to"):
            loot._check_no_gate_is_out_of_reach()
    finally:
        loot.RARITY_GEAR_LEVEL_GATE = original
