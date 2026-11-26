"""`tools/reapply_abyssal_rifts_row.py` redoes the #2864 edit to the design workbook, and only that edit.

WHY THIS EXISTS. The workbook is a binary git cannot merge, so the branch that lands second redoes its workbook edit
on top of `development`'s file (the coordinating session's instruction of 2026-11-24, issue #2864). The script is that
redo; these checks hold it to the text the game and the row check read, and run it on a copy of the workbook put back
to the text `development` held, so the real file is never touched.
"""

from __future__ import annotations

import csv
import importlib.util
import pathlib
import shutil

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "reapply_abyssal_rifts_row.py"


def load():
    spec = importlib.util.spec_from_file_location("reapply_abyssal_rifts_row", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_new_text_is_the_rows_and_the_keys() -> None:
    script = load()
    with (REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv").open(encoding="utf-8", newline="") as handle:
        rows = {row["Name"]: row for row in csv.DictReader(handle)}
    assert rows["Demonic_Abyssal_Rifts"]["Description"] == script.NEW_TEXT
    header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon" / "CataclysmDungeonModifierEffects.h").read_text(
        encoding="utf-8")
    key = header[:header.index("static const TCHAR* AbyssalRiftsKey;")]
    key = " ".join(line.strip().lstrip("*").strip() for line in key[key.rindex("/**"):].splitlines())
    assert f'"{script.NEW_TEXT}"' in key, "AbyssalRiftsKey's comment no longer quotes the row's text"
    assert script.check(), "the committed workbook does not hold the text the script writes"


def test_it_writes_the_one_cell_on_a_copy_holding_the_old_text(tmp_path: pathlib.Path) -> None:
    script = load()
    copy = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(script.WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[script.SHEET]
    row = script.find_row(sheet)
    sheet.cell(row, 4).value = script.OLD_TEXT
    book.save(copy)
    assert not script.check(copy), "set-up: the copy holds the old text"

    said = script.apply(copy)
    assert said.startswith(f"row {row} written"), said
    assert script.check(copy)
    assert script.apply(copy).endswith("nothing written"), "a second run must change nothing"
    assert openpyxl.load_workbook(copy)[script.SHEET].cell(row, 4).value == script.NEW_TEXT
