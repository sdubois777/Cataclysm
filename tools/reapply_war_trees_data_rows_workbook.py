"""Write the rows of the 23 War nodes that the War trees plan of 2026-11-27 found writable as data alone.

    python tools/reapply_war_trees_data_rows_workbook.py
    python tools/reapply_war_trees_data_rows_workbook.py --check

THE WAR TREES PLAN, 2026-11-27 (`feat/war-trees-complete`), accepted by the coordinating session the same day: 118 War
nodes had no effect row. This script writes the ones that need only conditions, scales, stats and actions the generator
already knows. The plan counted 29; 23 are here. War Cry (`Berserker_trunk_010`) and Rapid Fire (`Berserker_dw_009`) left the group
when it turned out a Condition on a row scaled by `own_stacks` is judged when the stat is asked, not when the stack is
granted (`refuse_a_condition_nothing_asks_for`), so "after attacking at maximum Fervour" cannot be said yet. They are
built with the full-pool attack fact in engine slice E1. Four Bulwark nodes left it when the generator refused a
row of each: block chance asked with a scale (Resolve to Steel, Living Fortress, Unyielding Guard) and
`retaliation_cannot_be_mitigated` under a condition (Undying Rage) have no asker yet. Each is written whole in E1.

JUDGEMENTS, each recorded in the decisions entry:
  - "+X% damage" is attack and spell damage, the owner's 2026-08-25 rule.
  - "Dual wield only" and "Two-handed weapons only" are `dual_wielding` and `wielding_two_handed_weapon` on every row
    of the node; on a stack row they are judged when the stat is asked, so a stack gained otherwise counts for nothing.
  - Flurry of Blows counts melee attacks made (`melee_attack_use`); Blur counts hits dealt (`hit_dealt`).
  - Battle Scars' "At 8 points: Fervour no longer decays while you are in combat" has no row: the Bulwark's Fervour
    decays only out of combat (`Bulwark_basic_trunk_000`, `fervour_decay_out_of_combat_per_second`), so the rider is
    already true.
  - Overkill's "+5% damage" a stack says no "per point", so its rows grant 5 whatever the points (Min Points 1).
  - Chain Reaction's "At 4 points: This chain can occur once per second" is NOT written here: the generator refuses a
    Trigger Cooldown on `trigger_traps_nearby`. It is engine slice E2's (a cooldown on that action).
  - Synchronized's second half sets off the nearest trap within 30 m of the destroyed gadget.

TWO RULED SENTENCES BELONG TO THESE ROWS, Hemorrhage (R8) and Blood Price (R9). They are written into the tree files by
`tools/apply_war_trees_sentences_2026_11_27.py`, not here.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
MELEE = base.MELEE
TRAP = base.TRAP
GADGET = "Type.Gadget"
BLEED = "Keyword.DoT.Bleed"
DUAL = {"cond": "dual_wielding"}
TWO_HANDED = {"cond": "wielding_two_handed_weapon"}
LABEL = "the War trees' data rows"


def _damage(value: float, **columns) -> tuple[base.Row, base.Row]:
    """"+X% damage": attack damage and spell damage alike."""
    return (R("attack_damage", "increased", value, **columns), R("spell_damage", "increased", value, **columns))


#: The data-only nodes: (node, Node Name label, rows), in the shape `base.NODES` takes.
DATA_NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # ---- Berserker ----
    ("Berserker_trunk_007", "Battle Cry", (R("fervour_decay_out_of_combat_per_second", "flat", -0.2),)),
    ("Berserker_twoh_005", "Crusher", (
        R(None, "", 8, tags=MELEE, action="apply_status", action_event="hit_dealt", ailment="Stagger", **TWO_HANDED),)),
    ("Berserker_dw_010", "Flurry of Blows", (
        *_damage(1, scale="own_stacks", step=1, cap=10, action_event="melee_attack_use", stack_seconds=2,
                 max_points=5, **DUAL),
        *_damage(1, scale="own_stacks", step=1, cap=10, action_event="melee_attack_use", stack_seconds=3,
                 min_points=5, per_point_from=1, **DUAL))),
    ("Berserker_dw_011", "Blur", (
        R("movement_speed", "increased", 2, scale="own_stacks", step=1, cap=5, action_event="hit_dealt",
          stack_seconds=4, **DUAL),)),
    ("Berserker_dw_016", "Bloodlust", (
        R("attack_speed", "increased", 4, scale="own_stacks", step=1, cap=5, action_event="kill", stack_seconds=4,
          **DUAL),
        R(None, "", 5, action="class_resource", action_event="kill", fraction_of="points", min_points=1,
          cond="in_frenzy", cond2="dual_wielding"))),
    ("Berserker_leech_006", "Feast", (
        R("life_leech", "flat", 0.3, scale="class_resource_held", step=10),
        R(None, "", 1, action="health", action_event="resource_empty", fraction_of="maximum"))),
    # ---- Bulwark ----
    ("Bulwark_basic_trunk_005", "Battle Scars", (R("class_resource", "increased", 10),)),
    ("Bulwark_basic_trunk_006", "Tempered Steel", (
        R("armor", "increased", 2, scale="class_resource_held", step=25),
        *_damage(1, scale="class_resource_held", step=25),
        R("armor", "increased", 2, scale="class_resource_held", step=25, min_points=6, per_point_from=1,
          cond="class_resource_at_maximum"),
        *_damage(1, scale="class_resource_held", step=25, min_points=6, per_point_from=1,
                 cond="class_resource_at_maximum"))),
    ("Bulwark_keystone_trunk_002", "Blood and Sweat", (
        R(None, "", 1, action="class_resource", action_event="kill", fraction_of="points", trigger_cooldown=0.2),)),
    ("Bulwark_keystone_trunk_003", "Last Resort", (
        R(None, "", 50, action="class_resource", action_event="health_falls_below", event_value=20,
          fraction_of="points", trigger_cooldown=60),)),
    ("Bulwark_basic_ic_stem_004", "Aegis Strike", _damage(3, scale="block_chance", step=10)),
    ("Bulwark_basic_ic_final_001", "Indomitable", (
        R("damage_reduction", "more", 0.02, scale="block_chance", step=1),)),
    ("Bulwark_basic_jg_t3_001", "War of Attrition", _damage(2, scale="seconds_in_combat", step=5, cap=10)),
    ("Bulwark_basic_tw_stem_002", "Thick Hide", (
        R("damage_reduction", "more", 3),
        R("retaliation", "increased", 1, scale="own_stacks", step=1, cap=10, action_event="hit_taken",
          stack_seconds=5, min_points=6))),
    ("Bulwark_basic_tw_t2_002", "Martyrdom", (
        R("retaliation", "increased", 5, scale="own_stacks", step=1, cap=1, action_event="health_falls_below",
          event_value=50, stack_seconds=5),)),
    ("Bulwark_basic_tw_t1_001", "Serrated Edge", (R("dot_duration", "increased", 10, tags=BLEED),)),
    ("Bulwark_basic_tw_t1_002", "Blood Price", (R("dot_damage", "increased", 5, tags=BLEED),)),
    ("Bulwark_keystone_tw_t1", "Hemorrhage", (
        R(None, "", 2, action="apply_status_sized", action_event="retaliation_dealt", ailment="Bleed"),)),
    # ---- Saboteur ----
    ("Saboteur_basic_trunk_002", "Overkill", (
        *_damage(5, scale="fervour_overflow_stacks", step=1, cap=4, min_points=1),
        R("fervour_overflow_attack_speed_seconds", "flat", 2, min_points=1))),
    ("Saboteur_basic_trap_qty_002", "Chain Reaction", (
        R(None, "", 8, action="trigger_traps_nearby", action_event="trap_triggered", cap=1, min_points=1),)),
    ("Saboteur_basic_trap_mid_005", "Bleeding Ground", (
        R(None, "", 10, tags=TRAP, action="apply_status", action_event="deployable_hit", ailment="Bleed"),)),
    ("Saboteur_basic_gadget_sus_004", "Symbiosis", (
        R("minion_damage", "increased", 2, tags=GADGET, scale="minion_seconds_active", step=1, cap=20),)),
    ("Saboteur_basic_gadget_mid_003", "Synchronized", (
        R(None, "", 100, action="gadget_fires_at_trap_enemy", action_event="trap_triggered"),
        R(None, "", 30, action="trigger_traps_nearby", action_event="gadget_destroyed", cap=1, min_points=1))),
)


#: ENGINE SLICE E1'S NODES (2026-11-27): written once the slice's conditions, scales, events and stats exist. The six
#: that left the data-only group come first; the rest are the nodes the plan put under E1. Each judgement is in the
#: decisions entry.
E1_NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # ---- Berserker ----
    ("Berserker_trunk_010", "War Cry", (
        R("attack_damage", "increased", 2, tags=MELEE, scale="own_stacks", step=1, cap=1,
          action_event="attack_at_full_fervour", stack_seconds=4),)),
    ("Berserker_dw_009", "Rapid Fire", (
        R("attack_speed", "increased", 5, tags=MELEE, scale="own_stacks", step=1, cap=4,
          action_event="attack_at_full_fervour", stack_seconds=3, **DUAL),)),
    ("Berserker_trunk_011", "Thick Skin", (
        R("damage_reduction", "flat", 1, scale="class_resource_held_percent", step=4, cap=25),)),
    ("Berserker_twoh_010", "Juggernaut", (
        R("movement_speed", "increased", 3, **TWO_HANDED),
        *(R(stat, "flat", 1, min_points=5, cond="class_resource_above", cond_value=50,
            cond2="wielding_two_handed_weapon")
          for stat in ("immune_to_stun", "immune_to_stagger", "knockback_suppressed")))),
    ("Berserker_twoh_013", "Executioner", (
        R("attack_damage", "increased", 1, tags=MELEE, scale="target_health_missing", step=1,
          cond="attack_at_full_fervour", cond2="wielding_two_handed_weapon"),)),
    ("Berserker_leech_003", "Vital Strike", (
        R("life_leech", "flat", 1, tags=MELEE, cond="blow_was_critical"),
        R("fervour_per_critical_strike", "flat", 1, tags=MELEE, cond="health_at_or_above", cond_value=100))),
    ("Berserker_dw_005", "Relentless", (
        R("fervour_per_hit_dealt", "flat", 1, tags=MELEE, cond="in_frenzy", cond2="blow_was_not_critical"),)),
    ("Berserker_dw_007", "Dancing Blades", (
        R("fervour_drain_per_second", "flat", 2, scale="enemies_struck_recently", step=1, min_points=1,
          cond="in_frenzy"),
        *_damage(6, cond="target_damaged_by_you_within_seconds", cond_value=3, cond2="in_frenzy"))),
    ("Berserker_leech_002", "Iron Will", (R("class_resource", "flat", 1, scale="increased_max_health", step=2),)),
    ("Berserker_leech_005", "Sanguine", (
        R("life_leech", "flat", 1, tags=MELEE),
        R("fervour_per_hit_dealt", "flat", 1, scale="own_stacks", step=1, cap=5, action_event="life_leeched",
          stack_seconds=2, max_points=4),
        R("fervour_per_hit_dealt", "flat", 1, scale="own_stacks", step=1, cap=10, action_event="life_leeched",
          stack_seconds=2, min_points=4, per_point_from=1))),
    # ---- Bulwark ----
    ("Bulwark_basic_ic_stem_003", "Stalwart", (R("armor", "increased", 2, cond="holding_shield"),)),
    ("Bulwark_basic_ic_t2_001", "Reinforced Guard", (R("max_health", "increased", 3, cond="holding_shield"),)),
    ("Bulwark_basic_jg_t1_002", "Iron Mind", (
        R("crowd_control_resistance", "flat", 15),
        R(None, "", 10, action="class_resource", action_event="crowd_control_taken", fraction_of="points",
          min_points=1))),
    ("Bulwark_keystone_trunk_001", "Unbroken", (
        R("immune_to_stun", "flat", 1, cond="class_resource_points_at_least", cond_value=50),)),
    ("Bulwark_basic_tw_t1_003", "Open Wounds", (
        R("retaliation", "increased", 3, scale="bleeding_enemies_in_reach", step=1, reach=4, cap=5),
        *_damage(20, cond="target_carries_bleed", min_points=5))),
    ("Bulwark_basic_tw_stem_004", "Outnumbered", (
        R("retaliation", "increased", 2, scale="enemies_that_struck_you_recently", step=1),
        R("retaliation_radius_metres", "flat", 4, cond="class_resource_at_maximum", min_points=1))),
    ("Bulwark_basic_ic_stem_005", "Resolve to Steel", (
        R("block_chance", "flat", 1, scale="class_resource_held", step=10),)),
    ("Bulwark_basic_ic_t3_002", "Living Fortress", (
        R("damage_reduction", "more", 2, scale="enemies_in_reach", step=1, reach=3),
        R("block_chance", "flat", 1, scale="enemies_in_reach", step=1, reach=3, min_points=6))),
    ("Bulwark_keystone_ic_t2", "Unyielding Guard", (
        R("block_chance", "flat", 5, scale="own_stacks", step=1, cap=4, action_event="block", stack_seconds=3),
        R(None, "", 3, action="class_resource", action_event="block", fraction_of="points"))),
    ("Bulwark_basic_tw_final_001", "Undying Rage", (
        R("retaliation", "increased", 3, scale="health_missing", step=10),
        R("retaliation", "more", 100, cond="health_below", cond_value=25, min_points=6),
        R("retaliation_cannot_be_mitigated", "flat", 1, cond="health_below", cond_value=25, min_points=6))),
)

#: ENGINE SLICE E2'S NODES (2026-11-27): written once the slice's actions exist.
E2_NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # OPEN WOUND (issue #964): "Critical melee strikes apply a wound to the target for 2 seconds per point. While
    # wounded, the target takes +3% increased damage per point from all sources. Applying a new wound refreshes the
    # duration." A placed stack's window does not grow with points, so the wound is one row for each point count, each
    # with its own window: 2 seconds at 1 point, 4 at 2, up to 12 at 6. One stack, refreshed.
    ("Berserker_dw_006", "Open Wound", tuple(
        R(None, "", 3, tags=MELEE, action="enemy_damage_taken", action_event="critical_strike",
          stack_seconds=2 * points, cap=1, min_points=points, max_points=points + 1 if points < 6 else None,
          per_point_from=1 if points > 1 else None, **DUAL)
        for points in range(1, 7))),
    # REND: "Each melee hit reduces the target's damage reduction by 1% per point for 3 seconds, stacking up to 5
    # times. Stacks refresh on each hit. At 5 points: The stack cap is increased to 8."
    ("Berserker_dw_004", "Rend", (
        R(None, "", 1, tags=MELEE, action="enemy_damage_reduction_removed", action_event="hit_dealt",
          stack_seconds=3, cap=5, max_points=5, **DUAL),
        R(None, "", 1, tags=MELEE, action="enemy_damage_reduction_removed", action_event="hit_dealt",
          stack_seconds=3, cap=8, min_points=5, per_point_from=1, **DUAL))),
    # MOUNTAIN'S WRATH (R5): "Once every 10 seconds, your next attack deals bonus damage equal to 5% of your Maximum HP."
    ("Bulwark_keystone_jg_t2", "Mountain's Wrath", (
        R(None, "", 5, action="next_attack_bonus_of_max_health", action_event="every_seconds", every_seconds=10),)),
    # DEAD WEIGHT (R27): "+3% damage reduction per point while crowd controlled (multiplicative). At 4 points: each
    # crowd control effect applied to you deals damage equal to 3% of your Maximum HP to the enemy that applied it."
    ("Bulwark_basic_jg_t1_003", "Dead Weight", (
        R("damage_reduction", "more", 3, cond="while_crowd_controlled"),
        R(None, "", 3, action="strike_target_of_max_health", action_event="crowd_control_taken", min_points=4))),
    # IMMOVABLE OBJECT (R4): "While above 50% health, you are immune to crowd control. Each crowd control effect you
    # are immune to this way deals damage equal to 3% of your Maximum HP to the enemy that applied it."
    ("Bulwark_keystone_jg_t1", "Immovable Object", (
        R("immune_to_crowd_control", "flat", 1, cond="health_above", cond_value=50),
        R(None, "", 3, action="strike_target_of_max_health", action_event="crowd_control_refused"))),
    # FORTIFIED MIND, the coordinating session's reading: "+3% Fervour generation from blocking per point. At 5 points:
    # While at maximum Fervour, blocking deals damage equal to 3% of your Maximum HP back to the attacker."
    ("Bulwark_basic_ic_t3_001", "Fortified Mind", (
        R("fervour_per_block", "increased", 3),
        R(None, "", 3, action="strike_target_of_max_health", action_event="block", cond="class_resource_at_maximum",
          min_points=5))),
    # SHIELD SLAM, the coordinating session's reading: "+2% damage per point for every 10% Block Chance you have. At 6
    # points: after you block, your next attack deals bonus damage equal to 5% of your Armor."
    ("Bulwark_basic_ic_t2_003", "Shield Slam", (
        *_damage(2, scale="block_chance", step=10),
        R(None, "", 5, action="next_attack_bonus_of_armor", action_event="block", min_points=6))),
    # THE INEVITABLE, with the coordinating session's 4 metres for "nearby": 1% of maximum health a second after 10
    # seconds in combat, and 0.5% more for each further 10 seconds, to 3% at 50.
    ("Bulwark_keystone_jg_t3", "The Inevitable", tuple(
        R(None, "", 1 if seconds == 10 else 0.5, action="smite_nearby_of_max_health", action_event="every_seconds",
          every_seconds=1, event_value=4, cond="in_combat_for_seconds", cond_value=seconds)
        for seconds in (10, 20, 30, 40, 50))),
    # OVERWHELMING FORCE: "When you attack at maximum Fervour, the force of the attack releases a shockwave that deals
    # 30% of your attack damage per point to all enemies within 5 meters." `smite_nearby` is that: a hit of the value's
    # share of attack damage on every enemy within 5 metres. A one-point keystone, so 30.
    ("Berserker_twoh_008", "Overwhelming Force", (
        R(None, "", 30, action="smite_nearby", action_event="attack_at_full_fervour", **TWO_HANDED),)),
    # MOMENTUM, the coordinating session's reading: "While in Wrath, this bonus is doubled" for "gained twice as fast".
    ("Berserker_twoh_002", "Momentum", (
        *_damage(3, tags=MELEE, scale="consecutive_hits", step=1, cap=5, action_event="hit_dealt", **TWO_HANDED),
        *_damage(3, tags=MELEE, scale="consecutive_hits", step=1, cap=5, action_event="hit_dealt", cond="in_wrath",
                 cond2="wielding_two_handed_weapon"))),
    # UNRELENTING, the coordinating session's reading: "Killing an enemy refunds 10% of your maximum Fervour per point."
    ("Berserker_dw_015", "Unrelenting", (
        R(None, "", 10, action="class_resource", action_event="kill", fraction_of="maximum", **DUAL),)),
    # APEX PREDATOR: "Killing an enemy with an attack made at maximum Fervour instantly reduces all cooldowns by 50%".
    ("Berserker_twoh_ks_003", "Apex Predator", (
        R(None, "", 50, action="cooldown_reduce_all_percent", action_event="kill", cond="attack_at_full_fervour",
          cond2="wielding_two_handed_weapon"),)),
    # COUP DE GRACE: "Your critical strikes deal bonus damage equal to 2% of the target's missing HP. This bonus is
    # tripled on attacks made at maximum Fervour." Tripled is 4 more on the full-pool attack, 6 in all.
    ("Berserker_crit_ks_001", "Coup de Grace", (
        R(None, "", 2, action="strike_target_of_target_missing_health", action_event="critical_strike"),
        R(None, "", 4, action="strike_target_of_target_missing_health", action_event="critical_strike",
          cond="attack_at_full_fervour"))),
    # WHIRLWIND: at maximum Fervour the strike hits all around (a full circle), each enemy hit gives 3 Fervour, and those
    # hit are knocked back 3 metres and take 50% increased damage. The arc is read while the targets are chosen, before
    # the attack begins and before Berserking spends the pool, so it reads the pool full; the Fervour, the damage and the
    # knockback read the attack's snapshot.
    ("Berserker_dw_ks_001", "Whirlwind", (
        R("melee_arc_full_circle", "flat", 1, cond="class_resource_at_maximum", cond2="dual_wielding"),
        R("fervour_per_enemy_hit", "flat", 3, cond="attack_at_full_fervour", cond2="dual_wielding"),
        R("attack_damage", "increased", 50, tags=MELEE, cond="attack_at_full_fervour", cond2="dual_wielding"),
        R(None, "", 3, tags=MELEE, action="knock_back_target", action_event="hit_dealt", cond="attack_at_full_fervour"))),
    # DEATH STRIKE: below 20% outside Frenzy, below 35% in it, +2% a point per 1% of the target's health missing.
    ("Berserker_dw_013", "Death Strike", (
        R("attack_damage", "increased", 2, tags=MELEE, scale="target_health_missing", step=1,
          cond="target_health_below", cond_value=20, cond2="dual_wielding_outside_frenzy"),
        R("attack_damage", "increased", 2, tags=MELEE, scale="target_health_missing", step=1,
          cond="target_health_below", cond_value=35, cond2="in_frenzy"))),
    # FRENETIC: in Frenzy, for every 10 Fervour Frenzy drains, a strike of 20% weapon damage a point on the nearest enemy.
    ("Berserker_dw_012", "Frenetic", (
        R(None, "", 20, action="strike_nearest_near", action_event="frenzy_drained"),)),
    # KNIFE DANCE: "Each automatic strike triggered by Frenetic generates 2 Fervour per point."
    ("Berserker_dw_018", "Knife Dance", (
        R(None, "", 2, action="class_resource", action_event="frenzy_drained", fraction_of="points"),)),
    # UNSTOPPABLE FORCE, the project owner's ruling (2026-11-27): a kill in Frenzy triggers one Frenetic strike, and
    # entering Frenzy strikes the nearest enemy three times. A keystone cannot read Frenetic's points, so each is 100% of
    # weapon damage, the old sentence's "full damage".
    ("Berserker_dw_ks_002", "Unstoppable Force", (
        R(None, "", 100, action="strike_nearest_near", action_event="kill", cond="in_frenzy"),
        R(None, "", 100, action="strike_nearest_near", action_event="frenzy_entered", event_value=3))),
    # WRECKING BALL: +5% area of effect a point; from 5 points an enemy killed by an area skill (`Type.AOE`) bursts for
    # 20% of its maximum health on the enemies near it.
    ("Berserker_twoh_014", "Wrecking Ball", (
        R("area_of_effect", "increased", 5, **TWO_HANDED),
        R(None, "", 20, tags="Type.AOE", action="burst_slain_for_share_of_maximum_health", action_event="kill",
          min_points=5, cond="wielding_two_handed_weapon"))),
    # FLURRY (R11): "Dual wield only. Every 5th consecutive melee attack deals 50% more damage per point." Every attack
    # is counted, the attacks "every Nth attack" rows count; the coordinating session marked the size a play-test point.
    ("Berserker_dw_002", "Flurry", (
        R(None, "", 50, action="nth_attack_more_damage", every_nth=5, **DUAL),)),
    # KILLING RHYTHM: "+5% critical strike multiplier per point for each consecutive critical strike, up to 5 stacks.
    # Missing a critical strike or your Fervour reaching 0 resets the streak." The sentence names no window; a stack
    # lasts 10 seconds, a judgement, so a streak broken by nothing at all still ends.
    ("Berserker_crit_005", "Killing Rhythm", (
        R("crit_multiplier", "flat", 5, scale="own_stacks", step=1, cap=5, action_event="critical_strike",
          stack_seconds=10),
        R(None, "", 100, action="own_stacks_removed", action_event="hit_dealt", cond="blow_was_not_critical"),
        R(None, "", 100, action="own_stacks_removed", action_event="resource_empty"))),
    # CONSECUTIVE BLOCKS: "+2% damage reduction per successive block per point, stacking up to 5 times. Resets if you
    # take an unblocked hit." No window named; 10 seconds, the same judgement.
    ("Bulwark_basic_ic_t2_002", "Consecutive Blocks", (
        R("damage_reduction", "flat", 2, scale="own_stacks", step=1, cap=5, action_event="block", stack_seconds=10),
        R(None, "", 100, action="own_stacks_removed", action_event="hit_taken", cond="hit_taken_was_unblocked"))),
    # FEEDING FRENZY: "+0.3% life leech per point for each melee attack you have landed in the last 2 seconds, up to 10
    # attacks. While in Frenzy this cap is doubled." Two counts, each granted on every hit: one up to 10 that pays
    # outside Frenzy, one up to 20 that pays in it.
    ("Berserker_dw_008", "Feeding Frenzy", (
        R("life_leech", "flat", 0.3, scale="own_stacks", step=1, cap=10, action_event="hit_dealt", stack_seconds=2,
          cond="dual_wielding_outside_frenzy"),
        R("life_leech", "flat", 0.3, scale="own_stacks", step=1, cap=20, action_event="hit_dealt", stack_seconds=2,
          cond="in_frenzy"))),
    # SPITEFUL, the coordinating session's reading: "+3% Fervour generated from taking hits per point. At 5 points: when
    # your Fervour becomes full, your next retaliation deals 50% more damage."
    ("Bulwark_basic_tw_stem_003", "Spiteful", (
        R("fervour_per_hit_taken", "increased", 3),
        R(None, "", 50, action="next_retaliation_more", action_event="resource_full", min_points=5))),
    # KILLING BLOW, with the coordinating session's 8 metres: "+6% critical strike multiplier per point on melee attacks.
    # At 10 points: when you attack at maximum Fervour, all enemies within 8 metres below 25% HP are instantly killed."
    ("Berserker_twoh_015", "Killing Blow", (
        R("crit_multiplier", "flat", 6, tags=MELEE, **TWO_HANDED),
        R(None, "", 25, action="execute_nearby_below", action_event="attack_at_full_fervour", event_value=8,
          min_points=10, cond="wielding_two_handed_weapon"))),
    # SHATTER: "Critical melee strikes reduce enemy armor by 5% per point for 5 seconds, stacking up to 4 times."
    ("Berserker_twoh_009", "Shatter", (
        R(None, "", 5, tags=MELEE, action="enemy_armor_removed", action_event="critical_strike", stack_seconds=5,
          cap=4, **TWO_HANDED),)),
)

#: ENGINE SLICE E3'S NODES (2026-11-27): the keystone rules.
E3_NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # OPEN WOUNDS (Berserker): "Life leech can overheal you up to 30% above your maximum HP. Bonus HP from overleech
    # decays at 1% per second when you are not leeching. While you have any overleech HP, incoming damage removes
    # overleech before your actual HP." Eternal Hunger's two stats, read as the largest of their rows (R14).
    ("Berserker_trunk_006", "Open Wounds", (
        R("leech_overheal_absorb_percent_of_maximum_health", "flat", 30),
        R("leech_overheal_absorb_decay_percent_per_second", "flat", 1))),
    # BERSERKER'S RAGE, the project owner's ruling (2026-11-27): "Berserking spends half the pool instead of all of it".
    ("Berserker_twoh_ks_002", "Berserker's Rage", (R("berserking_spends_half", "flat", 1, **TWO_HANDED),)),
    # BLOODIED: "While below 30% HP your Fervour generation is tripled and Berserking triggers on every melee hit rather
    # than only when Fervour is full." Tripled is 200% more.
    ("Berserker_trunk_012", "Bloodied", (
        R("class_resource_generation", "more", 200, cond="health_below", cond_value=30),
        R("berserking_on_every_melee_hit", "flat", 1, cond="health_below", cond_value=30))),
    # HAIR TRIGGER: "Critical strike chance past 100% is converted to critical strike multiplier at a 2:1 ratio." Every 2%
    # of chance past 100 adds 1% to the multiplier; the chance itself is still held to its ceiling.
    ("Berserker_crit_ks_002", "Hair Trigger", (R("crit_chance_past_cap_to_multiplier", "flat", 1),)),
    # COLOSSUS (R3): "+10 flat Maximum HP per point for every 1,000 Maximum HP you have from other sources."
    ("Bulwark_basic_jg_stem_004", "Colossus",
     (R("max_health", "flat", 10, scale="max_health_from_other_sources", step=1000),)),
    # FLESH FORTRESS (R16): "While you do not hold a shield: your Armor is 0, you gain 1% increased Maximum HP for every
    # 50 Armor you would have had, and you gain 50% increased Fervour from all sources." The removal is conditioned, so
    # the armour attribute the `armor` scale reads -- written from unconditioned rows only -- is what it would have been.
    ("Bulwark_keystone_tw_t2", "Flesh Fortress",
     (R("armor", "removed", 1, cond="not_holding_shield"),
      R("max_health", "increased", 1, cond="not_holding_shield", scale="armor", step=50),
      R("class_resource_generation", "increased", 50, cond="not_holding_shield"))),
    # EYE OF THE STORM: "While 3 or more enemies are within 3 meters, you gain 10 Fervour per second and your Block
    # Chance cannot be reduced below 30%."
    ("Bulwark_keystone_ic_t3", "Eye of the Storm",
     (R("fervour_per_second", "flat", 10, cond="enemies_in_reach_at_least", cond_value=3, reach=3),
      R("block_chance_floor", "flat", 30, cond="enemies_in_reach_at_least", cond_value=3, reach=3))),
    # THE LAST BASTION: "When you block an attack that would have killed you, instead survive at 1 HP, gain 100 Fervour,
    # and become immune to damage for 2 seconds. Can only trigger once every 120 seconds."
    ("Bulwark_keystone_ic_final", "The Last Bastion",
     (R("blocked_lethal_hit_survived_every_seconds", "flat", 120),
      R("damage_immunity_after_blocked_lethal_hit_seconds", "flat", 2),
      R("fervour_on_blocked_lethal_hit_survived", "flat", 100))),
    # GRINDING ADVANCE: "+1% damage reduction per point for every 10 seconds in continuous combat (multiplicative),
    # stacking up to 5 times. At 5 points: Combat duration stacks are lost 50% slower when leaving combat."
    ("Bulwark_basic_jg_t3_002", "Grinding Advance",
     (R("damage_reduction", "more", 1, scale="combat_seconds_held", step=10, cap=5),
      R("combat_seconds_fall_slower", "flat", 1, min_points=5))),
    # WRATHFUL PRESENCE: "Your retaliation aura now pulses every second, dealing damage equal to 5% of all damage you
    # have taken in the last 5 seconds to all nearby enemies." Nearby is the larger of 4 metres (the design document's
    # melee range) and the retaliation radius.
    ("Bulwark_keystone_tw_t3", "Wrathful Presence",
     (R(None, "", 5, action="smite_nearby_of_recent_damage_taken", action_event="every_seconds",
       every_seconds=1, event_value=4),)),
    # SEISMIC SLAM (Bulwark): "+4% chance per point for melee attacks to deal bonus flat damage equal to 1% of your
    # Maximum HP. At 5 points: This bonus damage hits all enemies within 2 meters." One roll: the strike on the target
    # below 5 points (Max Points is exclusive), the 2 metre nova from 5, each a chance per point (Per Point From 1).
    ("Bulwark_basic_jg_t2_003", "Seismic Slam",
     (R(None, "", 1, tags=MELEE, action="strike_target_of_max_health", action_event="hit_dealt", chance=4,
        max_points=5),
      R(None, "", 1, tags=MELEE, action="smite_nearby_of_max_health", action_event="hit_dealt", chance=4,
        event_value=2, min_points=5, per_point_from=1))),
    # GLASS CANNON: "+8% critical strike multiplier per point on melee attacks. At 4 points: Your critical melee strikes
    # ignore 20% of enemy resistances. Each point of Fervour above your normal maximum (from any source) increases your
    # critical strike multiplier by an additional 0.5% per point invested here." Both riders read from 4 points.
    ("Berserker_crit_003", "Glass Cannon",
     (R("crit_multiplier", "flat", 8, tags=MELEE),
      R("critical_penetration", "flat", 20, tags=MELEE, min_points=4),
      R("crit_multiplier", "flat", 0.5, scale="fervour_overflow_stacks", step=1, min_points=4, per_point_from=1))),
    # VAMPIRIC FURY: "Life leech now also restores Fervour at a rate of 1 Fervour per 2% of maximum HP leeched. Fervour
    # cannot be restored above maximum by this effect." 0.5 for each 1%.
    ("Berserker_leech_ks_001", "Vampiric Fury",
     (R(None, "", 0.5, action="fervour_per_percent_of_max_health_leeched", action_event="life_leeched"),)),
    # COUNTER STANCE: "+4% damage per point for 3 seconds after taking a hit. At 6 points: If you take two hits within 1
    # second, this bonus is doubled."
    ("Bulwark_basic_ic_t1_001", "Counter Stance",
     (R("attack_damage", "increased", 4, cond="seconds_after_hit_taken", cond_value=3),
      R("spell_damage", "increased", 4, cond="seconds_after_hit_taken", cond_value=3),
      R("attack_damage", "increased", 4, cond="seconds_after_hit_taken", cond_value=3,
        cond2="two_hits_taken_within_seconds", cond_value2=1, min_points=6, per_point_from=1),
      R("spell_damage", "increased", 4, cond="seconds_after_hit_taken", cond_value=3,
        cond2="two_hits_taken_within_seconds", cond_value2=1, min_points=6, per_point_from=1))),
    # SEISMIC SLAM (Berserker): "Two-handed weapons only. When you attack at maximum Fervour, the shockwave from the
    # attack travels along the ground dealing 30% per point of the hit's damage to all enemies in a line." Once per
    # attack (`first_hit_dealt`); the line 8 metres long, a judgement.
    ("Berserker_twoh_012", "Seismic Slam",
     (R(None, "", 30, action="smite_line_by_hit", action_event="first_hit_dealt", event_value=8,
        cond="wielding_two_handed_weapon", cond2="attack_at_full_fervour"),)),
    # WEAVING (R12): "Dual wield only. Using a different melee skill from the one you used last increases your damage
    # by 2% per point, stacking up to 6 times. Using the same melee skill twice in a row removes the stacks. While in
    # Frenzy, each stack you gain costs 3 Fervour and the damage per stack is doubled." The doubled half is a second
    # stack of the same events, read only in Frenzy; the stacks have no stated window, so 10 seconds, the judgement
    # Killing Rhythm's took.
    ("Berserker_dw_003", "Weaving",
     (R("attack_damage", "increased", 2, scale="own_stacks", step=1, cap=6, action_event="different_melee_skill_used",
        stack_seconds=10, cond="dual_wielding"),
      R("spell_damage", "increased", 2, scale="own_stacks", step=1, cap=6, action_event="different_melee_skill_used",
        stack_seconds=10, cond="dual_wielding"),
      R("attack_damage", "increased", 2, scale="own_stacks", step=1, cap=6, action_event="different_melee_skill_used",
        stack_seconds=10, cond="dual_wielding", cond2="in_frenzy"),
      R("spell_damage", "increased", 2, scale="own_stacks", step=1, cap=6, action_event="different_melee_skill_used",
        stack_seconds=10, cond="dual_wielding", cond2="in_frenzy"),
      R(None, "", 100, action="own_stacks_removed", action_event="same_melee_skill_used"),
      R(None, "", -3, action="class_resource", action_event="different_melee_skill_used", fraction_of="points",
        cond="in_frenzy"))),
    # THE FIVE RIDERS THE COORDINATING SESSION RULED ON 2026-11-27, each with its ruled sentence.
    # UNBREAKABLE WILL: "+2% of blocked damage is added to your next attack per point. At 5 points: blocked damage is
    # added at three times the rate." A second row from 5 points adds twice more; the five-block streak was dropped.
    ("Bulwark_basic_ic_t1_003", "Unbreakable Will",
     (R(None, "", 2, action="next_attack_bonus_of_blocked_damage", action_event="block"),
      R(None, "", 4, action="next_attack_bonus_of_blocked_damage", action_event="block", min_points=5,
        per_point_from=1))),
    # CARNAGE: "Dual wield only. Killing an enemy while in Frenzy restores Fervour equal to 1 second of Frenzy's drain
    # per point. This can happen at most once every 1 second."
    ("Berserker_dw_017", "Carnage",
     (R(None, "", 1, action="fervour_of_frenzy_drain_seconds", action_event="kill", cond="in_frenzy",
        cond2="dual_wielding", trigger_cooldown=1),)),
    # SPREADING THORNS: "+1 meter to retaliation aura radius per point. At 4 points: Enemies killed within the aura
    # explode, dealing 100% of your retaliation damage to every enemy within 4 meters."
    ("Bulwark_basic_tw_t3_002", "Spreading Thorns",
     (R("retaliation_radius_metres", "flat", 1),
      R(None, "", 100, action="retaliation_burst_on_kill", action_event="kill", event_value=4, min_points=4))),
    # PHANTOM BLADES: one echo per melee hit, 60% of weapon damage on the same enemy 0.75 seconds later; it crits and
    # leeches on its own, earns no Fervour and makes no echo.
    ("Berserker_dw_ks_003", "Phantom Blades",
     (R(None, "", 60, tags=MELEE, action="echo_strike_later", action_event="hit_dealt", event_value=0.75,
        cond="dual_wielding"),)),
    # BLOOD FOR BLOOD: its own engine function, the 50 its stat.
    ("Bulwark_keystone_tw_final", "Blood for Blood", (R("blood_for_blood_reflect_percent", "flat", 50),)),
)

#: Every node this script writes.
#: WINDOW 2, THE SABOTEUR (2026-11-28), its data rows and engine slice E4's.
E4_NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # MARKED TERRITORY: "Enemies that trigger a trap are marked for 5 seconds. Marked enemies take +5% increased damage per
    # point from all sources." The enemy that set the trap off; one mark, its window refreshed.
    ("Saboteur_basic_trap_ql_004", "Marked Territory",
     (R(None, "", 5, action="enemy_damage_taken", action_event="trap_triggered", stack_seconds=5, cap=1),)),
    # PERSISTENT SYSTEMS, THE 4-POINT HALF: "Gadgets that survive for 20 seconds gain a permanent +10% damage stack."
    ("Saboteur_basic_gadget_sus_002", "Persistent Systems",
     (R("minion_damage", "increased", 10, tags=GADGET, scale="minion_seconds_active", step=20, cap=1, min_points=4),)),
    # SCRAP AND REBUILD, THE FERVOUR HALF: "When a gadget is destroyed gain +5 Fervour per point".
    ("Saboteur_basic_gadget_deep_003", "Scrap and Rebuild",
     (R(None, "", 5, action="class_resource", action_event="gadget_destroyed", fraction_of="points"),)),
    # FORWARD OPERATING BASE: "When you deploy a gadget, all other active gadgets are pulled to the most recent gadget."
    ("Saboteur_keystone_gadget_dep", "Forward Operating Base",
     (R(None, "", 100, action="gadgets_pulled_to_placed", action_event="gadget_placed"),)),
    # STEADY POWER (ruled 2026-11-27): each active gadget drains 1 Fervour a second; "+4% damage per point for each point
    # of Fervour above 50% of your maximum".
    ("Saboteur_basic_gadget_sus_001", "Steady Power",
     (R("fervour_drain_per_second", "flat", 1, scale="gadgets_active", step=1, min_points=1),
      *_damage(4, tags=GADGET, scale="class_resource_above_half", step=1))),
    # HOT DEPLOY: "+2% gadget attack speed per point for every 10 Fervour spent in the last 5 seconds."
    ("Saboteur_basic_gadget_dep_001", "Hot Deploy",
     (R("minion_attack_speed", "increased", 2, tags=GADGET, scale="class_resource_spent_recently", step=10),)),
    # CALLOUS SETUP: "+5% trap damage per point for every 10% of your evasion chance, up to a maximum of 60%."
    ("Saboteur_basic_trap_mid_007", "Callous Setup",
     (R("minion_damage", "increased", 5, tags=TRAP, scale="evasion", step=10, cap=6),)),
    # CALCULATED RISK: "Your evasion chance is permanently set to 0%. All gadget damage is increased by 3% for every 1% of
    # evasion chance you would have had from all sources."
    ("Saboteur_basic_gadget_mid_005", "Calculated Risk",
     (R("evasion", "removed", 1), *_damage(3, tags=GADGET, scale="evasion_before_removal", step=1))),
    # HAIR WIRE EXPERTISE (ruled 2026-11-28): "+8% trap arming speed per point for each unique trap type you have active."
    ("Saboteur_basic_trap_mid_004", "Hair Wire Expertise",
     (R("trap_arming_speed", "increased", 8, tags=TRAP, scale="trap_kinds_active", step=1),)),
    # SIEGE MODE (R19): "Once you have stood still for 3 seconds, your gadgets gain +5% damage and +3% range for each
    # second you have stood still, up to 10 seconds. Moving ends the bonus."
    ("Saboteur_keystone_gadget_sus", "Siege Mode",
     (*_damage(5, tags=GADGET, scale="player_seconds_stationary", step=1, cap=10, cond="player_stationary_for_seconds",
               cond_value=3),
      R("minion_range", "increased", 3, tags=GADGET, scale="player_seconds_stationary", step=1, cap=10,
        cond="player_stationary_for_seconds", cond_value=3))),
    # COMBAT ENGINEER: "+8% gadget attack range per point for each active gadget on the battlefield." Its 5-point half,
    # "Gadgets can attack enemies through terrain within their range", is already true: a gadget's targeting has no
    # line-of-sight test (`ACataclysmMinion`, `UCataclysmTargeting`), so it has no row.
    ("Saboteur_basic_gadget_deep_001", "Combat Engineer",
     (R("minion_range", "increased", 8, tags=GADGET, scale="gadgets_active", step=1),)),
)

NODES = DATA_NODES + E1_NODES + E2_NODES + E3_NODES + E4_NODES


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def take_out(path: pathlib.Path) -> None:
    """Put a workbook back as it stood before this script: every row of its nodes DELETED.

    FOR TESTS. The scripts written before this one are held to reproduce the repository's sheets from an earlier state
    (`tools/tests/test_apply_aura_reservation.py` and the tests that use `tools/tests/without_the_aura_reservation.py`),
    and these rows come after theirs. Those tests compare against a copy with these rows taken out, made by
    `tools/tests/without_the_war_trees_rows.py`. A workbook that does not hold exactly this script's rows on each node
    is refused rather than half undone. Rows are deleted, not blanked, so rows another script appends later land where
    they would have without these.
    """
    import openpyxl

    book = openpyxl.load_workbook(path)
    sheet = book[base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    wanted = {node: len(rows) for node, _, rows in NODES}
    found: dict[str, list[int]] = {}
    for index in range(2, sheet.max_row + 1):
        node = sheet.cell(index, node_column).value
        if node in wanted:
            found.setdefault(node, []).append(index)
    wrong = {node: len(found.get(node, [])) for node in wanted if len(found.get(node, [])) != wanted[node]}
    if wrong:
        raise base.ReapplyError(f"{LABEL}: these nodes do not hold exactly this script's rows, so they cannot be taken "
                                f"out: {wrong}")
    for index in sorted((i for rows in found.values() for i in rows), reverse=True):
        sheet.delete_rows(index, 1)
    book.save(path)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
