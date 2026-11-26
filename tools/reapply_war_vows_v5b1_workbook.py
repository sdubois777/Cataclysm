"""Write the rows of the War vow options' fifth slice, second half, first part, V5b1, into the design workbook.

    python tools/reapply_war_vows_v5b1_workbook.py
    python tools/reapply_war_vows_v5b1_workbook.py --check

THE WAR VOW OPTIONS' FIFTH SLICE, SECOND HALF, FIRST PART, V5b1 (2026-11-25), on the coordinating session's rulings
S1 and S2 that day. One option, two rows:
  - Scrap Mines (Saboteur Second Oath, option 2): `scrap_mine` on `gadget_destroyed` and on `gadget_expired`, 200%
    ("at double damage") at the first enemy within 3 metres. The scrap trap is a copy of the gone gadget's type, marked
    a trap, at half size, reserving nothing.

The Second Oath already holds Trail of Wire's row (option 1, `tools/reapply_war_vows_v4a_workbook.py`); a group is one
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

LABEL = "the War vow options' fifth slice, second half, first part (V5b1)"

#: Scrap Mines' "at double damage" and "within 3 metres".
SCRAP_PERCENT = 200
SCRAP_METRES = 3

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_50", "Second Oath", (
        R(None, "", SCRAP_PERCENT, action="scrap_mine", action_event="gadget_destroyed", event_value=SCRAP_METRES,
          option=2),
        R(None, "", SCRAP_PERCENT, action="scrap_mine", action_event="gadget_expired", event_value=SCRAP_METRES,
          option=2))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
