"""Write the rows of the Death line's finishing slice E, the Shadow's last vows and Ever Moving, into the design workbook.

    python tools/reapply_death_finish_e_shadow_workbook.py
    python tools/reapply_death_finish_e_shadow_workbook.py --check

THE DEATH LINE'S FINISHING SLICE E (2026-11-26), on the owner's request that day to "finish whatever is left in the death
cataclysm". Four vow options of the Shadow and one keystone, six rows, each a stat with no gameplay attribute that the
engine reads where the option acts (`docs/DECISIONS.md`, the entry of 2026-11-26, carries every judgement):

  - Afterimage (Shadow_capstone_25, option 2): "A shadow of you stays where your movement skill started and repeats
    your next attack." A flag, `afterimage_repeats_your_next_attack` 1, read by `UCataclysmShadowImages`. Issue #2906.
  - Shadow Clone (Shadow_capstone_50, option 2): "Every third kill of an Elite or stronger leaves a clone of you that
    fights for 5 seconds with your basic attack." `shadow_clone_every_elite_kills` 3 and `shadow_clone_seconds` 5.
    Issue #2907.
  - Shadow Step (Shadow_capstone_100, option 1): "Your movement skill can target any enemy within 15 metres that you
    can see and puts you behind it." `movement_targets_an_enemy_within_metres` 15. Issue #2909.
  - Flurry of the Dead (Shadow_capstone_200, option 3): "Your hits strike again once for every 10 Fervour held, up to
    10 extra strikes." `hits_strike_again` 1 for every 10 Fervour held (`class_resource_held`, step 10), capped at 10
    steps, so the stat itself is the number of extra strikes.
  - Ever Moving (Shadow_keystone_d_kB): "While you are moving, your Evasion chance also applies to area damage, at
    half its value." `evasion_against_area_percent` 50 under `while_moving`, read on the defender by
    `UCataclysmDamageCalculation::Resolve`.

Each capstone already holds other options' rows (written by `tools/reapply_death_trees_workbook.py`); a group is one
option of one node, so none reads another's.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s, imported, as every later slice's script imports it. IT IS
IDEMPOTENT and REFUSES RATHER THAN GUESSES. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the Death line's finishing slice E (the Shadow)"

#: Fervour held, as the rows that scale with it name it.
HELD = "class_resource_held"

#: Shadow Clone's "every third kill" and "for 5 seconds".
CLONE_EVERY = 3
CLONE_SECONDS = 5
#: Shadow Step's "within 15 metres".
STEP_METRES = 15
#: Flurry of the Dead's "once for every 10 Fervour held, up to 10 extra strikes".
FLURRY_STEP = 10
FLURRY_CAP = 10
#: Ever Moving's "at half its value", in per cent of the evasion chance.
EVER_MOVING_PERCENT = 50

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number, and its
#: label is the option's own name, as `tools/reapply_death_trees_workbook.py` labels an option.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Shadow_capstone_25", "Afterimage", (
        R("afterimage_repeats_your_next_attack", "flat", 1, option=2),)),
    ("Shadow_capstone_50", "Shadow Clone", (
        R("shadow_clone_every_elite_kills", "flat", CLONE_EVERY, option=2),
        R("shadow_clone_seconds", "flat", CLONE_SECONDS, option=2))),
    ("Shadow_capstone_100", "Shadow Step", (
        R("movement_targets_an_enemy_within_metres", "flat", STEP_METRES, option=1),)),
    ("Shadow_capstone_200", "Flurry of the Dead", (
        R("hits_strike_again", "flat", 1, scale=HELD, step=FLURRY_STEP, cap=FLURRY_CAP, option=3),)),
    ("Shadow_keystone_d_kB", "Ever Moving", (
        R("evasion_against_area_percent", "flat", EVER_MOVING_PERCENT, cond="while_moving"),)),
)


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report."""
    return base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    return base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])


if __name__ == "__main__":
    sys.exit(main())
