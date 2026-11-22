"""The candidate rules for experience and loot by the Power Score gap, issue #2824 slice c.

`sim/analyse_power_gap_rewards.py` prints what each candidate does to the level curve and to drops per tier. These
tests hold the facts its proposal rests on, so a change to the scoring port, the experience curve or the drop rates
that moves one of them fails here rather than leaving the proposal describing a model that no longer exists.
"""

from __future__ import annotations

import contextlib
import io

import pytest

with contextlib.redirect_stdout(io.StringIO()):
    import analyse_power_gap_rewards as gaps


def test_the_reference_character_is_about_half_a_width_above_its_own_tiers_dungeon() -> None:
    # THE FINDING THE PROPOSAL TURNS ON: a band of a quarter width cuts the reward of a player exactly where the
    # design expects them at the end of a tier. Against the Dungeon Score, -0.48 to -0.50.
    for tier in gaps.TIERS:
        assert -0.55 < gaps.gap(tier, gaps.reference(tier)) < -0.45, tier


def test_the_rule_is_one_inside_the_band_and_reaches_its_cap_and_its_floor() -> None:
    rule = gaps.Rule("t", band=0.25, cap_up=0.25, ramp_up=0.5, floor=0.05, ramp_down=1.0, band_down=0.6)
    assert rule.multiplier(0.0) == 1.0 and rule.multiplier(0.25) == 1.0 and rule.multiplier(-0.6) == 1.0
    assert rule.multiplier(0.5) == pytest.approx(1.125)
    assert rule.multiplier(0.75) == pytest.approx(1.25) and rule.multiplier(5.0) == pytest.approx(1.25)
    assert rule.multiplier(-1.1) == pytest.approx(0.525)
    assert rule.multiplier(-1.6) == pytest.approx(0.05) and rule.multiplier(-9.0) == pytest.approx(0.05)


def test_a_band_that_holds_the_reference_leaves_the_level_curve_where_it_is() -> None:
    wide = next(rule for rule in gaps.RULES if rule.name.startswith("C:"))
    paid = {tier: gaps.curve.PER_DUNGEON[tier] * gaps.campaign_multiplier(wide, tier) for tier in gaps.TIERS}
    assert gaps.curve.whole_levels_at_tier_ends(per_dungeon=paid) == gaps.curve.whole_levels_at_tier_ends()


def test_the_plans_quarter_width_band_costs_the_climb_a_level() -> None:
    plan = next(rule for rule in gaps.RULES if rule.name.startswith("A:"))
    paid = {tier: gaps.curve.PER_DUNGEON[tier] * gaps.campaign_multiplier(plan, tier) for tier in gaps.TIERS}
    assert gaps.curve.whole_levels_at_tier_ends(per_dungeon=paid)[-1] == 99


def test_rule_d_never_cuts_a_player_at_the_reference_in_any_dungeon_their_tier_spawns() -> None:
    # THE COORDINATING SESSION'S QUESTION OF 2026-11-22: a dungeon's timer forces the player to clear shallow
    # dungeons of their own tier too, so the cut must not reach them there. The lowest Dungeon Score a tier spawns
    # is 0.48 to 0.49 of a width below the reference; rule D's cut begins at 0.6.
    # THE RULED RULE, for experience and for loot.
    for tier in gaps.TIERS:
        low, high = gaps.spawn_extremes(tier)
        width = gaps.scoring.tier_width(tier)
        for loot in (False, True):
            assert gaps.RULED.multiplier((low.score - gaps.reference(tier)) / width, loot=loot) == 1.0, tier
            assert gaps.RULED.multiplier((high.score - gaps.reference(tier)) / width, loot=loot) == 1.0, tier


def test_the_ruled_rule_has_two_floors_and_one_bonus() -> None:
    # Rule D, ruled by the coordinating session on 2026-11-22, with the owner's loot floor of the same day, verbatim
    # "25% loot floor (Recommended)": experience falls to 5%, loot quantity and the gold multiplier to 25%, over the
    # same width; the bonus above the band is the same for both.
    ruled = gaps.RULED
    assert (ruled.band, ruled.band_down, ruled.cap_up, ruled.ramp_up, ruled.ramp_down) == (0.25, 0.6, 0.25, 0.5, 1.0)
    assert ruled.multiplier(-5.0) == pytest.approx(0.05)
    assert ruled.multiplier(-5.0, loot=True) == pytest.approx(0.25)
    assert ruled.multiplier(-1.1, loot=True) == pytest.approx(0.625)
    assert ruled.multiplier(5.0) == ruled.multiplier(5.0, loot=True) == pytest.approx(1.25)
