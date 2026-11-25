"""Write the Saboteur's rows that need no new code into the design workbook's Passive Effects sheet.

    python tools/reapply_war_saboteur_rows_workbook.py
    python tools/reapply_war_saboteur_rows_workbook.py --check

THE SURVEY'S GROUPS A AND B (2026-11-25), on the coordinating session's rulings of that day. Sixteen nodes, each on a
stat, scale, condition or action the game already reads; where a node has a clause that needs a mechanism, only the
rest is written here and the clause waits for its own slice (named below).

  - Durable Modifications: damage reduction "more", EXACTLY DOUBLED by the ruling on question 4: 3% a point at or
    below 50% of the maximum Fervour (`class_resource_at_or_below`, new); 3% a point above 50% at 1 to 3 points (Max
    Points 4, new, exclusive); 6% a point above 50% from 4 points. The three never hold together, so no two multiply.

  - Tripwire Network: `fervour_per_trap_triggered` 1 a point.
  - Hair Trigger, Hair Wire: `trap_trigger_radius` 10% a point. (Their 5- and 4-point clauses, "arm instantly" and
    "trigger 20% faster after detecting an enemy", wait for the C1 plan.)
  - Trapsmith: `trap_arming_speed` 6% a point. (Its 4-point clause waits for the C1 plan.)
  - Volatile Mixture: `area_of_effect` 3% a point on `Type.Trap`. ("and explosive" is read as the traps' own blasts, a
    judgement; its 5-point burning patch waits for C3.)
  - Tinkerer: 3% attack and spell damage a point on `Type.Gadget`; from 7 points `minion_duration` 20% a point on
    `Type.Gadget`, counted from the seventh ("per point invested beyond 6"). A skill gadget has no lifetime, so the
    duration reaches War gadgets only.
  - Calculated: `class_resource` 5 a point. (Its 6-point clause waits for the C1/C5 plan's search.)
  - Slippery: `evasion` 1 a point. (Its 5-point clause, 3 Fervour on an evade, waits for C2.)
  - Ghost Step: `movement_speed` 1% a point; from 5 points 2% a point more, counted from the first, while above 50% of
    the maximum Fervour.
  - Reinforced Frame: `minion_health` 8% a point on `Type.Gadget`. (Its 5-point clause waits for C2.)
  - Death Ground: `dot_damage` 3% a point on `Type.Trap`; a trap's cast carries `Type.Trap`, and a damage over time is
    asked with its skill's tags. (Its 6-point lingering area waits for C3.)
  - Pressure Sensitive: a 20% slow a point for 2 seconds on `trap_triggered` (`slow_for_seconds`, as the trap
    enchantment writes it, so the rule of a tenth governs it, the ruling on question 5; the game holds a slow to
    Cripple's cap of 80%); from 4 points 15% increased trap damage (`minion_damage` on `Type.Trap`) against a crippled
    target, a judgement: any Cripple, not only one a trap laid.
  - Mechanical Familiar: 15% less Fervour reserved by gadgets (`Type.Gadget`); 1% attack and spell damage and 5%
    `minion_health` on `Type.Gadget` for every 10 Fervour held.
  - Bag of Tricks: 4% less reserved by traps and 10% less by gadgets, a point.
  - Minefield: from 6 points 5% less reserved by traps. (Its placement speed waits for the C1 plan.)

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
TRAP = "Type.Trap"
GADGET = "Type.Gadget"
LABEL = "the Saboteur's rows"
HELD = {"scale": "class_resource_held", "step": 10}
RESERVE = "minion_reserve_percent_less"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_basic_trap_stem_005", "Tripwire Network", (R("fervour_per_trap_triggered", "flat", 1),)),
    ("Saboteur_basic_trunk_001", "Hair Trigger", (R("trap_trigger_radius", "increased", 10),)),
    ("Saboteur_basic_trap_stem_002", "Hair Wire", (R("trap_trigger_radius", "increased", 10),)),
    ("Saboteur_basic_trap_stem_001", "Trapsmith", (R("trap_arming_speed", "increased", 6),)),
    ("Saboteur_basic_trunk_007", "Volatile Mixture", (R("area_of_effect", "increased", 3, tags=TRAP),)),
    ("Saboteur_basic_trunk_008", "Tinkerer", (
        R("attack_damage", "increased", 3, tags=GADGET),
        R("spell_damage", "increased", 3, tags=GADGET),
        R("minion_duration", "increased", 20, tags=GADGET, min_points=7, per_point_from=7))),
    ("Saboteur_basic_trunk_003", "Calculated", (R("class_resource", "flat", 5),)),
    ("Saboteur_basic_trunk_004", "Slippery", (R("evasion", "flat", 1),)),
    ("Saboteur_basic_trunk_011", "Ghost Step", (
        R("movement_speed", "increased", 1),
        R("movement_speed", "increased", 2, cond="class_resource_above", cond_value=50, min_points=5,
          per_point_from=1))),
    ("Saboteur_basic_gadget_stem_002", "Reinforced Frame", (R("minion_health", "increased", 8, tags=GADGET),)),
    ("Saboteur_basic_trap_deep_002", "Death Ground", (R("dot_damage", "increased", 3, tags=TRAP),)),
    ("Saboteur_basic_trap_mid_001", "Pressure Sensitive", (
        # AN ACTION ROW STATES NO STAT AND NO VALUE KIND; the kind is the empty text `row_from_cells` reads back.
        R(None, "", 20, tags=TRAP, action="slow_for_seconds", action_event="trap_triggered", stack_seconds=2),
        R("minion_damage", "increased", 15, tags=TRAP, cond="target_carries_cripple", min_points=4))),
    ("Saboteur_keystone_gadget_deep_001", "Mechanical Familiar", (
        R(RESERVE, "flat", 15, tags=GADGET),
        R("attack_damage", "increased", 1, tags=GADGET, **HELD),
        R("spell_damage", "increased", 1, tags=GADGET, **HELD),
        R("minion_health", "increased", 5, tags=GADGET, **HELD))),
    ("Saboteur_basic_trunk_006", "Bag of Tricks", (
        R(RESERVE, "flat", 4, tags=TRAP),
        R(RESERVE, "flat", 10, tags=GADGET))),
    ("Saboteur_basic_trap_qty_001", "Minefield", (R(RESERVE, "flat", 5, tags=TRAP, min_points=6),)),
    ("Saboteur_basic_trunk_014", "Durable Modifications", (
        R("damage_reduction", "more", 3, cond="class_resource_at_or_below", cond_value=50),
        R("damage_reduction", "more", 3, cond="class_resource_above", cond_value=50, max_points=4),
        R("damage_reduction", "more", 6, cond="class_resource_above", cond_value=50, min_points=4, per_point_from=1))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
