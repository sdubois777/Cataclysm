"""The script that writes the Berserker's states' rows, and what it wrote.

THE BERSERKER'S STATES, `plan_death_and_war.md` slice B29, 2026-11-25. `tools/reapply_war_berserker_states_workbook.py`
writes eight rows on Wrath, Frenzy and Berserking with slice D's machinery. These tests hold that the workbook and the
generated table hold them, that the script is idempotent, and that it refuses a node somebody wrote since.
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

import reapply_war_berserker_states_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def _all_rows():
    return [(node, row) for node, _, wanted in rows.NODES for row in wanted]


def test_there_are_eight_rows_on_three_nodes():
    assert len(_all_rows()) == 8
    assert [node for node, _, _ in rows.NODES] == ["Berserker_twoh_001", "Berserker_dw_001", "Berserker_trunk_009"]


def test_each_label_is_the_nodes_own_name():
    with open(REPO_ROOT / "game" / "Data" / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        names = {row["Name"]: row["NodeName"] for row in csv.DictReader(handle)}
    assert [(node, label) for node, label, _ in rows.NODES if names.get(node) != label] == []


def test_each_state_begins_under_its_weapon_rule_and_its_bonuses_hold_only_in_it():
    """The flag is the one row under the weapon rule; every other row of Wrath and Frenzy is under its state, so a
    bonus never outlives the state, and the weapon rule decides only whether the state can begin."""
    by_node = {node: wanted for node, _, wanted in rows.NODES}
    for node, flag, weapon, state in (("Berserker_twoh_001", "wrath_on_full_fervour", "wielding_two_handed_weapon",
                                       "in_wrath"),
                                      ("Berserker_dw_001", "frenzy_on_full_fervour", "dual_wielding", "in_frenzy")):
        conditions = {row.stat: row.cond for row in by_node[node]}
        assert conditions.pop(flag) == weapon
        assert set(conditions.values()) == {state}, conditions


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Condition"]) for row in csv.DictReader(handle)]
    for node, row in _all_rows():
        wanted = (node, row.stat, row.kind, float(row.value), row.tags or "", row.cond or "")
        assert written.count(wanted) == 1, wanted


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


def test_the_script_appends_the_rows_once(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 8, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Berserker_dw_001", "Stat": "attack_speed", "Value Kind": "increased",
                   "Value Per Point": 25}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the Berserker's states writes"):
        rows.apply(copy)
