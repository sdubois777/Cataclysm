"""Write Titanfall's row, the Bulwark's keystone, into the design workbook.

    python tools/reapply_war_titanfall_workbook.py
    python tools/reapply_war_titanfall_workbook.py --check

TITANFALL (`Bulwark_keystone_jg_final`), 2026-11-25, on the coordinating session's rulings T1-T6 that day and the
sentence R6 the owner accepted on 2026-11-24: "When a single hit takes more than 20% of your Maximum HP, you cannot be
killed for 2 seconds and your next attack deals bonus damage equal to that hit. Can only happen once every 30
seconds." One row: the action `titanfall` on `big_hit_taken`, its value the 2 seconds, its Event Value the 20 per cent,
its Trigger Cooldown the 30 seconds (ruled T1).

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "Titanfall, the Bulwark's keystone"

#: "you cannot be killed for 2 seconds", "more than 20% of your Maximum HP", "once every 30 seconds".
UNKILLABLE_SECONDS = 2
SHARE_PERCENT = 20
EVERY_SECONDS = 30

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Bulwark_keystone_jg_final", "Titanfall", (
        R(None, "", UNKILLABLE_SECONDS, action="titanfall", action_event="big_hit_taken", event_value=SHARE_PERCENT,
          trigger_cooldown=EVERY_SECONDS),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
