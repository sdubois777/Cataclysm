"""Write the rows of the War vow options' fourth slice, first half, V4a, into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v4a_workbook.py
    python tools/reapply_war_vows_v4a_workbook.py --check

THE WAR VOW OPTIONS' FOURTH SLICE, FIRST HALF, V4a (2026-11-25), on the coordinating session's rulings on the Saboteur's
B options that day. Two options, one row each:
  - Baited (Saboteur First Oath, option 1): `taunt_enemies_near_trap` on `trap_armed`, every enemy within 8 metres of
    the trap taunted by it for 4 seconds, through the engine's taunt with all its refusals; a trap that goes off ends
    its taunts.
  - Trail of Wire (Saboteur Second Oath, option 1): `place_last_trap_again` on `movement_skill`, value 100 (always):
    the last skill trap placed is placed again where the movement began, through the ordinary placement, armed at
    once. Skill traps only, as ruled: a War machine trap's placement is a skill cast, which the engine cannot repeat.

Neither node held a row before, so nothing is taken over.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' fourth slice, first half (V4a)"

#: Baited's "within 8 metres" and "for 4 seconds".
BAITED_METRES = 8
BAITED_SECONDS = 4

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_25", "First Oath", (
        R(None, "", BAITED_SECONDS, action="taunt_enemies_near_trap", action_event="trap_armed",
          event_value=BAITED_METRES, option=1),)),
    ("Saboteur_capstone_50", "Second Oath", (
        R(None, "", 100, action="place_last_trap_again", action_event="movement_skill", option=1),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
