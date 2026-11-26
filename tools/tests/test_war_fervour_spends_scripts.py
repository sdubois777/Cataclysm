"""The script that writes the Fervour spends' rows, and what it wrote.

THE FERVOUR SPENDS, 2026-11-25. `tools/reapply_war_fervour_spends_workbook.py` writes sixteen rows on Warmonger, Cut
and Run, Iron Will, Undying, Cleave and Blood Tithe with slice D's machinery. These tests hold that the workbook and the
generated table hold the rows (with Warmonger's and Cut and Run's 4 metre reach, the coordinating session's ruling for
"melee range"), that the design document states the ruling, that the script is idempotent, and that it refuses a node
somebody wrote since.
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

import reapply_war_fervour_spends_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def _all_rows():
    return [(node, row) for node, _, wanted in rows.NODES for row in wanted]


def test_there_are_sixteen_rows_on_six_nodes():
    assert len(_all_rows()) == 16
    assert len({node for node, _, _ in rows.NODES}) == len(rows.NODES) == 6


def test_each_label_is_the_nodes_own_name():
    with open(REPO_ROOT / "game" / "Data" / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        names = {row["Name"]: row["NodeName"] for row in csv.DictReader(handle)}
    assert [(node, label) for node, label, _ in rows.NODES if names.get(node) != label] == []


def test_melee_range_is_four_metres_and_the_design_document_says_so():
    """The ruling is one figure in the script and one sentence in the design document; both have to agree."""
    assert rows.MELEE_RANGE_METRES == 4
    design = (REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    assert '**"Melee range" in a node is 4 metres.**' in design
    reaches = {row.reach for _, row in _all_rows() if row.scale == "enemies_in_reach"}
    assert reaches == {rows.MELEE_RANGE_METRES}, reaches


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Condition"], number(row["ConditionValue"]), row["Scale"], number(row["Option"]),
                    number(row["MinPoints"]), number(row["ReachMetres"]) if row["Scale"] else 0.0)
                   for row in csv.DictReader(handle)]
    for node, row in _all_rows():
        wanted = (node, row.stat, row.kind, float(row.value), row.tags or "", row.cond or "",
                  float(row.cond_value or 0), row.scale or "", float(row.option or 0), float(row.min_points or 0),
                  float(row.reach or 0))
        assert written.count(wanted) == 1, wanted


def _workbook_without_the_rows(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    option_column = header.index("Option") + 1
    ours = {(node, option) for node, _, wanted in rows.NODES for option in {int(r.option or 0) for r in wanted}}
    for index in range(sheet.max_row, 1, -1):
        if (sheet.cell(index, node_column).value, int(sheet.cell(index, option_column).value or 0)) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_appends_the_rows_once(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 16, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Berserker_leech_004", "Stat": "health_percent_per_ten_fervour_on_lethal_blow",
                   "Value Kind": "flat", "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the Fervour spends writes"):
        rows.apply(copy)
