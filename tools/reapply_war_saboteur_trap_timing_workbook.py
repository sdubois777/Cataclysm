"""Write the rows of the Saboteur's trap timing into the design workbook's Passive Effects sheet.

    python tools/reapply_war_saboteur_trap_timing_workbook.py
    python tools/reapply_war_saboteur_trap_timing_workbook.py --check

THE C1 PLAN (2026-11-25), accepted as written by the coordinating session that day. Six nodes, every row of each:

  - Hair Trigger (`Saboteur_basic_trunk_001`): `trap_trigger_radius` 10% a point; from 5 points `traps_arm_instantly`,
    a flag read by `ACataclysmMinion::SkillTrapArmingSecondsFor`.
  - Hair Wire (`Saboteur_basic_trap_stem_002`): `trap_trigger_radius` 10% a point; from 4 points `trap_arming_speed`
    20%, the ruled rewording of "Traps trigger 20% faster after detecting an enemy" to "Your traps arm 20% faster".
  - Volatile Casing (`Saboteur_basic_trap_stem_003`): `trap_arming_seconds_less_per_other_trap` 0.1 a point, on the
    ruled rewording of "trigger delay" to "arming delay".
  - Minefield (`Saboteur_basic_trap_qty_001`): `trap_arming_speed` 10% a point, the ruled rewording of "trap placement
    speed"; from 6 points 5% less reserved by traps.
  - Field Engineer (`Saboteur_basic_gadget_stem_001`): `minion_attack_speed` 12% a point on `Type.Gadget`, the ruled
    rewording of "gadget deployment speed". Its 4-point clause is already true and has no row.
  - Delayed Fuse (`Saboteur_basic_trap_ql_003`): `minion_damage` 15% a point on `Type.Trap`; from the first point
    `trap_arming_seconds_added` 1 and `traps_not_set_off_while_arming`, a flag.

HAIR TRIGGER, HAIR WIRE AND MINEFIELD HELD ROWS FROM `tools/reapply_war_saboteur_rows_workbook.py`, which no longer
lists them: this script names them in `EXTENDING`, so the rows they hold are kept and only the rest are appended.

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
LABEL = "the Saboteur's trap timing"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_basic_trunk_001", "Hair Trigger", (
        R("trap_trigger_radius", "increased", 10),
        R("traps_arm_instantly", "flat", 1, min_points=5))),
    ("Saboteur_basic_trap_stem_002", "Hair Wire", (
        R("trap_trigger_radius", "increased", 10),
        R("trap_arming_speed", "increased", 20, min_points=4))),
    ("Saboteur_basic_trap_stem_003", "Volatile Casing", (
        R("trap_arming_seconds_less_per_other_trap", "flat", 0.1),)),
    ("Saboteur_basic_trap_qty_001", "Minefield", (
        R("trap_arming_speed", "increased", 10),
        R("minion_reserve_percent_less", "flat", 5, tags=TRAP, min_points=6))),
    ("Saboteur_basic_gadget_stem_001", "Field Engineer", (
        R("minion_attack_speed", "increased", 12, tags=GADGET),)),
    ("Saboteur_basic_trap_ql_003", "Delayed Fuse", (
        R("minion_damage", "increased", 15, tags=TRAP),
        R("trap_arming_seconds_added", "flat", 1, min_points=1),
        R("traps_not_set_off_while_arming", "flat", 1, min_points=1))),
)

#: The nodes `tools/reapply_war_saboteur_rows_workbook.py` wrote rows on before this script took them over.
EXTENDING = frozenset({"Saboteur_basic_trunk_001", "Saboteur_basic_trap_stem_002", "Saboteur_basic_trap_qty_001"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
