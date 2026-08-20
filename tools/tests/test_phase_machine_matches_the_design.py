"""The engine's phase machine must honour the rules the design states.

WHAT A PHASE IS. It selects which of a creature's abilities are in the rotation
and changes no number. That is the finding the whole boss design leans on, from
the research recorded with issue #354 in `docs/DECISIONS.md`: across ten shipped
bosses in Path of Exile and Last Epoch, not one gains damage, armour, attack
speed or critical strike at a transition.

WHICH IS AUTHORITATIVE. The Python. `PHASE_TRANSITIONS` and the `phase` field on
`Ability` in `sim/cataclysm_sim/enemy_abilities.py` are where phases are
designed.

WHY THESE READ SOURCE TEXT. Continuous integration never builds the C++, so
`CataclysmEnemyPhaseTests.cpp` cannot run on a pull request. Those tests check
the arithmetic by running it; these check that the shape and the rules have not
drifted, which is what a pull request can see.

NOTHING USES THIS YET. The Gatekeeper is the only creature the design gives
phases to and it is not built. Issue #759 builds it.
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

SOURCE_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm"
BASE_HEADER = SOURCE_DIR / "Character" / "CataclysmCharacterBase.h"
ENEMY_HEADER = SOURCE_DIR / "Character" / "CataclysmEnemyCharacter.h"
ENEMY_SOURCE = SOURCE_DIR / "Character" / "CataclysmEnemyCharacter.cpp"
CONTROLLER_SOURCE = SOURCE_DIR / "Character" / "CataclysmEnemyController.cpp"
VITALS_SOURCE = SOURCE_DIR / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def without_comments(text: str) -> str:
    """C++ with every comment removed.

    A SOURCE-READING TEST MUST STRIP COMMENTS FIRST. Every comparison below runs
    on the output of this, and `test_the_comment_stripping_really_strips` is the
    negative control that says it works.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def test_the_comment_stripping_really_strips():
    """The control for every check in this file. A guard that reads prose as
    code passes whatever the code says."""
    assert without_comments("// int32 Phase = 1;\n").strip() == ""
    assert without_comments("/* int32 Phase = 1; */").strip() == ""
    assert "int32 Phase" in without_comments("int32 Phase = 1;  // a comment")


# --------------------------------------------------------------------------
# The ability carries a phase
# --------------------------------------------------------------------------

def test_an_ability_carries_the_phase_it_becomes_available_in():
    text = without_comments(source(BASE_HEADER))

    match = re.search(r"\bint32\s+Phase\s*=\s*(\d+)\s*;", text)
    assert match is not None, (
        "FCataclysmEnemyAbility has no `int32 Phase` field, so nothing says "
        "which phase an ability belongs to and every ability is available from "
        "the start.")

    assert int(match.group(1)) == 1, (
        f"an ability's phase defaults to {match.group(1)}. It must default to "
        f"1: six of the seven creatures have no phases at all and would "
        f"otherwise be unable to use any of their abilities.")


def test_every_designed_ability_names_a_phase_of_at_least_one():
    """The model's own `_check_every_phase_is_reachable_and_starts_at_one`
    holds this too. Repeated here because the C++ default has to agree with it:
    a design phase of 0 would be unreachable in an engine that starts at 1."""
    from cataclysm_sim.enemy_abilities import ABILITIES

    for enemy, entries in ABILITIES.items():
        for ability in entries:
            assert ability.phase >= 1, (
                f"{enemy}'s {ability.name} is designed in phase "
                f"{ability.phase}, and the engine's first phase is 1.")


def test_only_the_gatekeeper_has_phases_and_it_has_three():
    """One creature, and the count is what decides how many `PhaseHealthFractions`
    entries its class will carry: N transitions make N+1 phases."""
    from cataclysm_sim.enemy_abilities import ABILITIES, PHASE_TRANSITIONS

    assert set(PHASE_TRANSITIONS) == {"Gatekeeper"}, (
        f"the creatures with phases are now {sorted(PHASE_TRANSITIONS)}. If a "
        f"second one is designed, that is fine and the comments in "
        f"CataclysmEnemyCharacter.h saying only the boss has them need "
        f"changing.")

    assert PHASE_TRANSITIONS["Gatekeeper"] == (0.60, 0.30), (
        f"the Gatekeeper's phase thresholds are now "
        f"{PHASE_TRANSITIONS['Gatekeeper']}. They were 0.60 and 0.30, and "
        f"CataclysmEnemyPhaseTests.cpp holds the same two figures.")

    highest = max(a.phase for a in ABILITIES["Gatekeeper"])
    assert highest == len(PHASE_TRANSITIONS["Gatekeeper"]) + 1, (
        f"the Gatekeeper's highest designed phase is {highest} and it has "
        f"{len(PHASE_TRANSITIONS['Gatekeeper'])} transitions, which make "
        f"{len(PHASE_TRANSITIONS['Gatekeeper']) + 1} phases. A phase nothing "
        f"transitions into can never begin.")


# --------------------------------------------------------------------------
# The rules
# --------------------------------------------------------------------------

def test_an_ability_from_an_earlier_phase_stays_available():
    """"Phases add, they do not take away." The chooser must ask whether an
    ability's phase is AT MOST the creature's, not equal to it. Equal would make
    a boss forget its basic attack the moment it learned anything else."""
    text = without_comments(source(CONTROLLER_SOURCE))

    match = re.search(r"if\s*\(\s*Ability\.Phase\s*([<>=!]+)\s*Phase\s*\)\s*"
                      r"\{\s*continue;", text)
    assert match is not None, (
        "ACataclysmEnemyController::ChooseAbility does not skip an ability by "
        "its phase, so a boss would use its whole kit from the first second of "
        "the fight.")

    assert match.group(1) == ">", (
        f"ChooseAbility skips an ability when its phase is {match.group(1)} the "
        f"creature's. It must skip only when the phase is GREATER -- an "
        f"ability from an earlier phase is still available, because phases add "
        f"and never take away.")


def test_the_phase_only_ever_goes_forward():
    """A creature healed back above a threshold keeps the phase it reached.
    Nothing heals a creature today, so only a test can catch this."""
    text = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"bool ACataclysmEnemyCharacter::RefreshPhase\(\)"
                      r"\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match is not None, (
        "CataclysmEnemyCharacter.cpp no longer defines RefreshPhase.")

    body = match.group(1)
    assert re.search(r"if\s*\(\s*Wanted\s*<=\s*PhaseReached\s*\)\s*\{\s*"
                     r"return\s+false;", body), (
        "RefreshPhase does not refuse to move backwards. A creature that "
        "dropped a phase would un-learn an ability mid-fight, which is what "
        "\"phases add, they do not take away\" forbids.")


def test_a_phase_changes_no_number():
    """The finding the whole design leans on: across ten shipped bosses, not one
    gains damage, armour, attack speed or crit at a transition. So the phase
    machinery must not write to any attribute."""
    text = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"bool ACataclysmEnemyCharacter::RefreshPhase\(\)"
                      r"\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match is not None, (
        "CataclysmEnemyCharacter.cpp no longer defines RefreshPhase.")

    body = match.group(1)
    for forbidden in ("SetNumericAttribute", "ApplyModToAttribute",
                      "ApplyStartingAttributes", "MaxWalkSpeed",
                      "AttackIntervalSeconds", "StartingAttackDamage",
                      "StartingArmour"):
        assert forbidden not in body, (
            f"RefreshPhase mentions {forbidden}, so a phase changes a number. "
            f"The design's research found no shipped boss that does: a phase "
            f"selects which abilities are in the rotation and nothing else. "
            f"docs/DECISIONS.md, the 2026-08-09 entry.")


def test_the_phase_is_noticed_on_the_hit_rather_than_on_a_frame():
    """A phase decides which ability the brain may choose, and the brain thinks
    on its own schedule, so a phase arriving a frame late could arrive after the
    choice it should have changed."""
    text = without_comments(source(VITALS_SOURCE))

    # TWO CALL SITES, ONE PER PLACE HEALTH IS WRITTEN. One is the damage
    # path and the other is a direct write to the Health attribute; a phase
    # that only fired on one of them would begin on some hits and not others.
    # The definition line does not match this pattern, so the count is 2.
    calls = text.count("NotifyHealthChanged();")
    assert calls == 2, (
        f"UCataclysmVitalAttributeSet calls NotifyHealthChanged {calls} "
        f"times and there are two places health is written. Both need it, "
        f"and a third would mean a phase change being noticed twice for one "
        f"hit -- harmless, because RefreshPhase only moves forward, but a "
        f"sign that a third write appeared without being read about.")

    match = re.search(r"void UCataclysmVitalAttributeSet::NotifyHealthChanged"
                      r"\(\)\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match is not None, (
        "CataclysmVitalAttributeSet.cpp no longer defines NotifyHealthChanged.")

    assert "HealthChanged();" in match.group(1), (
        "NotifyHealthChanged does not tell the character its health moved, so "
        "nothing drives a phase transition at all.")

    enemy = without_comments(source(ENEMY_SOURCE))
    match = re.search(r"void ACataclysmEnemyCharacter::HealthChanged\(\)"
                      r"\s*\{(.*?)\n\}", enemy, re.DOTALL)
    assert match is not None, (
        "ACataclysmEnemyCharacter does not override HealthChanged, so a "
        "creature never notices its own health moving.")

    assert "RefreshPhase();" in match.group(1), (
        "HealthChanged does not call RefreshPhase.")


def test_a_creature_with_no_thresholds_is_left_alone():
    """Six of the seven have no phases, and every ability defaults to phase 1.
    A creature that fell out of phase 1 would lose its whole kit."""
    text = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"bool ACataclysmEnemyCharacter::RefreshPhase\(\)"
                      r"\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match is not None

    body = match.group(1)
    assert re.search(r"if\s*\(\s*PhaseHealthFractions\.IsEmpty\(\)\s*\)\s*\{\s*"
                     r"return\s+false;", body), (
        "RefreshPhase does not return early for a creature with no thresholds.")

    header = without_comments(source(ENEMY_HEADER))
    match = re.search(r"\bint32\s+PhaseReached\s*=\s*(\d+)\s*;", header)
    assert match is not None, (
        "ACataclysmEnemyCharacter has no PhaseReached field.")
    assert int(match.group(1)) == 1, (
        f"PhaseReached starts at {match.group(1)}. It must start at 1, or a "
        f"creature would have no abilities available until it took damage.")


def test_a_creature_cannot_reach_its_last_phase_before_its_health_is_set():
    """A spawner sets health after the actor exists. Between construction and
    that call the maximum is zero, and zero over zero is not phase 1."""
    text = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"bool ACataclysmEnemyCharacter::RefreshPhase\(\)"
                      r"\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match is not None

    body = match.group(1)
    assert re.search(r"if\s*\(\s*MaxHealth\s*<=\s*0\.0f\s*\)\s*\{\s*"
                     r"return\s+false;", body), (
        "RefreshPhase does not refuse a creature whose maximum health is not "
        "set yet, so it would divide by zero and land in its last phase on the "
        "frame it spawned.")


def test_any_character_can_be_asked_its_phase():
    """The controller drives three classes that have nothing else in common, so
    the hook is on the shared base and answers 1 by default."""
    text = without_comments(source(BASE_HEADER))

    match = re.search(r"virtual\s+int32\s+CurrentPhase\(\)\s*const\s*"
                      r"\{\s*return\s+(\d+);\s*\}", text)
    assert match is not None, (
        "ACataclysmCharacterBase has no CurrentPhase hook, so "
        "ChooseAbility would need a cast to ask a creature what phase it is "
        "in.")

    assert int(match.group(1)) == 1, (
        f"the base answers phase {match.group(1)}. It must answer 1, because "
        f"every ability defaults to phase 1 and a character that is not an "
        f"enemy would otherwise be unable to use any.")

    assert re.search(r"virtual\s+void\s+HealthChanged\(\)\s*\{\s*\}", text), (
        "ACataclysmCharacterBase has no inert HealthChanged hook, so the "
        "attribute set has nothing to call on a character that does not care.")
