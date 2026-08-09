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
    from cataclysm_sim.enemy_abilities import (MAXIMUM_WIND_UP_SECONDS,
                                               REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED, wind_up_seconds)

    radius_metres = constant("MoltenRoarRadiusCm") / CM_PER_METRE
    expected = wind_up_seconds(radius_metres)

    assert constant("MoltenRoarWindUpSeconds") == pytest.approx(expected), (
        f"MoltenRoarWindUpSeconds is {constant('MoltenRoarWindUpSeconds')} and "
        f"the wind-up rule gives {expected:.4f} for a "
        f"{radius_metres} m marker. The rule is the LESSER of "
        f"{REACTION_ALLOWANCE} + Radius / {WALK_OUT_SPEED} and the "
        f"{MAXIMUM_WIND_UP_SECONDS} second ceiling, from the Attack Telegraphs "
        "subsection of docs/Cataclysm_GDD_v2.md.")

    # AND IT IS THE CEILING THAT DECIDES IT, not the formula. If the ring's
    # radius ever falls back below the crossover, this test would still pass
    # while the whole reason the ring was made bigger had quietly gone. Issues
    # #487 and #496.
    uncapped = REACTION_ALLOWANCE + radius_metres / WALK_OUT_SPEED
    assert uncapped > MAXIMUM_WIND_UP_SECONDS, (
        f"the ring's {radius_metres} m radius gives an uncapped wind-up of "
        f"{uncapped:.4f} s, which is under the {MAXIMUM_WIND_UP_SECONDS} s "
        "ceiling. The ring is designed to sit past the crossover, where the "
        "warning has stopped growing and extra radius is extra ground to cross "
        "with no extra time. Below it, a bigger ring is not a harder one.")


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


# --------------------------------------------------------------------------
# Stampede, the charge. Issue #491 built the Movement shape and this creature
# is its first customer.
#
# CONTINUOUS INTEGRATION RUNS NO C++ AT ALL, so every static_assert in the
# header is unchecked on a pull request. These read the numbers out of the
# source as text, which is what actually runs.
# --------------------------------------------------------------------------

def stampede():
    return next(a for a in warden_abilities() if a.slot == "Movement")


def test_the_model_still_designs_three_abilities():
    """The assumption every test below rests on."""
    designed = warden_abilities()
    assert len(designed) == 3, (
        f"the model now designs {len(designed)} abilities for the Abyssal "
        "Warden. These tests assume three: a swing, a charge and a ring.")


def test_the_charge_range_matches_the_designed_ability():
    designed = stampede().params["Range"] * CM_PER_METRE
    assert constant("StampedeRangeCm") == pytest.approx(designed), (
        f"StampedeRangeCm is {constant('StampedeRangeCm')} and the model "
        f"designs Range={stampede().params['Range']} metres, which is "
        f"{designed} cm. The model is authoritative.")


def test_the_charge_lane_half_width_matches_the_designed_ability():
    designed = stampede().params["Radius"] * CM_PER_METRE
    assert constant("StampedeRadiusCm") == pytest.approx(designed), (
        f"StampedeRadiusCm is {constant('StampedeRadiusCm')} and the model "
        f"designs Radius={stampede().params['Radius']} metres, which is "
        f"{designed} cm. That figure is both the half-width of the marker and "
        f"the half-width the charge hits within, so they cannot differ.")


def test_the_charge_cooldown_matches_the_designed_ability():
    assert constant("StampedeCooldownSeconds") == pytest.approx(
        stampede().cooldown), (
        f"StampedeCooldownSeconds is {constant('StampedeCooldownSeconds')} and "
        f"the model designs {stampede().cooldown}.")


def test_the_charge_is_a_charge_and_not_a_leap_or_a_blink():
    """The mode the art decided, recorded in the model.

    A Leap or a Blink would need different code: a leap hits only where it
    lands and a blink is not travel at all, where this hits along the way.
    """
    assert stampede().params["Mode"] == "Charge", (
        f"the model now designs Mode={stampede().params['Mode']} for Stampede. "
        "The C++ implements a Charge -- it travels along the ground hitting "
        "what it passes. A Leap hits only where it lands and a Blink hits at "
        "both ends and nothing between, so neither is this code.")


def test_the_charge_wind_up_is_the_formula_rather_than_a_number():
    """`0.4 + Radius / 3.5`, recomputed from the model's own constants."""
    from cataclysm_sim.enemy_abilities import (
        REACTION_ALLOWANCE, WALK_OUT_SPEED)

    radius_metres = constant("StampedeRadiusCm") / CM_PER_METRE
    expected = REACTION_ALLOWANCE + radius_metres / WALK_OUT_SPEED

    # THE DESIGN DOCUMENT AND THE MODEL BOTH ROUND IT TO 0.83, where the formula
    # gives 0.828571. The header carries the rounded figure and says so, so this
    # allows the rounding rather than demanding the full expansion.
    assert constant("StampedeWindUpSeconds") == pytest.approx(expected,
                                                              abs=0.002), (
        f"StampedeWindUpSeconds is {constant('StampedeWindUpSeconds')} and the "
        f"rule 0.4 + Radius / 3.5 gives {expected:.6f} for a "
        f"{radius_metres} metre radius. Change both or neither.")


def test_the_charge_damage_is_the_movement_slots_percentage():
    with (REPO_ROOT / "game" / "Data" / "SkillSlots.csv").open(
            encoding="utf-8-sig", newline="") as handle:
        rows = {row["Slot"]: row for row in csv.DictReader(handle)}

    movement = rows["Movement"]
    assert constant("StampedeDamagePercent") == pytest.approx(
        float(movement["DamagePercent"])), (
        f"StampedeDamagePercent is {constant('StampedeDamagePercent')} and the "
        f"Movement row of game/Data/SkillSlots.csv gives "
        f"{movement['DamagePercent']}.")


def test_the_charge_speed_covers_its_range_in_its_own_clip():
    """The rule the header states, recomputed rather than trusted.

    THE SPEED IS A JUDGEMENT and the header labels it one -- no shipped game
    publishes a monster charge speed. What is NOT a judgement is that it follows
    from the range and the clip length, which is the rule chosen. This checks the
    arithmetic, not the choice.
    """
    expected = constant("StampedeRangeCm") / constant("StampedeAnimationSeconds")
    assert constant("StampedeSpeedCmPerSecond") == pytest.approx(expected,
                                                                 abs=0.01), (
        f"StampedeSpeedCmPerSecond is {constant('StampedeSpeedCmPerSecond')} "
        f"and its range of {constant('StampedeRangeCm')} cm covered in the "
        f"{constant('StampedeAnimationSeconds')} second Stampede clip is "
        f"{expected:.4f}. The header's rule is that a charge covers its range "
        f"in the length of its own clip.")


def test_the_charge_outruns_the_fastest_player_class():
    """The bound that makes the charge worth having at all.

    A gap-closer slower than the thing it is closing on closes nothing. This is
    the reason the creature has the ability, so it is checked rather than
    assumed.
    """
    from cataclysm_sim.character import DEFAULT_STAT_LINE
    from cataclysm_sim.classes import MASOCHIST, RAVAGER, RITUALIST

    # THE THREE DEMONIC CLASSES, read off their own definitions rather than
    # copied. A class that does not override its movement speed uses the base
    # stat line's 4.0, which is why this falls back rather than indexing --
    # only two of the three state one.
    def speed_of(cls):
        scaling = cls.overrides.get("movement_speed")
        if scaling is None:
            scaling = DEFAULT_STAT_LINE["movement_speed"]
        return scaling.base

    fastest = max(speed_of(cls)
                  for cls in (RAVAGER, RITUALIST, MASOCHIST)) * CM_PER_METRE
    assert constant("StampedeSpeedCmPerSecond") > fastest, (
        f"StampedeSpeedCmPerSecond is {constant('StampedeSpeedCmPerSecond')} "
        f"and the fastest player class moves at {fastest} cm/s. A charge no "
        f"faster than the player cannot close a gap, which is the only reason "
        f"this creature has one -- it walks at "
        f"{constant('DesignedWalkSpeedCmPerSecond')} cm/s with no chase speed.")


def test_the_charge_beats_walking_during_its_own_wind_up():
    """The design's own test for whether a charge is worth winding up for.

    Stated in the Hellhound's section of the design document: "a charge shorter
    than that would be strictly worse than not winding up at all". The creature
    could simply walk during the wind-up, so the charge has to cover more ground
    than that walk would.
    """
    walked = (constant("DesignedWalkSpeedCmPerSecond")
              * constant("StampedeWindUpSeconds"))
    assert constant("StampedeRangeCm") > walked, (
        f"Stampede covers {constant('StampedeRangeCm')} cm and the creature "
        f"could walk {walked:.1f} cm during its own "
        f"{constant('StampedeWindUpSeconds')} second wind-up. A charge that "
        f"covers less than that is strictly worse than not winding up.")


def test_the_charge_refuses_a_target_it_could_simply_walk_to():
    """The minimum range, derived from the same walk. Not a picked number."""
    walked = (constant("DesignedWalkSpeedCmPerSecond")
              * constant("StampedeWindUpSeconds"))
    assert constant("StampedeMinimumRangeCm") == pytest.approx(walked,
                                                               abs=0.01), (
        f"StampedeMinimumRangeCm is {constant('StampedeMinimumRangeCm')} and "
        f"the creature walks {walked:.4f} cm during its own wind-up. Inside "
        f"that distance it arrives sooner by taking a step, so charging is "
        f"strictly worse. Change the walk speed or the wind-up and this moves "
        f"with them.")


def test_the_charge_is_built_and_is_reached_by_the_brain():
    """The C++ side: it exists, it is a Movement shape, and it is not first.

    THE ORDERING IS LOAD-BEARING. `ACataclysmEnemyController::ChooseAbility`
    takes the first entry whose range and cooldown fit, without looking at the
    shape. Stampede is legal from 2.32 to 8.00 metres on a 5 second cooldown and
    Molten Roar from 0 to 5.60 on a 12 second one, so listing the charge first
    would crowd the ring out of the band where both are legal.
    """
    text = source(WARDEN_CPP)

    assert "BeginCharge(" in text, (
        "CataclysmAbyssalWardenCharacter.cpp no longer calls BeginCharge, so "
        "its Stampede entry would be chosen by the brain and then do nothing. "
        "Issue #491 is the whole reason the Movement shape exists.")

    assert "ECataclysmSkillShape::Movement" in text, (
        "Stampede is no longer declared as a Movement shape. The controller "
        "draws its lane and captures the far end of the charge as the aim point "
        "by reading that shape, so any other shape silently changes both.")

    roar_at = text.index("MoltenRoar.Name")
    charge_at = text.index("Stampede.Name")
    assert roar_at < charge_at, (
        "Stampede is now built before Molten Roar in EnemyAbilities. Order in "
        "that array is priority and ChooseAbility takes the first entry that "
        "fits without looking at the shape, so the 5 second charge would crowd "
        "out the 12 second ring everywhere both are legal.")


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


def test_every_dressed_enemy_hides_its_placeholder_cylinder():
    """A creature that puts a real mesh on must turn the placeholder off.

    THE DEFECT THIS WAS WRITTEN FOR, reported by the project owner on 2026-08-09
    within minutes of first seeing the Abyssal Warden: "the cylinder base is
    appearing over him". `ACataclysmEnemyCharacter` creates a `PlaceholderBody`
    static mesh component in its constructor -- an engine cylinder -- and
    assigning a skeletal mesh does not remove it. The Brute hides it and the
    Warden did not.

    WHY IT WAS MISSED. In `ACataclysmBruteCharacter::ResolveBody` those four
    lines sit after two screens of rock, crater and montage loading that the
    Warden has none of, so the shorter function looked complete.

    WRITTEN FOR EVERY ENEMY RATHER THAN THIS ONE, because the next creature
    dressed from a Paragon pack will hit exactly the same thing. "Dressed" is
    the same proxy `tools/tests/test_game_readme_is_true.py` uses: a character
    class naming a Paragon content path.

    A TEXT CHECK RATHER THAN A BEHAVIOUR ONE, because continuous integration
    never builds the C++. `Cataclysm.Warden.ItHidesItsPlaceholderOnceDressed` is
    the behaviour check and it only runs on a machine with the editor.
    """
    character_dir = REPO_ROOT / "game" / "Source" / "Cataclysm"

    dressed = [
        path for path in character_dir.rglob("*.cpp")
        if "Tests" not in path.parts
        and "/Game/Paragon" in path.read_text(encoding="utf-8", errors="replace")
    ]

    assert dressed, (
        "no character class names a Paragon content path any more, so this "
        "check has nothing to look at. If the art was removed, say so; if the "
        "paths moved, point this at them.")

    for path in dressed:
        text = path.read_text(encoding="utf-8", errors="replace")
        assert "PlaceholderBody" in text, (
            f"{path.name} puts a real skeletal mesh on but never mentions "
            "PlaceholderBody. ACataclysmEnemyCharacter creates a placeholder "
            "cylinder in its constructor and assigning a mesh does not remove "
            "it, so the cylinder renders on top of the creature.")

        assert re.search(
            r"PlaceholderBody\s*\)\s*\{?\s*(?://[^\n]*\n\s*)*"
            r"PlaceholderBody->SetVisibility\(\s*false\s*\)",
            text, re.MULTILINE) or (
            "PlaceholderBody->SetVisibility(false)" in text), (
            f"{path.name} mentions PlaceholderBody but never calls "
            "SetVisibility(false) on it. The placeholder cylinder will render "
            "on top of the creature's real mesh.")


def test_the_attack_interval_clears_the_swing_clip_it_plays():
    """Nothing rate-scales an ordinary swing, so the interval is a hard floor.

    The two swing clips are 1.1333 seconds each. The length is read out of
    game/docs/enemy-source-assets.md rather than written here, so there is one
    copy of the measurement.
    """
    record = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
    if not record.is_file():
        pytest.fail("game/docs/enemy-source-assets.md does not exist")

    # ALL THREE CLIPS, NOT ONE. A basic attack became a left swing, a right
    # swing and a recovery on 2026-08-09, so checking one clip against the
    # interval would prove something adjacent to the thing that matters.
    played = ["PrimaryAttack_LA_Fast", "PrimaryAttack_RA_Fast",
              "PrimaryAttack_RA_Recovery"]

    text = record.read_text(encoding="utf-8")
    total = 0.0
    for name in played:
        stated = re.search(rf"`{re.escape(name)}`\s*\|\s*([\d.]+)\s*\|", text)
        if stated is None:
            pytest.fail(
                f"game/docs/enemy-source-assets.md no longer records the length "
                f"of {name}, one of the three clips the Abyssal Warden's basic "
                f"attack plays. Without it nothing knows whether the combo fits "
                f"inside the attack interval.")
        total += float(stated.group(1))

    assert total <= constant("DesignedAttackIntervalSeconds"), (
        f"the Abyssal Warden attacks every "
        f"{constant('DesignedAttackIntervalSeconds')} s and the three clips of "
        f"its basic attack are {total:.4f} s together. Nothing rate-scales "
        f"them, so the creature starts an attack it has not finished. The "
        f"full-speed swings would be 3.1 s, which is why the fast variants are "
        f"the ones in use.")
