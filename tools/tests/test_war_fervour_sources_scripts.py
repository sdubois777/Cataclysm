"""The two scripts that give the War starting nodes their Fervour rates, and what they wrote.

SLICE C OF `plan_death_and_war.md`, 2026-11-24.
`tools/apply_war_fervour_sources_2026_11_24.py` makes the Saboteur's starting
sentence state its rates, by node name (and the Bulwark's, until the final
bundle's slice of 2026-11-26 took that node over:
`tools/tests/test_war_bulwark_kill_fervour_scripts.py`);
`tools/reapply_war_fervour_sources_workbook.py` writes the six rows into the
design workbook. These tests hold that the files in the repository are in the
state the scripts write, that each script is idempotent, and that each refuses
a node somebody changed since rather than overwriting it.
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

import apply_war_fervour_sources_2026_11_24 as sentences  # noqa: E402
import reapply_war_fervour_sources_workbook as rows  # noqa: E402

DOCS = REPO_ROOT / "docs"
WORKBOOK = DOCS / "All_Things_Cataclysm.xlsx"


def _node_sentence(path: pathlib.Path, name: str) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    found = [node for node in data["nodes"] if node.get("data", {}).get("name") == name]
    assert len(found) == 1, f"{path.name} holds {len(found)} nodes named {name!r}"
    return found[0]["data"]["description"]


# ---------------------------------------------------------------------------
# The sentences
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("tree", sorted(sentences.FIXES))
def test_the_tree_files_state_the_rates(tree):
    path = DOCS / f"{tree}_Class_Tree_Final.json"
    (fix,) = sentences.FIXES[tree]
    assert _node_sentence(path, fix.node) == fix.after


def test_the_sentence_script_has_nothing_left_to_do():
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "apply_war_fervour_sources_2026_11_24.py"),
                             "--check", str(DOCS)], capture_output=True, text=True, cwd=REPO_ROOT)
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_sentence_script_rewrites_the_old_sentence_once(tmp_path):
    """From the sentence it was approved against, to the new one, and then nothing."""
    original = DOCS / "Saboteur_Class_Tree_Final.json"
    copy = tmp_path / original.name
    (fix,) = sentences.FIXES["Saboteur"]
    copy.write_bytes(original.read_bytes().replace(
        json.dumps(fix.after)[1:-1].encode("utf-8"), json.dumps(fix.before)[1:-1].encode("utf-8")))
    assert _node_sentence(copy, fix.node) == fix.before

    outcome = sentences.plan_file(copy)
    assert outcome.new_bytes is not None and outcome.changes
    copy.write_bytes(outcome.new_bytes)
    assert _node_sentence(copy, fix.node) == fix.after
    assert copy.read_bytes() == original.read_bytes(), "the rewrite reproduces the repository's file byte for byte"
    assert sentences.plan_file(copy).new_bytes is None


def test_the_sentence_script_refuses_a_sentence_changed_since(tmp_path):
    original = DOCS / "Saboteur_Class_Tree_Final.json"
    copy = tmp_path / original.name
    (fix,) = sentences.FIXES["Saboteur"]
    copy.write_bytes(original.read_bytes().replace(
        json.dumps(fix.after)[1:-1].encode("utf-8"), b"Somebody else's words."))
    with pytest.raises(sentences.order.Refused):
        sentences.plan_file(copy)


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], float(row["ValuePerPoint"]))
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            assert (node, row.stat, row.kind, float(row.value)) in written, (node, row.stat)


def _workbook_without_the_rows(tmp_path) -> pathlib.Path:
    """A copy of the workbook with the six rows taken out."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.SHEET]
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
    assert sum(1 for line in first if ": + " in line) == 6, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_row_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Berserker_trunk_000", "Stat": "fervour_per_critical_strike", "Value Kind": "flat",
                   "Value Per Point": 2}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.ReapplyError, match="somebody has written it since"):
        rows.apply(copy)
