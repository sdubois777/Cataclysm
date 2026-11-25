"""Write the rows of the War vow options' third slice, second half, V3b, into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v3b_workbook.py
    python tools/reapply_war_vows_v3b_workbook.py --check

THE WAR VOW OPTIONS' THIRD SLICE, SECOND HALF, V3b (2026-11-25), on the coordinating session's rulings on the vow
survey that day and on the V3 questions it ruled the same afternoon. Two options, every row of each:
  - Colossus Eternal (Bulwark Final Oath, option 2): its healing received 100% more; its increases to Maximum HP
    counted twice, a flag; its save, a lethal hit that is not a tick leaving 1 HP while above 50% health, with no
    cooldown; and 2 flat attack damage for every 100 points of current health while above 75% health.
  - Thousand Deaths (Berserker Final Oath, option 2): `frenzy_chain` on `hit_dealt`, 60% within 5 metres of the enemy
    struck, in Frenzy and dual wielding.

COLOSSUS ETERNAL'S FIRST ROW was written by `tools/reapply_war_vows_v1_workbook.py`, which no longer lists it: this
script names its node in `EXTENDING`, so the row held is kept and only the rest appended. The node also holds The
Unbreakable Wall's row (option 1, `tools/apply_war_owner_rulings_2026_11_25.py`) and Wrath Incarnate's (option 3,
`tools/reapply_war_vows_v3a_workbook.py`); a group is one option of one node, so no script reads another's option.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' third slice, second half (V3b)"

#: Thousand Deaths' "nearby", ruled "5 m (the engine's nearby radius)".
CHAIN_METRES = 5

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Bulwark_capstone_200", "Final Oath", (
        R("healing_received", "more", 100, option=2),
        R("max_health_increases_count_twice", "flat", 1, option=2),
        R("lethal_hit_survived_above_health", "flat", 50, option=2),
        R("attack_damage", "flat", 2, scale="current_health", step=100, cond="health_above", cond_value=75,
          option=2))),
    ("Berserker_capstone_200", "Final Oath", (
        R(None, "", 60, action="frenzy_chain", action_event="hit_dealt", cond="in_frenzy", cond2="dual_wielding",
          event_value=CHAIN_METRES, option=2),)),
)

#: The node `tools/reapply_war_vows_v1_workbook.py` wrote Colossus Eternal's first row on before this script took it.
EXTENDING = frozenset({"Bulwark_capstone_200"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
