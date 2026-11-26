"""Write the War vow options' rows that need no new code into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v1_workbook.py
    python tools/reapply_war_vows_v1_workbook.py --check

THE WAR VOW OPTIONS' FIRST SLICE (2026-11-25), on the coordinating session's rulings on the vow survey that day. Nine
rows on eight options were written here, each on a stat, scale, condition or pool action the game already reads; the
rest of each option's sentence waits for the slice that builds its mechanism (V2 or V3), named in the decisions entry.

SIX ROWS ON SIX OPTIONS WERE LISTED HERE AFTER V2a. Unstoppable's two rows (Berserker Second Oath, option 1: cannot be slowed
and cannot be knocked back, above 75% Fervour with a two-handed weapon) and Ragnarok's one (Berserker Final Oath, option
1: the drain removed while in Wrath) were taken over the same day by the second slice, V2a, with the rows it adds to
those two options: `tools/reapply_war_vows_v2a_workbook.py` holds every row of both.

FOUR ROWS ON FOUR OPTIONS SINCE THE THIRD SLICE, V3a, the same day. Eternal Hunger's leech row (Berserker Final Oath,
option 3) and Wrath Incarnate's armour row (Bulwark Final Oath, option 3) were taken over by it with the rows it adds to
those two options: `tools/reapply_war_vows_v3a_workbook.py` holds every row of both.

THREE ROWS ON THREE OPTIONS SINCE THE THIRD SLICE'S SECOND HALF, V3b, the same day. Colossus Eternal's healing row
(Bulwark Final Oath, option 2) was taken over by it with the rows it adds: `tools/reapply_war_vows_v3b_workbook.py`
holds every row of that option.

  - Shield Oath (Bulwark First Oath, option 1): a block restores 2% of maximum health.
  - Flesh Oath (Bulwark First Oath, option 2): 1% of current health every second (in combat, as every timed row runs).
  - Bloodied but Unbroken (Bulwark Third Oath, option 2): retaliation 1% increased for every 1% of maximum health
    missing.

AND THE PORT'S LAST EIGHT COLUMNS, all added (`base.PORT_EIGHT`), so the War line's sheet carries the fourteen the
Death line's does, all required: the coordinating session's ruling of 2026-11-25.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row or a column would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
LABEL = "the War vow options' first slice"

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Bulwark_capstone_25", "First Oath", (
        R(None, "", 2, action="health", action_event="block", fraction_of="maximum", option=1),
        R(None, "", 1, action="health", action_event="every_seconds", fraction_of="current", every_seconds=1,
          option=2))),
    ("Bulwark_capstone_100", "Third Oath", (
        R("retaliation", "increased", 1, scale="health_missing", step=1, option=2),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows and columns the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, columns=base.PORT_EIGHT)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], columns=base.PORT_EIGHT)


if __name__ == "__main__":
    sys.exit(main())
