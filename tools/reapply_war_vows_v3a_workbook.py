"""Write the rows of the War vow options' third slice, V3a, into the design workbook's Passive Effects sheet.

    python tools/reapply_war_vows_v3a_workbook.py
    python tools/reapply_war_vows_v3a_workbook.py --check

THE WAR VOW OPTIONS' THIRD SLICE, V3a (2026-11-25), on the coordinating session's rulings on the vow survey that day
and on the V3 questions it ruled the same afternoon. Three options and one basic node, every row of each:
  - Thornwall (Bulwark Second Oath, option 3): `attacker_retaliation_taken` on `hit_taken`, 5% a stack, up to 10,
    for 10 seconds refreshed by each hit ("any hit: pass the attacker on hit taken"; a landed hit only).
  - Wrath Incarnate (Bulwark Final Oath, option 3): its first sentence's row, 1% increased retaliation per 100 armour,
    and its pulse, `retaliation_pulse` on `big_hit_taken`, 200% within 4 metres.
  - Absorb and Return (Bulwark, `basic_tw_t2_001`): `retaliation_for_big_hits` 3 a point, added to the retaliation
    share while a big hit is paid for.
  - Eternal Hunger (Berserker Final Oath, option 3): its +50% leech row; leech's overheal held in the temporary absorb
    up to 100% of maximum health; that part decaying at 1% of maximum health a second while not leeching ("a
    judgement, play-test").

WRATH INCARNATE'S AND ETERNAL HUNGER'S FIRST ROWS were written by `tools/reapply_war_vows_v1_workbook.py`, which no
longer lists them: this script names their nodes in `EXTENDING`, so the rows held are kept and only the rest appended.
Eternal Hunger's node also holds Ragnarok's rows (option 1), which `tools/reapply_war_vows_v2b_workbook.py` writes; a
group is one option of one node, so neither script reads the other's option.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported. IT IS IDEMPOTENT and REFUSES RATHER
THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the War vow options' third slice (V3a)"

#: Wrath Incarnate's "every enemy within 4 metres".
WRATH_PULSE_METRES = 4

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Bulwark_capstone_50", "Second Oath", (
        R(None, "", 5, action="attacker_retaliation_taken", action_event="hit_taken", stack_seconds=10, cap=10,
          option=3),)),
    ("Bulwark_capstone_200", "Final Oath", (
        R("retaliation", "increased", 1, scale="armor", step=100, option=3),
        R(None, "", 200, action="retaliation_pulse", action_event="big_hit_taken", event_value=WRATH_PULSE_METRES,
          option=3))),
    ("Bulwark_basic_tw_t2_001", "Absorb and Return", (
        R("retaliation_for_big_hits", "flat", 3),)),
    ("Berserker_capstone_200", "Final Oath", (
        R("life_leech", "increased", 50, option=3),
        R("leech_overheal_absorb_percent_of_maximum_health", "flat", 100, option=3),
        R("leech_overheal_absorb_decay_percent_per_second", "flat", 1, option=3))),
)

#: The nodes `tools/reapply_war_vows_v1_workbook.py` wrote one row of an option on before this script took the option.
EXTENDING = frozenset({"Bulwark_capstone_200", "Berserker_capstone_200"})


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL, extending=EXTENDING)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0], extending=EXTENDING)


if __name__ == "__main__":
    sys.exit(main())
