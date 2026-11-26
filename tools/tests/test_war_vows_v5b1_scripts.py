"""The War vow options' fifth slice, second half, first part, V5b1: Scrap Mines' rows script and generator rules.

2026-11-25. `tools/reapply_war_vows_v5b1_workbook.py` writes two rows: `scrap_mine` on `gadget_destroyed` and on
`gadget_expired`, 200% within 3 metres. `tools/generate_datatables.py` gains the action and the event `gadget_expired`,
which names the wearer's own gadget and so may carry no strike.

These tests hold the rows, that the workbook and the generated table hold them, that the script is idempotent and
refuses what somebody wrote since; and each new generator rule, with its control. The game's half is
`game/Source/Cataclysm/Tests/CataclysmWarVowsV5b1Tests.cpp`.
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

import generate_datatables as gen  # noqa: E402
import reapply_war_vows_v5b1_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def test_the_script_lists_scrap_mines_two_rows():
    ((node, _, wanted),) = rows.NODES
    assert node == "Saboteur_capstone_50"
    assert [(row.option, row.action, row.action_event, row.value, row.event_value) for row in wanted] == [
        (2, "scrap_mine", "gadget_destroyed", 200, 3), (2, "scrap_mine", "gadget_expired", 200, 3)]


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = sorted((row["ActionEvent"], float(row["ValuePerPoint"]), float(row["EventValue"]), row["FractionOf"])
                         for row in csv.DictReader(handle)
                         if row["Node"] == "Saboteur_capstone_50" and row["Action"] == "scrap_mine")
    assert written == [("gadget_destroyed", 200.0, 3.0, ""), ("gadget_expired", 200.0, 3.0, "")]


def _workbook_without_ours(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    ours = {(node, row.key()) for node, _, wanted in rows.NODES for row in wanted}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_writes_both_rows_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 2, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_its_option(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_capstone_50", "Option": 2, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' fifth slice, second half, first part"):
        rows.apply(copy)
    assert copy.read_bytes() == before


#: Every column the passive reader asks for, as `test_passive_rows_carry_the_action_columns.py` writes them.
HEADERS = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Reach Metres", "Min Points",
           "Scale Max Steps", "Action", "Action Event", "Fraction Of", "Stack Seconds",
           "Scale Offset", "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
           "Trigger Cooldown", "Event Value", "Ailment", "Damage Share", "Per Point From", "Max Points"]

SCRAP = {"Node": "Saboteur_capstone_50", "Option": 2, "Value Per Point": 200, "Action": "scrap_mine",
         "Action Event": "gadget_expired", "Event Value": 3}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_a_scrap_row_is_read_with_its_radius_and_no_fraction(tmp_path):
    (got,) = _read(tmp_path, SCRAP)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["ValuePerPoint"], got["FractionOf"]) == (
        "scrap_mine", "gadget_expired", 3.0, 200.0, "")


def test_a_scrap_row_with_no_radius_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="leaves a scrap trap and states no Event Value"):
        _read(tmp_path, {**SCRAP, "Event Value": None})


def test_a_scrap_row_on_an_event_other_than_a_gadget_gone_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is ruled on gadget_destroyed, gadget_expired only"):
        _read(tmp_path, {**SCRAP, "Action Event": "trap_triggered"})


def test_the_names_are_the_generators():
    assert gen.SCRAP_MINE_ACTION == "scrap_mine"
    assert gen.SCRAP_MINE_EVENTS == ("gadget_destroyed", "gadget_expired")
    assert "gadget_expired" in gen.action_events()
    assert "gadget_expired" in gen.EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK
