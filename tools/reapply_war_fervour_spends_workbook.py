"""Write the rows of the War nodes that spend or drain Fervour, and of the two that count enemies within melee range.

    python tools/reapply_war_fervour_spends_workbook.py
    python tools/reapply_war_fervour_spends_workbook.py --check

THE FERVOUR SPENDS, `plan_death_and_war.md` (2026-11-25), stacked on the War riders. The coordinating session's order of
that day: the remaining Fervour spends first, since they complete the Berserker's and the Bulwark's kits.

"WITHIN MELEE RANGE" IS 4 METRES, the coordinating session's ruling of 2026-11-25, now a sentence in the design document
(Passive Class Trees): the reach the trees already use for enemies near a character in melee. So:

  - Warmonger (`Berserker_twoh_016`): in Wrath, a drain of 3 Fervour a second for each enemy within 4 metres (once,
    not a point), and 8% increased melee attack damage a point for each such enemy.
  - Cut and Run (`Berserker_capstone_50`, option 2): in Frenzy, 5% increased movement speed for each enemy within 4
    metres.

AND FOUR SPENDS, each a figure `FCataclysmWarFervour` reads where it applies (the C++ beside these rows):

  - Iron Will (`Bulwark_basic_ic_resolve_001`): at 75 Fervour or more, a block negates its blow
    (`block_negation_chance` 100, once) and the negation costs 20 Fervour less 2 a point
    (`fervour_cost_per_negated_block`).
  - Undying (`Berserker_leech_004`): at 50 Fervour or more, a lethal blow leaves 1 health, the whole pool is spent and
    5% of maximum health returns for each whole 10 spent (`health_percent_per_ten_fervour_on_lethal_blow`).
  - Cleave (`Berserker_twoh_004`), two-handed, melee: an arc of at least 180 degrees and 10 more a point
    (`strike_arc_at_least_degrees`); each enemy beyond the first costs 5 Fervour (once) and, when paid, gains 3 a point.
  - Blood Tithe (`Bulwark_basic_jg_resolve_001`): above 50% Fervour, 50% increased maximum health (once); 5 Fervour lost
    for each 100 health regenerated, and from 6 points 1 for each 150 (rows of -4 and +50 from 6 points).

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
#: "Within melee range": the coordinating session's ruling of 2026-11-25.
MELEE_RANGE_METRES = 4
IN_REACH = {"scale": "enemies_in_reach", "step": 1, "reach": MELEE_RANGE_METRES}
TWO_HANDED = {"cond": "wielding_two_handed_weapon"}
AT_75 = {"cond": "class_resource_points_at_least", "cond_value": 75}
LABEL = "the Fervour spends"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Berserker_twoh_016", "Warmonger", (
        R("fervour_drain_per_second", "flat", 3, cond="in_wrath", min_points=1, **IN_REACH),
        R("attack_damage", "increased", 8, tags=MELEE, cond="in_wrath", **IN_REACH))),
    ("Berserker_capstone_50", "Second Oath", (
        R("movement_speed", "increased", 5, cond="in_frenzy", option=2, **IN_REACH),)),
    ("Bulwark_basic_ic_resolve_001", "Iron Will", (
        R("block_negation_chance", "flat", 100, min_points=1, **AT_75),
        R("fervour_cost_per_negated_block", "flat", 20, min_points=1, **AT_75),
        R("fervour_cost_per_negated_block", "flat", -2, **AT_75))),
    ("Berserker_leech_004", "Undying", (
        R("health_percent_per_ten_fervour_on_lethal_blow", "flat", 5, cond="class_resource_points_at_least",
          cond_value=50),)),
    ("Berserker_twoh_004", "Cleave", (
        R("strike_arc_at_least_degrees", "flat", 180, tags=MELEE, min_points=1, **TWO_HANDED),
        R("strike_arc_at_least_degrees", "flat", 10, tags=MELEE, **TWO_HANDED),
        R("fervour_cost_per_extra_enemy_hit", "flat", 5, tags=MELEE, min_points=1, **TWO_HANDED),
        R("fervour_per_extra_enemy_hit_paid", "flat", 3, tags=MELEE, **TWO_HANDED))),
    ("Bulwark_basic_jg_resolve_001", "Blood Tithe", (
        R("max_health", "increased", 50, min_points=1, cond="class_resource_above", cond_value=50),
        R("fervour_lost_to_regeneration", "flat", 5, min_points=1),
        R("fervour_lost_to_regeneration", "flat", -4, min_points=6),
        R("regeneration_per_fervour_loss", "flat", 100, min_points=1),
        R("regeneration_per_fervour_loss", "flat", 50, min_points=6))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
