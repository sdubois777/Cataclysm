"""Write Slippery's rows into the design workbook's Passive Effects sheet.

    python tools/reapply_saboteur_slippery_workbook.py
    python tools/reapply_saboteur_slippery_workbook.py --check

WINDOW death3-w1'S SECOND PASS (2026-11-25), as the coordinating session ruled. Slippery (`Saboteur_basic_trunk_004`)
says "+1% evasion chance per point. At 5 points: After successfully evading, gain 3 Fervour." Its evasion row was
written by `tools/reapply_war_saboteur_rows_workbook.py`; the Fervour clause had no row, which the Death line's check
`tools/tests/test_a_sentence_stating_a_condition_has_a_row_carrying_it.py` found when it first read the War trees.
The clause is one `class_resource` action row on `dodge`, a flat 3 (`points`), paid once from 5 points: the form the
Death trees' slice C6b3 built for Opening Found's and Tear Loose's Fervour.

SLIPPERY HELD A ROW FROM `tools/reapply_war_saboteur_rows_workbook.py`, which no longer lists it: this script names it in
`EXTENDING`, so the row it holds is kept and only the rest is appended, as
`tools/reapply_war_saboteur_thin_air_workbook.py` took Calculated over.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
LABEL = "Slippery"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_basic_trunk_004", "Slippery", (
        R("evasion", "flat", 1),
        # AN ACTION ROW STATES NO STAT AND NO VALUE KIND; the kind is the empty text `row_from_cells` reads back.
        R(None, "", 3, action="class_resource", action_event="dodge", fraction_of="points", min_points=5))),
)

#: The node `tools/reapply_war_saboteur_rows_workbook.py` wrote a row on before this script took it over.
EXTENDING = frozenset({"Saboteur_basic_trunk_004"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
