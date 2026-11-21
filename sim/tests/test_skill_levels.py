"""The simulation's copy of skill levels behaves as ruled. Issue #2764."""

from __future__ import annotations

from cataclysm_sim import skill_levels as sl


def straight_curve(level: int) -> float:
    """A stand-in character curve: 1,000 experience a level."""
    return 1000.0 * (level - 1)


def test_level_one_is_free_and_each_level_costs_a_quarter_of_the_paired_character_climb():
    assert sl.experience_to_reach(1, straight_curve) == 0.0
    assert sl.experience_to_reach(2, straight_curve) == 0.25 * 1000.0
    assert sl.experience_to_reach(3, straight_curve) == 0.25 * 3000.0  # paired with character level 4


def test_the_characters_level_caps_a_skill_and_the_experience_above_it_is_kept():
    plenty = 1e12
    assert sl.level_for(plenty, 1, straight_curve) == 1
    assert sl.level_for(plenty, 3, straight_curve) == 2, "level 3 is paired with character level 4"
    assert sl.level_for(plenty, 4, straight_curve) == 3
    assert sl.level_for(plenty, 100, straight_curve) == 20


def test_one_short_of_a_level_is_the_level_below():
    assert sl.level_for(749.9, 100, straight_curve) == 2
    assert sl.level_for(750.0, 100, straight_curve) == 3


def test_a_kill_is_shared_by_the_health_each_skill_removed_and_never_more_than_the_whole():
    assert sl.split_a_kill(1000, {"Heavy": 60.0, "Special": 30.0}, 100.0) == {"Heavy": 600.0, "Special": 300.0}
    healed = sl.split_a_kill(1000, {"Heavy": 150.0, "Special": 50.0}, 100.0)
    assert healed == {"Heavy": 750.0, "Special": 250.0}
    assert sl.split_a_kill(0, {"Heavy": 10.0}, 100.0) == {}


def test_increased_skill_experience_gain_raises_every_share_alike_before_the_split():
    """Issue #2803: the gem Of The Scholar's stat, as the game's `SkillExperienceGainFactor` reads it."""
    raised = sl.split_a_kill(1000, {"Heavy": 60.0, "Special": 30.0}, 100.0, skill_experience_gain=0.5)
    assert raised == {"Heavy": 900.0, "Special": 450.0}
    assert sl.split_a_kill(1000, {"Heavy": 60.0}, 100.0, skill_experience_gain=0.0) == {"Heavy": 600.0}
    assert sl.split_a_kill(1000, {"Heavy": 60.0}, 100.0, skill_experience_gain=-2.0) == {"Heavy": 0.0}
