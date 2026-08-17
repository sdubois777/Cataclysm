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
from cataclysm_sim import reference_build as rb
from cataclysm_sim.character import SKILL_SLOTS


# --------------------------------------------------------------------------
# The pipeline itself
# --------------------------------------------------------------------------

def test_the_composed_hit_is_the_three_buckets_multiplied_in_order():
    """Worked by hand from the constants, so this fails if any of them moves.

    At difficulty tier 8 the reference character carries T7 affixes on +10 gear
    and holds an Axe and a Sword. Those sum to 46 + 40 = 86. Six flat damage
    affixes at 22 give 132, so the base bracket is 218. Six increased damage
    affixes at 125% give 750%.

        218 x 8.5 = 1853

    The flat damage affix was 18 until issue #511, which raised the damage target
    by 10.5% by applying the enemy's mitigation to it. 18 gave 1,649 per hit.
    """
    b = pd.breakdown(8)
    assert b.loadout == ("Axe", "Sword")
    assert b.weapon_damage == pytest.approx(86.0)
    assert b.flat_from_affixes == pytest.approx(132.0)
    assert b.base_bracket == pytest.approx(218.0)
    assert b.increased == pytest.approx(7.5)
    assert b.per_hit == pytest.approx(1853.0)
    assert pd.damage_per_hit(8) == pytest.approx(1853.0)


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
    assert pd.damage_per_hit(8, "Axe") == pytest.approx(1513.0)
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


# --------------------------------------------------------------------------
# Which sub-type a hit carries. Issue #639.
# --------------------------------------------------------------------------

def test_a_single_weapon_carries_its_own_sub_type():
    assert pd.subtype_of("Axe") == "Slashing"
    assert pd.subtype_of("Dagger") == "Piercing"
    assert pd.subtype_of("Wand") == "Magic"
    assert pd.subtype_of("Fist") == "Blunt"
    assert pd.subtype_of("Greatsword") == "Slashing"


def test_a_matched_pair_carries_that_sub_type_and_a_mixed_one_carries_none():
    """THE RULE, set by the project owner on 2026-08-16. Every weapon actually
    swung has to agree, because this model blends two weapons into ONE swing --
    base damage summed, attack speed averaged -- and a single swing cannot be
    both Slashing and Magic.

    Mixing sub-types is how a player carries more damage types at once, which
    the design document calls the primary route to multiclassing. Giving a mixed
    pair both bonuses would make mixing strictly better and matching pointless;
    costing it the sub-type makes the two a real trade.
    """
    assert pd.subtype_of(("Axe", "Sword")) == "Slashing"
    assert pd.subtype_of(("Dagger", "Crossbow")) == "Piercing"
    assert pd.subtype_of(("Axe", "Wand")) == pd.NO_SUBTYPE


def test_the_pair_is_still_unordered():
    """No primary hand. The ruling of 2026-08-15 that "there is no offhand
    position" stands, and a rule that read one weapon rather than both would
    have reversed it."""
    assert pd.subtype_of(("Axe", "Wand")) == pd.subtype_of(("Wand", "Axe"))
    assert pd.subtype_of(("Axe", "Sword")) == pd.subtype_of(("Sword", "Axe"))


def test_a_shield_does_not_decide_the_sub_type():
    """A Shield's own sub-type is Blunt, so counting it would take the slashing
    bonus away from a sword-and-board character for holding something they never
    swing. It falls out of `armed_weapons_in` rather than a rule about shields:
    a Shield grants no attack damage, so it is not swung."""
    assert af.base_named(pd.UNARMED_WEAPON).sub_type == "Blunt"
    assert pd.subtype_of(("Axe", pd.UNARMED_WEAPON)) == "Slashing"
    assert pd.subtype_of((pd.UNARMED_WEAPON, "Axe")) == "Slashing"
    assert pd.subtype_of(("Axe", pd.UNARMED_WEAPON)) == pd.subtype_of("Axe")


def test_every_weapon_sub_type_is_one_the_damage_model_knows():
    """A sub-type the damage model does not know would be refused by
    `damage.Attacker`, which raises on an unknown one. This is what stops the
    Item Bases sheet and the mitigation order drifting apart."""
    from cataclysm_sim import damage as dm

    for base in af.ITEM_BASES:
        if isinstance(base, af.WeaponBase):
            assert base.sub_type in dm.WEAPON_SUBTYPES, base.name
    assert pd.NO_SUBTYPE in dm.WEAPON_SUBTYPES


def test_a_loadout_of_something_that_is_not_a_weapon_is_refused():
    with pytest.raises(ValueError, match="not something a hand can hold"):
        pd.damage_per_hit(8, ("Helm",))


# --------------------------------------------------------------------------
# What the comparison against the target actually says
# --------------------------------------------------------------------------

def test_the_reference_pair_lands_on_the_damage_target():
    """THE HEADLINE. The target describes a dual wielder, stated by the project
    owner on 2026-08-15, and the arithmetic agrees from two directions.

    `reference_weapon_base` says the weapon term must supply about 87. An Axe and
    a Sword supply 86 and land at 1,853 against a target of 1,860.

    IT USED TO BE A LOOSER FIT. Before issue #511 the target applied no enemy
    mitigation and asked for 1,683, the weapon term was 90.03, and the pair's 86
    sat 4.5% under it. Correcting the target moved both, and re-deriving the flat
    damage affix against it brought the two within one per cent.
    """
    assert af.reference_weapon_base(8) == pytest.approx(87.0, abs=1.0)
    assert pd.weapon_base_damage(pd.REFERENCE_LOADOUT) == pytest.approx(86.0)
    assert pd.gap_against_target(8) == pytest.approx(1.0, abs=0.05)


def test_the_two_strongest_pairs_bracket_the_weapon_term_the_target_needs():
    """86.86 is not a pair sum, because pair sums are whole numbers. The two
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
    """The reference build's own figures, read off `reference_build`.

    THEY ARE NOW ACTUALLY READ OFF IT. This test said that and wrote 10.0 and
    258.0 out by hand, which happened to be right and would have gone on passing
    if the reference character's gear changed underneath it. Issue #663.
    """
    hero = rb.character()
    chance = hero.stat("crit_chance")
    multiplier = hero.stat("crit_multiplier")

    average = pd.average_damage_per_hit(8, crit_chance=chance,
                                        crit_multiplier=multiplier)
    assert average > pd.damage_per_hit(8)

    expected = 1.0 - chance / 100.0 + (chance / 100.0) * (multiplier / 100.0)
    assert average == pytest.approx(pd.damage_per_hit(8) * expected)


def test_the_reference_character_gains_about_a_sixth_from_criticals():
    """The figure nothing else in the project states, named so it is visible.

    It is what a player actually deals against the target they were fitted to,
    and it became worth having on 2026-08-17 when issue #649 made the engine roll
    critical strikes for the player for the first time.

    A RANGE RATHER THAN A FIGURE, because the reference character's gear is
    retuned and this should not fail every time it is. What it refuses is the
    two states that would mean something is broken: no gain at all, which is what
    a critical strike chance of zero looks like, and a gain so large that
    critical strikes rather than gear have become the player's damage.
    """
    hero = rb.character()
    average = pd.average_damage_per_hit(
        8, crit_chance=hero.stat("crit_chance"),
        crit_multiplier=hero.stat("crit_multiplier"))

    gain = average / pd.damage_per_hit(8) - 1.0
    assert 0.05 < gain < 0.50, (
        f"critical strikes are worth {gain:.1%} to the reference character. "
        "At or below 5% something has stopped granting critical strike chance; "
        "at or above 50% they have become the build rather than a bonus on it.")


def test_the_target_comparison_deliberately_uses_the_non_critical_hit():
    """The asymmetry issue #663 asked about, asserted so it cannot drift.

    The design states its damage target in NON-CRITICAL hits -- "should take 2
    non-critical hits to kill" -- so `gap_against_target` compares
    `damage_per_hit` against it, and an import-time check refuses a reference
    loadout more than 5% away. Averaging criticals into that comparison would
    overshoot a target stated in the other units.

    The enemy side averages instead, and that is right for the opposite reason:
    an enemy's damage is fitted to how long a character survives, which is a
    question about many hits rather than about one.
    """
    gap = pd.gap_against_target(8)
    assert gap == pytest.approx(pd.damage_per_hit(8) / af.damage_target(8))

    hero = rb.character()
    averaged = pd.average_damage_per_hit(
        8, crit_chance=hero.stat("crit_chance"),
        crit_multiplier=hero.stat("crit_multiplier"))

    assert averaged / af.damage_target(8) > gap, (
        "averaging criticals into the target comparison would read higher than "
        "the non-critical comparison, which is the overshoot this asymmetry "
        "exists to avoid")


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
