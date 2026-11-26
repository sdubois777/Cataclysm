"""The War vow options' fourth slice, second half, V4b: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v4b_workbook.py` writes two rows: Counterfire's `counterfire` on `dodge` (one
answer for every 2 devices, 100% of a shot, a Trigger Cooldown of 1 second) and Magnum Opus's flag
`eldest_gadget_casts_every_skill`. `tools/generate_datatables.py` gains the action, which may state a Trigger Cooldown.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent
and refuses what somebody wrote since; and each new generator rule, with its control. The game's half is
`game/Source/Cataclysm/Tests/CataclysmWarVowsV4bTests.cpp`.
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
import reapply_war_vows_v4b_workbook as rows  # noqa: E402

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
    assert groups == {("Saboteur_capstone_100", 3), ("Saboteur_capstone_200", 2)}


def test_counterfire_answers_once_for_every_two_devices_once_a_second():
    (counterfire,) = _rows_of("Saboteur_capstone_100")
    assert (counterfire.action, counterfire.action_event, counterfire.value, counterfire.event_value,
            counterfire.trigger_cooldown) == ("counterfire", "dodge", 100, 2, 1)


def test_magnum_opus_is_a_flag():
    (opus,) = _rows_of("Saboteur_capstone_200")
    assert (opus.stat, opus.kind, opus.value) == ("eldest_gadget_casts_every_skill", "flat", 1)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = {(row["Node"], row["Option"]): row for row in csv.DictReader(handle)
                   if row["Node"] in ("Saboteur_capstone_100", "Saboteur_capstone_200")}
    counterfire = written[("Saboteur_capstone_100", "3")]
    assert (counterfire["Action"], counterfire["ActionEvent"], float(counterfire["ValuePerPoint"]),
            float(counterfire["EventValue"]), float(counterfire["TriggerCooldown"]), counterfire["FractionOf"]) == (
        "counterfire", "dodge", 100.0, 2.0, 1.0, "")
    opus = written[("Saboteur_capstone_200", "2")]
    assert (opus["Stat"], opus["ValueKind"], float(opus["ValuePerPoint"])) == (
        "eldest_gadget_casts_every_skill", "flat", 1.0)


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
    sheet.append([{"Node": "Saboteur_capstone_200", "Option": 2, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' fourth slice, second half"):
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

COUNTERFIRE = {"Node": "Saboteur_capstone_100", "Option": 3, "Value Per Point": 100, "Action": "counterfire",
               "Action Event": "dodge", "Event Value": 2, "Trigger Cooldown": 1}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_counterfire_is_read_with_its_count_and_its_second(tmp_path):
    (got,) = _read(tmp_path, COUNTERFIRE)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["TriggerCooldown"], got["ValuePerPoint"],
            got["FractionOf"]) == ("counterfire", "dodge", 2.0, 1.0, 100.0, "")


def test_counterfire_with_no_count_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="answers an evaded blow and states no Event Value"):
        _read(tmp_path, {**COUNTERFIRE, "Event Value": None})


def test_counterfire_with_a_count_that_is_not_whole_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is a whole number from 1 to 10"):
        _read(tmp_path, {**COUNTERFIRE, "Event Value": 1.5})


def test_counterfire_on_an_event_other_than_a_dodge_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is ruled on dodge only"):
        _read(tmp_path, {**COUNTERFIRE, "Action Event": "block"})


def test_the_names_are_the_generators():
    assert gen.COUNTERFIRE_ACTION == "counterfire"
    assert gen.COUNTERFIRE_EVENTS == ("dodge",)
    assert gen.takes_a_trigger_cooldown("counterfire")
