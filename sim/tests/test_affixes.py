"""Tests for the resistance affix families."""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af


# --------------------------------------------------------------------------
# The structure the project owner specified
# --------------------------------------------------------------------------

def test_per_type_value_falls_as_a_family_covers_more():
    """Stated by the project owner: single resistance affixes have the highest
    values, hybrid lower, all-resistance the least.

    Checked as a STRICT decrease. A sorted-list comparison allows ties, so three
    families all granting the same value would pass a test named 'falls' -- and
    that is exactly the case where breadth becomes free and the whole structure
    collapses.
    """
    ordered = sorted(af.RESISTANCE_FAMILIES, key=lambda f: f.breadth)
    values = [f.value_at(7) for f in ordered]
    for wider, narrower in zip(values[1:], values, strict=False):
        assert wider < narrower, f"{values} is not strictly decreasing"


def test_total_coverage_rises_as_a_family_covers_more():
    """The other half of the trade. If a narrow family also gave more in total,
    the broad ones would be strictly worse and there would be no choice.

    Also a strict increase, for the same reason.
    """
    ordered = sorted(af.RESISTANCE_FAMILIES, key=lambda f: f.breadth)
    coverage = [f.total_coverage(7) for f in ordered]
    for wider, narrower in zip(coverage[1:], coverage, strict=False):
        assert wider > narrower, f"{coverage} is not strictly increasing"


def test_the_three_families_cover_one_two_and_all_eight():
    assert af.SINGLE_RESISTANCE.breadth == 1
    assert af.HYBRID_RESISTANCE.breadth == 2
    assert af.ALL_RESISTANCE.breadth == len(af.DAMAGE_TYPES) == 8


# --------------------------------------------------------------------------
# The crossover, which is the reason for having three families
# --------------------------------------------------------------------------

def test_a_single_resistance_affix_is_best_when_one_cataclysm_is_active():
    assert af.best_family(active_cataclysms=1) is af.SINGLE_RESISTANCE


def test_an_all_resistance_affix_is_best_when_all_eight_are_active():
    assert af.best_family(active_cataclysms=8) is af.ALL_RESISTANCE


def test_the_hybrid_family_wins_somewhere_in_the_middle():
    """If no tier preferred the hybrid, it would be a family with no purpose."""
    winners = {af.best_family(n).name for n in range(1, 9)}
    assert af.HYBRID_RESISTANCE.name in winners


def test_all_three_families_are_the_best_choice_at_some_tier():
    """The point of three rather than one. A family that never wins is dead
    content that still costs design and loot table space."""
    winners = {af.best_family(n).name for n in range(1, 9)}
    assert winners == {f.name for f in af.RESISTANCE_FAMILIES}


def test_the_efficient_family_only_ever_broadens_as_tiers_rise():
    """It should not flip back and forth. Breadth must be non-decreasing across
    the run, or the progression is noise rather than a curve."""
    breadths = [af.best_family(n).breadth for n in range(1, 9)]
    assert breadths == sorted(breadths)


# --------------------------------------------------------------------------
# The slot budget
# --------------------------------------------------------------------------

def test_capping_at_tier_eight_costs_a_reasonable_share_of_slots():
    """Reaching the cap on all eight is the hardest defensive requirement in the
    game. It should be a real cost and should not consume the whole character."""
    slots = af.slots_to_cap(af.ALL_RESISTANCE, tier=8, active_cataclysms=8)
    assert 8 <= slots <= 16, f"{slots:.1f} slots"
    assert slots / af.TOTAL_AFFIX_SLOTS < 0.25


def test_capping_at_tier_one_is_cheap():
    """Early on only one damage type is attacking, so resistance should be a
    minor tax rather than a build-defining one."""
    slots = af.slots_to_cap(af.SINGLE_RESISTANCE, tier=1, active_cataclysms=1)
    assert slots <= 5


def test_using_the_wrong_family_costs_meaningfully_more():
    """If the families were within a slot or two of each other everywhere, the
    choice would not matter and three families would be pointless."""
    at_eight_wrong = af.slots_to_cap(af.SINGLE_RESISTANCE, 8, 8)
    at_eight_right = af.slots_to_cap(af.ALL_RESISTANCE, 8, 8)
    assert at_eight_wrong > 2 * at_eight_right


def test_the_slot_total_matches_the_gear_the_design_describes():
    assert af.GEAR_PIECES == 18
    assert af.AFFIX_SLOTS_PER_PIECE == 4
    assert af.TOTAL_AFFIX_SLOTS == 72


# --------------------------------------------------------------------------
# Wasted coverage, which is what drives the crossover
# --------------------------------------------------------------------------

def test_breadth_beyond_the_active_cataclysms_is_wasted():
    """An all-resistance affix covers eight damage types. With two attacking,
    six of those are worth nothing. This is the whole mechanism."""
    with_two = af.ALL_RESISTANCE.useful_coverage(7, active_cataclysms=2)
    with_eight = af.ALL_RESISTANCE.useful_coverage(7, active_cataclysms=8)
    assert with_two == pytest.approx(with_eight / 4)


def test_a_narrow_family_wastes_nothing_even_at_high_tiers():
    single = af.SINGLE_RESISTANCE
    assert (single.useful_coverage(7, 1)
            == single.useful_coverage(7, 8)
            == single.value_at(7))


def test_useful_coverage_is_zero_with_nothing_attacking():
    assert af.ALL_RESISTANCE.useful_coverage(7, active_cataclysms=0) == 0.0


# --------------------------------------------------------------------------
# Affix tiers
# --------------------------------------------------------------------------

def test_there_are_seven_affix_tiers():
    """The crafting material that levels an affix raises it 'up to t7'."""
    assert af.AFFIX_TIERS == (1, 2, 3, 4, 5, 6, 7)
    assert set(af.TIER_FRACTIONS) == set(af.AFFIX_TIERS)


def test_an_affix_reaches_its_full_value_at_tier_seven():
    for family in af.RESISTANCE_FAMILIES:
        assert family.value_at(7) == pytest.approx(family.top_value)


def test_affix_value_rises_with_every_tier():
    for family in af.RESISTANCE_FAMILIES:
        values = [family.value_at(t) for t in af.AFFIX_TIERS]
        assert values == sorted(values)
        assert len(set(values)) == len(values)


def test_the_tier_curve_is_front_loaded_not_linear():
    """An affix should be half its final value by the middle tier, so a mid-tier
    roll is useful rather than filler. A linear curve would put T3 at 0.43."""
    assert af.TIER_FRACTIONS[3] == pytest.approx(0.50)
    linear_at_three = 3 / 7
    assert af.TIER_FRACTIONS[3] > linear_at_three


def test_an_affix_tier_outside_one_to_seven_is_rejected():
    for bad in (0, 8, -1):
        with pytest.raises(ValueError, match="affix tier"):
            af.SINGLE_RESISTANCE.value_at(bad)


# --------------------------------------------------------------------------
# Against the design
# --------------------------------------------------------------------------

def test_there_is_one_resistance_per_damage_type():
    from cataclysm_sim.character import DAMAGE_TYPES as SHEET_TYPES
    assert af.DAMAGE_TYPES == SHEET_TYPES


def test_the_cap_matches_the_character_sheet():
    from cataclysm_sim.character import SOFT_CAPS
    assert af.RESISTANCE_CAP == SOFT_CAPS["resistance_war"] == 70.0
