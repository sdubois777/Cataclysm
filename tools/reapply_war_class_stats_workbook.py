"""Write the three War class stat lines -- the Bulwark, the Berserker and the Saboteur -- into the design workbook.

Issue #932, as ruled by the coordinating session on 2026-11-27: no War class had a line in the `Class Stats` sheet, so a
character spending its points in a War tree stood on the `Default` line, which has no armour, block, evasion or damage
reduction. The three lines below are JUDGEMENTS under the owner's delegation of unstated numbers, as the three Death
lines were (the Death trees' slice A, 2026-11-24): each class leans on the defence its tree is built round, and
everything a line does not name falls back to `Default`. `docs/DECISIONS.md` carries the entry with the reasons.

TO CHANGE A NUMBER, edit `CLASS_STATS`, take `development`'s workbook and run this again; it refuses a War stat that
already holds other numbers, so a hand edit in the sheet is never overwritten in silence. It writes the `Class Stats`
sheet only and reads every other sheet back unchanged.

    python tools/reapply_war_class_stats_workbook.py            # write, then regenerate the CSVs
    python tools/reapply_war_class_stats_workbook.py --check    # report only; exit 1 if anything would change
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import reapply_death_trees_workbook as death  # noqa: E402

WORKBOOK = death.WORKBOOK
CLASS_SHEET = death.CLASS_SHEET
ReapplyError = death.ReapplyError

#: The War classes, in the order the design lists them (`UCataclysmCharacterCreation::ClassesByDamageType`).
WAR_CLASSES = ("Bulwark", "Berserker", "Saboteur")

#: (class, stat, level 1 base, gain per level). The Bulwark stands on health, armour, block and damage reduction; the
#: Berserker on health, leech and critical strike multiplier and is quicker; the Saboteur on a larger area and speed,
#: with less health and more mana for its devices. NO EVASION BASE FOR THE SABOTEUR, though its tree raises evasion: no
#: class has one, the rule the Shadow's line was held to on 2026-11-24 (the gem check took a first Shadow evasion base
#: out). Scaled against the six lines already in the sheet: the Bulwark below the Masochist's health and above the
#: Ravager's, the Saboteur between the Necromancer and the Shadow.
CLASS_STATS: tuple[tuple[str, str, float, float], ...] = (
    ("Bulwark", "max_health", 140, 21),
    ("Bulwark", "health_regen", 2, 0.25),
    ("Bulwark", "armor", 30, 4),
    ("Bulwark", "block_chance", 10, 0),
    ("Bulwark", "damage_reduction", 3, 0.05),
    ("Bulwark", "crowd_control_resistance", 8, 0.15),
    ("Bulwark", "movement_speed", 3.9, 0),
    ("Berserker", "max_health", 120, 18),
    ("Berserker", "life_leech", 1, 0.02),
    ("Berserker", "crit_multiplier", 160, 0),
    ("Berserker", "damage_reduction", 1, 0.02),
    ("Berserker", "movement_speed", 4.4, 0),
    ("Saboteur", "max_health", 95, 14),
    ("Saboteur", "max_mana", 60, 7),
    ("Saboteur", "area_of_effect", 110, 0),
    ("Saboteur", "movement_speed", 4.5, 0),
)


def plan_class_stats(sheet, lines=CLASS_STATS) -> death.SheetPlan:
    """The Class Stats sheet's new rows, refusing a War class stat that already holds other numbers."""
    header, old = death._read(sheet, death.CLASS_COLUMNS)
    report: list[str] = []
    added: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    for class_name, stat, base, per_level in lines:
        if class_name not in WAR_CLASSES:
            raise ReapplyError(f"{class_name} is not a War class; this script writes War lines only")
        if (class_name, stat) in seen:
            raise ReapplyError(f"{class_name} {stat} is listed twice")
        seen.add((class_name, stat))
        current = [cells for cells in old
                   if death._text(cells.get("Class")) == class_name and death._text(cells.get("Stat")) == stat]
        wanted = (float(base), float(per_level))
        if len(current) == 1 and (float(death._number(current[0].get("Base")) or 0),
                                  float(death._number(current[0].get("Per Level")) or 0)) == wanted:
            continue
        if current:
            raise ReapplyError(f"{class_name} {stat} already holds "
                               f"{[(c.get('Base'), c.get('Per Level')) for c in current]}, not {wanted}; "
                               f"somebody has changed it, so a person has to look")
        added.append({"Class": class_name, "Stat": stat, "Base": death._out(float(base)),
                      "Per Level": death._out(float(per_level))})
        report.append(f"{class_name} {stat}: base {base:g}, {per_level:g} a level, added")
    return death.SheetPlan(header, old, old + added, report, len(added))


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Add the War class lines to the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    death.refuse_what_a_save_would_lose(book)
    classes = plan_class_stats(book[CLASS_SHEET])
    if not classes.changed:
        return ["every War class line is already as this script writes it; nothing written"]
    if check_only:
        return classes.report + [f"{classes.changed} change(s) to make; nothing written (--check)"]
    others = {s.title: death._snapshot(s) for s in book.worksheets if s.title != CLASS_SHEET}
    death._write(book[CLASS_SHEET], classes)
    book.save(path)
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if death._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and only {CLASS_SHEET!r} was written")
    death._read_back(after, classes, CLASS_SHEET)
    return classes.report + [f"wrote {classes.changed} row(s); {CLASS_SHEET!r} now holds {len(classes.new)} rows "
                             f"(was {len(classes.old)}), and no other sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true", help="write the rows without regenerating the CSVs")
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
    return subprocess.run([sys.executable, str(death.GENERATOR), "--workbook", str(args.workbook)],
                          cwd=death.REPO_ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
