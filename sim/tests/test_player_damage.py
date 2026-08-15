"""Tests for the composed player damage figure.

`affixes.damage_target()` says what a player NEEDS. `player_damage` says what a
player HAS. These check the second, and check that the two are compared honestly
rather than made to agree.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import player_damage as pd
from cataclysm_sim import player_power as pp
from cataclysm_sim.character import SKILL_SLOTS


# --------------------------------------------------------------------------
# The pipeline itself
# --------------------------------------------------------------------------

def test_the_composed_hit_is_the_three_buckets_multiplied_in_order():
    """Worked by hand from the constants, so this fails if any of them moves.

    At difficulty tier 8 the reference character carries T7 affixes on +10 gear
    and a Greatsword. The Greatsword's implicit is 156 once the two-handed
    multiplier is applied. Five flat damage affixes sit off the weapon at 18
    each and one sits on it at 36, so the base bracket is 156 + 126 = 282. Five
    increased affixes off the weapon at 125% and one on it at 250% make 875%.

        282 x 9.75 = 2749.5
    """
    b = pd.breakdown(8)
    assert b.weapon_damage == pytest.approx(156.0)
    assert b.flat_from_affixes == pytest.approx(126.0)
    assert b.base_bracket == pytest.approx(282.0)
    assert b.increased == pytest.approx(8.75)
    assert b.per_hit == pytest.approx(2749.5)
    assert pd.damage_per_hit(8) == pytest.approx(2749.5)


def test_a_more_multiplier_multiplies_on_its_own_rather_than_joining_the_increases():
    """The whole difference between the two buckets. Two 50% more multipliers
    give 2.25x, not 2.0x, and that only holds if `more` is outside the bracket."""
    plain = pd.damage_per_hit(8)
    assert pd.damage_per_hit(8, more=1.5 * 1.5) == pytest.approx(plain * 2.25)


def test_the_skill_slot_sets_the_share_of_weapon_damage():
    """A basic attack is 100% by definition and every other slot is a share of it."""
    basic = pd.damage_per_hit(8, skill_slot="Basic")
    heavy = pd.damage_per_hit(8, skill_slot="Heavy")
    assert SKILL_SLOTS["Heavy"].typical_damage == 250.0
    assert heavy == pytest.approx(basic * 2.5)


def test_a_support_skill_deals_nothing_because_its_slot_deals_nothing():
    assert SKILL_SLOTS["Support"].typical_damage == 0.0
    assert pd.damage_per_hit(8, skill_slot="Support") == pytest.approx(0.0)


# --------------------------------------------------------------------------
# The two-handed multiplier, which is the gap reference_build.py left open
# --------------------------------------------------------------------------

def test_a_two_handed_weapon_doubles_its_own_implicit_damage():
    """The Greatsword is stored at 78 and supplies 156, because it is two-handed."""
    stored = next(i.value for i in af.base_named("Greatsword").implicits
                  if i.stat == "attack_damage")
    assert stored == pytest.approx(78.0)
    assert pd.weapon_base_damage("Greatsword", af.MAX_GEAR_LEVEL) == \
        pytest.approx(stored * af.TWO_HANDED_MULTIPLIER)


def test_a_one_handed_weapon_supplies_its_stored_value_unchanged():
    stored = next(i.value for i in af.base_named("Axe").implicits
                  if i.stat == "attack_damage")
    assert pd.weapon_base_damage("Axe", af.MAX_GEAR_LEVEL) == pytest.approx(stored)


def test_a_two_hander_doubles_the_damage_affixes_on_it_and_leaves_the_others_alone():
    """This is what `reference_build.py` says it does not model and understates.

    Five affixes off the weapon at 18 and one on it at 36 is 126. If the
    placement were ignored the six would be 108, so the difference is exactly one
    affix's worth.
    """
    two_handed = pd.breakdown(8, "Greatsword")
    one_handed = pd.breakdown(8, "Axe")
    assert two_handed.flat_from_affixes == pytest.approx(126.0)
    assert one_handed.flat_from_affixes == pytest.approx(108.0)
    assert two_handed.increased == pytest.approx(8.75)
    assert one_handed.increased == pytest.approx(7.50)


def test_placing_no_damage_affix_on_the_weapon_removes_the_doubling():
    """The placement is a constant, not a law, so the effect of changing it is
    stated rather than left to be discovered."""
    b = pd.breakdown(8, "Greatsword", flat_affixes=0, increased_affixes=0)
    assert b.flat_from_affixes == pytest.approx(0.0)
    assert b.increased == pytest.approx(0.0)
    # Weapon alone, no affixes at all.
    assert b.per_hit == pytest.approx(156.0)


def test_two_handed_weapons_deal_about_twice_one_handed_per_second():
    """The design rule, measured rather than asserted.

    Per HIT a two-hander is worth far more, and per SECOND it should be worth
    the stated multiplier, because a two-hander's slower swing is what pays for
    its bigger hit. This is the check that the weapon table is internally
    coherent.
    """
    one, two = [], []
    for base in af.ITEM_BASES:
        if not isinstance(base, af.WeaponBase):
            continue
        if pd.weapon_base_damage(base.name, af.MAX_GEAR_LEVEL) <= 0:
            continue
        (two if base.hands == 2 else one).append(
            pd.damage_per_second(8, base.name))

    assert one and two
    ratio = (sum(two) / len(two)) / (sum(one) / len(one))
    assert ratio == pytest.approx(af.TWO_HANDED_MULTIPLIER, abs=0.1), (
        f"two-handed weapons average {ratio:.2f}x one-handed per second, and "
        f"TWO_HANDED_MULTIPLIER is {af.TWO_HANDED_MULTIPLIER}")


# --------------------------------------------------------------------------
# What the tier decides
# --------------------------------------------------------------------------

def test_the_affix_tier_follows_the_drop_gate_rather_than_being_restated():
    for tier in range(1, 9):
        assert pd.affix_tier_at(tier) == af.max_affix_tier_on_a_drop(tier)


def test_the_gear_level_follows_the_reference_character_rather_than_being_restated():
    """The character being scored and the character being measured for damage
    must be the same character, or Power Score and damage describe two people."""
    for tier in range(1, 9):
        assert pd.gear_level_at(tier) == \
            pp.reference_character(tier).gear[0].upgrade


def test_damage_rises_with_every_difficulty_tier():
    hits = [pd.damage_per_hit(t) for t in range(1, 9)]
    assert all(hits[i] < hits[i + 1] for i in range(len(hits) - 1)), hits


# --------------------------------------------------------------------------
# Criticals and rate
# --------------------------------------------------------------------------

def test_no_critical_chance_leaves_the_average_equal_to_the_plain_hit():
    assert pd.average_damage_per_hit(8, crit_chance=0.0, crit_multiplier=250.0) \
        == pytest.approx(pd.damage_per_hit(8))


def test_criticals_raise_the_average_above_the_non_critical_hit():
    """The reference build's own figures, read off `reference_build`."""
    average = pd.average_damage_per_hit(8, crit_chance=10.0,
                                        crit_multiplier=258.0)
    assert average > pd.damage_per_hit(8)
    # 10% of hits at 2.58x: 0.9 + 0.1 * 2.58 = 1.158
    assert average == pytest.approx(pd.damage_per_hit(8) * 1.158)


def test_damage_per_second_is_the_hit_times_the_weapons_own_rate():
    rate = af.base_named("Greatsword").attack_speed
    assert pd.damage_per_second(8, "Greatsword") == \
        pytest.approx(pd.damage_per_hit(8, "Greatsword") * rate)


def test_a_weapon_that_is_not_a_weapon_has_no_rate():
    with pytest.raises(ValueError, match="not a weapon"):
        pd.damage_per_second(8, "Helm")


# --------------------------------------------------------------------------
# What the comparison against the target actually says
# --------------------------------------------------------------------------

def test_the_gap_is_the_composed_hit_over_the_stated_target():
    for tier in (1, 4, 8):
        assert pd.gap_against_target(tier) == pytest.approx(
            pd.damage_per_hit(tier) / af.damage_target(tier))


def test_no_weapon_in_the_game_lands_on_the_damage_target():
    """RECORDS A FINDING, and it is expected to fail the day the finding is fixed.

    `affixes.py` fits every offensive value to a single figure -- 1,683 damage
    per hit at difficulty tier 8 -- and `reference_weapon_base` says that figure
    implies a weapon supplying 90. No weapon supplies 90. Every one-handed weapon
    supplies at most 46 and every two-handed one at least 128, so the target sits
    in a gap between the two families: one-handers fall short of it and
    two-handers overshoot it, and nothing sits on it.

    See the pull request that added this file for the full table.
    """
    one_handed, two_handed = [], []
    for base in af.ITEM_BASES:
        if not isinstance(base, af.WeaponBase):
            continue
        if pd.weapon_base_damage(base.name, af.MAX_GEAR_LEVEL) <= 0:
            continue
        (two_handed if base.hands == 2 else one_handed).append(
            (base.name, pd.gap_against_target(8, base.name)))

    assert one_handed and two_handed
    for name, gap in one_handed:
        assert gap < 1.0, f"{name} reaches the target at {gap:.2f}x"
    for name, gap in two_handed:
        assert gap > 1.0, f"{name} falls short of the target at {gap:.2f}x"


def test_the_reference_greatsword_build_overshoots_the_target_by_about_two_thirds():
    """The headline number this module exists to produce."""
    assert pd.damage_per_hit(8) == pytest.approx(2749.5)
    assert af.damage_target(8) == pytest.approx(1683.0, abs=1.0)
    assert pd.gap_against_target(8) == pytest.approx(1.63, abs=0.01)


# --------------------------------------------------------------------------
# Refusals
# --------------------------------------------------------------------------

@pytest.mark.parametrize("tier", [0, -1, 9, 100])
def test_a_tier_outside_the_eight_is_refused(tier):
    with pytest.raises(ValueError, match="outside"):
        pd.damage_per_hit(tier)


def test_an_unknown_skill_slot_is_refused():
    with pytest.raises(ValueError, match="unknown skill slot"):
        pd.damage_per_hit(8, skill_slot="Sidearm")


def test_a_negative_affix_count_is_refused():
    with pytest.raises(ValueError, match="negative"):
        pd.damage_per_hit(8, flat_affixes=-1)


def test_an_unknown_weapon_is_refused():
    with pytest.raises(ValueError):
        pd.damage_per_hit(8, "Halberd")


def test_a_base_with_no_attack_damage_supplies_none():
    """The Shield is the only weapon base that grants no damage, and asking is
    not an error -- it is a real loadout."""
    assert pd.weapon_base_damage("Shield", af.MAX_GEAR_LEVEL) == 0.0
