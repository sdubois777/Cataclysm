"""The script for the Saboteur's device conditions, and what it wrote.

2026-11-25. `tools/reapply_war_saboteur_devices_workbook.py` writes the rows of the Saboteur nodes whose figures hold
while, or for some seconds after, something happens to the player's traps and gadgets. These tests hold that the
workbook and the generated table are in the state the script writes, that it is idempotent, that it refuses a node
somebody changed since, and that it adds the two columns a row's own stack needs to a sheet that lacks them.
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

import reapply_war_saboteur_devices_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
ROW_COUNT = 13


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def test_the_script_writes_its_rows_on_its_nodes():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    assert [node for node, _, _ in rows.NODES] == [
        "Saboteur_basic_trap_mid_006", "Saboteur_basic_trap_mid_002", "Saboteur_basic_gadget_mid_004",
        "Saboteur_basic_gadget_stem_005", "Saboteur_basic_trunk_012", "Saboteur_basic_gadget_mid_006",
        "Saboteur_basic_trap_stem_004", "Saboteur_basic_gadget_mid_002", "Saboteur_keystone_trap_qty"]


def test_snap_back_is_the_trap_count_capped_at_one_step():
    """"While you have at least one active trap" is the count of traps, one step at most: 4% with one trap or five."""
    (snap_back,) = _rows_of("Saboteur_basic_trap_mid_006")
    assert (snap_back.scale, snap_back.step, snap_back.cap) == ("traps_active", 1, 1)


def test_the_two_timed_nodes_grant_one_stack_on_their_event_for_their_seconds():
    timed = {(row.stat, row.action_event, row.stack_seconds, row.cap, row.scale)
             for node in ("Saboteur_basic_trap_mid_002", "Saboteur_basic_gadget_mid_004") for row in _rows_of(node)}
    assert timed == {("movement_speed", "trap_triggered", 3, 1, "own_stacks"),
                     ("movement_speed", "gadget_destroyed", 5, 1, "own_stacks"),
                     ("evasion", "gadget_destroyed", 5, 1, "own_stacks")}


def test_support_network_gives_its_radius_once_from_four_points_while_a_gadget_is_active():
    """The coordinating session's ruling of 2026-11-25: 10% while at least one gadget is active, not 10% a gadget."""
    (radius,) = [row for row in _rows_of("Saboteur_basic_gadget_stem_005") if row.stat == "trap_trigger_radius"]
    assert (radius.value, radius.scale, radius.step, radius.cap, radius.min_points) == (10, "gadgets_active", 1, 1, 4)


def test_razor_instincts_is_one_row_on_the_one_placing_event():
    """The coordinating session's ruling of 2026-11-25: one `device_placed` event for a trap or a gadget, and the node
    one row on it, so one placing grants one stack (3% at 1 point, not 6%)."""
    (razor,) = _rows_of("Saboteur_basic_trunk_012")
    assert (razor.stat, razor.value, razor.action_event, razor.stack_seconds, razor.cap) == (
        "evasion", 3, "device_placed", 3, 1)


def test_each_condition_node_states_its_ruled_condition_and_value():
    conditions = {node: {(row.stat, row.cond, row.cond_value) for row in wanted}
                  for node, _, wanted in rows.NODES if any(row.cond for row in wanted)}
    assert conditions == {
        "Saboteur_basic_gadget_mid_006": {("evasion", "own_gadget_within_metres", 15)},
        "Saboteur_basic_trap_stem_004": {("minion_damage", "target_not_yet_triggered_your_trap", None)},
        "Saboteur_basic_gadget_mid_002": {("evasion", "seconds_after_gadget_struck", 2)},
        "Saboteur_keystone_trap_qty": {("minion_damage", "no_room_for_a_trap", None),
                                       ("trap_trigger_radius", "no_room_for_a_trap", None)},
    }


def test_overwhelming_presence_doubles_the_radius_as_a_more():
    """The ruling: "doubled" is a true doubling, 100% more, as the 50% more damage beside it is."""
    kinds = {(row.stat, row.kind, row.value) for row in _rows_of("Saboteur_keystone_trap_qty")}
    assert kinds == {("minion_damage", "more", 50), ("trap_trigger_radius", "more", 100)}


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Scale"], number(row["ScaleStep"]), number(row["ScaleMaxSteps"]), number(row["MinPoints"]),
                    row["ActionEvent"], number(row["StackSeconds"]), row["Condition"],
                    number(row["ConditionValue"])) for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat, row.kind, float(row.value), row.tags or "", row.scale or "", float(row.step or 0),
                   float(row.cap or 0), float(row.min_points or 0), row.action_event or "",
                   float(row.stack_seconds or 0), row.cond or "", float(row.cond_value or 0))
            assert written.count(key) == 1, key


def _workbook_without_the_rows(tmp_path, drop_columns: tuple[str, ...] = ()) -> pathlib.Path:
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
    for name in drop_columns:
        header = [cell.value for cell in sheet[1]]
        sheet.delete_cols(header.index(name) + 1, 1)
    book.save(copy)
    return copy


def test_the_row_script_appends_the_rows_once(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_row_script_adds_the_two_stack_columns_to_a_sheet_without_them(tmp_path):
    """The first War slice whose row is "for N seconds after" an event adds Action Event and Stack Seconds. THE
    CONTROL: the copy is checked to lack both before the script runs."""
    copy = _workbook_without_the_rows(tmp_path, drop_columns=(rows.base.ACTION_EVENT, rows.base.STACK_SECONDS))
    header = [cell.value for cell in openpyxl.load_workbook(copy)[rows.base.SHEET][1]]
    assert rows.base.ACTION_EVENT not in header and rows.base.STACK_SECONDS not in header
    report = rows.apply(copy)
    assert f"added the column {rows.base.ACTION_EVENT!r} to {rows.base.SHEET!r}" in report, report
    assert f"added the column {rows.base.STACK_SECONDS!r} to {rows.base.SHEET!r}" in report, report
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_row_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_basic_trap_mid_006", "Stat": "evasion", "Value Kind": "flat",
                   "Value Per Point": 5}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the Saboteur's device conditions writes"):
        rows.apply(copy)
