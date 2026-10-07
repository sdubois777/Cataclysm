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
    # AND THE RANDOM DAMAGE OVER TIME, since issue #1833 group D.
    "ApplyRandomDotAction": "apply_random_dot",
    # AND THE HEALTH CAP, since issue #1833 group D part 2.
    "HealthCappedAtAction": "health_capped_at",
    # AND THE TWO NEARBY ACTIONS, since issue #1833 group D part 3.
    "SmiteNearbyAction": "smite_nearby",
    "HealNearbyEnemiesAction": "heal_nearby_enemies",
    # AND THE TWO REMAINING DAMAGE ACTIONS, since issue #1833 group D part 4.
    "RemainingDamageNearbyAction": "dot_remaining_nearby",
    "RemainingDamageTargetAction": "dot_remaining_target",
    # AND THE TWO STATUS ACTIONS AND THE TWO STATUSES THAT ARE NOT AILMENTS,
    # since issue #1833 group E part 1.
    "ApplyStatusAction": "apply_status",
    "ApplyStatusSecondsAction": "apply_status_seconds",
    "StaggerStatus": "Stagger",
    "RandomDebuffStatus": "Random Debuff",
    # AND THE THREE THAT LAY A STATUS ON THE WEARER, their two statuses and the
    # event one of them waits on. Ruled 2026-10-06.
    "ApplyStatusToSelfAction": "apply_status_to_self",
    "ApplyStatusToSelfSecondsAction": "apply_status_to_self_seconds",
    "ApplyStatusToSelfSizedAction": "apply_status_to_self_sized",
    "StunStatus": "Stun",
    "AppliedDotStatus": "Applied DoT",
    "SkillEndEvent": "skill_end",
    # AND THE NO-DAMAGE WINDOW, since issue #1833 group E part 2.
    "DamageImmunityAction": "damage_immunity",
    # AND THE REFLECT AND THE ARMOUR NOVA, since issue #1833 group E part 3.
    "ReflectBlockedAction": "reflect_blocked",
    "BlastFromTheDyingAction": "blast_from_the_dying",
    "SmiteNearbyByArmourAction": "smite_nearby_by_armor",
    # AND THE CLEANSE, which the engine has held since 2026-09-26 and the
    # generator accepted on 2026-10-05, issue #1833.
    "CleanseAction": "cleanse",
    # AND THE REPEAT OF THE SKILL JUST USED, since mechanism B2.
    "RepeatSkillAction": "repeat_skill",
    # AND THE RIDERS ON AN AILMENT, since issue #1833, 2026-10-06: four, and
    # the two speed riders after them.
    "AilmentDamageTakenAction": "ailment_damage_taken",
    "AilmentArmorRiderAction": "ailment_armor_removed",
    "AilmentDamageDealtAction": "ailment_damage_dealt",
    "AilmentHealingReceivedAction": "ailment_healing_received",
    "AilmentSpeedAction": "ailment_speed",
    "AilmentMovementSpeedAction": "ailment_movement_speed",
    "AilmentSpreadOnDeathAction": "ailment_spread_on_death",
    "AilmentDetonatesWhenReappliedAction": "ailment_detonates_when_reapplied",
    # AND THE TWO THAT TRIGGER A DIFFERENT HELD SKILL, ruled 2026-10-06.
    "TriggerHeldSkillAction": "trigger_held_skill",
    "TriggerHeldSpellAction": "trigger_held_spell",
    # AND THE FOUR THAT ROLL FOR THE USE IN HAND, ruled 2026-10-06.
    "UseNoDamageAction": "use_no_damage",
    "CooldownUseNoDamageAction": "cooldown_use_no_damage",
    "UseIncreasedDamageAction": "use_increased_damage",
    "CooldownUseIncreasedDamageAction": "cooldown_use_increased_damage",
    "UseHitsAllNearbyAction": "use_hits_all_nearby",
    # AND THE TWO THAT MAKE A USE HIT ITS OWN USER, by the owner's decision of 2026-10-06.
    "UseHitsItsUserAction": "use_hits_its_user",
    "UseBackfiresAction": "use_backfires",
}

AILMENTS_SOURCE = SOURCE.parent / "CataclysmAilments.cpp"

#: `{TEXT("Bleed"), TEXT("bleed_chance"),`, the first two fields of each kind
#: `UCataclysmAilments::KindNamed` reads.
AILMENT_KIND = re.compile(r'\{TEXT\("([A-Za-z ]+)"\),\s*TEXT\("[a-z_]+_chance"\)')


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


def test_the_generator_accepts_exactly_the_health_cap_name_the_engine_has() -> None:
    """Issue #1833 group D part 2."""
    assert gen.HEALTH_CAP_ACTION == engine_names()["HealthCappedAtAction"]


def test_the_generator_accepts_exactly_the_nearby_names_the_engine_has() -> None:
    """Issue #1833 group D part 3."""
    names = engine_names()
    assert set(gen.NEARBY_ACTIONS) == {names["SmiteNearbyAction"],
                                       names["HealNearbyEnemiesAction"],
                                       names["SmiteNearbyByArmourAction"]}


def test_the_generator_accepts_exactly_the_remaining_damage_names_the_engine_has() -> None:
    """Issue #1833 group D part 4."""
    names = engine_names()
    assert set(gen.REMAINING_DAMAGE_ACTIONS) == {names["RemainingDamageNearbyAction"],
                                                 names["RemainingDamageTargetAction"]}


def test_the_generator_accepts_exactly_the_random_dot_name_the_engine_has() -> None:
    """Issue #1833 group D."""
    assert gen.RANDOM_DOT_ACTION == engine_names()["ApplyRandomDotAction"]


def test_the_generator_accepts_exactly_the_status_names_the_engine_has() -> None:
    """Issue #1833 group E part 1."""
    names = engine_names()
    assert set(gen.APPLY_STATUS_ACTIONS) == {names["ApplyStatusAction"],
                                             names["ApplyStatusSecondsAction"]}
    assert set(gen.APPLY_STATUS_TO_SELF_ACTIONS) == {
        names["ApplyStatusToSelfAction"], names["ApplyStatusToSelfSecondsAction"],
        names["ApplyStatusToSelfSizedAction"]}
    assert names["StunStatus"] in gen.APPLY_STATUSES_TO_SELF_FOR_SECONDS
    assert gen.APPLIED_DOT_STATUS == names["AppliedDotStatus"]
    assert names["SkillEndEvent"] in gen.APPLY_STATUS_TO_SELF_EVENTS
    assert names["SkillEndEvent"] in gen.ACTION_ONLY_EVENTS


def test_the_generator_accepts_exactly_the_reflect_name_the_engine_has() -> None:
    """Issue #1833 group E part 3."""
    assert gen.REFLECT_BLOCKED_ACTION == engine_names()["ReflectBlockedAction"]
    assert gen.BLAST_FROM_THE_DYING_ACTION == engine_names()["BlastFromTheDyingAction"]


def test_the_generator_accepts_exactly_the_repeat_skill_name_the_engine_has() -> None:
    """Mechanism B2."""
    assert gen.REPEAT_SKILL_ACTION == engine_names()["RepeatSkillAction"]


def test_the_generator_knows_exactly_the_use_outcome_names_the_engine_has() -> None:
    """A chance per use, ruled 2026-10-06."""
    names = engine_names()
    assert gen.USE_NO_DAMAGE_ACTIONS == (
        names["UseNoDamageAction"], names["CooldownUseNoDamageAction"])
    assert gen.USE_INCREASED_DAMAGE_ACTIONS == (
        names["UseIncreasedDamageAction"], names["CooldownUseIncreasedDamageAction"])


def test_the_generator_knows_exactly_the_hit_its_user_names_the_engine_has() -> None:
    """The owner's decision of 2026-10-06."""
    names = engine_names()
    assert gen.USE_HITS_ITS_USER_ACTIONS == (
        names["UseHitsItsUserAction"], names["UseBackfiresAction"])


def test_the_generator_knows_exactly_the_hit_all_nearby_name_the_engine_has() -> None:
    """Ruled 2026-10-06."""
    assert gen.USE_HITS_ALL_NEARBY_ACTION == engine_names()["UseHitsAllNearbyAction"]


def test_the_generator_accepts_exactly_the_trigger_held_names_the_engine_has() -> None:
    """Spellblade's Will, ruled 2026-10-06."""
    assert gen.TRIGGER_HELD_SKILL_ACTION == engine_names()["TriggerHeldSkillAction"]
    assert gen.TRIGGER_HELD_SPELL_ACTION == engine_names()["TriggerHeldSpellAction"]


def test_the_generator_accepts_exactly_the_damage_immunity_name_the_engine_has() -> None:
    """Issue #1833 group E part 2."""
    assert gen.DAMAGE_IMMUNITY_ACTION == engine_names()["DamageImmunityAction"]


def test_the_generator_accepts_exactly_the_ailment_rider_names_the_engine_has() -> None:
    """Issue #1833, riders on an ailment."""
    engine = {name for constant, name in engine_names().items()
              if constant.startswith("Ailment")}
    assert set(gen.AILMENT_RIDER_ACTIONS) == engine


def test_the_generator_accepts_exactly_the_cleanse_name_the_engine_has() -> None:
    """Issue #1833, the timed cleanse."""
    assert gen.CLEANSE_ACTION == engine_names()["CleanseAction"]


def test_every_status_a_row_may_name_is_one_the_engine_applies() -> None:
    """Issue #1833 group E part 1. A status the generator accepts and the engine
    cannot find logs a warning when it fires and applies nothing. Every name but
    the two the component holds must be an ailment kind; Stun is one, and is
    left to the random debuff because `UCataclysmAilments::Apply` refuses it."""
    kinds = set(AILMENT_KIND.findall(AILMENTS_SOURCE.read_text(encoding="utf-8")))
    assert len(kinds) == 11, f"read {sorted(kinds)} from {AILMENTS_SOURCE.name}"
    names = engine_names()
    named = {names["StaggerStatus"], names["RandomDebuffStatus"]}
    assert set(gen.APPLY_STATUSES) - named == kinds - {"Stun"}
    assert set(gen.APPLY_STATUSES_FOR_SECONDS) <= set(gen.APPLY_STATUSES)
