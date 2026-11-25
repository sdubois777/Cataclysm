"""Write the rows of the War vow options' fourth slice, second half, V4b, into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v4b_workbook.py
    python tools/reapply_war_vows_v4b_workbook.py --check

THE WAR VOW OPTIONS' FOURTH SLICE, SECOND HALF, V4b (2026-11-25), on the coordinating session's rulings on the
Saboteur's B options that day. Two options, one row each:
  - Counterfire (Saboteur Third Oath, option 3): `counterfire` on `dodge`, one answer for every 2 traps and gadgets the
    player commands, skill and War alike, the nearest the attacker first, at 100% of a gadget's shot, with a Trigger
    Cooldown of 1 second ("No more than once a second").
  - Magnum Opus (Saboteur Final Oath, option 2): `eldest_gadget_casts_every_skill`, a flag: the gadget standing longest
    casts every skill the player uses, not the basic attack and not the movement skill, at its own target with the
    whole of the damage.

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

LABEL = "the War vow options' fourth slice, second half (V4b)"

#: Counterfire's "one ... for every 2 you have" and "No more than once a second".
COUNTERFIRE_DEVICES_PER_ANSWER = 2
COUNTERFIRE_SECONDS = 1

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_100", "Third Oath", (
        R(None, "", 100, action="counterfire", action_event="dodge", event_value=COUNTERFIRE_DEVICES_PER_ANSWER,
          trigger_cooldown=COUNTERFIRE_SECONDS, option=3),)),
    ("Saboteur_capstone_200", "Final Oath", (
        R("eldest_gadget_casts_every_skill", "flat", 1, option=2),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
