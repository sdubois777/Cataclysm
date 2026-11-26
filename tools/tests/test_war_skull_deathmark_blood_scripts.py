"""The script for Skull Splitter's, Deathmark's and Blood Price's rows, and what it wrote.

2026-11-25. `tools/reapply_war_skull_deathmark_blood_workbook.py` writes twelve rows on three Berserker nodes, each a
figure `FCataclysmWarFervour` reads where the node applies. These tests hold the shapes the sentences and the
coordinating session's rulings give the rows, that the workbook and the generated table hold them, that the script is
idempotent, and that it refuses a node somebody wrote.
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

import reapply_war_skull_deathmark_blood_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
ROW_COUNT = 12


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def _paid(row, points: int) -> float:
    """What `row` gives at `points`, as `UCataclysmPassiveTree::PointsPaid` pays it: from Min Points once (a threshold
    row with no Per Point From), below Max Points only, and every point otherwise."""
    if row.max_points is not None and points >= row.max_points:
        return 0.0
    if row.min_points is not None:
        return float(row.value) if points >= row.min_points else 0.0
    return float(row.value) * points


def _figure(node: str, stat: str, points: int) -> float:
    return sum(_paid(row, points) for row in _rows_of(node) if row.stat == stat)


def test_the_script_writes_twelve_rows_on_three_nodes():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    assert [node for node, _, _ in rows.NODES] == ["Berserker_twoh_003", "Berserker_crit_004", "Berserker_trunk_008"]


def test_skull_splitters_armour_and_cost_are_stated_under_wrath_and_a_two_handed_weapon():
    """"Two-handed weapons only ... While in Wrath, critical strikes drain an additional 3 Fervour": the percent and the
    cost under both conditions; the multiplier under the weapon alone; the cap and the seconds under neither, since
    they are read only once the percent is above nought."""
    conditions = {row.stat: (row.cond, row.cond2) for row in _rows_of("Berserker_twoh_003")}
    assert conditions == {
        "crit_multiplier": ("wielding_two_handed_weapon", None),
        "crit_armor_removed_in_wrath": ("in_wrath", "wielding_two_handed_weapon"),
        "crit_armor_fervour_cost": ("in_wrath", "wielding_two_handed_weapon"),
        "crit_armor_stack_cap": (None, None),
        "crit_armor_seconds": (None, None),
    }


def test_skull_splitters_figures_at_each_point():
    """6% multiplier and 10% a stack a point; a cost of 3 and 2 seconds from the first point; "At 5 points: This armor
    reduction stacks up to 6 times instead of 4", so 4 to 4 points (Max Points 5, exclusive) and 6 from 5."""
    for points in range(1, 9):
        assert _figure("Berserker_twoh_003", "crit_multiplier", points) == 6 * points
        assert _figure("Berserker_twoh_003", "crit_armor_removed_in_wrath", points) == 10 * points
        assert _figure("Berserker_twoh_003", "crit_armor_fervour_cost", points) == 3
        assert _figure("Berserker_twoh_003", "crit_armor_seconds", points) == 2
        assert _figure("Berserker_twoh_003", "crit_armor_stack_cap", points) == (6 if points >= 5 else 4), points


def test_deathmarks_cost_is_ten_less_one_a_point():
    """"Applying a Deathmark costs 10 Fervour, reduced by 1 per point": 9 at 1 point, 4 at 6."""
    assert [_figure("Berserker_crit_004", "deathmark_fervour_cost", points) for points in range(1, 7)] == [
        9, 8, 7, 6, 5, 4]
    assert [_figure("Berserker_crit_004", "deathmark_damage_taken_percent", points) for points in range(1, 7)] == [
        3, 6, 9, 12, 15, 18]
    assert {_figure("Berserker_crit_004", "deathmark_seconds", points) for points in range(1, 7)} == {5}


def test_blood_price_is_five_percent_of_the_maximum_a_point_for_three_seconds():
    assert [_figure("Berserker_trunk_008", "blood_price_percent_of_maximum_fervour", points)
            for points in (1, 8)] == [5, 40]
    assert {_figure("Berserker_trunk_008", "blood_price_seconds", points) for points in range(1, 9)} == {3}


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], number(row["ValuePerPoint"]), row["Condition"], row["Condition2"],
                    number(row["MinPoints"]), number(row["MaxPoints"]))
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat, float(row.value), row.cond or "", row.cond2 or "", float(row.min_points or 0),
                   float(row.max_points or 0))
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
    sheet.append([{"Node": "Berserker_crit_004", "Stat": "deathmark_damage_taken_percent", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="Skull Splitter, Deathmark and Blood Price writes"):
        rows.apply(copy)
