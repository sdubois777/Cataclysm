"""Write the rows of the Saboteur's device conditions into the design workbook's Passive Effects sheet.

    python tools/reapply_war_saboteur_devices_workbook.py
    python tools/reapply_war_saboteur_devices_workbook.py --check

THE SABOTEUR'S DEVICE CONDITIONS (2026-11-25), on the coordinating session's rulings of that day: nodes whose figures
hold while, or for some seconds after, something happens to the player's traps and gadgets. These four need no new
code, only rows:

  - Snap Back (`Saboteur_basic_trap_mid_006`): 4% evasion a point while at least one trap is active, the trap count
    capped at one step, which is exactly "at least one".
  - Sprung (`Saboteur_basic_trap_mid_002`): 3% increased movement speed a point, the row's own stack granted by
    `trap_triggered`, lasting 3 seconds, at most one (a second trigger restarts its window).
  - Emergency Protocol (`Saboteur_basic_gadget_mid_004`): 5% increased movement speed and 5% evasion a point, each the
    row's own stack granted by `gadget_destroyed`, lasting 5 seconds, at most one. A stack's key is its node and stat
    (`UCataclysmItemModifiers::OwnStackKeyFor`), so the two stats keep a stack each.
  - Support Network (`Saboteur_basic_gadget_stem_005`): 3% increased attack and spell damage a point on
    `Type.Deployable` for each active trap, the tag every "gadgets deal" row names; and from 4 points 10% increased
    trap trigger radius while at least one gadget is active (the coordinating session's ruling: not 10% a gadget).

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
DEPLOYABLE = "Type.Deployable"
LABEL = "the Saboteur's device conditions"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_basic_trap_mid_006", "Snap Back", (
        R("evasion", "flat", 4, scale="traps_active", step=1, cap=1),)),
    ("Saboteur_basic_trap_mid_002", "Sprung", (
        R("movement_speed", "increased", 3, scale="own_stacks", step=1, cap=1, action_event="trap_triggered",
          stack_seconds=3),)),
    ("Saboteur_basic_gadget_mid_004", "Emergency Protocol", (
        R("movement_speed", "increased", 5, scale="own_stacks", step=1, cap=1, action_event="gadget_destroyed",
          stack_seconds=5),
        R("evasion", "flat", 5, scale="own_stacks", step=1, cap=1, action_event="gadget_destroyed",
          stack_seconds=5))),
    ("Saboteur_basic_gadget_stem_005", "Support Network", (
        R("attack_damage", "increased", 3, tags=DEPLOYABLE, scale="traps_active", step=1),
        R("spell_damage", "increased", 3, tags=DEPLOYABLE, scale="traps_active", step=1),
        R("trap_trigger_radius", "increased", 10, scale="gadgets_active", step=1, cap=1, min_points=4))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
