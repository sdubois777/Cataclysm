"""The Gatekeeper's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmGatekeeperCharacter.h` hard-codes the
creature's whole stat block, its two phase thresholds and all four of its
abilities, and every one of those numbers also lives in the Python model. Two
copies of one number drift, and in this repository they have.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Gatekeeper"]` in
`sim/cataclysm_sim/enemy_stats.py`, and `ABILITIES["Gatekeeper"]`,
`ATTACK_REACH["Gatekeeper"]` and `PHASE_TRANSITIONS["Gatekeeper"]` in
`sim/cataclysm_sim/enemy_abilities.py`, are where the creature is designed. When
this file fails, the usual fix is to change the C++.

WHY IT EXISTS AT ALL RATHER THAN AN ENGINE TEST. Continuous integration never
builds the C++ and never opens the editor, so `CataclysmGatekeeperTests.cpp`
cannot run on a pull request. This reads the source text, which is present either
way, and it is therefore the only thing checking these numbers when the work is
reviewed.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second; Unreal
works in centimetres. The factor of 100 is stated at each conversion.

WHAT THIS CREATURE HAS THAT NO OTHER DOES is phases. It is the only entry in
`PHASE_TRANSITIONS`, so the checks below about which ability arrives when have no
counterpart in the other six creatures' files.

ONE FIGURE IN THIS CREATURE IS A GUESS. The walk play rate is 1.0 because
`tools/measure_animation_stride.py` failed its own control on the Sevarog rig.
`test_the_walk_play_rate_is_still_recorded_as_a_guess` is what keeps that stated
rather than quietly forgotten; issue #778 carries the evidence.
"""

from __future__ import annotations

import itertools
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
GATEKEEPER_HEADER = CHARACTER_DIR / "CataclysmGatekeeperCharacter.h"
GATEKEEPER_SOURCE = CHARACTER_DIR / "CataclysmGatekeeperCharacter.cpp"
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")
GAME_MODE_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.cpp")
SKILL_SHAPE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm"
                      / "AbilitySystem" / "CataclysmSkillShape.h")
ASSET_NOTES = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
SKILL_SLOTS = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
LEVEL_SCRIPT = REPO_ROOT / "tools" / "generate_input_assets.py"

CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def without_comments(text: str) -> str:
    """C++ with every comment removed.

    A SOURCE-READING TEST MUST STRIP COMMENTS FIRST, or it reads prose as code.
    This creature's header is more comment than code -- 660 lines of which the
    great majority is documentation -- and several of those comments quote the
    very constants the checks below look for. `test_the_comment_stripping_really_strips`
    is the negative control that says this works.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def code(path: pathlib.Path) -> str:
    return without_comments(source(path))


def constant(name: str, path: pathlib.Path = GATEKEEPER_HEADER) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        code(path))
    if match is None:
        pytest.fail(
            f"{path.name} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, this guard has "
            f"nothing left to check and the number is unguarded.")
    return float(match.group(1))


def whole_number_constant(name: str,
                          path: pathlib.Path = GATEKEEPER_HEADER) -> int:
    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        code(path))
    if match is None:
        pytest.fail(f"{path.name} has no "
                    f"'static constexpr int32 {name} = <number>;' line.")
    return int(match.group(1))


def text_constant(name: str, path: pathlib.Path = GATEKEEPER_SOURCE) -> str:
    """The value of a `const TCHAR* Class::<name> = TEXT("...");` line."""
    match = re.search(
        rf"{re.escape(name)}\s*=\s*\n?\s*TEXT\(\"([^\"]*)\"\)\s*;", code(path))
    if match is None:
        pytest.fail(f"{path.name} has no '{name} = TEXT(\"...\");' line.")
    return match.group(1)


def property_default(name: str, path: pathlib.Path = GAME_MODE_HEADER) -> float:
    match = re.search(
        rf"\bfloat\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        code(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'float {name} = <number>f;' line.")
    return float(match.group(1))


def whole_number_property(name: str,
                          path: pathlib.Path = GAME_MODE_HEADER) -> int:
    match = re.search(
        rf"\bint32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;", code(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'int32 {name} = <number>;' line.")
    return int(match.group(1))


def gatekeeper():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Gatekeeper")


def ability(name: str):
    from cataclysm_sim.enemy_abilities import abilities

    for entry in abilities("Gatekeeper"):
        if entry.name == name:
            return entry
    pytest.fail(
        f"the Gatekeeper has no ability called {name!r} in ABILITIES in "
        f"sim/cataclysm_sim/enemy_abilities.py. If it was renamed, rename it "
        f"here too.")


def cleave():
    return ability("Dread Cleave")


def soulfall():
    return ability("Soulfall")


def call_the_damned():
    return ability("Call the Damned")


def soul_harvest():
    return ability("Soul Harvest")


def slot_damage_percent(slot: str) -> float:
    """What one use of an ability in this slot is worth, from the design data."""
    match = re.search(rf"^{re.escape(slot)},{re.escape(slot)},(\d+(?:\.\d+)?),",
                      source(SKILL_SLOTS), re.MULTILINE)
    if match is None:
        pytest.fail(
            f"game/Data/SkillSlots.csv has no {slot} row, so nothing says what "
            f"a {slot} ability is worth.")
    return float(match.group(1))


def returned_ability_variables() -> list[str]:
    """The names in `return {A, B, C, D};` at the end of EnemyAbilities."""
    match = re.search(r"return\s*\{([^}]*)\};", code(GATEKEEPER_SOURCE))
    if match is None:
        pytest.fail(
            "CataclysmGatekeeperCharacter.cpp no longer returns a "
            "brace-enclosed list from EnemyAbilities, so this guard cannot "
            "read the array or its order.")
    return [name.strip() for name in match.group(1).split(",")]


def test_the_comment_stripping_really_strips():
    """The control for every check in this file. A guard that reads prose as
    code passes whatever the code says, and this creature's header quotes its
    own constants inside its documentation several times over."""
    assert without_comments(
        "// static constexpr float DreadCleaveRadiusCm = 999.0f;\n").strip() == ""
    assert without_comments(
        "/* static constexpr float DreadCleaveRadiusCm = 999.0f; */").strip() == ""
    assert "DreadCleaveRadiusCm" in without_comments(
        "static constexpr float DreadCleaveRadiusCm = 200.0f;  // a comment")


# --------------------------------------------------------------------------
# The profile, straight off the archetype
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,model_value,scale,what", [
    ("DesignedAttackIntervalSeconds", "attack_interval", 1.0,
     "seconds between sweeps"),
    ("DesignedResistancePercent", "resistance", 1.0,
     "percent of every hit resisted"),
    ("DesignedCritChancePercent", "crit_chance", 1.0,
     "percent of hits that critically strike"),
    ("DesignedCritMultiplierPercent", "crit_multiplier", 1.0,
     "what a critical strike is worth"),
    ("DesignedEvasionPercent", "evasion", 1.0, "percent of hits evaded"),
    ("DesignedEnergyShieldFraction", "energy_shield_fraction", 1.0,
     "the shield as a fraction of health"),
    ("DesignedWalkSpeedCmPerSecond", "move_speed", CM_PER_METRE,
     "how fast it walks"),
    ("DesignedTurnRateDegreesPerSecond", "turn_rate_degrees", 1.0,
     "how fast it turns"),
])
def test_the_profile_matches_the_archetype(cpp_name, model_value, scale, what):
    designed = getattr(gatekeeper(), model_value) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and the model's {model_value} is "
        f"{designed} ({what}). sim/cataclysm_sim/enemy_stats.py is "
        f"authoritative.")


@pytest.mark.parametrize("field,what", [
    ("health_share", "how much health it has"),
    ("damage_share", "how hard it hits"),
    ("crit_multiplier", "what a critical strike is worth"),
    ("attack_interval", "how long it waits between swings"),
])
def test_it_leads_the_roster_where_the_header_says_it_does(field, what):
    """The header names four figures this creature leads on. A test that
    repeats a claim is what stops the claim going stale in silence when another
    creature is tuned."""
    from cataclysm_sim.enemy_stats import ARCHETYPES

    values = {name: getattr(kind, field) for name, kind in ARCHETYPES.items()}

    assert values["Gatekeeper"] == max(values.values()), (
        f"the Gatekeeper's {field} is {values['Gatekeeper']} ({what}) and "
        f"something else in the roster has {max(values.values())}: {values}. "
        f"CataclysmGatekeeperCharacter.h says this creature leads on every one "
        f"of these; if that is a real design change, the header repeats a "
        f"claim that is now wrong.")


@pytest.mark.parametrize("field,leader", [
    ("armor_share", "Abyssal Warden"),
    ("resistance", "Abyssal Warden"),
])
def test_it_is_deliberately_not_the_best_defended(field, leader):
    """**THE TWO FIGURES THIS CREATURE DOES NOT LEAD ON**, and both are
    deliberate. `enemy_stats.py` says beside the Abyssal Warden's resistance
    that it is the one creature the design singles out for it, and says beside
    the Gatekeeper's that this one is high but below it.

    THIS TEST EXISTS BECAUSE THE COMMENTS SAID THE OPPOSITE. Until it was
    written, the class header claimed the Gatekeeper was "the largest thing in
    the game by every measure" and named its armour share among them; the
    resistance constant called 30.0 "the most of anything designed"; and
    `CataclysmGameMode.h` said the same of its armour. All three were wrong and
    nothing had noticed.
    """
    from cataclysm_sim.enemy_stats import ARCHETYPES

    values = {name: getattr(kind, field) for name, kind in ARCHETYPES.items()}

    assert values[leader] > values["Gatekeeper"], (
        f"the Gatekeeper's {field} is {values['Gatekeeper']} and the "
        f"{leader}'s is {values[leader]}: {values}. The design gives the "
        f"{leader} the better of the two and both C++ headers now say so in "
        f"as many words. If that has really changed, change the comments with "
        f"it rather than only this test.")


def test_it_cannot_catch_any_player_class():
    """3.0 m/s against classes at 3.5, 4.0 and 4.6, and it has no chase speed.
    That is what Soulfall exists to answer, so if it ever became able to walk
    somebody down the reason for the lobbed attack would have gone with it."""
    from cataclysm_sim.classes import DEMONIC_CLASSES

    default_speed = 4.0
    speeds = {
        name: (kind.overrides["movement_speed"].base
               if "movement_speed" in kind.overrides else default_speed)
        for name, kind in DEMONIC_CLASSES.items()
    }

    assert gatekeeper().chase_speed == 0.0, (
        f"the Gatekeeper now has a chase speed of {gatekeeper().chase_speed}, "
        f"so it moves faster once it has noticed somebody and the header's "
        f"claim that it can never close a gap is no longer true.")

    assert gatekeeper().move_speed < min(speeds.values()), (
        f"the Gatekeeper moves at {gatekeeper().move_speed} m/s and the "
        f"slowest player class moves at {min(speeds.values())}: {speeds}. The "
        f"header says it is slower than every class and that Soulfall is its "
        f"answer to a player who stands off.")


def test_the_capsule_radius_is_the_designed_body_radius():
    """0.48 is the archetype default rather than a measurement, which for a
    3.11 metre creature is the strongest case in issue #366. It is still what
    the design says, so it is still what the C++ carries."""
    designed = gatekeeper().body_radius * CM_PER_METRE
    written = constant("GatekeeperCapsuleRadius")

    assert written == pytest.approx(designed), (
        f"GatekeeperCapsuleRadius is {written} cm and the model's body_radius "
        f"is {gatekeeper().body_radius} m, which is {designed} cm.")


def test_the_capsule_half_height_comes_from_the_mesh():
    """Measured rather than chosen. Sevarog is 311.1 cm tall, so half is
    155.55 -- and the header carries 155.53, which is what the probe printed."""
    written = constant("GatekeeperCapsuleHalfHeight")
    notes = source(ASSET_NOTES)

    match = re.search(r"\|\s*The Gatekeeper\s*\|\s*(\d+(?:\.\d+)?)\s*cm\s*\|",
                      notes)
    assert match is not None, (
        "game/docs/enemy-source-assets.md no longer records the Gatekeeper's "
        "mesh height, so nothing says where the capsule's half-height came "
        "from.")

    height = float(match.group(1))
    assert written == pytest.approx(height / 2.0, abs=0.05), (
        f"GatekeeperCapsuleHalfHeight is {written} and the recorded mesh "
        f"height is {height} cm, half of which is {height / 2.0}.")


def test_it_is_the_tallest_creature_in_the_project():
    """The header and the asset notes both say so in as many words."""
    notes = source(ASSET_NOTES)
    heights = {
        match.group(1).strip(): float(match.group(2))
        for match in re.finditer(
            r"\|\s*(The [A-Za-z ]+?)\s*\|\s*(\d+(?:\.\d+)?)\s*cm\s*\|", notes)
    }

    assert "The Gatekeeper" in heights, (
        "game/docs/enemy-source-assets.md records no mesh height for the "
        "Gatekeeper.")
    assert heights["The Gatekeeper"] == max(heights.values()), (
        f"the Gatekeeper is {heights['The Gatekeeper']} cm tall and something "
        f"else is taller: {heights}. Both the header and the asset notes claim "
        f"it is by far the tallest in the project.")


def test_it_notices_as_far_as_its_longest_attack_reaches():
    """It cannot close a gap, so a notice radius shorter than Soulfall's range
    would be ground it can hit and refuses to walk towards. The Corrupted
    Sentinel's rule rather than the walking creatures'."""
    notices = constant("GatekeeperNoticeRadiusCm")
    lobs = constant("SoulfallRangeCm")

    assert notices >= lobs, (
        f"the Gatekeeper notices at {notices} cm and lobs {lobs} cm, so there "
        f"is ground it can hit and will not react to.")


# --------------------------------------------------------------------------
# Its phases
# --------------------------------------------------------------------------

def test_the_phase_thresholds_match_the_design():
    from cataclysm_sim.enemy_abilities import PHASE_TRANSITIONS

    designed = PHASE_TRANSITIONS["Gatekeeper"]
    assert len(designed) == 2, (
        f"PHASE_TRANSITIONS['Gatekeeper'] is {designed}, which makes "
        f"{len(designed) + 1} phases. The C++ names exactly two thresholds, "
        f"SecondPhaseHealthFraction and ThirdPhaseHealthFraction.")

    assert constant("SecondPhaseHealthFraction") == pytest.approx(designed[0])
    assert constant("ThirdPhaseHealthFraction") == pytest.approx(designed[1])


def test_the_thresholds_are_given_to_the_engine_highest_first():
    """`ACataclysmEnemyCharacter::RefreshPhase` counts the thresholds at or
    below the creature's health fraction, so a list in the wrong order would
    put the creature in its last phase at 59% health."""
    body = code(GATEKEEPER_SOURCE)

    match = re.search(r"PhaseHealthFractions\s*=\s*\{([^}]*)\}", body)
    assert match is not None, (
        "CataclysmGatekeeperCharacter.cpp no longer sets "
        "PhaseHealthFractions, so the only creature in the game with phases "
        "has none and never leaves phase 1.")

    listed = [name.strip() for name in match.group(1).split(",")]
    assert listed == ["SecondPhaseHealthFraction", "ThirdPhaseHealthFraction"], (
        f"PhaseHealthFractions is set to {listed}. RefreshPhase requires the "
        f"thresholds highest first.")

    assert (constant("ThirdPhaseHealthFraction")
            < constant("SecondPhaseHealthFraction")), (
        "the third phase's threshold is not below the second's, so the third "
        "phase would begin before the second.")


def test_it_is_the_only_creature_with_phases():
    """Six of the seven are in phase 1 for ever, which is why every ability
    defaults to phase 1 and nothing else in the game sets PhaseHealthFractions."""
    from cataclysm_sim.enemy_abilities import PHASE_TRANSITIONS

    assert list(PHASE_TRANSITIONS) == ["Gatekeeper"], (
        f"{sorted(PHASE_TRANSITIONS)} have designed phases. Only the "
        f"Gatekeeper does today, and a second one would need its own C++ "
        f"class to set PhaseHealthFractions.")


@pytest.mark.parametrize("cpp_name,ability_name", [
    ("CallTheDamnedPhase", "Call the Damned"),
    ("SoulHarvestPhase", "Soul Harvest"),
])
def test_each_late_ability_arrives_in_its_designed_phase(cpp_name, ability_name):
    designed = ability(ability_name).phase
    written = whole_number_constant(cpp_name)

    assert written == designed, (
        f"{cpp_name} is {written} and {ability_name} is designed in phase "
        f"{designed}.")


def test_the_two_opening_abilities_are_available_from_the_start():
    """Dread Cleave and Soulfall are phase 1 in the model, and the C++ writes
    the 1 out rather than leaving it to the field's default, so the array says
    what the design says."""
    assert cleave().phase == 1, (
        f"Dread Cleave is designed in phase {cleave().phase}. It is the basic "
        f"attack; a boss that cannot swing in phase 1 does nothing at all.")
    assert soulfall().phase == 1, (
        f"Soulfall is designed in phase {soulfall().phase}, and the header "
        f"says it is available from the start.")

    body = code(GATEKEEPER_SOURCE)
    for variable in ("Soulfall", "Cleave"):
        assert re.search(rf"\b{variable}\.Phase\s*=\s*1\s*;", body), (
            f"CataclysmGatekeeperCharacter.cpp does not set {variable}.Phase "
            f"to 1. Leaving it to the field's default would give the same "
            f"behaviour and say nothing, and this creature is the one place "
            f"where a phase number is a design decision rather than a default.")


def test_a_later_phase_keeps_every_earlier_ability():
    """`ChooseAbility` skips an ability whose phase is GREATER than the
    creature's, not one whose phase differs. Phases add and never take away,
    which is the research finding the whole boss rests on."""
    controller = code(CHARACTER_DIR / "CataclysmEnemyController.cpp")

    assert re.search(r"if\s*\(\s*Ability\.Phase\s*>\s*Phase\s*\)", controller), (
        "ACataclysmEnemyController::ChooseAbility no longer skips an ability "
        "whose phase is greater than the creature's. If it now compares for "
        "equality, the Gatekeeper loses Dread Cleave and Soulfall on reaching "
        "phase 2 and would stand there summoning.")


# --------------------------------------------------------------------------
# Dread Cleave, the telegraphed basic attack
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,param,scale,what", [
    ("DreadCleaveRadiusCm", "Radius", CM_PER_METRE, "how far the sweep reaches"),
    ("DreadCleaveAngleDegrees", "Angle", 1.0, "how wide the arc is"),
])
def test_the_sweep_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = float(cleave().params[param]) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and Dread Cleave's {param} is "
        f"{cleave().params[param]}, which is {designed} ({what}).")


def test_the_sweep_is_the_basic_attack_and_has_no_cooldown():
    """A zero cooldown is what makes an EnemyAbilities entry a basic attack:
    the creature's own 3.0 second attack interval is then the only thing
    spacing it out."""
    assert cleave().slot == "Basic", (
        f"Dread Cleave's slot is {cleave().slot!r} in the model, and the C++ "
        f"treats it as the basic attack.")
    assert cleave().cooldown == 0.0, (
        f"Dread Cleave's designed cooldown is {cleave().cooldown}, not zero, "
        f"so it is no longer a basic attack.")
    assert constant("DreadCleaveCooldownSeconds") == 0.0, (
        "DreadCleaveCooldownSeconds is not zero, so the sweep would be held "
        "back by a cooldown as well as by the attack interval and the creature "
        "would sometimes do nothing at all.")


def test_the_sweep_is_telegraphed_and_its_wind_up_is_derived():
    """It is the only basic attack in the slice that is telegraphed because it
    HAS to be: two of these kill the reference geared character."""
    from cataclysm_sim.enemy_abilities import is_telegraphed, wind_up_seconds

    assert is_telegraphed(cleave(), gatekeeper()), (
        "the model no longer telegraphs Dread Cleave, and the C++ gives it a "
        "wind-up and a marker.")

    radius = float(cleave().params["Radius"])
    designed = wind_up_seconds(radius)
    written = constant("DreadCleaveWindUpSeconds")

    assert written == pytest.approx(designed, abs=0.002), (
        f"DreadCleaveWindUpSeconds is {written} and the rule "
        f"0.4 + Radius / 3.5 gives {designed:.4f} for a radius of {radius} m.")


def test_the_sweeps_telegraph_fits_inside_half_the_interval():
    """The rule that keeps a marker walkable. A 3.0 second interval allows 1.5
    seconds of warning and the sweep uses 0.97 of it."""
    written = constant("DreadCleaveWindUpSeconds")
    interval = constant("DesignedAttackIntervalSeconds")

    assert written < interval / 2.0, (
        f"the sweep warns for {written} s and half the attack interval is "
        f"{interval / 2.0} s, so the marker is on the ground for more than "
        f"half the cycle and the player has no moment without one.")


def test_the_sweep_is_a_cone_rather_than_a_ring():
    """Standing behind the creature is the answer to it, which is what an
    Angle below 360 means and what separates it from Soul Harvest."""
    angle = float(cleave().params["Angle"])

    assert angle < 360.0, (
        f"Dread Cleave's designed Angle is {angle}, so it is a full circle and "
        f"there is no getting behind this creature at all. The header and the "
        f"design both say a 120 degree cone can be walked around.")
    assert constant("DreadCleaveAngleDegrees") < 360.0


def test_the_sweep_hits_everything_standing_in_it():
    """Unlike the other six creatures' basic attacks, which state MaxTargets.
    A boss swing hits the crowd, and the model says so by leaving the
    parameter out."""
    assert "MaxTargets" not in cleave().params, (
        f"Dread Cleave now states MaxTargets={cleave().params['MaxTargets']}, "
        f"and the C++ caps nothing: StrikeAround damages every actor the cone "
        f"search returns. One of the two has to change.")


def test_the_sweep_damage_is_the_basic_slots_percentage():
    assert constant("DreadCleaveDamagePercent") == pytest.approx(
        slot_damage_percent("Basic")), (
        f"DreadCleaveDamagePercent is {constant('DreadCleaveDamagePercent')} "
        f"and the Basic row of game/Data/SkillSlots.csv is "
        f"{slot_damage_percent('Basic')}.")


# --------------------------------------------------------------------------
# Soulfall, the lobbed gout and the ground it leaves
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,param,scale,what", [
    ("SoulfallRangeCm", "Range", CM_PER_METRE, "how far it lobs"),
    ("SoulfallRadiusCm", "Radius", CM_PER_METRE, "how wide it bursts"),
    ("SoulfallApexFraction", "Arc", 1.0, "how far the gout sags"),
    ("SoulfallGroundRadiusCm", "GroundRadius", CM_PER_METRE,
     "how wide the burning ground is"),
    ("SoulfallGroundSeconds", "GroundDuration", 1.0,
     "how long the burning ground lasts"),
    ("SoulfallGroundPercent", "GroundPercent", 1.0,
     "what one second in it costs"),
])
def test_the_gout_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = float(soulfall().params[param]) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and Soulfall's {param} is "
        f"{soulfall().params[param]}, which is {designed} ({what}).")


def test_the_gout_passes_through_nothing():
    designed = int(soulfall().params["Pierce"])
    written = whole_number_constant("SoulfallPierce")

    assert written == designed, (
        f"SoulfallPierce is {written} and Soulfall's Pierce is {designed}.")


def test_the_gout_cooldown_matches_the_designed_ability():
    assert constant("SoulfallCooldownSeconds") == pytest.approx(
        soulfall().cooldown), (
        f"SoulfallCooldownSeconds is {constant('SoulfallCooldownSeconds')} and "
        f"the designed cooldown is {soulfall().cooldown}.")


def test_the_gout_wind_up_is_derived_from_its_radius():
    from cataclysm_sim.enemy_abilities import is_telegraphed, wind_up_seconds

    assert is_telegraphed(soulfall(), gatekeeper()), (
        "the model no longer telegraphs Soulfall, and the C++ marks the circle "
        "it lands in.")

    designed = wind_up_seconds(float(soulfall().params["Radius"]))
    written = constant("SoulfallWindUpSeconds")

    assert written == pytest.approx(designed, abs=0.002), (
        f"SoulfallWindUpSeconds is {written} and the rule 0.4 + Radius / 3.5 "
        f"gives {designed:.4f}.")


def test_the_gout_is_lobbed_rather_than_shot_flat():
    """It states an Arc and no Speed, which is what makes the marker a circle
    where it lands rather than a lane along the way. The C++ passes a flight
    time and a speed of zero for the same reason."""
    from cataclysm_sim.enemy_abilities import is_lobbed

    assert is_lobbed(soulfall()), (
        f"Soulfall's params are {soulfall().params} and nothing in them makes "
        f"it a lob. A flat Projectile marks a lane and needs no minimum range, "
        f"and the C++ gives it both an arc and a minimum.")
    assert "Speed" not in soulfall().params, (
        "Soulfall now states a Speed as well as an Arc. A projectile states "
        "one or the other; the C++ passes InSpeed=0 and a flight time.")

    body = code(GATEKEEPER_SOURCE)
    assert "SoulfallFlightSecondsFor(AimedAt)" in body, (
        "CataclysmGatekeeperCharacter.cpp no longer passes a flight time to "
        "ACataclysmProjectile::Fire. A projectile given neither a speed nor a "
        "flight time is a beam, so the gout would arrive instantly.")


def test_the_gout_will_not_be_lobbed_at_the_creatures_own_feet():
    """DERIVED, NOT CHOSEN: marked radius plus the caster's body radius. Below
    that the circle covers the ground the creature stands on, which is a melee
    attack wearing a thrown attack's telegraph. Issue #475 on the Brute."""
    from cataclysm_sim.enemy_abilities import lob_minimum_range

    designed = lob_minimum_range(soulfall(), gatekeeper()) * CM_PER_METRE
    written = constant("SoulfallMinimumRangeCm")

    assert written == pytest.approx(designed), (
        f"SoulfallMinimumRangeCm is {written} cm and the rule gives "
        f"{designed} cm, which is Soulfall's {soulfall().params['Radius']} m "
        f"radius plus the creature's {gatekeeper().body_radius} m body.")


def test_the_burning_ground_lasts_exactly_its_own_cooldown():
    """The whole design of the ability. Shorter and the arena stops shrinking;
    longer and the patches accumulate faster than they expire."""
    assert (float(soulfall().params["GroundDuration"])
            == pytest.approx(soulfall().cooldown)), (
        f"Soulfall's GroundDuration is {soulfall().params['GroundDuration']} "
        f"and its cooldown is {soulfall().cooldown}. The design says these are "
        f"the same number and the header repeats the claim.")

    assert constant("SoulfallGroundSeconds") == pytest.approx(
        constant("SoulfallCooldownSeconds"))


def test_standing_in_the_burning_ground_for_its_life_costs_one_hit():
    """The project's rule is 100 / GroundDuration percent per second. The
    Hellhound's 25 over 4 seconds is the same arithmetic."""
    percent = constant("SoulfallGroundPercent")
    seconds = constant("SoulfallGroundSeconds")

    assert percent * seconds == pytest.approx(100.0, abs=1.0), (
        f"the burning ground deals {percent}% a second for {seconds} seconds, "
        f"which is {percent * seconds}% of an ordinary hit over its whole "
        f"life. The rule is 100.")


def test_the_burning_ground_spares_the_creatures_own_side():
    """**A creature does not burn itself or its own side.** Set by the project
    owner on 2026-08-20, and Soulfall carried `GroundHitsAllies=1` until that
    day, so the summoned Imps of phase 2 burned in it.

    THE ENGINE SIDE OF IT, which the Hellhound's own test cannot see: the
    round `ACataclysmGroundZone::Spawn` makes a zone that knows whose side it
    is on, and `SpawnAlong` is the only entry point that can be told to burn
    everything.
    """
    assert not soulfall().params.get("GroundHitsAllies"), (
        "Soulfall's design says its burning ground hits allies again. If that "
        "rule has changed it changed in docs/Cataclysm_GDD_v2.md and in "
        "docs/DECISIONS.md first, and CataclysmGroundZone.h says so too.")

    body = code(GATEKEEPER_SOURCE)
    assert "ACataclysmGroundZone::Spawn(" in body, (
        "CataclysmGatekeeperCharacter.cpp no longer calls "
        "ACataclysmGroundZone::Spawn, so Soulfall leaves no burning ground at "
        "all -- which is most of what the ability is for.")
    assert "SpawnAlong" not in body, (
        "CataclysmGatekeeperCharacter.cpp now calls "
        "ACataclysmGroundZone::SpawnAlong. That is the long, lane-shaped zone "
        "and the only one that can be told to burn everything standing in it, "
        "including this creature's own Imps.")
    assert "bBurnsEveryone" not in body, (
        "CataclysmGatekeeperCharacter.cpp mentions bBurnsEveryone. Soulfall "
        "leaves the ordinary kind of ground zone, which knows whose side it is "
        "on and needs no such argument.")


def test_the_gout_damage_is_the_special_slots_percentage():
    assert soulfall().slot == "Special", (
        f"Soulfall's slot is {soulfall().slot!r}, and the C++ pays it the "
        f"Special slot's percentage.")
    assert constant("SoulfallDamagePercent") == pytest.approx(
        slot_damage_percent("Special")), (
        f"SoulfallDamagePercent is {constant('SoulfallDamagePercent')} and the "
        f"Special row of game/Data/SkillSlots.csv is "
        f"{slot_damage_percent('Special')}.")


# --------------------------------------------------------------------------
# Call the Damned, the phase 2 summon
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,param,scale,what", [
    ("CallTheDamnedRangeCm", "Range", CM_PER_METRE,
     "how far from itself the Imps claw out"),
    ("CallTheDamnedRadiusCm", "Radius", CM_PER_METRE,
     "how far the ground opens"),
])
def test_the_summon_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = float(call_the_damned().params[param]) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and Call the Damned's {param} is "
        f"{call_the_damned().params[param]}, which is {designed} ({what}).")


@pytest.mark.parametrize("cpp_name,param,what", [
    ("CallTheDamnedCount", "Count", "how many arrive per cast"),
    ("CallTheDamnedMaxAlive", "MaxActive", "how many may be alive at once"),
])
def test_the_summon_counts_match_the_designed_ability(cpp_name, param, what):
    designed = int(call_the_damned().params[param])
    written = whole_number_constant(cpp_name)

    assert written == designed, (
        f"{cpp_name} is {written} and Call the Damned's {param} is {designed} "
        f"({what}).")


def test_the_summon_cooldown_matches_the_designed_ability():
    assert constant("CallTheDamnedCooldownSeconds") == pytest.approx(
        call_the_damned().cooldown), (
        f"CallTheDamnedCooldownSeconds is "
        f"{constant('CallTheDamnedCooldownSeconds')} and the designed cooldown "
        f"is {call_the_damned().cooldown}.")


def test_the_cap_leaves_room_for_more_than_one_cast():
    """Count 3 against MaxActive 6 is two casts of headroom, and it is what
    makes killing the Imps worthwhile: dead ones are only replaced on the next
    cast, ten seconds later."""
    per_cast = whole_number_constant("CallTheDamnedCount")
    cap = whole_number_constant("CallTheDamnedMaxAlive")

    assert cap > per_cast, (
        f"the cap is {cap} and one cast brings {per_cast}, so the field is "
        f"full after a single cast and the cooldown never means anything.")
    assert cap % per_cast == 0, (
        f"the cap is {cap} and one cast brings {per_cast}, so a cast can be "
        f"partly refused even when the player has killed a whole wave. The "
        f"design's 3 and 6 are two clean casts.")


def test_the_summon_draws_no_marker_and_that_is_designed():
    """A Summon is not one of the four shapes the telegraph table draws. There
    is no ground for it to be marked on, because the answer to adds is killing
    them rather than standing somewhere else."""
    from cataclysm_sim.enemy_abilities import TELEGRAPHED_SHAPES, is_telegraphed

    assert call_the_damned().shape == "Summon", (
        f"Call the Damned's shape is {call_the_damned().shape!r} in the model "
        f"and the C++ gives it ECataclysmSkillShape::Summon.")
    assert "Summon" not in TELEGRAPHED_SHAPES, (
        "the model now telegraphs a Summon, so this creature's summon should "
        "draw a marker and have a wind-up, and it has neither.")
    assert not is_telegraphed(call_the_damned(), gatekeeper())

    assert constant("CallTheDamnedWindUpSeconds") == 0.0, (
        "CallTheDamnedWindUpSeconds is not zero. A Summon draws no marker, so "
        "a wind-up would hold the creature still with nothing on screen "
        "explaining why.")

    body = code(GATEKEEPER_SOURCE)
    assert "Call.MarkerRadiusCm = 0.0f;" in body, (
        "Call the Damned now asks for a marker radius. Nothing draws a marker "
        "for a Summon, so the number would be a claim the screen never keeps.")


def test_the_cap_counts_what_is_alive_rather_than_what_was_summoned():
    """`ImpsStillAlive` forgets the dead before counting. If it stopped, the
    field would fill up and stay full and Call the Damned would never summon
    again -- and nothing on screen would say why."""
    body = code(GATEKEEPER_SOURCE)

    match = re.search(
        r"int32 ACataclysmGatekeeperCharacter::ImpsStillAlive\(\)\s*\{(.*?)\n\}",
        body, re.DOTALL)
    assert match is not None, (
        "CataclysmGatekeeperCharacter.cpp no longer defines ImpsStillAlive, so "
        "nothing counts how many of this creature's Imps are up.")

    within = match.group(1)
    assert "RemoveAll" in within, (
        f"ImpsStillAlive no longer removes anything before counting: "
        f"{within.strip()!r}. A weak pointer to a collected Imp reads as null "
        f"and would still be counted against the cap.")
    assert "IsDead" in within, (
        "ImpsStillAlive does not ask UCataclysmSkillEffects::IsDead. A dead "
        "Imp waiting for its death clip is still a valid pointer, so dropping "
        "that check leaves corpses counting against the cap.")


def test_the_summoned_imps_are_held_weakly():
    """A hard reference would keep every Imp this creature ever summoned alive
    for as long as the boss was, including ones already removed from the level
    -- and the cap counts what is alive, so it would fill up and stay full."""
    header = code(GATEKEEPER_HEADER)

    assert re.search(
        r"TArray<\s*TWeakObjectPtr<\s*class ACataclysmImpCharacter\s*>\s*>\s*"
        r"CalledImps", header), (
        "CataclysmGatekeeperCharacter.h no longer holds CalledImps as a "
        "TArray<TWeakObjectPtr<ACataclysmImpCharacter>>. A hard reference "
        "keeps dead Imps alive and the summon cap counts them for ever.")


# --------------------------------------------------------------------------
# Soul Harvest, the phase 3 ring
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,param,scale,what", [
    ("SoulHarvestRadiusCm", "Radius", CM_PER_METRE, "how far the ring reaches"),
    ("SoulHarvestAngleDegrees", "Angle", 1.0, "how wide the arc is"),
])
def test_the_ring_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = float(soul_harvest().params[param]) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and Soul Harvest's {param} is "
        f"{soul_harvest().params[param]}, which is {designed} ({what}).")


def test_the_ring_cooldown_matches_the_designed_ability():
    assert constant("SoulHarvestCooldownSeconds") == pytest.approx(
        soul_harvest().cooldown), (
        f"SoulHarvestCooldownSeconds is "
        f"{constant('SoulHarvestCooldownSeconds')} and the designed cooldown "
        f"is {soul_harvest().cooldown}.")


def test_the_ring_is_the_largest_marker_the_rules_allow():
    """6.5 metres is the cap for a creature with a 0.48 m body, the same ring
    as the Abyssal Warden's Molten Roar. A boss finale should be the hardest
    legal telegraph, and the cap is what hardest-legal means."""
    from cataclysm_sim.enemy_abilities import telegraph_cap_metres

    radius = float(soul_harvest().params["Radius"])
    cap = telegraph_cap_metres(gatekeeper())

    assert radius == pytest.approx(cap, abs=0.005), (
        f"Soul Harvest's radius is {radius} m and the cap for this creature is "
        f"{cap:.4f} m. The header and the design both claim the ring is "
        f"exactly the cap.")


def test_the_rings_wind_up_is_held_at_the_ceiling():
    """0.4 + 6.5 / 3.5 is 2.257, which is over the ceiling, so it is held at
    MAXIMUM_WIND_UP_SECONDS. Above the ceiling the warning stops growing while
    the ground to cross keeps growing, which is what makes a bigger radius
    harder."""
    from cataclysm_sim.enemy_abilities import (MAXIMUM_WIND_UP_SECONDS,
                                               REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED,
                                               wind_up_seconds)

    radius = float(soul_harvest().params["Radius"])
    uncapped = REACTION_ALLOWANCE + radius / WALK_OUT_SPEED

    assert uncapped > MAXIMUM_WIND_UP_SECONDS, (
        f"the rule gives {uncapped:.4f} s for a {radius} m ring, which is "
        f"under the {MAXIMUM_WIND_UP_SECONDS} s ceiling, so the header's claim "
        f"that this ring is held at the ceiling is no longer true.")

    written = constant("SoulHarvestWindUpSeconds")
    assert written == pytest.approx(wind_up_seconds(radius), abs=0.002)
    assert written == pytest.approx(MAXIMUM_WIND_UP_SECONDS)


def test_the_ring_is_a_full_circle():
    """There is no standing behind this one, which is what separates it from
    Dread Cleave and is why the C++ searches a sphere rather than a cone."""
    assert float(soul_harvest().params["Angle"]) == 360.0, (
        f"Soul Harvest's designed Angle is {soul_harvest().params['Angle']}, "
        f"so it is a cone and can be walked around. The C++ branches on "
        f"'>= 360' to choose a sphere search, so a smaller angle silently "
        f"turns the finale into a cone.")

    body = code(GATEKEEPER_SOURCE)
    assert "FindEnemiesInSphere" in body and "FindEnemiesInCone" in body, (
        "CataclysmGatekeeperCharacter.cpp no longer uses both searches. The "
        "ring is a sphere search and the sweep is a cone search, and they are "
        "two functions rather than one with an angle of 360.")


def test_the_ring_damage_is_the_ultimate_slots_percentage():
    assert soul_harvest().slot == "Ultimate", (
        f"Soul Harvest's slot is {soul_harvest().slot!r}, and the C++ pays it "
        f"the Ultimate slot's percentage.")
    assert constant("SoulHarvestDamagePercent") == pytest.approx(
        slot_damage_percent("Ultimate")), (
        f"SoulHarvestDamagePercent is "
        f"{constant('SoulHarvestDamagePercent')} and the Ultimate row of "
        f"game/Data/SkillSlots.csv is {slot_damage_percent('Ultimate')}.")


def test_the_ring_is_worth_four_ordinary_sweeps():
    """Standing in it is death from full health, which is designed rather than
    incidental: the reference geared character survives two ordinary hits."""
    ring = constant("SoulHarvestDamagePercent")
    sweep = constant("DreadCleaveDamagePercent")

    assert ring == pytest.approx(4.0 * sweep), (
        f"the ring is worth {ring}% and a sweep {sweep}%, so the ring is "
        f"{ring / sweep:.2f} sweeps rather than the four the header and the "
        f"design document both claim.")


# --------------------------------------------------------------------------
# The shape the class takes in the engine
# --------------------------------------------------------------------------

def test_the_reach_matches_the_ability_table():
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    designed = ATTACK_REACH["Gatekeeper"] * CM_PER_METRE
    written = constant("DreadCleaveRadiusCm")

    assert written == pytest.approx(designed), (
        f"the creature's reach is DreadCleaveRadiusCm at {written} cm and "
        f"ATTACK_REACH['Gatekeeper'] is {ATTACK_REACH['Gatekeeper']} m, which "
        f"is {designed} cm.")


def test_the_class_does_not_deal_a_free_melee_hit():
    """`ACataclysmEnemyCharacter::AttackTarget` applies direct damage at
    MeleeReachCm, and this creature's reach is Dread Cleave's own radius. Left
    to the base it would deal a free single-target hit at two metres every
    three seconds ON TOP of the sweep it already made -- and at a damage share
    of 2.10 that would be the largest unexplained number in the game."""
    body = source(GATEKEEPER_SOURCE)

    match = re.search(
        r"void ACataclysmGatekeeperCharacter::AttackTarget\([^)]*\)\s*\{(.*?)\n\}",
        body, re.DOTALL)
    assert match is not None, (
        "CataclysmGatekeeperCharacter.cpp no longer overrides AttackTarget, so "
        "the base class's melee hit lands at two metres every three seconds.")

    assert without_comments(match.group(1)).strip() == "", (
        f"AttackTarget has a body: {without_comments(match.group(1)).strip()!r}. "
        f"It must do nothing at all -- Dread Cleave is the basic attack and it "
        f"is an entry in EnemyAbilities.")


def test_it_overrides_the_designed_interval_rather_than_the_final_one():
    """`SecondsBetweenAttacks` is `final` on the enemy base and divides the
    designed figure by whatever the creature's buffs are worth. Six creatures
    overrode the wrong one before the Commander buff existed and every one
    would have ignored it in silence."""
    header = code(GATEKEEPER_HEADER)

    assert "DesignedSecondsBetweenAttacks" in header, (
        "CataclysmGatekeeperCharacter.h no longer overrides "
        "DesignedSecondsBetweenAttacks, so nothing states this creature's 3.0 "
        "second interval and it falls back to the base default.")
    assert not re.search(r"\bfloat\s+SecondsBetweenAttacks\s*\(", header), (
        "CataclysmGatekeeperCharacter.h overrides SecondsBetweenAttacks. That "
        "one is final on ACataclysmEnemyCharacter because it divides the "
        "designed interval by the creature's buffs; overriding it ignores "
        "every buff in the game without a word.")


def test_the_sweep_is_listed_last_so_the_rest_are_reachable():
    """`ChooseAbility` takes the first entry whose phase, range and cooldown
    fit and never looks at the shape. Dread Cleave's cooldown is zero, so a
    Dread Cleave at the front of the array would be the only thing this
    creature ever did. Issue #491 is that defect on the Abyssal Warden."""
    returned = returned_ability_variables()

    assert returned == ["Harvest", "Call", "Soulfall", "Cleave"], (
        f"EnemyAbilities returns {returned}. The sweep has to come last "
        f"because its cooldown is zero, and the other three descend by weight "
        f"so the creature reaches for the biggest thing it may use.")

    match = re.search(r"enum\s*:\s*int32\s*\{([^}]*)\}", code(GATEKEEPER_HEADER))
    assert match is not None, (
        "CataclysmGatekeeperCharacter.h no longer names the ability indices.")

    listed = match.group(1)
    for name, index in (("SoulHarvestAbility", 0), ("CallTheDamnedAbility", 1),
                        ("SoulfallAbility", 2), ("DreadCleaveAbility", 3)):
        assert f"{name} = {index}" in listed, (
            f"the enum does not say {name} = {index}. The indices have to "
            f"match the order of the array above or every ability does "
            f"something else's job.")


def test_the_array_holds_exactly_the_four_designed_abilities():
    """Nothing extra and nothing missing. An ability the model designs and the
    array leaves out is one the creature simply never uses."""
    from cataclysm_sim.enemy_abilities import abilities

    body = code(GATEKEEPER_SOURCE)
    named = set(re.findall(r"\.Name\s*=\s*TEXT\(\"([^\"]+)\"\)\s*;", body))
    designed = {entry.name for entry in abilities("Gatekeeper")}

    assert named == designed, (
        f"EnemyAbilities names {sorted(named)} and the model designs "
        f"{sorted(designed)}.")

    assert len(returned_ability_variables()) == len(designed), (
        f"the returned array has {len(returned_ability_variables())} entries "
        f"and the model designs {len(designed)} abilities.")


def test_every_shape_it_asks_for_is_one_the_enum_really_has():
    """A shape the enum does not hold silently reads as None and draws no
    marker at all. Summon was in ECataclysmSkillShape before this creature
    existed and no enemy had used it. That is issue #621, which happened to
    Deployable."""
    listed = source(SKILL_SHAPE_HEADER)

    for name in ("Strike", "Projectile", "Summon"):
        assert re.search(rf"^\t{name}\s", listed, re.MULTILINE), (
            f"ECataclysmSkillShape has no {name} entry, so an ability asking "
            f"for it reads as None and does nothing at all.")


# --------------------------------------------------------------------------
# Its animation
# --------------------------------------------------------------------------

def clip_seconds_from_notes(clip: str) -> float:
    """The length the asset notes record for a clip, in seconds."""
    match = re.search(rf"\|\s*`{re.escape(clip)}`\s*\|\s*(\d+(?:\.\d+)?)\s*\|",
                      source(ASSET_NOTES))
    if match is None:
        pytest.fail(
            f"game/docs/enemy-source-assets.md records no measured length for "
            f"{clip}, so the figure in the header came from nowhere. Run "
            f"tools/probe_gatekeeper_animation.py and record what it says.")
    return float(match.group(1))


@pytest.mark.parametrize("cpp_name,clip_constant", [
    ("CleaveAnimationSeconds", "CleaveAnimationName"),
    ("SoulfallAnimationSeconds", "SoulfallAnimationName"),
    ("UltimateAnimationSeconds", "UltimateAnimationName"),
])
def test_each_clip_length_is_the_one_that_was_measured(cpp_name, clip_constant):
    clip = text_constant(clip_constant)
    written = constant(cpp_name)
    measured = clip_seconds_from_notes(clip)

    assert written == pytest.approx(measured, abs=0.0005), (
        f"{cpp_name} is {written} and game/docs/enemy-source-assets.md records "
        f"{clip} at {measured} seconds.")


@pytest.mark.parametrize("clip_seconds,wind_up", [
    ("CleaveAnimationSeconds", "DreadCleaveWindUpSeconds"),
    ("SoulfallAnimationSeconds", "SoulfallWindUpSeconds"),
    ("UltimateAnimationSeconds", "SoulHarvestWindUpSeconds"),
])
def test_each_clip_fits_inside_the_wind_up_it_plays_across(clip_seconds,
                                                           wind_up):
    """The clip IS the wind-up: the blow lands as the telegraph ends, so the
    swing has to end there too. A clip longer than the wind-up even at the play
    rate ceiling would still be playing when the hammer landed."""
    clip = constant(clip_seconds)
    window = constant(wind_up)
    ceiling = constant("MaximumPlayRate")

    assert clip <= window * ceiling, (
        f"{clip_seconds} is {clip} s and {wind_up} is {window} s. Even at the "
        f"play rate ceiling of {ceiling} the clip needs {clip / ceiling:.4f} s.")


def test_the_summon_clip_plays_at_its_authored_speed():
    """Call the Damned has no wind-up to fit, because a Summon draws no marker,
    so there is no window to squeeze the clip into."""
    assert constant("CallTheDamnedWindUpSeconds") == 0.0

    body = code(GATEKEEPER_SOURCE)
    assert "PlayOneShot(CallAnimation.Get());" in body, (
        "the summon clip is no longer played at its own length. "
        "PlayOneShot's HoldSeconds defaults to zero, which means 'across your "
        "own length'; passing a hold for an ability with no wind-up would "
        "speed the clip up to fit a window that does not exist.")


def test_the_walk_play_rate_is_still_recorded_as_a_guess():
    """**THE ONE FIGURE IN THIS CREATURE THAT IS NOT DERIVED.**

    Every other creature's walk rate is `designed speed / authored speed`, with
    the authored speed measured by `tools/measure_animation_stride.py` and the
    idle checked as a control. That tool FAILED ITS OWN CONTROL on the Sevarog
    rig on 2026-08-20: 0.0 cm/s for `Walk_Fwd`, `Jog_Fwd` and `Run_Fwd`, and
    14.7 cm/s for `Idle`, which is the control and must read zero. Wrong in
    both directions, so none of its numbers may be used. Issue #778.

    WHAT THIS GUARD IS FOR. Guessing a walk play rate is what produced visible
    foot sliding on the Brute, which is why the measuring tool exists. This
    fails if somebody changes the rate without adding a measurement, and it
    fails if somebody removes the wording that says the rate is unverified
    while it still is. If the rig is ever measured, add
    `AuthoredJogSpeedCmPerSecond` the way every other creature carries it and
    this test will tell you to derive the rate from it.
    """
    header = source(GATEKEEPER_HEADER)

    measured = re.search(
        r"static\s+constexpr\s+float\s+AuthoredJogSpeedCmPerSecond\s*=\s*"
        r"(\d+(?:\.\d+)?)f\s*;", without_comments(header))

    if measured is not None:
        authored = float(measured.group(1))
        designed = constant("DesignedWalkSpeedCmPerSecond")
        assert constant("JogPlayRate") == pytest.approx(designed / authored,
                                                        abs=0.0005), (
            f"the rig has been measured at {authored} cm/s, so JogPlayRate "
            f"must be the designed {designed} divided by it, which is "
            f"{designed / authored:.4f}. Update the header's comment and this "
            f"test's docstring too: the rate is no longer a guess.")
        return

    assert constant("JogPlayRate") == 1.0, (
        f"JogPlayRate is {constant('JogPlayRate')} and nothing has measured "
        f"how fast this rig's walk is authored. A rate that is neither 1.0 nor "
        f"derived from a measurement is a guess presented as a number, which "
        f"is what put visible foot sliding on the Brute. Either measure the "
        f"rig -- see issue #778 -- and add AuthoredJogSpeedCmPerSecond, or "
        f"leave the rate at 1.0.")

    for phrase in ("issue #778", "#778"):
        if phrase in header:
            break
    else:
        pytest.fail(
            "CataclysmGatekeeperCharacter.h no longer names issue #778 beside "
            "JogPlayRate. The rate is a placeholder and the header is the only "
            "place a reader would find that out.")

    assert re.search(r"unverified|placeholder", header, re.IGNORECASE), (
        "CataclysmGatekeeperCharacter.h no longer says the walk play rate is "
        "unverified. It is 1.0 because the rig could not be measured, not "
        "because 1.0 was derived, and presenting it as derived is the mistake "
        "this guard exists to prevent.")

    notes = source(ASSET_NOTES)
    assert "#778" in notes, (
        "game/docs/enemy-source-assets.md no longer names issue #778, so the "
        "record of the failed stride measurement has gone.")


def test_the_walk_play_rate_is_inside_the_clamp():
    """A rate outside the clamp is one the creature cannot actually reach, so
    its feet slide whatever the number says."""
    rate = constant("JogPlayRate")
    floor = constant("MinimumPlayRate")
    ceiling = constant("MaximumPlayRate")

    assert floor <= rate <= ceiling, (
        f"the walk plays at {rate} and the clamp is {floor} to {ceiling}, so "
        f"it would be clamped and the feet would slide.")


def test_the_walk_clip_is_one_whose_feet_actually_move():
    """`Walk_Fwd` and `Run_Fwd` animate no leg bone at all -- every foot, calf
    and thigh reads 0.00 cm across their whole 1.6 seconds, measured by
    `tools/probe_gatekeeper_foot_bones.py` on 2026-08-20. `Jog_Fwd` is the only
    locomotion clip in the pack whose feet move."""
    assert text_constant("JogAnimationName") == "Jog_Fwd", (
        f"the creature walks with {text_constant('JogAnimationName')!r}. Only "
        f"Jog_Fwd moves any leg bone on this rig; see the foot bone table in "
        f"game/docs/enemy-source-assets.md. Re-run "
        f"tools/probe_gatekeeper_foot_bones.py before choosing another.")


def test_the_asset_notes_record_every_clip_it_plays():
    """Every clip the class names has to have a measured length on record, or
    the numbers in the header came from nowhere."""
    notes = source(ASSET_NOTES)

    for constant_name in ("IdleAnimationName", "JogAnimationName",
                          "CleaveAnimationName", "SoulfallAnimationName",
                          "CallAnimationName", "UltimateAnimationName"):
        clip = text_constant(constant_name)
        assert f"`{clip}`" in notes, (
            f"game/docs/enemy-source-assets.md does not mention {clip}, which "
            f"CataclysmGatekeeperCharacter.cpp plays. Run "
            f"tools/probe_gatekeeper_animation.py and record what it says.")

    death = re.search(r"DeathAnimationNames\s*\n?\s*\[DeathAnimationCount\]\s*=\s*\{"
                      r"([^}]*)\}", code(GATEKEEPER_SOURCE))
    assert death is not None, (
        "CataclysmGatekeeperCharacter.cpp no longer lists DeathAnimationNames.")

    for clip in re.findall(r"TEXT\(\"([^\"]+)\"\)", death.group(1)):
        assert f"`{clip}`" in notes, (
            f"game/docs/enemy-source-assets.md does not mention the death clip "
            f"{clip}.")


def test_it_has_one_way_to_fall_over():
    """The Sevarog pack ships exactly one death clip, which is the fewest in
    the project along with the Brute's and the Succubus's."""
    written = whole_number_constant("DeathAnimationCount")

    assert written == 1, (
        "DeathAnimationCount is no longer 1. If the pack really does ship more "
        "than one death clip, measure them and say so in "
        "game/docs/enemy-source-assets.md; if not, this is a typo that changes "
        "which clip PlayDeathAnimation draws.")

    death = re.search(r"DeathAnimationNames\s*\n?\s*\[DeathAnimationCount\]\s*=\s*\{"
                      r"([^}]*)\}", code(GATEKEEPER_SOURCE))
    assert death is not None
    assert len(re.findall(r"TEXT\(\"", death.group(1))) == written, (
        f"DeathAnimationCount is {written} and the array lists "
        f"{len(re.findall(r'TEXT..', death.group(1)))} names. A mismatch is a "
        f"compile error at best and an out-of-range draw at worst.")


# --------------------------------------------------------------------------
# The sandbox
# --------------------------------------------------------------------------

def sandbox_stat_block():
    from cataclysm_sim.enemy_stats import stats_on_floor

    return stats_on_floor("Common", 1, "Cataclysm", total_floors=50, floor=50,
                          kind="Gatekeeper")


def test_the_sandbox_actually_spawns_one():
    assert whole_number_property("GatekeeperCount") > 0, (
        "GatekeeperCount is zero, so no Gatekeeper is placed in the sandbox "
        "and the creature cannot be looked at.")

    assert "SpawnGatekeepers();" in code(GAME_MODE_SOURCE), (
        "ACataclysmGameMode::StartPlay does not call SpawnGatekeepers, so the "
        "spawner exists and nothing runs it.")


def test_the_sandbox_health_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().health
    written = property_default("GatekeeperHealth")

    assert written == pytest.approx(round(designed)), (
        f"GatekeeperHealth is {written} and the design model gives a Common "
        f"Gatekeeper at tier 1, on the last floor of a 50-floor Cataclysm "
        f"dungeon, {designed:.2f}.")


def test_the_sandbox_armour_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().armor
    written = property_default("GatekeeperArmour")

    assert written == pytest.approx(round(designed)), (
        f"GatekeeperArmour is {written} and the design model gives "
        f"{designed:.2f} at the same encounter as its health.")


def test_the_sandbox_damage_is_the_dummys_times_the_designed_share():
    dummy = property_default("TrainingDummyAttackDamage")
    expected = dummy * gatekeeper().damage_share
    written = property_default("GatekeeperAttackDamage")

    assert written == pytest.approx(expected), (
        f"GatekeeperAttackDamage is {written} and the training dummy's {dummy} "
        f"times the designed damage share of {gatekeeper().damage_share} is "
        f"{expected}.")


def test_the_phase_thresholds_land_where_the_header_says():
    """The header of GatekeeperHealth says phase 2 begins at 749 and phase 3 at
    374. Those are health figures a person watching the fight can check."""
    health = property_default("GatekeeperHealth")

    second = health * constant("SecondPhaseHealthFraction")
    third = health * constant("ThirdPhaseHealthFraction")

    assert round(second) == 749, (
        f"phase 2 now begins at {second:.0f} health rather than 749, which is "
        f"what CataclysmGameMode.h says beside GatekeeperHealth.")
    assert round(third) == 374, (
        f"phase 3 now begins at {third:.0f} health rather than 374, which is "
        f"what CataclysmGameMode.h says beside GatekeeperHealth.")


def test_it_is_spawned_beyond_its_own_notice_radius():
    """So it is standing about rather than already walking at the player when
    the level opens."""
    distance = property_default("GatekeeperDistanceCm")
    notices = constant("GatekeeperNoticeRadiusCm")

    assert distance > notices, (
        f"the Gatekeeper spawns {distance} cm out and notices at {notices} cm, "
        f"so it starts walking at the player the instant the level opens.")


def test_it_notices_further_than_anything_else_in_the_sandbox():
    """Which is why it has to stand so far out. The header of
    GatekeeperDistanceCm rests on this being true."""
    radii = {}
    for name, header_name, cpp_name in (
            ("the Brute", "CataclysmBruteCharacter.h", "BruteNoticeRadiusCm"),
            ("the Abyssal Warden", "CataclysmAbyssalWardenCharacter.h",
             "AbyssalWardenNoticeRadiusCm"),
            ("the Hellhound", "CataclysmHellhoundCharacter.h",
             "HellhoundNoticeRadiusCm"),
            ("the Imp", "CataclysmImpCharacter.h", "ImpNoticeRadiusCm"),
            ("the Corrupted Sentinel", "CataclysmCorruptedSentinelCharacter.h",
             "CorruptedSentinelNoticeRadiusCm"),
            ("the Succubus", "CataclysmSuccubusCharacter.h",
             "SuccubusNoticeRadiusCm")):
        path = CHARACTER_DIR / header_name
        match = re.search(
            rf"static\s+constexpr\s+float\s+{cpp_name}\s*=\s*"
            rf"(-?\d+(?:\.\d+)?)f\s*;", code(path))
        if match is not None:
            radii[name] = float(match.group(1))

    if not radii:
        pytest.skip(
            "no other creature states a notice radius constant under the names "
            "this test knows, so there is nothing to compare against. A skip "
            "means this did not run, not that it passed.")

    mine = constant("GatekeeperNoticeRadiusCm")
    assert mine > max(radii.values()), (
        f"the Gatekeeper notices at {mine} cm and something else notices "
        f"further: {radii}. Both the header and CataclysmGameMode.h say this "
        f"is the largest in the game, and the spawn distance was chosen "
        f"because of it.")


def test_no_creature_stands_inside_its_notice_radius():
    """Every creature must stand further from every other than its own notice
    radius, or two of them are fighting each other's neighbours rather than
    waiting to be walked up to. This creature's 1400 cm is the largest in the
    game, which is why it is placed furthest out."""
    placed = {
        "the Brute": (property_default("BruteDistanceCm"), 0.0),
        "the Abyssal Warden": (property_default("AbyssalWardenDistanceCm"), 0.0),
    }
    for name, distance_key, bearing_key in (
            ("the Hellhound", "HellhoundDistanceCm", "HellhoundBearingDegrees"),
            ("the Imp pack", "ImpDistanceCm", "ImpBearingDegrees"),
            ("the Corrupted Sentinel", "CorruptedSentinelDistanceCm",
             "CorruptedSentinelBearingDegrees"),
            ("the Succubus", "SuccubusDistanceCm", "SuccubusBearingDegrees"),
            ("the Gatekeeper", "GatekeeperDistanceCm",
             "GatekeeperBearingDegrees")):
        bearing = math.radians(property_default(bearing_key))
        distance = property_default(distance_key)
        placed[name] = (distance * math.cos(bearing),
                        distance * math.sin(bearing))

    needed = constant("GatekeeperNoticeRadiusCm")
    gatekeeper_at = placed["the Gatekeeper"]

    for name, where in placed.items():
        if name == "the Gatekeeper":
            continue
        gap = math.dist(gatekeeper_at, where)
        assert gap > needed, (
            f"the Gatekeeper stands {gap:.0f} cm from {name}, inside the "
            f"{needed:.0f} cm at which it notices anybody. Move it further out "
            f"or put it on another bearing.")

    # AND NO TWO CREATURES SHARE A PLACE, which a bearing typed twice would do.
    for left, right in itertools.combinations(placed, 2):
        assert math.dist(placed[left], placed[right]) > 1.0, (
            f"{left} and {right} are placed on the same spot.")


def test_it_is_spawned_inside_the_sandbox_floor():
    """It stands further out than anything else, so it is the creature most
    likely to be put over the edge."""
    floor_extent = re.search(r"^FLOOR_EXTENT\s*=\s*(\d+(?:\.\d+)?)",
                             source(LEVEL_SCRIPT), re.MULTILINE)
    assert floor_extent is not None, (
        "tools/generate_input_assets.py no longer defines FLOOR_EXTENT.")

    reach = float(floor_extent.group(1)) / 2.0
    bearing = math.radians(property_default("GatekeeperBearingDegrees"))
    distance = property_default("GatekeeperDistanceCm")
    radius = constant("GatekeeperCapsuleRadius")

    # ALONG EACH AXIS RATHER THAN ALONG THE BEARING, because the floor is a
    # SQUARE and this creature is placed on a diagonal. A radial test would be
    # wrong for a diagonal in the forgiving direction, and a guard that is
    # wrong in the forgiving direction is one that does not guard.
    furthest_x = abs(distance * math.cos(bearing)) + radius
    furthest_y = abs(distance * math.sin(bearing)) + radius

    assert max(furthest_x, furthest_y) < reach, (
        f"the Gatekeeper reaches {furthest_x:.0f} cm along X and "
        f"{furthest_y:.0f} cm along Y, and the sandbox floor only reaches "
        f"{reach} cm on each axis.")


def test_its_rarity_can_be_set_by_hand_like_every_other_creature():
    """`GatekeeperRarityStep` of -1 means draw one, and the sandbox
    configuration file has to carry the same key or the console cannot pin it."""
    assert whole_number_property("GatekeeperRarityStep") == -1, (
        "GatekeeperRarityStep is no longer -1, so the boss spawns at a fixed "
        "rarity rather than drawing one like the rest of the sandbox.")

    ini = source(REPO_ROOT / "game" / "Config" / "DefaultGame.ini")
    assert "GatekeeperRarityStep=" in ini, (
        "game/Config/DefaultGame.ini has no GatekeeperRarityStep entry, so the "
        "one creature in the sandbox that is a boss cannot have its rarity "
        "pinned the way the other six can.")
