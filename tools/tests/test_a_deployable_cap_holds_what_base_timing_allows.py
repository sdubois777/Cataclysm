"""The War deployables state no count cap: their Fervour reserve is the limit.

WHAT THIS FILE HELD UNTIL 2026-11-25. Issue #1833, the deployable cap, ruled
2026-09-25 under the owner's delegation: Bolt Turret, Ballista and Iron Fortress
stated caps of 1, 2 and 5, equal to what their duration and cooldown let stand
at once, and this file checked that no cap bound at base timing.

WHAT IT HOLDS NOW. Issue #1934, approved by the owner on 2026-11-25 and ruled by
the coordinating session that day: the three War machines reserve their rows'
Fervour, 5 a machine, and lose their caps under the owner's ruling of 2026-11-20
on issue #2787 ("that's limiting on its own"). So this checks the shipped rows:
each of the three is a Deployable, states a reserve of 5, and states no
`MaxActive`. The file keeps its name because `docs/DECISIONS.md` names it.

THE SCOPE: a check finding none of the three rows would pass on nothing, so the
rows' presence is asserted first. The generator refuses a `MaxActive` on any
Deployable row (`refuse_a_count_cap_on_a_summon`); this reads what shipped.
"""

from __future__ import annotations

import csv
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "game" / "Data"

#: The three War machine rows.
MACHINES = ("War_Crossbow_Special", "War_Spear_Special", "War_Spear_Ultimate")


def params(cell: str) -> dict[str, str]:
    out = {}
    for part in cell.split(";"):
        if "=" in part:
            key, value = part.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def deployable_rows() -> dict[str, dict]:
    with (DATA / "WeaponSkills.csv").open(encoding="utf-8") as handle:
        return {r["Name"]: r for r in csv.DictReader(handle) if r["Shape"] == "Deployable"}


def test_the_three_war_machines_are_the_shipped_deployables():
    assert set(MACHINES) <= set(deployable_rows()), sorted(deployable_rows())


def test_each_reserves_five_and_states_no_count_cap():
    rows = deployable_rows()
    for name in MACHINES:
        stated = params(rows[name]["ShapeParams"])
        assert "MaxActive" not in stated, f"{name} states MaxActive={stated['MaxActive']}"
        assert float(stated.get("FervourReserve", 0)) == 5.0, name


def test_no_description_states_a_count_cap():
    rows = deployable_rows()
    for name in MACHINES:
        assert "may stand" not in rows[name]["SkillDescription"], name
        assert "reserves 5 Fervour" in rows[name]["SkillDescription"], name
