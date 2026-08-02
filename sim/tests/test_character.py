"""Tests for class base stats and the stat pipeline they feed."""

from __future__ import annotations

import json
import pathlib
import re

import pytest

from cataclysm_sim import character as ch

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BULWARK_TREE = REPO_ROOT / "docs" / "Bulwark_Class_Tree_Final.json"


# --------------------------------------------------------------------------
# The scale constants are real, not copied assertions
# --------------------------------------------------------------------------

def _bulwark_text() -> str:
    data = json.loads(BULWARK_TREE.read_text(encoding="utf-8"))
    parts = []
    for node in data["nodes"]:
        d = node.get("data", {})
        parts.append(str(d.get("label", "")))
        parts.append(str(d.get("description", "") or d.get("effect", "")))
    return " | ".join(parts)


def test_the_bulwark_tree_file_is_where_the_scale_comes_from():
    """The module claims its scale is read out of the Bulwark class tree. If
    that file stops containing those numbers, the basis is gone and this must
    fail rather than let the module keep asserting them."""
    text = _bulwark_text()
    found = {int(m.replace(",", ""))
             for m in re.findall(r"([\d,]+)\s*Maximum HP", text)}
    for threshold in ch.BULWARK_HP_THRESHOLDS:
        assert threshold in found, (
            f"{threshold:,} Maximum HP is no longer in "
            f"{BULWARK_TREE.name}; the module's scale is out of date")


def test_the_flat_health_grants_per_passive_point_are_real():
    text = _bulwark_text()
    found = {int(m.replace(",", ""))
             for m in re.findall(r"\+([\d,]+)\s*flat (?:Maximum )?HP\b", text)}
    assert set(ch.BULWARK_FLAT_HP_PER_POINT) <= found


def test_the_endgame_and_ceiling_are_drawn_from_those_thresholds():
    assert ch.BULWARK_ENDGAME_HP in ch.BULWARK_HP_THRESHOLDS
    assert ch.BULWARK_CEILING_HP == max(ch.BULWARK_HP_THRESHOLDS)


# --------------------------------------------------------------------------
# The pipeline
# --------------------------------------------------------------------------

def test_attributes_multiply_the_base_rather_than_adding_to_it():
    """The whole point of the Stat Calculation rules. If Vitality were additive
    it would give a fixed number of points regardless of the base, and doubling
    the base would not double what Vitality is worth."""
    small = ch.Character(ch.MASOCHIST, level=50,
                         attributes=ch.Attributes(vitality=50))
    big = ch.Character(ch.MASOCHIST, level=50,
                       attributes=ch.Attributes(vitality=50),
                       gear=ch.Gear(flat_health=ch.MASOCHIST.at_level(50)["health"]))

    base_small = ch.MASOCHIST.at_level(50)["health"]
    gain_small = small.max_health() - base_small
    gain_big = big.max_health() - 2 * base_small

    # The base doubled, so what Vitality contributed must double too.
    assert gain_big == pytest.approx(2 * gain_small)


def test_fifty_vitality_points_add_exactly_one_hundred_percent():
    base = ch.MASOCHIST.at_level(50)["health"]
    c = ch.Character(ch.MASOCHIST, level=50,
                     attributes=ch.Attributes(vitality=50))
    assert c.max_health() == pytest.approx(base * 2.0)


def test_gear_flat_health_is_added_before_attributes_scale_it():
    """Order matters. Flat gear inside the multiplication is worth far more than
    flat gear outside it, and the design puts it inside."""
    level, flat = 50, 1000.0
    base = ch.MASOCHIST.at_level(level)["health"]
    c = ch.Character(ch.MASOCHIST, level=level,
                     attributes=ch.Attributes(vitality=50),
                     gear=ch.Gear(flat_health=flat))
    assert c.max_health() == pytest.approx((base + flat) * 2.0)
    # If flat gear were applied after scaling it would be base*2 + flat, which
    # is smaller by exactly the flat amount.
    assert c.max_health() > base * 2.0 + flat - 1e-9


def test_gear_increases_join_the_same_sum_as_attribute_points():
    level = 50
    base = ch.MASOCHIST.at_level(level)["health"]
    c = ch.Character(ch.MASOCHIST, level=level,
                     attributes=ch.Attributes(vitality=25),   # +50%
                     gear=ch.Gear(increased_health=0.50))     # +50%
    assert c.max_health() == pytest.approx(base * 2.0)


def test_health_regen_scales_with_vitality_at_one_percent_per_point():
    level = 100
    base = ch.MASOCHIST.at_level(level)["health_regen"]
    c = ch.Character(ch.MASOCHIST, level=level,
                     attributes=ch.Attributes(vitality=100))
    assert c.health_regen() == pytest.approx(base * 2.0)


def test_per_level_scaling_is_linear():
    steps = [ch.MASOCHIST.at_level(lv)["health"] for lv in range(1, 101)]
    # strict=False is deliberate: the two lists differ in length by one, because
    # this pairs each level with the next one.
    gaps = {round(b - a, 9) for a, b in zip(steps, steps[1:], strict=False)}
    assert gaps == {ch.MASOCHIST.health_per_level}


# --------------------------------------------------------------------------
# The Masochist's identity
# --------------------------------------------------------------------------

def test_a_masochist_has_no_mana_at_any_level():
    for level in (1, 25, 50, 100):
        c = ch.Character(ch.MASOCHIST, level=level,
                         attributes=ch.Attributes(mind=level))
        assert c.max_mana() == 0.0


def test_mana_gear_gives_a_masochist_nothing():
    """The class does not have the resource, so there is nothing to add to."""
    c = ch.Character(ch.MASOCHIST, level=100,
                     attributes=ch.Attributes(mind=100),
                     gear=ch.Gear(flat_mana=5000, increased_mana=3.0))
    assert c.max_mana() == 0.0


def test_the_masochist_is_marked_as_spending_health():
    assert ch.MASOCHIST.spends_health is True


def test_a_health_spending_class_carries_no_mana_values_at_all():
    """`max_mana` returns zero for a health-spending class before it reads any
    of the mana fields, so nonzero mana values on such a class would sit there
    unnoticed and become live the moment that check moved or the class changed.
    The data has to agree with the flag, not merely be hidden by it."""
    for stats in ch.CLASSES.values():
        if not stats.spends_health:
            continue
        assert stats.base_mana == 0.0, f"{stats.name} has base mana"
        assert stats.mana_per_level == 0.0, f"{stats.name} gains mana per level"
        assert stats.base_mana_regen == 0.0, f"{stats.name} has mana regen"
        assert stats.mana_regen_per_level == 0.0, (
            f"{stats.name} gains mana regen per level")


def test_a_masochist_has_no_energy_shield_without_gear():
    c = ch.Character(ch.MASOCHIST, level=100,
                     attributes=ch.Attributes(spirit=100))
    assert c.max_energy_shield() == 0.0


def test_energy_shield_from_gear_is_still_scaled_by_spirit():
    """Spirit stays meaningful for a player who builds for it deliberately."""
    c = ch.Character(ch.MASOCHIST, level=100,
                     attributes=ch.Attributes(spirit=50),
                     gear=ch.Gear(flat_energy_shield=1000))
    assert c.max_energy_shield() == pytest.approx(2000.0)
    assert c.effective_health_pool() > c.max_health()


def test_masochist_health_regen_is_higher_than_its_health_gain_would_suggest():
    """Health regeneration is this class's resource regeneration, so it has to
    be large enough to pay for abilities. At level 100 it should restore at
    least 1% of the class's own base health per second."""
    level = 100
    base = ch.MASOCHIST.at_level(level)
    assert base["health_regen"] / base["health"] >= 0.01


# --------------------------------------------------------------------------
# The numbers land inside the scale the Bulwark tree implies
# --------------------------------------------------------------------------

def test_a_finished_masochist_lands_below_the_endgame_threshold():
    """Base values plus gear plus attributes must NOT reach 20,000 on their own.
    The passive tree grants 50, 200 and 500 flat HP per point plus percentage
    nodes, and it has to have something left to do."""
    end = ch.geared(ch.MASOCHIST, 100).max_health()
    assert end < ch.BULWARK_ENDGAME_HP


def test_a_finished_masochist_is_within_reach_of_the_endgame_threshold():
    """It must also not be so far short that no realistic tree could close it.
    The Bulwark's 500-per-point node closes a gap this size in about 10 points,
    out of a 230 point budget."""
    end = ch.geared(ch.MASOCHIST, 100).max_health()
    gap = ch.BULWARK_ENDGAME_HP - end
    points_needed = gap / max(ch.BULWARK_FLAT_HP_PER_POINT)
    assert points_needed <= 30, (
        f"the tree would need {points_needed:.0f} points of the strongest flat "
        "node just to reach the endgame threshold")


def test_a_finished_masochist_clears_the_middle_thresholds():
    """The tree's nodes gate on 5,000, 8,000 and 10,000 Maximum HP. A character
    who has finished levelling and geared up should be past those without
    needing the tree, or those nodes are unreachable in practice."""
    end = ch.geared(ch.MASOCHIST, 100).max_health()
    for threshold in (5_000, 8_000, 10_000):
        assert end > threshold


def test_a_naked_level_one_character_is_not_absurd():
    c = ch.naked(ch.MASOCHIST, 1)
    assert 50 <= c.max_health() <= 500


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------

@pytest.mark.parametrize("level", [0, 101, -1])
def test_level_outside_one_to_one_hundred_is_rejected(level):
    with pytest.raises(ValueError):
        ch.Character(ch.MASOCHIST, level=level)


def test_spending_more_attribute_points_than_levels_is_rejected():
    """One point per level. A level 10 character cannot have 50 in Vitality."""
    with pytest.raises(ValueError, match="one point per level"):
        ch.Character(ch.MASOCHIST, level=10,
                     attributes=ch.Attributes(vitality=50))


def test_points_spread_across_attributes_still_counts_against_the_level():
    with pytest.raises(ValueError, match="one point per level"):
        ch.Character(ch.MASOCHIST, level=10,
                     attributes=ch.Attributes(vitality=4, mind=4, spirit=4))


def test_spending_exactly_the_level_in_points_is_allowed():
    c = ch.Character(ch.MASOCHIST, level=12,
                     attributes=ch.Attributes(vitality=6, mind=3, spirit=3))
    assert c.max_health() > 0


@pytest.mark.parametrize("level", [0, 101])
def test_class_stats_reject_levels_outside_the_range(level):
    with pytest.raises(ValueError):
        ch.MASOCHIST.at_level(level)


# --------------------------------------------------------------------------
# Only the vertical slice class is defined so far
# --------------------------------------------------------------------------

def test_only_the_masochist_is_defined():
    """23 classes still have no base values. When one is added this test should
    be updated deliberately, not silently."""
    assert list(ch.CLASSES) == ["Masochist"]
