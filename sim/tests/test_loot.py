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
# Rolling a whole item
# --------------------------------------------------------------------------

SLOTS = sorted(af.GEAR_SLOTS)


@pytest.mark.parametrize("slot", SLOTS)
def test_every_gear_slot_can_be_rolled_at_every_tier(slot):
    """No slot has a pool too thin to fill the affixes its rarity asks for.

    THE ONE THAT WOULD BITE IN PLAY. A rarity is rolled first and the affixes are
    drawn to its count, so a slot whose prefix pool holds only one distinct group
    would raise the first time a Masterful piece dropped for it -- and only for
    that slot, at that rarity, which is exactly the kind of thing that reaches a
    player rather than a test.
    """
    rng = random.Random(4242)
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for _ in range(40):
            item = loot.roll_item(slot, tier, magic_find=300.0, rng=rng)
            assert item.base.slot == slot


def test_an_unknown_slot_is_refused():
    with pytest.raises(ValueError, match="unknown gear slot"):
        loot.roll_item("Cape", 4, 0.0, random.Random(1))


def test_the_contents_match_the_rarity_that_was_rolled():
    """The rarity read back off the item is the one its contents describe.

    This is what makes rolling the rarity first legitimate. The item stores no
    rarity; if the affix and enchantment counts did not match the composition the
    roll asked for, `rarity` would either name a different rarity or raise.
    """
    rng = random.Random(11)
    for _ in range(500):
        item = loot.roll_item("Chest", tier=8, magic_find=200.0, rng=rng)
        enchantments, affixes = af.RARITY_COMPOSITION[item.rarity]
        assert item.enchantments == enchantments
        assert len(item.affixes) == affixes


def test_an_item_never_holds_two_affixes_from_one_group():
    """Two affixes that raise the same stat the same way cannot share a piece.

    Includes the resistance families, which is the case the draw has to be told
    about: a family says how many damage types it covers and the item says which,
    so two families that both landed on Fire occupy one group.
    """
    rng = random.Random(909)
    for slot in ("Chest", "Ring", "Weapon"):
        for _ in range(400):
            item = loot.roll_item(slot, tier=8, magic_find=400.0, rng=rng)
            seen: set[str] = set()
            for rolled in item.affixes:
                groups = af.groups_of(rolled.affix, rolled.covers)
                assert not (groups & seen), (
                    f"{rolled.affix.name} repeats a group already on the piece")
                seen |= groups


def test_a_resistance_family_covers_exactly_its_breadth():
    """And every other kind of affix covers nothing."""
    rng = random.Random(31)
    families = 0
    for _ in range(600):
        item = loot.roll_item("Chest", tier=8, magic_find=400.0, rng=rng)
        for rolled in item.affixes:
            if isinstance(rolled.affix, af.AffixFamily):
                families += 1
                assert len(set(rolled.covers)) == rolled.affix.breadth
            else:
                assert rolled.covers == ()
    assert families > 0, (
        "no resistance family was ever rolled, so the assertion above never ran")


def test_a_resistance_affix_does_not_always_land_on_the_same_damage_types():
    """Which types a resistance roll covers is decided per drop, not once.

    A GAP FOUND BY BREAKING THE GUARD. The test above checks only HOW MANY types
    a family covers, and it passes just as well if the code takes the first
    `breadth` damage types every time -- which would make every single-resistance
    affix in the game a fire resistance affix. This is what notices that.
    """
    rng = random.Random(808)
    landed: set[str] = set()
    for _ in range(400):
        item = loot.roll_item("Chest", tier=8, magic_find=400.0, rng=rng)
        for rolled in item.affixes:
            landed.update(rolled.covers)

    assert len(landed) > 1, (
        f"every resistance affix landed on {landed}, so the damage types are not "
        "being chosen per drop")


def test_the_upgrade_level_is_the_floor_its_rarity_forces():
    rng = random.Random(5)
    for _ in range(600):
        item = loot.roll_item("Boots", tier=8, magic_find=250.0, rng=rng)
        assert item.gear_level == loot.gear_level_gate(item.rarity)


def test_nothing_drops_below_the_upgrade_level_its_rarity_requires():
    """The gate, stated the other way round: a Legendary is never a +3 piece."""
    rng = random.Random(6)
    for _ in range(600):
        item = loot.roll_item("Head", tier=8, magic_find=250.0, rng=rng)
        assert item.gear_level >= loot.gear_level_gate(item.rarity)


def test_no_affix_rolls_above_the_tier_gate():
    """A drop's affix tiers are capped at the difficulty tier plus one."""
    rng = random.Random(77)
    for tier in (1, 3, 8):
        cap = af.max_affix_tier_on_a_drop(tier)
        for _ in range(200):
            item = loot.roll_item("Gloves", tier, magic_find=400.0, rng=rng)
            for rolled in item.affixes:
                assert 1 <= rolled.tier <= cap


def test_a_roll_lands_inside_its_band():
    rng = random.Random(8)
    for _ in range(300):
        item = loot.roll_item("Belt", tier=5, magic_find=0.0, rng=rng)
        for rolled in item.affixes:
            assert 0.0 <= rolled.roll <= 1.0


def test_the_same_seed_rolls_the_same_item():
    """Deterministic from its generator, so a bad drop can be reproduced."""
    first = loot.roll_item("Chest", 6, 100.0, random.Random(4242))
    again = loot.roll_item("Chest", 6, 100.0, random.Random(4242))
    assert first == again


def test_a_three_affix_item_goes_both_ways_on_the_split():
    """An odd affix count is not always two prefixes and one suffix.

    A CHECK THAT WOULD OTHERWISE PASS SILENTLY. `affixes.prefix_suffix_split`
    returns the even shape and says a drop may legitimately go the other way; if
    `split_for_a_drop` simply returned that shape, every three-affix item in the
    game would carry two prefixes, and nothing else here would notice.
    """
    rng = random.Random(2024)
    shapes = {loot.split_for_a_drop(3, rng) for _ in range(200)}
    assert shapes == {(2, 1), (1, 2)}


def test_a_split_never_exceeds_either_cap_or_loses_a_slot():
    rng = random.Random(3)
    for slots in range(af.AFFIX_SLOTS_PER_PIECE + 1):
        for _ in range(60):
            prefixes, suffixes = loot.split_for_a_drop(slots, rng)
            assert prefixes + suffixes == slots
            assert prefixes <= af.PREFIXES_PER_PIECE
            assert suffixes <= af.SUFFIXES_PER_PIECE


def test_a_cataclysmic_item_carries_no_regular_affixes():
    """Four enchantments and nothing else, which is what the design says it is."""
    rng = random.Random(1234)
    seen = False
    for _ in range(4000):
        item = loot.roll_item("Relic", tier=8, magic_find=600.0, rng=rng)
        if item.rarity == "Cataclysmic":
            seen = True
            assert item.affixes == ()
            assert item.enchantments == 4
            assert item.gear_level == 10
    assert seen, "no Cataclysmic item dropped, so the assertions never ran"


def test_a_dropped_item_carries_residue_inside_its_rarity_band():
    rng = random.Random(21)
    for _ in range(500):
        item = loot.roll_item("Chest", tier=8, magic_find=250.0, rng=rng)
        lowest, highest = loot.residue_band(item.rarity)
        assert lowest <= item.residue <= highest


def test_two_drops_of_one_rarity_can_carry_different_residue():
    """The whole point of a band rather than a figure.

    A CHECK THAT WOULD OTHERWISE PASS SILENTLY. If the roll returned the band's
    low end every time, every assertion above would still hold.
    """
    rng = random.Random(64)
    seen = {loot.roll_residue("Masterful", rng) for _ in range(200)}
    assert len(seen) > 10, (
        f"200 rolls produced only {len(seen)} different residue values")


def test_residue_is_a_whole_number_of_points():
    """The two formulas that read it work in points: the craft day penalty is
    CR / 100 rounded down and the gold multiplier is (CR / 50) + 1."""
    rng = random.Random(65)
    for rarity in af.RARITIES:
        for _ in range(50):
            assert loot.roll_residue(rarity, rng).is_integer()


def test_neither_end_of_a_band_falls_as_rarity_rises():
    """A better item is never cheaper to improve at either end.

    Residue is a cost throughout, so it has to run the same way as the power it
    is attached to. The bands overlap, which is intended, but neither end may
    step backwards.
    """
    bands = [loot.residue_band(rarity) for rarity in af.RARITIES]
    assert [b[0] for b in bands] == sorted(b[0] for b in bands)
    assert [b[1] for b in bands] == sorted(b[1] for b in bands)


def test_the_bands_of_neighbouring_rarities_overlap():
    """Which is what makes residue a trade rather than a strict ladder: a lucky
    Superb piece arrives cheaper to craft than an unlucky Masterful one."""
    lower = loot.residue_band("Superb")
    higher = loot.residue_band("Masterful")
    assert higher[0] < lower[1], (
        f"Masterful starts at {higher[0]} and Superb ends at {lower[1]}, so the "
        "two never overlap")


def test_the_top_rarity_carries_the_band_the_project_owner_stated():
    """300 to 500, said on 2026-08-18. Two lighter proposals were rejected.

    Written out here rather than read from the table, because every other test
    compares two copies to each other and would pass with both of them wrong.
    """
    assert loot.residue_band("Cataclysmic") == (300.0, 500.0)


def test_a_top_rarity_drop_costs_days_to_craft_from_the_moment_it_lands():
    """What the owner's band means, put through the design's own two formulas.

    The gold multiplier is (CR / 50) + 1 and the craft day penalty is CR / 100
    rounded down. So a Cataclysmic drop costs seven to eleven times the gold and
    three to five real in-game days per craft.
    """
    lowest, highest = loot.residue_band("Cataclysmic")
    assert (lowest / 50.0) + 1.0 == pytest.approx(7.0)
    assert (highest / 50.0) + 1.0 == pytest.approx(11.0)
    assert int(lowest // 100) == 3
    assert int(highest // 100) == 5


def test_one_hundred_residue_is_not_a_ceiling_on_a_drop():
    """It is where crafting starts costing days, and most drops arrive past it.

    Stated as a test because treating it as a ceiling was wrong twice before the
    project owner corrected it, and a future reader meeting CRITICAL_RESIDUE will
    have the same instinct.
    """
    assert loot.residue_band("Masterful")[0] > loot.CRITICAL_RESIDUE


def test_an_unknown_rarity_has_no_residue_band():
    with pytest.raises(ValueError, match="is not a rarity"):
        loot.residue_band("Mythic")


def test_that_the_residue_band_table_check_actually_fires():
    original = loot.RARITY_RESIDUE_BAND
    try:
        loot.RARITY_RESIDUE_BAND = {"Everyday": (1.0, 2.0)}
        with pytest.raises(AssertionError, match="does not match the rarity"):
            loot._check_every_rarity_has_a_residue_band()
    finally:
        loot.RARITY_RESIDUE_BAND = original


def test_that_the_backwards_band_check_actually_fires():
    original = loot.RARITY_RESIDUE_BAND
    try:
        loot.RARITY_RESIDUE_BAND = dict(original, Superb=(400.0, 300.0))
        with pytest.raises(AssertionError, match="running upward"):
            loot._check_every_band_runs_upward_from_above_zero()
    finally:
        loot.RARITY_RESIDUE_BAND = original


def test_that_the_falling_band_check_actually_fires():
    original = loot.RARITY_RESIDUE_BAND
    try:
        loot.RARITY_RESIDUE_BAND = dict(original, Cataclysmic=(1.0, 2.0))
        with pytest.raises(AssertionError, match="fall somewhere along"):
            loot._check_no_residue_band_falls_as_rarity_rises()
    finally:
        loot.RARITY_RESIDUE_BAND = original
