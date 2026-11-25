"""Write the rows of the War nodes the Berserker's states and the War Fervour rates made writable as data.

    python tools/reapply_war_riders_workbook.py
    python tools/reapply_war_riders_workbook.py --check

THE BERSERKER'S RIDERS AND THE FERVOUR SPENDS, `plan_death_and_war.md` (2026-11-25). Ten nodes whose whole sentence is
now a row: the Berserker's states (`FCataclysmBerserkerStates`, the conditions `in_wrath`, `in_frenzy` and
`dual_wielding`, and the drain `fervour_drain_per_second`), the War Fervour rates of slice C, and a second condition,
which this script adds to the workbook as the `Condition 2` and `Condition Value 2` columns. Twenty rows:

  Berserker
  - Battle Lust: 1 Fervour a point for each melee critical strike.
  - Razor Mind: 4% critical strike multiplier a point on melee; 2 Fervour a point for each critical strike, and from 5
    points another 2 a point while above 75% Fervour ("generate double Fervour from this effect").
  - Hair-Trigger: 2% critical strike chance a point in Frenzy, and another 2 a point in Frenzy above 75% Fervour.
  - Death Blow: 5% critical strike chance a point on melee against an enemy below 25% health, two-handed.
  - Brute Force: 10% increased attack and spell damage a point against a staggered enemy, two-handed (the owner's
    2026-08-25 rule: plain "damage" is both).
  - Titan's Grip: in Wrath, a drain of 4 more Fervour a second (once, not a point) and 10% increased melee attack damage
    a point.

  Bulwark
  - Shield Mastery: 2% block chance a point; from 5 points, 3 Fervour for each block.
  - Resolve Under Fire: 1 Fervour a point for each hit taken; from 5 points, 10 more for a hit that leaves health below
    30% (read after the blow is written, so "would reduce you below 30% HP" is the health it left).
  - Immovable: 2% damage reduction a point after 2 seconds without moving; from 5 points, the hit-taken Fervour doubled
    while standing still.
  - Steeled Resolve: at 50 Fervour or more, 3% damage reduction a point and a drain of 5 Fervour a second (once); from 6
    points 3 less, with no condition, which leaves 2 while the drain holds and nothing below 50 (a drain that sums to
    nought or less takes nothing).

"+X% DAMAGE REDUCTION" WITHOUT "(MULTIPLICATIVE)" IS FLAT: the tree writes "(multiplicative)" where it means a More
(Iron Wall, Dense Muscle), and the "Flat damage reduction" affix adds points of the same stat. A judgement.

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
TWO_HANDED = {"cond2": "wielding_two_handed_weapon"}
ABOVE_75 = {"cond": "class_resource_above", "cond_value": 75}
AT_50 = {"cond": "class_resource_points_at_least", "cond_value": 50}
LABEL = "the War riders"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # ---- Berserker ----
    ("Berserker_trunk_001", "Battle Lust", (R("fervour_per_critical_strike", "flat", 1, tags=MELEE),)),
    ("Berserker_crit_001", "Razor Mind", (
        R("crit_multiplier", "flat", 4, tags=MELEE),
        R("fervour_per_critical_strike", "flat", 2),
        R("fervour_per_critical_strike", "flat", 2, min_points=5, per_point_from=1, **ABOVE_75))),
    ("Berserker_dw_014", "Hair-Trigger", (
        R("crit_chance", "flat", 2, cond="in_frenzy"),
        R("crit_chance", "flat", 2, cond="in_frenzy", cond2="class_resource_above", cond_value2=75))),
    ("Berserker_twoh_007", "Death Blow", (
        R("crit_chance", "flat", 5, tags=MELEE, cond="target_health_below", cond_value=25, **TWO_HANDED),)),
    ("Berserker_twoh_006", "Brute Force", (
        R("attack_damage", "increased", 10, cond="target_is_staggered", **TWO_HANDED),
        R("spell_damage", "increased", 10, cond="target_is_staggered", **TWO_HANDED))),
    ("Berserker_twoh_011", "Titan's Grip", (
        R("fervour_drain_per_second", "flat", 4, cond="in_wrath", min_points=1),
        R("attack_damage", "increased", 10, tags=MELEE, cond="in_wrath"))),
    # ---- Bulwark ----
    ("Bulwark_basic_ic_stem_001", "Shield Mastery", (
        R("block_chance", "flat", 2),
        R("fervour_per_block", "flat", 3, min_points=5))),
    ("Bulwark_basic_trunk_007", "Resolve Under Fire", (
        R("fervour_per_hit_taken", "flat", 1),
        R("fervour_per_hit_taken", "flat", 10, cond="health_below", cond_value=30, min_points=5))),
    ("Bulwark_basic_jg_t1_001", "Immovable", (
        R("damage_reduction", "flat", 2, cond="stationary_for_seconds", cond_value=2),
        R("fervour_per_hit_taken", "more", 100, cond="while_stationary", min_points=5))),
    ("Bulwark_basic_trunk_009", "Steeled Resolve", (
        R("damage_reduction", "flat", 3, **AT_50),
        R("fervour_drain_per_second", "flat", 5, min_points=1, **AT_50),
        R("fervour_drain_per_second", "flat", -3, min_points=6))),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
