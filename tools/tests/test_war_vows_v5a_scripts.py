"""The War vow options' fifth slice, first half, V5a: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v5a_workbook.py` writes five rows: Lightning Rod's chance for each device,
Creeping Mines' speed and reach, Where They Fall's `copy_last_trap_at_kill` on `kill`, and Crossfire's `crossfire` on
`hit_dealt`. `tools/generate_datatables.py` gains the two actions.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent
and refuses what somebody wrote since; and each new generator rule, with its control. The game's half is
`game/Source/Cataclysm/Tests/CataclysmWarVowsV5aTests.cpp`.
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
import reapply_war_vows_v5a_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_script_lists_five_rows_on_four_options():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == 5
    groups = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert groups == {("Saboteur_capstone_25", 3), ("Saboteur_capstone_100", 1), ("Saboteur_capstone_200", 1),
                      ("Saboteur_capstone_200", 3)}


def test_lightning_rod_is_three_for_each_device():
    (rod,) = _rows_of("Saboteur_capstone_25")
    assert (rod.option, rod.stat, rod.kind, rod.value) == (3, "blow_to_device_chance_per_device", "flat", 3)


def test_creeping_mines_creep_three_metres_a_second_within_ten():
    speed, reach = _rows_of("Saboteur_capstone_100")
    assert (speed.option, speed.stat, speed.value) == (1, "trap_creep_metres_per_second", 3)
    assert (reach.option, reach.stat, reach.value) == (1, "trap_creep_reach_metres", 10)


def test_where_they_fall_and_crossfire():
    fall, crossfire = _rows_of("Saboteur_capstone_200")
    assert (fall.option, fall.action, fall.action_event, fall.value, fall.event_value) == (
        1, "copy_last_trap_at_kill", "kill", 4, 10)
    assert (crossfire.option, crossfire.action, crossfire.action_event, crossfire.value, crossfire.event_value) == (
        3, "crossfire", "hit_dealt", 10, 15)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Option"], row["Stat"], row["Action"], row["ActionEvent"],
                    float(row["ValuePerPoint"]), float(row["EventValue"] or 0), row["FractionOf"])
                   for row in csv.DictReader(handle) if row["Node"].startswith("Saboteur_capstone_")]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, str(row.option), row.stat or "", row.action or "", row.action_event or "", float(row.value),
                   float(row.event_value or 0), "")
            assert written.count(key) == 1, key


# ---------------------------------------------------------------------------
# The script on a copy of the workbook
# ---------------------------------------------------------------------------

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


def test_the_script_writes_all_five_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 5, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_one_of_its_options(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_capstone_100", "Option": 1, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' fifth slice, first half"):
        rows.apply(copy)
    assert copy.read_bytes() == before


# ---------------------------------------------------------------------------
# The generator's new rules, each with its control
# ---------------------------------------------------------------------------

#: Every column the passive reader asks for, as `test_passive_rows_carry_the_action_columns.py` writes them.
HEADERS = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Reach Metres", "Min Points",
           "Scale Max Steps", "Action", "Action Event", "Fraction Of", "Stack Seconds",
           "Scale Offset", "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
           "Trigger Cooldown", "Event Value", "Ailment", "Damage Share", "Per Point From", "Max Points"]

FALL = {"Node": "Saboteur_capstone_200", "Option": 1, "Value Per Point": 4, "Action": "copy_last_trap_at_kill",
        "Action Event": "kill", "Event Value": 10}

CROSSFIRE = {"Node": "Saboteur_capstone_200", "Option": 3, "Value Per Point": 10, "Action": "crossfire",
             "Action Event": "hit_dealt", "Event Value": 15}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


@pytest.mark.parametrize("cells", [FALL, CROSSFIRE], ids=["where_they_fall", "crossfire"])
def test_the_control_each_is_read_with_its_metres_and_no_fraction(tmp_path, cells):
    (got,) = _read(tmp_path, cells)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["FractionOf"]) == (
        cells["Action"], cells["Action Event"], float(cells["Event Value"]), "")


@pytest.mark.parametrize("cells", [FALL, CROSSFIRE], ids=["where_they_fall", "crossfire"])
def test_each_with_no_metres_is_refused(tmp_path, cells):
    with pytest.raises(gen.DataError, match="states no Event Value. It is the metres from a device"):
        _read(tmp_path, {**cells, "Event Value": None})


def test_where_they_fall_on_an_event_other_than_a_kill_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is ruled on kill only"):
        _read(tmp_path, {**FALL, "Action Event": "afflicted_death"})


def test_crossfire_above_a_whole_hit_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is above 0 and up to 100"):
        _read(tmp_path, {**CROSSFIRE, "Value Per Point": 101})


def test_the_names_are_the_generators():
    assert gen.COPY_LAST_TRAP_AT_KILL_ACTION == "copy_last_trap_at_kill"
    assert gen.CROSSFIRE_ACTION == "crossfire"
    assert gen.COPY_LAST_TRAP_AT_KILL_EVENTS == ("kill",)
    assert gen.CROSSFIRE_EVENTS == ("hit_dealt",)
