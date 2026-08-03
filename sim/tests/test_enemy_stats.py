"""Tests for turning an Enemy Score into health, damage and attack rate."""

from __future__ import annotations

import pytest

from cataclysm_sim import enemy_stats as es
from cataclysm_sim import scoring
from cataclysm_sim.character import Attributes, Character
from cataclysm_sim.classes import DEMONIC_CLASSES
from cataclysm_sim.player_power import reference_character


def ravager_at(tier: int) -> float:
    """Effective health of the middle-durability class at a tier's level."""
    ref = reference_character(tier)
    c = Character(DEMONIC_CLASSES["Ravager"], level=ref.level,
                  attributes=Attributes(vitality=ref.level))
    return c.stat("max_health") + c.stat("max_energy_shield")


def common_score(tier: int) -> float:
    return scoring.enemy_scores(tier, "Basic", "None", 50, 50)["Common"]


# --------------------------------------------------------------------------
# The two targets, which are what this was solved from
# --------------------------------------------------------------------------

@pytest.mark.parametrize("tier", range(1, 9))
def test_a_common_enemy_kills_the_player_in_eight_to_ten_hits(tier):
    """The project owner's first target, checked at every tier rather than the
    one it was solved at."""
    hits = es.hits_to_kill_player(common_score(tier), "Common", ravager_at(tier))
    assert 8.0 <= hits <= 10.0, f"tier {tier}: {hits:.1f} hits"


@pytest.mark.parametrize("tier", range(1, 9))
def test_the_player_kills_a_common_enemy_in_one_to_three_hits(tier):
    """The second target. Both hold across all eight tiers, which is the thing
    worth checking: solving at one tier is easy, holding at all of them is not."""
    hits = es.hits_to_kill_enemy(common_score(tier), "Common",
                                 scoring.PLAYER_MAX_SCORES[tier])
    assert 1.0 <= hits <= 3.0, f"tier {tier}: {hits:.1f} hits"


@pytest.mark.parametrize("tier", range(1, 9))
def test_the_chosen_factors_sit_in_the_middle_of_the_one_to_three_band(tier):
    """The band the project owner set is 1 to 3 hits, which is wide enough that
    doubling the player damage factor stays inside it. That makes the band alone
    a weak guard: a change of that size would pass unnoticed.

    This pins where the chosen values actually land, so moving them is a
    deliberate act rather than an accident. The band above is the requirement;
    this is the decision.
    """
    hits = es.hits_to_kill_enemy(common_score(tier), "Common",
                                 scoring.PLAYER_MAX_SCORES[tier])
    assert hits == pytest.approx(2.0, abs=0.3), f"tier {tier}: {hits:.2f} hits"


def test_the_targets_hold_for_the_frailest_class_too():
    """The Ritualist has roughly half the Ravager's effective health, so it
    should die faster. It should still not be one-shot by a common enemy."""
    ref = reference_character(8)
    c = Character(DEMONIC_CLASSES["Ritualist"], level=ref.level,
                  attributes=Attributes(vitality=ref.level))
    pool = c.stat("max_health") + c.stat("max_energy_shield")
    hits = es.hits_to_kill_player(common_score(8), "Common", pool)
    assert hits < 8.0, "the frailest class should die faster than the middle one"
    assert hits > 3.0, "but a common enemy should not nearly one-shot anyone"


# --------------------------------------------------------------------------
# Rarity changes the shape of a fight, not only its size
# --------------------------------------------------------------------------

ORDER = ["Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss"]


def test_the_rarity_list_matches_the_authoritative_one():
    """`scoring.RARITY_WEIGHTS` is a verified port. If a rarity is added or
    renamed there and not here, an enemy would have a score and no stats."""
    assert set(es.RARITY_PROFILES) == set(scoring.RARITY_WEIGHTS)


def test_rarer_enemies_have_more_health_and_hit_harder():
    for lower, higher in zip(ORDER, ORDER[1:], strict=False):
        assert es.RARITY_PROFILES[higher].health > es.RARITY_PROFILES[lower].health
        assert es.RARITY_PROFILES[higher].damage > es.RARITY_PROFILES[lower].damage


def test_rarer_enemies_attack_less_often():
    """This is what stops a boss being a common enemy with more health. If every
    rarity attacked at the same rate, rarity would only change how long a fight
    lasted and not how it is fought."""
    for lower, higher in zip(ORDER, ORDER[1:], strict=False):
        assert (es.RARITY_PROFILES[higher].attack_interval
                > es.RARITY_PROFILES[lower].attack_interval)


def test_a_boss_hits_much_harder_per_hit_than_a_common_enemy():
    score = 6000.0
    ratio = (es.enemy_damage_per_hit(score, "Cataclysm Boss")
             / es.enemy_damage_per_hit(score, "Common"))
    assert ratio > 2.0, "a boss hit should be a different kind of event"


def test_a_boss_deals_less_damage_per_second_than_its_per_hit_suggests():
    """Hitting harder is paid for by hitting less often. Damage per second must
    not scale as steeply as damage per hit, or bosses are simply strictly
    better at everything."""
    score = 6000.0
    per_hit = (es.enemy_damage_per_hit(score, "Cataclysm Boss")
               / es.enemy_damage_per_hit(score, "Common"))
    per_second = (es.enemy_damage_per_second(score, "Cataclysm Boss")
                  / es.enemy_damage_per_second(score, "Common"))
    assert per_second < per_hit


def test_even_with_no_gear_a_boss_does_not_one_shot_the_player():
    """A gearless character is not a real tier 8 character, so this is a floor
    rather than a balance target. But a boss removing a whole health bar in one
    hit would leave gear no room to make the fight longer -- only room to make
    it survivable at all."""
    scores = scoring.enemy_scores(8, "Cataclysm", "None", 50, 50)
    hits = es.hits_to_kill_player(scores["Cataclysm Boss"], "Cataclysm Boss",
                                  ravager_at(8))
    assert hits >= 2.0, f"a gearless Ravager survives only {hits:.1f} boss hits"


def test_a_boss_takes_a_long_time_to_kill():
    scores = scoring.enemy_scores(8, "Cataclysm", "None", 50, 50)
    hits = es.hits_to_kill_enemy(scores["Cataclysm Boss"], "Cataclysm Boss", 6327)
    assert 20 <= hits <= 80, f"{hits:.0f} hits is not a boss fight"


# --------------------------------------------------------------------------
# The conversions themselves
# --------------------------------------------------------------------------

def test_health_and_damage_scale_with_the_score():
    assert es.enemy_health(2000.0, "Common") == 2 * es.enemy_health(1000.0, "Common")
    assert (es.enemy_damage_per_hit(2000.0, "Common")
            == 2 * es.enemy_damage_per_hit(1000.0, "Common"))


def test_player_damage_scales_with_power_score():
    assert es.player_damage_per_hit(6327.0) == pytest.approx(6327.0 * es.PLAYER_DAMAGE_FACTOR)
    assert es.player_damage_per_hit(0.0) == 0.0


def test_an_enemy_always_has_at_least_one_health():
    """Guards against a zero-score enemy being unkillable through division."""
    assert es.enemy_health(0.0, "Common") >= 1.0


def test_an_unknown_rarity_is_rejected():
    with pytest.raises(ValueError, match="unknown rarity"):
        es.profile_for("Rare")


def test_the_superseded_rare_tier_is_not_present():
    """The design document lists Rare; the authoritative model does not. Adding
    it here would resurrect the stale list. See issue #30."""
    assert "Rare" not in es.RARITY_PROFILES


# --------------------------------------------------------------------------
# Nothing here is fitted to passive tree content
# --------------------------------------------------------------------------

def test_the_numbers_are_not_fitted_to_the_bulwark_keystone():
    """Passive tree content can change and the engine takes precedence, so the
    Bulwark keystone that triggers on a hit above 5,000 is evidence of intent at
    most. If these factors had been fitted to it, the largest hit in the game
    would sit just above 5,000. It does not.
    """
    scores = scoring.enemy_scores(8, "Cataclysm", "None", 50, 50)
    biggest = es.enemy_damage_per_hit(scores["Cataclysm Boss"], "Cataclysm Boss")
    assert biggest < 5000.0
