"""Write the rows of Skull Splitter, Deathmark and Blood Price into the design workbook's Passive Effects sheet.

    python tools/reapply_war_skull_deathmark_blood_workbook.py
    python tools/reapply_war_skull_deathmark_blood_workbook.py --check

THREE BERSERKER NODES (2026-11-25), on the coordinating session's rulings of that day, each a figure
`FCataclysmWarFervour` reads where it applies. Twelve rows:

  - Skull Splitter (`Berserker_twoh_003`, 8 points): 6% critical strike multiplier a point under a two-handed weapon;
    and, under Wrath and a two-handed weapon, 10% armour removed a point for each stack and a cost of 3; a cap of 4
    stacks to 4 points (Max Points 5) and 6 from 5 points; 2 seconds.
  - Deathmark (`Berserker_crit_004`, 6 points): 3% more damage taken a point for a marked enemy; a cost of 10, less 1 a
    point (9 at 1 point, 4 at 6); 5 seconds.
  - Blood Price (`Berserker_trunk_008`, 8 points): 5% of the maximum Fervour a point, held for 3 seconds.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
LABEL = "Skull Splitter, Deathmark and Blood Price"
WRATH_TWO_HANDED = {"cond": "in_wrath", "cond2": "wielding_two_handed_weapon"}

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Berserker_twoh_003", "Skull Splitter", (
        R("crit_multiplier", "flat", 6, cond="wielding_two_handed_weapon"),
        R("crit_armor_removed_in_wrath", "flat", 10, **WRATH_TWO_HANDED),
        R("crit_armor_fervour_cost", "flat", 3, min_points=1, **WRATH_TWO_HANDED),
        R("crit_armor_stack_cap", "flat", 4, min_points=1, max_points=5),
        R("crit_armor_stack_cap", "flat", 6, min_points=5),
        R("crit_armor_seconds", "flat", 2, min_points=1))),
    ("Berserker_crit_004", "Deathmark", (
        R("deathmark_damage_taken_percent", "flat", 3),
        R("deathmark_fervour_cost", "flat", 10, min_points=1),
        R("deathmark_fervour_cost", "flat", -1),
        R("deathmark_seconds", "flat", 5, min_points=1))),
    ("Berserker_trunk_008", "Blood Price", (
        R("blood_price_percent_of_maximum_fervour", "flat", 5),
        R("blood_price_seconds", "flat", 3, min_points=1))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
