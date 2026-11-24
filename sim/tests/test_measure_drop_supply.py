"""The measurement behind the issue #2853 drop supply proposal walks the model's own drops.

`sim/measure_drop_supply.py` re-implements the item roll, the kill and the resistance a piece grants so a supply lever
can reach inside them. Its figures are only worth quoting if, with every lever at today's value, it rolls exactly what
`loot` rolls and scores exactly what `measure_power_score_weights.py` scores. These are the checks that say so, and the
checks on the properties the proposal states about its own recommendations. The measurement itself is too slow for the
suite and is run by hand.
"""

import random

import pytest

import measure_drop_supply as d
import measure_power_score_weights as mw
from cataclysm_sim import affixes as af
from cataclysm_sim import loot


@pytest.mark.parametrize("seed", range(6))
@pytest.mark.parametrize("slot", ["Chest", "Ring", "Weapon"])
def test_the_item_roll_is_the_models_own_when_nothing_is_left_out(seed, slot):
    ours = d.roll_item(slot, d.TIER, 100.0, random.Random(seed))
    theirs = loot.roll_item(slot, d.TIER, 100.0, random.Random(seed))
    assert ours == theirs


def test_a_left_out_family_never_rolls_and_the_piece_keeps_its_affix_count():
    rng = random.Random(3)
    seen_suffix_resistance = False
    for _ in range(400):
        item = d.roll_item("Chest", d.TIER, 300.0, rng, exclude=(af.ALL_RESISTANCE,))
        assert all(rolled.affix is not af.ALL_RESISTANCE for rolled in item.affixes)
        assert len(item.affixes) == af.affix_slots_for(item.rarity)
        seen_suffix_resistance |= any(isinstance(r.affix, af.AffixFamily) for r in item.affixes)
    assert seen_suffix_resistance, "no other resistance family rolled either, so this checked nothing"


def test_one_floor_walked_with_todays_supply_is_the_weight_measurements_own_walk():
    """Same seed, same floor: the re-implemented kill draws in `loot`'s order, so the two characters are identical."""
    ours = d.Character(d.TODAY)
    d.walk(ours, 2, random.Random(5))
    theirs = mw.Character()
    mw.walk(theirs, 2, mw.RECOMMENDED, random.Random(5), gap_rule=False)
    assert ours.parts() == theirs.parts()
    assert ours.parts().pieces, "nothing was worn, so this compared two empty characters"


def test_todays_floors_read_the_models_own_families():
    read = d.families(d.TODAY)
    for family in af.RESISTANCE_FAMILIES:
        assert read[family.name] is family


def test_no_floors_falls_back_to_the_derived_tenth_of_the_top():
    read = d.families(d.Supply("none", floors=(0.0, 0.0, 0.0)))
    assert read["Single resistance"].value_at(1, 0.0, 0) == pytest.approx(4.0)
    assert read["All resistances"].value_at(1, 0.0, 0) == pytest.approx(1.2)
    assert af.SINGLE_RESISTANCE.value_at(1, 0.0, 0) == pytest.approx(10.0)


def test_a_zero_factor_drops_nothing_of_that_kind():
    c = d.Character(d.Supply("nothing", gear=0.0, gems=0.0))
    d.walk(c, 1, random.Random(2))
    assert c.gear_dropped == 0 and c.gems_dropped == 0 and not c.gems
    assert c.level > 1, "the kills still pay experience"


def test_the_gem_proposal_is_todays_figure_at_tier_8_and_never_below_any_tiers_need():
    assert d.halving_share(8) == loot.GEM_DROPS_PER_GEAR_DROP
    for tier in range(1, 9):
        assert d.halving_share(tier) >= d.gem_need_share(tier), tier


def test_the_expected_climb_without_the_rule_is_the_decided_curve():
    assert [level for level, _, _ in d.climb(None)] == mw.LEVELS_AT_TIER_ENDS


def test_the_refit_at_the_decided_rate_reproduces_the_decided_curve():
    levels, _ = d.refit_rate_levels(d.curve.DECIDED_RATE)
    assert levels == mw.LEVELS_AT_TIER_ENDS


@pytest.mark.parametrize("tier", range(1, 9))
def test_the_proposed_band_covers_the_curves_character_and_still_cuts_the_next_tiers(tier):
    """The proposal's two claims: a character on the curve in its tier's reference gear is never cut in any dungeon
    its own tier spawns, with at least the ruled band's margin; and the next tier's finished character is still cut
    to at most a fifth of its experience."""
    band = d.proposed_band_down(tier)
    assert band >= d.above_lowest(tier, d.score_at_curve_level(tier)) + d.BAND_MARGIN - 1e-9
    assert band >= d.gap_rewards.RULED.band_down
    if tier < 8:
        assert d.one_tier_over(tier, d.proposed_rule(tier)) <= 0.2, tier
