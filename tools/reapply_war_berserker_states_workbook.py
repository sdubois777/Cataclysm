"""Write the Berserker's states' rows into the design workbook's Passive Effects sheet.

    python tools/reapply_war_berserker_states_workbook.py
    python tools/reapply_war_berserker_states_workbook.py --check

THE BERSERKER'S STATES, `plan_death_and_war.md` slice B29 (2026-11-25): the rows of the three nodes the states are,
each sentence's numbers as the tree file states them. Eight rows on three nodes:

  - Wrath (`Berserker_twoh_001`): the flag that lets a full pool begin Wrath, stated under
    `wielding_two_handed_weapon` ("Two-handed weapons only"); +15% increased damage on melee attacks and a drain of 5
    Fervour a second, both under `in_wrath`.
  - Frenzy (`Berserker_dw_001`): the flag, under `dual_wielding` ("Dual wield only"); +20% increased attack speed, 2
    Fervour for each melee blow landed, and a drain of 8 a second, all under `in_frenzy`.
  - Berserking (`Berserker_trunk_009`): 10% increased damage for each point of Fervour the next melee attack consumes,
    a point, on `Type.Melee`.

"YOUR MELEE ATTACKS DEAL +15% DAMAGE" IS ATTACK DAMAGE ALONE, scoped to melee: it names attacks, as the Ravager's "your
attacks deal 20% more damage" does, so the owner's 2026-08-25 rule that plain "damage" is attack and spell damage does
not reach it. "+10% BONUS DAMAGE" IS AN INCREASE, the bucket Bought With Ruin's Fervour buys into, on the same sum.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D), imported: the row shape, the refusal of a node
somebody wrote since, the read-back, and the generator run. IT IS IDEMPOTENT and REFUSES RATHER THAN GUESSES, as that
script does. `--check` writes nothing and exits 1 when a row would be written.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row
MELEE = base.MELEE
LABEL = "the Berserker's states"

#: (node, Node Name label, rows), in the shape `base.NODES` takes.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Berserker_twoh_001", "Wrath", (
        R("wrath_on_full_fervour", "flat", 1, cond="wielding_two_handed_weapon"),
        R("attack_damage", "increased", 15, tags=MELEE, cond="in_wrath"),
        R("fervour_drain_per_second", "flat", 5, cond="in_wrath"))),
    ("Berserker_dw_001", "Frenzy", (
        R("frenzy_on_full_fervour", "flat", 1, cond="dual_wielding"),
        R("attack_speed", "increased", 20, cond="in_frenzy"),
        R("fervour_per_hit_dealt", "flat", 2, tags=MELEE, cond="in_frenzy"),
        R("fervour_drain_per_second", "flat", 8, cond="in_frenzy"))),
    ("Berserker_trunk_009", "Berserking", (
        R("berserking_damage_per_fervour", "flat", 10, tags=MELEE),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
