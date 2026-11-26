"""The script that writes the War riders' rows, and what it wrote.

THE WAR RIDERS, 2026-11-25. `tools/reapply_war_riders_workbook.py` writes twenty rows on ten War nodes with slice D's
machinery, and adds the `Condition 2` and `Condition Value 2` columns the first rows with a second condition need. These
tests hold that the workbook and the generated table hold the rows, that the script adds both columns together and is
idempotent, and that it refuses a node somebody wrote since.
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

import reapply_war_riders_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
SECOND = (rows.base.CONDITION_2, rows.base.CONDITION_VALUE_2)


def _all_rows():
    return [(node, row) for node, _, wanted in rows.NODES for row in wanted]


def test_there_are_twenty_rows_on_ten_nodes():
    assert len(_all_rows()) == 20
    assert len({node for node, _, _ in rows.NODES}) == len(rows.NODES) == 10


def test_each_label_is_the_nodes_own_name():
    with open(REPO_ROOT / "game" / "Data" / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        names = {row["Name"]: row["NodeName"] for row in csv.DictReader(handle)}
    assert [(node, label) for node, label, _ in rows.NODES if names.get(node) != label] == []


def test_the_workbook_holds_the_rows_and_both_second_condition_columns():
    sheet = openpyxl.load_workbook(WORKBOOK, read_only=True)[rows.base.SHEET]
    header = [cell.value for cell in next(sheet.iter_rows(min_row=1, max_row=1))]
    assert all(column in header for column in SECOND), header
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows_with_their_second_conditions():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Condition"], number(row["ConditionValue"]), number(row["MinPoints"]),
                    number(row["PerPointFrom"]), row["Condition2"], number(row["ConditionValue2"]))
                   for row in csv.DictReader(handle)]
    for node, row in _all_rows():
        wanted = (node, row.stat, row.kind, float(row.value), row.tags or "", row.cond or "",
                  float(row.cond_value or 0), float(row.min_points or 0), float(row.per_point_from or 0),
                  row.cond2 or "", float(row.cond_value2 or 0))
        assert written.count(wanted) == 1, wanted


def _workbook_without_the_rows_or_columns(tmp_path) -> pathlib.Path:
    """A copy of the workbook with these rows and the two second-condition columns taken out."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    ours = {node for node, _, _ in rows.NODES}
    for index in range(sheet.max_row, 1, -1):
        if sheet.cell(index, node_column).value in ours:
            sheet.delete_rows(index, 1)
    for column in sorted((header.index(name) + 1 for name in SECOND), reverse=True):
        sheet.delete_cols(column, 1)
    book.save(copy)
    return copy


def test_the_script_adds_both_columns_and_the_rows_once(tmp_path):
    copy = _workbook_without_the_rows_or_columns(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 20, first
    for column in SECOND:
        assert f"added the column {column!r} to {rows.base.SHEET!r}" in first, first
    header = [cell.value for cell in openpyxl.load_workbook(copy, read_only=True)[rows.base.SHEET][1]]
    assert header[-2:] == list(SECOND), header
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows_or_columns(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Berserker_twoh_007", "Stat": "crit_chance", "Value Kind": "flat",
                   "Value Per Point": 6}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the War riders writes"):
        rows.apply(copy)
