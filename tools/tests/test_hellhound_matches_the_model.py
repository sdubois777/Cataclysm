"""The Hellhound's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmHellhoundCharacter.h` hard-codes
nineteen numbers that also live in the Python model. Two copies of one number
drift, and in this repository they have.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Hellhound"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ABILITIES["Hellhound"]` and
`ATTACK_REACH["Hellhound"]` in `sim/cataclysm_sim/enemy_abilities.py` are where
the creature is designed. When this file fails, the usual fix is to change the
C++.

WHY IT EXISTS AT ALL RATHER THAN AN ENGINE TEST. Continuous integration never
builds the C++ and never opens the editor, so an automation test cannot run on a
pull request. This reads the source text, which is present either way.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second because
the design document does; Unreal works in centimetres. The factor of 100 is
stated at each conversion rather than hidden in a helper.

THE ONE NUMBER THIS FILE CANNOT CHECK AGAINST THE MODEL is the charge's speed.
`ABILITIES["Hellhound"]` does not state one, and unlike the Abyssal Warden's it
cannot be derived from a clip, because the Paragon Iggy and Scorch pack holds no
charge animation. It is checked here against the two things that must stay true
of it whatever it is: it has to beat walking, and it has to cross its range in a
time a player can read.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SIM = REPO_ROOT / "sim"
if str(SIM) not in sys.path:
    sys.path.insert(0, str(SIM))

CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
HELLHOUND_HEADER = CHARACTER_DIR / "CataclysmHellhoundCharacter.h"
HELLHOUND_SOURCE = CHARACTER_DIR / "CataclysmHellhoundCharacter.cpp"
WARDEN_HEADER = CHARACTER_DIR / "CataclysmAbyssalWardenCharacter.h"
ASSET_NOTES = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path = HELLHOUND_HEADER) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(
            f"{path.name} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, this guard has "
            f"nothing left to check and the number is unguarded.")
    return float(match.group(1))


def hellhound():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Hellhound")


def ability(name: str):
    from cataclysm_sim.enemy_abilities import abilities

    for entry in abilities("Hellhound"):
        if entry.name == name:
            return entry
    pytest.fail(
        f"the Hellhound has no ability called {name!r} in ABILITIES in "
        f"sim/cataclysm_sim/enemy_abilities.py. If it was renamed, rename it "
        f"here too.")


def hellrush():
    return ability("Hellrush")


# --------------------------------------------------------------------------
# The profile, straight off the archetype
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, model_value, scale, what", [
    ("DesignedAttackIntervalSeconds", "attack_interval", 1.0,
     "seconds between bites"),
    ("DesignedResistancePercent", "resistance", 1.0,
     "percent of all incoming damage resisted"),
    ("DesignedCritChancePercent", "crit_chance", 1.0, "critical strike chance"),
    ("DesignedCritMultiplierPercent", "crit_multiplier", 1.0,
     "critical strike multiplier"),
    ("DesignedEvasionPercent", "evasion", 1.0, "evasion"),
    ("DesignedWalkSpeedCmPerSecond", "move_speed", CM_PER_METRE,
     "how fast it moves"),
    ("DesignedTurnRateDegreesPerSecond", "turn_rate_degrees", 1.0,
     "how fast it turns"),
])
def test_the_profile_matches_the_archetype(cpp_name, model_value, scale, what):
    designed = getattr(hellhound(), model_value) * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} in CataclysmHellhoundCharacter.h is {constant(cpp_name)} "
        f"and the model designs {designed} for {what} "
        f"({model_value} in ARCHETYPES['Hellhound']). The model is "
        f"authoritative.")


def test_the_reach_matches_the_ability_table():
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    designed = ATTACK_REACH["Hellhound"] * CM_PER_METRE
    assert constant("DesignedMeleeReachCm") == pytest.approx(designed), (
        "DesignedMeleeReachCm in CataclysmHellhoundCharacter.h has drifted "
        "from ATTACK_REACH['Hellhound'] in "
        "sim/cataclysm_sim/enemy_abilities.py.")


def test_the_capsule_radius_is_the_designed_body_radius():
    """AND THE DESIGNED BODY RADIUS IS THE DEFAULT NOBODY MEASURED.

    `body_radius` is 0.48 for this creature, which is the figure the model gives
    anything that has not been measured -- issue #366. The capsule matching it is
    still the right thing to check: the two must agree whatever the number turns
    out to be, and this is what notices when somebody finally measures it and
    changes only one of them.
    """
    designed = hellhound().body_radius * CM_PER_METRE
    assert constant("HellhoundCapsuleRadius") == pytest.approx(designed), (
        "HellhoundCapsuleRadius in CataclysmHellhoundCharacter.h has drifted "
        "from body_radius in ARCHETYPES['Hellhound'].")


def test_it_has_no_separate_chase_speed():
    """`chase_speed` is 0.0, so it moves at one figure whether or not it has seen
    anything -- the same arrangement the Abyssal Warden has and the opposite of
    the Brute's.

    CHECKED IN THE C++ AS WELL AS IN THE MODEL, because a chase speed added to
    the class without one in the model would be a creature that speeds up for a
    reason nothing designed.
    """
    assert hellhound().chase_speed == pytest.approx(0.0), (
        "the model now gives the Hellhound a chase speed, so the class needs "
        "one too and this test needs rewriting.")

    assert "ChaseSpeed" not in source(HELLHOUND_HEADER), (
        "CataclysmHellhoundCharacter.h mentions a chase speed and the model "
        "designs none.")


def test_it_is_the_fastest_creature_in_the_slice():
    """WHAT MAKES THIS CREATURE WHAT IT IS. The roster calls it "an aggressive
    charger", and a charger the player can walk away from is not one.

    WRITTEN AS A COMPARISON RATHER THAN AS A LITERAL, so it still means something
    if the numbers move. What it guards is the ORDERING: this creature has to be
    faster than the two that exist, or its design has quietly gone.
    """
    from cataclysm_sim.enemy_stats import archetype

    mine = hellhound().move_speed
    for other in ("Brute", "Abyssal Warden"):
        assert mine > archetype(other).move_speed, (
            f"the Hellhound moves at {mine} m/s and the {other} at "
            f"{archetype(other).move_speed}. The Hellhound is designed to be "
            f"the fastest thing in the roster; if that changed, its class "
            f"comments say the opposite.")


# --------------------------------------------------------------------------
# Maul, the bite
# --------------------------------------------------------------------------

def test_the_bite_is_not_telegraphed():
    """The model's note says why: "a 1.1 second attack interval allows a 0.5
    metre marker, which is smaller than the animal standing in it".

    SO THERE MUST BE NO MARKER RADIUS FOR IT IN THE CLASS. A basic attack does
    not go through EnemyAbilities at all -- it is MeleeReachCm plus
    AttackIntervalSeconds -- and this is what notices if somebody adds one.
    """
    from cataclysm_sim.enemy_abilities import is_telegraphed

    assert not is_telegraphed(ability("Maul"), "Hellhound"), (
        "the model now telegraphs Maul, so the creature needs a marker for it "
        "and the class draws none.")

    assert "MaulMarkerRadiusCm" not in source(HELLHOUND_HEADER), (
        "CataclysmHellhoundCharacter.h draws a marker for Maul and the model "
        "says it is not telegraphed.")


def test_the_bite_fits_inside_the_interval_between_bites():
    """MEASURED, NOT ASSUMED. `tools/probe_hellhound_animation.py` read
    Scorch_Primary_Fire_Med at 0.9667 seconds from the asset on 2026-08-20.

    A clip longer than the interval means one bite is still playing when the
    next begins. The header carries a static_assert for the same thing, and this
    is the copy continuous integration can run.
    """
    clip = constant("MaulAnimationSeconds")
    interval = constant("DesignedAttackIntervalSeconds")
    assert clip < interval, (
        f"the Hellhound's bite runs {clip} s and its bites are {interval} s "
        f"apart, so one would still be playing when the next began.")


# --------------------------------------------------------------------------
# Hellrush, the charge
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, param, scale, what", [
    ("HellrushRangeCm", "Range", CM_PER_METRE, "how far it charges"),
    ("HellrushRadiusCm", "Radius", CM_PER_METRE, "half the lane's width"),
    ("HellrushKnockbackCm", "Knockback", CM_PER_METRE, "how far it shoves"),
    ("HellrushGroundRadiusCm", "GroundRadius", CM_PER_METRE,
     "half the width of the lane it leaves burning"),
    ("HellrushGroundSeconds", "GroundDuration", 1.0, "how long the lane burns"),
    ("HellrushGroundPercent", "GroundPercent", 1.0,
     "what one second in the fire is worth"),
])
def test_the_charge_matches_the_designed_ability(cpp_name, param, scale, what):
    assert param in hellrush().params, (
        f"Hellrush no longer states {param} in "
        f"sim/cataclysm_sim/enemy_abilities.py, so {cpp_name} is unguarded.")

    designed = float(hellrush().params[param]) * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} in CataclysmHellhoundCharacter.h is {constant(cpp_name)} "
        f"and the model designs {designed} for {what}. The model is "
        f"authoritative.")


def test_the_charge_cooldown_matches_the_designed_ability():
    assert constant("HellrushCooldownSeconds") == pytest.approx(
        hellrush().cooldown), (
        "HellrushCooldownSeconds has drifted from Hellrush's cooldown in "
        "sim/cataclysm_sim/enemy_abilities.py.")


def test_the_charge_is_a_charge_and_not_a_leap_or_a_blink():
    """The three Movement modes behave differently and the class picks one.

    A Charge travels and hits everything along the way, which is why it can leave
    a lane of fire behind it. A Leap hits only where it lands and a Blink hits
    both ends, and either would make the burning lane a different thing.
    """
    assert hellrush().params.get("Mode") == "Charge", (
        f"Hellrush is a {hellrush().params.get('Mode')} in the model and the "
        f"class builds a charge.")

    assert "BeginCharge" in source(HELLHOUND_SOURCE), (
        "CataclysmHellhoundCharacter.cpp no longer calls BeginCharge, so "
        "Hellrush does not travel.")


def test_the_charge_wind_up_is_the_formula_rather_than_a_number():
    """0.4 + Radius / 3.5, from the Attack Telegraphs subsection of the design
    document and `wind_up_seconds` in the model.

    A WIND-UP TYPED IN BY HAND is the thing this catches: it would be right on
    the day it was typed and would stop tracking the radius the moment that
    moved.
    """
    from cataclysm_sim.enemy_abilities import wind_up_seconds

    radius = float(hellrush().params["Radius"])
    designed = wind_up_seconds(radius)

    assert constant("HellrushWindUpSeconds") == pytest.approx(designed, abs=0.005), (
        f"HellrushWindUpSeconds is {constant('HellrushWindUpSeconds')} and the "
        f"formula gives {designed:.4f} for a {radius} metre radius.")


def test_the_charge_draws_the_same_lane_it_hits_and_burns():
    """ONE CONSTANT FOR ALL THREE, so what the player is shown is what hurts.

    A marker drawn from a different number than the damage sweeps is the failure
    this catches, and it is invisible until somebody stands just outside a lane
    and is hit anyway.
    """
    assert constant("HellrushRadiusCm") == pytest.approx(
        constant("HellrushGroundRadiusCm")), (
        "Hellrush's lane and the fire it leaves are different widths, so the "
        "marker cannot describe both.")


def test_the_charge_beats_walking_during_its_own_wind_up():
    """A charge covering less ground than the creature could walk while it winds
    up is strictly worse than not winding up at all.

    THIS CREATURE IS THE ONE WHERE THAT NEARLY BITES. It walks at 7.5 metres per
    second, so 0.83 seconds of wind-up covers 6.2 of its 10 metre range and only
    the last 3.8 metres are worth charging.
    """
    walk = constant("DesignedWalkSpeedCmPerSecond")
    wind_up = constant("HellrushWindUpSeconds")
    reach = constant("HellrushRangeCm")

    assert walk * wind_up < reach, (
        f"the Hellhound walks {walk * wind_up:.1f} cm during Hellrush's "
        f"{wind_up} s wind-up and Hellrush covers {reach} cm, so charging is "
        f"never worth it.")

    assert constant("HellrushMinimumRangeCm") == pytest.approx(
        walk * wind_up, abs=0.01), (
        "HellrushMinimumRangeCm has drifted from the walk speed and wind-up it "
        "is derived from.")


def test_the_charge_speed_beats_walking_and_reads_like_the_other_one():
    """THE ONE NUMBER ON THIS CREATURE THAT IS NOT IN THE MODEL.

    The Abyssal Warden's charge speed falls out of its clip length; this creature
    has no charge clip, so the number was chosen. What can still be checked is
    the two things that have to be true of it whatever it is.
    """
    speed = constant("HellrushSpeedCmPerSecond")
    walk = constant("DesignedWalkSpeedCmPerSecond")

    assert speed > walk, (
        f"Hellrush travels at {speed} cm/s and the creature walks at {walk}, so "
        f"charging is slower than running and the telegraph buys the player a "
        f"free escape.")

    # THE DURATION IS WHAT A PLAYER READS once the marker has gone, and this
    # creature's was chosen to match the Abyssal Warden's so the two charges read
    # the same. If either moves, this says so.
    mine = constant("HellrushRangeCm") / speed
    warden = (constant("StampedeRangeCm", WARDEN_HEADER)
              / constant("StampedeSpeedCmPerSecond", WARDEN_HEADER))

    assert mine == pytest.approx(warden, abs=0.01), (
        f"Hellrush takes {mine:.3f} s to cross its range and the Abyssal "
        f"Warden's Stampede takes {warden:.3f} s. The Hellhound's speed was "
        f"chosen to make the two the same, because a charge's duration is what "
        f"a player reads. If that is no longer wanted, say so in the header "
        f"rather than leaving this failing.")


def test_the_burning_lane_is_the_only_thing_that_burns_its_own_side():
    """`GroundHitsAllies=1`, and the model's note: "The fire burns other enemies
    and the Hellhound itself."

    NO OTHER ABILITY IN THE MODEL SAYS IT, which is what makes this worth
    guarding: the ground zone gained a whole option for one creature, and if that
    creature stops needing it the option is dead code.
    """
    from cataclysm_sim.enemy_abilities import ABILITIES

    assert hellrush().params.get("GroundHitsAllies") == 1, (
        "Hellrush no longer burns its own side, so ACataclysmGroundZone's "
        "bBurnsEveryone has no caller left.")

    others = [
        (enemy, entry.name)
        for enemy, entries in ABILITIES.items()
        for entry in entries
        if entry.params.get("GroundHitsAllies") and enemy != "Hellhound"
    ]
    assert not others, (
        f"something other than the Hellhound now leaves fire that burns its own "
        f"side: {others}. That is fine, and the comments saying the Hellhound is "
        f"the only one need changing.")

    assert "bBurnsEveryone=*/true" in source(HELLHOUND_SOURCE).replace(" ", ""), (
        "CataclysmHellhoundCharacter.cpp no longer asks for a lane that burns "
        "everyone, so the fire would spare the creature that lit it.")


# --------------------------------------------------------------------------
# What its art can and cannot do
# --------------------------------------------------------------------------

def test_the_walk_needs_a_play_rate_the_project_allows():
    """MEASURED, NOT GUESSED. `tools/measure_animation_stride.py` read the jog at
    302.6 cm/s on 2026-08-20, with the idle at 0.0 as the control.

    **THIS IS THE TIGHTEST FIT IN THE PROJECT.** 750 / 302.6 is 2.478 against a
    ceiling of 2.5. The header carries a static_assert for the same thing; this
    is the copy continuous integration can run, and it is what notices if the
    designed speed is raised even slightly.
    """
    needed = (constant("DesignedWalkSpeedCmPerSecond")
              / constant("AuthoredJogSpeedCmPerSecond"))
    ceiling = constant("MaximumPlayRate")

    assert needed <= ceiling, (
        f"the Hellhound's walk needs a play rate of {needed:.3f} and the "
        f"ceiling is {ceiling}. Above it the clip is clamped and its feet "
        f"slide. There is no faster clip in the pack -- Travelmode_Fwd measures "
        f"268.1 cm/s -- so the answer is an animation Blueprint or a slower "
        f"designed speed.")


def test_the_asset_notes_record_what_was_measured():
    """The durable record of a measurement is the notes, not a comment in one
    class. A figure in the C++ that the notes do not carry is one nobody can
    check without opening the editor again.
    """
    notes = source(ASSET_NOTES)

    for figure in ("302.6", "0.9667", "1.6667"):
        assert figure in notes, (
            f"game/docs/enemy-source-assets.md does not record {figure}, which "
            f"CataclysmHellhoundCharacter.h depends on. Measurements belong in "
            f"the notes as well as in the class.")
