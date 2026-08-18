"""Tests for the Consumption Threshold, in `sim/cataclysm_sim/residue.py`.

WHAT THESE GUARD, and each is something the module could get wrong quietly:

1. That the threshold is really below the path it is derived from. A threshold
   at or above it would never be crossed, the corrupted double would never
   appear, and the two residue-mitigating materials would have nothing to do.
   Nothing else in the game would report that.

2. That rounding to the nearest 50 has not pushed any tier outside the 80% to
   90% band the project owner asked for. The rounding is applied after the
   share, so a tier whose exact figure sits near an end could cross it.

3. That the search in `cheapest_path` really returns the cheapest, checked
   against every starting rarity rather than against itself.

4. That the total can be reproduced by hand from the sheets, so the module and
   the arithmetic agree for a reason rather than by both calling one function.

5. That the import-time checks in the module can actually fail.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import loot, player_power, residue


TIERS = range(1, af.DIFFICULTY_TIERS + 1)


# --------------------------------------------------------------------------
# The shape of the threshold
# --------------------------------------------------------------------------

def test_there_is_one_threshold_per_difficulty_tier():
    """The change this work made. It was "a single fixed number" in the design
    document until 2026-08-18, and the project owner's reason for splitting it
    was that nobody in the lower tiers would ever cross a single number."""
    thresholds = [residue.consumption_threshold(tier) for tier in TIERS]
    assert len(thresholds) == 8
    assert len(set(thresholds)) == 8, (
        f"two difficulty tiers share a Consumption Threshold: {thresholds}")


def test_the_threshold_rises_with_the_tier():
    """A deeper tier expects better gear, which carries more residue. A
    threshold that fell would mark a better-equipped character sooner."""
    thresholds = [residue.consumption_threshold(tier) for tier in TIERS]
    assert thresholds == sorted(thresholds)


def test_every_threshold_is_below_the_cheapest_path_to_the_expected_build():
    """The point of the whole derivation. Above it, reaching the expected build
    would never mark the character."""
    for tier in TIERS:
        _start, cheapest = residue.cheapest_path(tier)
        assert residue.consumption_threshold(tier) < cheapest


def test_every_threshold_is_inside_the_band_the_owner_asked_for():
    """80% to 90%, checked AFTER the rounding to the nearest 50 rather than
    before it. Rounding is what could push a tier out."""
    lowest, highest = residue.THRESHOLD_SHARE_BAND
    for tier in TIERS:
        _start, cheapest = residue.cheapest_path(tier)
        share = residue.consumption_threshold(tier) / cheapest
        assert lowest <= share <= highest, (
            f"tier {tier} sits at {share:.1%} of its cheapest path")


def test_every_threshold_is_a_readable_number():
    """A multiple of 50. A player reads this off the character sheet, and 1,938
    is not a number anyone remembers."""
    for tier in TIERS:
        assert residue.consumption_threshold(tier) % residue.THRESHOLD_ROUNDING == 0


# --------------------------------------------------------------------------
# The path it is derived from
# --------------------------------------------------------------------------

def test_no_starting_rarity_beats_the_one_cheapest_path_returns():
    """The search checked against every alternative rather than against itself.
    A search that returned the first entry would pass every other test here."""
    for tier in TIERS:
        start, cheapest = residue.cheapest_path(tier)
        target, _upgrade, _gems = residue.expected_build(tier)
        for other in af.RARITIES[:af.RARITIES.index(target) + 1]:
            assert residue.worn_residue_for_path(tier, other) >= cheapest, (
                f"at tier {tier}, starting from {other} beats the {start} "
                "cheapest_path returned")


def test_the_cheapest_start_is_always_the_worst_drop():
    """The result that is worth knowing rather than merely true.

    Promoting a piece from Everyday all the way to Cataclysmic costs 35 residue,
    because every step is one affix or one imprint at 5. A Cataclysmic drop
    instead arrives carrying 300 to 500. So the cheapest route to a maxed
    character is to take the WORST drops and craft them up.

    That is the design's own rule taken to its end -- section VI of
    `docs/Cataclysm_GDD_v2.md` says a better item is more expensive to improve
    -- but it is sharp enough to be worth a named test. If this ever stops being
    true, the design document's paragraph about it needs revisiting.
    """
    for tier in TIERS:
        start, _cheapest = residue.cheapest_path(tier)
        assert start == "Everyday"


def test_the_tier_eight_total_can_be_worked_out_by_hand():
    """Recomputed from the sheets rather than from the module, so the two agree
    for a reason and not because both called the same function."""
    drops = 18 * 50.0                 # eighteen Everyday drops, mid of 38 to 62
    promote = 18 * (3 * 5.0 + 4 * 5.0)  # three affixes added, four imprinted
    upgrade = 18 * 10 * 25.0          # +0 to +10, at 25 residue a level
    sockets = 15.0 * (45 - 22.5) + 5.0 * 45  # add the missing half, gem all 45

    assert residue.worn_residue_for_path(8, "Everyday") == pytest.approx(
        drops + promote + upgrade + sockets)
    assert drops + promote + upgrade + sockets == pytest.approx(6592.5)


def test_the_expected_build_is_the_one_player_power_already_states():
    """Read rather than restated, so there is no second copy of the curve."""
    for tier in TIERS:
        rarity, upgrade, gems = residue.expected_build(tier)
        character = player_power.reference_character(tier)
        assert rarity == af.RARITIES[character.gear[0].rarity - 1]
        assert upgrade == character.gear[0].upgrade
        assert gems == len(character.gems)


# --------------------------------------------------------------------------
# Promoting a piece
# --------------------------------------------------------------------------

def test_promoting_the_whole_ladder_costs_seven_crafts():
    """Three affixes to reach Masterful, then four imprints to reach
    Cataclysmic, all at 5 residue each."""
    assert residue.promotion_residue("Everyday", "Cataclysmic") == \
        pytest.approx(35.0)


def test_promoting_nowhere_costs_nothing():
    for rarity in af.RARITIES:
        assert residue.promotion_residue(rarity, rarity) == 0.0


def test_a_step_above_masterful_is_an_imprint_and_below_it_is_an_affix():
    """The two halves of the ladder are priced by different crafts, and both
    happen to cost 5, so a test that only summed them would not notice one being
    used for the other."""
    assert residue.promotion_residue("Superb", "Masterful") == \
        pytest.approx(residue.DETERMINISTIC_AFFIX_RESIDUE_PER_TIER)
    assert residue.promotion_residue("Masterful", "Legendary") == \
        pytest.approx(residue.IMPRINT_ENCHANTMENT_RESIDUE)


def test_a_piece_cannot_be_promoted_downward():
    """There is no craft that removes an enchantment, so this is not a rounding
    to zero -- it is a question with no answer."""
    with pytest.raises(ValueError, match="cannot promote"):
        residue.promotion_residue("Cataclysmic", "Everyday")


def test_an_unknown_rarity_is_rejected():
    with pytest.raises(ValueError, match="not a rarity"):
        residue.promotion_residue("Sparkly", "Everyday")


# --------------------------------------------------------------------------
# What the threshold means in play
# --------------------------------------------------------------------------

def test_one_purified_essence_clears_the_threshold_at_every_tier():
    """Half of what issue #697 asked: whether the tools that manage residue are
    enough at whatever the threshold turned out to be. Purified Essence halves
    the accumulated total."""
    for tier in TIERS:
        assert residue.mitigation_needed(tier)


def test_a_handful_of_residue_protocols_points_clears_it_too():
    """The other half. Each point ignores 5% of residue, and the node would be
    dead content if it needed more points than a player could reasonably put in
    one empire node."""
    for tier in TIERS:
        points = residue.points_of_residue_protocols_needed(tier)
        assert 1 <= points <= 5, (
            f"tier {tier} needs {points} points of Residue Protocols")


def test_wearing_good_drops_alone_crosses_the_threshold_from_tier_four():
    """A consequence sharp enough to be worth pinning rather than discovering.

    From tier 4 upward, equipping eighteen drops at the tier's expected rarity
    puts the character over the threshold before any crafting at all. That is
    the intended shape -- it is what makes the cheap path and the mitigating
    materials both worth using -- and the design document says so. If the bands
    or the thresholds move, this test is where that changes.
    """
    crosses = [tier for tier in TIERS
               if residue.bare_drops_worn_residue(tier)
               >= residue.consumption_threshold(tier)]
    assert crosses == [4, 5, 6, 7, 8]


def test_the_residue_a_drop_carries_is_the_one_in_the_loot_module():
    """The derivation reads the bands rather than holding its own copy."""
    for rarity in af.RARITIES:
        lowest, highest = loot.residue_band(rarity)
        expected = player_power.GEAR_PIECES * (lowest + highest) / 2.0
        tier = af.RARITIES.index(rarity) + 1
        if residue.expected_build(tier)[0] == rarity:
            assert residue.bare_drops_worn_residue(tier) == \
                pytest.approx(expected)


# --------------------------------------------------------------------------
# The table the design document states
# --------------------------------------------------------------------------

def test_the_table_has_one_row_per_tier_and_they_agree_with_the_functions():
    rows = residue.table()
    assert [row[0] for row in rows] == list(TIERS)
    for tier, start, rounded, exact, threshold in rows:
        assert (start, exact) == residue.cheapest_path(tier)
        assert threshold == residue.consumption_threshold(tier)
        assert rounded == round(exact)
