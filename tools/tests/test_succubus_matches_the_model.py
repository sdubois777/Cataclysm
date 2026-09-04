"""The Succubus's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmSuccubusCharacter.h` hard-codes the
creature's whole stat block and all three of its abilities, and every one of
those numbers also lives in the Python model. Two copies of one number drift, and
in this repository they have.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Succubus"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ABILITIES["Succubus"]` and
`ATTACK_REACH["Succubus"]` in `sim/cataclysm_sim/enemy_abilities.py` are where
the creature is designed. When this file fails, the usual fix is to change the
C++.

WHY IT EXISTS AT ALL RATHER THAN AN ENGINE TEST. Continuous integration never
builds the C++ and never opens the editor, so an automation test cannot run on a
pull request. This reads the source text, which is present either way.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second; Unreal
works in centimetres. The factor of 100 is stated at each conversion.

WHAT THIS CREATURE HAS THAT NO OTHER DOES is an ability that is not an ability:
Dominion is held on for as long as the creature lives, so it is not an entry in
`EnemyAbilities` and several of the checks below are about that shape rather than
about a number.
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
SUCCUBUS_HEADER = CHARACTER_DIR / "CataclysmSuccubusCharacter.h"
SUCCUBUS_SOURCE = CHARACTER_DIR / "CataclysmSuccubusCharacter.cpp"
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")
GAME_MODE_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.cpp")
ASSET_NOTES = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
TAGS_INI = REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini"
LEVEL_SCRIPT = REPO_ROOT / "tools" / "generate_input_assets.py"

CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path = SUCCUBUS_HEADER) -> float:
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
                          path: pathlib.Path = SUCCUBUS_HEADER) -> int:
    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no "
                    f"'static constexpr int32 {name} = <number>;' line.")
    return int(match.group(1))


def text_constant(name: str, path: pathlib.Path = SUCCUBUS_SOURCE) -> str:
    """The value of a `const TCHAR* Class::<name> = TEXT("...");` line."""
    match = re.search(
        rf"{re.escape(name)}\s*=\s*\n?\s*TEXT\(\"([^\"]*)\"\)\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no "
                    f"'{name} = TEXT(\"...\");' line.")
    return match.group(1)


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


def succubus():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Succubus")


def ability(name: str):
    from cataclysm_sim.enemy_abilities import abilities

    for entry in abilities("Succubus"):
        if entry.name == name:
            return entry
    pytest.fail(
        f"the Succubus has no ability called {name!r} in ABILITIES in "
        f"sim/cataclysm_sim/enemy_abilities.py. If it was renamed, rename it "
        f"here too.")


def soulfire():
    return ability("Soulfire")


def wither():
    return ability("Wither the Living")


def dominion():
    return ability("Dominion")


# --------------------------------------------------------------------------
# The profile, straight off the archetype
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,model_value,scale,what", [
    ("DesignedAttackIntervalSeconds", "attack_interval", 1.0,
     "seconds between shots"),
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
    designed = getattr(succubus(), model_value) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and the model's {model_value} is "
        f"{designed} ({what}). sim/cataclysm_sim/enemy_stats.py is "
        f"authoritative.")


def test_it_carries_the_largest_energy_shield_in_the_roster():
    """0.50 against the Corrupted Sentinel's 0.35 and zero for the other five.
    A shield is not reduced by armour, and this creature's armour share is
    nearly the lowest in the slice, so the shield is most of what killing it
    costs."""
    from cataclysm_sim.enemy_stats import ARCHETYPES

    fractions = {name: kind.energy_shield_fraction
                 for name, kind in ARCHETYPES.items()}
    largest = max(fractions.values())

    assert fractions["Succubus"] == largest, (
        f"the Succubus's energy shield fraction is "
        f"{fractions['Succubus']} and something else in the roster has "
        f"{largest}: {fractions}. If that is a real design change, this test's "
        f"claim is stale and the header repeats it.")
    assert constant("DesignedEnergyShieldFraction") == pytest.approx(largest)


def test_the_capsule_radius_is_the_designed_body_radius():
    designed = succubus().body_radius * CM_PER_METRE
    written = constant("SuccubusCapsuleRadius")

    assert written == pytest.approx(designed), (
        f"SuccubusCapsuleRadius is {written} cm and the model's body_radius is "
        f"{succubus().body_radius} m, which is {designed} cm.")


def test_the_capsule_half_height_comes_from_the_mesh():
    """Measured rather than chosen. The mesh is 180.8 cm tall, so half is
    90.4."""
    written = constant("SuccubusCapsuleHalfHeight")
    notes = source(ASSET_NOTES)

    match = re.search(r"\|\s*The Succubus\s*\|\s*(\d+(?:\.\d+)?)\s*cm\s*\|",
                      notes)
    assert match is not None, (
        "game/docs/enemy-source-assets.md no longer records the Succubus's "
        "mesh height, so nothing says where the capsule's half-height came "
        "from.")

    height = float(match.group(1))
    assert written == pytest.approx(height / 2.0, abs=0.05), (
        f"SuccubusCapsuleHalfHeight is {written} and the recorded mesh height "
        f"is {height} cm, half of which is {height / 2.0}.")


def test_it_notices_further_than_it_can_shoot():
    """It walks, unlike the Corrupted Sentinel, so it can close what it
    notices."""
    notices = constant("SuccubusNoticeRadiusCm")
    shoots = constant("SoulfireRangeCm")

    assert notices > shoots, (
        f"the Succubus notices at {notices} cm and shoots {shoots} cm, so "
        f"there is ground it can hit and refuses to walk towards.")


# --------------------------------------------------------------------------
# Soulfire, the telegraphed basic attack
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,param,scale,what", [
    ("SoulfireRangeCm", "Range", CM_PER_METRE, "how far it shoots"),
    ("SoulfireRadiusCm", "Radius", CM_PER_METRE, "half the lane's width"),
    ("SoulfireSpeedCmPerSecond", "Speed", 1.0, "how fast the bolt travels"),
])
def test_the_bolt_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = float(soulfire().params[param]) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and Soulfire's {param} is "
        f"{soulfire().params[param]}, which is {designed} ({what}).")


def test_the_bolt_is_the_basic_attack_and_has_no_cooldown():
    """A zero cooldown is what makes an EnemyAbilities entry a basic attack:
    the creature's own attack interval is then the only thing spacing it out."""
    assert soulfire().slot == "Basic", (
        f"Soulfire's slot is {soulfire().slot!r} in the model, and the C++ "
        f"treats it as the basic attack.")
    assert soulfire().cooldown == 0.0, (
        f"Soulfire's designed cooldown is {soulfire().cooldown}, not zero, so "
        f"it is no longer a basic attack.")
    assert constant("SoulfireCooldownSeconds") == 0.0, (
        "SoulfireCooldownSeconds is not zero, so the bolt would be held back "
        "by a cooldown as well as by the attack interval and the creature "
        "would sometimes do nothing at all.")


def test_the_bolt_is_telegraphed_and_uses_its_whole_allowance():
    """The wind-up is 0.4 + Radius / 3.5 and the radius is the largest a 2.6
    second interval allows, so the wind-up is exactly half the interval. Only
    the Corrupted Sentinel's Siege Bolt does the same."""
    from cataclysm_sim.enemy_abilities import (is_telegraphed,
                                               largest_telegraphed_radius,
                                               wind_up_seconds)

    assert is_telegraphed(soulfire(), succubus()), (
        "the model no longer telegraphs Soulfire, and the C++ gives it a "
        "wind-up and a marker.")

    radius = float(soulfire().params["Radius"])
    allowed = largest_telegraphed_radius(succubus().attack_interval)
    assert radius == pytest.approx(allowed), (
        f"Soulfire's radius is {radius} m and the largest its 2.6 second "
        f"interval allows is {allowed:.4f} m. The header claims it uses the "
        f"whole allowance.")

    designed = wind_up_seconds(radius)
    written = constant("SoulfireWindUpSeconds")
    assert written == pytest.approx(designed, abs=0.002), (
        f"SoulfireWindUpSeconds is {written} and the rule 0.4 + Radius / 3.5 "
        f"gives {designed:.4f}.")

    assert written == pytest.approx(succubus().attack_interval / 2.0,
                                    abs=0.002), (
        f"the wind-up is {written} and half the attack interval is "
        f"{succubus().attack_interval / 2.0}. The header says these are the "
        f"same number and the design document says so too.")


def test_the_bolt_is_the_slowest_projectile_in_the_game():
    """1200 cm/s is Magma Quake's, the slowest player projectile. A slow bolt
    is a readable one, which is the point of a 3.15 metre telegraph."""
    written = constant("SoulfireSpeedCmPerSecond")

    # ZERO IS NOT A SLOW PROJECTILE, IT IS THE ABSENCE OF ONE. A lobbed skill
    # states a flight time and no speed, and a beam states neither, so both
    # write Speed=0 and both would win a "slowest" comparison while travelling
    # no distance at that speed at all. Found by this test failing on 0.0.
    speeds = []
    weapon_skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if weapon_skills.is_file():
        # `(?<![A-Za-z])` OR THIS MATCHES INSIDE ANOTHER PARAMETER'S NAME.
        # `ScalesWithAttackSpeed=1` contains the text "Speed=1", so on
        # 2026-09-01 the slowest player projectile in the game became one
        # centimetre per second and this test failed on a skill that fires no
        # projectile at all. Any future parameter ending in Speed would have
        # done the same.
        for found in re.finditer(r"(?<![A-Za-z])Speed\s*=\s*(\d+(?:\.\d+)?)",
                                 source(weapon_skills)):
            speed = float(found.group(1))
            if speed > 0.0:
                speeds.append(speed)

    if not speeds:
        pytest.skip("game/Data/WeaponSkills.csv states no projectile speeds, "
                    "so there is nothing to compare against. A skip means this "
                    "did not run, not that it passed.")

    assert written <= min(speeds), (
        f"SoulfireSpeedCmPerSecond is {written} and the slowest player "
        f"projectile is {min(speeds)}. The header claims the Succubus's is the "
        f"slowest anything in the game uses.")


def test_the_bolt_damage_is_the_basic_slots_percentage():
    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    match = re.search(r"^Basic,Basic,(\d+(?:\.\d+)?),", source(slots),
                      re.MULTILINE)
    assert match is not None, (
        "game/Data/SkillSlots.csv has no Basic row, so nothing says what a "
        "basic attack is worth.")

    assert constant("SoulfireDamagePercent") == pytest.approx(
        float(match.group(1))), (
        f"SoulfireDamagePercent is {constant('SoulfireDamagePercent')} and the "
        f"Basic row of game/Data/SkillSlots.csv is {match.group(1)}.")


# --------------------------------------------------------------------------
# Wither the Living, the curse
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name,param,scale,what", [
    ("WitherRangeCm", "Range", CM_PER_METRE, "how far the curse reaches"),
    ("WitherDurationSeconds", "Duration", 1.0, "how long it lasts"),
])
def test_the_curse_matches_the_designed_ability(cpp_name, param, scale, what):
    designed = float(wither().params[param]) * scale
    written = constant(cpp_name)

    assert written == pytest.approx(designed), (
        f"{cpp_name} is {written} and Wither the Living's {param} is "
        f"{wither().params[param]}, which is {designed} ({what}).")


def test_the_curse_cooldown_matches_the_designed_ability():
    assert constant("WitherCooldownSeconds") == pytest.approx(
        wither().cooldown), (
        f"WitherCooldownSeconds is {constant('WitherCooldownSeconds')} and the "
        f"designed cooldown is {wither().cooldown}.")


def test_the_curse_reaches_one_target():
    designed = int(wither().params["MaxTargets"])
    written = whole_number_constant("WitherMaxTargets")

    assert written == designed, (
        f"WitherMaxTargets is {written} and the design's MaxTargets is "
        f"{designed}.")


def test_the_curse_names_an_effect_the_table_really_has():
    """Withered Touch is chosen from game/Data/StatusEffects.csv rather than
    invented, and the tag it becomes has to exist too or the curse grants
    nothing."""
    named = text_constant("WitherEffectName")
    designed = str(wither().params["Effect"])

    assert named == designed, (
        f"the C++ applies {named!r} and the design says {designed!r}.")

    assert re.search(rf"^[^,]*,Debuff,{re.escape(named)},",
                     source(STATUS_EFFECTS), re.MULTILINE), (
        f"game/Data/StatusEffects.csv has no Debuff row called {named!r}, so "
        f"the curse names an effect nobody designed.")

    # UNDER Status.Debuff AND NOT Status.Buff, which is a stronger check than it
    # was before issue #1145 split the branch. The tag now carries the sheet the
    # effect came from, so a curse that had drifted onto the Buffs sheet would
    # fail here rather than quietly stop counting as a debuff.
    segment = "".join(c for c in named if c.isalnum())
    assert f'Tag="Status.Debuff.{segment}"' in source(TAGS_INI), (
        f"game/Config/Tags/CataclysmTags.ini has no Status.Debuff.{segment} "
        f"tag, so UCataclysmSkillShapes::StatusTagFor returns an invalid tag "
        f"and ApplyTagForDuration grants nothing at all.")


def test_the_curse_draws_no_marker_and_that_is_designed():
    """A Debuff is not one of the four shapes the telegraph table draws, so a
    wind-up would be the creature standing still for no visible reason."""
    from cataclysm_sim.enemy_abilities import TELEGRAPHED_SHAPES, is_telegraphed

    assert wither().shape == "Debuff", (
        f"Wither the Living's shape is {wither().shape!r} in the model and the "
        f"C++ gives it ECataclysmSkillShape::Debuff.")
    assert "Debuff" not in TELEGRAPHED_SHAPES, (
        "the model now telegraphs a Debuff, so this creature's curse should "
        "draw a marker and have a wind-up, and it has neither.")
    assert not is_telegraphed(wither(), succubus())

    assert constant("WitherWindUpSeconds") == 0.0, (
        "WitherWindUpSeconds is not zero. A Debuff draws no marker, so a "
        "wind-up would hold the creature still with nothing on screen "
        "explaining why.")

    body = source(SUCCUBUS_SOURCE)
    assert "Wither.MarkerRadiusCm = 0.0f;" in body, (
        "Wither the Living now asks for a marker radius. Nothing draws a "
        "marker for a Debuff, so the number would be a claim the screen never "
        "keeps.")


def test_the_curse_cannot_be_recast_before_it_expires():
    """Twice the duration, so the player has as long without the curse as with
    it. The design says so and the header asserts it."""
    assert wither().cooldown >= 2.0 * float(wither().params["Duration"]), (
        f"the designed cooldown is {wither().cooldown} and the duration is "
        f"{wither().params['Duration']}, so the curse can be recast before it "
        f"expires and the player never has a moment without it.")


# --------------------------------------------------------------------------
# Dominion, the aura
# --------------------------------------------------------------------------

def test_the_aura_matches_the_designed_radius():
    designed = float(dominion().params["Radius"]) * CM_PER_METRE
    written = constant("DominionRadiusCm")

    assert written == pytest.approx(designed), (
        f"DominionRadiusCm is {written} and Dominion's Radius is "
        f"{dominion().params['Radius']} m, which is {designed} cm.")


def test_the_aura_radius_is_the_creatures_own_attack_range():
    """Derived rather than chosen. The Succubus stands 8 metres from the
    player, so an ally fighting that player is at most 8 metres from the
    Succubus."""
    assert constant("DominionRadiusCm") == pytest.approx(
        constant("SoulfireRangeCm")), (
        f"DominionRadiusCm is {constant('DominionRadiusCm')} and "
        f"SoulfireRangeCm is {constant('SoulfireRangeCm')}. The design "
        f"document derives one from the other; a smaller field would buff "
        f"nothing at the moment it matters.")


def test_the_aura_is_held_on_rather_than_cast():
    """It has no cooldown and no cycle, which is what makes it exempt from the
    telegraph cap and what keeps it out of EnemyAbilities."""
    assert dominion().is_held_on, (
        "Dominion is no longer held on in the model, so it fires at a moment "
        "like anything else and would need a cooldown, a marker and a place in "
        "EnemyAbilities.")
    assert dominion().cycle_seconds(succubus()) == 0.0
    assert dominion().cooldown == 0.0


def test_the_aura_is_over_the_telegraph_cap_and_is_the_one_exemption():
    """8 metres against a 6.50 metre cap. `fits_its_cycle` exempts an ability
    held on for as long as the creature lives, because the cap asks whether the
    player can be clear by the time an attack lands and this one never
    lands."""
    from cataclysm_sim.enemy_abilities import (fits_its_cycle,
                                               telegraph_cap_metres)

    radius = float(dominion().params["Radius"])
    cap = telegraph_cap_metres(succubus())

    assert radius > cap, (
        f"Dominion's radius is {radius} m and the cap is {cap:.2f} m. This "
        f"test and the header both claim it is over the cap; if the numbers "
        f"changed, that claim is now wrong in two places.")
    assert fits_its_cycle(dominion(), succubus()), (
        "the model now refuses Dominion, so the exemption for an ability held "
        "on while the creature lives has gone and the design needs revisiting "
        "rather than the C++.")


def test_the_aura_is_not_an_entry_in_the_ability_array():
    """Every entry in EnemyAbilities is chosen by range, gated by the attack
    interval and USED at a moment. This is none of those. Putting it in the
    array would make the creature stop shooting in order to hold up an aura it
    is already holding up."""
    body = source(SUCCUBUS_SOURCE)

    match = re.search(r"return\s*\{([^}]*)\};", body)
    assert match is not None, (
        "CataclysmSuccubusCharacter.cpp no longer returns a brace-enclosed "
        "list from EnemyAbilities, so this guard cannot read the array.")

    returned = match.group(1)
    assert "Dominion" not in returned, (
        f"EnemyAbilities returns {returned.strip()!r}, which mentions "
        f"Dominion. An aura held on for as long as the creature lives must not "
        f"compete with Soulfire for the creature's attack interval.")


def test_the_aura_names_an_effect_the_table_really_has():
    named = text_constant("DominionEffectName")
    designed = str(dominion().params["Effect"])

    assert named == designed, (
        f"the C++ grants {named!r} and the design says {designed!r}.")

    assert re.search(rf"^[^,]*,Buff,{re.escape(named)},",
                     source(STATUS_EFFECTS), re.MULTILINE), (
        f"game/Data/StatusEffects.csv has no Buff row called {named!r}, so the "
        f"aura names an effect nobody designed.")

    # UNDER Status.Buff, for the reason the curse's own check gives. This is the
    # buff half of that pair: the aura makes an ally better, and after issue
    # #1145 the tag itself has to say so or the seven Masochist nodes paid per
    # debuff carried would be paid for carrying it.
    segment = "".join(c for c in named if c.isalnum())
    assert f'Tag="Status.Buff.{segment}"' in source(TAGS_INI), (
        f"game/Config/Tags/CataclysmTags.ini has no Status.Buff.{segment} tag, "
        f"so the aura grants nothing at all.")


def test_the_aura_is_renewed_faster_than_it_expires():
    """Otherwise an ally standing still inside the field would lose the buff
    and get it back over and over."""
    grant = constant("DominionGrantSeconds")
    refresh = constant("DominionRefreshSeconds")

    assert grant > refresh, (
        f"DominionGrantSeconds is {grant} and DominionRefreshSeconds is "
        f"{refresh}, so the effect expires before the next sweep renews it.")


def test_the_aura_ends_when_the_creature_dies():
    """The design's whole claim is that killing it first is the correct play.
    A buff that outlived the caster would make that false."""
    body = source(SUCCUBUS_SOURCE)

    match = re.search(r"void ACataclysmSuccubusCharacter::HandleDeath\(\)"
                      r"\s*\{(.*?)\n\}", body, re.DOTALL)
    assert match is not None, (
        "CataclysmSuccubusCharacter.cpp no longer overrides HandleDeath, so "
        "nothing takes the aura off when the creature dies.")

    assert "EndDominion();" in match.group(1), (
        "HandleDeath does not call EndDominion, so allies keep the buff after "
        "the Succubus is dead. The design document's claim is that killing it "
        "ends the aura at once.")


def test_the_aura_looks_for_allies_and_not_for_enemies():
    """It is the only thing in the game that helps a creature rather than
    hurting one, and searching the wrong side would buff the player."""
    body = source(SUCCUBUS_SOURCE)

    assert "FindAlliesInSphere" in body, (
        "the Succubus no longer calls UCataclysmTargeting::FindAlliesInSphere, "
        "so nothing says whose side its aura is on.")
    assert "FindEnemiesInSphere" not in body, (
        "the Succubus calls FindEnemiesInSphere. Dominion buffs ALLIES; "
        "searching for enemies would grant Commander to the player.")


# --------------------------------------------------------------------------
# The shape the class takes in the engine
# --------------------------------------------------------------------------

def test_the_reach_matches_the_ability_table():
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    designed = ATTACK_REACH["Succubus"] * CM_PER_METRE
    written = constant("SoulfireRangeCm")

    assert written == pytest.approx(designed), (
        f"the creature's reach is SoulfireRangeCm at {written} cm and "
        f"ATTACK_REACH['Succubus'] is {ATTACK_REACH['Succubus']} m, which is "
        f"{designed} cm.")


def test_the_class_does_not_deal_a_free_melee_hit():
    """`ACataclysmEnemyCharacter::AttackTarget` applies direct damage at
    MeleeReachCm, and this creature's reach is its SHOT's range. Left to the
    base it would hit for a full weapon's worth at eight metres, through walls,
    every 2.6 seconds, on top of the bolt."""
    body = source(SUCCUBUS_SOURCE)

    match = re.search(
        r"void ACataclysmSuccubusCharacter::AttackTarget\([^)]*\)\s*\{(.*?)\n\}",
        body, re.DOTALL)
    assert match is not None, (
        "CataclysmSuccubusCharacter.cpp no longer overrides AttackTarget, so "
        "the base class's melee hit lands at eight metres every 2.6 seconds.")

    without_comments = re.sub(r"//[^\n]*", "", match.group(1))
    without_comments = re.sub(r"/\*.*?\*/", "", without_comments, flags=re.DOTALL)

    assert without_comments.strip() == "", (
        f"AttackTarget has a body: {without_comments.strip()!r}. It must do "
        f"nothing at all -- Soulfire is the basic attack and it is an entry in "
        f"EnemyAbilities.")


def test_the_curse_is_listed_before_the_bolt():
    """`ChooseAbility` takes the first entry whose range and cooldown fit.
    Soulfire has a cooldown of zero and the same range, so a Soulfire at the
    front would be the only thing the creature ever did. Issue #491 is that
    defect on the Abyssal Warden."""
    body = source(SUCCUBUS_SOURCE)

    match = re.search(r"return\s*\{([^}]*)\};", body)
    assert match is not None, (
        "CataclysmSuccubusCharacter.cpp no longer returns a brace-enclosed "
        "list from EnemyAbilities.")

    returned = [name.strip() for name in match.group(1).split(",")]
    assert returned == ["Wither", "Soulfire"], (
        f"EnemyAbilities returns {returned}, and the curse has to come first. "
        f"Soulfire's cooldown is zero, so anything behind it is unreachable.")

    header = source(SUCCUBUS_HEADER)
    match = re.search(r"enum\s*:\s*int32\s*\{([^}]*)\}", header)
    assert match is not None, (
        "CataclysmSuccubusCharacter.h no longer names the ability indices.")
    assert "WitherTheLivingAbility = 0" in match.group(1)
    assert "SoulfireAbility = 1" in match.group(1)


def test_both_shapes_are_ones_the_enum_really_has():
    """Debuff and Aura were in ECataclysmSkillShape before this creature
    existed, and no enemy had used either. A shape the enum does not hold
    silently reads as None."""
    shapes = REPO_ROOT / ("game/Source/Cataclysm/AbilitySystem/"
                          "CataclysmSkillShape.h")
    listed = source(shapes)

    for name in ("Debuff", "Projectile"):
        assert re.search(rf"^\t{name}\s", listed, re.MULTILINE), (
            f"ECataclysmSkillShape has no {name} entry, so an ability asking "
            f"for it reads as None and does nothing at all. That is issue "
            f"#621, which happened to Deployable.")


# --------------------------------------------------------------------------
# Its animation
# --------------------------------------------------------------------------

def test_the_attack_clip_fits_inside_the_wind_up():
    """The clip IS the wind-up: the bolt leaves as the telegraph ends. A clip
    longer than the wind-up even at the play rate ceiling would still be
    playing when the bolt left."""
    clip = constant("AttackAnimationSeconds")
    wind_up = constant("SoulfireWindUpSeconds")
    ceiling = constant("MaximumPlayRate")

    assert clip <= wind_up * ceiling, (
        f"the attack clip is {clip} s and the wind-up is {wind_up} s. Even at "
        f"the play rate ceiling of {ceiling} it needs {clip / ceiling:.4f} s.")


def test_the_walk_play_rate_is_inside_the_clamp():
    """350 / 321.0 is 1.090, the gentlest walk in the project. A rate outside
    the clamp is one the creature cannot actually reach, so its feet slide."""
    designed = constant("DesignedWalkSpeedCmPerSecond")
    authored = constant("AuthoredJogSpeedCmPerSecond")
    floor = constant("MinimumPlayRate")
    ceiling = constant("MaximumPlayRate")

    rate = designed / authored
    assert floor <= rate <= ceiling, (
        f"the walk needs a play rate of {rate:.4f} and the clamp is {floor} to "
        f"{ceiling}, so it would be clamped and the feet would slide.")


def test_the_asset_notes_record_what_was_measured():
    """Every clip the class names has to have a measured length on record, or
    the numbers in the header came from nowhere."""
    notes = source(ASSET_NOTES)

    for name in ("Idle_Relaxed", "Jog_Fwd", "Primary_Attack_Normal", "Cast",
                 "Death"):
        assert f"`{name}`" in notes, (
            f"game/docs/enemy-source-assets.md does not mention {name}, which "
            f"CataclysmSuccubusCharacter.cpp plays. Run "
            f"tools/probe_succubus_animation.py and record what it says.")


def test_it_has_one_way_to_fall_over():
    """The Countess pack ships exactly one death clip, which is the fewest in
    the project along with the Brute's."""
    assert whole_number_constant("DeathAnimationCount") == 1, (
        "DeathAnimationCount is no longer 1. If the pack really does ship more "
        "than one death clip, measure them and say so in "
        "game/docs/enemy-source-assets.md; if not, this is a typo that changes "
        "which clip PlayDeathAnimation draws.")


# --------------------------------------------------------------------------
# The sandbox
# --------------------------------------------------------------------------

def sandbox_stat_block():
    from cataclysm_sim.enemy_stats import stats_on_floor

    return stats_on_floor("Common", 1, "Cataclysm", total_floors=50, floor=50,
                          kind="Succubus")


def test_the_sandbox_actually_spawns_one():
    assert whole_number_property("SuccubusCount") > 0, (
        "SuccubusCount is zero, so no Succubus is placed in the sandbox and "
        "the creature cannot be looked at.")

    assert "SpawnSuccubi();" in source(GAME_MODE_SOURCE), (
        "ACataclysmGameMode::StartPlay does not call SpawnSuccubi, so the "
        "spawner exists and nothing runs it.")


def test_the_sandbox_health_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().health
    written = property_default("SuccubusHealth")

    assert written == pytest.approx(round(designed)), (
        f"SuccubusHealth is {written} and the design model gives a Common "
        f"Succubus at tier 1, on the last floor of a 50-floor Cataclysm "
        f"dungeon, {designed:.2f}.")


def test_the_sandbox_armour_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().armor
    written = property_default("SuccubusArmour")

    assert written == pytest.approx(round(designed)), (
        f"SuccubusArmour is {written} and the design model gives "
        f"{designed:.2f} at the same encounter as its health.")


def test_the_sandbox_damage_is_the_dummys_times_the_designed_share():
    dummy = property_default("TrainingDummyAttackDamage")
    expected = dummy * succubus().damage_share
    written = property_default("SuccubusAttackDamage")

    assert written == pytest.approx(expected), (
        f"SuccubusAttackDamage is {written} and the training dummy's {dummy} "
        f"times the designed damage share of {succubus().damage_share} is "
        f"{expected}.")


def test_it_is_spawned_beyond_its_own_notice_radius():
    """So it is standing about rather than already walking at the player when
    the level opens. Unlike the Corrupted Sentinel it can close the gap."""
    distance = property_default("SuccubusDistanceCm")
    notices = constant("SuccubusNoticeRadiusCm")

    assert distance > notices, (
        f"the Succubus spawns {distance} cm out and notices at {notices} cm, "
        f"so it starts walking at the player the instant the level opens.")


def test_it_is_spawned_on_a_bearing_of_its_own():
    """Five creatures already occupy four directions, and
    `CorruptedSentinelBearingDegrees` says in as many words that a fifth would
    need somewhere else to go. This is that somewhere."""
    import itertools

    placed = {
        "the Brute": (property_default("BruteDistanceCm"), 0.0),
        "the Abyssal Warden": (property_default("AbyssalWardenDistanceCm"), 0.0),
    }
    for name, distance_key, bearing_key in (
            ("the Hellhound", "HellhoundDistanceCm", "HellhoundBearingDegrees"),
            ("the Imp pack", "ImpDistanceCm", "ImpBearingDegrees"),
            ("the Corrupted Sentinel", "CorruptedSentinelDistanceCm",
             "CorruptedSentinelBearingDegrees"),
            ("the Succubus", "SuccubusDistanceCm", "SuccubusBearingDegrees")):
        bearing = math.radians(property_default(bearing_key))
        distance = property_default(distance_key)
        placed[name] = (distance * math.cos(bearing),
                        distance * math.sin(bearing))

    # NOT INSIDE ANOTHER CREATURE'S NOTICE RADIUS. Two creatures that start
    # inside one another's are fighting each other's neighbours rather than
    # waiting to be walked up to.
    needed = constant("SuccubusNoticeRadiusCm")
    succubus_at = placed["the Succubus"]

    for name, where in placed.items():
        if name == "the Succubus":
            continue
        gap = math.dist(succubus_at, where)
        assert gap > needed, (
            f"the Succubus stands {gap:.0f} cm from {name}, inside the "
            f"{needed:.0f} cm at which it notices anybody.")

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
    bearing = math.radians(property_default("SuccubusBearingDegrees"))
    distance = property_default("SuccubusDistanceCm")
    radius = constant("SuccubusCapsuleRadius")

    # ALONG EACH AXIS RATHER THAN ALONG THE BEARING, because the floor is a
    # SQUARE and this is the first creature placed off a cardinal direction. A
    # radial test would be wrong for a diagonal in the forgiving direction, and
    # a guard that is wrong in the forgiving direction is one that does not
    # guard.
    furthest_x = abs(distance * math.cos(bearing)) + radius
    furthest_y = abs(distance * math.sin(bearing)) + radius

    assert max(furthest_x, furthest_y) < reach, (
        f"the Succubus reaches {furthest_x:.0f} cm along X and "
        f"{furthest_y:.0f} cm along Y, and the sandbox floor only reaches "
        f"{reach} cm on each axis.")
