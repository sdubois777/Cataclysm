"""Write the War weapon skills' shapes into the design workbook, and regenerate the CSVs. Issue #2972.

    python tools/reapply_war_skills_workbook.py              # write the cells, then run tools/generate_datatables.py
    python tools/reapply_war_skills_workbook.py --check      # say what would change, write nothing
    python tools/reapply_war_skills_workbook.py --workbook COPY.xlsx --no-generate    # a copy, nothing regenerated

WHY THIS EXISTS, as `tools/reapply_death_skills_workbook.py` (issue #2885) does: `docs/All_Things_Cataclysm.xlsx` is a
binary git cannot merge, so the branch records its edit as this script, run on whichever workbook development holds
when the branch lands. It reuses that script's helpers.

WHAT IT WRITES. The 61 War rows already hold their names, descriptions, tags and numbers; 53 have no Shape. Each row
here is found by its Weapon Type, Damage Type (War) and Slot, never by position, and gets:
- its Shape and Shape Params, ONLY when every code piece it needs is in `BUILT` (the plan's keys W1 to W11). A row still
  waiting keeps an empty Shape, so the game grants the "not designed yet" placeholder;
- the sentence the coordinating session's rulings of 2026-11-28 reword, as one fragment replaced by another;
- a Damage Percent cell changed by a ruling (Annihilator's 300 is its final hit, `FinalHitPercent=300`).

WHAT IT REFUSES: a workbook holding formulas, charts or images; a War row missing or found twice; and a cell holding
neither what it held before nor what this writes, which means somebody else changed it. It reads the file back and
refuses if any other cell changed. Run on a workbook already carrying the edit, it writes nothing (its positive control).
Afterwards the editor step regenerates `DT_WeaponSkills`.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from typing import NamedTuple

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import reapply_death_skills_workbook as death  # noqa: E402

REPO_ROOT = death.REPO_ROOT
WORKBOOK = death.WORKBOOK
GENERATOR = death.GENERATOR
SKILLS_SHEET = death.SKILLS_SHEET
DAMAGE_TYPE = "War"
ApplyError = death.ApplyError

#: The code pieces this branch has built, by the plan's numbering (`.claude/windows/war_skills_plan.md`, accepted by the
#: coordinating session on 2026-11-28). Slice WS1: W1 `AilmentChance`, W2 `AilmentMagnitude`, W3 `ArmourRemoved`, W4
#: `DamageTakenIncreased`, W5 `Stagger`.
BUILT = frozenset({"W1", "W2", "W3", "W4", "W5"})
KNOWN_PIECES = frozenset({f"W{n}" for n in range(1, 12) if n != 7})

WEAPON, DAMAGE, SLOT = "Weapon Type", "Damage Type", "Slot"
DESCRIPTION, SHAPE, PARAMS, DAMAGE_PERCENT = "Skill Description", "Shape", "Shape Params", "Damage Percent"


class WarRow(NamedTuple):
    weapon: str
    slot: str
    shape: str
    params: str
    needs: tuple[str, ...] = ()
    #: A fragment of the sentence a ruling rewords, and what replaces it.
    reword: tuple[str, str] | None = None
    #: A Damage Percent cell a ruling changes: (before, after); None for an empty cell.
    damage: tuple[object, object] | None = None

    @property
    def works(self) -> bool:
        return all(piece in BUILT for piece in self.needs)


ROWS: tuple[WarRow, ...] = (
    # ---- DATA: an existing Shape with parameters is enough (ranges past 14 m cut to 14, ruled 2026-11-28).
    WarRow("2H Crossbow", "Heavy", "Projectile", "Range=14; Radius=0.5; Pierce=2; Speed=3000; Ailment=Bleed",
           reword=(", causing them to bleed for 10 seconds.", ", causing them to bleed.")),
    WarRow("2H Crossbow", "Movement", "Movement", "Mode=Blink; Range=6; Radius=2.5; Knockback=2"),
    WarRow("2H Crossbow", "Ultimate", "Projectile", "Range=14; Radius=6; Speed=1400; Arc=0.25; Ailment=Bleed",
           reword=("up to 20 meters away", "up to 14 meters away")),
    WarRow("Axe", "Movement", "Movement",
           "Mode=Charge; Range=10; Radius=2; Duration=1; Knockback=2; MoreDamagePer=5; ScalingSource=Meter; "
           "Ailment=Bleed"),
    WarRow("Crossbow", "Support", "Projectile",
           "Range=12; Radius=5; Speed=0; GroundRadius=5; GroundDuration=4; GroundPercent=25; Effect=Cripple; "
           "EffectMagnitude=40",
           reword=("have their movement speed reduced by 40%.", "move and attack 40% slower.")),
    WarRow("Fist", "Movement", "Movement",
           "Mode=Charge; Range=10; Radius=2; Duration=1.1; Knockback=2; Immune=CrowdControl"),
    WarRow("Greataxe", "Special", "Projectile", "Range=12; Radius=1.5; Pierce=99; Returns=1; Speed=1800; Ailment=Bleed"),
    WarRow("Greatsword", "Movement", "Movement",
           "Mode=Leap; Range=10; Radius=4; Knockback=3; Effect=Cripple; EffectMagnitude=30; EffectDuration=2",
           reword=("leaves a shockwave that briefly slows enemies caught in it.",
                   "leaves a shockwave that makes enemies caught in it move and attack 30% slower for 2 seconds.")),
    WarRow("Shield", "Movement", "Movement",
           "Mode=Charge; Range=10; Radius=2; Duration=1; Knockback=1; MoreDamagePer=4; ScalingSource=Meter; "
           "Immune=Displacement, Stun"),
    WarRow("Sword", "Special", "Projectile",
           "Range=14; Radius=0.5; Speed=2400; Ailment=Bleed; Effect=Cripple; EffectMagnitude=30; EffectDuration=4",
           reword=("reducing their movement speed by 30% for 4 seconds.",
                   "making them move and attack 30% slower for 4 seconds.")),
    # ---- WS1's keys W1 to W5.
    WarRow("Axe", "Heavy", "Strike",
           "Radius=2.5; Angle=60; MaxTargets=1; Ailment=Bleed; AilmentChance=30; ArmourRemoved=20; "
           "ArmourRemovedDuration=8", needs=("W1", "W3")),
    WarRow("Axe", "Special", "Projectile",
           "Range=10; Radius=1; Speed=2000; Bounces=2; Ailment=Bleed; AilmentChance=40", needs=("W1",)),
    WarRow("Fist", "Special", "Projectile",
           "Range=12; Radius=1; Speed=2000; ForcedMovement=Pin; ForcedMovementDuration=3; Ailment=Bleed; "
           "AilmentChance=40", needs=("W1",)),
    WarRow("Greataxe", "Movement", "Movement", "Mode=Leap; Range=10; Radius=5; Ailment=Bleed; Stagger=1",
           needs=("W5",)),
    # ANNIHILATOR: the ticks take the slot's figure and the 300 is the final hit (ruled 2026-11-28), as Pyroclasm.
    WarRow("Greataxe", "Ultimate", "Strike",
           "Radius=3.5; Angle=360; Duration=3; Interval=0.5; Ailment=Bleed; FinalHitPercent=300; ArmourRemoved=5; "
           "ArmourRemovedDuration=6; ArmourRemovedCap=0", needs=("W3",), damage=(300.0, None)),
    WarRow("Greatsword", "Ultimate", "Strike",
           "ChargeTime=1; Radius=8; Angle=360; ForcedMovement=Knockdown; ForcedMovementDuration=2; "
           "DamageTakenIncreased=40; DamageTakenIncreasedDuration=8", needs=("W4",)),
    WarRow("Sword", "Heavy", "Strike",
           "Radius=3; Angle=30; Ailment=Bleed; ArmourRemoved=15; ArmourRemovedDuration=8", needs=("W3",),
           reword=("that causes the target to bleed for 8 seconds. The wound reduces the target's armor by 15% for "
                   "the duration.",
                   "that causes the target to bleed. The wound reduces the target's armor by 15% for 8 seconds.")),
    # EARTHQUAKE (accepted 2026-11-28): the fissure is the Ground rider, 2 m for 5 s, a judgement.
    WarRow("Warhammer", "Heavy", "Strike",
           "Radius=6; Angle=360; GroundRadius=2; GroundDuration=5; GroundPercent=20; Stagger=0.5; ArmourRemoved=20; "
           "ArmourRemovedDuration=6", needs=("W3", "W5")),
    WarRow("Warhammer", "Special", "Projectile",
           "Range=12; Radius=4; Speed=1400; Arc=0.25; ArmourRemoved=25; ArmourRemovedDuration=8", needs=("W3",)),
    # ENTANGLE: one Cripple figure, 50 (ruled 2026-11-28).
    WarRow("Whip", "Support", "Debuff",
           "Range=10; MaxTargets=1; Effect=Cripple; EffectMagnitude=50; EffectDuration=5; DamageTakenIncreased=20; "
           "DamageTakenIncreasedDuration=5", needs=("W4",),
           reword=("reducing their movement speed by 60% and attack speed by 40% for 5 seconds.",
                   "making them move and attack 50% slower for 5 seconds.")),
)


class Write(NamedTuple):
    sheet: str
    row: int
    column: int
    before: object
    after: object
    what: str

    def describe(self) -> str:
        return f"{self.what}: {self.before!r} -> {self.after!r}"


def check_rows() -> None:
    seen = set()
    for row in ROWS:
        key = (row.weapon, row.slot)
        if key in seen:
            raise ApplyError(f"War {row.weapon} {row.slot} is listed twice")
        seen.add(key)
        unknown = set(row.needs) - KNOWN_PIECES
        if unknown:
            raise ApplyError(f"War {row.weapon} {row.slot} needs unknown pieces {sorted(unknown)}")


def plan(book) -> list[Write]:
    check_rows()
    death.refuse_what_a_save_would_lose(book)
    sheet = book[SKILLS_SHEET]
    columns = death.header_of(sheet, (WEAPON, DAMAGE, SLOT, DESCRIPTION, SHAPE, PARAMS, DAMAGE_PERCENT))
    found: dict[tuple[str, str], list[int]] = {}
    for cells in sheet.iter_rows(min_row=2):
        if cells[columns[DAMAGE] - 1].value == DAMAGE_TYPE:
            found.setdefault((cells[columns[WEAPON] - 1].value, cells[columns[SLOT] - 1].value), []).append(
                cells[0].row)
    writes: list[Write] = []
    for row in ROWS:
        rows = found.get((row.weapon, row.slot), [])
        if len(rows) != 1:
            raise ApplyError(f"War {row.weapon} {row.slot} is in the sheet {len(rows)} time(s), not once")
        at = rows[0]
        name = f"War {row.weapon} {row.slot}"

        def cell(column: str) -> object:
            return sheet.cell(at, columns[column]).value

        wanted_shape, wanted_params = (row.shape, row.params) if row.works else (None, None)
        for column, wanted in ((SHAPE, wanted_shape), (PARAMS, wanted_params)):
            held = cell(column)
            if death.same(held, wanted):
                continue
            # A ROW STILL WAITING MAY HOLD NOTHING, AND A WORKING ROW MAY HOLD NOTHING OR ITS OWN VALUE: anything else is
            # somebody else's edit.
            if not death.same(held, None):
                raise ApplyError(f"{name} {column} holds {held!r}, neither empty nor {wanted!r}; refusing")
            writes.append(Write(SKILLS_SHEET, at, columns[column], held, wanted, f"{name} {column}"))
        if row.reword:
            before_fragment, after_fragment = row.reword
            held = cell(DESCRIPTION) or ""
            if after_fragment in held and before_fragment not in held:
                pass
            elif held.count(before_fragment) == 1:
                writes.append(Write(SKILLS_SHEET, at, columns[DESCRIPTION], held,
                                    held.replace(before_fragment, after_fragment), f"{name} {DESCRIPTION}"))
            else:
                raise ApplyError(f"{name} {DESCRIPTION} holds neither the ruled sentence nor the one it rewords: "
                                 f"{held!r}")
        if row.damage:
            before_value, after_value = row.damage
            held = cell(DAMAGE_PERCENT)
            if not death.same(held, after_value):
                if not death.same(held, before_value):
                    raise ApplyError(f"{name} {DAMAGE_PERCENT} holds {held!r}, neither {before_value!r} nor "
                                     f"{after_value!r}; refusing")
                writes.append(Write(SKILLS_SHEET, at, columns[DAMAGE_PERCENT], held, after_value,
                                    f"{name} {DAMAGE_PERCENT}"))
    return writes


def apply(path: pathlib.Path = WORKBOOK) -> list[Write]:
    """Write the cells, read the file back, and answer what was written."""
    book = openpyxl.load_workbook(path)
    writes = plan(book)
    if not writes:
        return []
    before = death.snapshot(book)
    sheets_before = list(book.sheetnames)
    for write in writes:
        book[write.sheet].cell(write.row, write.column).value = write.after
    book.save(path)
    after_book = openpyxl.load_workbook(path)
    if after_book.sheetnames != sheets_before:
        raise ApplyError(f"the sheets changed: {sheets_before} became {after_book.sheetnames}")
    after = death.snapshot(after_book)
    changed = {key for key in set(before) | set(after) if before.get(key) != after.get(key)}
    intended = {(write.sheet, write.row, write.column) for write in writes}
    if changed != intended:
        raise ApplyError(f"the save changed {sorted(changed - intended)} and missed {sorted(intended - changed)}; "
                         "inspect the workbook")
    if plan(after_book):
        raise ApplyError("read back, the workbook still has cells to write; inspect it")
    return writes


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true", help="report what would change and write nothing")
    parser.add_argument("--no-generate", action="store_true", help="edit the workbook and do not regenerate the CSVs")
    args = parser.parse_args(argv)
    try:
        if args.check:
            writes = plan(openpyxl.load_workbook(args.workbook))
            for write in writes:
                print(f"to write   {write.describe()}")
            print(f"{len(writes)} cells to write; nothing written")
            return 0
        writes = apply(args.workbook)
    except ApplyError as error:
        print(f"refused: {error}", file=sys.stderr)
        return 1
    for write in writes:
        print(f"wrote      {write.describe()}")
    working = sum(1 for row in ROWS if row.works)
    print(f"wrote {len(writes)} cells, and no other cell changed. {working} of the {len(ROWS)} War rows listed have "
          f"their shape.")
    if args.no_generate:
        return 0
    return subprocess.call([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], cwd=REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main())
