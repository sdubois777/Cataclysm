"""Write the rows of the War vow options' second slice (V2a) into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v2a_workbook.py
    python tools/reapply_war_vows_v2a_workbook.py --check

THE WAR VOW OPTIONS' SECOND SLICE, V2a (2026-11-25), on the coordinating session's rulings on the vow survey that day.
Six nodes and options, every row of each, on the mechanisms this slice builds:

  - Unbridled (`Berserker_trunk_004`): `class_resource` 10 a point; from 5 points `damage_reduction` 1 for every 20
    points of maximum Fervour, on the new `max_class_resource` scale.
  - Opportunist (Berserker First Oath, option 2): `cooldown_reset_movement` always (100) on the new `frenzy_entered`
    event. Frenzy is entered only dual wielding, so the row states no weapon condition, as Cut and Run's does not.
  - Unstoppable (Berserker Second Oath, option 1): cannot be slowed, cannot be knocked back and, new, cannot be pinned
    (`pin_suppressed`, a pin counted as the root, as ruled), each above 75% Fervour with a two-handed weapon.
  - Titan (Berserker Third Oath, option 1): `max_health` 1% increased for every 2 points of maximum Fervour, with a
    two-handed weapon, on `max_class_resource`.
  - Ragnarok (Berserker Final Oath, option 1): the drain removed in Wrath, and, new, `class_resource` 100% more with a
    two-handed weapon: "Your maximum Fervour is doubled". (The shockwaves are V2b's.)
  - Bastion (Bulwark Third Oath, option 1): `max_health` 1% increased for every 1% block chance, on the new
    `block_chance` scale, which reads the attribute, as ruled.

UNSTOPPABLE'S AND RAGNAROK'S OPTIONS HELD ROWS FROM `tools/reapply_war_vows_v1_workbook.py`, which no longer lists them:
this script names their nodes in `EXTENDING`, so the rows they hold are kept and only the rest are appended.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
LABEL = "the War vow options' second slice (V2a)"
TWO_HANDED = "wielding_two_handed_weapon"
TWO_HANDED_ABOVE_75 = {"cond": "class_resource_above", "cond_value": 75, "cond2": TWO_HANDED}

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Berserker_trunk_004", "Unbridled", (
        R("class_resource", "flat", 10),
        R("damage_reduction", "flat", 1, scale="max_class_resource", step=20, min_points=5))),
    ("Berserker_capstone_25", "First Oath", (
        R(None, "", 100, action="cooldown_reset_movement", action_event="frenzy_entered", option=2),)),
    ("Berserker_capstone_50", "Second Oath", (
        R("movement_speed_reduction_suppressed", "flat", 1, option=1, **TWO_HANDED_ABOVE_75),
        R("knockback_suppressed", "flat", 1, option=1, **TWO_HANDED_ABOVE_75),
        R("pin_suppressed", "flat", 1, option=1, **TWO_HANDED_ABOVE_75))),
    ("Berserker_capstone_100", "Third Oath", (
        R("max_health", "increased", 1, scale="max_class_resource", step=2, cond=TWO_HANDED, option=1),)),
    ("Berserker_capstone_200", "Final Oath", (
        R("fervour_drain_per_second", "removed", 1, cond="in_wrath", option=1),
        R("class_resource", "more", 100, cond=TWO_HANDED, option=1))),
    ("Bulwark_capstone_100", "Third Oath", (
        R("max_health", "increased", 1, scale="block_chance", step=1, option=1),)),
)

#: The nodes `tools/reapply_war_vows_v1_workbook.py` wrote rows on, for the options this script took over.
EXTENDING = frozenset({"Berserker_capstone_50", "Berserker_capstone_200"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
