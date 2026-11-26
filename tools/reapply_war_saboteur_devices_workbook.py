"""Write the rows of the Saboteur's device conditions into the design workbook's Passive Effects sheet.

    python tools/reapply_war_saboteur_devices_workbook.py
    python tools/reapply_war_saboteur_devices_workbook.py --check

THE SABOTEUR'S DEVICE CONDITIONS (2026-11-25), on the coordinating session's rulings of that day: nodes whose figures
hold while, or for some seconds after, something happens to the player's traps and gadgets. Nine nodes, thirteen rows.
These four need no new code, only rows:

  - Snap Back (`Saboteur_basic_trap_mid_006`): 4% evasion a point while at least one trap is active, the trap count
    capped at one step, which is exactly "at least one".
  - Sprung (`Saboteur_basic_trap_mid_002`): 3% increased movement speed a point, the row's own stack granted by
    `trap_triggered`, lasting 3 seconds, at most one (a second trigger restarts its window).
  - Emergency Protocol (`Saboteur_basic_gadget_mid_004`): 5% increased movement speed and 5% evasion a point, each the
    row's own stack granted by `gadget_destroyed`, lasting 5 seconds, at most one. A stack's key is its node and stat
    (`UCataclysmItemModifiers::OwnStackKeyFor`), so the two stats keep a stack each.
  - Support Network (`Saboteur_basic_gadget_stem_005`): 3% increased attack and spell damage a point on
    `Type.Gadget` for each active trap; and from 4 points 10% increased trap trigger radius while at least one gadget
    is active (the coordinating session's ruling: not 10% a gadget). WRITTEN ON `Type.Deployable` UNTIL THE SAME DAY'S
    RULING put `Type.Gadget` on every machine that is not a trap (`tools/apply_gadget_tag_2026_11_25.py` moved the two
    rows), so a trap's blow no longer takes "gadget damage".

These five stand on what the same branch adds to the engine, one event and four conditions:

  - Razor Instincts (`Saboteur_basic_trunk_012`): 3% evasion a point, the row's own stack granted by `device_placed`
    (a trap or a gadget placed, one event for either, the ruling), lasting 3 seconds, at most one.
  - Sentry Network (`Saboteur_basic_gadget_mid_006`): 2% evasion a point under `own_gadget_within_metres` 15.
  - Ambush (`Saboteur_basic_trap_stem_004`): 5% increased trap damage (`minion_damage` on `Type.Trap`) a point under
    `target_not_yet_triggered_your_trap`; an enemy's first trap counts as not yet triggered (the ruling).
  - Overcautious (`Saboteur_basic_gadget_mid_002`): 4% evasion a point under `seconds_after_gadget_struck` 2 ("under
    attack" lasts 2 seconds after a gadget takes a hit, the ruling).
  - Overwhelming Presence (`Saboteur_keystone_trap_qty`), as the passive order fixes reword it: 50% more trap damage and
    100% more trap trigger radius ("doubled", a true doubling by the ruling), each under `no_room_for_a_trap`.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
GADGET = "Type.Gadget"
TRAP = "Type.Trap"
NO_ROOM = {"cond": "no_room_for_a_trap"}
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
        R("attack_damage", "increased", 3, tags=GADGET, scale="traps_active", step=1),
        R("spell_damage", "increased", 3, tags=GADGET, scale="traps_active", step=1),
        R("trap_trigger_radius", "increased", 10, scale="gadgets_active", step=1, cap=1, min_points=4))),
    ("Saboteur_basic_trunk_012", "Razor Instincts", (
        R("evasion", "flat", 3, scale="own_stacks", step=1, cap=1, action_event="device_placed", stack_seconds=3),)),
    ("Saboteur_basic_gadget_mid_006", "Sentry Network", (
        R("evasion", "flat", 2, cond="own_gadget_within_metres", cond_value=15),)),
    ("Saboteur_basic_trap_stem_004", "Ambush", (
        R("minion_damage", "increased", 5, tags=TRAP, cond="target_not_yet_triggered_your_trap"),)),
    ("Saboteur_basic_gadget_mid_002", "Overcautious", (
        R("evasion", "flat", 4, cond="seconds_after_gadget_struck", cond_value=2),)),
    ("Saboteur_keystone_trap_qty", "Overwhelming Presence", (
        R("minion_damage", "more", 50, tags=TRAP, **NO_ROOM),
        R("trap_trigger_radius", "more", 100, **NO_ROOM))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
