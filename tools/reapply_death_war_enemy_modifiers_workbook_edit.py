"""Re-apply the Death and War enemy modifiers' workbook edit to a fresh copy, then regenerate.

WHY THIS EXISTS. `docs/All_Things_Cataclysm.xlsx` is a binary git cannot merge,
and any branch that edits it while another does must re-apply its cells to
whichever copy lands first. This is that re-application for
`feat/death-war-enemy-modifiers` (2026-11-25), recorded as data rather than as
a binary, after the pattern of `tools/reapply_aura_damage_workbook_edit.py`.

THE EDIT, KEYED ON CONTENT AND NEVER ON A ROW NUMBER:

    Debuffs, a new row "Mortal Reminder: ..."      Death's Mortal Reminder status
    Debuffs, a new row "Lingering Decay: ..."      Death's Lingering Decay status
        Both recommended in the Death modifiers' decisions entry (Q6) and
        accepted by the owner on 2026-11-24: "These are fine". Text only: the
        figures are the C++ constants', which the sentences state.
    Debuffs, the row "Battle Cry: ..."             War's Battle Cry
        Its sentence (no longer "(planned)"), 5 seconds in column B, 25 in
        column D and the eight resistances in column I, which the shout's
        named effect moves.
    Buffs, a new row "Drums of War: ..."           War's Drums of War buff
        50% increased movement and attack speed within 4 metres, half
        Commander's reach: the plan's Q12, accepted by the owner 2026-11-24.
    Enemy Modifiers, the War column's "Shield Wall: ..." cell
        "Take 75% less damage from projectiles.", the plan's Q10 wording the
        owner accepted on 2026-11-24, in place of "from attacks from the
        front" (no positional weak points, ruled 2026-08-09).

IT REFUSES TO WRITE unless every cell it touches holds either the value it had
before this edit or the value the edit gives it, and a new row's name is either
absent or present with exactly the new text. It verifies afterwards that those
are the only cells in the workbook that changed.

    python tools/reapply_death_war_enemy_modifiers_workbook_edit.py          # apply, then regenerate
    python tools/reapply_death_war_enemy_modifiers_workbook_edit.py --check  # report, write nothing

Run against the workbook already carrying the edit, it reports every cell
already set and writes nothing, which is its positive control. Regenerating
runs `tools/generate_datatables.py` and `tools/generate_gameplay_tags.py`; the
DataTable assets (`DT_StatusEffects`, `DT_EnemyModifiers`) still need the
editor step, `python tools/run_editor_python.py tools/generate_datatable_assets.py`.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATORS = (REPO_ROOT / "tools" / "generate_datatables.py",
              REPO_ROOT / "tools" / "generate_gameplay_tags.py")

#: The eight resistances Battle Cry moves, as `sim/cataclysm_sim/character.py`
#: spells them. Column I of the Debuffs sheet.
BATTLE_CRY_STATS = ("resistance_war, resistance_demonic, resistance_death, "
                    "resistance_pestilence, resistance_famine, "
                    "resistance_celestial, resistance_chaos, resistance_void")

BATTLE_CRY_BEFORE = ("Battle Cry: (planned) A terrifying shout that reduces the "
                     "player's resistances or armor for 5 seconds.")
BATTLE_CRY_AFTER = ("Battle Cry: A terrifying shout from a creature carrying "
                    "Battle Cry that reduces each of the player's resistances "
                    "by 25 for 5 seconds, never below zero.")

MORTAL_REMINDER = ("Mortal Reminder: A debuff a creature carrying Mortal "
                   "Reminder lays with each attack that takes the player's "
                   "health. For 4 seconds the player cannot be healed above the "
                   "health that attack left.")
LINGERING_DECAY = ("Lingering Decay: The player is standing in a necrotic zone "
                   "a creature carrying Lingering Decay left behind, and every "
                   "heal of their health is reduced by 50% while they stay "
                   "inside it.")
DRUMS_OF_WAR = ("Drums of War: All allies within 4 meters gain 50% increased "
                "movement speed and attack speed.")

SHIELD_WALL_BEFORE = "Shield Wall: Take 75% less damage from attacks from the front."
SHIELD_WALL_AFTER = "Shield Wall: Take 75% less damage from projectiles."

#: (sheet, the name the cell opens with, column letter, value before, value after)
#: for cells that already exist. Column A of the Debuffs sheet is keyed by the
#: name before the colon; the Enemy Modifiers cell by its own name in column C.
CELL_EDITS = (
    ("Debuffs", "Battle Cry", "A", BATTLE_CRY_BEFORE, BATTLE_CRY_AFTER),
    ("Debuffs", "Battle Cry", "B", None, 5),
    ("Debuffs", "Battle Cry", "D", None, 25),
    ("Debuffs", "Battle Cry", "I", None, BATTLE_CRY_STATS),
    ("Enemy Modifiers", "Shield Wall", "C", SHIELD_WALL_BEFORE, SHIELD_WALL_AFTER),
)

#: (sheet, the column the row's text is in, its whole text) for rows this edit
#: adds. Each goes on the first empty row after the sheet's last.
NEW_ROWS = (
    ("Debuffs", "A", MORTAL_REMINDER),
    ("Debuffs", "A", LINGERING_DECAY),
    ("Buffs", "A", DRUMS_OF_WAR),
)


def snapshot(book) -> dict:
    return {(ws.title, cell.coordinate): cell.value
            for ws in book.worksheets for row in ws.iter_rows() for cell in row
            if cell.value is not None}


def name_of(text) -> str:
    return str(text).split(":", 1)[0].strip() if text is not None and ":" in str(text) else ""


def rows_named(ws, column: str, name: str) -> list[int]:
    return [cell.row for cell in ws[column] if name_of(cell.value) == name]


def last_row(ws) -> int:
    filled = [cell.row for row in ws.iter_rows() for cell in row
              if cell.value is not None and str(cell.value).strip()]
    return max(filled) if filled else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing")
    parser.add_argument("--no-generate", action="store_true",
                        help="edit the workbook and do not regenerate")
    args = parser.parse_args()

    book = openpyxl.load_workbook(args.workbook)
    before = snapshot(book)
    to_write: list[tuple[str, str, object]] = []

    for sheet, name, column, old, new in CELL_EDITS:
        ws = book[sheet]
        key_column = "A" if sheet != "Enemy Modifiers" else column
        found = rows_named(ws, key_column, name)
        if len(found) != 1:
            sys.exit(f"{sheet}: expected one row named {name!r} in column "
                     f"{key_column}, found {len(found)}; refusing")
        cell = ws[f"{column}{found[0]}"]
        where = f"{sheet}!{cell.coordinate} ({name})"
        if cell.value == new:
            print(f"already set   {where}")
        elif cell.value == old:
            print(f"to change     {where}: {old!r} -> {new!r}")
            to_write.append((sheet, cell.coordinate, new))
        else:
            sys.exit(f"{where} holds {cell.value!r}, neither the value before "
                     f"this edit ({old!r}) nor after it ({new!r}); refusing")

    appended: dict[str, int] = {}
    for sheet, column, text in NEW_ROWS:
        ws = book[sheet]
        name = name_of(text)
        found = rows_named(ws, column, name)
        if len(found) > 1:
            sys.exit(f"{sheet}: {len(found)} rows are named {name!r}; refusing")
        if found:
            cell = ws[f"{column}{found[0]}"]
            if cell.value != text:
                sys.exit(f"{sheet}!{cell.coordinate} is named {name!r} and holds "
                         f"{cell.value!r}, not this edit's text; refusing")
            print(f"already set   {sheet}!{cell.coordinate} ({name})")
            continue
        row = max(last_row(ws), appended.get(sheet, 0)) + 1
        appended[sheet] = row
        print(f"to add        {sheet}!{column}{row}: {text!r}")
        to_write.append((sheet, f"{column}{row}", text))

    total = len(CELL_EDITS) + len(NEW_ROWS)
    if args.check or not to_write:
        print(f"{len(to_write)} of {total} cells to write; nothing written")
    else:
        for sheet, coordinate, value in to_write:
            book[sheet][coordinate].value = value
        book.save(args.workbook)
        after = snapshot(openpyxl.load_workbook(args.workbook))
        changed = {k for k in set(before) | set(after)
                   if before.get(k) != after.get(k)}
        intended = {(sheet, coordinate) for sheet, coordinate, _ in to_write}
        if changed != intended:
            sys.exit(f"the save changed {sorted(changed - intended)} and missed "
                     f"{sorted(intended - changed)}; inspect the workbook")
        print(f"wrote {len(to_write)} cells, and no other cell changed")

    if args.check or args.no_generate:
        return 0
    for generator in GENERATORS:
        code = subprocess.call([sys.executable, str(generator),
                                "--workbook", str(args.workbook)], cwd=REPO_ROOT)
        if code != 0:
            return code
    return 0


if __name__ == "__main__":
    sys.exit(main())
