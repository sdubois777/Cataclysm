"""The Wraith's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmWraithCharacter.h` hard-codes the numbers `ARCHETYPES["Wraith"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ABILITIES["Wraith"]` and `ATTACK_REACH["Wraith"]` in
`sim/cataclysm_sim/enemy_abilities.py` design (Death's Wraith, approved by the project owner on 2026-11-26). The
Python is authoritative: when this file fails, the usual fix is to change the C++.

WHY A SOURCE-TEXT TEST: continuous integration never builds the C++, so an automation test cannot run on a pull
request. This reads the header, which is present either way. Metres in the model, centimetres in Unreal; the factor of
100 is stated at each conversion.

THE NUMBERS THE MODEL DOES NOT STATE are checked against what must stay true of them: the dash's speed (a judgement)
must beat walking, and the sandbox fallback figures must be the model's own `stats_on_floor` at tier 1.
"""
from __future__ import annotations

import math
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SIM = REPO_ROOT / "sim"
if str(SIM) not in sys.path:
    sys.path.insert(0, str(SIM))

CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
WRAITH_HEADER = CHARACTER_DIR / "CataclysmWraithCharacter.h"
WRAITH_SOURCE = CHARACTER_DIR / "CataclysmWraithCharacter.cpp"
GAME_MODE_HEADER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player" / "CataclysmGameMode.h"
CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path = WRAITH_HEADER) -> float:
    """The value of a `static constexpr float <name> = <number>f;` (or int32) line, or a `float <name> = <n>f;`."""
    match = re.search(
        rf"(?:static\s+constexpr\s+(?:float|int32)\s+|\bfloat\s+){re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f?\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no '{name} = <number>;' line. If it was renamed, rename it here too.")
    return float(match.group(1))


def wraith():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Wraith")


def ability(name: str):
    from cataclysm_sim.enemy_abilities import abilities

    found = [each for each in abilities("Wraith") if each.name == name]
    assert len(found) == 1, f"ABILITIES['Wraith'] holds {len(found)} abilities named {name!r}"
    return found[0]


# ---------------------------------------------------------------------------
# The stat block
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(("cpp", "field"), [
    ("DesignedAttackIntervalSeconds", "attack_interval"),
    ("DesignedResistancePercent", "resistance"),
    ("DesignedCritChancePercent", "crit_chance"),
    ("DesignedCritMultiplierPercent", "crit_multiplier"),
    ("DesignedEvasionPercent", "evasion"),
    ("DesignedEnergyShieldFraction", "energy_shield_fraction"),
    ("DesignedTurnRateDegreesPerSecond", "turn_rate_degrees"),
])
def test_a_stat_is_the_models(cpp, field):
    assert math.isclose(constant(cpp), getattr(wraith(), field)), (cpp, field)


def test_its_walk_is_the_models_in_centimetres():
    assert math.isclose(constant("DesignedWalkSpeedCmPerSecond"), wraith().move_speed * CM_PER_METRE)


def test_its_body_is_the_models_width():
    assert math.isclose(constant("WraithCapsuleRadius"), wraith().body_radius * CM_PER_METRE)


def test_it_is_deaths_and_has_no_separate_chase_speed():
    assert wraith().cataclysm == "Death"
    assert wraith().chase_speed == 0.0, "the C++ has one walk speed; a chase speed would need it"


# ---------------------------------------------------------------------------
# Reap
# ---------------------------------------------------------------------------

def test_its_reach_is_reaps_radius_and_the_attack_reach():
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    reap = ability("Reap")
    assert reap.slot == "Basic" and reap.shape == "Strike"
    assert math.isclose(constant("DesignedMeleeReachCm"), float(reap.params["Radius"]) * CM_PER_METRE)
    assert math.isclose(constant("DesignedMeleeReachCm"), ATTACK_REACH["Wraith"] * CM_PER_METRE)


def test_reaps_arc_and_catch_are_the_models():
    reap = ability("Reap")
    assert math.isclose(constant("ReapAngleDegrees"), float(reap.params["Angle"]))
    assert constant("ReapMaxTargets") == float(reap.params["MaxTargets"])


def test_reap_is_unmarked_by_the_telegraph_rules():
    """The judgement of 2026-11-26: the interval is short enough that no marker a metre wide fits half of it."""
    from cataclysm_sim.enemy_abilities import is_telegraphed

    assert not is_telegraphed(ability("Reap"), "Wraith")


def test_the_swing_clip_fits_the_interval_under_the_ceiling():
    rate = constant("ReapAnimationSeconds") / constant("DesignedAttackIntervalSeconds")
    assert 1.0 < rate <= constant("MaximumPlayRate"), rate


# ---------------------------------------------------------------------------
# Phase Dash
# ---------------------------------------------------------------------------

def test_the_dash_is_the_models():
    from cataclysm_sim.enemy_abilities import wind_up_seconds

    dash = ability("Phase Dash")
    assert dash.slot == "Movement" and dash.shape == "Movement" and dash.params["Mode"] == "Charge"
    assert math.isclose(constant("PhaseDashRangeCm"), float(dash.params["Range"]) * CM_PER_METRE)
    assert math.isclose(constant("PhaseDashRadiusCm"), float(dash.params["Radius"]) * CM_PER_METRE)
    assert math.isclose(constant("PhaseDashCooldownSeconds"), dash.cooldown)
    assert abs(constant("PhaseDashWindUpSeconds") - wind_up_seconds(float(dash.params["Radius"]))) < 0.005


def test_the_dash_is_marked_and_shoves_nothing():
    from cataclysm_sim.enemy_abilities import is_telegraphed

    dash = ability("Phase Dash")
    assert is_telegraphed(dash, "Wraith")
    assert "Knockback" not in dash.params
    assert re.search(r"BeginCharge\([^;]*/\*KnockbackCm=\*/0\.0f\)", source(WRAITH_SOURCE)), (
        "the dash goes through the player, so BeginCharge is handed no knockback")


def test_the_dash_beats_walking_and_runs_in_the_charges_readable_time():
    """The speed is the one figure the model does not state: a judgement, held to what must stay true of it."""
    speed = constant("PhaseDashSpeedCmPerSecond")
    assert speed > constant("DesignedWalkSpeedCmPerSecond")
    assert abs(constant("PhaseDashRangeCm") / speed - 0.7) < 0.001


# ---------------------------------------------------------------------------
# Soul Grab
# ---------------------------------------------------------------------------

def test_soul_grab_is_the_models():
    from cataclysm_sim.enemy_abilities import wind_up_seconds

    grab = ability("Soul Grab")
    assert grab.slot == "Special" and grab.shape == "Strike" and grab.params["ForcedMovement"] == "Pull"
    assert math.isclose(constant("SoulGrabRadiusCm"), float(grab.params["Radius"]) * CM_PER_METRE)
    assert math.isclose(constant("SoulGrabCooldownSeconds"), grab.cooldown)
    assert abs(constant("SoulGrabWindUpSeconds") - wind_up_seconds(float(grab.params["Radius"]))) < 0.005


def test_the_bands_cover_every_distance_without_overlap():
    """Reap to its reach, Soul Grab from there to its radius, the dash from there to its range: the design's claim that
    a player at any distance has something coming. The C++ makes the dash's shortest use Soul Grab's radius."""
    reach = constant("DesignedMeleeReachCm")
    grab = constant("SoulGrabRadiusCm")
    dash_range = constant("PhaseDashRangeCm")
    assert reach < grab < dash_range
    assert re.search(r"PhaseDashMinimumRangeCm\s*=\s*SoulGrabRadiusCm\s*;", source(WRAITH_HEADER))
    assert re.search(r"SoulGrab\.MinRangeCm\s*=\s*DesignedMeleeReachCm\s*;", source(WRAITH_SOURCE))


def test_three_abilities_the_most_a_basic_creature_may_have():
    from cataclysm_sim.enemy_abilities import abilities

    assert [each.name for each in abilities("Wraith")] == ["Reap", "Phase Dash", "Soul Grab"]


# ---------------------------------------------------------------------------
# The Reaper, which wears it
# ---------------------------------------------------------------------------

def test_the_reaper_walks_slower_than_the_slowest_class():
    """The Reaper's row: "slowly stalks the player". The Warden's 2.8 metres a second, the slowest class's 3.5."""
    from cataclysm_sim.enemy_stats import archetype

    reaper = constant("ReaperWalkSpeedCmPerSecond")
    assert math.isclose(reaper, archetype("Abyssal Warden").move_speed * CM_PER_METRE)
    assert reaper < 350.0


# ---------------------------------------------------------------------------
# The game mode's fallback figures
# ---------------------------------------------------------------------------

def test_the_fallback_figures_are_the_models_at_tier_one():
    from cataclysm_sim.enemy_stats import stats_on_floor

    stats = stats_on_floor("Common", 1, "Cataclysm", kind="Wraith")
    assert constant("WraithHealth", GAME_MODE_HEADER) == round(stats.health)
    assert math.isclose(constant("WraithArmour", GAME_MODE_HEADER), stats.armor)
    assert math.isclose(constant("WraithAttackDamage", GAME_MODE_HEADER), 20.0 * wraith().damage_share)
