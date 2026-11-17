"""Tests for the player's Power Score model."""

from __future__ import annotations

import math

import pytest

from cataclysm_sim import player_power as pp
from cataclysm_sim import scoring


# --------------------------------------------------------------------------
# Calibration against the fixed anchors
# --------------------------------------------------------------------------

def test_tier_8_reference_character_lands_exactly_on_the_anchor():
    """The end of the game is the one anchor that has to be exact."""
    score = pp.power_score(pp.reference_character(8))
    assert score == scoring.PLAYER_MAX_SCORES[8] == 6327


def test_tier_1_reference_character_is_within_one_point_of_the_anchor():
    """Tier 1 is pinned in the arithmetic but loses a point to rounding.

    The continuous curve wants level 12.5 and 5.625 filled sockets. A character
    has whole levels and whole gems, so the reference character is level 12 with
    6 gems and scores 384 rather than 385.
    """
    score = pp.power_score(pp.reference_character(1))
    assert abs(score - scoring.PLAYER_MAX_SCORES[1]) <= 1


def test_every_tier_lands_within_six_percent_of_its_anchor():
    for tier, predicted, anchor, pct in pp.anchor_report():
        assert abs(pct) < 6.0, (
            f"tier {tier} predicted {predicted} against anchor {anchor}, "
            f"{pct:+.1f}% off")


def test_the_model_now_hits_every_anchor_above_tier_one_exactly():
    """This test used to record a defect. It now records that the defect is gone.

    Until 2026-08-05 the anchors were not smooth: tier 5 was 1107 points wide
    where the surrounding trend was about 790, and tier 6 was NARROWER than tier
    5. A smoothly progressing character cannot pass through that kink, so this
    file asserted the resulting residual signature instead -- largest positive
    error at tier 4, largest negative at tier 5. That signature was one of the two
    independent pieces of evidence on issue #7 that the anchors were wrong.

    The anchors were then reset to what this model predicts, in DungeonSimulator
    commit 6c9be8b. Tiers 2 to 7 moved; tiers 1 and 8 deliberately did not,
    because `_curve_coefficients` pins the curve through those two and nothing
    else, so moving either would re-pin the curve and shift every prediction.

    What is left is a single residual at tier 1, where the reference character
    scores 384 against an anchor of 385. Every other tier is exact.
    """
    errors = {tier: pct for tier, _, _, pct in pp.anchor_report()}
    for tier in range(2, 9):
        assert errors[tier] == pytest.approx(0.0, abs=1e-9), (
            f"tier {tier} is {errors[tier]:+.2f}% off its anchor. The anchors "
            "were set to this model's own predictions on 2026-08-05, so every "
            "tier above 1 should be exact. Issue #7.")
    assert abs(errors[1]) < 1.0, (
        f"tier 1 is {errors[1]:+.2f}% off. The reference character not landing "
        "exactly on the curve at the bottom end is expected; a whole percent "
        "is not.")


def test_the_tier_widths_climb_at_every_tier():
    """The defect issue #7 was filed for, asserted directly rather than through
    its residual signature. Tier width multiplies every weighted term in
    `scoring.py`, so a tier narrower than the one below it compresses that tier's
    whole rarity spread."""
    widths = [scoring.tier_width(t) for t in range(1, 9)]
    for lower, higher in zip(widths, widths[1:], strict=False):
        assert higher > lower, (
            f"tier widths do not climb: {[round(w) for w in widths]}. A tier "
            "narrower than the one below it means its Boss gains less power "
            "over its Common enemies than the tier below does. Issue #7.")


def test_reference_character_scores_rise_with_tier():
    scores = [pp.power_score(pp.reference_character(t)) for t in range(1, 9)]
    assert scores == sorted(scores)
    assert len(set(scores)) == 8


# --------------------------------------------------------------------------
# The reference character matches what the design fixes
# --------------------------------------------------------------------------

def test_reference_character_reaches_the_documented_maximums_at_tier_8():
    c = pp.reference_character(8)
    assert c.level == pp.MAX_LEVEL == 100
    assert len(c.gear) == pp.GEAR_PIECES == 18
    assert all(p.rarity == pp.MAX_RARITY for p in c.gear)
    assert all(p.upgrade == pp.MAX_UPGRADE for p in c.gear)
    assert len(c.gems) == pp.TOTAL_SOCKETS == 45
    assert all(g == pp.MAX_RARITY for g in c.gems)
    assert c.resistances == tuple([pp.RESISTANCE_CAP] * 8)


@pytest.mark.parametrize("tier, needed_upgrade", [
    (5, 4),   # Legendary requires gear level 4+
    (6, 6),   # Mythical requires gear level 6+
    (7, 8),   # Ascendant requires gear level 8+
    (8, 10),  # Cataclysmic requires gear level 10
])
def test_reference_character_clears_the_rarity_gates_in_the_design(
        tier, needed_upgrade):
    """`docs/Cataclysm_GDD_v2.md` gates the top four rarities behind a minimum
    gear upgrade level. The reference character wears rarity N at tier N, so its
    upgrade level has to be high enough for that rarity to legally exist."""
    c = pp.reference_character(tier)
    assert c.gear[0].rarity == tier
    assert c.gear[0].upgrade >= needed_upgrade


def test_reference_tiers_outside_one_to_eight_are_rejected():
    for bad in (0, 9, -1):
        with pytest.raises(ValueError):
            pp.reference_character(bad)


# --------------------------------------------------------------------------
# The formula responds to each input
# --------------------------------------------------------------------------

def test_every_input_raises_the_score_on_its_own():
    """A stat listed as a Power Score input must actually change the score."""
    base = pp.Character(level=50,
                        gear=(pp.GearPiece(4, 5),),
                        gems=(4,),
                        resistances=tuple([30.0] * 8))
    baseline = pp.power_score(base)

    more_level = pp.power_score(pp.Character(
        level=51, gear=base.gear, gems=base.gems,
        resistances=base.resistances))
    better_rarity = pp.power_score(pp.Character(
        level=50, gear=(pp.GearPiece(5, 5),), gems=base.gems,
        resistances=base.resistances))
    better_upgrade = pp.power_score(pp.Character(
        level=50, gear=(pp.GearPiece(4, 6),), gems=base.gems,
        resistances=base.resistances))
    better_gem = pp.power_score(pp.Character(
        level=50, gear=base.gear, gems=(5,), resistances=base.resistances))
    more_sockets = pp.power_score(pp.Character(
        level=50, gear=base.gear, gems=(4, 4), resistances=base.resistances))
    more_resist = pp.power_score(pp.Character(
        level=50, gear=base.gear, gems=base.gems,
        resistances=tuple([31.0] * 8)))

    for label, value in [("level", more_level), ("gear rarity", better_rarity),
                         ("gear upgrade", better_upgrade), ("gem rarity", better_gem),
                         ("socket count", more_sockets), ("resistance", more_resist)]:
        assert value > baseline, f"{label} did not raise the score"


def test_upgrade_level_multiplies_rarity_rather_than_adding_to_it():
    """A +10 on a Cataclysmic piece must be worth far more than a +10 on an
    Everyday piece. If upgrades were additive the two gains would be equal.

    Measured through `gear_term`, not recomputed from the weights: a test that
    redoes the module's arithmetic cannot detect the module changing. This is
    asserted against the full eight-fold ratio rather than a bare inequality,
    because integer rounding alone can make an additive model look like it
    passes a `gain(8) > gain(1)` check by one point.
    """
    def gain(rarity):
        at_zero = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(rarity, 0),)))
        at_ten = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(rarity, 10),)))
        return at_ten - at_zero

    # Under multiplication the gain scales with rarity, so Cataclysmic gains
    # exactly eight times what Everyday gains. Under addition it would be 1.0x.
    assert gain(8) / gain(1) == pytest.approx(8.0)

    # And a fully upgraded piece is worth the stated multiple of an unupgraded
    # one, measured the same way.
    f = pp.WEIGHTS["upgrade_factor"]
    bare = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(8, 0),)))
    upgraded = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(8, 10),)))
    assert upgraded / bare == pytest.approx(1 + 10 * f)


def test_an_empty_character_scores_only_its_level():
    c = pp.Character(level=100)
    assert pp.power_score(c) == math.floor(pp.WEIGHTS["level"] * 100 + 0.5)


def test_resistance_above_the_cap_adds_nothing():
    """Over-capping is legal in the design but is headroom against penetration,
    not power, so Power Score stops counting at 70%."""
    capped = pp.Character(level=1, resistances=tuple([70.0] * 8))
    over = pp.Character(level=1, resistances=tuple([200.0] * 8))
    assert pp.power_score(capped) == pp.power_score(over)


def test_dual_wielding_is_not_worth_more_than_a_two_handed_weapon():
    """Both give 6 sockets by design, and both count as one gear piece here, so
    the choice must not move Power Score."""
    assert pp.GEAR_PIECES == 7 + 8 + 1 + 1 + 1  # armour, rings, necklace, relic, weapon


def test_socket_total_matches_the_design_document():
    armour = 2 + 6 + 2 + 2 + 4 + 2 + 4   # head chest shoulders gloves pants boots belt
    jewellery = 8 * 1 + 1 + 4            # rings, necklace, relic
    weapon = 6
    potions = 4
    assert armour + jewellery + weapon + potions == pp.TOTAL_SOCKETS == 45


# --------------------------------------------------------------------------
# Input validation
# --------------------------------------------------------------------------

@pytest.mark.parametrize("rarity", [0, 9, -1, 100])
def test_gear_rarity_outside_one_to_eight_is_rejected(rarity):
    with pytest.raises(ValueError):
        pp.GearPiece(rarity=rarity, upgrade=0)


@pytest.mark.parametrize("upgrade", [-1, 11, 50])
def test_gear_upgrade_outside_zero_to_ten_is_rejected(upgrade):
    with pytest.raises(ValueError):
        pp.GearPiece(rarity=1, upgrade=upgrade)


@pytest.mark.parametrize("level", [0, 101, -5])
def test_level_outside_one_to_one_hundred_is_rejected(level):
    with pytest.raises(ValueError):
        pp.Character(level=level)


def test_more_than_eighteen_gear_pieces_is_rejected():
    with pytest.raises(ValueError):
        pp.Character(level=1, gear=tuple([pp.GearPiece(1, 0)] * 19))


def test_more_than_forty_five_gems_is_rejected():
    with pytest.raises(ValueError):
        pp.Character(level=1, gems=tuple([1] * 46))


def test_a_resistance_count_other_than_eight_is_rejected():
    with pytest.raises(ValueError):
        pp.Character(level=1, resistances=(10.0, 10.0))


def test_gem_rarity_outside_one_to_eight_is_rejected():
    with pytest.raises(ValueError):
        pp.Character(level=1, gems=(0,))
    with pytest.raises(ValueError):
        pp.Character(level=1, gems=(9,))


# --------------------------------------------------------------------------
# The derivation refuses impossible share allocations
# --------------------------------------------------------------------------

def test_weight_derivation_rejects_a_gem_share_that_leaves_no_curvature(
        monkeypatch):
    """If gems are given so large a share that they alone over-supply the
    curve's quadratic term, gear would need a negative weight. That must raise
    rather than silently produce a nonsense model."""
    monkeypatch.setattr(pp, "SHARE_GEMS", 0.90)
    monkeypatch.setattr(pp, "SHARE_GEAR", 0.05)
    monkeypatch.setattr(pp, "SHARE_LEVEL", 0.03)
    monkeypatch.setattr(pp, "SHARE_RESISTANCES", 0.02)
    with pytest.raises(ValueError, match="no curvature for gear"):
        pp._derive_weights()


def test_weight_derivation_rejects_a_gear_share_too_small_to_carry_the_curve(
        monkeypatch):
    monkeypatch.setattr(pp, "SHARE_GEMS", 0.10)
    monkeypatch.setattr(pp, "SHARE_GEAR", 0.10)
    monkeypatch.setattr(pp, "SHARE_LEVEL", 0.40)
    monkeypatch.setattr(pp, "SHARE_RESISTANCES", 0.40)
    with pytest.raises(ValueError, match="too small to carry"):
        pp._derive_weights()


def test_the_shares_in_the_module_sum_to_one():
    total = (pp.SHARE_LEVEL + pp.SHARE_GEAR + pp.SHARE_GEMS
             + pp.SHARE_RESISTANCES)
    assert total == pytest.approx(1.0)


# --------------------------------------------------------------------------
# Power Score does not read anything issue #77 is blocked on
# --------------------------------------------------------------------------

def test_power_score_needs_no_class_base_stats():
    """The point of issue #26 being separable from #77: the formula's inputs
    contain no health, mana or energy shield, so it can be defined and checked
    before any class base values exist."""
    fields = set(pp.Character.__dataclass_fields__)
    assert fields == {"level", "gear", "gems", "resistances"}
    for forbidden in ("health", "mana", "shield", "vitality"):
        assert not any(forbidden in f for f in fields)
