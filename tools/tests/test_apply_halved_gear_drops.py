"""`tools/apply_halved_gear_drops.py` writes the halved Gear Drops column into the design workbook, and only that.

WHY THIS EXISTS. Issue #2853: the owner halved the gear drop column on 2026-11-24, and the branch that made the change
was told not to edit `docs/All_Things_Cataclysm.xlsx`, which other branches held. It carries the generated
`game/Data/EnemyDrops.csv` and this script, which makes the workbook agree when it is free. These checks hold the
script's figures to the CSV, the simulation and the design document, and run it on a copy of the workbook -- put back to
the figures before #2853 if the committed one already holds the new ones -- so the real file is never touched.
"""

from __future__ import annotations

import csv
import importlib.util
import pathlib
import shutil
import subprocess
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "apply_halved_gear_drops.py"

if str(REPO_ROOT / "sim") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import loot  # noqa: E402


def load():
    spec = importlib.util.spec_from_file_location("apply_halved_gear_drops", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def committed_csv() -> dict[str, dict[str, str]]:
    with (REPO_ROOT / "game" / "Data" / "EnemyDrops.csv").open(encoding="utf-8", newline="") as handle:
        return {row["EnemyRarity"]: row for row in csv.DictReader(handle)}


def put_back(script, path: pathlib.Path) -> None:
    """Write the figures before #2853, and the old note, into the workbook at `path`."""
    book = openpyxl.load_workbook(path)
    sheet = book[script.SHEET]
    at = script.columns(sheet)
    rows = script.rows_of(sheet, at[script.RARITY_HEADER])
    for rarity, (old, _new) in script.GEAR_DROPS.items():
        sheet.cell(rows[rarity], at[script.GEAR_HEADER]).value = old
    sheet.cell(rows[script.NOTE_ROW], at[script.NOTE_HEADER]).value = script.OLD_NOTE
    book.save(path)


def test_the_new_figures_are_half_the_old_and_are_the_csvs_and_the_simulations() -> None:
    script = load()
    table = committed_csv()
    for rarity, (old, new) in script.GEAR_DROPS.items():
        assert new == old / 2, rarity
        assert float(table[rarity]["GearDrops"]) == new, (
            f"game/Data/EnemyDrops.csv gives a {rarity} {table[rarity]['GearDrops']} gear, not the {new} the script "
            "writes into the workbook")
        assert loot.ENEMY_GEAR_DROPS[rarity] == new, rarity
    assert set(script.GEAR_DROPS) == set(loot.ENEMY_GEAR_DROPS)


def test_it_writes_the_column_on_a_copy_holding_the_old_figures(tmp_path: pathlib.Path) -> None:
    script = load()
    copy = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(script.WORKBOOK, copy)
    put_back(script, copy)
    assert not script.check(copy), "set-up: the copy holds the figures before #2853"

    said = script.apply(copy)
    assert said.startswith("7 cell(s) written"), said
    assert script.check(copy)
    assert script.apply(copy).endswith("nothing written"), "a second run must change nothing"


def test_the_csv_the_branch_carries_is_what_the_generator_makes_of_the_written_workbook(tmp_path: pathlib.Path) -> None:
    """The proof that `game/Data/EnemyDrops.csv` is the generator's own output and not a hand edit: write the column
    into a copy, generate every table from it into a scratch directory, and compare."""
    script = load()
    copy = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(script.WORKBOOK, copy)
    put_back(script, copy)
    script.apply(copy)
    out = tmp_path / "generated"
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "generate_datatables.py"),
                             "--workbook", str(copy), "--output-dir", str(out)],
                            cwd=REPO_ROOT, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    generated = (out / "EnemyDrops.csv").read_bytes()
    committed = (REPO_ROOT / "game" / "Data" / "EnemyDrops.csv").read_bytes()
    assert generated == committed, "game/Data/EnemyDrops.csv is not what the generator makes of the halved column"


def test_it_refuses_a_gear_cell_somebody_else_changed(tmp_path: pathlib.Path) -> None:
    script = load()
    copy = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(script.WORKBOOK, copy)
    put_back(script, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[script.SHEET]
    at = script.columns(sheet)
    rows = script.rows_of(sheet, at[script.RARITY_HEADER])
    sheet.cell(rows["Herald"], at[script.GEAR_HEADER]).value = 3.5
    book.save(copy)
    with pytest.raises(script.ApplyError, match="Herald's Gear Drops is 3.5, neither 2"):
        script.apply(copy)
    assert openpyxl.load_workbook(copy)[script.SHEET].cell(rows["Common"], at[script.GEAR_HEADER]).value == 0.16, (
        "a refused run wrote some cells before refusing")
