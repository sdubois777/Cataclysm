"""The script for Thin Air's and Calculated's rows, and what it wrote.

2026-11-25, the C5 plan, accepted as written by the coordinating session. `tools/reapply_war_saboteur_thin_air_workbook.py`
writes every row of two nodes, one of which it took over from `tools/reapply_war_saboteur_rows_workbook.py`; Calculated's
rewording is in `tools/apply_saboteur_rewordings_2026_11_25.py`. These tests hold the rows' shapes, the hand-over, that
the workbook, the generated table and the tree file hold what was written, that the script is idempotent, and that it
refuses a row somebody wrote on the node it extends.
"""
from __future__ import annotations

import csv
import json
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_saboteur_rewordings_2026_11_25 as reword  # noqa: E402
import reapply_war_saboteur_rows_workbook as earlier  # noqa: E402
import reapply_war_saboteur_thin_air_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
TREE = REPO_ROOT / "docs" / "Saboteur_Class_Tree_Final.json"
ROW_COUNT = 4


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def test_the_script_writes_four_rows_on_two_nodes_and_took_calculated_over():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    assert rows.EXTENDING == {"Saboteur_basic_trunk_003"}
    assert not {node for node, _, _ in rows.NODES} & {node for node, _, _ in earlier.NODES}


def test_thin_air_is_a_flag_and_two_seconds():
    shapes = {(row.stat, row.kind, row.value, row.min_points) for row in _rows_of("Saboteur_basic_trunk_013")}
    assert shapes == {("cannot_be_targeted_while_moving", "flat", 1, None),
                      ("cannot_be_targeted_broken_seconds", "flat", 2, None)}


def test_calculated_keeps_its_five_a_point_and_gains_ten_percent_from_six_points():
    shapes = {(row.stat, row.kind, row.value, row.min_points) for row in _rows_of("Saboteur_basic_trunk_003")}
    assert shapes == {("class_resource", "flat", 5, None), ("class_resource", "increased", 10, 6)}


def test_calculated_is_reworded_to_what_its_row_gives():
    (fix,) = [fix for fix in reword.REWORDS if fix.node == "Calculated"]
    assert fix.before.endswith("At 6 points: Your Fervour cannot be reduced by enemy effects.")
    assert fix.after.endswith("At 6 points: Your maximum Fervour is increased by 10%.")
    data = json.loads(TREE.read_text(encoding="utf-8"))
    assert reword.order.node_named(data, "Calculated", reword.TREE)["data"]["description"] == fix.after


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), number(row["MinPoints"]))
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat, row.kind, float(row.value), float(row.min_points or 0))
            assert written.count(key) == 1, key


def _workbook_without(tmp_path, keep) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    kind_column = header.index("Value Kind") + 1
    ours = {node for node, _, _ in rows.NODES}
    for index in range(sheet.max_row, 1, -1):
        node = sheet.cell(index, node_column).value
        if node in ours and not keep(node, sheet.cell(index, kind_column).value):
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def _held_before(node: str, kind: str) -> bool:
    """Calculated's flat row, the one the earlier script wrote."""
    return node == "Saboteur_basic_trunk_003" and kind == "flat"


def test_the_row_script_appends_the_rows_once(tmp_path):
    copy = _workbook_without(tmp_path, lambda node, kind: False)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_row_script_keeps_calculateds_earlier_row_and_appends_the_rest(tmp_path):
    copy = _workbook_without(tmp_path, _held_before)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT - 1, first
    assert any("Saboteur_basic_trunk_003: holds 1 of its 2" in line for line in first), first


def test_the_row_script_refuses_a_row_somebody_wrote_on_calculated(tmp_path):
    copy = _workbook_without(tmp_path, _held_before)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_basic_trunk_003", "Stat": "evasion", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the rows Thin Air and Calculated writes") as refusal:
        rows.apply(copy)
    assert "WAS SAVED" not in str(refusal.value), refusal.value
    assert copy.read_bytes() == before
