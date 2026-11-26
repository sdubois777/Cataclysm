"""Write the rows of the War vow options' fifth slice, second half, second part, V5b2, into the design workbook.

    python tools/reapply_war_vows_v5b2_workbook.py
    python tools/reapply_war_vows_v5b2_workbook.py --check

THE WAR VOW OPTIONS' FIFTH SLICE, SECOND HALF, SECOND PART, V5b2 (2026-11-25), on the coordinating session's rulings
C1 and C2 that day. One option, three rows:
  - Cat's Cradle (Saboteur Second Oath, option 3): "Each of your traps and gadgets is joined by a wire to every other
    one within 10 metres. An enemy crossing a wire is pinned for 1 second, no more than once every 4 seconds, and the
    nearer end answers: a trap goes off, a gadget fires at it." Three flat stats with no gameplay attribute:
    `device_wire_reach_metres` 10, `device_wire_pin_seconds` 1 and `device_wire_repin_seconds` 4. The 50 cm a wire is
    crossed within is the engine's (`UCataclysmAbilitySystemComponent::DeviceWireCrossingCm`), a judgement.

The Second Oath already holds Trail of Wire's row (option 1, `tools/reapply_war_vows_v4a_workbook.py`) and Scrap Mines'
two (option 2, `tools/reapply_war_vows_v5b1_workbook.py`); a group is one option of one node, so none reads another's.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' fifth slice, second half, second part (V5b2)"

#: Cat's Cradle's "within 10 metres", "pinned for 1 second" and "no more than once every 4 seconds".
WIRE_REACH_METRES = 10
WIRE_PIN_SECONDS = 1
WIRE_REPIN_SECONDS = 4

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_capstone_50", "Second Oath", (
        R("device_wire_reach_metres", "flat", WIRE_REACH_METRES, option=3),
        R("device_wire_pin_seconds", "flat", WIRE_PIN_SECONDS, option=3),
        R("device_wire_repin_seconds", "flat", WIRE_REPIN_SECONDS, option=3))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
