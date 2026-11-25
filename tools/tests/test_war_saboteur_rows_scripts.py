"""The script for the Saboteur's rows that need no new code, and what it wrote.

2026-11-25. `tools/reapply_war_saboteur_rows_workbook.py` writes the survey's groups A and B: fifteen nodes on stats,
scales, conditions and an action the game already reads. These tests hold the rulings the rows carry, that the
workbook and the generated table hold them, that the script is idempotent, and that it refuses a node somebody wrote.
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

import reapply_war_saboteur_rows_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
#: 26 until the C1 plan's slice of 2026-11-25 took over Hair Trigger's, Hair Wire's and Minefield's rows
#: (`tools/reapply_war_saboteur_trap_timing_workbook.py`), and 23 until the C5 plan's took over Calculated's
#: (`tools/reapply_war_saboteur_thin_air_workbook.py`).
ROW_COUNT = 22


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def test_the_script_writes_its_rows_on_twelve_nodes():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    assert len({node for node, _, _ in rows.NODES}) == 12


def test_durable_modifications_is_exactly_doubled_by_rows_that_never_hold_together():
    """The ruling on question 4: 6% a point above 50% from 4 points, 3% a point otherwise. At or below 50% one row
    holds at every point; above 50% the 3% row stops at 4 points (Max Points, exclusive) where the 6% row starts."""
    shapes = {(row.value, row.cond, row.cond_value, row.min_points, row.per_point_from, row.max_points)
              for row in _rows_of("Saboteur_basic_trunk_014")}
    assert shapes == {(3, "class_resource_at_or_below", 50, None, None, None),
                      (3, "class_resource_above", 50, None, None, 4),
                      (6, "class_resource_above", 50, 4, 1, None)}


def test_every_row_meaning_gadgets_names_gadgets_alone():
    """The coordinating session's ruling on question 1: a row meaning gadgets is on `Type.Gadget`, never on
    `Type.Deployable`, which traps carry too."""
    tags = {row.tags for _, _, wanted in rows.NODES for row in wanted}
    assert "Type.Deployable" not in tags
    assert {(row.stat, row.tags) for row in _rows_of("Saboteur_basic_trunk_006")} == {
        ("minion_reserve_percent_less", "Type.Trap"), ("minion_reserve_percent_less", "Type.Gadget")}


def test_tinkerers_duration_is_paid_for_each_point_beyond_six():
    (duration,) = [row for row in _rows_of("Saboteur_basic_trunk_008") if row.stat == "minion_duration"]
    assert (duration.value, duration.tags, duration.min_points, duration.per_point_from) == (20, "Type.Gadget", 7, 7)


def test_pressure_sensitive_is_the_trap_slow_and_a_bonus_against_the_crippled():
    """The rule of a tenth governs the slow (question 5) because it is the trap enchantment's own action."""
    slow, bonus = _rows_of("Saboteur_basic_trap_mid_001")
    assert (slow.action, slow.action_event, slow.value, slow.stack_seconds, slow.tags) == (
        "slow_for_seconds", "trap_triggered", 20, 2, "Type.Trap")
    assert (bonus.stat, bonus.cond, bonus.min_points, bonus.tags) == (
        "minion_damage", "target_carries_cripple", 4, "Type.Trap")


def test_ghost_steps_second_row_is_per_point_from_five_above_half_the_pool():
    _, extra = _rows_of("Saboteur_basic_trunk_011")
    assert (extra.value, extra.cond, extra.cond_value, extra.min_points, extra.per_point_from) == (
        2, "class_resource_above", 50, 5, 1)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], number(row["ValuePerPoint"]), row["RequiredTags"], row["Condition"],
                    row["Scale"], number(row["MinPoints"]), number(row["PerPointFrom"]), row["Action"])
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", float(row.value), row.tags or "", row.cond or "", row.scale or "",
                   float(row.min_points or 0), float(row.per_point_from or 0), row.action or "")
            assert written.count(key) == 1, key


def _workbook_without_the_rows(tmp_path) -> pathlib.Path:
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
    book.save(copy)
    return copy


def test_the_row_script_appends_the_rows_once(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_row_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_basic_trap_stem_005", "Stat": "class_resource", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the Saboteur's rows writes"):
        rows.apply(copy)
