"""The next-use and placed-stack action names the generator accepts are the
ones the engine compares a row's Action against.

Issue #1833, phase 2. `UCataclysmItemModifiers::AccumulateEnchantmentsInto`
recognises these rows by comparing `Effect->Action` with constants on
`UCataclysmAbilitySystemComponent`. A name spelled differently on the two sides
validates in the generator, is written to the table, and is read in the game as
a pool it has no case for, so the row does nothing and nothing says so.
`test_pool_action_names_match_the_engine.py` guards the pool actions; these had
no guard until this file.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
          / "CataclysmAbilitySystemComponent.cpp")

#: Which constant holds which name, as the engine defines them.
CONSTANTS = {
    "NextSkillDamageAction": "next_skill_damage",
    "NextAttackDamageAction": "next_attack_damage",
    "NextSkillEffectivenessAction": "next_skill_effectiveness",
    "EnemyArmorRemovedAction": "enemy_armor_removed",
    "AttackerDamageRemovedAction": "attacker_damage_removed",
    # AND THE THREE EVERY-Nth NAMES, since issue #1833's every Nth.
    "NthHitTakenDamageAction": "nth_hit_taken_damage",
    "NthSpellManaCostAction": "nth_spell_mana_cost",
    "NthAttackNoDamageAction": "nth_attack_no_damage",
    # AND THE SIX COOLDOWN RESET NAMES, since issue #1833's cooldown reset.
    "CooldownResetAllAction": "cooldown_reset_all",
    "CooldownResetOthersAction": "cooldown_reset_others",
    "CooldownResetHeavyAction": "cooldown_reset_heavy",
    "CooldownResetSpecialAction": "cooldown_reset_special",
    "CooldownResetMovementAction": "cooldown_reset_movement",
    "CooldownResetEventSkillAction": "cooldown_reset_event_skill",
    # AND THE COOLDOWN REDUCTION NAMES, since the cooldown reduction action.
    "CooldownReduceAllAction": "cooldown_reduce_all",
    "CooldownReduceHeavyAction": "cooldown_reduce_heavy",
    "NextSpellCooldownReducedAction": "next_spell_cooldown_reduced",
}


def engine_names() -> dict[str, str]:
    text = SOURCE.read_text(encoding="utf-8")
    found = {}
    for constant in CONSTANTS:
        match = re.search(
            rf"UCataclysmAbilitySystemComponent::{constant}\s*=\s*TEXT\(\"([^\"]+)\"\)",
            text)
        if match:
            found[constant] = match.group(1)
    return found


def test_every_constant_is_found_in_the_engine() -> None:
    """A reader that finds nothing would pass the comparisons below."""
    assert set(engine_names()) == set(CONSTANTS), (
        f"found {sorted(engine_names())} of {sorted(CONSTANTS)} in {SOURCE.name}")


def test_the_engine_spells_each_name_as_this_file_does() -> None:
    assert engine_names() == CONSTANTS


def test_the_generator_accepts_exactly_the_next_use_names_the_engine_has() -> None:
    engine = {name for constant, name in engine_names().items()
              if constant.startswith("Next")}
    assert set(gen.NEXT_USE_ACTIONS) == engine


def test_the_generator_accepts_exactly_the_placed_names_the_engine_has() -> None:
    engine = {name for constant, name in engine_names().items()
              if constant.endswith("RemovedAction")}
    assert set(gen.PLACED_ACTIONS) == engine


def test_the_generator_accepts_exactly_the_every_nth_names_the_engine_has() -> None:
    engine = {name for constant, name in engine_names().items()
              if constant.startswith("Nth")}
    assert set(gen.NTH_ACTIONS) == engine


def test_the_generator_accepts_exactly_the_cooldown_reduce_names_the_engine_has() -> None:
    engine = {name for constant, name in engine_names().items()
              if constant.startswith("CooldownReduce")}
    assert set(gen.COOLDOWN_REDUCE_ACTIONS) == engine


def test_the_generator_accepts_exactly_the_cooldown_reset_names_the_engine_has() -> None:
    engine = {name for constant, name in engine_names().items()
              if constant.startswith("CooldownReset")}
    assert set(gen.COOLDOWN_RESET_ACTIONS) == engine
