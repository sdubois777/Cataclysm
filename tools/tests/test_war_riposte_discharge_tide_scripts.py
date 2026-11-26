"""The two scripts for Riposte, Vengeful Discharge and Crimson Tide, and what they wrote.

2026-11-25. `tools/apply_war_riposte_discharge_tide_2026_11_25.py` rewords Vengeful Discharge (the War rulings' R7) and
Crimson Tide (its fractions made steps); `tools/reapply_war_riposte_discharge_tide_workbook.py` writes sixteen rows on
the three nodes. These tests hold that the files are in the state the scripts write, that each script is idempotent,
and that each refuses a node somebody changed since.
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

import apply_war_riposte_discharge_tide_2026_11_25 as sentences  # noqa: E402
import reapply_war_riposte_discharge_tide_workbook as rows  # noqa: E402

DOCS = REPO_ROOT / "docs"
WORKBOOK = DOCS / "All_Things_Cataclysm.xlsx"


def _sentence(path: pathlib.Path, name: str) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    (found,) = [node for node in data["nodes"] if node.get("data", {}).get("name") == name]
    return found["data"]["description"]


@pytest.mark.parametrize("tree", sorted(sentences.FIXES))
def test_the_tree_files_hold_the_new_sentences(tree):
    for fix in sentences.FIXES[tree]:
        assert _sentence(DOCS / f"{tree}_Class_Tree_Final.json", fix.node) == fix.after


def test_no_figure_in_crimson_tide_is_a_fraction():
    """The design document's rule "A threshold or a step, never a fraction"."""
    (fix,) = sentences.FIXES["Berserker"]
    assert "0.5" not in fix.after and "0.5" in fix.before


def test_the_sentence_script_has_nothing_left_to_do():
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "apply_war_riposte_discharge_tide_2026_11_25.py"),
                             "--check", str(DOCS)], capture_output=True, text=True, cwd=REPO_ROOT)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("tree", sorted(sentences.FIXES))
def test_the_sentence_script_rewrites_the_old_sentence_once(tmp_path, tree):
    original = DOCS / f"{tree}_Class_Tree_Final.json"
    copy = tmp_path / original.name
    (fix,) = sentences.FIXES[tree]
    raw = original.read_bytes()
    after = json.dumps(fix.after)[1:-1].encode("utf-8")
    assert raw.count(after) == 1
    copy.write_bytes(raw.replace(after, json.dumps(fix.before)[1:-1].encode("utf-8")))
    outcome = sentences.plan_file(copy)
    assert outcome.new_bytes is not None
    copy.write_bytes(outcome.new_bytes)
    assert copy.read_bytes() == raw
    assert sentences.plan_file(copy).new_bytes is None


def test_the_sentence_script_refuses_a_sentence_changed_since(tmp_path):
    original = DOCS / "Bulwark_Class_Tree_Final.json"
    copy = tmp_path / original.name
    (fix,) = sentences.FIXES["Bulwark"]
    copy.write_bytes(original.read_bytes().replace(json.dumps(fix.after)[1:-1].encode("utf-8"), b"Somebody else's words."))
    with pytest.raises(sentences.order.Refused):
        sentences.plan_file(copy)


def test_there_are_sixteen_rows_on_three_nodes():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == 16
    assert [node for node, _, _ in rows.NODES] == ["Bulwark_keystone_ic_t1", "Bulwark_basic_tw_resolve_001",
                                                    "Berserker_leech_001"]


def test_riposte_judges_its_two_conditions_on_the_share_and_the_stun():
    """The share holds at 25 Fervour or more and the stun at the maximum; the cost carries no condition of its own,
    because `RiposteOnBlock` spends it only when the share holds."""
    (riposte,) = [wanted for node, _, wanted in rows.NODES if node == "Bulwark_keystone_ic_t1"]
    conditions = {row.stat: (row.cond, row.cond_value) for row in riposte}
    assert conditions == {"riposte_share_of_blocked": ("class_resource_points_at_least", 25),
                          "riposte_fervour_cost": (None, None),
                          "riposte_stun_seconds_at_maximum": ("class_resource_at_maximum", None)}


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Condition"], number(row["MinPoints"])) for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat, row.kind, float(row.value), row.tags or "", row.cond or "",
                   float(row.min_points or 0))
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
    assert sum(1 for line in first if ": + " in line) == 16, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_row_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Bulwark_keystone_ic_t1", "Stat": "riposte_fervour_cost", "Value Kind": "flat",
                   "Value Per Point": 30}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the last Fervour spends writes"):
        rows.apply(copy)
