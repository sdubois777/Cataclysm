"""Tests for the enemy rarity base classes."""

from __future__ import annotations

import pytest

from cataclysm_sim import enemy_stats as es
from cataclysm_sim import scoring

ORDER = es.RARITY_ORDER


def at_tier_eight(rarity: str) -> es.EnemyStats:
    return es.stats_on_floor(rarity, 8, "Cataclysm")


# --------------------------------------------------------------------------
# One formula, six rarities
# --------------------------------------------------------------------------

def test_the_rarity_list_matches_the_authoritative_one():
    """`scoring.RARITY_WEIGHTS` is a verified port. If a rarity were added or
    renamed there and not here, an enemy would have a score and no stats."""
    assert set(ORDER) == set(scoring.RARITY_WEIGHTS)
    assert len(ORDER) == 6


def test_the_rarity_step_is_the_distance_above_common():
    assert es.rarity_step("Common") == 0
    assert es.rarity_step("Cataclysm Boss") == 5
    assert [es.rarity_step(r) for r in ORDER] == list(range(6))


def test_an_unknown_rarity_is_rejected():
    with pytest.raises(ValueError, match="unknown rarity"):
        es.rarity_step("Rare")


def test_the_superseded_rare_tier_is_not_present():
    """The design document lists Rare; the authoritative model does not."""
    assert "Rare" not in ORDER


def test_every_rarity_produces_a_complete_stat_block():
    for rarity in ORDER:
        e = es.stats_for(rarity, 1000.0)
        for field in ("health", "damage_per_hit", "armor", "attack_interval",
                      "resistance", "penetration", "crit_chance",
                      "crit_multiplier", "move_speed"):
            assert getattr(e, field) is not None, f"{rarity} has no {field}"


# --------------------------------------------------------------------------
# Score-scaled stats say how BIG an enemy is
# --------------------------------------------------------------------------

def test_health_damage_and_armor_scale_with_the_score():
    small = es.stats_for("Boss", 1000.0)
    big = es.stats_for("Boss", 2000.0)
    assert big.health == pytest.approx(2 * small.health)
    assert big.damage_per_hit == pytest.approx(2 * small.damage_per_hit)
    assert big.armor == pytest.approx(2 * small.armor)


def test_rarity_traits_do_not_scale_with_the_score():
    """The whole distinction. A big Common enemy is still a Common enemy: it
    does not start critting more or resisting more because the floor is deeper.
    """
    small = es.stats_for("Elite", 100.0)
    big = es.stats_for("Elite", 10_000.0)
    for field in ("attack_interval", "resistance", "penetration",
                  "crit_chance", "crit_multiplier", "move_speed"):
        assert getattr(small, field) == getattr(big, field), field


def test_an_enemy_always_has_at_least_one_health():
    """Guards against a zero-score enemy being unkillable through division."""
    assert es.stats_for("Common", 0.0).health >= 1.0


def test_a_negative_score_is_treated_as_zero():
    assert es.stats_for("Common", -500.0).damage_per_hit == 0.0


# --------------------------------------------------------------------------
# A boss is not a large common enemy
# --------------------------------------------------------------------------

def test_health_grows_much_faster_than_damage_across_rarities():
    """If both grew together a boss would be unkillable and lethal at once."""
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    health_ratio = boss.health / common.health
    damage_ratio = boss.damage_per_hit / common.damage_per_hit
    assert health_ratio > 10
    assert damage_ratio < 5
    assert health_ratio > 4 * damage_ratio


def test_damage_per_second_grows_far_less_than_damage_per_hit():
    """Rarer enemies wind up more slowly, so hitting harder is paid for."""
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    per_hit = boss.damage_per_hit / common.damage_per_hit
    per_second = boss.damage_per_second / common.damage_per_second
    assert per_second < per_hit


@pytest.mark.parametrize("field, rises", [
    ("health", True), ("damage_per_hit", True), ("armor", True),
    ("attack_interval", True), ("resistance", True), ("penetration", True),
    ("crit_chance", True), ("crit_multiplier", True), ("move_speed", False),
])
def test_every_stat_moves_strictly_in_one_direction_across_the_ladder(field, rises):
    """No stat should peak in the middle, and none should be flat either.

    Checked as a STRICT change between every neighbouring pair. Comparing
    against a sorted list allows ties, so a stat flattened to the same value at
    every rarity would pass a test named 'moves in one direction' -- and a flat
    stat is one that stopped distinguishing the rarities at all, which is
    exactly the failure worth catching.
    """
    values = [getattr(at_tier_eight(r), field) for r in ORDER]
    # strict=False is required: the two lists differ in length by one, because
    # this pairs each rarity with the next.
    for lower, higher in zip(values, values[1:], strict=False):
        if rises:
            assert higher > lower, f"{field} does not rise: {values}"
        else:
            assert higher < lower, f"{field} does not fall: {values}"


def test_a_common_enemy_has_no_armor_resistance_or_penetration():
    """Swarm fodder should die to anything and punish nothing."""
    common = at_tier_eight("Common")
    assert common.armor == 0.0
    assert common.resistance == 0.0
    assert common.penetration == 0.0


def test_move_speed_never_goes_negative():
    for rarity in ORDER:
        assert at_tier_eight(rarity).move_speed > 0.0


# --------------------------------------------------------------------------
# Penetration, which is what makes over-capping worth anything
# --------------------------------------------------------------------------

def test_penetration_rises_with_rarity_so_over_capping_has_a_purpose():
    """The design says resistance is reduced by enemy penetration scaling and
    never says by how much. Without a number, a player at exactly the cap and
    one over-capped would be identical and over-capping would be pointless."""
    boss = at_tier_eight("Cataclysm Boss")
    assert boss.penetration > 0
    at_cap = 70.0 - boss.penetration
    over_capped = min(70.0, 95.0 - boss.penetration)
    assert over_capped > at_cap


def test_penetration_does_nothing_against_a_common_enemy():
    """So resistance headroom matters against the things that punish it, and a
    player is not taxed for the whole game by the hardest case."""
    common = at_tier_eight("Common")
    assert 70.0 - common.penetration == 70.0


# --------------------------------------------------------------------------
# Critical strikes
# --------------------------------------------------------------------------

def test_criticals_raise_average_damage_above_the_per_hit_figure():
    boss = at_tier_eight("Cataclysm Boss")
    assert boss.average_damage_per_hit > boss.damage_per_hit


def test_a_rarer_enemy_gets_more_of_its_damage_from_criticals():
    """What makes a boss hit feel like a spike rather than a metronome."""
    def crit_share(e: es.EnemyStats) -> float:
        return e.average_damage_per_hit / e.damage_per_hit

    assert crit_share(at_tier_eight("Cataclysm Boss")) > crit_share(
        at_tier_eight("Common"))


# --------------------------------------------------------------------------
# Placing an enemy on a floor
# --------------------------------------------------------------------------

def test_a_deeper_floor_produces_a_stronger_enemy():
    shallow = es.stats_on_floor("Common", 8, "Cataclysm", floor=5)
    deep = es.stats_on_floor("Common", 8, "Cataclysm", floor=50)
    assert deep.health > shallow.health


def test_a_higher_tier_produces_a_stronger_enemy():
    low = es.stats_on_floor("Common", 1, "Cataclysm")
    high = es.stats_on_floor("Common", 8, "Cataclysm")
    assert high.health > 10 * low.health


def test_dungeon_modifiers_make_an_enemy_harder():
    """Dungeon modifiers carry a weight that is summed into the score, which is
    how they reach the enemies inside. Enemy modifiers deliberately do not."""
    plain = es.stats_on_floor("Common", 8, "Cataclysm", modifier_score=0.0)
    modified = es.stats_on_floor("Common", 8, "Cataclysm", modifier_score=200.0)
    assert modified.health > plain.health


# --------------------------------------------------------------------------
# The player side is reported, not asserted
# --------------------------------------------------------------------------

def test_the_player_helpers_are_reporting_tools_with_no_targets_baked_in():
    """The enemy side is now set on its own terms and gear is fitted to it, so
    nothing here should constrain a player number. These two functions exist to
    answer questions about the enemy stats, not to enforce anything.

    An earlier version of this file asserted player survival targets directly,
    which is what kept producing conflicts with the gear work.
    """
    boss = at_tier_eight("Cataclysm Boss")
    assert es.hits_to_kill_player(boss, 6330.0) > 0
    assert es.player_damage_to_kill_in(boss, 30.0) == pytest.approx(
        boss.health / 30.0)


def test_mitigation_reduces_how_hard_an_enemy_hits_a_player():
    boss = at_tier_eight("Cataclysm Boss")
    bare = es.hits_to_kill_player(boss, 6330.0, mitigation_fraction=0.0)
    armoured = es.hits_to_kill_player(boss, 6330.0, mitigation_fraction=0.5)
    assert armoured == pytest.approx(2 * bare)


def test_killing_an_enemy_faster_needs_proportionally_more_damage():
    boss = at_tier_eight("Cataclysm Boss")
    assert es.player_damage_to_kill_in(boss, 10.0) == pytest.approx(
        3 * es.player_damage_to_kill_in(boss, 30.0))
