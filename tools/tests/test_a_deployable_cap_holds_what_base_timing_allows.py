"""A deployable skill's cap holds what its own timing already allows.

WHY THIS EXISTS. Issue #1833, the deployable cap, ruled 2026-09-25 under the
owner's delegation: Bolt Turret, Ballista and Iron Fortress state caps equal to
what their duration and cooldown already let stand at once, so a character with
no cooldown reduction and no extra charges is never refused a placement, and
"Add +1-3 to your max deployable count" matters only once something shortens
the wait. This checks that for every deployable row stating a cap:
machines per cast x ceil(duration / cooldown) <= the cap.

THE COOLDOWN IS THE ROW'S OWN when it states one above nought, else its slot's
default in `game/Data/SkillSlots.csv`, which is what the game reads.
"""

from __future__ import annotations

import csv
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "game" / "Data"

#: The three the ruling capped. THE SCOPE: a check with none of them would pass.
CAPPED = {"War_Crossbow_Special": 1, "War_Spear_Special": 2, "War_Spear_Ultimate": 5}


def params(cell: str) -> dict[str, str]:
    out = {}
    for part in cell.split(";"):
        if "=" in part:
            key, value = part.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def machines_per_cast(minions: str) -> int:
    return sum(int(count) for count in re.findall(r":\s*(\d+)", minions))


def deployable_rows() -> list[dict]:
    with (DATA / "WeaponSkills.csv").open(encoding="utf-8") as handle:
        return [r for r in csv.DictReader(handle) if r["Shape"] == "Deployable"]


def slot_cooldowns() -> dict[str, float]:
    with (DATA / "SkillSlots.csv").open(encoding="utf-8") as handle:
        return {r["Slot"]: float(r["Cooldown"]) for r in csv.DictReader(handle)}


def test_the_three_capped_deployables_state_their_caps():
    stated = {r["Name"]: int(params(r["ShapeParams"]).get("MaxActive", 0))
              for r in deployable_rows()}
    assert {name: stated.get(name) for name in CAPPED} == CAPPED


def test_no_cap_binds_at_base_timing():
    cooldowns = slot_cooldowns()
    binding = []
    for row in deployable_rows():
        stated = params(row["ShapeParams"])
        cap = int(stated.get("MaxActive", 0))
        if cap <= 0:
            continue
        cooldown = float(row["Cooldown"]) if float(row["Cooldown"]) > 0 else cooldowns[row["Slot"]]
        standing = machines_per_cast(stated.get("Minions", "")) * math.ceil(
            float(stated["Duration"]) / cooldown)
        if standing > cap:
            binding.append(f"{row['Name']}: {standing} can stand at base timing, cap {cap}")
    assert not binding, "; ".join(binding)


def test_the_check_names_a_cap_below_base_timing():
    """THE CHECK CAN FAIL: a turret lasting 5 seconds on a 1-second cooldown
    stands 5 at once, which a cap of 1 would refuse."""
    assert machines_per_cast("BoltTurret:1") * math.ceil(5.0 / 1.0) > 1
