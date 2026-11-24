"""The measurement behind the issue #2853 Power Score weight proposal re-scores characters faithfully.

`sim/measure_power_score_weights.py` re-scores characters under candidate weights. Its figures are only worth
quoting if its scoring under TODAY's weights is exactly `player_power.power_score`, and if the character it walks
is one the model allows (the rarity cap, the piece and socket counts). These are the checks that say so; the
measurement itself is too slow for the suite and is run by hand.
"""

import random

import pytest

import measure_power_score_weights as m
from cataclysm_sim import loot, player_power, scoring

#: The plain numbers the project owner listed on 2026-11-24 (issue #2853).
OWNERS_PLAIN_NUMBERS = {1, 2, 5, 10, 25, 50, 100}


def character_of(parts: m.Parts) -> player_power.Character:
    return player_power.Character(
        level=parts.level,
        gear=tuple(player_power.GearPiece(rarity=r, upgrade=u) for r, u in parts.pieces),
        gems=parts.gems,
        resistances=parts.resistances)


@pytest.mark.parametrize("tier", range(1, 9))
def test_todays_weights_score_the_reference_exactly_as_the_model_does(tier):
    reference = player_power.reference_character(tier)
    assert m.TODAY.score(m.parts_of(reference)) == player_power.power_score(reference)


def test_a_walked_character_is_scored_exactly_as_the_model_scores_it():
    """One floor of an Outpost dungeon, then the character's parts through both scorers."""
    rng = random.Random(7)
    c = m.Character()
    m.walk(c, 1, m.TODAY, rng, gap_rule=False)
    parts = c.parts()
    assert parts.pieces, "a full-clear floor dropped nothing worn, so this checks nothing"
    assert m.TODAY.score(parts) == player_power.power_score(character_of(parts))


def test_the_walk_respects_the_rarity_cap_and_the_counts():
    rng = random.Random(11)
    c = m.Character()
    m.walk(c, 2, m.TODAY, rng, gap_rule=False)
    parts = c.parts()
    cap = loot.rarity_index(loot.highest_droppable_rarity(m.TIER))
    assert all(rarity <= cap for rarity, _ in parts.pieces)
    assert all(upgrade == 0 for _, upgrade in parts.pieces), "no tier 1 drop may arrive above +0 under the cap"
    assert len(parts.pieces) <= player_power.GEAR_PIECES
    assert len(parts.resistances) == player_power.RESISTANCE_COUNT


def test_every_recommended_weight_is_one_of_the_owners_plain_numbers():
    w = m.RECOMMENDED
    for value in (w.level, w.per_rarity, w.per_upgrade, w.gem, w.resistance):
        assert value in OWNERS_PLAIN_NUMBERS, value
    assert w.flat_upgrade == 0


@pytest.mark.parametrize("tier", range(1, 9))
def test_the_recommendation_lands_within_six_percent_of_every_ceiling(tier):
    """The proposal states the worst miss as 5.5%, at tier 1."""
    score = m.RECOMMENDED.score(m.reference_parts(tier))
    ceiling = scoring.PLAYER_MAX_SCORES[tier]
    assert abs(score - ceiling) / ceiling < 0.06, (tier, score, ceiling)
