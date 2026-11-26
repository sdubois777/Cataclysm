"""Write the remains rows, the Necromancer's Ossuary limb, its two vows and Many Made One, into the design workbook.

    python tools/reapply_death_remains_workbook.py
    python tools/reapply_death_remains_workbook.py --check

SLICE D OF THE DEATH FINISH (2026-11-26), on the remains design the coordinating session approved that day: one actor,
`ACataclysmRemains`, left where a hostile creature dies for each player holding `remains_left`, seen only by that
player, raised on by a summon skill, detonated by a weapon skill's landing, and fed to the Bone Colossus. Every
number below is the node's own sentence:

  - Bone Fuse (keystone): remains appear (`remains_left`), and each minion raised on remains generates 3 Fervour (an
    action row on `minion_raised_on_remains`).
  - Gravefinder: 1 metre per point (`remains_raise_reach_metres`).
  - Raised on Bones: +5% increased damage and health per point, on minions carrying `Minion.RaisedOnRemains`.
  - Mantle of Bone: the detonation (`weapon_skills_detonate_remains`), 1% of the creature's maximum health for every 2
    points (a row at each of 2, 4 ... 12), and +1% increased Armor for every 2 points per remains within 6 metres, up
    to 10 (a row at each of 2, 4 ... 12 on the scale `remains_in_reach`).
  - Gathered Remains: 25% longer per point (`remains_duration`, increased).
  - Fallen Again: 6% per point (`minion_leaves_remains_chance`).
  - Bone-Bound: 4% reduced Fervour per point (`minion_on_remains_reserve_percent_less`).
  - Picked Clean: 1% of maximum health for every 3 points (action rows on `remains_consumed` at 3 and 6).
  - Two From One: 4% per point (`remains_kept_on_raise_chance`).
  - Festering: +2% increased damage per point per ailment (`remains_detonation_increased_per_ailment`).
  - Grave Spores: 6% per point to inflict Necrosis (an `apply_status` row on `remains_detonation_hit`).
  - Grave Robber (keystone): 5 Fervour more for the raise (an action row on `minion_raised_on_remains`).
  - Plague Pit (keystone): `detonated_remains_pass_ailments`.
  - Many Made One: 2 off the percentage per point (`abomination_kill_percent_less`).
  - The Final Rite, option 1, Abomination: `abomination_rises`. Its numbers are the engine's (`UCataclysmBoneGiants`).
  - The First Rite, option 2, Bone Colossus: `bone_colossus`, and `remains_left`, because Bone Fuse's sentence says
    remains appear with "this keystone, or the Bone Colossus vow".

AND ONE TAG, `Minion.RaisedOnRemains`, appended to the Tags sheet (which `game/Config/Tags/CataclysmTags.ini` is
generated from) when the sheet lacks it, before the rows: Raised on Bones' rows require it, and the generator refuses a
row naming a tag the sheet does not declare. Every other sheet is read back unchanged, as the rows' machinery does.

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s (slice D of the War line), imported. IT IS IDEMPOTENT and
REFUSES RATHER THAN GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_demonic_tree_rework_workbook as helpers  # noqa: E402
import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the Death finish's remains slice (D)"

#: The tag a minion raised on remains carries, which Raised on Bones' rows require.
RAISED_ON_REMAINS_TAG = "Minion.RaisedOnRemains"

#: The script that writes the tag list from the Tags sheet.
TAG_LIST = pathlib.Path(__file__).resolve().parent / "generate_gameplay_tags.py"

#: The Tags sheet, its header, and the description the tag is declared with.
TAGS_SHEET = "Tags"
RAISED_ON_REMAINS_DESCRIPTION = ("A minion a summon skill raised on its summoner's remains. Raised on Bones' rows "
                                 "require it.")

#: The sentences' numbers.
BONE_FUSE_FERVOUR = 3
GRAVE_ROBBER_FERVOUR = 5
GRAVEFINDER_METRES_PER_POINT = 1
RAISED_ON_BONES_PERCENT = 5
MANTLE_POINTS_PER_STEP = 2
MANTLE_MAX_POINTS = 12
MANTLE_REACH_METRES = 6
MANTLE_MOST_REMAINS = 10
GATHERED_PERCENT = 25
FALLEN_AGAIN_CHANCE = 6
BONE_BOUND_PERCENT = 4
PICKED_CLEAN_POINTS_PER_STEP = 3
PICKED_CLEAN_MAX_POINTS = 8
TWO_FROM_ONE_CHANCE = 4
FESTERING_PERCENT = 2
GRAVE_SPORES_CHANCE = 6
MANY_MADE_ONE_PERCENT = 2

MANTLE_STEPS = tuple(range(MANTLE_POINTS_PER_STEP, MANTLE_MAX_POINTS + 1, MANTLE_POINTS_PER_STEP))
PICKED_CLEAN_STEPS = tuple(range(PICKED_CLEAN_POINTS_PER_STEP, PICKED_CLEAN_MAX_POINTS + 1,
                                 PICKED_CLEAN_POINTS_PER_STEP))

#: (node, Node Name label, rows), in the shape `base.NODES` takes. A capstone option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    ("Necromancer_basic_os_stem0", "Bone Fuse", (
        R("remains_left", "flat", 1),
        R(None, "", BONE_FUSE_FERVOUR, action="class_resource", action_event="minion_raised_on_remains",
          fraction_of="points", trigger_cooldown=0))),
    ("Necromancer_basic_os_stem1", "Gravefinder", (
        R("remains_raise_reach_metres", "flat", GRAVEFINDER_METRES_PER_POINT),)),
    ("Necromancer_basic_os_stem2", "Raised on Bones", (
        R("minion_damage", "increased", RAISED_ON_BONES_PERCENT, tags=RAISED_ON_REMAINS_TAG),
        R("minion_health", "increased", RAISED_ON_BONES_PERCENT, tags=RAISED_ON_REMAINS_TAG))),
    ("Necromancer_basic_os_a0", "Mantle of Bone", (
        R("weapon_skills_detonate_remains", "flat", 1, min_points=1),
        *(R("remains_detonation_percent_of_maximum_health", "flat", 1, min_points=step) for step in MANTLE_STEPS),
        *(R("armor", "increased", 1, scale="remains_in_reach", step=1, cap=MANTLE_MOST_REMAINS,
            reach=MANTLE_REACH_METRES, min_points=step) for step in MANTLE_STEPS))),
    ("Necromancer_basic_os_a1", "Gathered Remains", (R("remains_duration", "increased", GATHERED_PERCENT),)),
    ("Necromancer_basic_os_a2", "Fallen Again", (R("minion_leaves_remains_chance", "flat", FALLEN_AGAIN_CHANCE),)),
    ("Necromancer_basic_os_b0", "Bone-Bound", (
        R("minion_on_remains_reserve_percent_less", "flat", BONE_BOUND_PERCENT),)),
    ("Necromancer_basic_os_b1", "Picked Clean", tuple(
        R(None, "", 1, action="health", action_event="remains_consumed", fraction_of="maximum", min_points=step,
          trigger_cooldown=0) for step in PICKED_CLEAN_STEPS)),
    ("Necromancer_basic_os_b2", "Two From One", (R("remains_kept_on_raise_chance", "flat", TWO_FROM_ONE_CHANCE),)),
    ("Necromancer_basic_os_c0", "Festering", (
        R("remains_detonation_increased_per_ailment", "flat", FESTERING_PERCENT),)),
    ("Necromancer_basic_os_c1", "Grave Spores", (
        R(None, "", GRAVE_SPORES_CHANCE, action="apply_status", action_event="remains_detonation_hit",
          ailment="Necrosis", trigger_cooldown=0),)),
    ("Necromancer_keystone_os_kB", "Grave Robber", (
        R(None, "", GRAVE_ROBBER_FERVOUR, action="class_resource", action_event="minion_raised_on_remains",
          fraction_of="points", trigger_cooldown=0),)),
    ("Necromancer_keystone_os_kC", "Plague Pit", (R("detonated_remains_pass_ailments", "flat", 1),)),
    ("Necromancer_basic_lg_d0", "Many Made One", (R("abomination_kill_percent_less", "flat", MANY_MADE_ONE_PERCENT),)),
    ("Necromancer_capstone_200", "The Final Rite", (R("abomination_rises", "flat", 1, option=1),)),
    ("Necromancer_capstone_25", "The First Rite", (
        R("bone_colossus", "flat", 1, option=2),
        R("remains_left", "flat", 1, option=2))),
)


def apply_tag(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append `RAISED_ON_REMAINS_TAG` to the Tags sheet when it lacks it; return the report."""
    book = openpyxl.load_workbook(path)
    helpers.refuse_what_a_save_would_lose(book)
    sheet = book[TAGS_SHEET]
    held = [row[0].row for row in sheet.iter_rows(min_row=2, max_col=1)
            if isinstance(row[0].value, str) and row[0].value.strip() == RAISED_ON_REMAINS_TAG]
    if len(held) > 1:
        raise base.ReapplyError(f"{TAGS_SHEET!r} declares {RAISED_ON_REMAINS_TAG} {len(held)} times. Nothing was "
                                f"written")
    if held:
        return [f"{TAGS_SHEET}: {RAISED_ON_REMAINS_TAG} is already declared; nothing written"]
    if check_only:
        return [f"{TAGS_SHEET}: + {RAISED_ON_REMAINS_TAG}", "1 tag to write; nothing written (--check)"]
    others = {s.title: helpers._snapshot(s) for s in book.worksheets if s.title != TAGS_SHEET}
    before = helpers._snapshot(sheet)
    last = max((cell.row for row in sheet.iter_rows(min_row=2, max_col=2) for cell in row
                if cell.value is not None), default=1)
    sheet.cell(last + 1, 1).value = RAISED_ON_REMAINS_TAG
    sheet.cell(last + 1, 2).value = RAISED_ON_REMAINS_DESCRIPTION
    book.save(path)
    after = openpyxl.load_workbook(path)
    for title, cells in others.items():
        if helpers._snapshot(after[title]) != cells:
            raise base.ReapplyError(f"the sheet {title!r} changed on saving, and only {TAGS_SHEET!r} was written")
    kept = {where: value for where, value in helpers._snapshot(after[TAGS_SHEET]).items() if where[0] <= last}
    if kept != before or after[TAGS_SHEET].cell(last + 1, 1).value != RAISED_ON_REMAINS_TAG:
        raise base.ReapplyError(f"the workbook WAS SAVED, and {TAGS_SHEET!r} did not read back as written")
    return [f"{TAGS_SHEET}: + {RAISED_ON_REMAINS_TAG}", f"wrote 1 tag after sheet row {last}"]


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Append the tag and the rows the workbook at `path` lacks; return the report."""
    return apply_tag(path, check_only=check_only) + base.apply(path, check_only=check_only, nodes=NODES,
                                                                label=LABEL)


def main(argv: list[str] | None = None) -> int:
    check = argv is not None and "--check" in argv or argv is None and "--check" in sys.argv[1:]
    workbook = base.WORKBOOK
    arguments = list(sys.argv[1:] if argv is None else argv)
    if "--workbook" in arguments:
        workbook = pathlib.Path(arguments[arguments.index("--workbook") + 1])
    try:
        tag_report = apply_tag(workbook, check_only=check)
    except base.ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in tag_report:
        print(line, flush=True)
    status = base.main(argv, nodes=NODES, label=LABEL, description=__doc__.splitlines()[0])
    if check and status == 0 and any(line.endswith("(--check)") for line in tag_report):
        return 1
    if check or status != 0 or "--no-generate" in arguments:
        return status
    # AND THE TAG LIST, which `tools/generate_datatables.py` does not write: `tools/generate_gameplay_tags.py` writes
    # `game/Config/Tags/CataclysmTags.ini` from the Tags sheet, as `tools/apply_gadget_tag_2026_11_25.py` runs it.
    return subprocess.run([sys.executable, str(TAG_LIST)], cwd=base.REPO_ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
