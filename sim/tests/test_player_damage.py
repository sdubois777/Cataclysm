"""Tests for the composed player damage figure.

`affixes.damage_target()` says what a player NEEDS. `player_damage` says what a
player HAS. These check the second, and check that the two are compared honestly
rather than made to agree.

A HIT BELONGS TO A LOADOUT. The first version of this module took one weapon and
concluded that no weapon reaches the damage target. That was wrong: the target's
weapon term of about 90 is what a PAIR of one-handed weapons supplies. Issue #610
was filed on that mistake and closed. Several tests below exist to stop it
returning.
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
    and holds an Axe and a Sword. Those sum to 46 + 40 = 86. Six flat damage
    affixes at 18 give 108, so the base bracket is 194. Six increased damage
    affixes at 125% give 750%.

        194 x 8.5 = 1649
    """
    b = pd.breakdown(8)
    assert b.loadout == ("Axe", "Sword")
    assert b.weapon_damage == pytest.approx(86.0)
    assert b.flat_from_affixes == pytest.approx(108.0)
    assert b.base_bracket == pytest.approx(194.0)
    assert b.increased == pytest.approx(7.5)
    assert b.per_hit == pytest.approx(1649.0)
    assert pd.damage_per_hit(8) == pytest.approx(1649.0)


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
# Loadouts: what a character may hold, and what summing means
# --------------------------------------------------------------------------

def test_two_one_handed_weapons_sum_their_base_damage():
    """`docs/Cataclysm_GDD_v2.md` line 2378 states the rule and this example:
    "an Axe and a Sword give 86 against a Greatsword's stated 78"."""
    axe = pd.weapon_base_damage("Axe")
    sword = pd.weapon_base_damage("Sword")
    assert axe == pytest.approx(46.0)
    assert sword == pytest.approx(40.0)
    assert pd.weapon_base_damage(("Axe", "Sword")) == pytest.approx(86.0)


def test_a_two_handed_weapon_doubles_its_own_implicit_damage():
    """The Greatsword is stored at 78 and supplies 156, because it is two-handed."""
    stored = next(i.value for i in af.base_named("Greatsword").implicits
                  if i.stat == "attack_damage")
    assert stored == pytest.approx(78.0)
    assert pd.weapon_base_damage("Greatsword") == \
        pytest.approx(stored * af.TWO_HANDED_MULTIPLIER)


def test_a_single_one_handed_weapon_is_a_legal_loadout():
    """Stated by the project owner on 2026-08-15, against what the design
    document says. It deals less than a pair, which is the point of a pair."""
    assert pd.damage_per_hit(8, "Axe") == pytest.approx(1309.0)
    assert pd.damage_per_hit(8, "Axe") < pd.damage_per_hit(8, ("Axe", "Sword"))


def test_a_shield_is_an_offhand_that_adds_no_attack_damage():
    """Also the project owner, 2026-08-15. A shield is held, not swung, so a
    one-hander with a shield hits exactly as hard as that one-hander alone."""
    assert pd.weapon_base_damage(("Axe", pd.UNARMED_WEAPON)) == \
        pytest.approx(pd.weapon_base_damage("Axe"))
    assert pd.damage_per_hit(8, ("Axe", pd.UNARMED_WEAPON)) == \
        pytest.approx(pd.damage_per_hit(8, "Axe"))


def test_the_offhand_does_not_drag_the_attack_rate():
    """A shield is not swung, so it is left out of the average. Were it counted,
    an Axe with a Shield would swing at a different rate from an Axe alone."""
    assert pd.attack_rate(("Axe", pd.UNARMED_WEAPON)) == pytest.approx(pd.attack_rate("Axe"))


def test_the_attack_rate_of_a_pair_is_the_average_and_not_the_sum():
    """`docs/Cataclysm_GDD_v2.md` line 2401: "Attack speed is the average of the
    two weapons. Not the sum, and not the slower." That is what stops summed base
    damage becoming a strict advantage."""
    axe = af.base_named("Axe").attack_speed
    sword = af.base_named("Sword").attack_speed
    assert pd.attack_rate(("Axe", "Sword")) == pytest.approx((axe + sword) / 2.0)
    assert pd.attack_rate(("Axe", "Sword")) < axe + sword


@pytest.mark.parametrize("loadout,expected", [
    (("Greatsword", "Axe"), "hands"),
    (("Greatsword", "Greatsword"), "hands"),
    (("Axe", "Sword", "Dagger"), "two hands"),
    ((pd.UNARMED_WEAPON,), "grants no attack damage"),
    ((pd.UNARMED_WEAPON, pd.UNARMED_WEAPON), "grants no attack damage"),
])
def test_an_illegal_loadout_is_refused(loadout, expected):
    with pytest.raises(ValueError, match=expected):
        pd.damage_per_hit(8, loadout)


def test_which_hand_holds_the_shield_does_not_matter():
    """There is no offhand position. A Shield is one of the one-handed weapons,
    so the pair is unordered and both spellings are the same loadout."""
    assert pd.damage_per_hit(8, ("Axe", pd.UNARMED_WEAPON)) == \
        pytest.approx(pd.damage_per_hit(8, (pd.UNARMED_WEAPON, "Axe")))


def test_a_loadout_of_something_that_is_not_a_weapon_is_refused():
    with pytest.raises(ValueError, match="not something a hand can hold"):
        pd.damage_per_hit(8, ("Helm",))


# --------------------------------------------------------------------------
# What the comparison against the target actually says
# --------------------------------------------------------------------------

def test_the_reference_pair_lands_on_the_damage_target():
    """THE HEADLINE. The target describes a dual wielder, stated by the project
    owner on 2026-08-15, and the arithmetic agrees from two directions.

    `reference_weapon_base` says the weapon term must supply about 90. An Axe and
    a Sword supply 86 and land at 1,649 against a target of 1,683.
    """
    assert af.reference_weapon_base(8) == pytest.approx(90.0, abs=1.0)
    assert pd.weapon_base_damage(pd.REFERENCE_LOADOUT) == pytest.approx(86.0)
    assert pd.gap_against_target(8) == pytest.approx(1.0, abs=0.05)


def test_the_two_strongest_pairs_bracket_the_weapon_term_the_target_needs():
    """90.03 is not a pair sum, because pair sums are whole numbers. The two
    strongest legal pairs sit either side of it, which is why the module states a
    five per cent margin rather than claiming an exact fit."""
    required = af.reference_weapon_base(8)
    assert pd.weapon_base_damage(("Axe", "Axe")) == pytest.approx(92.0)
    assert pd.weapon_base_damage(("Axe", "Sword")) == pytest.approx(86.0)
    assert pd.weapon_base_damage(("Axe", "Sword")) < required
    assert pd.weapon_base_damage(("Axe", "Axe")) > required


def test_a_two_hander_beats_the_reference_pair_by_the_stated_multiplier():
    """`docs/Cataclysm_GDD_v2.md` line 2382: "a two-handed weapon deals about
    **1.33 times** the damage per hit". A two-hander exceeding the target is that
    advantage working, not a loadout breaking the target."""
    ratio = pd.damage_per_hit(8, ("Greatsword",)) / pd.damage_per_hit(8)
    assert ratio == pytest.approx(1.33, abs=0.05), (
        f"a Greatsword deals {ratio:.2f}x the reference pair and the design "
        "document states about 1.33x")


def test_every_two_hander_is_above_the_target_and_every_lone_one_hander_below_it():
    """Records the shape of the spread rather than a claim that it is wrong.

    This replaces a test that asserted no weapon reaches the target, which was
    true only because it measured single weapons against a target describing a
    pair. Issue #610.
    """
    for name in ("Greatsword", "Greataxe", "Spear", "Staff", "Warhammer"):
        assert pd.gap_against_target(8, (name,)) > 1.0
    for name in ("Axe", "Sword", "Dagger"):
        assert pd.gap_against_target(8, name) < 1.0


def test_the_gap_is_the_composed_hit_over_the_stated_target():
    for tier in (1, 4, 8):
        assert pd.gap_against_target(tier) == pytest.approx(
            pd.damage_per_hit(tier) / af.damage_target(tier))


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


def test_damage_per_second_is_the_hit_times_the_loadouts_own_rate():
    assert pd.damage_per_second(8) == \
        pytest.approx(pd.damage_per_hit(8) * pd.attack_rate(pd.REFERENCE_LOADOUT))


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
