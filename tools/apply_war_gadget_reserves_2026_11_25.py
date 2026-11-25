"""Take the count caps off the three War machine rows of the design workbook's Weapon Skills sheet.

    python tools/apply_war_gadget_reserves_2026_11_25.py
    python tools/apply_war_gadget_reserves_2026_11_25.py --check

ISSUE #1934, approved by the owner on 2026-11-25 and ruled by the coordinating session that day (Q2): the War machines
reserve their rows' Fervour, and lose their count caps as every kind that reserves did under issue #2787 ("that's
limiting on its own"). Each of Bolt Turret, Ballista and Iron Fortress loses `MaxActive` from its Shape Params and the
sentence stating its cap from its Skill Description. The old text is beside the new below and in the decisions entry.

IDEMPOTENT AND REFUSING: a cell already holding the new text is left alone; a cell holding the old text is written; a
cell holding anything else is refused and nothing is saved, so text somebody wrote since is never passed over.
`--check` writes nothing and exits 1 when a cell would be written.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
from dataclasses import dataclass

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
SHEET = "Weapon Skills"


@dataclass(frozen=True)
class Edit:
    skill: str
    column: str
    before: str
    after: str


def _without(text: str, part: str) -> str:
    assert text.count(part) == 1, (text, part)
    return text.replace(part, "")


_TURRET_TEXT = ("Fire a specially designed bolt into the ground that deploys into a stationary bolt turret. The turret "
                "automatically fires at the nearest enemy within 10 meters every 1.5 seconds, dealing physical damage "
                "and applying bleed on each hit. The turret has 200 HP, lasts 5 seconds, and reserves 5 Fervour while "
                "it stands. One turret may stand at a time.")
_BALLISTA_TEXT = ("Hammer a ballista into the ground at your location. The ballista automatically fires massive bolts at "
                  "the furthest enemy within 15 meters every 2 seconds, dealing enormous physical damage and pinning "
                  "struck enemies in place for 1 second. The ballista has 500 HP, lasts 8 seconds, and reserves 5 "
                  "Fervour while it stands. Up to 2 ballistas may stand at once.")
_FORTRESS_TEXT = ("Slam your spear into the ground to instantly deploy 2 ballista turrets and 3 ground spike traps "
                  "arranged around your position. All deployed gadgets and traps from this ability are fully armed "
                  "immediately and have 50% increased HP. Each of the five reserves 5 Fervour, so the set reserves 25. "
                  "Lasts 20 seconds. One set may stand at a time.")
_TURRET_PARAMS = "Count=1; Duration=5; Minions=BoltTurret:1; FervourReserve=5; Effect=Bleed; MaxActive=1"
_BALLISTA_PARAMS = "Count=1; Duration=8; Minions=Ballista:1; FervourReserve=5; MaxActive=2"
_FORTRESS_PARAMS = "Duration=20; Minions=Ballista:2, SpikeTrap:3; FervourReserve=5; HealthPercent=150; MaxActive=5"

EDITS: tuple[Edit, ...] = (
    Edit("Bolt Turret", "Skill Description", _TURRET_TEXT, _without(_TURRET_TEXT, " One turret may stand at a time.")),
    Edit("Bolt Turret", "Shape Params", _TURRET_PARAMS, _without(_TURRET_PARAMS, "; MaxActive=1")),
    Edit("Ballista", "Skill Description", _BALLISTA_TEXT,
         _without(_BALLISTA_TEXT, " Up to 2 ballistas may stand at once.")),
    Edit("Ballista", "Shape Params", _BALLISTA_PARAMS, _without(_BALLISTA_PARAMS, "; MaxActive=2")),
    Edit("Iron Fortress", "Skill Description", _FORTRESS_TEXT,
         _without(_FORTRESS_TEXT, " One set may stand at a time.")),
    Edit("Iron Fortress", "Shape Params", _FORTRESS_PARAMS, _without(_FORTRESS_PARAMS, "; MaxActive=5")),
)


class RewordError(Exception):
    """A cell holds neither the old text nor the new."""


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Write the edits the workbook at `path` lacks; return the report. Refuses before saving anything."""
    book = openpyxl.load_workbook(path)
    sheet = book[SHEET]
    header = [cell.value for cell in sheet[1]]
    name_column = header.index("Skill Name") + 1
    shape_column = header.index("Shape") + 1
    report: list[str] = []
    writes: list[tuple[object, str]] = []
    for edit in EDITS:
        rows = [index for index in range(2, sheet.max_row + 1)
                if sheet.cell(index, name_column).value == edit.skill
                and sheet.cell(index, shape_column).value == "Deployable"]
        if len(rows) != 1:
            raise RewordError(f"{edit.skill}: {len(rows)} Deployable rows of that name, not 1. Nothing was written")
        cell = sheet.cell(rows[0], header.index(edit.column) + 1)
        if cell.value == edit.after:
            report.append(f"{edit.skill} {edit.column}: already written; left alone")
        elif cell.value == edit.before:
            writes.append((cell, edit.after))
            report.append(f"{edit.skill} {edit.column}: written")
        else:
            raise RewordError(f"{edit.skill} {edit.column} holds {cell.value!r}, which is neither the text this "
                              f"script replaces nor what it writes; somebody has written it since. Nothing was written")
    if not writes:
        report.append("every cell is already written; nothing written")
        return report
    if check_only:
        report.append(f"{len(writes)} cell(s) to write; nothing written (--check)")
        return report
    for cell, value in writes:
        cell.value = value
    book.save(path)
    report.append(f"{len(writes)} cell(s) written")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if a cell would be written")
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except RewordError as error:
        print(error)
        return 2
    print("\n".join(report))
    return 1 if args.check and report[-1].endswith("(--check)") else 0


if __name__ == "__main__":
    sys.exit(main())
