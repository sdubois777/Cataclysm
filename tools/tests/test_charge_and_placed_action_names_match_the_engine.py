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
    # AND THE STUN NEAR A DYING ENEMY, AND THE STATUS THAT IS ALL FIVE DAMAGE
    # OVER TIME AILMENTS, ruled 2026-10-08.
    "StunNearTheDyingAction": "stun_near_the_dying",
    "AllDotsStatus": "All DoTs",
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
    "AilmentSpreadOnApplicationAction": "ailment_spread_on_application",
    # AND THE ONE THAT LOWERS ITS CARRIER'S MAXIMUM HEALTH, ruled 2026-10-08.
    "AilmentMaxHealthRiderAction": "ailment_max_health_removed",
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
    # AND THE ONE THAT EXPLODES A KILLED ENEMY FOR ITS OVERKILL, ruled 2026-10-07.
    "ExplodeVictimForOverkillAction": "explode_victim_for_overkill",
    # AND THE ONE THAT STRIKES THE OTHER CHARACTER OF ITS EVENT, ruled 2026-10-07.
    "StrikeTargetAction": "strike_target",
    # AND THE ONE THAT GRANTS A TEMPORARY ABSORB, by the owner's decision of 2026-10-07.
    "TemporaryAbsorbAction": "temporary_absorb",
    # AND THE STATUS THAT HOLDS ONE OF THE WEARER'S RESISTANCES AT NOUGHT, ruled 2026-10-12.
    "RandomResistanceAtNoughtStatus": "Random Resistance At Nought",
    # AND THE STATUS THAT LAYS A RANDOM BUFF OR DEBUFF ON THE WEARER, ruled 2026-10-12.
    "RandomBuffOrDebuffStatus": "Random Buff or Debuff",
    # AND THE TWO STATS OF AN IMMUNITY TO THE DAMAGE TYPE THAT HIT, ruled
    # 2026-10-12. Stats and not actions; the generator holds both names for
    # the refusal it makes on their rows, so they are compared here as well.
    "DamageTypeImmunityOnHitChanceStat": "damage_type_immunity_on_hit_chance",
    "DamageTypeImmunityOnHitSecondsStat": "damage_type_immunity_on_hit_seconds",
    # AND THE ONE THAT GAINS KEPT STACKS ON A CLOCK AND SPENDS THEM ON A USE TO
    # CAST ITS SKILL AGAIN, ruled 2026-10-12.
    "KeptStacksRepeatSkillAction": "kept_stacks_repeat_skill",
    # AND THE ONE THAT EXECUTES THE ENEMY A HIT LEAVES BELOW A SHARE OF ITS
    # HEALTH, ruled 2026-10-12.
    "ExecuteTargetAction": "execute_target",
    # AND THE ONE THAT MOVES ITS WEARER TO A RANDOM REACHABLE POINT NEARBY WHEN
    # IT IS HIT, ruled 2026-10-12.
    "TeleportRandomlyAction": "teleport_randomly",
    # AND THE STATUS THAT SILENCES THE OTHER CHARACTER OF THE EVENT, ruled 2026-10-17.
    "SilenceStatus": "Silence",
    # AND THE ONE THAT GAINS KEPT STACKS ON AN EVENT AND, AT THEIR CAP, MAKES THE
    # NEXT ABILITY FREE AND EMPOWERED, ruled 2026-10-17.
    "KeptStacksFreeNextAbilityAction": "kept_stacks_free_next_ability",
    # AND THE ONE THAT FIRES EVERY GADGET THE WEARER COMMANDS AT ITS DEATH, ruled 2026-10-19.
    "GadgetsCastNowAction": "gadgets_cast_now",
    # AND THE TWO OF TRAPS LAYER 3, ruled 2026-10-21: a damage over time at a
    # size on the other character, and a slow at a strength on it.
    "ApplyStatusSizedAction": "apply_status_sized",
    "SlowForSecondsAction": "slow_for_seconds",
    # AND THE ONE OF TRAPS LAYER 4, ruled 2026-10-21: an enemy killed explodes
    # for a share of the blow that killed it.
    "ExplodeVictimForKillingBlowAction": "explode_victim_for_killing_blow",
    # AND THE TWO OF TRAPS LAYER 5, ruled 2026-10-22: the wearer's other traps
    # armed, or set off.
    "ArmTrapsNearbyAction": "arm_traps_nearby",
    "TriggerTrapsNearbyAction": "trigger_traps_nearby",
    # AND THE ONE OF TRAPS LAYER 6, ruled 2026-10-22: the wearer's nearest skill
    # gadget fires at the enemy that set a trap off.
    "GadgetFiresAtTrapEnemyAction": "gadget_fires_at_trap_enemy",
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


def test_the_generator_accepts_exactly_the_stun_near_the_dying_name_the_engine_has() -> None:
    """Ruled 2026-10-08."""
    assert gen.STUN_NEAR_THE_DYING_ACTION == engine_names()["StunNearTheDyingAction"]


def test_the_generator_spells_all_five_damage_over_time_ailments_as_the_engine_does() -> None:
    """The owner, 2026-10-08. The one status name `apply_status` alone may
    use; and the five it stands for are the generator's `AILMENTS`, which the
    engine reads off `RandomDamageOverTimePool`."""
    assert gen.ALL_DOTS_STATUS == engine_names()["AllDotsStatus"]
    assert gen.ALL_DOTS_STATUS not in gen.APPLY_STATUSES
    assert gen.ALL_DOTS_STATUS not in gen.APPLY_STATUSES_FOR_SECONDS
    pool = re.search(
        r"UCataclysmAilments::RandomDamageOverTimePool\(\)\s*\{(.*?)\n\}",
        AILMENTS_SOURCE.read_text(encoding="utf-8"), re.DOTALL)
    assert pool, "RandomDamageOverTimePool was not found in CataclysmAilments.cpp"
    listed = re.search(r"for \(const TCHAR\* Name : \{(.*?)\}\)", pool.group(1), re.DOTALL)
    assert listed, "the pool's list of names was not found"
    assert tuple(re.findall(r'TEXT\("([A-Za-z ]+)"\)', listed.group(1))) == gen.AILMENTS


def test_the_generator_spells_the_statuses_for_a_wearer_only_as_the_engine_does() -> None:
    """Ruled 2026-10-12. A status only `apply_status_to_self` may name is not an
    ailment kind, so `UCataclysmAilments::KindNamed` cannot find it: the engine
    compares the row's Ailment cell with a constant of its own before it looks.
    A name spelled differently on the two sides would pass the generator, and
    in the game it would log a warning each time it fired and do nothing."""
    names = engine_names()
    assert gen.RANDOM_RESISTANCE_AT_NOUGHT_STATUS == names["RandomResistanceAtNoughtStatus"]
    assert gen.RANDOM_BUFF_OR_DEBUFF_STATUS == names["RandomBuffOrDebuffStatus"]
    assert set(gen.APPLY_STATUSES_TO_SELF_ONLY) == {
        name for constant, name in names.items()
        if constant in ("RandomResistanceAtNoughtStatus", "RandomBuffOrDebuffStatus")}
    # FOR A WEARER ONLY: the lists the other four status actions read hold none.
    for other in (gen.APPLY_STATUSES, gen.APPLY_STATUSES_FOR_SECONDS,
                  gen.APPLY_STATUSES_TO_SELF_FOR_SECONDS):
        assert not set(gen.APPLY_STATUSES_TO_SELF_ONLY) & set(other)


def test_the_generator_accepts_exactly_the_repeat_skill_name_the_engine_has() -> None:
    """Mechanism B2."""
    assert gen.REPEAT_SKILL_ACTION == engine_names()["RepeatSkillAction"]


def test_the_generator_accepts_exactly_the_kept_stacks_repeat_skill_name_the_engine_has() -> None:
    """Ruled 2026-10-12. One name on each side, and the item code compares a
    row's Action with the engine's constant and not with a spelling of its
    own."""
    assert gen.KEPT_STACKS_REPEAT_SKILL_ACTION == engine_names()["KeptStacksRepeatSkillAction"]
    item = (ROOT / "game" / "Source" / "Cataclysm" / "Items"
            / "CataclysmItem.cpp").read_text(encoding="utf-8")
    assert "UCataclysmAbilitySystemComponent::KeptStacksRepeatSkillAction" in item
    assert '"kept_stacks_repeat_skill"' not in item


def test_the_generator_accepts_exactly_the_kept_stacks_free_next_ability_name_the_engine_has() -> None:
    """Ruled 2026-10-17, the Null Emperor 6-piece. As for the action above: one
    name on each side, and the item code compares with the engine's constant."""
    assert gen.KEPT_STACKS_FREE_NEXT_ABILITY_ACTION == engine_names()["KeptStacksFreeNextAbilityAction"]
    assert gen.KEPT_STACKS_FREE_NEXT_ABILITY_ACTION not in gen.POOL_ACTIONS
    item = (ROOT / "game" / "Source" / "Cataclysm" / "Items"
            / "CataclysmItem.cpp").read_text(encoding="utf-8")
    assert "UCataclysmAbilitySystemComponent::KeptStacksFreeNextAbilityAction" in item
    assert '"kept_stacks_free_next_ability"' not in item


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


def test_the_generator_accepts_exactly_the_overkill_explosion_name_the_engine_has() -> None:
    """Ruled 2026-10-07."""
    assert gen.OVERKILL_EXPLOSION_ACTION == engine_names()["ExplodeVictimForOverkillAction"]


def test_the_generator_accepts_exactly_the_strike_target_name_the_engine_has() -> None:
    """Ruled 2026-10-07."""
    assert gen.STRIKE_TARGET_ACTION == engine_names()["StrikeTargetAction"]


def test_the_generator_accepts_exactly_the_execute_target_name_the_engine_has() -> None:
    """Ruled 2026-10-12. And the condition the generator requires on the row is
    one the engine's condition table names, because the engine reads the row's
    share of health from that condition's value and from no other."""
    assert gen.EXECUTE_TARGET_ACTION == engine_names()["ExecuteTargetAction"]
    assert gen.EXECUTE_TARGET_ACTION not in gen.POOL_ACTIONS
    pipeline = (SOURCE.parent / "CataclysmStatPipeline.cpp").read_text(encoding="utf-8")
    assert re.search(
        rf'TEXT\("{gen.EXECUTE_TARGET_CONDITION}"\),\s*'
        r"ECataclysmStatCondition::TargetHealthBelowPercent", pipeline)
    assert ("Action.Condition == ECataclysmStatCondition::TargetHealthBelowPercent"
            in SOURCE.read_text(encoding="utf-8"))


def test_the_generator_accepts_exactly_the_temporary_absorb_name_the_engine_has() -> None:
    """The owner's decision of 2026-10-07: separate from the energy shield."""
    assert gen.TEMPORARY_ABSORB_ACTION == engine_names()["TemporaryAbsorbAction"]


def test_a_temporary_absorb_is_not_a_pool_of_two_attributes() -> None:
    """It is one plain number on the ability system component, so
    `PoolAttributesFor` has no pair for it and it must not be listed with the
    pools that have one."""
    assert gen.TEMPORARY_ABSORB_ACTION not in gen.POOL_ACTIONS
    assert gen.TEMPORARY_ABSORB_EVENTS == (gen.TIMED_EVENT,)


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


def test_the_generator_accepts_exactly_the_teleport_randomly_name_the_engine_has() -> None:
    """Ruled 2026-10-12. And the three lines of code the action needs are in the
    engine as code and not only named in a comment: the item sets the flag from
    the name, the row loop asks the flag and whether the blow was a tick, and
    `NoteHitTaken` says whether it was for the length of its call."""
    assert gen.TELEPORT_RANDOMLY_ACTION == engine_names()["TeleportRandomlyAction"]
    assert gen.TELEPORT_RANDOMLY_ACTION not in gen.POOL_ACTIONS
    source = SOURCE.read_text(encoding="utf-8")
    code = "\n".join(line for line in source.splitlines()
                     if not line.lstrip().startswith(("//", "*", "/*")))
    assert re.search(
        r"if \(Action\.bTeleportRandomly\)\s*\{\s*"
        r"if \(bLanded && !bHitBeingAnnouncedIsATick && ", code)
    assert ("TGuardValue<bool> SaysWhetherATick(bHitBeingAnnouncedIsATick, bDamageOverTime);"
            in code)
    item = (SOURCE.parents[1] / "Items" / "CataclysmItem.cpp").read_text(encoding="utf-8")
    assert re.search(
        r"Action\.bTeleportRandomly = Effect->Action\.Equals\(\s*"
        r"UCataclysmAbilitySystemComponent::TeleportRandomlyAction,", item)


def test_the_generator_holds_exactly_the_damage_type_immunity_stats_the_engine_reads() -> None:
    """Ruled 2026-10-12. The chance and the seconds of "become immune to the
    damage type that hit you": the two names the generator refuses bad rows
    on are the two `RollDamageTypeImmunityOnHit` asks for, and no other."""
    engine = engine_names()
    assert gen.DAMAGE_TYPE_IMMUNITY_ON_HIT_CHANCE_STAT == engine[
        "DamageTypeImmunityOnHitChanceStat"]
    assert gen.DAMAGE_TYPE_IMMUNITY_ON_HIT_SECONDS_STAT == engine[
        "DamageTypeImmunityOnHitSecondsStat"]
    assert set(gen.DAMAGE_TYPE_IMMUNITY_ON_HIT_STATS) == {
        engine["DamageTypeImmunityOnHitChanceStat"],
        engine["DamageTypeImmunityOnHitSecondsStat"]}


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


def test_the_generator_spells_silence_as_the_engine_does() -> None:
    """Ruled 2026-10-17, for the Null Emperor's 2-piece bonus. Silence is not an
    ailment kind, so `UCataclysmAilments::KindNamed` cannot find it: the engine
    compares the row's Ailment cell with `SilenceStatus` before it looks. A name
    spelled differently on the two sides would pass the generator and, in the
    game, log a warning each time the row fired and silence nobody."""
    assert gen.SILENCE_STATUS == engine_names()["SilenceStatus"]
    # FOR `apply_status` ALONE: the lists the other four status actions read hold it not.
    for other in (gen.APPLY_STATUSES, gen.APPLY_STATUSES_FOR_SECONDS,
                  gen.APPLY_STATUSES_TO_SELF_FOR_SECONDS, gen.APPLY_STATUSES_TO_SELF_ONLY):
        assert gen.SILENCE_STATUS not in other


def test_the_generator_accepts_exactly_the_gadgets_cast_now_name_the_engine_has() -> None:
    """Ruled 2026-10-19, for "When you die, all your active traps and gadgets
    immediately trigger simultaneously". And the lines the action needs are in
    the engine as code and not only named in a comment: the item sets the flag
    from the name, the row loop asks the flag and fires every gadget of the
    avatar, and the gadget's death cast lifts only the dead-player refusal."""
    assert gen.GADGETS_CAST_NOW_ACTION == engine_names()["GadgetsCastNowAction"]
    assert gen.GADGETS_CAST_NOW_ACTION not in gen.POOL_ACTIONS

    def code_of(path: Path) -> str:
        return "\n".join(line for line in path.read_text(encoding="utf-8").splitlines()
                         if not line.lstrip().startswith(("//", "* ", "/*", "*/"))
                         and line.strip() != "*")

    assert re.search(
        r"if \(Action\.bGadgetsCastNow\)\s*\{\s*"
        r"if \(bLanded && !StackedThisEvent\.Contains\(Action\.TriggerKey\)\)\s*\{\s*"
        r"StackedThisEvent\.Add\(Action\.TriggerKey\);\s*"
        r"ACataclysmMinion::FireEveryGadgetAtTheDeathOf\(GetAvatarActor\(\)\);", code_of(SOURCE))
    assert re.search(
        r"Action\.bGadgetsCastNow = Effect->Action\.Equals\(\s*"
        r"UCataclysmAbilitySystemComponent::GadgetsCastNowAction,",
        code_of(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp"))
    minion = code_of(SOURCE.parent / "CataclysmMinion.cpp")
    assert "(!bEvenForADeadPlayer && UCataclysmSkillEffects::IsDead(Player))" in minion
    assert "CastPlayersSkillFromHere(/*bEvenForADeadPlayer=*/true)" in minion


def test_the_generator_accepts_exactly_the_two_names_of_traps_layer_three_the_engine_has() -> None:
    """Ruled 2026-10-21 (traps layer 3, M3). The sized status on another
    character is a status action, the slow is not; and each is recognised by
    the item from its constant. THE CONTROL: the to-self sized name, which the
    new one must not replace."""
    names = engine_names()
    assert gen.APPLY_STATUS_SIZED_ACTION == names["ApplyStatusSizedAction"]
    assert gen.APPLY_STATUS_SIZED_ACTION in gen.ALL_APPLY_STATUS_ACTIONS
    assert names["ApplyStatusToSelfSizedAction"] in gen.ALL_APPLY_STATUS_ACTIONS
    assert gen.APPLY_STATUS_SIZED_ACTION not in gen.APPLY_STATUS_TO_SELF_ACTIONS
    assert gen.SLOW_FOR_SECONDS_ACTION == names["SlowForSecondsAction"]
    assert gen.SLOW_FOR_SECONDS_ACTION not in gen.POOL_ACTIONS
    assert gen.SLOW_FOR_SECONDS_ACTION not in gen.ALL_APPLY_STATUS_ACTIONS
    item = (SOURCE.parents[1] / "Items" / "CataclysmItem.cpp").read_text(encoding="utf-8")
    # THE COMPARISON ITSELF, which no comment holds.
    assert re.search(r"Effect->Action\.Equals\(\s*UCataclysmAbilitySystemComponent::"
                     r"ApplyStatusSizedAction,", item)
    assert re.search(r"Action\.bSlowForSeconds = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::SlowForSecondsAction,", item)
    # AND THE SLOW ASKS THE RULE OF A TENTH against what the trap's cast took
    # from the enemy that set it off, which `trap_triggered` carries. The owner,
    # 2026-10-21: "Follow the tenth rule".
    # WHEN NONE HAS QUALIFIED YET, IT WAITS FOR A LATER BLOW OF THE SAME CAST,
    # which `ApplyHit` hands it (ruled 2026-10-21, brief 60c).
    engine = SOURCE.read_text(encoding="utf-8")
    assert re.search(r"if \(UCataclysmAilments::BlowCanCarryAnAilment\(Slowed, EventAmount\)\)\s*\{\s*"
                     r"if \(Slower && UCataclysmAilments::ApplySlowOfStrength\(", engine)
    assert "LateSkillTrapSlows.Add(Late);" in engine
    effects = (SOURCE.parent / "CataclysmSkillEffects.cpp").read_text(encoding="utf-8")
    # SINCE TRAPS LAYER 3B (#2462) THE LATE BLOW IS FOUND ONCE, by the used-up
    # trap's identity, and the slow is asked with that identity beside the
    # trap's `deployable_hit`.
    assert re.search(r"MachinesPlayer->LayALateSkillTrapSlow\(SpentTrap\.Trap, Target, "
                     r"Result->DealtToHealth\);", effects)
    minion = (SOURCE.parent / "CataclysmMinion.cpp").read_text(encoding="utf-8")
    assert re.search(r'ActOnEvent\(FName\(TEXT\("trap_triggered"\)\), &TrapTags, '
                     r'/\*EventAmount=\*/SkillTrapTookFromItsEnemy,', minion)


def test_a_used_up_traps_late_blows_are_found_by_its_identity_where_they_are_priced() -> None:
    """Ruled 2026-10-21 (traps layer 3b, #2462, Z1 (a)). A skill trap is kept
    on its player as it is used up, and found again by its identity where a
    blow is priced and announced and where a zone is priced as it is laid; a
    buried weapon hands the identity on and never through `Get`. Read from the
    code with comment lines left out. THE CONTROL: the same reading finds the
    buried weapon's `Caster.Get()`, so a pattern that saw no `.Get()` at all
    could not pass the check that `CastBy` is never read through one.

    COMMENTS ARE STRIPPED BY TRACKING THEM, `/* ... */` across lines and `//`
    to the end of a line, and never by a line's opening characters (ruled
    2026-10-21, R-c): a wrapped product continues on a line opening with
    `* `, which a prefix rule drops. A `//` or `/*` inside a string literal is
    read as a comment too; it only shortens what is read, and none of the
    files read holds one. THE CONTROL ON THE STRIPPER: `ApplyHit`'s continued
    line `* TrapMore` is kept."""
    def code_of(path: Path) -> str:
        text = path.read_text(encoding="utf-8")
        kept = []
        at = 0
        while at < len(text):
            if text.startswith("/*", at):
                end = text.find("*/", at + 2)
                end = len(text) if end < 0 else end + 2
                kept.append("\n" * text.count("\n", at, end))
                at = end
            elif text.startswith("//", at):
                end = text.find("\n", at)
                at = len(text) if end < 0 else end
            else:
                kept.append(text[at])
                at += 1
        return "".join(kept)

    minion = code_of(SOURCE.parent / "CataclysmMinion.cpp")
    assert re.search(r"Theirs->NoteSkillTrapSpent\(this, TypeTags\);\s*Destroy\(\);", minion)
    effects = code_of(SOURCE.parent / "CataclysmSkillEffects.cpp")
    assert re.search(r"^\s*\* TrapMore$", effects, re.MULTILINE)
    assert "SpentTrapsPlayer->SpentSkillTrapNamed(NamedCastBy, SpentTrap)" in effects
    assert "FGameplayTagContainer GadgetsTags = Machine ? Machine->TypeTags : SpentTrap.TypeTags;" in effects
    template = code_of(SOURCE.parent / "CataclysmSkillTemplate.cpp")
    assert "LaysForATrap->SpentSkillTrapNamed(FreeRepeatCastBy, SpentTrap)" in template
    assert "PerTick * TrapsPartOfTheTick" in template
    buried = code_of(SOURCE.parent / "CataclysmBuriedWeapon.cpp")
    assert "Buried->Caster.Get()" in buried
    assert "CastBy.Get()" not in buried
    templates = code_of(SOURCE.parent / "CataclysmSkillTemplates.cpp")
    assert re.search(r"Params\.bBurns, FreeRepeatCastBy\)\)", templates)
    engine = code_of(SOURCE)
    assert "SpentSkillTraps.Empty();" in engine


def code_without_comments(path: Path) -> str:
    """The file's text with comments taken out by TRACKING them, `/* ... */`
    across lines and `//` to the end of a line, never by a line's opening
    characters (ruled 2026-10-21, R-c): a wrapped product continues on a line
    opening with `* `. A `//` or `/*` inside a string literal is read as a
    comment too, which only shortens what is read."""
    text = path.read_text(encoding="utf-8")
    kept = []
    at = 0
    while at < len(text):
        if text.startswith("/*", at):
            end = text.find("*/", at + 2)
            end = len(text) if end < 0 else end + 2
            kept.append("\n" * text.count("\n", at, end))
            at = end
        elif text.startswith("//", at):
            end = text.find("\n", at)
            at = len(text) if end < 0 else end
        else:
            kept.append(text[at])
            at += 1
    return "".join(kept)


def test_the_killing_blow_explosion_of_traps_layer_four_is_recorded_and_made_at_the_kill() -> None:
    """Ruled 2026-10-21 (traps layer 4, F10 to F12). The generator's name is
    the engine's; the item recognises the row by its constant; the event loop
    records the share; and the kill site takes it, makes the explosion with
    the killing blow's own figure at 4 m, and refuses a kill by a tick. Read
    from the code with comments taken out. THE CONTROL: the same reading finds
    the overkill explosion's own call, so a pattern blind to the kill site
    could not pass."""
    names = engine_names()
    assert gen.KILLING_BLOW_EXPLOSION_ACTION == names["ExplodeVictimForKillingBlowAction"]
    assert gen.KILLING_BLOW_EXPLOSION_EVENTS == ("kill",)
    assert gen.KILLING_BLOW_EXPLOSION_ACTION not in gen.POOL_ACTIONS
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Action\.bExplodeVictimForKillingBlow = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction,", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r"PendingKillingBlowExplosionSharePercent =\s*"
                     r"FMath::Max\(PendingKillingBlowExplosionSharePercent, Action\.Percent\);", engine)
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"UCataclysmSkillEffects::ExplodeForOverkill\(Killer\.Get\(\), SlainActor\.Get\(\), At, "
                     r"Overkill,", player)
    assert "Acting->TakePendingKillingBlowExplosionSharePercent();" in player
    assert re.search(r"if \(TrapKillExplosionShare > 0\.0f && !Notice\.bByDamageOverTime && "
                     r"Notice\.KillingBlowDamage > 0\.0f\)", player)
    assert re.search(r"KillingBlowFigure, TrapKillExplosionShare,\s*"
                     r"UCataclysmSkillEffects::KillingBlowExplosionRadiusCm\);", player)
    events = code_without_comments(SOURCE.parent / "CataclysmCombatEvents.cpp")
    assert "Blow.KillingBlowDamage = Hit.Damage;" in events
    assert "Notice.KillingBlowDamage = Blow.KillingBlowDamage;" in events


def test_the_two_trap_actions_of_traps_layer_five_set_off_a_recorded_trap_whole_once_a_chain() -> None:
    """Ruled 2026-10-22 (traps layer 5, H1 to H11; H5 (b) by the owner; G-1
    (a): a trap set off by a row is a whole trigger). The generator's names
    are the engine's; the item recognises each row by its constant; the action
    RECORDS a trap, putting it into the chain first, and no longer sets it off
    itself; the drain, run after the queued deaths in `ActOnEvent`'s
    `ON_SCOPE_EXIT`, sets each off by `TriggerSkillTrap` at its own triggering
    enemy; and no `trap_triggered` is kept at depth one any more. Read from the
    code with comments taken out by tracking `/* ... */` and `//` (R-c). THE
    CONTROL: the same reading finds the queue of `afflicted_death`, which
    stays."""
    names = engine_names()
    assert gen.ARM_TRAPS_NEARBY_ACTION == names["ArmTrapsNearbyAction"]
    assert gen.TRIGGER_TRAPS_NEARBY_ACTION == names["TriggerTrapsNearbyAction"]
    for action in gen.TRAPS_NEARBY_ACTIONS:
        assert action not in gen.POOL_ACTIONS, action
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Action\.bArmTrapsNearby = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::ArmTrapsNearbyAction,", item)
    assert re.search(r"Action\.bSetOffTrapsNearby = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::TriggerTrapsNearbyAction,", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r'Event == FName\(TEXT\("afflicted_death"\)\)\)\s*\{\s*'
                     r"QueueAfflictedDeath\(Event, EventTags, EventAmount, EventTarget\);", engine)
    assert 'Event == FName(TEXT("trap_triggered")))' not in engine
    assert re.search(r"ON_SCOPE_EXIT\s*\{\s*DrainQueuedAfflictedDeaths\(\);\s*"
                     r"DrainQueuedSkillTrapSetOffs\(\);\s*\};", engine)
    assert re.search(r"Trap->TriggerSkillTrap\(Trap->SkillTrapEnemyForAForcedTrigger\(\), "
                     r"Next\.bEvenForADeadPlayer\);", engine)
    assert "SkillTrapsSetOffThisChain.Reset();" in engine
    minion = code_without_comments(SOURCE.parent / "CataclysmMinion.cpp")
    assert re.search(r"Theirs->SkillTrapsSetOffThisChain\.Add\(TWeakObjectPtr<AActor>\(Each\)\);\s*"
                     r"Theirs->QueueSkillTrapSetOff\(Each, bEvenForADeadPlayer\);", minion)
    assert "SkillTrapEnemyForAForcedTrigger(), bEvenForADeadPlayer)" not in minion


def test_a_gadgets_shot_at_a_traps_enemy_is_recorded_and_fired_at_depth_nought_and_the_pull_comes_first() -> None:
    """Ruled 2026-10-22 (traps layer 6, J1 to J10, J2 changed: skill gadgets
    only). The generator's name is the engine's; the item recognises the row by
    its constant; the event loop COUNTS, then RECORDS the shot, and fires
    nothing itself; a drain of its own, in an `ON_SCOPE_EXIT` declared before
    the queued deaths' so it runs after them, fires each recorded shot by
    `FireSkillGadgetAtTrapEnemy` at its "more", so the shot's blows are at
    depth nought; the shot casts by the clock's own path, aimed at the enemy;
    and a trap's pull is made in `TriggerSkillTrap` BEFORE its cast starts.
    Read from the code with comments taken out by tracking `/* ... */` and
    `//` (R-c). THE CONTROL: the same reading finds layer 5's drain of set-off
    traps, which stays as it was."""
    names = engine_names()
    assert gen.GADGET_FIRES_AT_TRAP_ENEMY_ACTION == names["GadgetFiresAtTrapEnemyAction"]
    assert gen.GADGET_FIRES_AT_TRAP_ENEMY_ACTION not in gen.POOL_ACTIONS
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Action\.bGadgetFiresAtTrapEnemy = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::GadgetFiresAtTrapEnemyAction,", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r"ON_SCOPE_EXIT\s*\{\s*DrainQueuedAfflictedDeaths\(\);\s*"
                     r"DrainQueuedSkillTrapSetOffs\(\);\s*\};", engine)
    assert re.search(r"ON_SCOPE_EXIT\s*\{\s*DrainQueuedGadgetShotsAtTrapEnemies\(\);\s*\};\s*"
                     r"ON_SCOPE_EXIT\s*\{\s*DrainQueuedAfflictedDeaths\(\);", engine)
    assert re.search(r"if \(CountedToNth\(Action\) && QueueGadgetShotAtTrapEnemy\(Action, EventTarget\)\)", engine)
    assert "Shooter->FireSkillGadgetAtTrapEnemy(Next.Enemy.Get(), Next.DamageMultiplier);" in engine
    assert re.search(r"if \(PoolActionDepth > 0 \|\| bDrainingQueuedGadgetShotsAtTrapEnemies\)", engine)
    minion = code_without_comments(SOURCE.parent / "CataclysmMinion.cpp")
    assert "CastPlayersSkillFromHere(false, Enemy, ShotDamageMultiplier)" in minion
    assert re.search(r"StartPlayersSkillFromHere\(Player, AsTheGadgets, SkillGadgetReachCm\(Skill\), AimedAt, nullptr,\s*"
                     r"ShotDamageMultiplier\)", minion)
    trigger = minion[minion.index("bool ACataclysmMinion::TriggerSkillTrap("):]
    assert trigger.index("PullEnemiesToThisSkillTrap(Player);") \
        < trigger.index("StartPlayersSkillFromHere(Player, AsTheTraps,")


def test_a_used_up_traps_shot_and_landing_look_for_its_wearer_by_its_identity() -> None:
    """Ruled 2026-10-22 (traps layer 7, K1). A shot or a landing of a skill
    trap already used up still strikes its wearer standing inside its blast:
    the explosion where a shot stops and the projectile skill's landing each
    hand `WearerInSphere` the identity of what cast the use as well as `Get`
    of it, `WearerInSphere` asks for a used-up trap only when no standing
    deployable answered, and `IsTheWearersUsedUpTrap` finds the identity among
    the wearer's used-up traps. Read from the code with comments taken out by
    tracking `/* ... */` and `//` (R-c). THE CONTROL: the same reading finds
    `UCataclysmStrikeSkill::SwingOnce`'s sphere handing `Get` alone, which K1
    leaves as it was, so a pattern that saw no identity argument anywhere
    could not pass."""
    blast = code_without_comments(SOURCE.parent / "CataclysmDeployableSelfBlast.cpp")
    assert "return Cataclysm && Cataclysm->SpentSkillTrapNamed(CastBy, Spent);" in blast
    assert re.search(r"!\(IsTheWearersDeployable\(Wearer, CastFrom\) \|\| "
                     r"IsTheWearersUsedUpTrap\(Wearer, UsedUpCastBy\)\)", blast)
    projectile = code_without_comments(SOURCE.parent / "CataclysmProjectile.cpp")
    assert re.search(r"WearerInSphere\(\s*GetWorld\(\), Firer, CastBy\.Get\(\), GetActorLocation\(\), RadiusCm, "
                     r"CastBy\)\)", projectile)
    templates = code_without_comments(SOURCE.parent / "CataclysmSkillTemplates.cpp")
    assert re.search(r"WearerInSphere\(\s*GetWorld\(\), Self, FreeRepeatCastBy\.Get\(\), Destination, "
                     r"ScaledRadiusCm\(\), FreeRepeatCastBy\);", templates)
    assert re.search(r"WearerInSphere\(\s*GetWorld\(\), Self, FreeRepeatCastBy\.Get\(\), Apex, "
                     r"FMath::Max\(ThisUseHitsAllWithinCm, ScaledRadiusCm\(\)\)\)", templates)
