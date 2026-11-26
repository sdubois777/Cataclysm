"""Titanfall, the Bulwark's keystone: its sentence script, its rows script, and its generator rules.

2026-11-25, on the coordinating session's rulings T1-T6 and the sentence R6 the owner accepted on 2026-11-24.
`tools/apply_titanfall_sentence_2026_11_25.py` writes R6 into the Bulwark tree file by the node's id;
`tools/reapply_war_titanfall_workbook.py` writes one row, `titanfall` on `big_hit_taken`, 2 seconds, Event Value 20
(per cent of maximum health), Trigger Cooldown 30. `tools/generate_datatables.py` gains the action.

These tests hold the sentence in the tree file and the generated node table, the row in the workbook and the generated
effect table, that each script is idempotent and refuses what somebody changed since, and each new generator rule with
its control. The game's half is `game/Source/Cataclysm/Tests/CataclysmWarTitanfallTests.cpp`.
"""
from __future__ import annotations

import csv
import json
import pathlib
import shutil
import subprocess
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_titanfall_sentence_2026_11_25 as sentence  # noqa: E402
import generate_datatables as gen  # noqa: E402
import reapply_war_titanfall_workbook as rows  # noqa: E402

TREE_FILE = REPO_ROOT / "docs" / "Bulwark_Class_Tree_Final.json"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def test_the_tree_file_and_the_node_table_carry_r6():
    data = json.loads(TREE_FILE.read_text(encoding="utf-8"))
    (node,) = [each for each in data["nodes"] if each.get("id") == sentence.NODE_ID]
    assert node["data"]["description"] == sentence.AFTER
    with open(REPO_ROOT / "game" / "Data" / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        table = {row["Name"]: row for row in csv.DictReader(handle)}
    assert table[f"Bulwark_{sentence.NODE_ID}"]["Description"] == sentence.AFTER


def test_the_sentence_script_has_nothing_left_to_do():
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "apply_titanfall_sentence_2026_11_25.py"),
                             "--check", str(REPO_ROOT / "docs")], capture_output=True, text=True, cwd=REPO_ROOT)
    assert result.returncode == 0, result.stdout + result.stderr


def _with_sentence(tmp_path, text: str) -> pathlib.Path:
    raw = TREE_FILE.read_bytes()
    old = json.dumps(sentence.AFTER).encode()
    assert raw.count(old) == 1
    copy = tmp_path / TREE_FILE.name
    copy.write_bytes(raw.replace(old, json.dumps(text).encode()))
    return copy


def test_the_sentence_script_writes_r6_once_byte_for_byte(tmp_path):
    copy = _with_sentence(tmp_path, sentence.BEFORE)
    outcome = sentence.plan_file(copy)
    assert outcome.changes == [f"wrote R6's sentence into {sentence.NODE_ID}"]
    copy.write_bytes(outcome.new_bytes)
    assert copy.read_bytes() == TREE_FILE.read_bytes()
    assert sentence.plan_file(copy).new_bytes is None


def test_the_sentence_script_refuses_a_sentence_somebody_changed_since(tmp_path):
    copy = _with_sentence(tmp_path, "Somebody else's sentence.")
    with pytest.raises(sentence.order.Refused, match="neither the sentence R6 was ruled against"):
        sentence.plan_file(copy)


def test_the_rows_script_lists_titanfalls_row():
    ((node, _, wanted),) = rows.NODES
    assert node == "Bulwark_keystone_jg_final"
    assert [(row.action, row.action_event, row.value, row.event_value, row.trigger_cooldown) for row in wanted] == [
        ("titanfall", "big_hit_taken", 2, 20, 30)]


def test_the_workbook_holds_the_row():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_row():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["ActionEvent"], float(row["ValuePerPoint"]), float(row["EventValue"]),
                    float(row["TriggerCooldown"]), row["FractionOf"])
                   for row in csv.DictReader(handle)
                   if row["Node"] == "Bulwark_keystone_jg_final"]
    assert written == [("big_hit_taken", 2.0, 20.0, 30.0, "")]


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


def test_the_rows_script_writes_the_row_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 1, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_rows_script_refuses_a_row_somebody_wrote_on_the_node(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Bulwark_keystone_jg_final", "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="Titanfall, the Bulwark's keystone"):
        rows.apply(copy)
    assert copy.read_bytes() == before


#: Every column the passive reader asks for, as `test_passive_rows_carry_the_action_columns.py` writes them.
HEADERS = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Reach Metres", "Min Points",
           "Scale Max Steps", "Action", "Action Event", "Fraction Of", "Stack Seconds",
           "Scale Offset", "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
           "Trigger Cooldown", "Event Value", "Ailment", "Damage Share", "Per Point From", "Max Points"]

TITANFALL = {"Node": "Bulwark_keystone_jg_final", "Value Per Point": 2, "Action": "titanfall",
             "Action Event": "big_hit_taken", "Event Value": 20, "Trigger Cooldown": 30}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_a_titanfall_row_is_read_with_its_share_its_wait_and_no_fraction(tmp_path):
    (got,) = _read(tmp_path, TITANFALL)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["ValuePerPoint"], got["TriggerCooldown"],
            got["FractionOf"]) == ("titanfall", "big_hit_taken", 20.0, 2.0, 30.0, "")


def test_a_titanfall_row_with_no_share_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="acts on a big hit and states no Event Value"):
        _read(tmp_path, {**TITANFALL, "Event Value": None})


def test_a_titanfall_row_on_another_event_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="It is ruled on big_hit_taken only"):
        _read(tmp_path, {**TITANFALL, "Action Event": "hit_taken"})


def test_a_titanfall_row_unkillable_too_long_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="unkillable for 11 to 11 seconds"):
        _read(tmp_path, {**TITANFALL, "Value Per Point": 11})


def test_the_names_are_the_generators():
    assert gen.TITANFALL_ACTION == "titanfall"
    assert gen.TITANFALL_EVENTS == ("big_hit_taken",)
    assert gen.takes_a_trigger_cooldown(gen.TITANFALL_ACTION)
