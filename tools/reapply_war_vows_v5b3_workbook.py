"""Write the rows of the War vow options' fifth slice, second half, third part, V5b3, into the design workbook.

    python tools/reapply_war_vows_v5b3_workbook.py
    python tools/reapply_war_vows_v5b3_workbook.py --check

THE WAR VOW OPTIONS' FIFTH SLICE, SECOND HALF, THIRD PART, V5b3 (2026-11-25), on the coordinating session's rulings
A1 and A2 that day. One option, one row:
  - Walking Arsenal (Saboteur First Oath, option 2): "Your gadgets walk after you, staying within 8 metres of you, and
    keep firing as they move." One flat stat with no gameplay attribute: `gadget_follow_within_metres` 8. The speed is
    the one the player's other followers walk at, the engine's.

The First Oath already holds Baited's row (option 1, `tools/reapply_war_vows_v4a_workbook.py`) and Lightning Rod's
(option 3, `tools/reapply_war_vows_v5a_workbook.py`); a group is one option of one node, so none reads another's.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' fifth slice, second half, third part (V5b3)"

#: Walking Arsenal's "staying within 8 metres of you".
FOLLOW_WITHIN_METRES = 8

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_25", "First Oath", (
        R("gadget_follow_within_metres", "flat", FOLLOW_WITHIN_METRES, option=2),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
