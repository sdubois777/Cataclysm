"""The Corrupted Sentinel's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmCorruptedSentinelCharacter.h`
hard-codes the creature's whole stat block and both of its abilities, and every
one of those numbers also lives in the Python model. Two copies of one number
drift, and in this repository they have.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Corrupted Sentinel"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ABILITIES["Corrupted Sentinel"]` and
`ATTACK_REACH["Corrupted Sentinel"]` in `sim/cataclysm_sim/enemy_abilities.py`
are where the creature is designed. When this file fails, the usual fix is to
change the C++.

WHY IT EXISTS AT ALL RATHER THAN AN ENGINE TEST. Continuous integration never
builds the C++ and never opens the editor, so an automation test cannot run on a
pull request. This reads the source text, which is present either way.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second; Unreal
works in centimetres. The factor of 100 is stated at each conversion.

THE THING THIS CREATURE HAS THAT NO OTHER DOES is a telegraphed BASIC attack, so
several of the checks below are about the shape that takes in the engine rather
than about a number.
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
SENTINEL_HEADER = CHARACTER_DIR / "CataclysmCorruptedSentinelCharacter.h"
SENTINEL_SOURCE = CHARACTER_DIR / "CataclysmCorruptedSentinelCharacter.cpp"
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")
GAME_MODE_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.cpp")
ASSET_NOTES = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
LEVEL_SCRIPT = REPO_ROOT / "tools" / "generate_input_assets.py"

CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path = SENTINEL_HEADER) -> float:
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


def whole_number_constant(name: str,
                          path: pathlib.Path = SENTINEL_HEADER) -> int:
    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no "
                    f"'static constexpr int32 {name} = <number>;' line.")
    return int(match.group(1))


def property_default(name: str, path: pathlib.Path = GAME_MODE_HEADER) -> float:
    match = re.search(
        rf"\bfloat\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'float {name} = <number>f;' line.")
    return float(match.group(1))


def whole_number_property(name: str,
                          path: pathlib.Path = GAME_MODE_HEADER) -> int:
    match = re.search(
        rf"\bint32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;", source(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'int32 {name} = <number>;' line.")
    return int(match.group(1))


def sentinel():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Corrupted Sentinel")


def ability(name: str):
    from cataclysm_sim.enemy_abilities import abilities

    for entry in abilities("Corrupted Sentinel"):
        if entry.name == name:
            return entry
    pytest.fail(
        f"the Corrupted Sentinel has no ability called {name!r} in ABILITIES "
        f"in sim/cataclysm_sim/enemy_abilities.py. If it was renamed, rename it "
        f"here too.")


def siege_bolt():
    return ability("Siege Bolt")


def brimstone_mortar():
    return ability("Brimstone Mortar")


# --------------------------------------------------------------------------
# The profile, straight off the archetype
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, model_value, scale, what", [
    ("DesignedAttackIntervalSeconds", "attack_interval", 1.0,
     "seconds between shots"),
    ("DesignedResistancePercent", "resistance", 1.0,
     "percent of all incoming damage resisted"),
    ("DesignedCritChancePercent", "crit_chance", 1.0, "critical strike chance"),
    ("DesignedCritMultiplierPercent", "crit_multiplier", 1.0,
     "critical strike multiplier"),
    ("DesignedEvasionPercent", "evasion", 1.0, "evasion"),
    ("DesignedEnergyShieldFraction", "energy_shield_fraction", 1.0,
     "what fraction of its health is an energy shield"),
    ("DesignedWalkSpeedCmPerSecond", "move_speed", CM_PER_METRE,
     "how fast it moves"),
    ("DesignedTurnRateDegreesPerSecond", "turn_rate_degrees", 1.0,
     "how fast it turns"),
])
def test_the_profile_matches_the_archetype(cpp_name, model_value, scale, what):
    designed = getattr(sentinel(), model_value) * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} in CataclysmCorruptedSentinelCharacter.h is "
        f"{constant(cpp_name)} and ARCHETYPES['Corrupted Sentinel']."
        f"{model_value} in sim/cataclysm_sim/enemy_stats.py gives {designed} "
        f"for {what}. The model is authoritative.")


def test_it_cannot_move_at_all_and_it_is_the_only_one():
    """Zero, and not a small number. It is the whole creature."""
    from cataclysm_sim.enemy_stats import ARCHETYPES

    assert sentinel().move_speed == 0.0, (
        f"the Corrupted Sentinel now moves at {sentinel().move_speed} m/s. Its "
        f"design is one line -- 'Stationary ranged. Forces the player to stay "
        f"mobile' -- and a turret that walks is a different creature.")
    assert sentinel().chase_speed == 0.0, (
        "the Corrupted Sentinel now has a chase speed, which for a creature "
        "with no movement speed is a contradiction.")

    others = [name for name, a in ARCHETYPES.items()
              if name not in ("Baseline", "Corrupted Sentinel")
              and a.move_speed == 0.0]
    assert not others, (
        f"something other than the Corrupted Sentinel now cannot move: "
        f"{others}. That is fine, and the comments saying it is the only one "
        f"need changing.")

    assert constant("SentinelRoamRadiusCm") == 0.0, (
        "the Corrupted Sentinel has a roam radius, and it cannot walk to "
        "anywhere it might roam to.")


def test_it_has_the_first_energy_shield_of_any_built_creature():
    """An energy shield sits in front of health and armour does not reduce it,
    so it changes what killing the creature costs rather than only how much."""
    assert sentinel().energy_shield_fraction > 0.0, (
        "the Corrupted Sentinel's energy shield fraction is now zero, and it is "
        "what makes this creature harder to burst down than its health says.")

    for other in ("Brute", "Abyssal Warden", "Hellhound", "Imp"):
        from cataclysm_sim.enemy_stats import archetype

        assert archetype(other).energy_shield_fraction == 0.0, (
            f"{other} now has an energy shield too, so the comment calling the "
            f"Corrupted Sentinel the first built creature with one is stale.")


def test_the_capsule_radius_is_the_designed_body_radius():
    designed = sentinel().body_radius * CM_PER_METRE
    assert constant("SentinelCapsuleRadius") == pytest.approx(designed), (
        f"SentinelCapsuleRadius is {constant('SentinelCapsuleRadius')} and the "
        f"model's body_radius is {sentinel().body_radius} m. That figure is the "
        f"unmeasured default six creatures share, which is issue #366.")


def test_the_capsule_half_height_comes_from_the_mesh():
    """The design gives this creature a width and no height at all."""
    mesh_height = 196.2
    assert constant("SentinelCapsuleHalfHeight") == pytest.approx(
        mesh_height / 2.0, abs=0.05), (
        f"SentinelCapsuleHalfHeight is {constant('SentinelCapsuleHalfHeight')} "
        f"and the mesh measures {mesh_height} cm tall, so half of it is "
        f"{mesh_height / 2.0}.")


def test_it_notices_as_far_as_it_can_shoot():
    """A turret whose notice radius is shorter than its range has dead ground it
    could shoot across and refuses to. Every other creature notices at 1000 cm
    and closes the difference on foot; this one cannot close anything."""
    assert constant("SentinelNoticeRadiusCm") >= constant("SiegeBoltRangeCm"), (
        f"the Corrupted Sentinel notices at {constant('SentinelNoticeRadiusCm')} "
        f"cm and shoots {constant('SiegeBoltRangeCm')} cm, so the difference is "
        f"ground it could hit and never will.")


# --------------------------------------------------------------------------
# Siege Bolt, the telegraphed basic attack
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, param, scale, what", [
    ("SiegeBoltRangeCm", "Range", CM_PER_METRE, "how far it shoots"),
    ("SiegeBoltRadiusCm", "Radius", CM_PER_METRE, "half the lane's width"),
    ("SiegeBoltSpeedCmPerSecond", "Speed", 1.0, "how fast the bolt travels"),
])
def test_the_bolt_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = siege_bolt().params[param] * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} is {constant(cpp_name)} and ABILITIES['Corrupted "
        f"Sentinel']'s Siege Bolt gives {designed} for {what}.")


def test_the_bolt_is_the_basic_attack_and_has_no_cooldown():
    """Which is what makes it a basic attack rather than something on a timer.

    `ACataclysmEnemyController::UseAbilitiesOn` gates every ability by the
    creature's own `SecondsBetweenAttacks()` as well as by its cooldown, so an
    ability with no cooldown fires exactly on the attack interval.
    """
    assert siege_bolt().slot == "Basic", (
        f"Siege Bolt's slot is now {siege_bolt().slot!r}, and the class treats "
        f"it as the basic attack -- it is the reason AttackTarget is overridden "
        f"to do nothing.")
    assert siege_bolt().cooldown == 0.0, (
        f"Siege Bolt now has a cooldown of {siege_bolt().cooldown}, so it is no "
        f"longer spaced by the attack interval alone and the creature has no "
        f"basic attack at all.")
    assert constant("SiegeBoltCooldownSeconds") == 0.0


def test_the_bolt_is_telegraphed_and_uses_its_whole_allowance():
    """Its radius is exactly the largest the rule allows for a 2.0 second
    interval, so its wind-up is exactly half its interval."""
    from cataclysm_sim.enemy_abilities import is_telegraphed, wind_up_seconds

    assert is_telegraphed(siege_bolt(), "Corrupted Sentinel"), (
        "Siege Bolt is no longer telegraphed in the model, and the class draws "
        "a marker for it. A basic attack that marks the ground when the design "
        "says it should not is worse than one that does not when it should.")

    interval = constant("DesignedAttackIntervalSeconds")
    largest_radius_metres = 3.5 * (interval / 2.0 - 0.4)

    assert (constant("SiegeBoltRadiusCm") / CM_PER_METRE
            == pytest.approx(largest_radius_metres)), (
        f"Siege Bolt's radius is {constant('SiegeBoltRadiusCm') / CM_PER_METRE} "
        f"m and the largest a {interval} s interval allows is "
        f"{largest_radius_metres} m. The design has it using the whole of its "
        f"allowance.")

    designed_wind_up = wind_up_seconds(siege_bolt().params["Radius"])
    assert constant("SiegeBoltWindUpSeconds") == pytest.approx(
        designed_wind_up, abs=0.002), (
        f"SiegeBoltWindUpSeconds is {constant('SiegeBoltWindUpSeconds')} and "
        f"the rule 0.4 + Radius / 3.5 gives {designed_wind_up}.")

    assert constant("SiegeBoltWindUpSeconds") == pytest.approx(interval / 2.0), (
        "Siege Bolt's wind-up is no longer exactly half its attack interval, "
        "which is what using the whole telegraph allowance means.")


def test_the_bolt_crosses_its_range_in_a_time_a_player_can_read():
    """1400 cm/s over 1400 cm is exactly one second, and that is not a
    coincidence worth losing."""
    seconds = constant("SiegeBoltRangeCm") / constant("SiegeBoltSpeedCmPerSecond")
    assert seconds == pytest.approx(1.0), (
        f"the bolt now crosses its whole range in {seconds:.3f} s. At one "
        f"second a player who leaves the lane as the marker goes still has "
        f"time; much faster and the marker is the only warning there is.")


def test_the_bolt_damage_is_the_basic_slots_percentage():
    slot = 100.0
    assert constant("SiegeBoltDamagePercent") == pytest.approx(slot), (
        f"SiegeBoltDamagePercent is {constant('SiegeBoltDamagePercent')} and "
        f"the Basic row of game/Data/SkillSlots.csv is {slot}. A basic attack "
        f"IS weapon damage, which is what makes it the anchor.")


# --------------------------------------------------------------------------
# Brimstone Mortar
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, param, scale, what", [
    ("BrimstoneMortarRangeCm", "Range", CM_PER_METRE, "how far it lobs"),
    ("BrimstoneMortarRadiusCm", "Radius", CM_PER_METRE, "how wide it bursts"),
    ("BrimstoneMortarApexFraction", "Arc", 1.0, "how far it sags"),
])
def test_the_mortar_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = brimstone_mortar().params[param] * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} is {constant(cpp_name)} and the designed Brimstone Mortar "
        f"gives {designed} for {what}.")


def test_the_mortar_cooldown_matches_the_designed_ability():
    assert constant("BrimstoneMortarCooldownSeconds") == pytest.approx(
        brimstone_mortar().cooldown)


def test_the_mortar_lobs_and_the_bolt_does_not():
    """A projectile states Speed or Arc, never both, and which one it states is
    what decides whether its marker is a lane or a circle."""
    assert "Arc" in brimstone_mortar().params
    assert "Speed" not in brimstone_mortar().params, (
        "Brimstone Mortar now states a Speed as well as an Arc. The model's own "
        "comment says a projectile states one or the other, and the engine "
        "treats a flight time and a speed as different kinds of shot.")

    assert "Speed" in siege_bolt().params
    assert "Arc" not in siege_bolt().params, (
        "Siege Bolt now states an Arc, so it lobs -- and the class draws it a "
        "LANE, which marks ground a lobbed shot flies over.")

    text = source(SENTINEL_SOURCE)
    assert "Mortar.bArcsOntoItsTarget = true" in text.replace("\t", "")
    assert "Bolt.bArcsOntoItsTarget = false" in text.replace("\t", "")


def test_the_mortar_wind_up_is_the_formula_rather_than_a_number():
    from cataclysm_sim.enemy_abilities import wind_up_seconds

    designed = wind_up_seconds(brimstone_mortar().params["Radius"])
    assert constant("BrimstoneMortarWindUpSeconds") == pytest.approx(
        designed, abs=0.002), (
        f"BrimstoneMortarWindUpSeconds is "
        f"{constant('BrimstoneMortarWindUpSeconds')} and the rule "
        f"0.4 + Radius / 3.5 gives {designed}.")


def test_both_abilities_fit_their_own_cycle():
    """The model's own legality check, run on both.

    A marker's radius is bounded by half the CYCLE the ability runs on, and the
    two abilities run on different ones: the bolt on the 2.0 second attack
    interval, the mortar on its own 8 second cooldown.
    """
    from cataclysm_sim.enemy_abilities import fits_its_cycle

    for entry in (siege_bolt(), brimstone_mortar()):
        assert fits_its_cycle(entry, sentinel()), (
            f"{entry.name} no longer fits its own cycle, so its marker cannot "
            f"be walked out of with the reaction allowance the design promises.")


def test_the_mortar_refuses_a_target_standing_on_top_of_it():
    """Below `marker radius + own body radius` the circle the shell marks covers
    the ground the creature is standing on, which is a melee attack wearing a
    thrown attack's telegraph. Issue #475 found that on the Brute."""
    expected = (constant("BrimstoneMortarRadiusCm")
                + constant("SentinelCapsuleRadius"))
    assert constant("BrimstoneMortarMinimumRangeCm") == pytest.approx(expected)

    assert (constant("BrimstoneMortarMinimumRangeCm")
            < constant("BrimstoneMortarRangeCm")), (
        "the mortar's minimum range has passed its maximum, so it can never be "
        "used at all.")


def test_the_mortar_damage_is_the_special_slots_percentage():
    slot = 150.0
    assert constant("BrimstoneMortarDamagePercent") == pytest.approx(slot), (
        f"BrimstoneMortarDamagePercent is "
        f"{constant('BrimstoneMortarDamagePercent')} and the Special row of "
        f"game/Data/SkillSlots.csv is {slot}.")


def test_the_mortar_is_listed_before_the_bolt():
    """ChooseAbility takes the first entry whose range and cooldown fit and
    never looks at the shape. Siege Bolt has no cooldown and the same range, so
    listing it first would make it the only thing the creature ever does."""
    text = source(SENTINEL_HEADER)
    match = re.search(
        r"enum\s*:\s*int32\s*\{([^}]*)\}", text)
    assert match is not None, (
        "CataclysmCorruptedSentinelCharacter.h no longer names its abilities in "
        "an enumeration, so which one the brain reaches for first cannot be "
        "read here.")

    order = match.group(1)
    mortar = order.index("BrimstoneMortarAbility")
    bolt = order.index("SiegeBoltAbility")
    assert mortar < bolt, (
        "Siege Bolt is now listed before Brimstone Mortar. It has a cooldown of "
        "zero, so it would be chosen every time and the mortar would never "
        "fire. That is issue #491 on the Abyssal Warden with the numbers "
        "changed.")


def test_the_reach_matches_the_ability_table():
    """Its reach is its shot's range, which is what makes a creature that cannot
    walk stop trying to close."""
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    designed = ATTACK_REACH["Corrupted Sentinel"] * CM_PER_METRE
    assert constant("SiegeBoltRangeCm") == pytest.approx(designed)


def test_the_class_does_not_deal_a_free_melee_hit():
    """The base class's AttackTarget applies direct damage at MeleeReachCm, and
    this creature's reach is fourteen metres."""
    text = source(SENTINEL_SOURCE)
    match = re.search(
        r"void\s+ACataclysmCorruptedSentinelCharacter::AttackTarget\s*\("
        r"[^)]*\)\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match is not None, (
        "CataclysmCorruptedSentinelCharacter.cpp no longer overrides "
        "AttackTarget. The base class applies direct damage at MeleeReachCm, "
        "which for this creature is 14 metres, so it would deal a free melee "
        "hit at that range every two seconds on top of its bolt.")

    body = re.sub(r"//[^\n]*", "", match.group(1))
    assert not body.strip(), (
        "AttackTarget now does something. Its basic attack is Siege Bolt, which "
        "is an entry in EnemyAbilities because it is telegraphed, so this "
        "function must stay empty.")


# --------------------------------------------------------------------------
# Its art
# --------------------------------------------------------------------------

def test_the_firing_clip_fits_the_interval_at_a_rate_the_project_allows():
    """It is the only creature whose attack clip is LONGER than its interval.

    Issue #369 settled what to do: the clip is played to fit the interval, which
    for 2.40 seconds into 2.00 is 1.20.
    """
    clip = constant("FireAnimationSeconds")
    interval = constant("DesignedAttackIntervalSeconds")
    needed = clip / interval
    ceiling = constant("MaximumPlayRate")

    assert needed > 1.0, (
        f"the firing clip is now {clip} s against a {interval} s interval, so "
        f"it no longer needs speeding up. The comments calling this the only "
        f"creature whose attack clip is longer than its interval are stale.")
    assert needed <= ceiling, (
        f"the firing clip needs a play rate of {needed:.3f} and the ceiling is "
        f"{ceiling}. Above it the clip is clamped and one shot is still playing "
        f"when the next begins. The pack's alternatives -- Fire_A, Fire_B and "
        f"Fire_C -- are 2.80 s and unrooted, so there is nothing shorter.")


def test_it_alternates_two_firing_clips():
    assert whole_number_constant("FireAnimationCount") == 2, (
        "the Corrupted Sentinel no longer alternates two firing clips. "
        "game/docs/enemy-source-assets.md says why it should: at a play rate of "
        "1.20 there is no gap between one shot and the next, so a single clip "
        "reads as one animation looping.")


def test_it_has_eight_ways_to_fall_over():
    """The most in the project. The Brute ships one, the Warden and the
    Hellhound two, the Imp five."""
    assert whole_number_constant("DeathAnimationCount") == 8


def test_the_asset_notes_record_what_was_measured():
    notes = source(ASSET_NOTES)

    for figure in ("2.40", "196.2", "0.03"):
        assert figure in notes, (
            f"game/docs/enemy-source-assets.md does not record {figure}, which "
            f"CataclysmCorruptedSentinelCharacter.h depends on.")


# --------------------------------------------------------------------------
# The sandbox scaffolding
# --------------------------------------------------------------------------

def sandbox_stat_block():
    from cataclysm_sim.enemy_stats import stats_on_floor

    return stats_on_floor("Common", 1, "Cataclysm", total_floors=50, floor=50,
                          kind="Corrupted Sentinel")


def test_the_sandbox_actually_spawns_one():
    assert whole_number_property("CorruptedSentinelCount") > 0, (
        "CorruptedSentinelCount is zero, so no Corrupted Sentinel is placed in "
        "the sandbox and the creature cannot be looked at.")

    assert "SpawnCorruptedSentinels();" in source(GAME_MODE_SOURCE), (
        "ACataclysmGameMode::StartPlay does not call SpawnCorruptedSentinels, "
        "so the spawner exists and nothing runs it.")


def test_the_sandbox_health_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().health
    written = property_default("CorruptedSentinelHealth")

    assert written == pytest.approx(round(designed)), (
        f"CorruptedSentinelHealth is {written} and the design model gives a "
        f"Common Corrupted Sentinel at tier 1, on the last floor of a 50-floor "
        f"Cataclysm dungeon, {designed:.2f}.")


def test_the_sandbox_armour_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().armor
    written = property_default("CorruptedSentinelArmour")

    assert written == pytest.approx(round(designed)), (
        f"CorruptedSentinelArmour is {written} and the design model gives "
        f"{designed:.2f} at the same encounter as its health.")


def test_the_sandbox_damage_is_the_dummys_times_the_designed_share():
    dummy = property_default("TrainingDummyAttackDamage")
    expected = dummy * sentinel().damage_share
    written = property_default("CorruptedSentinelAttackDamage")

    assert written == pytest.approx(expected), (
        f"CorruptedSentinelAttackDamage is {written} and the training dummy's "
        f"{dummy} times the designed damage share of {sentinel().damage_share} "
        f"is {expected}.")


def test_it_is_spawned_just_beyond_its_own_range():
    """Far enough that it cannot hit a player who has only just appeared, close
    enough that finding out what it does is one step rather than a walk. It will
    not come to you."""
    distance = property_default("CorruptedSentinelDistanceCm")
    shoots = constant("SiegeBoltRangeCm")

    assert distance > shoots, (
        f"the Corrupted Sentinel spawns {distance} cm out and shoots {shoots} "
        f"cm, so it opens fire the instant the level opens.")
    assert distance < shoots * 1.5, (
        f"the Corrupted Sentinel spawns {distance} cm out and shoots only "
        f"{shoots} cm. It cannot move, so every centimetre beyond its range is "
        f"ground the player has to cross before the creature does anything.")


def test_it_is_spawned_on_a_bearing_of_its_own():
    """Four creatures already occupy four directions, and the floor reaches
    2000 cm in each."""
    import itertools

    placed = {
        "the Brute": (property_default("BruteDistanceCm"), 0.0),
        "the Abyssal Warden": (property_default("AbyssalWardenDistanceCm"), 0.0),
    }
    for name, distance_key, bearing_key in (
            ("the Hellhound", "HellhoundDistanceCm", "HellhoundBearingDegrees"),
            ("the Imp pack", "ImpDistanceCm", "ImpBearingDegrees"),
            ("the Corrupted Sentinel", "CorruptedSentinelDistanceCm",
             "CorruptedSentinelBearingDegrees")):
        bearing = math.radians(property_default(bearing_key))
        distance = property_default(distance_key)
        placed[name] = (distance * math.cos(bearing),
                        distance * math.sin(bearing))

    # NOT INSIDE EACH OTHER'S NOTICE RADIUS, which is the strongest of the
    # notice radii in play. Two creatures that start inside one another's are
    # fighting each other's neighbours rather than waiting to be walked up to.
    needed = constant("SentinelNoticeRadiusCm")
    sentinel_at = placed["the Corrupted Sentinel"]

    for name, where in placed.items():
        if name == "the Corrupted Sentinel":
            continue
        gap = math.dist(sentinel_at, where)
        assert gap > needed, (
            f"the Corrupted Sentinel stands {gap:.0f} cm from {name}, inside "
            f"the {needed:.0f} cm at which it notices anybody.")

    # AND NO TWO CREATURES SHARE A PLACE, which a bearing typed twice would do.
    for left, right in itertools.combinations(placed, 2):
        assert math.dist(placed[left], placed[right]) > 1.0, (
            f"{left} and {right} are placed on the same spot.")


def test_it_is_spawned_inside_the_sandbox_floor():
    floor_extent = re.search(r"^FLOOR_EXTENT\s*=\s*(\d+(?:\.\d+)?)",
                             source(LEVEL_SCRIPT), re.MULTILINE)
    assert floor_extent is not None, (
        "tools/generate_input_assets.py no longer defines FLOOR_EXTENT.")

    reach = float(floor_extent.group(1)) / 2.0
    furthest = (property_default("CorruptedSentinelDistanceCm")
                + constant("SentinelCapsuleRadius"))

    assert furthest < reach, (
        f"the Corrupted Sentinel reaches {furthest} cm out and the sandbox "
        f"floor only reaches {reach} cm. **It cannot walk back on**, so a "
        f"creature spawned over the edge is stuck there for ever.")
