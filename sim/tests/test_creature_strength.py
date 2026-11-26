"""What the game builds a creature with: the design's enemy score, eased three ways.

ISSUE #1569. The project owner, 2026-09-11: creature health and damage come from
the design's enemy score on each floor. Ruled 2026-10-30, and all three live in
`enemy_stats.strength_score` / `strength_on_floor` and the game's copy, never in
`scoring.py`:

1. the tier relief of #1425 is subtracted from the score;
2. the score is never below the same creature on floor 1 of a 12-floor dungeon
   of the same kind;
3. damage is multiplied by a figure for the tier: at tier 1 by the ring the
   dungeon was rolled on (0.05 on an Outpost, 0.10 on a Bulwark, 0.12 on a
   Sanctuary, 0.07 on the Pillar, since issue #2865; the ring since issue #2749;
   it was 0.20 until then),
   then 0.314 at tier 2 rising evenly to 1.00 at tier 8 on every ring.

`stats_on_floor` stays the design's figure, because `affixes.py` anchors the
player's offence on it.
"""
from __future__ import annotations

import pytest

from cataclysm_sim import enemy_stats as es
from cataclysm_sim import scoring
from cataclysm_sim.config import TuningConfig


def test_the_damage_scale_is_a_twentieth_at_tier_one_and_tiers_two_to_eight_are_on_the_line_from_a_fifth():
    """Issue #2749, ruled 2026-11-19: tier 1 is 0.05, refitted to a character that starts at level 1; tiers 2 to 8
    stay where they were, on the line rising evenly from 0.20 at tier 1 to one at tier 8. Typed rather than read
    from the constants, so a moved constant fails here."""
    assert es.damage_scale(1) == pytest.approx(0.05)
    assert [es.damage_scale(t) for t in range(2, 9)] == pytest.approx(
        [0.20 + 0.80 * (t - 1) / 7 for t in range(2, 9)])
    assert es.damage_scale(2) == pytest.approx(0.3142857)
    assert es.damage_scale(8) == pytest.approx(1.0)
    # Held at the ends, as the game's DamageScale clamps.
    assert es.damage_scale(0) == es.damage_scale(1)
    assert es.damage_scale(9) == es.damage_scale(8)


def test_at_tier_one_the_damage_scale_rises_by_the_ring_the_dungeon_was_rolled_on():
    """Issue #2749's ring slice. The project owner, 2026-11-19: "Rise by city ring" -- Outpost dungeons stay at 0.05,
    Bulwark and Sanctuary dungeons hit harder. The coordinating session ruled the figures the same day, 0.11, 0.13 and
    0.10 on the Pillar, and on 2026-11-24 lowered them by the same rule to 0.10, 0.12 and 0.07 after the surge rulings
    of issue #2865, provisionally until issue #2853. Typed, so a moved constant fails here."""
    assert es.damage_scale(1, "Outpost") == pytest.approx(0.05)
    assert es.damage_scale(1, "Bulwark") == pytest.approx(0.10)
    assert es.damage_scale(1, "Sanctuary") == pytest.approx(0.12)
    assert es.damage_scale(1, "Pillar") == pytest.approx(0.07)
    # With no ring named, an Outpost's: the sandbox and a walk with no empire dungeon behind it.
    assert es.damage_scale(1) == es.damage_scale(1, "Outpost")
    # A ring may be passed as the simulation's own `CityTier`.
    from cataclysm_sim.config import CityTier
    assert es.damage_scale(1, CityTier.SANCTUARY) == es.damage_scale(1, "Sanctuary")


def test_tiers_two_to_eight_are_the_same_on_every_ring():
    """Ruled 2026-11-19: tiers 2 to 8 do not rise by ring in this slice."""
    for tier in range(2, 9):
        assert {es.damage_scale(tier, ring) for ring in ("Outpost", "Bulwark", "Sanctuary", "Pillar")} == {
            es.damage_scale(tier)}, tier


def test_an_unknown_ring_is_refused_rather_than_read_as_an_outpost():
    with pytest.raises(ValueError):
        es.damage_scale(1, "Capital")


def test_the_ring_moves_a_creatures_damage_and_nothing_else():
    outpost = es.strength_on_floor("Elite", 1, "Basic", "None", 12, 6, kind="Brute")
    bulwark = es.strength_on_floor("Elite", 1, "Basic", "None", 12, 6, kind="Brute", ring="Bulwark")
    assert bulwark.health == outpost.health
    assert bulwark.armor == outpost.armor
    assert bulwark.score == outpost.score
    assert bulwark.damage_per_hit == pytest.approx(outpost.damage_per_hit * 0.10 / 0.05)


def test_the_relief_is_taken_off_the_score():
    """THE CASE: at tier 4, the middle floor of a deep dungeon, where the minimum
    does not reach, the strength is the design's score less 0.45 tier widths."""
    design = scoring.enemy_scores(4, "Basic", "None", 50, 25)["Common"]
    relief = TuningConfig().content_relief(4) * scoring.tier_width(4)
    assert relief > 0
    assert es.strength_score("Common", 4, "Basic", "None", 50, 25) == pytest.approx(design - relief)


def test_control_tier_one_has_no_relief():
    design = scoring.enemy_scores(1, "Basic", "None", 50, 25)["Common"]
    assert es.strength_score("Common", 1, "Basic", "None", 50, 25) == pytest.approx(design)


def test_an_early_floor_of_a_deep_dungeon_is_lifted_to_floor_one_of_a_twelve_floor_one():
    """THE CASE: floor 1 of a 50-floor tier-1 dungeon scores below nothing in the
    design; the game builds it as floor 1 of a 12-floor dungeon."""
    assert scoring.enemy_scores(1, "Basic", "None", 50, 1)["Common"] < 0
    twelve = scoring.enemy_scores(1, "Basic", "None", 12, 1)["Common"]
    assert twelve > 0
    assert es.strength_score("Common", 1, "Basic", "None", 50, 1) == pytest.approx(twelve)


def test_control_a_floor_above_the_minimum_is_not_moved():
    design = scoring.enemy_scores(1, "Basic", "None", 50, 10)["Common"]
    assert design > scoring.enemy_scores(1, "Basic", "None", 12, 1)["Common"]
    assert es.strength_score("Common", 1, "Basic", "None", 50, 10) == pytest.approx(design)


def test_damage_alone_is_scaled_and_the_rest_is_the_design_conversion_at_that_score():
    built = es.strength_on_floor("Legendary", 3, "Basic", "None", 12, 6, kind="Brute")
    at_that_score = es.stats_for("Legendary", built.score, "Brute", tier=3)
    assert built.health == pytest.approx(at_that_score.health)
    assert built.armor == pytest.approx(at_that_score.armor)
    assert built.damage_per_hit == pytest.approx(at_that_score.damage_per_hit * es.damage_scale(3))


def test_the_design_figure_the_offence_is_anchored_on_is_not_eased():
    """`affixes.py` anchors the player's damage on `stats_on_floor`; the ease must
    not reach it, or re-deriving the offence would take the ease back out."""
    design = es.stats_on_floor("Common", 8, "Cataclysm")
    built = es.strength_on_floor("Common", 8, "Cataclysm")
    assert design.score == scoring.enemy_scores(8, "Cataclysm", "None", 50, 50)["Common"]
    assert built.score < design.score
