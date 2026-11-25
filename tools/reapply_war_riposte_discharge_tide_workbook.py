"""Write the rows of Riposte, Vengeful Discharge and Crimson Tide into the design workbook's Passive Effects sheet.

    python tools/reapply_war_riposte_discharge_tide_workbook.py
    python tools/reapply_war_riposte_discharge_tide_workbook.py --check

THE LAST THREE FERVOUR SPENDS (2026-11-25), on the coordinating session's rulings of that day, each a figure
`FCataclysmWarFervour` reads where it applies. Sixteen rows on three nodes:

  - Riposte (`Bulwark_keystone_ic_t1`): 75% of the damage blocked dealt back, stated under at least 25 Fervour; a cost
    of 25; a stun of 1 second, stated under the pool at its maximum. One action judges both conditions before it spends.
  - Vengeful Discharge (`Bulwark_basic_tw_resolve_001`, the War rulings' R7): its own drain of 10 a second (once); 3%
    increased retaliation a point; the pulse's radius of 4 metres (once); Bleed from 5 points. The pulse is sized by the
    last blow taken this floor (the coordinating session's ruling).
  - Crimson Tide (`Berserker_leech_001`, its fractions made steps): 1% life leech on melee for every 2 points (rows
    from 2, 4, 6 and 8); from 5 points 1,100 more leech payout rate on its base of 100, which pays a leech within the next quarter-second
    step (3 seconds at the base rate of 100, and a twelfth of that at 1,200), the data route the coordinating session
    accepted for "instantly" (not in the same frame); and 1% of maximum health for every 10 Fervour spent or drained for
    every 2 points (rows from 2, 4, 6 and 8).

"+3% PER POINT" ON VENGEFUL DISCHARGE HOLDS WHETHER OR NOT THE DRAIN IS DRAINING, a judgement: the sentence gives the
drain as the bonus's price, and the node holds both.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
MELEE = base.MELEE
EVERY_TWO_POINTS = (2, 4, 6, 8)
LABEL = "the last Fervour spends"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Bulwark_keystone_ic_t1", "Riposte", (
        R("riposte_share_of_blocked", "flat", 75, cond="class_resource_points_at_least", cond_value=25),
        R("riposte_fervour_cost", "flat", 25),
        R("riposte_stun_seconds_at_maximum", "flat", 1, cond="class_resource_at_maximum"))),
    ("Bulwark_basic_tw_resolve_001", "Vengeful Discharge", (
        R("retaliation_pulse_drain_per_second", "flat", 10, min_points=1),
        R("retaliation", "increased", 3),
        R("retaliation_pulse_radius_metres", "flat", 4, min_points=1),
        R("retaliation_pulse_applies_bleed", "flat", 1, min_points=5))),
    ("Berserker_leech_001", "Crimson Tide", (
        *(R("life_leech", "flat", 1, tags=MELEE, min_points=at) for at in EVERY_TWO_POINTS),
        R("leech_payout_rate", "flat", 1100, min_points=5),
        *(R("health_percent_per_ten_fervour_spent", "flat", 1, min_points=at) for at in EVERY_TWO_POINTS))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
