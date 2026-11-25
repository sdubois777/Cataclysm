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
import csv
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
    # AND EVERY SKILL BUT THE EVENT'S OWN, since the Death trees' slice C3b (Turn of the Wheel).
    "CooldownReduceOthersAction": "cooldown_reduce_others",
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
    # AND THE DEATH TREES' SLICE C4c1 (2026-11-25): an enemy killed bursts for a
    # share of its maximum health, The Ferryman.
    "BurstSlainForMaximumHealthAction": "burst_slain_for_share_of_maximum_health",
    # AND THE DEATH TREES' SLICE C4c2 (2026-11-25): the wearer's Bleed left on an
    # enemy that died passed to the strongest enemy near it, Blood Debt.
    "PassBleedLeftToStrongestAction": "pass_bleed_left_to_strongest",
    # AND THE DEATH TREES' SLICE C4d1 (2026-11-25): the next landed hit within
    # the seconds made a critical strike, Chain Execution.
    "CriticalNextHitAction": "next_hit_critical",
    # AND THE DEATH TREES' SLICE C6a (2026-11-25): the movement skill's cooldown
    # cut, and the hold before decay lengthened on an evasion.
    "CooldownReduceMovementAction": "cooldown_reduce_movement",
    "FervourHoldExtendedAction": "fervour_hold_extended",
    # AND THE DEATH TREES' SLICE C6b4 (2026-11-25): an evasion's critical
    # strike charge doubled on a roll, Twice Answered.
    "CriticalTwiceOnEvasionAction": "next_hit_critical_twice",
    # AND THE DEATH TREES' SLICE C6e (2026-11-25): Blade Echo's and Thousand
    # Cuts' strikes on the strongest enemy near a kill.
    "StrikeStrongestNearAction": "strike_strongest_near",
    "DashToStrongestAndStrikeAction": "dash_to_strongest_and_strike",
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
    # THE CONTROL READS THE CASTER THROUGH `Get` WHEREVER THE LEAP KEEPS IT: on the component until issue #2872, and
    # since then on what the leap copies out of the component before any blow (`Carried`).
    assert "Carried.Caster.Get()" in buried
    assert "CastBy.Get()" not in buried
    templates = code_of(SOURCE.parent / "CataclysmSkillTemplates.cpp")
    # THE TRAP'S IDENTITY IS STILL HANDED TO THE BURIED AXE, and since issue #2764 slice 2a the throwing skill's row after
    # it, so a leap scales by that skill's level and credits it. Two closing brackets and not three since issue #2872,
    # when the call's result was kept (`Left`) rather than tested inside an `if`.
    assert re.search(r"Params\.bBurns, FreeRepeatCastBy,\s*UCataclysmSkillLevels::RowThatLevels\(this\)\);", templates)
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


def test_the_ferrymans_burst_is_recorded_and_made_at_the_kill_by_the_same_explosion() -> None:
    """The Death trees' slice C4c1 (2026-11-25). The generator's name is the
    engine's; the item recognises the row by its constant; the event loop
    records the share; and the kill site takes it and makes the burst through
    `ExplodeForOverkill`, sized by the slain creature's maximum health, at
    `SlainBurstRadiusCm`, handed to the announcer so a creature it kills bursts
    too. Read from the code with comments taken out. THE CONTROL: the same
    reading finds the overkill explosion's own call."""
    names = engine_names()
    assert gen.SLAIN_BURST_ACTION == names["BurstSlainForMaximumHealthAction"]
    assert gen.SLAIN_BURST_EVENTS == ("kill",)
    assert gen.SLAIN_BURST_ACTION not in gen.POOL_ACTIONS
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Action\.bBurstSlainForMaximumHealth = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::BurstSlainForMaximumHealthAction,", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r"PendingSlainBurstSharePercent = FMath::Max\(PendingSlainBurstSharePercent, Action\.Percent\);",
                     engine)
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"UCataclysmFervour::BurstTheSlain\(this, Notice\.Victim, Notice\.Location, SlainMaximum,\s*"
                     r"Acting->TakePendingSlainBurstSharePercent\(\)\);", player)
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    assert re.search(r"UCataclysmSkillEffects::ExplodeForOverkill\(Burster\.Get\(\), Body\.Get\(\), At, SlainMaximum, "
                     r"SharePercent,\s*UCataclysmSkillEffects::SlainBurstRadiusCm\);", fervour)
    assert re.search(r"Events->AfterThisDeathIsHeard\(MoveTemp\(Burst\)\);", fervour)
    assert re.search(r"UCataclysmSkillEffects::ExplodeForOverkill\(Killer\.Get\(\), SlainActor\.Get\(\), At, "
                     r"Overkill,", player)


def test_passed_on_reads_the_hit_mark_and_takes_the_largest_radius_any_row_gives() -> None:
    """The Death trees' slice C4c1 (2026-11-25), as ruled. The kill handler
    marks its kill's rows as made by a hit or a tick around the event and clears
    the mark after; the conditions fill reads the mark; the event loop gives a
    passive node's overkill row the radius stat and every other row the old
    radius, and keeps the largest; and the kill site hands that radius to the
    explosion. Read from the code with comments taken out."""
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"Acting->SetActingOnAKillByAHit\(!Notice\.bByDamageOverTime\);\s*"
                     r"Acting->ActOnEvent\(FName\(TEXT\(\"kill\"\)\), Notice\.KillingSkillTags, SlainMaximum,\s*"
                     r"\s*true, Notice\.Victim\);\s*"
                     r"Acting->SetActingOnAKillByAHit\(false\);", player)
    assert "const float OverkillRadiusCm = Acting->TakePendingOverkillExplosionRadiusCm();" in player
    assert re.search(r"Overkill,\s*OverkillShare, OverkillRadiusCm\);", player)
    engine = code_without_comments(SOURCE)
    assert "State.bKillingBlowWasAHit = bActingOnAKillByAHit;" in engine
    assert re.search(r"Action\.TriggerKey\.ToString\(\)\.StartsWith\(UCataclysmPassiveTree::PassiveEnchantmentPrefix\)",
                     engine)
    assert re.search(r"PendingOverkillExplosionRadiusCm = FMath::Max\(PendingOverkillExplosionRadiusCm, RowRadiusCm\);",
                     engine)


def test_grim_wake_marks_the_enemies_near_a_kill_and_the_condition_reads_the_mark() -> None:
    """The Death trees' slice C4c2 (2026-11-25), as ruled (Q6). The kill
    handler and a follower's kill under Death's Toll both mark the enemies near
    the body; the marking reads the rows' own reach, leaves the slain out and
    writes the record keyed by the killer; the conditions fill reads that record
    for the asker; and the condition holds within its seconds and never for -1.
    Read from the code with comments taken out."""
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"UCataclysmFervour::NoteOwnKill\(Acting, Notice\.Victim\);\s*"
                     r"UCataclysmFervour::MarkNearAKill\(Acting, Notice\.Victim, Notice\.Location\);", player)
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    assert re.search(r"NoteOwnKill\(AbilitySystem, Notice\.Victim\);\s*"
                     r"MarkNearAKill\(AbilitySystem, Notice\.Victim, Notice\.Location\);", fervour)
    assert re.search(r"WidestReachOfCondition\(ECataclysmStatCondition::TargetNearYourKillWithinSeconds\)", fervour)
    assert re.search(r"FindEnemiesInSphere\(World, KillerActor, At, ReachMetres \* 100\.0f\)", fervour)
    assert re.search(r"if \(Near == Slain\)\s*\{\s*continue;", fervour)
    assert "Marked->NoteNearKillBy(Killer);" in fervour
    engine = code_without_comments(SOURCE)
    assert "State.SecondsSinceNearYourKill = Marked->SecondsSinceNearKillBy(State.AskingAbilitySystem);" in engine
    assert re.search(r"Row\.Condition == Condition \|\| Row\.Condition2 == Condition", engine)
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::TargetNearYourKillWithinSeconds:\s*"
                     r"return State\.SecondsSinceNearYourKill >= 0\.0f && State\.SecondsSinceNearYourKill <= Value;",
                     pipeline)


def test_every_target_question_reaches_both_lists_in_with_target_state() -> None:
    """The Death trees' slice C4c2 (2026-11-25). `WithTargetState` declares a
    `bWants...` flag for each question a row may ask about the target, stops
    walking the rows once every flag is set, and returns before any fill when
    none is. A flag missing from the stop-early list can end the walk before
    its row is seen; one missing from the early return makes a stat line whose
    only question it is return before the fill, so its condition refuses. Three
    were missing from both until this slice. THE CONTROL: the declarations are
    found, and there are more than three, so an empty reading cannot pass."""
    engine = code_without_comments(SOURCE)
    start = engine.index("FCataclysmStatConditions UCataclysmAbilitySystemComponent::WithTargetState(")
    body = engine[start:engine.index("\n}\n", start)]
    declared = set(re.findall(r"bool (bWants\w+) = false;", body))
    assert len(declared) >= 10, declared
    stop_early = re.search(r"if \((bWants\w+(?:\s*&&\s*bWants\w+)+)\)\s*\{\s*break;", body)
    early_return = re.search(r"if \(!Target\s*\|\|\s*\((!bWants\w+(?:\s*&&\s*!bWants\w+)+)\)\)\s*\{\s*return State;",
                             body)
    assert stop_early and early_return
    assert set(re.findall(r"bWants\w+", stop_early.group(1))) == declared
    assert set(re.findall(r"bWants\w+", early_return.group(1))) == declared


def test_a_critical_strike_pays_fervour_and_a_minions_does_not() -> None:
    """The Death trees' slice C4d1 (2026-11-25), as ruled (Q2): the Shadow's
    starting node's "2 for each critical strike you land" and Telling Blows.
    The hit handler pays a landed critical strike of the character's own, not
    a minion's, before the `critical_strike` rows act; the payment asks the new
    stat against the character struck and goes through `RaisePool`. Read from
    the code with comments taken out."""
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"if \(Notice\.bCritical\)\s*\{\s*if \(!Cast<ACataclysmMinion>\(Notice\.DealtBy\)\)\s*\{\s*"
                     r"UCataclysmFervour::GainOnCriticalStrike\(Acting, Notice\.Target\);\s*\}\s*"
                     r"Acting->ActOnEvent\(FName\(TEXT\(\"critical_strike\"\)\)", player)
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    start = fervour.index("float UCataclysmFervour::GainOnCriticalStrike(")
    body = fervour[start:fervour.index("\n}\n", start)]
    assert re.search(r"FName\(PerCriticalStrikeStat\), FGameplayTagContainer\(\), 0\.0f, -1\.0f, "
                     r"FCataclysmBlowContext\(\), -1\.0f, -1\.0f, false,\s*Struck\);", body)
    assert "RaisePool(AbilitySystem, Change);" in body
    assert "ClassResourceGainScaled(AbilitySystem, PerCritical)" in body
    assert "BeginKillPayment" not in body


def test_reapers_edge_marks_an_executes_kill_for_its_payment_and_rows() -> None:
    """The Death trees' slice C4d1 (2026-11-25), as ruled (Q3). The struck
    character is marked as being executed only around `ExecuteIfLeftBelow`'s
    kill, not around a cull's; the death notice copies the mark; the kill
    handler marks its payment and rows from the notice and clears the mark
    after; the conditions fill reads it; and the condition holds on it alone.
    Read from the code with comments taken out."""
    engine = code_without_comments(SOURCE)
    assert re.search(r"TGuardValue<bool> Executing\(bBeingExecuted, true\);\s*"
                     r"bKilled = KillIfLeftBelow\(By, Asked\.BelowPercent\);", engine)
    cull = engine[engine.index("bool UCataclysmAbilitySystemComponent::CullIfLeftBelow("):]
    assert "bBeingExecuted" not in cull[:cull.index("\n}\n")]
    assert "State.bKillWasAnExecute = bActingOnAKillByAnExecute;" in engine
    events = code_without_comments(SOURCE.parent / "CataclysmCombatEvents.cpp")
    assert re.search(r"if \(System\)\s*\{\s*Notice\.bByAnExecute = System->IsBeingExecuted\(\);", events)
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"Acting->SetActingOnAKillByAnExecute\(Notice\.bByAnExecute\);\s*"
                     r"UCataclysmFervour::GiveBackDecayOnKill\(Acting\);\s*"
                     r"UCataclysmFervour::GainOnOwnKill\(Acting, Notice\.Victim\);", player)
    assert re.search(r"Acting->SetActingOnAKillByAHit\(false\);\s*\}\s*"
                     r"Acting->SetActingOnAKillByAnExecute\(false\);", player)
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::KillWasAnExecute:\s*return State\.bKillWasAnExecute;", pipeline)


def test_relentless_is_sized_by_the_whole_hundreds_of_fervour_held() -> None:
    """The Death trees' slice C4d1 (2026-11-25), as ruled (Q4): whole hundreds,
    read when the row acts. The item maps the generator's Fraction Of to the new
    base; the event loop multiplies a row on that base by the whole hundreds of
    Fervour counted as held, as it does the two event-amount bases; and the
    generator keeps the base off the pool rows' list. Read from the code with
    comments taken out."""
    assert gen.FRACTION_OF_HUNDREDS_HELD == "for_each_hundred_class_resource_held"
    assert gen.FRACTION_OF_HUNDREDS_HELD not in gen.FRACTION_BASES
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Effect->FractionOf\.Equals\(TEXT\(\"for_each_hundred_class_resource_held\"\),\s*"
                     r"ESearchCase::IgnoreCase\)\)\s*\{\s*"
                     r"Action\.Base = ECataclysmPoolActionBase::ForEachHundredClassResourceHeld;", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r"const float Times = Listed\.Base == ECataclysmPoolActionBase::ForEachHundredClassResourceHeld\s*"
                     r"\? TimesSizedByFervourHeld\(\)\s*: TimesSizedByEventAmount\(Listed\.Base, EventAmount\);", engine)
    assert re.search(r"\|\| Listed\.Base == ECataclysmPoolActionBase::ForEachHundredClassResourceHeld\)\s*\{\s*"
                     r"Sized\.Emplace\(Listed\);", engine)
    assert re.search(r"const float Held = UCataclysmStatPipeline::FervourHeldCounted\(CurrentConditions\(\)\);\s*"
                     r"return FMath::Max\(0\.0f, FMath::FloorToFloat\(Held / 100\.0f \+ Allowance\)\);", engine)


def test_chain_execution_makes_the_next_landed_hit_critical_and_is_used_by_it() -> None:
    """The Death trees' slice C4d1 (2026-11-25), as ruled (Q5). The generator's
    name is the engine's, on `kill` only; the item recognises the row; the event
    loop sets the charge to the full window, once per row per event; the
    defender's attribute set makes a hit certain to be critical while the
    attacker holds the charge and uses it on the hit it made critical; and the
    resolver honours the certainty after evasion, so an evaded hit is never
    critical and uses nothing. Read from the code with comments taken out."""
    names = engine_names()
    assert gen.NEXT_HIT_CRITICAL_ACTION == names["CriticalNextHitAction"]
    # WIDENED IN THE DEATH TREES' SLICE C6a (2026-11-25) to Slip's `dodge` and
    # Brim's `resource_full`.
    assert gen.NEXT_HIT_CRITICAL_EVENTS == ("kill", "dodge", "resource_full")
    assert gen.NEXT_HIT_CRITICAL_ACTION not in gen.POOL_ACTIONS
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Action\.bNextHitCritical = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::CriticalNextHitAction,", item)
    engine = code_without_comments(SOURCE)
    # NOUGHT SECONDS IS "UNTIL USED", Brim's, since the Death trees' slice C6a.
    assert re.search(r"NextHitCriticalUntilSeconds = Action\.Percent > 0\.0f \? World->GetTimeSeconds\(\) \+ "
                     r"Action\.Percent\s*: TNumericLimits<float>::Max\(\);", engine)
    vital = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    assert "Hit.bCertainCritical = Asking && Asking->HoldsNextHitCritical();" in vital
    assert re.search(r"if \(Hit\.bCertainCritical && Resolved\.bWasCritical\)\s*\{[^}]*"
                     r"Spending->SpendNextHitCritical\(\);", vital)
    calc = code_without_comments(SOURCE.parent / "CataclysmDamageCalculation.cpp")
    certain = calc.index("if (Hit.bCertainCritical || Roll < Hit.CritChance)")
    assert calc.index("bEvaded = true") < certain


def test_patient_blade_pays_a_kill_for_a_boss_tick_once_every_its_seconds() -> None:
    """The Death trees' slice C4d2 (2026-11-25), as ruled (Q6): Fervour only,
    one clock per character. The hit handler pays a tick of the character's own
    damage over time, not a minion's, before ticks are dropped; the payment asks
    the stat against the creature ticked, refuses inside the seconds, notes the
    clock and pays one own kill's Fervour, and raises no kill. Read from the
    code with comments taken out."""
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"if \(Notice\.bDamageOverTime && !Notice\.bEvaded && !Cast<ACataclysmMinion>\(Notice\.DealtBy\)\)\s*"
                     r"\{\s*UCataclysmFervour::PayForABossTick\(GetAbilitySystemComponent\(\), Notice\.Target\);\s*\}\s*"
                     r"if \(Notice\.bDamageOverTime \|\| Notice\.bEvaded\)", player)
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    start = fervour.index("float UCataclysmFervour::PayForABossTick(")
    body = fervour[start:fervour.index("\n}\n", start)]
    assert re.search(r"FName\(BossTicksPayAKillEverySecondsStat\),[^;]*false, Ticked\);", body)
    assert re.search(r"if \(Since >= 0\.0f && Since \+ 1\.0e-3f < Every\)\s*\{\s*return 0\.0f;", body)
    assert re.search(r"Cataclysm->NoteBossTickPaid\(\);\s*return GainOnOwnKill\(AbilitySystem, Ticked\);", body)
    assert "ActOnEvent" not in body and "NoteOwnKill" not in body


def test_death_by_inches_credits_every_nth_hit_on_a_boss_as_a_kill_without_a_death() -> None:
    """The Death trees' slice C4d2 (2026-11-25), as ruled (Q7). The hit handler
    counts each landed hit of the character's own, after the hit's rows; the
    count is kept per creature; at every Nth a kill is credited without a
    death, in the kill handler's order, with `kill` raised against the Boss;
    and the shares that need a body are taken and dropped. Read from the code
    with comments taken out."""
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"Acting->ActOnEvent\(FName\(TEXT\(\"critical_strike\"\)\)[^;]*;\s*\}\s*"
                     r"if \(!Cast<ACataclysmMinion>\(Notice\.DealtBy\)\)\s*\{\s*"
                     r"UCataclysmFervour::CountAHitOnABoss\(Acting, Notice\.Target\);", player)
    engine = code_without_comments(SOURCE)
    assert "return ++BossHitsCounted.FindOrAdd(Boss);" in engine
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    count = fervour[fervour.index("bool UCataclysmFervour::CountAHitOnABoss("):]
    count = count[:count.index("\n}\n")]
    assert "UCataclysmSkillEffects::IsDead(Struck)" in count
    assert re.search(r"if \(Cataclysm->CountBossHit\(Struck\) % Nth != 0\)", count)
    credit = fervour[fervour.index("void UCataclysmFervour::CreditAKillWithoutADeath("):]
    credit = credit[:credit.index("\n}\n")]
    assert re.search(r"GiveBackDecayOnKill\(AbilitySystem\);\s*GainOnOwnKill\(AbilitySystem, Slain\);\s*"
                     r"NoteOwnKill\(AbilitySystem, Slain\);\s*MarkNearAKill\(AbilitySystem, Slain,", credit)
    assert re.search(r"Acting->ActOnEvent\(FName\(TEXT\(\"kill\"\)\), nullptr, SlainMaximum, true, Slain\);", credit)
    for taken in ("TakePendingOverkillExplosionSharePercent", "TakePendingOverkillExplosionRadiusCm",
                  "TakePendingSlainBurstSharePercent", "TakePendingKillingBlowExplosionSharePercent"):
        assert f"Acting->{taken}();" in credit, taken
    for left_out in ("MarkDead", "ExplodeForOverkill", "BurstTheSlain", "AnnounceAfflictedDeath", "NoteDeath"):
        assert left_out not in credit, left_out


def test_the_worn_enemy_modifiers_are_one_list_in_the_generator_and_the_engine() -> None:
    """The Death trees' slice C4d3 (2026-11-25), as ruled (W6). A row's
    `wearing_enemy_modifier` value is a place in a list the generator and the
    engine each hold, so the two must be the same list in the same order; the
    condition's bounds are that list's ends; every place has its row on Wearer
    of the Dead; and every name is a row of the enemy modifier table. THE
    CONTROL: the engine list is found and has five names."""
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    start = fervour.index("const TArray<FName>& UCataclysmFervour::WornEnemyModifierRows()")
    body = fervour[start:fervour.index("\n}\n", start)]
    engine = tuple(re.findall(r'FName\(TEXT\("([A-Za-z_]+)"\)\)', body))
    assert len(engine) == 5, engine
    assert engine == gen.WORN_ENEMY_MODIFIERS
    assert gen.CONDITIONS["wearing_enemy_modifier"][:2] == (1.0, float(len(gen.WORN_ENEMY_MODIFIERS)))
    effects = list(csv.DictReader((ROOT / "game" / "Data" / "PassiveEffects.csv").open(encoding="utf-8")))
    places = sorted(int(float(r["ConditionValue"])) for r in effects
                    if r["Condition"] == "wearing_enemy_modifier")
    assert places == list(range(1, len(gen.WORN_ENEMY_MODIFIERS) + 1)), places
    modifiers = {r["Name"] for r in csv.DictReader((ROOT / "game" / "Data" / "EnemyModifiers.csv").open(encoding="utf-8"))}
    assert set(gen.WORN_ENEMY_MODIFIERS) <= modifiers


def test_wearer_of_the_dead_records_an_elite_or_strongers_modifiers_at_every_kind_of_kill() -> None:
    """The Death trees' slice C4d3 (2026-11-25), as ruled (W3 to W6). The kill
    handler, a follower's kill under Death's Toll and a kill without a death
    each call the wearing; it reads the slain creature's rung of 1 or more,
    records only a place the character holds a row for, refreshes maximum
    health, and the conditions fill reads the record. A second record of the
    same place sets its time again rather than adding. Read from the code with
    comments taken out."""
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"UCataclysmFervour::MarkNearAKill\(Acting, Notice\.Victim, Notice\.Location\);\s*"
                     r"UCataclysmFervour::WearTheModifiersOf\(Acting, Notice\.Victim\);", player)
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    assert re.search(r"MarkNearAKill\(AbilitySystem, Notice\.Victim, Notice\.Location\);\s*"
                     r"WearTheModifiersOf\(AbilitySystem, Notice\.Victim\);", fervour)
    assert re.search(r"MarkNearAKill\(AbilitySystem, Slain, Slain->GetActorLocation\(\)\);\s*"
                     r"WearTheModifiersOf\(AbilitySystem, Slain\);", fervour)
    wear = fervour[fervour.index("int32 UCataclysmFervour::WearTheModifiersOf("):]
    wear = wear[:wear.index("\n}\n")]
    assert "Creature->RarityStep < 1" in wear
    assert re.search(r"if \(Place > 0 && Wearer->HoldsARowWearing\(Place\)\)", wear)
    assert "Wearer->NoteWornEnemyModifier(Place, WornEnemyModifierSeconds);" in wear
    assert "Wearer->RefreshLiveMaximumHealth();" in wear
    engine = code_without_comments(SOURCE)
    assert "State.WornEnemyModifierPlaces = WornEnemyModifierPlacesNow();" in engine
    assert "WornEnemyModifierUntil.Add(Place, World->GetTimeSeconds() + Seconds);" in engine
    header = code_without_comments(SOURCE.parent / "CataclysmFervour.h")
    assert "static constexpr float WornEnemyModifierSeconds = 20.0f;" in header


def test_a_stepped_action_row_is_keyed_apart_in_the_engine_and_the_generator() -> None:
    """The Death trees' slice C6a (2026-11-25). "1 second for every 4 points" is
    an action row at 4 points and another at 8. An action row's trigger, reset
    and charge keys begin with its enchantment name, so the passive tree names a
    stepped action row apart by its threshold, and the generator's one-row-per-
    action check keys it the same way. A stat row is not renamed. Read from the
    code with comments taken out."""
    tree = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPassiveTree.cpp")
    assert re.search(r"if \(!Effect\.Action\.IsEmpty\(\) && Effect\.MinPoints > 0\)\s*\{\s*"
                     r"Row\.Enchantment \+= FString::Printf\(TEXT\(\"_From%d\"\), Effect\.MinPoints\);", tree)
    generator = (ROOT / "tools" / "generate_datatables.py").read_text(encoding="utf-8")
    assert re.search(r'key = \(row\["Node"\], row\["Option"\], row\["Action"\], row\["ActionEvent"\],\s*'
                     r'row\["MinPoints"\]\)', generator)


def test_the_movement_cut_the_longer_hold_and_brims_condition_are_wired() -> None:
    """The Death trees' slice C6a (2026-11-25). The generator's two new action
    names are the engine's; the item maps the movement cut to its kind and the
    hold to its flag; the cooldown cut takes the movement skill's cooldown tags;
    the event loop lengthens a running hold, never past its full length; and
    Brim's condition reads the next-hit charge. Read from the code with comments
    taken out."""
    names = engine_names()
    assert "cooldown_reduce_movement" in gen.COOLDOWN_REDUCE_ACTIONS
    assert names["CooldownReduceMovementAction"] == "cooldown_reduce_movement"
    assert gen.FERVOUR_HOLD_EXTENDED_ACTION == names["FervourHoldExtendedAction"]
    assert gen.FERVOUR_HOLD_EXTENDED_EVENTS == ("dodge",)
    assert gen.FERVOUR_HOLD_EXTENDED_ACTION not in gen.POOL_ACTIONS
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"UCataclysmAbilitySystemComponent::CooldownReduceMovementAction,\s*ESearchCase::IgnoreCase\)\)\s*"
                     r"\{\s*Action\.CooldownReduce = ECataclysmCooldownReset::Movement;", item)
    assert re.search(r"Action\.bExtendFervourHold = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::FervourHoldExtendedAction,", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r"else if \(Action\.CooldownReduce == ECataclysmCooldownReset::Movement\)\s*\{\s*"
                     r"Reducing\.AppendTags\(CooldownTagsOfSkillsDesignedFor\(ECataclysmAbilitySlot::Movement\)\);", engine)
    assert "ExtendKillDecayHold(Action.Percent, UCataclysmFervour::KillDecayHoldFullSeconds(this));" in engine
    assert "State.bHoldsNextHitCritical = HoldsNextHitCritical();" in engine
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::NextHitIsCritical:\s*return State\.bHoldsNextHitCritical;", pipeline)


def test_the_four_facts_of_a_hits_target_are_filled_and_judged() -> None:
    """The Death trees' slice C6b1 (2026-11-25). From behind: the target-state
    fill asks the geometry and the striker's mark, and a `RearHits=1` strike
    sets that mark around its hit and clears it after. Full health reads the
    health share already filled for `target_health_below`. Your Poison alone is
    filled beside your Poison or Bleed. Beyond metres reads the distance
    `target_within_metres` reads, strictly. Read from the code with comments
    taken out."""
    engine = code_without_comments(SOURCE)
    assert re.search(r"State\.bTargetStruckFromBehind = \(Asker && Asker->IsStrikingFromBehindByRule\(\)\)\s*"
                     r"\|\| UCataclysmSkillEffects::IsBehind\(State\.AskingAbilitySystem->GetAvatarActor\(\), Target\);",
                     engine)
    assert re.search(r"State\.bTargetCarriesYourPoison =\s*UCataclysmDebuffs::CarriesPoisonLaidBy\(Target,", engine)
    template = code_without_comments(SOURCE.parent / "CataclysmSkillTemplate.cpp")
    assert re.search(r"RearStriker->SetStrikingFromBehindByRule\(true\);\s*\}\s*"
                     r"const float Dealt = UCataclysmSkillEffects::ApplyHit\(\s*"
                     r"Self, Target, PercentThisBlow, SkillTags, Delivery, &Resolved\);\s*"
                     r"if \(RearStriker\)\s*\{\s*RearStriker->SetStrikingFromBehindByRule\(false\);", template)
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::TargetAtFullHealth:\s*"
                     r"return State\.TargetHealthPercent >= 0\.0f && State\.TargetHealthPercent >= 100\.0f - 1\.0e-3f;",
                     pipeline)
    assert re.search(r"case ECataclysmStatCondition::TargetBeyondMetres:\s*"
                     r"return State\.TargetDistanceMetres >= 0\.0f && State\.TargetDistanceMetres > Value;", pipeline)
    for condition in ("target_struck_from_behind", "target_at_full_health", "target_carries_your_poison",
                      "target_beyond_metres"):
        assert condition in gen.CONDITIONS_OF_A_HIT_DEALT, condition


def test_the_movement_skills_end_and_its_struck_enemies_are_recorded_and_judged() -> None:
    """The Death trees' slice C6b2 (2026-11-25). Arrival: a movement skill's
    use that was paid for is marked once `CommitAndBegin` succeeds, and its end
    notes the clock and clears the mark; the state fill reads the clock, and the
    condition is inclusive. Glancing Cuts: `ApplyHit` marks the creature struck,
    keyed by the striker, for a landed blow of a skill made for the Movement slot
    and not for a share, a tick, an evaded or an immune blow; the target-state
    fill reads that record when a row asks. Read from the code with comments
    taken out."""
    moves = code_without_comments(SOURCE.parent / "CataclysmSkillTemplates.cpp")
    assert re.search(r"if \(!CommitAndBegin\(Handle, ActorInfo, ActivationInfo\)\)\s*\{\s*return;\s*\}\s*"
                     r"bPaidForThisUse = true;", moves)
    assert re.search(r"const bool bAPaidMovementSkillUse = bPaidForThisUse "
                     r"&& GetDesignedSlot\(\) == ECataclysmAbilitySlot::Movement;\s*"
                     r"bPaidForThisUse = false;\s*if \(bAPaidMovementSkillUse\)\s*\{\s*"
                     r"if \(UCataclysmAbilitySystemComponent\* Mover =\s*"
                     r"Cast<UCataclysmAbilitySystemComponent>\(GetAbilitySystemComponentFromActorInfo\(\)\)\)\s*"
                     r"\{\s*Mover->NoteMovementSkillEnded\(\);", moves)
    engine = code_without_comments(SOURCE)
    assert "State.SecondsSinceMovementSkillEnded = SecondsSinceMovementSkillEnded();" in engine
    assert re.search(r"State\.SecondsSinceStruckByYourMovementSkill =\s*"
                     r"Struck->SecondsSinceStruckByMovementSkillOf\(State\.AskingAbilitySystem\);", engine)
    effects = code_without_comments(SOURCE.parent / "CataclysmSkillEffects.cpp")
    assert re.search(r"if \(!bAShareOnAnAlly && !Arrived\.bIsDamageOverTime && !Landed->bEvaded "
                     r"&& !Landed->bRefusedByImmunityToAttacker\s*"
                     r"&& DeliveredBy && DeliveredBy->GetDesignedSlot\(\) == ECataclysmAbilitySlot::Movement\)\s*"
                     r"\{\s*if \(UCataclysmAbilitySystemComponent\* Struck =\s*"
                     r"Cast<UCataclysmAbilitySystemComponent>\(UCataclysmTargeting::AbilitySystemOf\(Target\)\)\)\s*"
                     r"\{\s*Struck->NoteStruckByMovementSkillOf\(Source\);", effects)
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::WithinSecondsOfMovementSkillEnd:\s*"
                     r"return State\.SecondsSinceMovementSkillEnded >= 0\.0f "
                     r"&& State\.SecondsSinceMovementSkillEnded <= Value;", pipeline)
    assert re.search(r"case ECataclysmStatCondition::TargetStruckByYourMovementSkillWithinSeconds:\s*"
                     r"return State\.SecondsSinceStruckByYourMovementSkill >= 0\.0f\s*"
                     r"&& State\.SecondsSinceStruckByYourMovementSkill <= Value;", pipeline)
    assert "target_struck_by_your_movement_skill_within_seconds" in gen.CONDITIONS_OF_A_HIT_DEALT
    assert "movement_skill_ends" in gen.action_events()


def test_a_flat_amount_and_a_chance_of_class_resource_are_paid_as_every_gain_is() -> None:
    """The Death trees' slice C6b3 (2026-11-25), as ruled. A `points` row pays its value itself, on the class
    resource only. EVERY CLASS RESOURCE GAIN a pool action makes, the three enchantment rows that already granted it
    included, goes through `UCataclysmFervour::RaisePool` after `ClassResourceGainScaled`, rather than `TopUp`. A row
    with a chance rolls before it pays and before its wait starts, on the timed path as on an event; a chance of 0 or
    100 is not rolled. The passive tree pays a chance per point and leaves the amount alone, and names a second action
    row of one node on one action and event `_Row2`. Read from the code with comments taken out."""
    engine = code_without_comments(SOURCE)
    assert re.search(r"if \(Action\.Base == ECataclysmPoolActionBase::Points\)\s*\{\s*"
                     r"if \(Action\.Pool != ClassResourcePoolName\)\s*\{\s*return;\s*\}\s*Amount = Action\.Percent;",
                     engine)
    assert re.search(r"Amount = ClassResourceGainScaled\(this, Amount\);.*?"
                     r"if \(Amount > 0\.0f && Action\.Pool == ClassResourcePoolName\)\s*\{\s*"
                     r"UCataclysmFervour::RaisePool\(this, Amount\);\s*return;\s*\}\s*if \(Amount > 0\.0f\)\s*\{\s*"
                     r"UCataclysmRegeneration::TopUp", engine, re.S)
    assert re.search(r"if \(ChancePercent <= 0\.0f \|\| ChancePercent >= 100\.0f\)\s*\{\s*return true;\s*\}"
                     r".*?return Roll < ChancePercent;", engine, re.S)
    assert re.search(r"return Action\.Pool != ClassResourcePoolName \|\| ChancePercentComesUp\(Action\.ChancePercent\);",
                     engine)
    assert re.search(r"if \(!PoolActionChanceComesUp\(Action\)\)\s*\{\s*continue;\s*\}\s*"
                     r"ApplyPoolAction\(Action, EventTags, EventAmount\);\s*NoteTriggerFired\(Action\);", engine)
    assert re.search(r"else if \(PoolActionChanceComesUp\(Action\)\)\s*\{\s*ApplyPoolAction\(Action, nullptr, 0\.0f\);",
                     engine)
    item = code_without_comments(SOURCE.parents[1] / "Items/CataclysmItem.cpp")
    assert re.search(r'FractionOf\.Equals\(TEXT\("points"\), ESearchCase::IgnoreCase\)\)\s*\{\s*'
                     r"Action\.Base = ECataclysmPoolActionBase::Points;\s*\}\s*Action\.ChancePercent = Effect->ChancePercent;",
                     item)
    tree = code_without_comments(SOURCE.parents[1] / "Character/CataclysmPassiveTree.cpp")
    assert re.search(r"if \(Effect\.ChancePercent > 0\.0f\)\s*\{\s*Row\.ChancePercent = FMath::Min\(100\.0f, "
                     r"Effect\.ChancePercent \* static_cast<float>\(Paid\)\);\s*Row\.ValueLow = Effect\.ValuePerPoint;",
                     tree)
    assert re.search(r'if \(Seen > 1\)\s*\{\s*Row\.Enchantment \+= FString::Printf\(TEXT\("_Row%d"\), Seen\);', tree)
    assert "points" in gen.FRACTION_BASES


def test_the_attackers_rung_and_the_enemies_near_are_filled_and_judged() -> None:
    """The Death trees' slice C6b3 (2026-11-25). Read the Blow: the creature that threw a blow puts its rung on the
    hit, and the blow context carries it. Vigil Over the Strong and Reaper's Due: the walk keeps each hostile's rung
    beside its distance. Hunting Ground: the distances walk says it was made, and "no enemy in reach" holds only then.
    Read from the code with comments taken out."""
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    assert re.search(r"Hit\.bFromBoss = Striker->IsBoss\(\);\s*Hit\.AttackerRarityStep = Striker->RarityStep;", vitals)
    calc = code_without_comments(SOURCE.parent / "CataclysmDamageCalculation.cpp")
    assert "Blow.OpponentRarityStep = Hit.AttackerRarityStep;" in calc
    engine = code_without_comments(SOURCE)
    assert re.search(r"State\.HostileDistancesMetres\);\s*State\.bHostileDistancesRead = true;", engine)
    assert re.search(r"State\.HostileRarityRungs\.Add\(Creature \? Creature->RarityStep : -1\);", engine)
    assert re.search(r"if \(Asked == ECataclysmStatCondition::NoEnemyInReach\)\s*\{\s*"
                     r"Widest = FMath::Max\(Widest, Modifier\.ReachMetres\);", engine)
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::OpponentRarityAtLeast:\s*"
                     r"return State\.Blow\.OpponentRarityStep >= 0 && State\.Blow\.OpponentRarityStep >= "
                     r"FMath::RoundToInt\(Value\);", pipeline)
    assert re.search(r"case ECataclysmStatCondition::NoEnemyInReach:\s*return ReachMetres > 0\.0f && "
                     r"State\.bHostileDistancesRead && EnemiesInReach\(State, ReachMetres\) == 0;", pipeline)
    assert re.search(r"State\.HostileRarityDistancesMetres\[Index\] <= ReachMetres\s*"
                     r"&& State\.HostileRarityRungs\.IsValidIndex\(Index\) && State\.HostileRarityRungs\[Index\] >= Rung",
                     pipeline)


def test_a_doubled_evasion_and_an_unbroken_poison_are_recorded_and_read() -> None:
    """The Death trees' slice C6b4 (2026-11-25), as ruled. Twice Answered: the doubling is rolled before the event's
    rows act, its own row then does nothing, the charge grant takes the larger of what it held and 1 or 2, a use takes
    one, and the charge is held only while it holds one. Festering: a Poison laid on a creature carrying none of the
    applier's restarts the record, the record reads -1 when the creature carries none, and the scale reads whole
    seconds of the lookup's target. Read from the code with comments taken out."""
    engine = code_without_comments(SOURCE)
    assert re.search(r"if \(Listed\.bNextHitCriticalTwice && Listed\.Event == Event\s*&& PoolActionAllowed\(Listed, "
                     r"EventTags, EventTarget\).*?&& ChancePercentComesUp\(Listed\.ChancePercent\)\)\s*\{\s*"
                     r"bCriticalStrikesTwice = true;", engine, re.S)
    assert re.search(r"if \(Action\.bNextHitCriticalTwice\)\s*\{\s*continue;\s*\}\s*if \(Action\.bNextHitCritical\)",
                     engine)
    assert "NextHitCriticalCharges = FMath::Max(NextHitCriticalCharges, bCriticalStrikesTwice ? 2 : 1);" in engine
    assert re.search(r"return World && NextHitCriticalCharges > 0 && NextHitCriticalUntilSeconds >= 0\.0f", engine)
    assert re.search(r"if \(!World \|\| !From \|\| !UCataclysmDebuffs::CarriesPoisonLaidBy\(GetAvatarActor\(\), "
                     r"Applier\)\)\s*\{\s*return -1\.0f;", engine)
    header = code_without_comments(SOURCE.with_suffix(".h"))
    assert re.search(r"NextHitCriticalCharges = FMath::Max\(0, NextHitCriticalCharges - 1\);\s*"
                     r"if \(NextHitCriticalCharges == 0\)\s*\{\s*NextHitCriticalUntilSeconds = -1\.0f;", header)
    effects = code_without_comments(SOURCE.parent / "CataclysmSkillEffects.cpp")
    assert re.search(r"EffectTag\.MatchesTag\(Poison\) && Instigator != Target\s*"
                     r"&& !UCataclysmDebuffs::CarriesPoisonLaidBy\(Target, Instigator\)\)\s*\{\s*"
                     r"if \(UCataclysmAbilitySystemComponent\* Poisoned = Cast<UCataclysmAbilitySystemComponent>"
                     r"\(Defender\)\)\s*\{\s*Poisoned->NotePoisonBeganBy\(Instigator\);\s*\}\s*\}\s*"
                     r"ApplyTypedSpec\(", effects)
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatScale::PerSecondPoisonedByYou:\s*\{\s*const float Seconds =\s*"
                     r"UCataclysmAbilitySystemComponent::SecondsPoisonedByAsker\(State\.AskingAbilitySystem, "
                     r"State\.LookupTarget\);\s*return Seconds > 0\.0f \? StackedValue\(Modifier, "
                     r"FMath::FloorToInt\(Seconds\)\) : 0\.0f;", pipeline)
    assert gen.NEXT_HIT_CRITICAL_TWICE_ACTION == "next_hit_critical_twice"
    assert gen.NEXT_HIT_CRITICAL_TWICE_EVENTS == ("dodge",)


def test_how_fervour_is_lost_and_what_fills_past_the_bar_are_wired() -> None:
    """The Death trees' slice C6c (2026-11-25), as ruled. Miser's Grip: the kill decay stops at a share of the full bar
    after reserves. Soulbound: health lost to damage takes Fervour beside the Masochist's gain from it, through an
    accumulator that keeps the remainder, never below nought and not held by the floor. Overflow: RaisePool measures what
    did not fit and records whole Fervour of it. Unbroken Chain: a passive row's stack granted on a kill lasts the
    chain's extra seconds, and Soul Tide's condition adds them. Read from the code with comments taken out."""
    fervour = code_without_comments(SOURCE.parent / "CataclysmFervour.cpp")
    assert re.search(r"const float Floor = Resource->MaximumClassResourceAsked\(\) \* FloorPercent / 100\.0f;\s*"
                     r"if \(Before <= Floor\)\s*\{\s*return 0\.0f;\s*\}", fervour)
    assert "FMath::Clamp(Before - PerSecond * SecondsInStep, Floor, Resource->MaximumClassResourceAsked())" in fervour
    assert re.search(r"const float Overflow = Change - FMath::Max\(0\.0f, Added\);.*?"
                     r"Overflowing->NoteFervourOverflow\(Overflow, Window\);.*?if \(Added <= 0\.0f\)", fervour, re.S)
    assert re.search(r"Cataclysm->AccumulateHealthLostForFervour\(HealthLost / MaximumHealth \* 100\.0f, PerFervour\);",
                     fervour)
    assert "FMath::Min(static_cast<float>(Whole), FMath::Max(0.0f, Before))" in fervour
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    # SINCE THE MERGE OF development (2026-11-25), the Masochist's gain reads the health AND the energy shield a blow
    # took (#2873); Soulbound's loss reads the health alone, as ruled.
    assert re.search(r"UCataclysmFervour::GainFromDamage\(\s*GetOwningAbilitySystemComponent\(\), "
                     r"HealthLostToTheBlow \+ ShieldLostToTheBlow, AssetTags\);\s*"
                     r"UCataclysmFervour::LoseFromDamage\(GetOwningAbilitySystemComponent\(\), HealthLostToTheBlow\);",
                     vitals)
    engine = code_without_comments(SOURCE)
    assert re.search(r'const float ChainSeconds = Event == FName\(TEXT\("kill"\)\)\s*'
                     r'&& Action\.StackKey\.ToString\(\)\.StartsWith\(TEXT\("Passive_"\)\)\s*\? KillChainSecondsHeld\(\)\s*'
                     r": 0\.0f;\s*GrantOwnStack\(Action\.StackKey, Action\.StackSeconds \+ ChainSeconds, Action\.StackCap\);",
                     engine)
    assert re.search(r"HealthPercentLostTowardsFervour = FMath::Max\(0\.0f, HealthPercentLostTowardsFervour - Whole \* "
                     r"PercentPerFervour\);", engine)
    assert "State.KillChainSeconds = KillChainSecondsHeld();" in engine
    pipeline = code_without_comments(SOURCE.parent / "CataclysmStatPipeline.cpp")
    assert re.search(r"case ECataclysmStatCondition::KillChainWithinSeconds:\s*return State\.SecondsSinceOwnKill >= 0\.0f\s*"
                     r"&& State\.SecondsSinceOwnKill <= Value \+ FMath::Max\(0\.0f, State\.KillChainSeconds\);", pipeline)
    assert not any(name.startswith(gen.ACTION_EVENT_PREFIX) for name in ("kill_chain_within_seconds",))


def test_blood_debt_passes_the_bleed_left_to_the_strongest_enemy_near_the_body() -> None:
    """The Death trees' slice C4c2 (2026-11-25), as ruled (Q7). The generator's
    name is the engine's, on `afflicted_death` only; the item recognises the
    row by its constant; the event loop passes the Bleed left once per row per
    event, landed only; and the pass lays a Bleed of the applier's on the
    strongest enemy within 10 metres, the whole left over the row's seconds,
    unscaled. Read from the code with comments taken out."""
    names = engine_names()
    assert gen.BLEED_LEFT_TO_STRONGEST_ACTION == names["PassBleedLeftToStrongestAction"]
    assert gen.BLEED_LEFT_TO_STRONGEST_EVENTS == ("afflicted_death",)
    assert gen.BLEED_LEFT_TO_STRONGEST_ACTION not in gen.POOL_ACTIONS
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"Action\.bPassBleedLeftToStrongest = Effect->Action\.Equals\(\s*"
                     r"UCataclysmAbilitySystemComponent::PassBleedLeftToStrongestAction,", item)
    engine = code_without_comments(SOURCE)
    assert re.search(r"if \(bLanded && Dead && Self && Action\.Percent > 0\.0f && "
                     r"!StackedThisEvent\.Contains\(Action\.TriggerKey\)\)", engine)
    assert "UCataclysmDebuffs::PassBleedLeftToStrongest(Self, Dead, Action.Percent);" in engine
    debuffs = code_without_comments(SOURCE.parent / "CataclysmDebuffs.cpp")
    assert "StrongestEnemyNear(Dead, Applier, BloodDebtReachCm)" in debuffs
    assert re.search(r"ApplyDamageOverTime\(Applier, Strongest, Left / Seconds, Seconds, BleedTag\(\),\s*"
                     r"false\)", debuffs)
    assert re.search(r"if \(Rung > BestRung \|\| \(Rung == BestRung && Health > BestHealth\)\)", debuffs)
    header = code_without_comments(SOURCE.parent / "CataclysmDebuffs.h")
    assert "static constexpr float BloodDebtReachCm = 1000.0f;" in header


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
    # AND NOT WHILE AN ABILITY IS ACTIVATING (traps layer 6b, #2471): a grant under the engine's ability-list lock is
    # deferred and refused, and the shot would be lost.
    assert re.search(r"if \(PoolActionDepth > 0 \|\| bDrainingQueuedGadgetShotsAtTrapEnemies "
                     r"\|\| AbilityScopeLockCount > 0\)", engine)
    minion = code_without_comments(SOURCE.parent / "CataclysmMinion.cpp")
    assert "CastPlayersSkillFromHere(false, Enemy, ShotDamageMultiplier)" in minion
    assert re.search(r"StartPlayersSkillFromHere\(Player, AsTheGadgets, SkillGadgetReachCm\(Skill\), AimedAt, nullptr,\s*"
                     r"ShotDamageMultiplier\)", minion)
    trigger = minion[minion.index("bool ACataclysmMinion::TriggerSkillTrap("):]
    assert trigger.index("PullEnemiesToThisSkillTrap(Player);") \
        < trigger.index("StartPlayersSkillFromHere(Player, AsTheTraps,")


def test_a_used_up_traps_shot_landing_and_held_back_swing_look_for_its_wearer_by_its_identity() -> None:
    """Ruled 2026-10-22 (traps layer 7, K1, and L-1 for the swing). A shot, a
    landing or a held-back swing of a skill trap already used up still strikes
    its wearer standing inside its blast: the explosion where a shot stops,
    the projectile skill's landing and the Strike's sphere and cone each hand
    the identity of what cast the use as well as `Get` of it, `WearerInCone`
    hands it on to `WearerInSphere`, `WearerInSphere` asks for a used-up trap
    only when no standing deployable answered, and `IsTheWearersUsedUpTrap`
    finds the identity among the wearer's used-up traps. Read from the code
    with comments taken out by tracking `/* ... */` and `//` (R-c). THE
    CONTROL: the same reading finds `ACataclysmMinion::Explode`'s sphere, a
    dying deployable's own blast, handing no identity, so a pattern that saw
    an identity argument everywhere could not pass."""
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
                     r"FMath::Max\(ThisUseHitsAllWithinCm, ScaledRadiusCm\(\)\),\s*FreeRepeatCastBy\)", templates)
    assert re.search(r"WearerInCone\(\s*GetWorld\(\), Self, FreeRepeatCastBy\.Get\(\), Apex, AimDirection\(\), "
                     r"ScaledRadiusCm\(\), ArcDegrees\(\),\s*FreeRepeatCastBy\);", templates)
    assert "WearerInSphere(World, Wearer, CastFrom, Apex, RadiusCm, UsedUpCastBy);" in blast
    minion = code_without_comments(SOURCE.parent / "CataclysmMinion.cpp")
    assert re.search(r"WearerInSphere\(\s*GetWorld\(\), Summoner\.Get\(\), this, GetActorLocation\(\),\s*"
                     r"ExplosionRadiusCm \* UCataclysmSecondSelf::AreaMultiplierFor\(this\)\)\)", minion)


def test_a_minions_and_a_thralls_blow_raise_minion_hit_on_the_commander() -> None:
    """The Death trees' slice C6d1 (2026-11-25), as ruled (P2): Cold Hands and
    Grave Cold act on `minion_hit`, raised ONCE for each blow on the summoner
    with the enemy struck. A summoned minion's swing raises it in
    `ACataclysmMinion::AttackTarget` beside a machine's `deployable_hit`, as the
    branch for every other minion, landed only when not evaded and not refused;
    a thrall's landed blow raises it on its commander in `ApplyHit`'s thrall
    branch. Read from the code with comments taken out."""
    minion = code_without_comments(SOURCE.parent / "CataclysmMinion.cpp")
    assert re.search(r"else if \(OwnDamagePerHit > 0\.0f\)\s*\{\s*if \(UCataclysmAbilitySystemComponent\* Theirs =\s*"
                     r"Cast<UCataclysmAbilitySystemComponent>\(UCataclysmTargeting::AbilitySystemOf\(Summoner\)\)\)\s*"
                     r"\{\s*Theirs->ActOnEvent\(FName\(TEXT\(\"minion_hit\"\)\), &TypeTags, 0\.0f,\s*"
                     r"!Resolved\.bEvaded && !Resolved\.bRefusedByImmunityToAttacker, Target\);",
                     minion)
    assert minion.count('"minion_hit"') == 1
    effects = code_without_comments(SOURCE.parent / "CataclysmSkillEffects.cpp")
    assert re.search(r"UCataclysmTargeting::AbilitySystemOf\(UCataclysmCommand::CommanderOf\(Instigator\)\)\)\)\s*\{\s*"
                     r"Commanders->ActOnEvent\(FName\(TEXT\(\"minion_hit\"\)\), &SkillTags, 0\.0f,\s*true, "
                     r"Target\);", effects)
    assert effects.count('"minion_hit"') == 1


def test_the_minion_stats_of_the_slice_are_read_where_they_act() -> None:
    """The Death trees' slice C6d1 (2026-11-25): Ringed in Bone's
    `immune_to_slow` is read inside `UCataclysmSkillTemplate::IsImmuneTo` for
    Slow, the one function every slow check goes through, with the character's
    own state; Grave Pact's `minion_damage_taken` multiplies a commanded
    defender's damage taken; Hasty Dead's `minion_movement_speed` multiplies a
    follower's walk. Read from the code with comments taken out."""
    template = code_without_comments(SOURCE.parent / "CataclysmSkillTemplate.cpp")
    start = template.index("bool UCataclysmSkillTemplate::IsImmuneTo(")
    body = template[start:template.index("\n}\n", start)]
    assert re.search(r"Effect\.Equals\(TEXT\(\"Slow\"\), ESearchCase::IgnoreCase\)\s*&& Cataclysm->StatForSkill\("
                     r"FName\(TEXT\(\"immune_to_slow\"\)\), FGameplayTagContainer\(\), 0\.0f\) > 0\.0f\)\s*\{\s*"
                     r"return true;", body)
    calculation = code_without_comments(SOURCE.parent / "CataclysmDamageCalculation.cpp")
    assert re.search(r"\* MinionDamageTakenFactor\(Defender\);\s*Damage \*= DamageTakenFactor;", calculation)
    assert 'SummonerMultiplierFor(\n\t\t\tCommander, TEXT("minion_damage_taken")' in calculation \
        or re.search(r'SummonerMultiplierFor\(\s*Commander, TEXT\("minion_damage_taken"\)', calculation)
    command = code_without_comments(SOURCE.parent / "CataclysmCommand.cpp")
    assert re.search(r"return Movement->MaxWalkSpeed \* CommandedWalkSpeedShareOfCommander\s*\* SummonerMultiplierFor\("
                     r"Commander, TEXT\(\"minion_movement_speed\"\),", command)


def test_a_row_saying_nothing_may_slow_the_character_is_an_immunity_to_slow() -> None:
    """The Death trees' slice C6d2 (2026-11-25), ruled by the coordinating
    session as a fix to bundle 1's ruling that an immunity to Slow works
    everywhere: `IsImmuneTo` answers true for Slow while
    `movement_speed_reduction_suppressed` stands above nought, asked with its
    conditions judged and the attribute as the answer when no row states it,
    so Unstoppable, Nothing Moves You and Relentless drop a War aura's movement
    reduction. And the movement refresh still reads the stat as it did. Read
    from the code with comments taken out."""
    template = code_without_comments(SOURCE.parent / "CataclysmSkillTemplate.cpp")
    start = template.index("bool UCataclysmSkillTemplate::IsImmuneTo(")
    body = template[start:template.index("\n}\n", start)]
    assert re.search(r"UCataclysmCombatAttributeSet::GetMovementSpeedReductionSuppressedAttribute\(\);\s*"
                     r"const float Held = Cataclysm->HasAttributeSetForAttribute\(Suppressed\)\s*"
                     r"\? Cataclysm->GetNumericAttribute\(Suppressed\)\s*: 0\.0f;\s*"
                     r"if \(Cataclysm->StatForSkill\(FName\(TEXT\(\"movement_speed_reduction_suppressed\"\)\), "
                     r"FGameplayTagContainer\(\),\s*Held\) > 0\.0f\)\s*\{\s*return true;", body)
    player = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmPlayerCharacter.cpp")
    assert re.search(r"const float Suppressed = Cataclysm->StatForSkill\(\s*FName\(MovementSpeedReductionSuppressedStat\)",
                     player)
    assert "if (Suppressed <= 0.0f && !bImmuneToSlow)" in player


def test_a_followers_death_is_told_to_its_commander_and_eternal_army_comes_first() -> None:
    """The Death trees' slice C6d2 (2026-11-25), as ruled (P1): where a
    follower's health reaches nought, its commander is told `minion_death`
    beside the Fervour for losing it, for a minion, an imp or a thrall and not a
    gadget or a trap; `NoteMinionDied` records when and how far and raises the
    event with the body. Eternal Army's refusal comes BEFORE all of it, so a
    follower kept at 1 health raises nothing. Read from the code with comments
    taken out."""
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    start = vitals.index("void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero(")
    body = vitals[start:vitals.index("\n}\n", start)]
    assert re.search(r"if \(AbovePercent > 0\.0f && TheirMaximum > 0\.0f && TheirHealth / TheirMaximum \* 100\.0f > "
                     r"AbovePercent\)\s*\{\s*const_cast<UAbilitySystemComponent\*>\(AbilitySystem\)->"
                     r"SetNumericAttributeBase\(GetHealthAttribute\(\), 1\.0f\);\s*return;", body)
    assert body.index("MinionCannotDieAboveHealthPercentStat") < body.index("UCataclysmFervour::GainOnMinionDeath(")
    assert re.search(r"if \(!AsMinion \|\| !AsMinion->IsDeployable\(\)\)\s*\{\s*if \(UCataclysmAbilitySystemComponent\* "
                     r"Commanders =\s*Cast<UCataclysmAbilitySystemComponent>\(UCataclysmTargeting::AbilitySystemOf\("
                     r"Commander\)\)\)\s*\{\s*Commanders->NoteMinionDied\(Character, AsMinion \? AsMinion->TypeTags : "
                     r"UCataclysmCommand::ThrallMinionTags\(\)\);", body)
    assert body.index("NoteMinionDied(") < body.index("Character->HandleDeath();")
    engine = code_without_comments(SOURCE)
    start = engine.index("void UCataclysmAbilitySystemComponent::NoteMinionDied(")
    died = engine[start:engine.index("\n}\n", start)]
    assert re.search(r'ActOnEvent\(FName\(TEXT\("minion_death"\)\), &FollowerTags, 0\.0f,\s*true, Body\);', died)
    assert "Death.Metres = (Self && Body) ? UCataclysmTargeting::MetresBetween(Self, Body) : -1.0f;" in died


def test_avenging_deads_condition_is_judged_at_the_grant_and_hit_taken_carries_the_share() -> None:
    """The Death trees' slice C6d2 (2026-11-25), as ruled (Q2). `NoteHitTaken`
    holds the share of maximum health the hit took while it raises `hit_taken`,
    which carries the same share as its amount (nought when unknown); an
    own-stack row's event condition goes on its grant and comes off its stat.
    Read from the code with comments taken out."""
    engine = code_without_comments(SOURCE)
    assert re.search(r"TGuardValue<float> SaysHowMuchItTook\(HitBeingAnnouncedPercentOfMaxHealth, "
                     r"PercentOfMaxHealthLost\);\s*ActOnEvent\(FName\(TEXT\(\"hit_taken\"\)\), nullptr, "
                     r"FMath::Max\(0\.0f, PercentOfMaxHealthLost\), bLanded\);", engine)
    assert "State.HitTakenPercentOfMaxHealth = HitBeingAnnouncedPercentOfMaxHealth;" in engine
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert re.search(r"if \(UCataclysmStatPipeline::IsJudgedAtItsEvent\(Out\.Condition\)\)\s*\{\s*Out\.Condition = "
                     r"ECataclysmStatCondition::Always;\s*Out\.ConditionValue = 0\.0f;", item)
    assert re.search(r"&& UCataclysmStatPipeline::IsJudgedAtItsEvent\(AtTheEvent\)\)\s*\{\s*Stack\.Condition = "
                     r"AtTheEvent;\s*Stack\.ConditionValue = Effect->ConditionValue;", item)
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    assert "MaximumHealthNow > 0.0f ? HealthLostToTheBlow / MaximumHealthNow * 100.0f : -1.0f);" in vitals


def test_marshal_shared_leech_and_bound_bones_are_read_where_they_act() -> None:
    """The Death trees' slice C6d2 (2026-11-25). Marshal: a minion's blow and
    explosion and a thrall's blow and explosion name the minion to the
    summoner's lookup, which fills its distance. Shared Leech: beside the leech
    a blow pays its dealer, the same figure to the dealer's commander. Bound
    Bones: every regeneration step, after Shared Blood's. Read from the code
    with comments taken out."""
    minion = code_without_comments(SOURCE.parent / "CataclysmMinion.cpp")
    assert re.search(r'SummonerMultiplierAgainst\(Summoner, TEXT\("minion_damage"\), Target, TypeTags,\s*this\)', minion)
    assert re.search(r'SummonerMultiplierFor\(Summoner, TEXT\("minion_damage"\), TypeTags, this\)', minion)
    assert re.search(r'SummonerMultiplierFor\(Commander, TEXT\("minion_damage"\), '
                     r'UCataclysmCommand::ThrallMinionTags\(\),\s*Lost\)', minion)
    command = code_without_comments(SOURCE.parent / "CataclysmCommand.cpp")
    assert re.search(r'SummonerMultiplierAgainst\(Commander, TEXT\("minion_damage"\), Target, ThrallMinionTags\(\), '
                     r'Striker\)', command)
    engine = code_without_comments(SOURCE)
    assert re.search(r"State\.AskingMinionMetres = \(AskingMinion && Self\) \? "
                     r"UCataclysmTargeting::MetresBetween\(Self, AskingMinion\) : -1\.0f;", engine)
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    assert re.search(r"UCataclysmLeech::NoteHit\([^;]*;\s*UCataclysmLeech::NoteMinionHit\(", vitals)
    regeneration = code_without_comments(SOURCE.parent / "CataclysmRegeneration.cpp")
    assert re.search(r"SharedBloodStep\(Character, SecondsInStep, false\);\s*BoundBonesStep\(Character\);", regeneration)


def test_the_shadows_marks_are_kept_moved_and_asked() -> None:
    """The Death trees' slice C6e (2026-11-25), as ruled (Q5). The marks are
    kept up each regeneration step, moved off a dying creature where its death
    begins and before its own `HandleDeath`, and asked of the marker by
    `WithTargetState`. Read from the code with comments taken out."""
    regeneration = code_without_comments(SOURCE.parent / "CataclysmRegeneration.cpp")
    assert re.search(r"BoundBonesStep\(Character\);\s*if \(UCataclysmAbilitySystemComponent\* Marker = "
                     r"Cast<UCataclysmAbilitySystemComponent>\(AbilitySystem\)\)\s*\{\s*Marker->MaintainShadowMarks\(\);",
                     regeneration)
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    start = vitals.index("void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero(")
    body = vitals[start:vitals.index("\n}\n", start)]
    assert body.index("UCataclysmAbilitySystemComponent::MoveShadowMarksOff(Character);") \
        < body.index("Character->HandleDeath();")
    engine = code_without_comments(SOURCE)
    assert re.search(r"State\.bTargetMarkedByYou = Marker && Marker->HoldsShadowMarkOn\(Target\);", engine)
    start = engine.index("void UCataclysmAbilitySystemComponent::MaintainShadowMarks(")
    maintain = engine[start:engine.index("\n}\n", start)]
    assert "UCataclysmDebuffs::StrongestEnemyNear(Self, Self, ShadowMarkReachCm, Marked)" in maintain
    start = engine.index("void UCataclysmAbilitySystemComponent::MoveShadowMarkFrom(")
    move = engine[start:engine.index("\n}\n", start)]
    assert "UCataclysmDebuffs::StrongestEnemyNear(Dying, Self, ShadowMarkMoveCm, Marked)" in move


def test_hollow_step_passes_through_for_the_move_only_and_hits_the_line_once() -> None:
    """The Death trees' slice C6e (2026-11-25), as ruled (H1, H2). A creature
    on the monsters' side carries the hostile bit on its filter only; an
    instant move passes through it, is restored, and steps out of any body it
    ended in; `EndAbility` restores and steps out on every way out; and the line
    is hit once, skipping the skill's own targets. Read from the code with
    comments taken out."""
    base = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmCharacterBase.cpp")
    assert "Capsule->SetMaskFilterOnBodyInstance(bHostile ? HostileBodyMaskBit : OwnBit);" in base
    assert "Capsule->SetMoveIgnoreMask(MovesThroughMaskFor(OwnBit));" in base
    templates = code_without_comments(SOURCE.parent / "CataclysmSkillTemplates.cpp")
    assert re.search(r"if \(bThroughEnemies\)\s*\{\s*PassThroughEnemies\(true\);\s*\}\s*"
                     r"UCataclysmMovement::MoveWithoutSticking\(Self, End - Self->GetActorLocation\(\)\);\s*"
                     r"ArrivedAt = Self->GetActorLocation\(\);\s*if \(bThroughEnemies\)\s*\{\s*"
                     r"PassThroughEnemies\(false\);\s*StepOutOfEnemies\(Start\);", templates)
    assert re.search(r"if \(bThroughEnemies\)\s*\{\s*HitTheLineOnce\(Start, ArrivedAt, Targets\);", templates)
    start = templates.index("void UCataclysmMovementSkill::EndAbility(")
    end_ability = templates[start:templates.index("\n}\n", start)]
    assert re.search(r"if \(bPassingThrough\)\s*\{\s*PassThroughEnemies\(false\);\s*"
                     r"StepOutOfEnemies\(PassingThroughFrom\);", end_ability)
    assert end_ability.index("PassThroughEnemies(false);") < end_ability.index("Super::EndAbility(")
    start = templates.index("void UCataclysmMovementSkill::HitTheLineOnce(")
    line = templates[start:templates.index("\n}\n", start)]
    assert "if (!AlreadyStruck.Contains(OnTheLine))" in line
    assert "HitWhatThePathCrossed(Crossed, PercentHitScaledGivesAnAlly(TArray<AActor*>()));" in line


def test_the_last_five_shadow_and_necromancer_nodes_are_wired() -> None:
    """The Death trees' slice C6e (2026-11-25), as ruled (Q6, Q8, Q9). The two
    kill actions are converted from their rows and strike the mark or the
    strongest near, Thousand Cuts moving beside it first; a critical strike's
    flag reaches the ailment roll, which lends Lethal Dose's share for that
    application alone; Interpose re-deals the hit to the minion and takes it off
    the defender; and Guard the Master's choice comes after a Quarry order.
    Read from the code with comments taken out."""
    item = code_without_comments(SOURCE.parents[1] / "Items" / "CataclysmItem.cpp")
    assert "Action.bStrikeStrongestNear = Effect->Action.Equals(" in item
    assert "Action.bDashToStrongestAndStrike = Effect->Action.Equals(" in item
    engine = code_without_comments(SOURCE)
    assert re.search(r"if \(Action\.bDashToStrongestAndStrike\)\s*\{\s*UCataclysmMovement::PlaceBeside\(Self, Struck\);"
                     r"\s*\}", engine)
    assert "? MarkedOrStrongestNear(StrongestNearReachCm) : nullptr;" in engine
    vitals = code_without_comments(SOURCE.parent / "CataclysmVitalAttributeSet.cpp")
    assert re.search(r"Hit\.bIsBlunt && !bAreaSkillShareOnAnAlly,\s*Outcome\.bWasCritical\);", vitals)
    assert re.search(r"UCataclysmSkillEffects::ApplyDirectDamage\(Interposer, Guard, Hit\.Damage, Taken\);\s*"
                     r"Hit\.Damage = 0\.0f;", vitals)
    ailments = code_without_comments(SOURCE.parent / "CataclysmAilments.cpp")
    assert "TGuardValue<float> LendsIt(LentSlot, Lent);" in ailments
    effects = code_without_comments(SOURCE.parent / "CataclysmSkillEffects.cpp")
    assert re.search(r"Numbers\.DamagePerTick \*= \(1\.0f \+ Increases \+ "
                     r"Lender->AilmentIncreasePercentLentByCritical / 100\.0f\)\s*/ \(1\.0f \+ Increases\);", effects)
    controller = code_without_comments(SOURCE.parents[1] / "Character" / "CataclysmEnemyController.cpp")
    assert controller.index("UCataclysmCommand::OrderedTargetFor(Driven)")         < controller.index("UCataclysmCommand::CommandersLastAttackerFor(Driven)")         < controller.index("Gadget->MostHealthWithinReach()")
