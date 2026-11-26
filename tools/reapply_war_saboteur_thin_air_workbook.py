"""Write the rows of Thin Air and Calculated into the design workbook's Passive Effects sheet.

    python tools/reapply_war_saboteur_thin_air_workbook.py
    python tools/reapply_war_saboteur_thin_air_workbook.py --check

THE C5 PLAN (2026-11-25), accepted as written by the coordinating session that day. Two nodes, every row of each:

  - Thin Air (`Saboteur_basic_trunk_013`, 1 point): `cannot_be_targeted_while_moving`, a flag, and
    `cannot_be_targeted_broken_seconds` 2, read by `UCataclysmAbilitySystemComponent::IsHiddenByThinAir`.
  - Calculated (`Saboteur_basic_trunk_003`): `class_resource` 5 a point; from 6 points `class_resource` increased 10,
    the ruled rewording of "Your Fervour cannot be reduced by enemy effects" to "Your maximum Fervour is increased by
    10%", since no enemy effect lowers a player's Fervour.

CALCULATED HELD A ROW FROM `tools/reapply_war_saboteur_rows_workbook.py`, which no longer lists it: this script names it
in `EXTENDING`, so the row it holds is kept and only the rest is appended.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
LABEL = "Thin Air and Calculated"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Saboteur_basic_trunk_013", "Thin Air", (
        R("cannot_be_targeted_while_moving", "flat", 1),
        R("cannot_be_targeted_broken_seconds", "flat", 2))),
    ("Saboteur_basic_trunk_003", "Calculated", (
        R("class_resource", "flat", 5),
        R("class_resource", "increased", 10, min_points=6))),
)

#: The node `tools/reapply_war_saboteur_rows_workbook.py` wrote a row on before this script took it over.
EXTENDING = frozenset({"Saboteur_basic_trunk_003"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
