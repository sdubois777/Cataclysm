"""Write the rows of the War vow options' fifth slice, first half, V5a, into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v5a_workbook.py
    python tools/reapply_war_vows_v5a_workbook.py --check

THE WAR VOW OPTIONS' FIFTH SLICE, FIRST HALF, V5a (2026-11-25), on the coordinating session's rulings on the Saboteur's
C options that day. Four options, five rows:
  - Lightning Rod (Saboteur First Oath, option 3): `blow_to_device_chance_per_device` 3, at most 30% (the engine's
    bound), the nearest device within 10 metres taking the blow.
  - Creeping Mines (Saboteur Third Oath, option 1): `trap_creep_metres_per_second` 3 and `trap_creep_reach_metres` 10;
    skill traps and War traps, in quarter-second steps.
  - Where They Fall (Saboteur Final Oath, option 1): `copy_last_trap_at_kill` on `kill`, a copy lasting 4 seconds
    when a device stands within 10 metres of the enemy killed; it reserves 1 Fervour (the engine's figure).
  - Crossfire (Saboteur Final Oath, option 3): `crossfire` on `hit_dealt`, 10% of what the hit sent from each device
    within 15 metres of the enemy, at most 15 of them (the engine's figure).

The Final Oath already holds Magnum Opus's row (option 2, `tools/reapply_war_vows_v4b_workbook.py`); a group is one
option of one node, so neither script reads the other's.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' fifth slice, first half (V5a)"

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_25", "First Oath", (
        R("blow_to_device_chance_per_device", "flat", 3, option=3),)),
    ("Saboteur_capstone_100", "Third Oath", (
        R("trap_creep_metres_per_second", "flat", 3, option=1),
        R("trap_creep_reach_metres", "flat", 10, option=1))),
    ("Saboteur_capstone_200", "Final Oath", (
        R(None, "", 4, action="copy_last_trap_at_kill", action_event="kill", event_value=10, option=1),
        R(None, "", 10, action="crossfire", action_event="hit_dealt", event_value=15, option=3))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
