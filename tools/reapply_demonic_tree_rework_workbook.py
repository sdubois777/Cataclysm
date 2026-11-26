"""Re-apply the Demonic tree rework's Passive Effects rows to the design workbook.

    python tools/reapply_demonic_tree_rework_workbook.py            # write the rows, then regenerate the CSVs
    python tools/reapply_demonic_tree_rework_workbook.py --check    # say what would change, write nothing
    python tools/reapply_demonic_tree_rework_workbook.py --workbook <copy.xlsx> --no-generate

WHY THIS EXISTS. The project owner approved three audits of the Demonic class trees
on 2026-11-24 ("Accept all, write them (Recommended)"). A node's sentence lives in its
tree file and `tools/apply_demonic_tree_rework_2026_11_24.py` writes those; the numbers
behind each sentence are rows in the Passive Effects sheet of
`docs/All_Things_Cataclysm.xlsx`, which `tools/generate_datatables.py` turns into
`game/Data/PassiveEffects.csv`. The workbook is a binary git cannot merge, so this
branch's edit to it is recorded here as data: whichever workbook `development` holds
when this lands, this script redoes the edit on it.

ALSO THE MASOCHIST ORDER FIX'S HALF OF THE WORKBOOK. `tools/apply_passive_order_fixes_
2026_11_24.py` swaps Blood Price and Deeper Cuts in the tree and lowers Grand Tithe to
2%, and writes no workbook row. Rows are keyed by node identifier, so after that swap
the rows on `Masochist_basic_spine_007` must be Deeper Cuts' and those on
`Masochist_basic_bt_stem1` Blood Price's, and Grand Tithe's two rows must compare
against 2. Both swapped nodes are reworked here anyway, and each accepts either the
unswapped rows or the swapped-but-not-reworked ones as its starting state.

EACH CHANGE IS ONE NODE'S ROWS, OR ONE CAPSTONE OPTION'S, FOUND BY THE `Node` COLUMN
AND NEVER BY A ROW NUMBER. It states the rows it was approved against and the rows it
writes, compared as a set, every column but the human `Node Name`. Rows already as
written are left alone; rows in any other state are refused, writing nothing, because
somebody has changed them since and a person has to look. The new rows take the place
of the old ones, at the position of the node's first row, so the sheet stays grouped
as it was; the rows below move up or down by the difference, which is why the check
afterwards compares the whole sheet with what was intended rather than cell by cell.

"FOR EVERY N POINTS" IS ONE ROW PER STEP. The project owner chose whole-number steps
over fractional per-point values (question 4 of the Masochist audit): a node reading
"+1% Life Leech for every 2 points" is five `flat 1` rows from 2, 4, 6, 8 and 10
points, each granted whole from its `Min Points` (issue #1755). `steps()` builds them.

A CAP ON A PASSIVE ROW. Wall of Servants, the Ritualist's new Second Pact option, is
"1% less damage for each minion you have, up to 20% less" (question 3 of the Ritualist
audit). The cap is a `Scale Max Steps` column, which this script adds to the sheet when
it is missing; the Ritualist half of this branch teaches the generator and the game to
read it on a passive row.

IT REFUSES a workbook holding formulas, charts or images, which an openpyxl save can
drop without saying so (as `tools/reapply_abyssal_rifts_row.py` does), and it proves
after saving that the Passive Effects sheet holds exactly the intended rows and that no
other sheet changed. It prints every row it removes and writes.

AFTERWARDS the DataTable assets need the editor step,
`python tools/run_editor_python.py tools/generate_datatable_assets.py`.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import pathlib
import subprocess
import sys

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"
SHEET = "Passive Effects"

#: The sheet's columns this script reads and writes, by header name.
COLUMNS = ("Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Node Name", "Reach Metres",
           "Min Points", "Scale Max Steps")

#: The column the Ritualist's capped option needs, added at the end when missing.
CAP_COLUMN = "Scale Max Steps"


class ReapplyError(Exception):
    """Something about the workbook that makes writing the rows unsafe or impossible."""


@dataclasses.dataclass(frozen=True)
class Row:
    """One Passive Effects row, less the node it belongs to and its human label."""
    stat: str
    kind: str
    value: float
    tags: str | None = None
    cond: str | None = None
    cond_value: float | None = None
    scale: str | None = None
    step: float | None = None
    option: int | None = None
    reach: float | None = None
    min_points: int | None = None
    cap: int | None = None

    def key(self) -> tuple:
        def num(v):
            return None if v is None else float(v)
        return (self.stat, self.kind, float(self.value), self.tags or None,
                self.cond or None, num(self.cond_value), self.scale or None,
                num(self.step), num(self.option), num(self.reach),
                num(self.min_points), num(self.cap))


R = Row


def steps(stat: str, kind: str, value: float, every: int, most: int, **more) -> tuple:
    """The rows of "<value> for every <every> points" on a node of `most` points."""
    return tuple(Row(stat, kind, value, min_points=points, **more)
                 for points in range(every, most + 1, every))


@dataclasses.dataclass(frozen=True)
class Change:
    """One node's rows (option 0) or one capstone option's, before and after.

    `before` holds every state the rows may be found in and still be rewritten;
    `label` is the `Node Name` the written rows carry when it changes (a renamed
    node, an option replaced, or the Ritualist's swapped pair).
    """
    node: str
    option: int
    before: tuple
    after: tuple
    label: str | None = None


def node(node_key: str, before, after, label: str | None = None,
         also_before: tuple = ()) -> Change:
    return Change(node_key, 0, (tuple(before),) + tuple(tuple(b) for b in also_before),
                  tuple(after), label)


def option(node_key: str, number: int, before, after, label: str | None = None) -> Change:
    return Change(node_key, number, (tuple(before),), tuple(after), label)


# --------------------------------------------------------------------------
# The Masochist
# --------------------------------------------------------------------------

M = "Masochist_"

#: The rows on the two positions the order fix swaps, as development holds them.
BLOOD_PRICE_ROWS = (R("fervour_from_cost", "increased", 2),)
DEEPER_CUTS_ROWS = (R("added_health_cost", "flat", 0.25),)

MASOCHIST = (
    node(M + "basic_spine_001",
         (R("max_health", "increased", 1), R("armor", "increased", 0.5)),
         (R("max_health", "increased", 1),)),
    node(M + "basic_spine_002",
         (R("health_regen", "increased", 2),),
         (R("health_regen", "increased", 1, scale="health_missing", step=10),)),
    node(M + "basic_spine_003",
         (R("attack_damage", "increased", 1, cond="seconds_after_foreign_damage", cond_value=5),
          R("spell_damage", "increased", 1, cond="seconds_after_foreign_damage", cond_value=5)),
         (R("attack_damage", "increased", 1, scale="class_resource_held", step=25),
          R("spell_damage", "increased", 1, scale="class_resource_held", step=25)),
         label="Gathered Pain"),
    node(M + "basic_spine_004",
         (R("life_leech", "flat", 0.4),),
         steps("life_leech", "flat", 1, every=2, most=10)),
    node(M + "basic_spine_005",
         (R("damage_over_time_taken", "increased", -1),),
         (R("damage_over_time_taken", "increased", -2),)),
    node(M + "basic_spine_006",
         (R("fervour_from_damage", "increased", 2),),
         (R("fervour_from_damage", "increased", 5),)),
    # DEEPER CUTS, AT ITS PLACE AFTER THE ORDER FIX.
    node(M + "basic_spine_007", BLOOD_PRICE_ROWS,
         steps("added_health_cost_of_current", "flat", 1, every=2, most=10),
         label="Deeper Cuts", also_before=(DEEPER_CUTS_ROWS,)),
    node(M + "basic_spine_008",
         (R("class_resource", "increased", 2),),
         (R("class_resource", "flat", 5),)),
    node(M + "basic_spine_009",
         (R("crowd_control_resistance", "increased", 2),),
         (R("crowd_control_resistance", "flat", 4),)),
    node(M + "basic_spine_010",
         (R("retaliation", "increased", 3),),
         (R("retaliation", "flat", 2),)),
    node(M + "keystone_spine_001",
         (R("attack_damage", "more", 20, cond="class_resource_at_maximum"),
          R("spell_damage", "more", 20, cond="class_resource_at_maximum"),
          R("damage_taken", "more", 20, cond="class_resource_at_maximum")),
         (R("attack_damage", "more", 20, cond="class_resource_points_at_least", cond_value=100),
          R("spell_damage", "more", 20, cond="class_resource_points_at_least", cond_value=100))),
    node(M + "keystone_spine_002",
         (R("fervour_loss_suppressed", "flat", 1, tags="Keyword.Regeneration"),
          R("health_regen", "more", -50)),
         (R("fervour_floor_against_healing", "flat", 50),)),
    node(M + "basic_fc_stem0",
         (R("armor", "increased", 3),),
         (R("attack_damage", "increased", 2, tags="Type.Melee", cond="seconds_after_hit_taken",
            cond_value=4),),
         label="Provoked"),
    node(M + "basic_fc_stem1",
         (R("max_health", "increased", 1),),
         (R("max_health", "increased", 1), R("life_leech", "flat", 1, min_points=6))),
    node(M + "basic_fc_stem2",
         (R("retaliation", "increased", 3),),
         (R("retaliation", "increased", 1, scale="enemies_in_reach", step=1, reach=4),)),
    node(M + "basic_fc_a2",
         (R("attack_speed", "increased", 2, cond="while_bleeding"),),
         (R("attack_speed", "increased", 1, scale="bloodlust_stacks", step=1),)),
    node(M + "basic_fc_b0",
         (R("armor", "increased", 2, scale="debuffs_carried", step=1),),
         steps("life_leech", "flat", 1, every=2, most=8, cond="health_at_or_below",
               cond_value=50)),
    node(M + "basic_fc_b1",
         (R("life_leech", "increased", 3),),
         (R("life_leech", "increased", 5),)),
    node(M + "basic_fc_b2",
         (R("bleed_on_crit_chance", "flat", 5),),
         (R("attack_damage", "increased", 1, tags="Type.Melee", scale="life_leech", step=2),),
         label="Ravenous"),
    node(M + "basic_fc_c0",
         (R("fervour_from_damage", "increased", 2),),
         (R("fervour_on_enemy_death_nearby", "flat", 1),)),
    node(M + "basic_fc_c1",
         (R("crowd_control_resistance", "increased", 2),),
         (R("attack_speed", "increased", 3, cond="class_resource_points_at_least",
            cond_value=50),)),
    node(M + "keystone_fc_kC",
         (R("fervour_from_damage", "more", 30), R("fervour_from_cost", "more", -50)),
         (R("fervour_from_damage", "more", 30),)),
    node(M + "basic_fl_stem0",
         (R("dot_damage", "increased", 2),),
         (R("dot_damage", "increased", 3), R("burn_self_on_burning_an_enemy", "flat", 1))),
    node(M + "basic_fl_stem1",
         (R("dot_duration", "increased", 2),),
         (R("dot_duration", "increased", 4),)),
    node(M + "basic_fl_stem2",
         (R("max_health", "increased", 1),),
         (R("health_regen", "increased", 2, scale="debuffs_carried", step=1),)),
    node(M + "basic_fl_a0",
         (R("attack_damage", "increased", 1, scale="debuffs_carried", step=1),
          R("spell_damage", "increased", 1, scale="debuffs_carried", step=1),
          R("damage_reduction", "increased", 0.5, scale="debuffs_carried", step=1)),
         (R("attack_damage", "increased", 2, scale="debuffs_carried", step=1),
          R("spell_damage", "increased", 2, scale="debuffs_carried", step=1))),
    node(M + "basic_fl_a1",
         (R("damage_over_time_taken", "increased", 1),
          R("damage_to_enemies_sharing_a_debuff", "flat", 1)),
         (R("damage_to_enemies_sharing_a_debuff", "flat", 3),)),
    node(M + "basic_fl_a2",
         (R("debuff_duration_taken", "increased", 2),
          R("damage_over_time_taken", "increased", -1)),
         (R("debuff_duration_taken", "increased", 5),
          R("damage_over_time_taken", "increased", -2))),
    node(M + "basic_fl_b0",
         (R("debuff_spread_chance", "flat", 1),),
         (R("debuff_spread_chance", "flat", 5),)),
    node(M + "basic_fl_b1",
         (R("debuff_spread_on_death_chance", "flat", 2),),
         (R("debuff_spread_on_death_chance", "flat", 10),)),
    node(M + "basic_fl_b2",
         (R("aura_debuff_duration", "flat", 4),),
         (R("aura_debuff_duration", "flat", 4),
          R("attack_damage", "increased", 1, scale="target_debuffs", step=1),
          R("spell_damage", "increased", 1, scale="target_debuffs", step=1))),
    node(M + "basic_fl_c0",
         (R("fervour_from_damage", "increased", 2, tags="Keyword.DoT"),),
         (R("fervour_from_damage", "increased", 5, tags="Keyword.DoT"),)),
    node(M + "basic_fl_c1",
         (R("class_resource", "increased", 2),),
         (R("dot_damage", "increased", 3, cond="class_resource_points_at_least",
            cond_value=50),)),
    node(M + "keystone_fl_kA",
         (R("debuff_duration_taken", "increased", 50), R("damage_over_time_taken", "more", 50)),
         (R("debuff_duration_taken", "increased", 50),
          R("debuffs_carried_count_twice", "flat", 1))),
    node(M + "basic_ll_stem0",
         (R("movement_speed", "increased", 1),),
         (R("movement_speed", "increased", 1, scale="health_missing", step=10),)),
    node(M + "basic_ll_stem1",
         (R("crit_multiplier", "increased", 3),),
         (R("attack_damage", "increased", 2, cond="health_at_or_below", cond_value=50),
          R("spell_damage", "increased", 2, cond="health_at_or_below", cond_value=50))),
    node(M + "basic_ll_stem2",
         (R("retaliation", "increased", 2),),
         (R("retaliation", "flat", 1, scale="health_missing", step=25),)),
    node(M + "basic_ll_a1",
         (R("crit_chance", "increased", 3, cond="health_at_or_below", cond_value=20),),
         (R("damage_reduction", "flat", 1, cond="health_at_or_below", cond_value=35),)),
    node(M + "basic_ll_a2",
         (R("cooldown_skip_chance", "flat", 5, cond="health_at_or_below", cond_value=5),),
         (R("cooldown_skip_chance", "flat", 4, cond="health_at_or_below", cond_value=35),)),
    node(M + "basic_ll_b0",
         (R("movement_speed", "increased", 1, cond="health_at_or_below", cond_value=50),),
         (R("attack_speed", "increased", 3, cond="health_at_or_below", cond_value=50),)),
    node(M + "basic_ll_b1",
         (R("damage_to_bleeding_on_low_health", "flat", 1),
          R("damage_to_bleeding_window", "increased", 5)),
         (R("damage_to_bleeding_on_low_health", "flat", 1),)
         + steps("damage_to_bleeding_window", "flat", 1, every=2, most=8)),
    node(M + "basic_ll_b2",
         (R("nova_damage_of_missing_health", "flat", 1, cond="health_at_or_below",
            cond_value=10),),
         (R("nova_damage_of_weapon_damage", "flat", 10, cond="health_at_or_below",
            cond_value=50),)),
    node(M + "basic_ll_c0",
         (R("fervour_from_cost", "increased", 2),),
         steps("fervour_per_second", "flat", 1, every=2, most=8, cond="health_at_or_below",
               cond_value=50)),
    node(M + "basic_ll_c1",
         (R("class_resource", "increased", 2),),
         (R("fervour_lost_to_healing", "increased", -10, tags="Keyword.Regeneration",
            cond="health_at_or_below", cond_value=50),)),
    node(M + "basic_bt_stem0",
         (R("fervour_from_cost", "increased", 2),),
         steps("fervour_per_cast", "flat", 1, every=3, most=12)),
    # BLOOD PRICE, AT ITS PLACE AFTER THE ORDER FIX.
    node(M + "basic_bt_stem1", DEEPER_CUTS_ROWS,
         (R("fervour_from_cost", "increased", 5),),
         label="Blood Price", also_before=(BLOOD_PRICE_ROWS,)),
    node(M + "basic_bt_a2",
         (R("health_debt_delay_extension", "flat", 0.5),),
         steps("health_debt_delay_extension", "flat", 1, every=2, most=6)),
    # THE ORDER FIX'S OWN CHANGE: 10% BECOMES 2%.
    node(M + "basic_bt_b2",
         (R("attack_damage", "increased", 4, cond="skill_health_cost_above", cond_value=10),
          R("spell_damage", "increased", 4, cond="skill_health_cost_above", cond_value=10)),
         (R("attack_damage", "increased", 4, cond="skill_health_cost_above", cond_value=2),
          R("spell_damage", "increased", 4, cond="skill_health_cost_above", cond_value=2))),
    node(M + "basic_bt_c1",
         (R("class_resource", "increased", 2),),
         (R("attack_damage", "increased", 3, cond="class_resource_points_at_least",
            cond_value=50),
          R("spell_damage", "increased", 3, cond="class_resource_points_at_least",
            cond_value=50))),
    node(M + "keystone_bt_kB",
         (R("added_health_cost_of_current", "flat", 15), R("attack_damage", "more", 40),
          R("spell_damage", "more", 40)),
         (R("attack_damage", "more", 30, cond="skill_health_cost_above", cond_value=0),
          R("spell_damage", "more", 30, cond="skill_health_cost_above", cond_value=0))),
    node(M + "keystone_bt_kC",
         (R("fervour_from_cost", "more", 30), R("fervour_from_damage", "more", -50)),
         (R("fervour_from_cost", "more", 30),)),
    option(M + "capstone_25", 2,
           (R("retaliation_radius_metres", "flat", 4, option=2),),
           (R("retaliation_radius_metres", "flat", 4, option=2),
            R("retaliation_radius_metres", "flat", 1, scale="class_resource_held", step=25,
              option=2))),
    option(M + "capstone_50", 2,
           (R("retaliation_leeches", "flat", 1, option=2),),
           (R("retaliation_leeches", "flat", 1, option=2),
            R("life_leech", "increased", 1, scale="class_resource_held", step=2, option=2))),
    option(M + "capstone_100", 2,
           (R("retaliation", "increased", 1, scale="life_leech", step=1, option=2),),
           (R("attack_damage", "more", 1, scale="class_resource_held", step=10, option=2),
            R("spell_damage", "more", 1, scale="class_resource_held", step=10, option=2))),
    option(M + "capstone_100", 3,
           (R("attack_damage", "more", 1, scale="debuffs_carried", step=1, option=3),
            R("spell_damage", "more", 1, scale="debuffs_carried", step=1, option=3),
            R("damage_over_time_taken", "increased", -10, option=3)),
           (R("attack_damage", "more", 3, scale="debuffs_carried", step=1, option=3),
            R("spell_damage", "more", 3, scale="debuffs_carried", step=1, option=3),
            R("damage_over_time_taken", "increased", -10, option=3))),
    option(M + "capstone_200", 2,
           (R("carnage_from_damage_taken", "flat", 1, option=2),
            R("carnage_has_no_maximum", "flat", 1, option=2),
            R("attack_damage", "more", 2, scale="carnage_stacks", step=1, option=2),
            R("spell_damage", "more", 2, scale="carnage_stacks", step=1, option=2)),
           (R("carnage_from_damage_taken", "flat", 1, option=2),
            R("carnage_has_no_maximum", "flat", 1, option=2),
            R("attack_damage", "more", 2, scale="carnage_stacks", step=1, option=2),
            R("spell_damage", "more", 2, scale="carnage_stacks", step=1, option=2),
            R("carnage_stack_seconds", "flat", 1, scale="class_resource_held", step=10,
              option=2))),
)


# --------------------------------------------------------------------------
# The Ravager
# --------------------------------------------------------------------------

V = "Ravager_"
CROWD = {"cond": "enemies_in_reach_at_least", "cond_value": 3, "reach": 4}
FIFTY = {"cond": "class_resource_points_at_least", "cond_value": 50}

RAVAGER = (
    node(V + "basic_spine_001",
         (R("armor", "increased", 1), R("max_health", "increased", 0.5)),
         (R("armor", "increased", 1), R("max_health", "increased", 1))),
    node(V + "basic_spine_006",
         (R("damage_reduction", "increased", 1),
          R("knockback_suppressed", "flat", 1, cond="enemies_in_reach_at_least", cond_value=1,
            reach=4, min_points=4)),
         (R("damage_reduction", "flat", 1),
          R("knockback_suppressed", "flat", 1, cond="enemies_in_reach_at_least", cond_value=1,
            reach=4, min_points=4))),
    node(V + "basic_spine_008",
         (R("armor", "increased", 1.5), R("crowd_control_resistance", "increased", 5,
                                            min_points=4)),
         (R("armor", "increased", 2), R("crowd_control_resistance", "flat", 10, min_points=4))),
    node(V + "basic_spine_009",
         (R("class_resource", "increased", 2),),
         (R("fervour_decay_grace_seconds", "flat", 1),)),
    node(V + "keystone_spine_001",
         (R("armor_penetration_suppressed", "flat", 1),),
         (R("armor_penetration_suppressed", "flat", 1), R("armor_overwhelm_halved", "flat", 1))),
    node(V + "basic_a_a0",
         (R("armor", "increased", 1.5),),
         (R("armor", "increased", 1), R("armor", "increased", 1, **CROWD))),
    node(V + "basic_a_a1",
         (R("life_leech", "increased", 1),),
         (R("life_leech", "flat", 1, **CROWD),)),
    node(V + "basic_a_a2",
         (R("health_regen", "increased", 2),),
         (R("damage_reduction", "flat", 1, **CROWD),)),
    node(V + "basic_a_b0",
         (R("damage_reduction", "increased", 1),),
         (R("armor", "increased", 1, scale="class_resource_held", step=20),)),
    node(V + "basic_a_b1",
         (R("damage_reduction", "increased", 2, **FIFTY), R("attack_damage", "increased", 3, **FIFTY)),
         (R("damage_reduction", "flat", 1, **FIFTY), R("attack_damage", "increased", 3, **FIFTY))),
    node(V + "basic_a_b2",
         (R("health_regen", "increased", 2),),
         (R("health_restored_on_fervour_full", "flat", 2),)),
    node(V + "keystone_a_kB",
         (R("crowd_control_resistance", "flat", 50),
          R("crowd_control_ends_when_its_applier_dies", "flat", 1)),
         (R("crowd_control_resistance", "flat", 50),
          R("crowd_control_ends_when_its_applier_dies", "flat", 1),
          R("crowd_control_resistance", "flat", 100, **FIFTY),
          R("movement_speed_reduction_suppressed", "flat", 1, **FIFTY))),
    node(V + "basic_a_stem2",
         (R("crowd_control_resistance", "increased", 3),),
         (R("max_health", "increased", 3),)),
    node(V + "basic_a_c0",
         (R("crowd_control_resistance", "increased", 3),),
         (R("crowd_control_resistance", "flat", 5),)),
    node(V + "basic_a_c1",
         (R("class_resource", "increased", 2),),
         (R("max_health", "increased", 2), R("armor", "increased", 2))),
    node(V + "keystone_a_kC",
         (R("armor", "flat", 1, scale="max_health", step=10),),
         (R("armor", "flat", 1, scale="max_health", step=4),)),
    node(V + "basic_b_a0",
         (R("area_of_effect", "increased", 2),),
         (R("area_of_effect", "increased", 1, scale="enemies_in_reach", step=1, reach=4),)),
    node(V + "basic_b_a1",
         (R("attack_damage", "increased", 2),),
         (R("armor_penetration", "flat", 2, tags="Type.Melee"),)),
    node(V + "basic_b_b1",
         (R("area_of_effect", "increased", 2),),
         (R("fervour_on_enemy_death_nearby", "flat", 1),)),
    node(V + "basic_b_c0",
         (R("attack_damage", "increased", 2),),
         (R("cooldown_reduction", "flat", 3, tags="Slot.Heavy"),)),
    node(V + "basic_b_c1",
         (R("attack_speed", "increased", 2),),
         (R("area_of_effect", "increased", 3, tags="Slot.Heavy"),
          R("strike_arc_at_least_degrees", "flat", 180, tags="Slot.Heavy", min_points=6))),
    node(V + "basic_c_stem2",
         (R("cripple_and_weaken_duration", "increased", 3),),
         (R("cripple_and_weaken_spread_on_death_metres", "flat", 1),)),
    node(V + "basic_c_a1",
         (R("cripple_magnitude", "increased", 3),),
         (R("cripple_magnitude", "increased", 5),)),
    node(V + "basic_c_b1",
         (R("weaken_magnitude", "increased", 3),),
         (R("weaken_magnitude", "increased", 5),)),
    node(V + "basic_c_b2",
         (R("damage_reduction", "increased", 2, cond="opponent_carries_weaken"),),
         (R("damage_reduction", "flat", 1, cond="opponent_carries_weaken"),)),
    node(V + "basic_d_stem0",
         (R("movement_speed", "increased", 1),),
         (R("movement_speed", "increased", 1),
          R("movement_speed", "increased", 1, cond="out_of_combat"))),
    node(V + "basic_d_stem1",
         (R("movement_speed", "increased", 1), R("attack_speed", "increased", 1)),
         (R("attack_speed", "increased", 2, cond="seconds_after_movement_skill",
            cond_value=4),)),
    node(V + "basic_d_stem2",
         (R("life_leech", "increased", 1),),
         (R("life_leech", "flat", 1),)),
    node(V + "basic_d_a0",
         (R("attack_damage", "increased", 2, cond="enemies_in_reach_at_least", cond_value=1,
            reach=4),),
         (R("armor_penetration", "flat", 5, cond="target_not_yet_struck_by_you"),)),
    node(V + "basic_d_a1",
         (R("movement_speed", "increased", 1),),
         (R("movement_speed", "increased", 3, cond="class_resource_at_maximum"),)),
    node(V + "keystone_d_kA",
         (R("movement_speed_reduction_suppressed", "flat", 1),),
         (R("movement_speed_reduction_suppressed", "flat", 1),
          R("attack_speed_reduction_suppressed", "flat", 1),
          R("attack_speed", "increased", 1, scale="increased_movement_speed", step=2))),
    node(V + "basic_d_b0",
         (R("fervour_per_enemy_in_reach", "increased", 2),),
         (R("class_resource_generation", "increased", 5),)),
    node(V + "basic_d_b1",
         (R("class_resource", "increased", 2),),
         (R("class_resource", "flat", 5),)),
    node(V + "basic_d_b2",
         (R("damage_reduction", "increased", 1, scale="class_resource_held", step=25),),
         (R("attack_speed", "increased", 1, scale="class_resource_held", step=20),)),
    node(V + "basic_d_c0",
         (R("life_leech", "increased", 1),),
         (R("attack_speed", "increased", 3, cond="in_combat_for_seconds", cond_value=5),)),
    node(V + "keystone_d_kC",
         (R("fervour_decay_grace_metres", "flat", 4),),
         (R("fervour_decay_per_second", "removed", 1),)),
    option(V + "capstone_25", 1,
           (R("armor", "increased", 3, scale="enemies_in_reach", step=1, option=1, reach=4),
            R("attack_damage", "increased", 3, scale="enemies_in_reach", step=1, option=1,
              reach=4)),
           (R("armor", "increased", 3, scale="enemies_in_reach", step=1, option=1, reach=4),
            R("attack_damage", "more", 2, scale="enemies_in_reach", step=1, option=1,
              reach=4))),
    option(V + "capstone_25", 2,
           (R("cripple_chance", "flat", 100, option=2),
            R("attack_damage", "increased", 20, cond="target_carries_cripple", option=2)),
           (R("cripple_chance", "flat", 100, option=2),
            R("attack_damage", "more", 20, cond="target_carries_cripple", option=2))),
    option(V + "capstone_25", 3,
           (R("movement_speed", "increased", 25, option=3),
            R("attack_damage", "increased", 50, tags="Type.Melee",
              cond="metres_moved_before_attack", cond_value=5, option=3)),
           (R("movement_speed", "increased", 25, option=3),
            R("attack_damage", "more", 50, tags="Type.Melee",
              cond="metres_moved_before_attack", cond_value=5, option=3))),
    option(V + "capstone_50", 3,
           (R("health_restored_on_kill_at_no_cost", "flat", 5, option=3),),
           (R("health_restored_on_kill_at_no_cost", "flat", 1, scale="enemies_in_reach", step=1,
              option=3, reach=4),)),
    option(V + "capstone_100", 2,
           (R("attack_damage", "increased", 1, scale="damage_reduction", step=2, option=2),),
           (R("ultimate_fervour_cost_reduction", "flat", 5, scale="enemies_in_reach", step=1,
              option=2, reach=4),),
           label="Thronged"),
    option(V + "capstone_200", 1,
           (R("enemies_cannot_move_away_within_metres", "flat", 8, option=1),),
           (R("enemies_cannot_move_away_within_metres", "flat", 8, option=1),
            R("attack_damage", "more", 2, scale="enemies_in_reach", step=1, option=1, reach=8),
            R("spell_damage", "more", 2, scale="enemies_in_reach", step=1, option=1, reach=8))),
)


# --------------------------------------------------------------------------
# The Ritualist
# --------------------------------------------------------------------------

T = "Ritualist_"

RITUALIST = (
    node(T + "basic_spine_002",
         (R("spell_damage", "increased", 2),),
         (R("spell_damage", "increased", 2), R("debuff_duration", "increased", 25,
                                                min_points=3))),
    node(T + "basic_spine_003",
         (R("fervour_from_minions", "increased", 2), R("fervour_on_minion_death", "increased", 2)),
         (R("spell_damage", "increased", 1, scale="class_resource_held", step=25),)),
    node(T + "basic_a_stem0",
         (R("spell_damage", "increased", 2),),
         (R("spell_damage", "increased", 2),
          R("debuffed_enemies_deal_less_percent", "flat", 10, min_points=4))),
    node(T + "basic_a_stem1",
         (R("spell_damage", "increased", 2, cond="target_health_below", cond_value=50),),
         (R("spell_damage", "increased", 2, cond="target_health_below", cond_value=50),
          R("debuff_spread_on_death_chance", "flat", 50, min_points=4))),
    node(T + "basic_a_a0",
         (R("spell_damage", "increased", 2),),
         (R("spell_damage", "increased", 2),
          R("spell_damage", "increased", 20, cond="target_carries_your_curse", min_points=6))),
    node(T + "basic_a_a2",
         (R("minion_damage", "increased", 2),),
         (R("minion_damage", "increased", 2),
          R("minion_damage", "increased", 20, cond="target_carries_your_curse", min_points=4))),
    # THE SWAP: the explosion node is now Every One Bursts' parent, and Legion
    # takes the place Volatile left.
    node(T + "basic_b_a2",
         (R("minion_explosion_damage", "increased", 3),),
         (R("minion_damage", "increased", 1, scale="minions_held", step=1),),
         label="Legion"),
    node(T + "basic_b_b2",
         (R("dot_damage", "increased", 3),),
         (R("minion_explosion_damage", "increased", 3),),
         label="Volatile"),
    node(T + "basic_b_a1",
         (R("area_of_effect", "increased", 3),),
         (R("minion_damage", "increased", 3, cond="target_health_below", cond_value=50),)),
    node(T + "keystone_b_kA",
         (R("minion_reserve_percent_less", "flat", 12),),
         # A STAT OF ITS OWN, NOT THE REAPER'S EMBRACE'S `minion_kill_spawns_minion`,
         # which was ruled on 2026-10-19 to answer the wearer's own kills only and
         # to give nothing without a summon skill. This one answers a minion's
         # kill too, and summons Summon Imp's kind with no summon skill held.
         (R("kill_summons_minion_where_it_fell", "flat", 1),)),
    node(T + "keystone_b_kC",
         (R("minion_explosion_replaced_every_seconds", "flat", 5),),
         (R("minion_lifetime_renewed_every_seconds", "flat", 5),)),
    node(T + "basic_c_stem2",
         (R("movement_speed", "increased", 1),),
         (R("spell_damage", "increased", 1, scale="metres_to_target", step=2),)),
    node(T + "basic_c_c0",
         (R("crowd_control_resistance", "increased", 2),),
         (R("crowd_control_resistance", "flat", 2),)),
    node(T + "basic_c_b1",
         (R("spell_damage", "increased", 2),),
         (R("spell_damage", "increased", 2),
          R("energy_shield_leech", "flat", 2, tags="Type.Spell", min_points=4))),
    node(T + "keystone_d_kB",
         (R("mana_cost", "removed", 1, cond="while_stationary"),),
         (R("mana_cost", "removed", 1, cond="while_stationary"),
          R("area_of_effect", "increased", 30, cond="while_stationary"))),
    node(T + "basic_d_b0",
         (R("spell_damage", "increased", 2),),
         (R("spell_damage", "increased", 2),
          R("minion_damage", "increased", 20, cond="stationary_for_seconds", cond_value=2,
            min_points=4))),
    option(T + "capstone_25", 1,
           (R("minion_damage", "increased", 25, option=1),
            R("minion_health", "increased", 25, option=1)),
           (R("minion_damage", "increased", 5, scale="minions_held", step=1, option=1),
            R("minion_health", "increased", 5, scale="minions_held", step=1, option=1))),
    option(T + "capstone_25", 2,
           (R("max_energy_shield", "increased", 40, option=2),
            R("energy_shield_regen", "increased", 20, option=2)),
           (R("energy_shield_on_minion_death_percent", "flat", 10, option=2),)),
    option(T + "capstone_25", 3,
           (R("class_resource", "increased", 20, option=3), R("mana_regen", "increased", 20,
                                                               option=3)),
           (R("curse_death_extra_imps", "flat", 1, option=3),)),
    option(T + "capstone_50", 2,
           (R("fervour_on_enemy_death_nearby", "flat", 10, option=2),),
           (R("damage_taken", "more", -1, scale="minions_held", step=1, option=2, cap=20),),
           label="Wall of Servants"),
    option(T + "capstone_100", 1,
           (R("minion_damage", "more", 25, cond="target_damaged_by_you_within_seconds",
              cond_value=2, option=1),),
           (R("minion_damage", "more", 3, cond="target_damaged_by_you_within_seconds",
              cond_value=2, scale="minions_held", step=1, option=1),)),
)

CHANGES: tuple[Change, ...] = MASOCHIST + RAVAGER + RITUALIST


# --------------------------------------------------------------------------
# Reading and writing the sheet
# --------------------------------------------------------------------------

def _number(value):
    if value is None or (isinstance(value, str) and not value.strip()):
        return None
    number = float(value)
    return int(number) if number.is_integer() else number


def _text(value):
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def row_from_cells(cells: dict[str, object]) -> Row:
    option_value = _number(cells.get("Option"))
    return Row(stat=_text(cells.get("Stat")), kind=(_text(cells.get("Value Kind")) or "").lower(),
               value=_number(cells.get("Value Per Point")) or 0,
               tags=_text(cells.get("Required Tags")), cond=_text(cells.get("Condition")),
               cond_value=_number(cells.get("Condition Value")),
               scale=_text(cells.get("Scale")), step=_number(cells.get("Scale Step")),
               option=option_value or None, reach=_number(cells.get("Reach Metres")),
               min_points=_number(cells.get("Min Points")) or None,
               cap=_number(cells.get(CAP_COLUMN)) or None)


def cells_from_row(node_key: str, label: str | None, row: Row) -> dict[str, object]:
    def out(value):
        if value is None:
            return None
        if isinstance(value, float) and value.is_integer():
            return int(value)
        return value
    return {"Node": node_key, "Stat": row.stat, "Value Kind": row.kind,
            "Value Per Point": out(row.value), "Required Tags": row.tags,
            "Condition": row.cond, "Condition Value": out(row.cond_value),
            "Scale": row.scale, "Scale Step": out(row.step), "Option": out(row.option),
            "Node Name": label, "Reach Metres": out(row.reach),
            "Min Points": out(row.min_points), CAP_COLUMN: out(row.cap)}


def _multiset(rows) -> collections.Counter:
    return collections.Counter(row.key() for row in rows)


def refuse_what_a_save_would_lose(book) -> None:
    formulas = sum(1 for sheet in book.worksheets for row in sheet.iter_rows() for cell in row
                   if isinstance(cell.value, str) and cell.value.startswith("="))
    charts = sum(len(sheet._charts) for sheet in book.worksheets)
    images = sum(len(sheet._images) for sheet in book.worksheets)
    if formulas or charts or images:
        raise ReapplyError(f"the workbook holds {formulas} formula(s), {charts} chart(s) and "
                           f"{images} image(s), which an openpyxl save can lose without saying "
                           f"so; make the edit by hand in Excel instead")


def _snapshot(sheet) -> dict:
    return {(cell.row, cell.column): cell.value for row in sheet.iter_rows() for cell in row
            if cell.value is not None}


@dataclasses.dataclass
class Plan:
    header: list[str]
    add_cap_column: bool
    old: list[dict[str, object]]
    new: list[dict[str, object]]
    report: list[str]
    changed: int


def plan(sheet, changes: tuple[Change, ...] = CHANGES) -> Plan:
    """Work out the sheet's new rows. Raises ReapplyError on a row in an unknown state."""
    header = [_text(cell.value) for cell in sheet[1]]
    while header and header[-1] is None:
        header.pop()
    missing = [c for c in COLUMNS if c not in header and c != CAP_COLUMN]
    if missing:
        raise ReapplyError(f"{SHEET!r} has no column(s) {missing}; its header is {header}")
    if len(set(header)) != len(header):
        raise ReapplyError(f"{SHEET!r} names a column twice: {header}")

    old: list[dict[str, object]] = []
    for values in sheet.iter_rows(min_row=2, max_col=len(header), values_only=True):
        cells = dict(zip(header, values, strict=False))
        if all(v is None or (isinstance(v, str) and not v.strip()) for v in values):
            cells = {}
        old.append(cells)
    # TRAILING BLANK ROWS ARE NOT ROWS. openpyxl reports a sheet's used range, which
    # can run past the last row anybody wrote.
    while old and not old[-1]:
        old.pop()

    def group_of(change: Change) -> list[int]:
        found = []
        for index, cells in enumerate(old):
            if _text(cells.get("Node")) != change.node:
                continue
            number = _number(cells.get("Option")) or 0
            if number == change.option:
                found.append(index)
        return found

    report: list[str] = []
    replace: dict[int, list[dict[str, object]]] = {}
    drop: set[int] = set()
    relabel: dict[int, str] = {}
    changed = 0
    seen: set[tuple[str, int]] = set()
    for change in changes:
        if (change.node, change.option) in seen:
            raise ReapplyError(f"{change.node} option {change.option} is changed twice")
        seen.add((change.node, change.option))
        where = f"{change.node}" + (f" option {change.option}" if change.option else "")
        indices = group_of(change)
        if not indices:
            raise ReapplyError(f"{where} has no rows on {SHEET!r}; the rework was approved "
                               f"against rows that are not there")
        current = [row_from_cells(old[i]) for i in indices]
        labels = {_text(old[i].get("Node Name")) for i in indices}
        label = change.label or (labels.pop() if len(labels) == 1 else None)
        if _multiset(current) == _multiset(change.after):
            stale = [i for i in indices if change.label
                     and _text(old[i].get("Node Name")) != change.label]
            for i in stale:
                relabel[i] = change.label
            if stale:
                changed += 1
                report.append(f"relabelled {where} as {change.label!r} (sheet rows "
                              f"{', '.join(str(i + 2) for i in stale)})")
            continue
        if not any(_multiset(current) == _multiset(state) for state in change.before):
            raise ReapplyError(
                f"{where} holds {[r.key() for r in current]}, which is neither the rows the "
                f"rework was approved against nor the rows it writes; somebody has changed "
                f"them since")
        changed += 1
        replace[indices[0]] = [cells_from_row(change.node, label, row) for row in change.after]
        drop.update(indices[1:])
        report.append(f"{where} (sheet rows {', '.join(str(i + 2) for i in indices)}): "
                      f"{len(current)} row(s) out, {len(change.after)} in")
        for row in current:
            report.append(f"    - {_describe(row)}")
        for row in change.after:
            report.append(f"    + {_describe(row)}")

    new: list[dict[str, object]] = []
    for index, cells in enumerate(old):
        if index in replace:
            new.extend(replace[index])
        elif index in drop:
            continue
        else:
            cells = dict(cells)
            if index in relabel:
                cells["Node Name"] = relabel[index]
            new.append(cells)

    needs_cap = any(cells.get(CAP_COLUMN) not in (None, "", 0) for cells in new)
    add_cap_column = needs_cap and CAP_COLUMN not in header
    return Plan(header, add_cap_column, old, new, report, changed)


def _describe(row: Row) -> str:
    parts = [row.stat, row.kind, f"{row.value:g}"]
    for name in ("tags", "cond", "cond_value", "scale", "step", "option", "reach",
                 "min_points", "cap"):
        value = getattr(row, name)
        if value is not None:
            parts.append(f"{name}={value:g}" if isinstance(value, (int, float))
                         else f"{name}={value}")
    return " | ".join(parts)


def write(sheet, the_plan: Plan) -> list[str]:
    header = list(the_plan.header)
    if the_plan.add_cap_column:
        header.append(CAP_COLUMN)
        sheet.cell(1, len(header)).value = CAP_COLUMN
    width = len(header)
    for offset, cells in enumerate(the_plan.new):
        for column, name in enumerate(header, start=1):
            sheet.cell(offset + 2, column).value = cells.get(name)
    surplus = len(the_plan.old) - len(the_plan.new)
    if surplus > 0:
        sheet.delete_rows(len(the_plan.new) + 2, surplus)
    return header[:width]


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Rewrite the rows in the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    refuse_what_a_save_would_lose(book)
    sheet = book[SHEET]
    the_plan = plan(sheet)
    if not the_plan.changed and not the_plan.add_cap_column:
        return ["every row is already as the rework writes it; nothing written"]
    report = list(the_plan.report)
    if the_plan.add_cap_column:
        report.append(f"added the column {CAP_COLUMN!r} to {SHEET!r}")
    if check_only:
        return report + [f"{the_plan.changed} change(s) to make; nothing written (--check)"]

    others = {s.title: _snapshot(s) for s in book.worksheets if s.title != SHEET}
    header = write(sheet, the_plan)
    book.save(path)

    # READ BACK FROM DISK: the other sheets as they were, and this one exactly as planned.
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if _snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and only {SHEET!r} "
                               f"was written")
    written = after[SHEET]
    if [_text(c.value) for c in written[1]][:len(header)] != header:
        raise ReapplyError(f"{SHEET!r}'s header did not read back as {header}")
    rows = [dict(zip(header, values, strict=False))
            for values in written.iter_rows(min_row=2, max_col=len(header), values_only=True)]
    while rows and all(v is None for v in rows[-1].values()):
        rows.pop()
    intended = [{name: cells.get(name) for name in header} for cells in the_plan.new]
    if rows != intended:
        first = next(i for i, (a, b) in enumerate(zip(rows, intended, strict=False)) if a != b) \
            if len(rows) == len(intended) else min(len(rows), len(intended))
        raise ReapplyError(f"{SHEET!r} did not read back as intended: first difference at "
                           f"sheet row {first + 2}")
    return report + [f"wrote {the_plan.changed} change(s); {SHEET!r} now holds "
                     f"{len(the_plan.new)} rows (was {len(the_plan.old)}), and no other sheet "
                     f"changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true",
                        help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if any(line.endswith("(--check)") for line in report) else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)],
                            cwd=REPO_ROOT)
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py "
          "tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
