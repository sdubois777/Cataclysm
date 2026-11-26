"""Write the Necromancer's detonation rows -- the Charnel limb and two vows -- into the design workbook.

    python tools/reapply_death_finish_c_workbook.py            # write the rows, then regenerate the CSVs
    python tools/reapply_death_finish_c_workbook.py --check    # say what would change, write nothing
    python tools/reapply_death_finish_c_workbook.py --workbook <copy.xlsx> --no-generate

THE FINISHING OF THE DEATH CATACLYSM, SLICE C (2026-11-26). The owner: "finish whatever is left in the death
cataclysm". This slice is the Necromancer's Charnel limb (every `Necromancer_*_ch_*` node that held no row: Lit Fuse,
Made Room, Teeming Grave, Close Packed, Lingering Rot, Bursting Ranks, Swollen, Ripened, Mass Grave and Last Breath) and
two vows, the Second Rite's Living Bombs and the Final Rite's The Horde Ascendant. Bone Colossus, the First Rite's second
option, moved to slice D by the coordinating session's ruling of the same day, because it eats remains, which slice D
builds. `docs/DECISIONS.md` carries the judgements.

THE MACHINERY IS `tools/reapply_death_trees_workbook.py`'s, imported: its `Row`, its `Node`, its plan and its refusals.
A node already holding exactly these rows is left alone; one holding any other rows was written by somebody else, and
nothing is written. Rows for a node with none are appended at the end of the sheet, AFTER the Death trees' own rows,
which is why `tools/tests/test_reapply_death_trees_workbook.py` runs this script after that one when it rebuilds the
sheet. Only the Passive Effects sheet is written, and the script proves on reading back that no other sheet changed.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_death_trees_workbook as base  # noqa: E402

R = base.Row
Node = base.Node
NE = base.NE

WORKBOOK = base.WORKBOOK
GENERATOR = base.GENERATOR
EFFECTS_SHEET = base.EFFECTS_SHEET
ReapplyError = base.ReapplyError

#: A detonation's damage, which Made Room raises and Close Packed, Bursting Ranks and Swollen raise by a count.
DETONATION_DAMAGE = "minion_detonation_damage"

#: "up to 5", the cap Close Packed, Bursting Ranks and Swollen each state.
UP_TO = 5

NODES: tuple[base.Node, ...] = (
    # LIT FUSE: "Your weapon skills that land on one of your own minions detonate it, whatever kind it is." A flag.
    Node(NE + "basic_ch_stem0", "Lit Fuse", (R("weapon_skills_detonate_own_minions", "flat", 1),)),
    # MADE ROOM: the release's detonation is one flag granted whole from the first point; "+2% increased area and deal
    # +2% increased damage per point" are two per-point rows.
    Node(NE + "basic_ch_stem1", "Made Room",
         (R("minion_detonates_when_released", "flat", 1, min_points=1),
          R("minion_detonation_area", "increased", 2),
          R(DETONATION_DAMAGE, "increased", 2))),
    # TEEMING GRAVE: "an 8% chance per point to raise one more", on the stat Rise Together shares (the coordinating
    # session's ruling of 2026-11-26).
    Node(NE + "basic_ch_stem2", "Teeming Grave", (R("minion_extra_raise_chance", "flat", 8),)),
    # CLOSE PACKED: "+1% increased damage per point for each other minion of yours within 3 metres of it, up to 5".
    Node(NE + "basic_ch_a0", "Close Packed",
         (R(DETONATION_DAMAGE, "increased", 1, scale="minions_near_the_minion", step=1, reach=3, cap=UP_TO),)),
    # LINGERING ROT: "rotting ground for 1 second for every 2 points" is one row per step, each granted whole from its
    # Min Points, the shape every "for every 2 points" sentence takes; "10% of the detonation's damage each second" is
    # one figure, granted whole once there is ground to deal it.
    Node(NE + "basic_ch_a1", "Lingering Rot",
         (R("minion_detonation_rot_seconds", "flat", 1, min_points=2),
          R("minion_detonation_rot_seconds", "flat", 1, min_points=4),
          R("minion_detonation_rot_seconds", "flat", 1, min_points=6),
          R("minion_detonation_rot_seconds", "flat", 1, min_points=8),
          R("minion_detonation_rot_percent", "flat", 10, min_points=2))),
    # BURSTING RANKS: "+1% increased damage per point for each detonation of yours in the last 2 seconds, of a minion or
    # of remains, up to 5": own stacks granted by `minion_detonated`, lasting 2 seconds, at most 5.
    Node(NE + "basic_ch_a2", "Bursting Ranks",
         (R(DETONATION_DAMAGE, "increased", 1, scale="own_stacks", step=1, cap=UP_TO, event="minion_detonated",
            stack_seconds=2),)),
    # SWOLLEN: "+1% increased damage per point for each full second it has stood, up to 5 seconds".
    Node(NE + "basic_ch_b1", "Swollen",
         (R(DETONATION_DAMAGE, "increased", 1, scale="minion_seconds_active", step=1, cap=UP_TO),)),
    # RIPENED: "a 10% chance per point to explode anyway".
    Node(NE + "basic_ch_b2", "Ripened", (R("minion_explodes_at_lifespan_end_chance", "flat", 10),)),
    # MASS GRAVE: "detonates every other minion of yours within 3 metres, each for 40% damage".
    Node(NE + "keystone_ch_kA", "Mass Grave",
         (R("minion_detonation_spreads_percent", "flat", 40),
          R("minion_detonation_spread_radius_metres", "flat", 3))),
    # LAST BREATH: "you gain Fervour equal to twice what it reserved".
    Node(NE + "keystone_ch_kC", "Last Breath", (R("fervour_on_minion_detonated_reserve_multiple", "flat", 2),)),
    # THE SECOND RITE'S LIVING BOMBS (option 2): "Your minions run at the nearest enemy and detonate on contact, and each
    # detonation has a 50% chance to raise a husk that lasts 8 seconds where it went off."
    Node(NE + "capstone_50", "Living Bombs",
         (R("minions_detonate_on_contact", "flat", 1),
          R("minion_detonation_raises_husk_chance", "flat", 50),
          R("minion_detonation_husk_seconds", "flat", 8)), option=2),
    # THE FINAL RITE'S THE HORDE ASCENDANT (option 3), built as written by the coordinating session's ruling of
    # 2026-11-26: "Whenever you cast a skill, every minion within 10 metres casts it too, at 10% of its damage."
    Node(NE + "capstone_200", "The Horde Ascendant",
         (R("minions_cast_your_skills_within_metres", "flat", 10),
          R("minion_skill_copy_damage_percent", "flat", 10)), option=3),
)


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Add this slice's rows to the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    base.refuse_what_a_save_would_lose(book)
    effects = base.plan_effects(book[EFFECTS_SHEET], NODES)
    if not effects.changed:
        return ["every detonation row is already as this script writes it; nothing written"]
    if check_only:
        return effects.report + [f"{effects.changed} change(s) to make; nothing written (--check)"]

    others = {s.title: base._snapshot(s) for s in book.worksheets if s.title != EFFECTS_SHEET}
    base._write(book[EFFECTS_SHEET], effects)
    book.save(path)

    # READ BACK FROM DISK: every other sheet as it was, and this one exactly as planned.
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if base._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and only {EFFECTS_SHEET!r} was written")
    base._read_back(after, effects, EFFECTS_SHEET)
    return effects.report + [f"wrote {effects.changed} change(s); {EFFECTS_SHEET!r} now holds {len(effects.new)} rows "
                             f"(was {len(effects.old)}), and no other sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true",
                        help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if any(line.endswith("(--check)") for line in report) else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)],
                            cwd=base.REPO_ROOT)
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py "
          "tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
