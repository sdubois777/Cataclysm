"""The two scripts that give the Bulwark's starting node its Fervour for a kill, and what they wrote.

THE FINAL BUNDLE, 2026-11-26. `tools/apply_war_bulwark_kill_fervour_sentence_2026_11_26.py` makes the node's sentence
state "1 for each enemy you kill"; `tools/reapply_war_bulwark_kill_fervour_workbook.py` writes the node's four rows,
slice C's three and the `fervour_per_kill` row, into the design workbook. These tests hold that the files in the
repository are in the state the scripts write, that each script is idempotent, that the row script appends only the
kill row to a workbook holding slice C's three, and that each refuses a node somebody changed since.
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

import apply_war_bulwark_kill_fervour_sentence_2026_11_26 as sentence  # noqa: E402
import apply_war_fervour_sources_2026_11_24 as slice_c_sentences  # noqa: E402
import reapply_war_bulwark_kill_fervour_workbook as rows  # noqa: E402
import reapply_war_fervour_sources_workbook as slice_c_rows  # noqa: E402

DOCS = REPO_ROOT / "docs"
TREE = DOCS / "Bulwark_Class_Tree_Final.json"
WORKBOOK = DOCS / "All_Things_Cataclysm.xlsx"
SHEET = rows.base.SHEET


def _node_sentence(path: pathlib.Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    found = [node for node in data["nodes"] if node.get("data", {}).get("name") == "Fervour"]
    assert len(found) == 1, f"{path.name} holds {len(found)} nodes named 'Fervour'"
    return found[0]["data"]["description"]


# ---------------------------------------------------------------------------
# The sentence
# ---------------------------------------------------------------------------

def test_the_tree_file_states_the_kill_fervour():
    assert _node_sentence(TREE) == sentence.AFTER
    assert "1 for each enemy you kill" in sentence.AFTER


def test_the_sentence_script_has_nothing_left_to_do():
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "apply_war_bulwark_kill_fervour_sentence_2026_11_26.py"),
                             "--check", str(DOCS)], capture_output=True, text=True, cwd=REPO_ROOT)
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_sentence_script_rewrites_slice_cs_sentence_once(tmp_path):
    """From slice C's sentence to the new one, byte for byte the repository's file, and then nothing."""
    copy = tmp_path / TREE.name
    copy.write_bytes(TREE.read_bytes().replace(json.dumps(sentence.AFTER)[1:-1].encode("utf-8"),
                                               json.dumps(sentence.BEFORE)[1:-1].encode("utf-8")))
    assert _node_sentence(copy) == sentence.BEFORE

    outcome = sentence.plan_file(copy)
    assert outcome.new_bytes is not None and outcome.changes
    copy.write_bytes(outcome.new_bytes)
    assert copy.read_bytes() == TREE.read_bytes(), "the rewrite reproduces the repository's file byte for byte"
    assert sentence.plan_file(copy).new_bytes is None


def test_the_sentence_script_refuses_a_sentence_changed_since(tmp_path):
    copy = tmp_path / TREE.name
    copy.write_bytes(TREE.read_bytes().replace(json.dumps(sentence.AFTER)[1:-1].encode("utf-8"),
                                               b"Somebody else's words."))
    with pytest.raises(sentence.order.Refused):
        sentence.plan_file(copy)


def test_slice_cs_sentence_script_no_longer_writes_the_bulwark():
    """TAKEN OVER, NOT SHARED: slice C's script left alone, it would refuse the new sentence as somebody else's."""
    assert "Bulwark" not in slice_c_sentences.FIXES
    assert sentence.BEFORE == ("Taking hits, blocking and killing enemies generate Fervour: 1 for each hit you take and "
                               "1 for each block. While you have this, your Fervour decays at 5 per second after 3 "
                               "seconds out of combat.")


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_four_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        held = sorted((row["Stat"], row["ValueKind"], float(row["ValuePerPoint"]), row["Condition"])
                      for row in csv.DictReader(handle) if row["Node"] == rows.NODE)
    assert held == sorted([
        ("fervour_per_hit_taken", "flat", 1.0, ""),
        ("fervour_per_block", "flat", 1.0, ""),
        ("fervour_decay_out_of_combat_per_second", "flat", 5.0, ""),
        ("fervour_per_kill", "flat", 1.0, ""),
    ]), held


def test_slice_cs_row_script_no_longer_writes_the_bulwark():
    assert rows.NODE not in {node for node, _, _ in slice_c_rows.NODES}


def _workbook_keeping(tmp_path, keep) -> pathlib.Path:
    """A copy of the workbook with the Bulwark starting node's rows taken out, except those whose stat `keep` holds."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    stat_column = header.index("Stat") + 1
    for index in range(sheet.max_row, 1, -1):
        if sheet.cell(index, node_column).value == rows.NODE and sheet.cell(index, stat_column).value not in keep:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_a_workbook_with_slice_cs_three_rows_gets_only_the_kill_row(tmp_path):
    """THE CASE A MERGE MEETS: slice C's script has written its three; this one appends the fourth, once."""
    copy = _workbook_keeping(tmp_path, {"fervour_per_hit_taken", "fervour_per_block",
                                        "fervour_decay_out_of_combat_per_second"})
    first = rows.apply(copy)
    appended = [line for line in first if ": + " in line]
    assert appended == [f"{rows.NODE}: + fervour_per_kill flat 1"], first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_a_workbook_with_none_of_the_rows_gets_all_four(tmp_path):
    """THE CONTROL for the case above: the same script on a node with no rows writes four, not one."""
    copy = _workbook_keeping(tmp_path, set())
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 4, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_row_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_keeping(tmp_path, set())
    book = openpyxl.load_workbook(copy)
    sheet = book[SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": rows.NODE, "Stat": "fervour_per_kill", "Value Kind": "flat",
                   "Value Per Point": 2}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="somebody has written it since"):
        rows.apply(copy)
