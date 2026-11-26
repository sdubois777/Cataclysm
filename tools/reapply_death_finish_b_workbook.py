"""Write the rows of the Death line's finishing slice B, the Necromancer's army, into the design workbook.

    python tools/reapply_death_finish_b_workbook.py
    python tools/reapply_death_finish_b_workbook.py --check

THE DEATH LINE'S FINISHING SLICE B (2026-11-26): the Necromancer's Legion limb, the spine's husks raised on a kill,
and four vow options. Every node and option below had no row. The code each new stat names is
`game/Source/Cataclysm/AbilitySystem/CataclysmNecromancerArmy.cpp`; the decisions and judgements are in
`docs/DECISIONS.md` under the entry of 2026-11-26 for this slice.

  - "YOUR PERMANENT MINIONS" IS THE TAG `Minion.Permanent`, which a `Permanent=1` summon's minion and every thrall carry
    (`UCataclysmNecromancerArmy::MakePermanent`, `UCataclysmCommand::ThrallMinionTags`).
  - Many Made One (`Necromancer_basic_lg_d0`) is slice D's and is not here.
  - Rise Together writes a stat of its own, `permanent_minion_raises_a_second_chance`, rolled once for each permanent
    minion raised: the coordinating session's ruling of 2026-11-26 (it first ruled the opposite the same day, one
    shared stat with slice C's Teeming Grave, which is rolled once a use).
  - Lone Revenant also writes `lone_revenant_held`, the flag slice D's Abomination reads (the same ruling day).

THE MACHINERY IS `tools/reapply_war_data_rows_workbook.py`'s, imported. IT IS IDEMPOTENT and REFUSES RATHER THAN
GUESSES, as that script does. `--check` writes nothing and exits 1 when a row would be written.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_demonic_tree_rework_workbook as helpers  # noqa: E402
import reapply_war_data_rows_workbook as base  # noqa: E402

R = base.Row

LABEL = "the Death line's finishing slice B"

#: The tag every "your permanent minions" row requires.
PERMANENT = "Minion.Permanent"

#: The Restless Dead's "a chance": the sentence states none, so this is a judgement of the slice (see the module's
#: DECISIONS entry): one kill in five, with the 3 Fervour each skeleton reserves as the real limit.
RESTLESS_DEAD_CHANCE = 20

#: (node, Node Name label, rows), in the shape `base.NODES` takes. An option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[base.Row, ...]], ...] = (
    # ---- the Legion limb
    ("Necromancer_basic_lg_stem0", "Standing Army", (
        R("minion_damage", "increased", 3, tags=PERMANENT),
        R("minion_health", "increased", 3, tags=PERMANENT))),
    ("Necromancer_basic_lg_stem1", "Reforged", (
        R("summon_without_room_heals_permanent_percent", "flat", 10),)),
    ("Necromancer_basic_lg_stem2", "Motley Host", (
        R("minion_damage", "increased", 2, scale="minion_kinds_beyond_the_first", step=1),)),
    ("Necromancer_basic_lg_a0", "Old Bones", (
        R("minion_damage", "increased", 1, tags=PERMANENT, scale="minion_seconds_active", step=30, cap=4),)),
    ("Necromancer_basic_lg_a1", "First Among the Dead", (
        R("minion_damage", "increased", 4, tags=PERMANENT, cond="minion_has_your_most_maximum_health"),
        R("minion_health", "increased", 4, tags=PERMANENT, cond="minion_has_your_most_maximum_health"))),
    ("Necromancer_basic_lg_a2", "Master's Edge", (
        R("minion_crit_share_of_yours_percent", "flat", 4, tags=PERMANENT),)),
    ("Necromancer_basic_lg_b0", "Lighter Bones", (
        R("minion_reserve_percent_less", "flat", 2, tags=PERMANENT),)),
    ("Necromancer_basic_lg_b1", "Will Not Stay Down", (
        R("minion_rises_again_chance", "flat", 4, tags=PERMANENT),)),
    ("Necromancer_basic_lg_b2", "Rise Together", (
        R("permanent_minion_raises_a_second_chance", "flat", 4, tags=PERMANENT),)),
    ("Necromancer_basic_lg_c0", "Mending Dead", tuple(
        R("minion_health_regen_percent_per_second", "flat", 1, tags=PERMANENT, min_points=points)
        for points in (2, 4, 6))),
    ("Necromancer_basic_lg_c1", "Called to Heel", (
        R("minion_damage_taken", "increased", -3, tags=PERMANENT, cond="minion_within_metres_of_you", cond_value=8),)),
    ("Necromancer_keystone_lg_kA", "Lone Revenant", (
        R("minion_reserve", "more", 300, tags=PERMANENT),
        R("minion_damage", "more", 400, tags=PERMANENT),
        R("minion_health", "more", 400, tags=PERMANENT),
        R("lone_revenant_held", "flat", 1))),
    ("Necromancer_keystone_lg_kB", "Bone Lord", (
        R("summon_raises_more_permanent_minions", "flat", 2),)),
    # ---- the spine's husks raised on a kill
    ("Necromancer_basic_spine_010", "Carrion Host", (
        R("permanent_minion_kill_rises_as_husk_chance", "flat", 3),)),
    ("Necromancer_keystone_spine_001", "Second Death", (
        R("minion_death_rises_as_husk", "flat", 1),)),
    ("Necromancer_keystone_spine_003", "Harvest of the Host", (
        R("permanent_minion_kill_rises_as_husk_chance", "flat", 100),)),
    # ---- the vow options
    ("Necromancer_capstone_25", "The First Rite", (
        R("kill_rises_as_skeleton_chance", "flat", RESTLESS_DEAD_CHANCE, option=1),)),
    ("Necromancer_capstone_50", "The Second Rite", (
        R("movement_swaps_with_minion", "flat", 1, option=1),)),
    ("Necromancer_capstone_100", "The Third Rite", (
        R("attack_damage", "more", 50, cond="seconds_after_resource_full", cond_value=10, option=1),
        R("spell_damage", "more", 50, cond="seconds_after_resource_full", cond_value=10, option=1),
        R("skills_raise_husks_seconds_after_resource_full", "flat", 10, option=1),
        R("damage_taken_split_with_minions", "flat", 1, option=2))),
)


#: The Tags sheet's new row, (Tag Name, Description): the generator refuses a Required Tag the sheet does not declare,
#: and `tools/generate_gameplay_tags.py` writes `game/Config/Tags/CataclysmTags.ini` from the sheet.
PERMANENT_TAG = (PERMANENT, "A minion that stands until it is killed: a Permanent=1 summon's minion and every thrall. "
                            "The Necromancer's rows for your permanent minions require it.")

TAG_LIST = base.REPO_ROOT / "tools" / "generate_gameplay_tags.py"


def apply_tag(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Declare `Minion.Permanent` on the Tags sheet when it is not; refuse a row somebody wrote differently."""
    book = openpyxl.load_workbook(path)
    helpers.refuse_what_a_save_would_lose(book)
    tags = book["Tags"]
    declared = [row for row in tags.iter_rows(min_row=2) if row[0].value == PERMANENT_TAG[0]]
    if len(declared) > 1:
        raise base.ReapplyError(f"{LABEL}: the Tags sheet declares {PERMANENT} {len(declared)} times. "
                                "Nothing was written")
    if declared:
        if declared[0][1].value != PERMANENT_TAG[1]:
            raise base.ReapplyError(f"{LABEL}: the Tags sheet declares {PERMANENT} as {declared[0][1].value!r}: "
                                    "somebody has written it since. Nothing was written")
        return [f"Tags: {PERMANENT} is already declared; nothing written"]
    if check_only:
        return [f"Tags: + {PERMANENT} (--check)"]
    before = {sheet.title: helpers._snapshot(sheet) for sheet in book.worksheets}
    last = max((cell.row for row in tags.iter_rows(min_row=2, max_col=2) for cell in row if cell.value is not None),
               default=1)
    tags.cell(last + 1, 1).value = PERMANENT_TAG[0]
    tags.cell(last + 1, 2).value = PERMANENT_TAG[1]
    book.save(path)
    after = openpyxl.load_workbook(path)
    for title, cells in before.items():
        expected = dict(cells)
        if title == "Tags":
            expected[(last + 1, 1)] = PERMANENT_TAG[0]
            expected[(last + 1, 2)] = PERMANENT_TAG[1]
        if helpers._snapshot(after[title]) != expected:
            raise base.ReapplyError(f"{LABEL}: the sheet {title!r} did not read back as written")
    return [f"Tags: + {PERMANENT}; no other cell or sheet changed"]


def apply(path: pathlib.Path = base.WORKBOOK, check_only: bool = False) -> list[str]:
    """Declare the tag and append the rows the workbook at `path` lacks; return the report."""
    return apply_tag(path, check_only=check_only) + base.apply(path, check_only=check_only, nodes=NODES, label=LABEL)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=base.WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true", help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except base.ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if any(line.endswith("(--check)") for line in report) else 0
    if args.no_generate:
        return 0
    # THE CSVs, AND THE TAG LIST UNREAL READS, written from the Tags sheet by its own generator.
    for command in ([sys.executable, str(base.GENERATOR), "--workbook", str(args.workbook)],
                    [sys.executable, str(TAG_LIST)]):
        result = subprocess.run(command, cwd=base.REPO_ROOT)
        if result.returncode != 0:
            return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
