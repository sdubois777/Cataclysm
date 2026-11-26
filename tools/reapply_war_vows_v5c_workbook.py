"""Write the rows of the War vow options' last slice, V5c, Bolted Together, into the design workbook.

    python tools/reapply_war_vows_v5c_workbook.py
    python tools/reapply_war_vows_v5c_workbook.py --check

THE WAR VOW OPTIONS' LAST SLICE, V5c (2026-11-25), on the coordinating session's rulings B1 and BT1-BT4 that day. One
option, two rows:
  - Bolted Together (Saboteur Third Oath, option 2): "A gadget you place within 4 metres of another of yours is bolted
    onto it: one gadget, with their health added together, that fires all their attacks with 10% more damage for each
    gadget in it." Two flat stats with no gameplay attribute: `gadget_bolt_reach_metres` 4 and
    `bolted_more_percent_per_gadget` 10.

The Third Oath already holds Creeping Mines' rows (option 1, `tools/reapply_war_vows_v5a_workbook.py`) and
Counterfire's (option 3, `tools/reapply_war_vows_v4b_workbook.py`); a group is one option of one node, so none reads
another's.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' last slice (V5c)"

#: Bolted Together's "within 4 metres of another of yours" and "10% more damage for each gadget in it".
BOLT_REACH_METRES = 4
MORE_PERCENT_PER_GADGET = 10

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_100", "Third Oath", (
        R("gadget_bolt_reach_metres", "flat", BOLT_REACH_METRES, option=2),
        R("bolted_more_percent_per_gadget", "flat", MORE_PERCENT_PER_GADGET, option=2))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
