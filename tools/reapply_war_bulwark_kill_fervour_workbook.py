"""Write the Bulwark's starting node's rows, its Fervour for a kill included, into the design workbook.

    python tools/reapply_war_bulwark_kill_fervour_workbook.py
    python tools/reapply_war_bulwark_kill_fervour_workbook.py --check

THE BULWARK'S KILL FERVOUR (2026-11-26, the final bundle). The Bulwark's starting node, `Bulwark_basic_trunk_000`
("Fervour"), has always said "Taking hits, blocking and killing enemies generate Fervour", and slice C (2026-11-24)
built the first two and left the kill waiting for `fervour_per_kill`, which the Death line built
(`UCataclysmFervour::GainOnOwnKill`). That generator pays any row on the stat, whatever the class, through
`UCataclysmFervour::RaisePool`, scaled by `class_resource_generation`: so the kill is one more row on the node, 1 a kill
at every rarity, the rate every War starting node's gain has (slice C's judgement, a play-test point).

THIS SCRIPT TAKES THE NODE OVER from `tools/reapply_war_fervour_sources_workbook.py`, which wrote its first three rows and
now writes only the Berserker's and the Saboteur's: the node is named in `EXTENDING`, so a workbook holding slice C's
three rows gets the fourth appended, and one holding none gets all four. A row held that is not among these four is
refused. `tools/apply_war_bulwark_kill_fervour_sentence_2026_11_26.py` states the rate in the node's sentence.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the Bulwark's kill Fervour"

NODE = "Bulwark_basic_trunk_000"

#: "1 for each enemy you kill": the same 1 as a hit taken and a block, at every rarity.
PER_KILL = 1

#: (node, Node Name label, rows), in the shape `base.NODES` takes. Slice C's three rows first, unchanged.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    (NODE, "Fervour", (
        R("fervour_per_hit_taken", "flat", 1),
        R("fervour_per_block", "flat", 1),
        R("fervour_decay_out_of_combat_per_second", "flat", 5),
        R("fervour_per_kill", "flat", PER_KILL),
    )),
)

#: The node whose first three rows slice C's script wrote; only what it lacks is appended.
EXTENDING = frozenset({NODE})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
