"""Write the rows of the War vow options' second slice, V2b, into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v2b_workbook.py
    python tools/reapply_war_vows_v2b_workbook.py --check

THE WAR VOW OPTIONS' SECOND SLICE, V2b (2026-11-25), on the coordinating session's rulings on the vow survey that day.
Five options, every row of each, on the mechanisms this slice builds:

  - Warlord (Berserker First Oath, option 1): `enemy_armor_removed_nearby`, 15% of armour for 3 seconds, one stack
    refreshed on each melee hit, on every enemy within 5 metres, above 50% Fervour with a two-handed weapon (an action
    may carry a second condition since this slice).
  - Pure Carnage (Berserker Third Oath, option 2): `smite_nearby_by_hit` on `critical_strike` in Frenzy, 30% of what
    the hit took from health, within 4 metres: "'Melee range' in a node is 4 metres" (`docs/Cataclysm_GDD_v2.md`,
    ruled by the coordinating session on 2026-11-25).
  - Ragnarok (Berserker Final Oath, option 1): its shockwaves, `smite_nearby_by_hit` on `hit_dealt` in Wrath, 50%
    within 6 metres, beside its drain removed in Wrath and its doubled maximum Fervour.
  - Blood Oath (Bulwark First Oath, option 3): `retaliation_penetration` 25.
  - Spiteful Guard (Bulwark Third Oath, option 3): `blocked_damage_added_to_next_retaliation`, a flag.

RAGNAROK'S OPTION HELD TWO ROWS FROM `tools/reapply_war_vows_v2a_workbook.py`, which no longer lists them: this script
names its node in `EXTENDING`, so the rows it holds are kept and only the shockwaves are appended.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
LABEL = "the War vow options' second slice (V2b)"
TWO_HANDED = "wielding_two_handed_weapon"
MELEE = base.MELEE
#: "'Melee range' in a node is 4 metres": `docs/Cataclysm_GDD_v2.md`, ruled by the coordinating session on 2026-11-25.
MELEE_RANGE_METRES = 4

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Berserker_capstone_25", "First Oath", (
        R(None, "", 15, action="enemy_armor_removed_nearby", action_event="hit_dealt", tags=MELEE,
          cond="class_resource_above", cond_value=50, cond2=TWO_HANDED, stack_seconds=3, cap=1, option=1),)),
    ("Berserker_capstone_100", "Third Oath", (
        R(None, "", 30, action="smite_nearby_by_hit", action_event="critical_strike", cond="in_frenzy",
          event_value=MELEE_RANGE_METRES, option=2),)),
    ("Berserker_capstone_200", "Final Oath", (
        R("fervour_drain_per_second", "removed", 1, cond="in_wrath", option=1),
        R("class_resource", "more", 100, cond=TWO_HANDED, option=1),
        R(None, "", 50, action="smite_nearby_by_hit", action_event="hit_dealt", cond="in_wrath", event_value=6,
          option=1))),
    ("Bulwark_capstone_25", "First Oath", (
        R("retaliation_penetration", "flat", 25, option=3),)),
    ("Bulwark_capstone_100", "Third Oath", (
        R("blocked_damage_added_to_next_retaliation", "flat", 1, option=3),)),
)

#: The node `tools/reapply_war_vows_v2a_workbook.py` wrote Ragnarok's first two rows on before this script took them.
EXTENDING = frozenset({"Berserker_capstone_200"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
