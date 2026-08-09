"""The Abyssal Warden's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmAbyssalWardenCharacter.h` hard-codes
fourteen numbers that also live in the Python model, and
`game/Source/Cataclysm/Player/CataclysmGameMode.h` derives two more from it. Two
copies of one number drift, and in this repository they have.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Abyssal Warden"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ABILITIES["Abyssal Warden"]` and
`ATTACK_REACH["Abyssal Warden"]` in `sim/cataclysm_sim/enemy_abilities.py` are
where the creature is designed. When this file fails, the usual fix is to change
the C++.

WHY IT EXISTS AT ALL RATHER THAN AN ENGINE TEST. Continuous integration never
builds the C++ and never opens the editor -- `.github/workflows/ci.yml` is a
single Linux job -- so an automation test cannot run on a pull request. This
reads the source text, which is present either way.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second because
the design document does. Unreal works in centimetres. The factor of 100 is
applied here in the open rather than hidden on either side.

WHAT IS DELIBERATELY NOT COMPARED. The three shares -- `health_share`,
`damage_share` and `armor_share`. A share is a multiplier on a score-scaled base
and nothing in the engine knows an encounter's Power Score, so a figure for one
declared on a C++ class would be invented rather than designed. They reach the
sandbox as scaffolding instead, and the two scaffolding figures ARE checked below
against the shares they claim to apply.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WARDEN_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                 / "CataclysmAbyssalWardenCharacter.h")
WARDEN_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmAbyssalWardenCharacter.cpp")
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")
CONTROLLER_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                     / "CataclysmEnemyController.h")
PLAYER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmPlayerCharacter.cpp")

sys.path.insert(0, str(REPO_ROOT / "sim"))

CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path = WARDEN_HEADER) -> float:
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


def property_default(name: str, path: pathlib.Path) -> float:
    """The value of a `float <name> = <number>f;` property default."""
    match = re.search(
        rf"\bfloat\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'float {name} = <number>f;' line.")
    return float(match.group(1))


def warden():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Abyssal Warden")


def warden_abilities():
    from cataclysm_sim.enemy_abilities import abilities

    return abilities("Abyssal Warden")


def molten_roar():
    return next(a for a in warden_abilities() if a.slot == "Ultimate")


# --------------------------------------------------------------------------
# The profile, straight off the archetype
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, model_value, scale, what", [
    ("DesignedAttackIntervalSeconds", "attack_interval", 1.0,
     "seconds between ordinary swings"),
    ("DesignedResistancePercent", "resistance", 1.0,
     "percent of all incoming damage resisted"),
    ("DesignedCritChancePercent", "crit_chance", 1.0, "critical strike chance"),
    ("DesignedCritMultiplierPercent", "crit_multiplier", 1.0,
     "critical strike multiplier"),
    ("DesignedEvasionPercent", "evasion", 1.0, "evasion"),
    ("DesignedWalkSpeedCmPerSecond", "move_speed", CM_PER_METRE,
     "how fast it walks"),
    ("DesignedTurnRateDegreesPerSecond", "turn_rate_degrees", 1.0,
     "how fast it turns"),
])
def test_the_profile_matches_the_archetype(cpp_name, model_value, scale, what):
    designed = getattr(warden(), model_value) * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} in CataclysmAbyssalWardenCharacter.h is {constant(cpp_name)} "
        f"and the model designs {designed} for {what} "
        f"({model_value} in ARCHETYPES['Abyssal Warden']). The model is "
        f"authoritative.")


def test_the_reach_matches_the_ability_table():
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    designed = ATTACK_REACH["Abyssal Warden"] * CM_PER_METRE
    assert constant("DesignedMeleeReachCm") == pytest.approx(designed), (
        f"DesignedMeleeReachCm is {constant('DesignedMeleeReachCm')} and "
        f"ATTACK_REACH['Abyssal Warden'] designs {designed} cm.")


def test_the_capsule_radius_is_the_designed_body_radius():
    """And therefore the same 0.48 m six of the seven enemies use by default.

    The body radius has never been measured for this creature -- issue #366 --
    so this checks the C++ against the default the model still carries, which is
    what will change when somebody measures it.
    """
    designed = warden().body_radius * CM_PER_METRE
    assert constant("WardenCapsuleRadius") == pytest.approx(designed), (
        f"WardenCapsuleRadius is {constant('WardenCapsuleRadius')} and the "
        f"model's body_radius is {designed} cm.")


def test_it_has_no_chase_speed_in_the_model_or_in_the_class():
    """The fact its whole design rests on, checked from both ends.

    WHY THIS IS A TEST AND NOT A COMMENT. A chase speed is one line to add and it
    would make the creature able to catch the player, which is precisely what the
    design says it cannot do. The Brute has one and this class was written from
    the Brute, so the failure mode is copying a line rather than adding one on
    purpose.
    """
    assert warden().chase_speed == 0.0, (
        f"the model now gives the Abyssal Warden a chase speed of "
        f"{warden().chase_speed} m/s. Its design is that it cannot catch "
        "anybody; if that changed, issue #491 and its design document "
        "subsection both have to change with it.")

    for forbidden in ("ChaseSpeed", "DesignedChaseSpeedCmPerSecond",
                      "IsChasing"):
        assert forbidden not in source(WARDEN_HEADER), (
            f"CataclysmAbyssalWardenCharacter.h now mentions {forbidden}. This "
            "creature is designed with no chase speed at all -- it walks at one "
            "pace whether or not it has seen the player.")


# --------------------------------------------------------------------------
# Molten Roar
# --------------------------------------------------------------------------

def test_the_ring_radius_matches_the_designed_ability():
    designed = float(molten_roar().params["Radius"]) * CM_PER_METRE
    assert constant("MoltenRoarRadiusCm") == pytest.approx(designed), (
        f"MoltenRoarRadiusCm is {constant('MoltenRoarRadiusCm')} and the model "
        f"designs {designed} cm.")


def test_the_ring_cooldown_matches_the_designed_ability():
    assert constant("MoltenRoarCooldownSeconds") == pytest.approx(
        molten_roar().cooldown), (
        f"MoltenRoarCooldownSeconds is {constant('MoltenRoarCooldownSeconds')} "
        f"and the model designs {molten_roar().cooldown} s.")


def test_the_ring_wind_up_is_the_formula_rather_than_a_number():
    """Recomputed from the radius, not compared against a written copy.

    The C++ carries a static_assert holding the same two together at compile
    time. This is the same check from outside, so it fails on a pull request
    rather than only on a machine that builds.
    """
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED)

    radius_metres = constant("MoltenRoarRadiusCm") / CM_PER_METRE
    expected = REACTION_ALLOWANCE + radius_metres / WALK_OUT_SPEED

    assert constant("MoltenRoarWindUpSeconds") == pytest.approx(expected), (
        f"MoltenRoarWindUpSeconds is {constant('MoltenRoarWindUpSeconds')} and "
        f"the wind-up rule gives {expected:.4f} for a "
        f"{radius_metres} m marker. The rule is "
        f"{REACTION_ALLOWANCE} + Radius / {WALK_OUT_SPEED}, from the Attack "
        "Telegraphs subsection of docs/Cataclysm_GDD_v2.md.")


def test_the_ring_damage_is_the_ultimate_slots_percentage():
    """Read out of game/Data/SkillSlots.csv rather than written here.

    It is the first thing in the game to use that slot, so this is also what
    catches the slot's own figure being retuned without the creature following.
    """
    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    if not slots.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with slots.open(encoding="utf-8-sig", newline="") as handle:
        ultimate = next(row for row in csv.DictReader(handle)
                        if row["Slot"] == "Ultimate")

    assert constant("MoltenRoarDamagePercent") == pytest.approx(
        float(ultimate["DamagePercent"])), (
        f"MoltenRoarDamagePercent is {constant('MoltenRoarDamagePercent')} and "
        f"the Ultimate slot in game/Data/SkillSlots.csv is "
        f"{ultimate['DamagePercent']}.")


def test_the_charge_is_absent_from_the_cpp_and_the_reason_is_recorded():
    """Stampede is designed and deliberately not built. Issue #491.

    THE FAILURE THIS CATCHES is somebody adding it to EnemyAbilities to be
    helpful. `ACataclysmEnemyController::ChooseAbility` picks by range and
    cooldown without looking at the shape and takes the first entry that fits, so
    a Movement entry spanning 0 to 800 cm would be chosen ahead of Molten Roar
    everywhere inside eight metres, draw no marker, and do nothing.

    THE MODEL STILL DESIGNS THREE ABILITIES and that is correct -- the design is
    not what is missing. This checks only that the C++ implements one of them and
    says why.
    """
    designed = warden_abilities()
    assert len(designed) == 3, (
        f"the model now designs {len(designed)} abilities for the Abyssal "
        "Warden. This test assumes three: a swing, a charge and a ring.")

    text = source(WARDEN_CPP)

    assert "Stampede" not in text.replace("Stampede, the designed charge", ""), (
        "CataclysmAbyssalWardenCharacter.cpp now implements Stampede. Nothing "
        "can execute a Movement-shape ability: ChooseAbility does not look at "
        "the shape and the marker switch returns silently for one, so it would "
        "be chosen ahead of Molten Roar and then do nothing. Issue #491.")

    assert "#491" in text, (
        "CataclysmAbyssalWardenCharacter.cpp no longer cites issue #491 as the "
        "reason its charge is missing, so the omission reads as an oversight.")


# --------------------------------------------------------------------------
# The capsule, and the test it is bounded by
# --------------------------------------------------------------------------

def test_the_capsule_half_height_stays_inside_the_contact_tolerance():
    """The upper bound that is not obvious, recomputed rather than trusted.

    The brain compares reach on the floor plane, but the two capsule centres do
    not sit at the same height, and `ContactToleranceCm` is the only slack there
    is. At contact the bodies are `player radius + warden radius` apart
    horizontally; the 3D distance between the centres is the hypotenuse with the
    height difference; and that must stay inside `reach + tolerance` or the
    creature can never register as being in contact.

    EVERY TERM IS READ FROM ITS OWN SOURCE, so this cannot pass by agreeing with
    itself: the player's capsule from CataclysmPlayerCharacter.cpp, the tolerance
    from CataclysmEnemyController.h, and the rest from the Warden's header.
    """
    import math

    player_radius = float(re.search(
        r"CapsuleRadius\s*=\s*(-?\d+(?:\.\d+)?)f", source(PLAYER_CPP)).group(1))
    player_half_height = float(re.search(
        r"CapsuleHalfHeight\s*=\s*(-?\d+(?:\.\d+)?)f",
        source(PLAYER_CPP)).group(1))

    tolerance = float(re.search(
        r"static\s+constexpr\s+float\s+ContactToleranceCm\s*=\s*"
        r"(-?\d+(?:\.\d+)?)f", source(CONTROLLER_HEADER)).group(1))

    radius = constant("WardenCapsuleRadius")
    half_height = constant("WardenCapsuleHalfHeight")
    reach = constant("DesignedMeleeReachCm")

    horizontal = player_radius + radius
    gap = half_height - player_half_height
    distance_3d = math.hypot(horizontal, gap)

    assert distance_3d <= reach + tolerance, (
        f"an Abyssal Warden with a {half_height} cm capsule half-height sits "
        f"{gap:.2f} cm above the player's capsule centre, so the 3D distance at "
        f"contact is {distance_3d:.3f} cm against a reach of {reach} plus "
        f"{tolerance} cm of tolerance. It could never register as being in "
        f"contact. The largest half-height that works is "
        f"{player_half_height + math.sqrt((reach + tolerance) ** 2 - horizontal ** 2):.2f} cm.")


def test_the_capsule_half_height_is_not_taller_than_the_art():
    """The lower bound, read out of the measured asset record.

    A capsule taller than the mesh would leave the creature's head inside it and
    its feet floating, and the mesh is offset by exactly this figure.
    """
    record = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
    if not record.is_file():
        pytest.fail("game/docs/enemy-source-assets.md does not exist")

    stated = re.search(r"\|\s*The Abyssal Warden\s*\|\s*([\d.]+) cm\s*\|",
                       record.read_text(encoding="utf-8"))
    if stated is None:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer records the Abyssal "
            "Warden's mesh height, so nothing bounds its capsule from below.")

    mesh_half_height = float(stated.group(1)) / 2.0
    assert constant("WardenCapsuleHalfHeight") <= mesh_half_height + 0.5, (
        f"WardenCapsuleHalfHeight is {constant('WardenCapsuleHalfHeight')} and "
        f"the GruxMolten mesh is only {mesh_half_height * 2} cm tall, so half "
        f"of it is {mesh_half_height}. A capsule taller than the art leaves the "
        "creature floating.")


# --------------------------------------------------------------------------
# The sandbox scaffolding
# --------------------------------------------------------------------------

def test_the_sandbox_health_is_the_dummys_times_the_designed_share():
    """Scaffolding, but scaffolding that claims a ratio, so the ratio is checked.

    The comment on the property says it is the training dummy's health times the
    archetype's health share. That claim is what makes the sandbox creature as
    much tougher than a dummy as the model says it should be, and it is the kind
    of claim that goes stale when a share is retuned.
    """
    dummy = property_default("TrainingDummyHealth", GAME_MODE_HEADER)
    expected = dummy * warden().health_share

    assert property_default("AbyssalWardenHealth", GAME_MODE_HEADER) == \
        pytest.approx(expected), (
        f"AbyssalWardenHealth is "
        f"{property_default('AbyssalWardenHealth', GAME_MODE_HEADER)} and the "
        f"training dummy's {dummy} times the designed health share of "
        f"{warden().health_share} is {expected}.")


def test_the_sandbox_damage_is_the_dummys_times_the_designed_share():
    dummy = property_default("TrainingDummyAttackDamage", GAME_MODE_HEADER)
    expected = dummy * warden().damage_share

    assert property_default("AbyssalWardenAttackDamage", GAME_MODE_HEADER) == \
        pytest.approx(expected), (
        f"AbyssalWardenAttackDamage is "
        f"{property_default('AbyssalWardenAttackDamage', GAME_MODE_HEADER)} and "
        f"the training dummy's {dummy} times the designed damage share of "
        f"{warden().damage_share} is {expected}.")


def test_it_is_spawned_further_out_than_the_brutes():
    """Because its ring is 5.6 metres across and the two should not start inside
    each other's telegraphs."""
    brute = property_default("BruteDistanceCm", GAME_MODE_HEADER)
    warden_distance = property_default("AbyssalWardenDistanceCm",
                                       GAME_MODE_HEADER)

    assert warden_distance > brute + constant("MoltenRoarRadiusCm"), (
        f"the Abyssal Warden spawns {warden_distance} cm out and the Brutes "
        f"spawn {brute} cm out, which is closer than its own "
        f"{constant('MoltenRoarRadiusCm')} cm ring. They would start inside "
        "each other's telegraphs.")


def test_the_attack_interval_clears_the_swing_clip_it_plays():
    """Nothing rate-scales an ordinary swing, so the interval is a hard floor.

    The two swing clips are 1.1333 seconds each. The length is read out of
    game/docs/enemy-source-assets.md rather than written here, so there is one
    copy of the measurement.
    """
    record = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
    if not record.is_file():
        pytest.fail("game/docs/enemy-source-assets.md does not exist")

    stated = re.search(
        r"`PrimaryAttack_LA`\s*\|\s*([\d.]+)\s*\|",
        record.read_text(encoding="utf-8"))
    if stated is None:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer records the length of "
            "PrimaryAttack_LA, the clip the Abyssal Warden swings with.")

    clip = float(stated.group(1))
    assert clip <= constant("DesignedAttackIntervalSeconds"), (
        f"the Abyssal Warden swings every "
        f"{constant('DesignedAttackIntervalSeconds')} s and PrimaryAttack_LA is "
        f"{clip} s long. Nothing rate-scales it, so the creature starts a swing "
        "it has not finished.")
