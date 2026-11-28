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


#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
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
