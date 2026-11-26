"""The script for Slippery's rows, and what it wrote.

Window death3-w1's second pass (2026-11-25), as the coordinating session ruled. `tools/reapply_saboteur_slippery_workbook.py`
writes both rows of Slippery (`Saboteur_basic_trunk_004`): the evasion row it took over from
`tools/reapply_war_saboteur_rows_workbook.py`, and "At 5 points: After successfully evading, gain 3 Fervour", which no
row carried. These tests hold the rows' shapes, the hand-over, that the workbook and the generated table hold what was
written, that the script is idempotent, and that it refuses a row somebody wrote on the node it extends. The Unreal half,
that a `points` row on `dodge` pays, is the Death trees' slice C6b3's (`Cataclysm.EvadeAndFull.`).
"""
from __future__ import annotations

import csv
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import reapply_saboteur_slippery_workbook as rows  # noqa: E402
import reapply_war_saboteur_rows_workbook as earlier  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
NODE = "Saboteur_basic_trunk_004"


def test_the_script_writes_two_rows_on_slippery_and_took_it_over():
    (wanted,) = [wanted for node, _, wanted in rows.NODES if node == NODE]
    assert len(rows.NODES) == 1 and len(wanted) == 2
    assert rows.EXTENDING == {NODE}
    assert NODE not in {node for node, _, _ in earlier.NODES}


def test_the_fervour_is_a_flat_three_on_an_evasion_from_five_points():
    (wanted,) = [wanted for node, _, wanted in rows.NODES if node == NODE]
    shapes = {(row.stat, row.kind, row.value, row.action, row.action_event, row.fraction_of, row.min_points,
               row.per_point_from) for row in wanted}
    assert shapes == {("evasion", "flat", 1, None, None, None, None, None),
                      (None, "", 3, "class_resource", "dodge", "points", 5, None)}


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_fervour_row():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        mine = [row for row in csv.DictReader(handle) if row["Node"] == NODE]
    assert len(mine) == 2, mine
    (fervour,) = [row for row in mine if row["Action"] == "class_resource"]
    assert (fervour["ActionEvent"], fervour["FractionOf"], float(fervour["ValuePerPoint"]), int(fervour["MinPoints"])) \
        == ("dodge", "points", 3.0, 5)


def _workbook_keeping_only_the_evasion_row(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    action_column = header.index("Action") + 1
    for index in range(sheet.max_row, 1, -1):
        if sheet.cell(index, node_column).value == NODE and sheet.cell(index, action_column).value:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_keeps_the_evasion_row_appends_the_fervour_row_once(tmp_path):
    copy = _workbook_keeping_only_the_evasion_row(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 1, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_slippery(tmp_path):
    copy = _workbook_keeping_only_the_evasion_row(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": NODE, "Stat": "armor", "Value Kind": "flat", "Value Per Point": 4}.get(name)
                  for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the rows Slippery writes"):
        rows.apply(copy)
    assert copy.read_bytes() == before
