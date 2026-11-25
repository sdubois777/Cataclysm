"""Tests for the player's Power Score model."""

from __future__ import annotations

import math

import pytest

from cataclysm_sim import player_power as pp
from cataclysm_sim import scoring


# --------------------------------------------------------------------------
# The weights: five plain numbers, ruled by the owner on 2026-11-24 (#2853)
# --------------------------------------------------------------------------

def test_the_weights_are_the_plain_numbers_the_owner_ruled():
    """"Adopt, and look at drop supply (Recommended)", the owner, 2026-11-24.

    Written out rather than read back from the module, because the ruling is the
    numbers: 10 a level, 2 a rarity step of a gear piece with an upgrade factor
    of 1, 5 a gem rarity step and 1 a resistance percent.
    """
    assert pp.WEIGHTS == {"level": 10.0, "gear": 2.0, "upgrade_factor": 1.0,
                          "gem": 5.0, "resistance": 1.0}
    assert (pp.LEVEL_WEIGHT, pp.GEAR_WEIGHT, pp.UPGRADE_FACTOR, pp.GEM_WEIGHT,
            pp.RESISTANCE_WEIGHT) == (10.0, 2.0, 1.0, 5.0, 1.0)


def test_every_weight_is_a_number_on_the_owners_list():
    """The owner, 2026-11-24: "It would make sense as 1, 2, 5, 10, 25, 50, 100,
    but 6.3 makes no sense at all." A weight a player cannot read is the fault
    this change fixes, so a future re-tune that slips a 6.3 back in fails here."""
    plain = {1.0, 2.0, 5.0, 10.0, 25.0, 50.0, 100.0}
    for name, value in pp.WEIGHTS.items():
        assert value in plain, f"{name} is {value}, not one of {sorted(plain)}"


def test_a_level_six_character_scores_sixty_from_its_level():
    """The owner's screen of 2026-11-24 read 38 for level 6 (6.327 a level),
    and asked why. Under the ruled weights it is 60."""
    c = pp.Character(level=6)
    assert pp.level_term(c) == 60.0
    assert pp.power_score(c) == 60


@pytest.mark.parametrize("rarity, upgrade, points", [
    (1, 0, 2),     # Everyday +0
    (4, 0, 8),     # Masterful +0, a tier 1 drop: 2 x its rarity
    (1, 3, 8),     # Everyday +3, the tier 1 reference's piece
    (5, 4, 50),    # Legendary +4, at its gate
    (6, 6, 84),    # Mythical +6
    (8, 10, 176),  # Cataclysmic +10
])
def test_a_piece_scores_two_times_its_rarity_times_one_plus_its_upgrade(
        rarity, upgrade, points):
    """The proposal's table of pieces, through `gear_term` rather than the
    weights, so a change to how the term is built is seen."""
    c = pp.Character(level=1, gear=(pp.GearPiece(rarity, upgrade),))
    assert pp.gear_term(c) == pytest.approx(points)


def test_a_fresh_masterful_piece_counts_the_same_as_the_tier_1_references():
    """The point the recommendation was set to, and not past: a Masterful +0
    drop and the tier 1 reference's Everyday +3 are both 8, so wearing the
    rarer piece never lowers the score. The alternative the owner did not choose
    (1 a rarity step, 2 an upgrade level each) put the Masterful piece below."""
    masterful = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(4, 0),)))
    reference = pp.gear_term(pp.Character(
        1, gear=(pp.reference_character(1).gear[0],)))
    assert pp.reference_character(1).gear[0] == pp.GearPiece(1, 3)
    assert masterful == reference == 8.0


def test_a_gem_and_a_resistance_percent_are_five_and_one():
    assert pp.gem_term(pp.Character(1, gems=(3,))) == 15.0
    assert pp.resistance_term(pp.Character(
        1, resistances=(10.0, 0, 0, 0, 0, 0, 0, 0))) == 10.0


# --------------------------------------------------------------------------
# Where the reference character lands against the fixed anchors
# --------------------------------------------------------------------------

#: What the reference character scores at each tier under the ruled weights:
#: the proposal's "Recommended" column (section 1a), measured on branch
#: proposal/power-score-shares-2853 before the owner ruled.
PROPOSAL_REFERENCE_SCORES = {1: 364, 2: 860, 3: 1493, 4: 2228,
                             5: 3110, 6: 4134, 7: 5255, 8: 6528}


def test_the_reference_character_scores_the_proposals_figures():
    for tier, expected in PROPOSAL_REFERENCE_SCORES.items():
        assert pp.power_score(pp.reference_character(tier)) == expected, tier


def test_every_tier_lands_within_the_proposals_band_of_its_anchor():
    """From -5.5% (tier 1, 364 against 385) to +3.2% (tier 8, 6,528 against
    6,327), the band the owner was shown and ruled on. The anchors are
    `scoring.PLAYER_MAX_SCORES`, the DungeonSimulator ceilings, and did not
    move; the weights are chosen rather than fitted, so the character lands near
    them rather than on them."""
    for tier, predicted, anchor, pct in pp.anchor_report():
        assert -5.5 <= round(pct, 1) <= 3.2, (
            f"tier {tier} predicted {predicted} against anchor {anchor}, "
            f"{pct:+.1f}% off")
        assert abs(pct) < 6.0
    errors = {tier: pct for tier, _, _, pct in pp.anchor_report()}
    assert round(errors[1], 1) == -5.5
    assert round(errors[8], 1) == 3.2


def test_the_anchors_are_still_the_dungeon_simulator_ceilings():
    """The ruling changed the weights and not the ceilings, which are
    DungeonSimulator's and are never hand-edited here."""
    assert [scoring.PLAYER_MAX_SCORES[t] for t in range(1, 9)] == [
        385, 883, 1508, 2225, 3078, 4057, 5120, 6327]


def test_the_shares_fall_out_at_fifteen_forty_nine_twenty_eight_and_nine():
    """The shares are an outcome now. Until 2026-11-24 they were the input,
    10/50/30/10, and the weights were derived from them."""
    shares = pp.shares_of(pp.reference_character(8))
    assert {name: round(100 * share) for name, share in shares.items()} == {
        "level": 15, "gear": 49, "gems": 28, "resistances": 9}
    assert sum(shares.values()) == pytest.approx(1.0)


def test_the_finished_tier_1_character_draws_a_third_from_level():
    """"maybe put more points on your level instead of gear", the owner,
    2026-11-24. The proposal's figures for the finished tier 1 character: 33 /
    40 / 8 / 19, against 20 / 51 / 8 / 21 under the old weights."""
    shares = pp.shares_of(pp.reference_character(1))
    assert {name: round(100 * share) for name, share in shares.items()} == {
        "level": 33, "gear": 40, "gems": 8, "resistances": 19}


def test_a_character_that_scores_nothing_has_no_shares():
    """Not reachable through `Character`, whose level is at least 1, so the
    guard is exercised with a stand-in at level 0 that wears nothing."""
    with pytest.raises(ValueError):
        pp.shares_of(type("Nothing", (), {"level": 0, "gear": (), "gems": (),
                                          "resistances": ()})())


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
    # one, measured the same way: 11 times since 2026-11-24, 3.52 before.
    f = pp.WEIGHTS["upgrade_factor"]
    bare = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(8, 0),)))
    upgraded = pp.gear_term(pp.Character(1, gear=(pp.GearPiece(8, 10),)))
    assert upgraded / bare == pytest.approx(1 + 10 * f)
    assert upgraded / bare == pytest.approx(11.0)


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
# The shares are no longer an input
# --------------------------------------------------------------------------

def test_the_shares_and_their_derivation_are_gone():
    """Until 2026-11-24 four SHARE_ constants were solved for the weights. A
    copy left behind would read as still deciding something."""
    for gone in ("SHARE_LEVEL", "SHARE_GEAR", "SHARE_GEMS", "SHARE_RESISTANCES",
                 "_derive_weights", "_curve_coefficients"):
        assert not hasattr(pp, gone), gone


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
