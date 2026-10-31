"""What the game builds a creature with: the design's enemy score, eased three ways.

ISSUE #1569. The project owner, 2026-09-11: creature health and damage come from
the design's enemy score on each floor. Ruled 2026-10-30, and all three live in
`enemy_stats.strength_score` / `strength_on_floor` and the game's copy, never in
`scoring.py`:

1. the tier relief of #1425 is subtracted from the score;
2. the score is never below the same creature on floor 1 of a 12-floor dungeon
   of the same kind;
3. damage is multiplied by 0.20 at tier 1, rising evenly to 1.00 at tier 8.

`stats_on_floor` stays the design's figure, because `affixes.py` anchors the
player's offence on it.
"""
from __future__ import annotations

import pytest

from cataclysm_sim import enemy_stats as es
from cataclysm_sim import scoring
from cataclysm_sim.config import TuningConfig


def test_the_damage_scale_is_a_fifth_at_tier_one_rising_evenly_to_one_at_tier_eight():
    assert [es.damage_scale(t) for t in range(1, 9)] == pytest.approx(
        [0.20 + 0.80 * (t - 1) / 7 for t in range(1, 9)])
    assert es.damage_scale(1) == pytest.approx(0.20)
    assert es.damage_scale(8) == pytest.approx(1.0)


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
