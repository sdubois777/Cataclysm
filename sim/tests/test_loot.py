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

import collections
import random

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import loot


# --------------------------------------------------------------------------
# How far the tier reaches without a penalty
#
# THERE IS NO CAP ANY MORE. These tests used to hold one: a rarity above the
# tier plus one carried a share of exactly zero. The project owner played that
# on 2026-08-23 and said it was too strict -- at difficulty tier 1 only Everyday
# and Quality could drop at all. Every rarity now drops at every tier, and the
# same figure decides where a penalty starts instead.
# --------------------------------------------------------------------------

def test_the_tier_reaches_one_rarity_above_itself_without_penalty():
    assert loot.highest_unpenalised_rarity(1) == "Quality"
    assert loot.highest_unpenalised_rarity(4) == "Legendary"
    assert loot.highest_unpenalised_rarity(6) == "Ascendant"


def test_the_reach_stops_at_cataclysmic_for_the_last_two_tiers():
    """Eight rarities and eight tiers, so the one-above spends the difference
    at the top -- the same way affix tiers 6, 7 and 8 all reach T7."""
    assert loot.highest_unpenalised_rarity(7) == "Cataclysmic"
    assert loot.highest_unpenalised_rarity(8) == "Cataclysmic"


def test_a_tier_outside_the_eight_is_refused():
    with pytest.raises(ValueError, match="outside 1 to 8"):
        loot.highest_unpenalised_rarity(0)
    with pytest.raises(ValueError, match="outside 1 to 8"):
        loot.highest_unpenalised_rarity(9)


def test_the_penalty_is_one_at_or_below_the_reach_and_doubles_above_it():
    """Nothing at or under the tier's own reach pays anything, and each rung
    above it costs the same again."""
    assert loot.penalty_above_the_tier("Everyday", 1) == 1.0
    assert loot.penalty_above_the_tier("Quality", 1) == 1.0

    assert loot.penalty_above_the_tier("Superb", 1) == pytest.approx(
        loot.RARITY_PENALTY_ABOVE_THE_TIER)
    assert loot.penalty_above_the_tier("Masterful", 1) == pytest.approx(
        loot.RARITY_PENALTY_ABOVE_THE_TIER ** 2)

    # AND THE TWO DEEPEST TIERS PAY NOTHING AT ALL, which is what keeps every
    # figure the 2026-08-18 decision set intact.
    for tier in (7, 8):
        for rarity in af.RARITIES:
            assert loot.penalty_above_the_tier(rarity, tier) == 1.0


def test_every_rarity_drops_at_every_tier_however_much_magic_find():
    """What the project owner asked for on 2026-08-23, checked directly.

    This is the exact opposite of what stood here before, which held that a
    rarity above the tier's cap carried a share of exactly zero. Nothing is
    forbidden now: the penalty divides and magic find multiplies, and neither
    can reach zero.
    """
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for magic_find in (0.0, 300.0, 100_000.0):
            spread = loot.rarity_distribution(tier, magic_find)
            for rarity in af.RARITIES:
                assert spread[rarity] > 0.0, (
                    f"{rarity} never drops at difficulty tier {tier} with "
                    f"{magic_find}% magic find")


# --------------------------------------------------------------------------
# The distribution
# --------------------------------------------------------------------------

def test_a_distribution_covers_every_rarity_and_none_of_them_is_zero():
    """It used to assert the opposite for anything above the tier's cap.

    A caller still gets every rarity as a key. What changed on 2026-08-23 is
    that none of them is zero any more, at any difficulty tier.
    """
    spread = loot.rarity_distribution(1)
    assert set(spread) == set(af.RARITIES)
    assert spread["Cataclysmic"] > 0.0
    assert 1 / spread["Cataclysmic"] > 100_000, (
        "a Cataclysmic at difficulty tier 1 should be a remote chance rather "
        f"than a real one: one drop in {1 / spread['Cataclysmic']:.0f}")


def test_the_weights_fall_in_two_segments_split_at_the_enchantment_boundary():
    """The shape the project owner set on 2026-08-18.

    Written out as the ratios rather than as the eight shares, because the
    ratios are the decision and the shares follow from them: the four ordinary
    rarities each 2.5 times rarer than the one below, a step of 8 at the
    boundary, then the four enchanted rarities each 5 times rarer.
    """
    weight = loot.RARITY_DROP_WEIGHT

    for lower, higher in (("Everyday", "Quality"), ("Quality", "Superb"),
                          ("Superb", "Masterful")):
        assert weight[lower] / weight[higher] == pytest.approx(2.5)

    assert weight["Masterful"] / weight["Legendary"] == pytest.approx(8.0)

    for lower, higher in (("Legendary", "Mythical"), ("Mythical", "Ascendant"),
                          ("Ascendant", "Cataclysmic")):
        assert weight[lower] / weight[higher] == pytest.approx(5.0)


def test_a_cataclysmic_drop_is_one_in_twenty_five_thousand():
    """The figure the project owner chose, after one in 255 was rejected as too
    generous. At difficulty tier 8 with no magic find."""
    share = loot.rarity_distribution(8)["Cataclysmic"]
    assert 1 / share == pytest.approx(25531, rel=0.001)


def test_masterful_stays_common_enough_to_craft_with():
    """The reason the ladder falls in two segments rather than one.

    Masterful is the top of the ordinary ladder, and `docs/Cataclysm_GDD_v2.md`
    fits its affix values against a full set of it. Crafting promotes a piece
    upward from there, so it is the supply line rather than the destination. A
    single ratio steep enough to make Cataclysmic this rare would have put
    Masterful past one in 80.
    """
    share = loot.rarity_distribution(8)["Masterful"]
    assert 1 / share < 40


def test_the_distribution_follows_the_weights_rather_than_the_ladder():
    """Change a weight and the share changes with it.

    A CHECK ON THE MECHANISM RATHER THAN THE NUMBERS. It replaces the shipped
    weights with a flat set and makes one rarity three times as heavy, so it
    still fails if the cascade ever stops reading the table -- and it does not
    have to be rewritten every time the shipped weights are tuned.
    """
    flat = dict.fromkeys(loot.RARITY_DROP_WEIGHT, 1.0)
    tripled = dict(flat, Superb=3.0)

    original = loot.RARITY_DROP_WEIGHT
    try:
        loot.RARITY_DROP_WEIGHT = flat
        even = loot.rarity_distribution(8)
        loot.RARITY_DROP_WEIGHT = tripled
        heavier = loot.rarity_distribution(8)
    finally:
        loot.RARITY_DROP_WEIGHT = original

    # COMPARED AS A RATIO BETWEEN TWO RARITIES IN THE SAME SEGMENT, where this
    # used to compare a share against 3/10 directly. Issue #886 gave the
    # ordinary segment a per-tier multiplier that differs by rung, so a share on
    # its own no longer follows from the table alone. Superb and Masterful both
    # carry that multiplier, it cancels in their ratio, and tripling Superb's
    # weight therefore has to triple their ratio exactly.
    before = even["Superb"] / even["Masterful"]
    after = heavier["Superb"] / heavier["Masterful"]
    assert after / before == pytest.approx(3.0, rel=1e-9)

    assert sum(even.values()) == pytest.approx(1.0, abs=1e-9)
    assert sum(heavier.values()) == pytest.approx(1.0, abs=1e-9)


# --------------------------------------------------------------------------
# The distribution moves with the difficulty tier. Issue #886.
# --------------------------------------------------------------------------

def test_difficulty_tier_one_is_the_gear_rarity_sheet_untouched():
    """The per-tier curve starts where the workbook already is.

    The Gear Rarity sheet is authoritative and `RARITY_DROP_WEIGHT` mirrors it.
    A tier 1 that came out as anything else would mean the sheet had stopped
    describing any tier at all, and the cross-check against it in
    `tools/tests/test_loot_sheet_matches_the_model.py` would be comparing two
    different things without saying so.

    THE PENALTY IS MULTIPLIED BACK OUT BEFORE COMPARING, and that is not a
    dodge. Since the cap became a penalty on 2026-08-23, `drop_weight` also
    divides by how far a rarity sits above the tier, and at difficulty tier 1
    that is real: Superb is two rungs up, so its finished weight is a quarter
    of the sheet's on purpose. The claim here is about the per-tier shape.
    """
    for rarity, weight in loot.RARITY_DROP_WEIGHT.items():
        shaped = (loot.drop_weight(rarity, 1)
                  * loot.penalty_above_the_tier(rarity, 1))
        assert shaped == pytest.approx(weight, rel=1e-12)


def test_going_deeper_drops_less_of_the_weakest_rarity():
    """What the project owner reported: only Everyday and Quality ever dropped.

    From difficulty tier 2 upward. Tier 1 reaches two rarities and tier 2 three,
    so at the very bottom it is the cap moving the shares rather than the
    weights.
    """
    everyday = [loot.rarity_distribution(tier)["Everyday"]
                for tier in range(2, af.DIFFICULTY_TIERS + 1)]

    assert everyday == sorted(everyday, reverse=True)
    assert everyday[0] > everyday[-1] * 2, (
        "Everyday should fall a long way between tier 2 and tier 8 rather than "
        f"inch down: {everyday[0]:.3f} to {everyday[-1]:.3f}")


def test_the_deepest_tier_mostly_drops_masterful():
    """The endpoint the project owner chose on 2026-08-23, a fall ratio of 0.8.

    Masterful is the top of the ordinary ladder and `docs/Cataclysm_GDD_v2.md`
    fits its affix values against a full set of it, so a deep player finding
    mostly Masterful is the design's own statement rather than a generosity.
    """
    spread = loot.rarity_distribution(af.DIFFICULTY_TIERS)

    assert (spread["Masterful"] > spread["Superb"] > spread["Quality"]
            > spread["Everyday"])
    assert spread["Everyday"] + spread["Quality"] < 0.40, (
        "the two weakest rarities were 86% of drops before #886 and should now "
        f"be under 40%: {(spread['Everyday'] + spread['Quality']) * 100:.1f}%")


def test_the_enchanted_rarities_do_not_get_commoner_with_the_tier():
    """The half of the 2026-08-23 decision that says what must NOT move.

    The project owner chose to flatten the ordinary segment while leaving
    Legendary at one in 204 and Cataclysmic at one in 25,531. Those two rates
    fall out of the scaling inside `drop_weight` rather than being written down
    anywhere, so nothing else would notice them drifting.

    CHECKED AT THE TIERS WHERE NOTHING IS PENALISED, which is 7 and 8. Below
    them the above-the-tier penalty divides these rungs down deliberately, so
    comparing them to the sheet there would be checking that the penalty had
    been skipped.

    TWO SEPARATE SCALINGS ACT ON THESE FOUR AND THEY MUST NOT BE CONFUSED: the
    pinning, which undoes the per-tier flattening and is meant to leave their
    share alone, and the penalty, which is meant to move it. If the pinning
    ever started reading penalised weights it would cancel the penalty, and
    every tier below 7 would quietly return to the sheet's figures.
    """
    weight = loot.RARITY_DROP_WEIGHT
    whole_ladder = sum(weight.values())

    for tier in (af.DIFFICULTY_TIERS - 1, af.DIFFICULTY_TIERS):
        spread = loot.rarity_distribution(tier)

        for rung in range(loot.ORDINARY_RARITIES + 1, len(af.RARITIES) + 1):
            rarity = loot.rarity_at_index(rung)
            assert spread[rarity] == pytest.approx(
                weight[rarity] / whole_ladder, rel=1e-9), (
                f"{rarity} at difficulty tier {tier}, where nothing is "
                "penalised, has drifted off the share the Gear Rarity "
                "sheet's weights give it")

    # AND THE HEADLINE FIGURE THE 2026-08-18 DECISION WAS ARGUED ON SURVIVES.
    deepest = loot.rarity_distribution(af.DIFFICULTY_TIERS)
    assert 1 / deepest["Legendary"] == pytest.approx(204, rel=0.01)
    assert 1 / deepest["Cataclysmic"] == pytest.approx(25531, rel=0.001)


def test_the_ordinary_fall_ratio_runs_from_the_sheet_to_the_endpoint():
    """The curve's two ends are the two constants, and it falls between them.

    A CHECK ON THE CURVE RATHER THAN ON THE SHARES, so it keeps working when the
    endpoint is retuned. Both constants are a one-line change and the project
    owner is expected to move the second one after playing it.
    """
    ratios = [loot.ordinary_fall_at(tier)
              for tier in range(1, af.DIFFICULTY_TIERS + 1)]

    assert ratios[0] == pytest.approx(loot.ORDINARY_FALL_AT_TIER_ONE)
    assert ratios[-1] == pytest.approx(loot.ORDINARY_FALL_AT_DEEPEST)
    assert ratios == sorted(ratios, reverse=True)


def test_a_difficulty_tier_off_the_ladder_is_refused():
    """There are eight difficulty tiers, and a ninth is a caller error rather
    than a curve to extrapolate."""
    with pytest.raises(ValueError, match="outside 1 to"):
        loot.ordinary_fall_at(0)
    with pytest.raises(ValueError, match="outside 1 to"):
        loot.ordinary_fall_at(af.DIFFICULTY_TIERS + 1)


def test_an_unknown_rarity_has_no_drop_weight():
    with pytest.raises(ValueError, match="is not a rarity"):
        loot.drop_weight("Splendid", 4)


def test_no_amount_of_magic_find_stops_a_rarity_dropping():
    """The opposite of what this file asserted before issue #890.

    IT USED TO CHECK THAT MAGIC FIND REMOVED THE WEAKEST RARITY, and treated
    that as a feature: magic find multiplied each cascade step, a step saturates
    at 1, and past about +80% at difficulty tier 8 nothing could fall through to
    Everyday. That reading was wrong. The same arithmetic made a Boss drop
    Masterful 97.6% of the time at tier 8 and Quality 100% of the time at tier 1,
    which is the complaint #886 was filed about, in a worse form.

    Magic find now multiplies weights instead. Every weight stays above zero, so
    a rung's share of the weight at or below it is always under one and no rarity
    can be removed at any magic find at all.
    """
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for magic_find in (0.0, 80.0, 300.0, 500.0, 10_000.0, 1e9):
            spread = loot.rarity_distribution(tier, magic_find)
            for rarity in af.RARITIES:
                assert spread[rarity] > 0.0, (
                    f"{rarity} never drops at difficulty tier {tier} with "
                    f"{magic_find}% magic find")


def test_a_boss_kill_drops_a_spread_of_rarities_rather_than_one():
    """The fault issue #890 was about, checked from the player's side.

    A Boss carries +300% magic find and dropped Masterful 97.6% of the time at
    difficulty tier 8, with Everyday, Quality and Superb impossible. At tier 1 it
    dropped Quality 100% of the time -- every item identical.
    """
    for tier in (1, af.DIFFICULTY_TIERS):
        for enemy in ("Boss", "Cataclysm Boss"):
            spread = loot.rarity_distribution(tier, loot.magic_find_from(enemy))
            reachable = [share for share in spread.values() if share > 0.0]

            assert max(reachable) < 0.90, (
                f"a {enemy} kill at difficulty tier {tier} drops one rarity "
                f"{max(reachable) * 100:.1f}% of the time, which is the fault "
                "#890 was about")
            assert len(reachable) == len(af.RARITIES), (
                f"a {enemy} kill at difficulty tier {tier} cannot drop every "
                "rarity")


def test_magic_find_cannot_run_away_however_much_is_carried():
    """The ceiling, and why both halves of #890 were needed.

    Putting magic find on the weights stops it removing a rarity. On its own it
    then runs away instead, because the multiplier is raised to
    `MAGIC_FIND_REACH`: +400% made a Cataclysmic drop 77 times likelier, where
    the note of 2026-08-18 expected about 5. Diminishing returns bound it.
    """
    assert loot.effective_magic_find(0.0) == 0.0

    for magic_find in (1.0, 400.0, 10_000.0, 1e12):
        effective = loot.effective_magic_find(magic_find)
        assert 0.0 < effective < loot.MAGIC_FIND_CEILING
        assert effective <= magic_find

    # AND THE CONSEQUENCE THE CEILING EXISTS FOR: the rarest drop settles rather
    # than climbing without limit.
    huge = loot.rarity_distribution(8, 1e9)["Cataclysmic"]
    assert 1 / huge > 300, (
        f"with unlimited magic find a Cataclysmic drops one in {1 / huge:.0f}, "
        "which is not a ceiling")


def test_a_distribution_sums_to_one_at_every_tier_and_magic_find():
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for magic_find in (0.0, 25.0, 100.0, 700.0):
            total = sum(loot.rarity_distribution(tier, magic_find).values())
            assert total == pytest.approx(1.0, abs=1e-9)


# --------------------------------------------------------------------------
# Magic find
# --------------------------------------------------------------------------

def test_magic_find_is_worth_more_the_rarer_the_item():
    """What magic find does since issue #890, stated as the ordering.

    IT USED TO DOUBLE THE RAREST DROP AT +100%, which was Path of Exile's own
    statement made concrete. That went with the mechanism: magic find multiplied
    every rung of the cascade equally, which is also what let a rung saturate and
    wipe out every rarity below it.

    It now multiplies each rarity's WEIGHT, and the rarer the rarity the more of
    the multiplier it receives. So the effect is no longer one figure for the
    whole ladder, and the thing worth holding is the ordering rather than any
    number: every rarity above the floor gains, the floor loses, and each gains
    more than the one below it.
    """
    plain = loot.rarity_distribution(8)
    lucky = loot.rarity_distribution(8, magic_find=100.0)

    gains = [lucky[rarity] / plain[rarity] for rarity in af.RARITIES]

    assert gains[0] < 1.0, "the weakest rarity should lose share, not gain it"
    assert gains == sorted(gains), (
        "each rarity should gain more from magic find than the one below it: "
        + ", ".join(f"{r} {g:.2f}x" for r, g in zip(af.RARITIES, gains, strict=True)))
    assert gains[-1] > 1.0


def test_magic_find_moves_weight_upward_and_takes_it_from_the_floor():
    plain = loot.rarity_distribution(8)
    lucky = loot.rarity_distribution(8, magic_find=200.0)

    assert lucky["Cataclysmic"] > plain["Cataclysmic"]
    assert lucky["Everyday"] < plain["Everyday"]


def test_negative_magic_find_is_refused():
    """It is an added percentage with a baseline of zero, so below zero is a
    caller passing something else -- a multiplier, or loot quantity."""
    with pytest.raises(ValueError, match="cannot be negative"):
        loot.rarity_step_chance(4, tier=8, magic_find=-1.0)


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


def test_rolling_can_return_a_rarity_far_above_the_tier():
    """It used to assert the opposite, and that was the cap.

    The exact distribution says every rarity has a share above zero at every
    difficulty tier. This is the half of that claim about what actually comes
    out of `roll_rarity`, which walks the cascade rather than reading the
    distribution -- so a cascade that still stopped at the old cap would pass
    every arithmetic check and fail here.

    WITH A LARGE MAGIC FIND AND MANY ROLLS, because at difficulty tier 1 a
    Cataclysmic is one drop in 1.5 million without help and rolling for one
    would take all day.
    """
    rng = random.Random(7)
    for tier in (1, 3, 5):
        reach = loot.rarity_index(loot.highest_unpenalised_rarity(tier))
        above = {loot.roll_rarity(tier, magic_find=100_000.0, rng=rng)
                 for _ in range(20_000)}

        assert any(loot.rarity_index(r) > reach for r in above), (
            f"20,000 rolls at difficulty tier {tier} never produced a rarity "
            f"above {loot.highest_unpenalised_rarity(tier)}, so the cascade "
            "is still stopping at the old cap")


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
    """Four enchantments and nothing else, which is what the design says it is.

    THE WEIGHT TABLE IS SWAPPED RATHER THAN THE RARITY FORCED, so this still
    goes through `roll_item` and every step it takes, which is the point: a
    hand-built item would prove nothing about the code path.

    IT USED TO USE A MAGIC FIND NO CHARACTER COULD HAVE, relying on the top
    rung saturating at certainty. Issue #890 removed saturation deliberately --
    it was what made a Boss drop one rarity of item -- so magic find can no
    longer force a rarity, and at its ceiling a Cataclysmic is still only about
    one drop in 333.
    """
    top_heavy = dict.fromkeys(loot.RARITY_DROP_WEIGHT, 1.0)
    top_heavy["Cataclysmic"] = 10_000.0

    original = loot.RARITY_DROP_WEIGHT
    try:
        loot.RARITY_DROP_WEIGHT = top_heavy
        rng = random.Random(1234)
        seen = False
        for _ in range(200):
            item = loot.roll_item("Relic", tier=8, magic_find=0.0, rng=rng)
            if item.rarity != "Cataclysmic":
                continue
            seen = True
            assert item.affixes == ()
            assert item.enchantments == 4
            assert item.gear_level == 10
    finally:
        loot.RARITY_DROP_WEIGHT = original

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


# --------------------------------------------------------------------------
# Sockets
# --------------------------------------------------------------------------

def test_a_drop_never_has_more_sockets_than_its_base_allows():
    rng = random.Random(300)
    for slot in sorted(af.GEAR_SLOTS):
        for _ in range(200):
            item = loot.roll_item(slot, tier=8, magic_find=100.0, rng=rng)
            assert 0 <= item.sockets <= loot.max_sockets_for(item.base)


def test_a_drop_can_arrive_with_none_and_with_all_of_them():
    """Both ends of the range are reachable, which "0 to n" means.

    A CHECK THAT WOULD OTHERWISE PASS SILENTLY. A roll that never produced zero,
    or never produced the maximum, satisfies the bounds test above perfectly.
    """
    rng = random.Random(301)
    seen = {loot.roll_item("Chest", 8, 0.0, rng).sockets for _ in range(600)}
    assert 0 in seen, "no Chest ever dropped with no sockets"
    assert 6 in seen, "no Chest ever dropped with all six sockets"


def test_the_socket_count_does_not_depend_on_the_difficulty_tier():
    """Chosen by the project owner over a tier-gated roll.

    It is the one place the drop rules do NOT gate on the difficulty tier, so it
    is worth a test rather than an assumption: a tier 1 Chest can drop with all
    six sockets.
    """
    rng = random.Random(302)
    seen = {loot.roll_item("Chest", tier=1, magic_find=0.0, rng=rng).sockets
            for _ in range(600)}
    assert seen == {0, 1, 2, 3, 4, 5, 6}


def test_the_socket_maximum_of_every_slot_matches_the_design_document():
    """Written out again on purpose.

    Every other socket test reads the maxima from the same table the code uses,
    so all of them pass together if that table is wrong. These are the twelve
    numbers in the socket table of section VI of docs/Cataclysm_GDD_v2.md.
    """
    stated = {"Head": 2, "Chest": 6, "Shoulders": 2, "Gloves": 2, "Pants": 4,
              "Boots": 2, "Belt": 4, "Ring": 1, "Necklace": 1, "Relic": 4}
    for slot, most in stated.items():
        assert loot.MAX_SOCKETS_BY_SLOT[slot] == most, slot

    assert loot.MAX_SOCKETS_BY_WEAPON_HANDS == {1: 3, 2: 6}


def test_a_two_handed_weapon_carries_twice_a_one_handers_sockets():
    one_handed = [b for b in af.bases_for("Weapon") if b.hands == 1]
    two_handed = [b for b in af.bases_for("Weapon") if b.hands == 2]
    assert one_handed and two_handed
    assert (loot.max_sockets_for(one_handed[0]) * 2
            == loot.max_sockets_for(two_handed[0]))


def test_a_base_in_a_slot_with_no_maximum_is_refused():
    class Nowhere:
        name = "Tiara"
        slot = "Crown"

    with pytest.raises(ValueError, match="no socket maximum"):
        loot.max_sockets_for(Nowhere())


def test_that_the_socket_slot_check_actually_fires():
    original = loot.MAX_SOCKETS_BY_SLOT
    try:
        loot.MAX_SOCKETS_BY_SLOT = {"Head": 2}
        with pytest.raises(AssertionError, match="does not match the gear slots"):
            loot._check_every_gear_slot_has_a_socket_maximum()
    finally:
        loot.MAX_SOCKETS_BY_SLOT = original


def test_that_the_loadout_socket_check_actually_fires():
    original = loot.MAX_SOCKETS_BY_WEAPON_HANDS
    try:
        loot.MAX_SOCKETS_BY_WEAPON_HANDS = {1: 2, 2: 6}
        with pytest.raises(AssertionError, match="worth more sockets"):
            loot._check_two_one_handed_weapons_match_a_two_hander()
    finally:
        loot.MAX_SOCKETS_BY_WEAPON_HANDS = original


def test_that_the_socket_total_check_actually_fires():
    """One mistyped maximum changes the total, which is what this catches."""
    original = loot.MAX_SOCKETS_BY_SLOT
    try:
        loot.MAX_SOCKETS_BY_SLOT = dict(original, Chest=5)
        with pytest.raises(AssertionError, match="the design says"):
            loot._check_the_socket_maxima_add_up_to_the_design_total()
    finally:
        loot.MAX_SOCKETS_BY_SLOT = original


# --------------------------------------------------------------------------
# What a kill drops
# --------------------------------------------------------------------------

ENEMY_RARITIES = list(loot.ENEMY_GEAR_DROPS)


def test_the_ladder_is_the_six_enemy_rarities_the_power_model_has():
    """Six, not eight. They come from scoring.RARITY_WEIGHTS, which is a port of
    an external power model, so the count is not this project's to change."""
    from cataclysm_sim import scoring
    assert ENEMY_RARITIES == list(scoring.RARITY_WEIGHTS)
    assert len(ENEMY_RARITIES) == 6


def test_a_common_kill_drops_the_genre_figure():
    """0.16, Path of Exile's base chance for an item from a normal monster,
    taken as the starting point because this design had none."""
    assert loot.expected_gear_drops("Common") == pytest.approx(0.16)


def test_a_rarer_enemy_drops_more_and_better():
    """Both columns rise together. Either one falling would be a typo that
    nothing else reports."""
    counts = [loot.expected_gear_drops(r) for r in ENEMY_RARITIES]
    finds = [loot.magic_find_from(r) for r in ENEMY_RARITIES]
    assert counts == sorted(counts) and len(set(counts)) == len(counts)
    assert finds == sorted(finds) and len(set(finds)) == len(finds)


def test_a_common_kill_adds_no_magic_find():
    """The baseline the whole ladder is read against."""
    assert loot.magic_find_from("Common") == 0.0


def test_loot_quantity_is_a_percentage_of_what_would_otherwise_drop():
    """The design states the baseline is 100, so 100 changes nothing. Every
    source of loot quantity is an increase, and an increase applied to a
    baseline of zero would be worth nothing."""
    assert loot.expected_gear_drops("Boss", 100.0) == \
        pytest.approx(loot.ENEMY_GEAR_DROPS["Boss"])
    assert loot.expected_gear_drops("Boss", 400.0) == \
        pytest.approx(loot.ENEMY_GEAR_DROPS["Boss"] * 4)
    assert loot.expected_gear_drops("Boss", 0.0) == 0.0


def test_negative_loot_quantity_is_rejected():
    with pytest.raises(ValueError, match="cannot be negative"):
        loot.expected_gear_drops("Boss", -1.0)


def test_an_unknown_enemy_rarity_is_rejected():
    """"Rare" is the one that would be typed by mistake: the design document's
    older enemy list had it and the ladder does not."""
    with pytest.raises(ValueError, match="not an enemy rarity"):
        loot.expected_gear_drops("Rare")


def test_the_rolled_count_averages_the_expected_one():
    """The fractional part is a probability, so a Common enemy drops nothing
    most of the time and the mean still comes out at 0.16. Rounding to the
    nearest whole number instead would make it drop nothing ever."""
    rng = random.Random(20260818)
    for rarity in ENEMY_RARITIES:
        rolled = [loot.roll_gear_drop_count(rarity, 100.0, rng)
                  for _ in range(20000)]
        expected = loot.expected_gear_drops(rarity)
        assert sum(rolled) / len(rolled) == pytest.approx(expected, rel=0.05)


def test_a_common_kill_usually_drops_nothing_at_all():
    """0.16 is a rate, not a guarantee, and the shape matters as much as the
    mean: most kills give the player nothing.

    THE EXACT SHARE MOVED WHEN THE COUNT BECAME A POISSON DRAW, from 84% empty
    to 85.2%, and a Common enemy can now occasionally drop two. Issue #725.
    """
    rng = random.Random(1)
    rolled = [loot.roll_gear_drop_count("Common", 100.0, rng)
              for _ in range(20000)]

    assert rolled.count(0) / len(rolled) == pytest.approx(0.852, abs=0.02)

    # AND TWO IS POSSIBLE, at about one kill in eighty. Asserted rather than
    # left to chance, because the old method made two impossible at any rate
    # below one and this is the difference.
    assert max(rolled) >= 2


def test_a_whole_number_rate_still_varies():
    """The fault issue #725 was filed for, asserted as its opposite.

    THIS TEST USED TO SAY THE REVERSE. It was called
    `test_a_whole_number_rate_always_drops_that_many` and asserted that a
    Legendary enemy, whose rate is exactly 1.0, always dropped exactly one --
    because the old method put all of the randomness in the fractional part and a
    whole number has none. Four of the six enemy rarities were fixed that way,
    including both bosses. The project owner decided on 2026-08-19 that "item
    count should vary for every enemy".
    """
    rng = random.Random(2)
    rolled = [loot.roll_gear_drop_count("Legendary", 100.0, rng)
              for _ in range(2000)]

    assert len(set(rolled)) > 1, (
        "a Legendary enemy dropped the same number of items every time; the "
        "count is fixed again")
    assert 0 in rolled, "a rate of 1.0 must sometimes give nothing"
    assert max(rolled) >= 3, "a rate of 1.0 must sometimes give several"


def test_every_enemy_rarity_varies_and_averages_its_designed_rate():
    """The whole of issue #725, checked for all six rarities at once.

    THE MEAN IS THE PART THAT MUST NOT MOVE. Every figure on the Enemy Drops
    sheet is an average, and changing how the count is drawn was only acceptable
    because a Poisson draw averages the number it is given. If that stopped being
    true, every drop rate in the design would quietly mean something else.
    """
    rng = random.Random(4)

    for rarity, expected in loot.ENEMY_GEAR_DROPS.items():
        rolled = [loot.roll_gear_drop_count(rarity, 100.0, rng)
                  for _ in range(20000)]

        assert sum(rolled) / len(rolled) == pytest.approx(expected, rel=0.06), (
            f"{rarity} averages {sum(rolled) / len(rolled)} items and the "
            f"design says {expected}")
        assert len(set(rolled)) > 1, f"{rarity} drops a fixed number of items"


def test_a_kill_produces_whole_items():
    """HOW MANY IS NOT ASSERTED, because a Boss's five is a mean rather than a
    count since issue #725. What is asserted is that every item that arrives is
    a whole one."""
    rng = random.Random(3)
    items = loot.roll_drops_from_kill("Boss", 8, 0.0, 100.0, rng)

    assert items, "a Boss dropping nothing is possible but not at this seed"
    for item in items:
        assert item.base.slot in af.GEAR_SLOTS
        assert item.rarity in af.RARITIES


def test_the_enemys_magic_find_reaches_the_rarity_roll():
    """The point of the whole mechanic, and the thing that would silently not
    happen: `roll_drops_from_kill` adds the enemy's contribution to the
    player's, so a caller cannot forget it.

    Measured rather than asserted on the call, because passing it and then
    discarding it would look identical from outside.
    """
    rng = random.Random(4)
    rounds = 4000

    def share_above_superb(enemy_rarity: str) -> float:
        seen = 0
        for _ in range(rounds):
            for item in loot.roll_drops_from_kill(
                    enemy_rarity, 8, 0.0, 100.0, rng):
                seen += af.RARITIES.index(item.rarity) >= 3
        return seen

    # A Herald adds 150% magic find and drops two items; a Common adds none and
    # drops 0.16. Comparing shares rather than counts would be confounded by
    # that, so this compares against what the distribution itself predicts.
    plain = loot.rarity_distribution(8, 0.0)
    lucky = loot.rarity_distribution(8, loot.magic_find_from("Herald"))
    above = af.RARITIES[3:]
    assert sum(lucky[r] for r in above) > sum(plain[r] for r in above) * 1.5

    # And the roll really uses it: a Herald's drops beat what no-magic-find
    # drops would give over the same number of items.
    heralds = share_above_superb("Herald")
    expected_without = sum(plain[r] for r in above) * rounds * 2
    assert heralds > expected_without * 1.3, (
        f"a Herald's {rounds * 2} drops gave {heralds} above Superb, and with "
        f"no magic find at all they would give about {expected_without:.0f}")


def test_the_import_time_checks_on_the_kill_tables_can_fail():
    """A check that cannot fail is worthless. Each is fed the condition it
    guards against and must refuse."""
    original = loot.ENEMY_GEAR_DROPS
    try:
        loot.ENEMY_GEAR_DROPS = dict(original, Elite=0.0)
        with pytest.raises(AssertionError, match="drop nothing at all"):
            loot._check_every_kill_can_drop_something()

        loot.ENEMY_GEAR_DROPS = dict(original, Boss=0.2)
        with pytest.raises(AssertionError, match="drops no more"):
            loot._check_a_better_enemy_never_drops_less()

        loot.ENEMY_GEAR_DROPS = {k: v for k, v in original.items()
                                 if k != "Herald"}
        with pytest.raises(AssertionError, match="different"):
            loot._check_every_enemy_rarity_has_a_drop_rate()
    finally:
        loot.ENEMY_GEAR_DROPS = original

    finds = loot.ENEMY_MAGIC_FIND
    try:
        loot.ENEMY_MAGIC_FIND = dict(finds, Common=25.0)
        with pytest.raises(AssertionError, match="rather than none"):
            loot._check_a_common_kill_adds_no_magic_find()

        loot.ENEMY_MAGIC_FIND = dict(finds, Boss=10.0)
        with pytest.raises(AssertionError, match="adds no more magic find"):
            loot._check_a_better_enemy_never_drops_less()
    finally:
        loot.ENEMY_MAGIC_FIND = finds


# --------------------------------------------------------------------------
# Which slot a drop is for
# --------------------------------------------------------------------------

def test_every_slot_is_equally_likely():
    rng = random.Random(20260818)
    drawn = [loot.roll_slot(rng) for _ in range(22000)]
    seen = collections.Counter(drawn)
    assert set(seen) == set(af.GEAR_SLOTS)
    for slot, count in seen.items():
        assert count == pytest.approx(22000 / len(af.GEAR_SLOTS), rel=0.1), slot


def test_a_ring_is_no_likelier_than_a_helmet_even_though_eight_are_worn():
    """The consequence of the rule as stated, pinned so it cannot change
    unnoticed.

    `affixes.GEAR_SLOTS` maps each slot to how many are WORN and a character
    wears eight rings. Uniform over slots therefore does NOT make each worn
    position equally likely: a ring position fills about an eighth as often as
    the helmet. If the design ever moves to weighting by worn count, this test
    is the one that says so.
    """
    assert af.GEAR_SLOTS["Ring"] == 8
    assert af.GEAR_SLOTS["Head"] == 1

    rng = random.Random(5)
    seen = collections.Counter(loot.roll_slot(rng) for _ in range(22000))
    assert seen["Ring"] == pytest.approx(seen["Head"], rel=0.15)


def test_a_weapon_is_not_a_quarter_of_every_drop():
    """Uniform over SLOTS, not over bases. There are 14 weapon bases against
    four for most slots, so drawing from all 55 bases would give a weapon about
    a quarter of the time."""
    rng = random.Random(6)
    seen = collections.Counter(loot.roll_slot(rng) for _ in range(11000))
    assert seen["Weapon"] / 11000 == pytest.approx(1 / 11, rel=0.15)


def test_a_kill_can_drop_for_more_than_one_slot():
    """The slot is rolled per item rather than once per kill, so a Boss's five
    items are not five of the same thing."""
    rng = random.Random(20260818)
    slots = set()
    for _ in range(50):
        for item in loot.roll_drops_from_kill("Boss", 8, 0.0, 100.0, rng):
            slots.add(item.base.slot)
    assert len(slots) > 1


# --------------------------------------------------------------------------
# Crafting materials, on their own roll
# --------------------------------------------------------------------------

def test_a_kill_drops_as_many_materials_as_gear():
    """Halved from twice the gear rate on 2026-08-23, issue #850.

    THE RELATIONSHIP IS ASSERTED RATHER THAN THE SIX FIGURES, and it was written
    that way when the ratio was two. It still earns its place at one: what the
    test is really guarding is that the two columns move together deliberately,
    so a change to one rarity alone shows up here rather than passing quietly.
    """
    for rarity in loot.ENEMY_GEAR_DROPS:
        assert loot.expected_material_drops(rarity) == pytest.approx(
            loot.expected_gear_drops(rarity))


def test_the_material_drop_figures_are_what_the_owner_chose():
    """The six numbers, stated once so halving them again cannot pass unnoticed.

    The test above compares two columns against each other, so multiplying BOTH
    by the same factor would satisfy it. This is the one that says what the
    figures actually are.
    """
    assert loot.ENEMY_MATERIAL_DROPS == {
        "Common":          0.16,
        "Elite":           0.5,
        "Legendary":       1.0,
        "Herald":          2.0,
        "Boss":            5.0,
        "Cataclysm Boss": 12.0,
    }


def test_loot_quantity_multiplies_material_drops_too():
    assert loot.expected_material_drops("Boss", 400.0) == \
        pytest.approx(loot.ENEMY_MATERIAL_DROPS["Boss"] * 4)


def test_each_material_tier_is_four_times_rarer_than_the_one_below():
    weights = [loot.MATERIAL_TIER_DROP_WEIGHT[t] for t in loot.MATERIAL_TIERS]
    for below, above in zip(weights[:-1], weights[1:], strict=True):
        assert above == pytest.approx(below / 4)


def test_the_top_material_tier_is_one_drop_in_the_stated_number():
    """341, which is what the 256 to 1 spread across five tiers comes to. Stated
    as a number here rather than recomputed from the weights, so mistyping two
    weights that still agree with each other is caught."""
    shares = loot.material_tier_distribution(0.0)
    assert 1 / shares["Extremely Rare"] == pytest.approx(341, abs=1)


def test_a_named_top_tier_material_is_one_drop_in_seventeen_hundred():
    """Purified Essence is the only thing that clears the Consumption
    Threshold, so how often it turns up is the figure the tier weight was
    chosen against.

    IT WAS ONE IN 1,023 AND IS NOW ONE IN 1,705, because the top tier went from
    three materials to five on 2026-08-23. The ten upgrade stones replaced one
    placeholder row and were spread two to a tier, so the +9 and +10 stones
    share this rung. Issue #852.

    THAT IS A REAL DILUTION AND NOT AN ACCOUNTING CHANGE. A named top-tier
    material is now two thirds as likely to turn up as it was, and the tier
    weight was not adjusted to compensate.
    """
    shares = loot.material_tier_distribution(0.0)
    in_tier = loot.MATERIALS_IN_TIER["Extremely Rare"]
    assert 1 / shares["Extremely Rare"] * in_tier == pytest.approx(1705, abs=5)


def test_the_material_distribution_always_sums_to_one():
    for magic_find in (0.0, 50.0, 200.0, 500.0, 5000.0):
        total = sum(loot.material_tier_distribution(magic_find).values())
        assert total == pytest.approx(1.0)


def test_magic_find_raises_the_material_tier():
    """This departs from the genre on purpose: Path of Exile's item rarity does
    not affect currency at all. It applies here so that a harder enemy is more
    rewarding in materials as well as in gear."""
    plain = loot.material_tier_distribution(0.0)
    lucky = loot.material_tier_distribution(300.0)
    assert lucky["Extremely Rare"] > plain["Extremely Rare"] * 2
    assert lucky["Common"] < plain["Common"]


def test_a_saturating_magic_find_stops_the_commonest_tier_appearing():
    """A CONSEQUENCE WORTH PINNING. Each cascade step is multiplied by magic
    find and clamped at certainty, so at 500% the Uncommon rung reaches 1 and
    nothing falls through to Common.

    A Cataclysm Boss adds exactly 500%, so its materials are never Common. That
    is a reasonable shape -- the ordinary supply comes from ordinary enemies,
    which add no magic find at all -- but it is sharp enough that it should not
    be discovered by accident.
    """
    assert loot.material_tier_distribution(500.0)["Common"] == 0.0
    assert loot.material_tier_distribution(0.0)["Common"] > 0.7
    assert loot.magic_find_from("Cataclysm Boss") == 500.0


def test_rolling_materials_draws_from_the_stated_distribution():
    rng = random.Random(818)
    stated = loot.material_tier_distribution(0.0)
    drawn = collections.Counter(
        loot.roll_material_tier(0.0, rng) for _ in range(40000))
    for tier in ("Common", "Uncommon", "Rare"):
        assert drawn[tier] / 40000 == pytest.approx(stated[tier], rel=0.1)


def test_a_kill_produces_material_tiers():
    """HOW MANY IS NOT ASSERTED, for the reason `test_a_kill_produces_whole_items`
    gives: a Boss's ten materials became a mean under issue #725. Materials go
    through the same `roll_count` as gear, which is what the average below
    checks."""
    rng = random.Random(9)

    tiers = loot.roll_material_drops_from_kill("Boss", 0.0, 100.0, rng)
    assert tiers, "a Boss dropping no materials is possible but not at this seed"
    assert set(tiers) <= set(loot.MATERIAL_TIERS)

    # AND MATERIALS AVERAGE THEIR OWN DESIGNED RATE, which is twice the gear
    # rate. A second copy of the counting arithmetic living here was how the two
    # could have drifted apart; there is one function now.
    counts = [len(loot.roll_material_drops_from_kill("Boss", 0.0, 100.0, rng))
              for _ in range(4000)]
    assert sum(counts) / len(counts) == pytest.approx(
        loot.ENEMY_MATERIAL_DROPS["Boss"], rel=0.06)
    assert len(set(counts)) > 1, "a Boss drops a fixed number of materials"


def test_the_material_import_time_checks_can_fail():
    original = loot.MATERIAL_TIER_DROP_WEIGHT
    try:
        loot.MATERIAL_TIER_DROP_WEIGHT = dict(original, Rare=512.0)
        with pytest.raises(AssertionError, match="not rarer at all"):
            loot._check_no_material_tier_gets_commoner_as_it_rises()

        loot.MATERIAL_TIER_DROP_WEIGHT = {k: v for k, v in original.items()
                                          if k != "Rare"}
        with pytest.raises(AssertionError, match="disagree"):
            loot._check_every_material_tier_has_a_weight()
    finally:
        loot.MATERIAL_TIER_DROP_WEIGHT = original

    drops = loot.ENEMY_MATERIAL_DROPS
    try:
        loot.ENEMY_MATERIAL_DROPS = dict(drops, Boss=0.5)
        with pytest.raises(AssertionError, match="drops no more materials"):
            loot._check_a_better_enemy_never_drops_fewer_materials()

        loot.ENEMY_MATERIAL_DROPS = {k: v for k, v in drops.items()
                                     if k != "Elite"}
        with pytest.raises(AssertionError, match="different enemy"):
            loot._check_every_enemy_rarity_drops_materials()
    finally:
        loot.ENEMY_MATERIAL_DROPS = drops
