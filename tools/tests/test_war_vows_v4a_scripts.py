"""The War vow options' fourth slice, first half, V4a: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v4a_workbook.py` writes two rows: Baited's `taunt_enemies_near_trap` on
`trap_armed`, and Trail of Wire's `place_last_trap_again` on `movement_skill`. `tools/generate_datatables.py` gains
the two actions and the event `trap_armed`, which names the wearer's own trap and so may carry no strike.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent
and refuses what somebody wrote since; and each new generator rule, with its control. The game's half is
`game/Source/Cataclysm/Tests/CataclysmWarVowsV4aTests.cpp`.
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
import reapply_war_vows_v4a_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_script_lists_one_row_on_each_of_two_options():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == 2
    groups = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert groups == {("Saboteur_capstone_25", 1), ("Saboteur_capstone_50", 1)}


def test_baited_taunts_within_eight_metres_for_four_seconds_when_a_trap_arms():
    (baited,) = _rows_of("Saboteur_capstone_25")
    assert (baited.action, baited.action_event, baited.value, baited.event_value) == (
        "taunt_enemies_near_trap", "trap_armed", 4, 8)


def test_trail_of_wire_places_the_last_trap_again_on_the_movement_skill():
    (trail,) = _rows_of("Saboteur_capstone_50")
    assert (trail.action, trail.action_event, trail.value) == ("place_last_trap_again", "movement_skill", 100)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = {(row["Node"], row["Option"]): row for row in csv.DictReader(handle)
                   if row["Node"] in ("Saboteur_capstone_25", "Saboteur_capstone_50")}
    baited = written[("Saboteur_capstone_25", "1")]
    assert (baited["Action"], baited["ActionEvent"], float(baited["ValuePerPoint"]), float(baited["EventValue"]),
            baited["FractionOf"]) == ("taunt_enemies_near_trap", "trap_armed", 4.0, 8.0, "")
    trail = written[("Saboteur_capstone_50", "1")]
    assert (trail["Action"], trail["ActionEvent"], float(trail["ValuePerPoint"]), trail["FractionOf"]) == (
        "place_last_trap_again", "movement_skill", 100.0, "")


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


def test_the_script_writes_both_rows_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 2, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_one_of_its_options(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_capstone_25", "Option": 1, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' fourth slice, first half"):
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

BAITED = {"Node": "Saboteur_capstone_25", "Option": 1, "Value Per Point": 4, "Action": "taunt_enemies_near_trap",
          "Action Event": "trap_armed", "Event Value": 8}

TRAIL = {"Node": "Saboteur_capstone_50", "Option": 1, "Value Per Point": 100, "Action": "place_last_trap_again",
         "Action Event": "movement_skill"}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_baited_is_read_with_its_radius_and_no_fraction(tmp_path):
    (got,) = _read(tmp_path, BAITED)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["ValuePerPoint"], got["FractionOf"]) == (
        "taunt_enemies_near_trap", "trap_armed", 8.0, 4.0, "")


def test_baited_with_no_radius_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="taunts the enemies near a trap and states no Event Value"):
        _read(tmp_path, {**BAITED, "Event Value": None})


def test_baited_on_an_event_other_than_a_trap_arming_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is ruled on trap_armed only"):
        _read(tmp_path, {**BAITED, "Action Event": "trap_placed"})


def test_baited_beyond_thirty_seconds_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="taunts for 31 to 31 seconds"):
        _read(tmp_path, {**BAITED, "Value Per Point": 31})


def test_the_control_trail_of_wire_is_read_with_no_fraction(tmp_path):
    (got,) = _read(tmp_path, TRAIL)
    assert (got["Action"], got["ActionEvent"], got["ValuePerPoint"], got["FractionOf"]) == (
        "place_last_trap_again", "movement_skill", 100.0, "")


def test_trail_of_wire_with_a_value_other_than_100_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is 100, always"):
        _read(tmp_path, {**TRAIL, "Value Per Point": 50})


def test_trail_of_wire_on_an_event_other_than_the_movement_skill_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is ruled on movement_skill only"):
        _read(tmp_path, {**TRAIL, "Action Event": "skill_use"})


def test_the_names_are_the_generators():
    assert gen.TAUNT_NEAR_TRAP_ACTION == "taunt_enemies_near_trap"
    assert gen.PLACE_LAST_TRAP_AGAIN_ACTION == "place_last_trap_again"
    assert gen.TAUNT_NEAR_TRAP_EVENTS == ("trap_armed",)
    assert gen.PLACE_LAST_TRAP_AGAIN_EVENTS == ("movement_skill",)
    assert "trap_armed" in gen.action_events()
    assert "trap_armed" in gen.EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK
