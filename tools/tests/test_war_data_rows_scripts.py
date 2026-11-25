"""The two scripts that give slice D's War nodes their rows, and what they wrote.

SLICE D OF `plan_death_and_war.md`, 2026-11-24.
`tools/apply_war_data_rows_2026_11_24.py` rewords the eleven War sentences a
ruling changed, by node name and, for Crimson Pact, by option name;
`tools/reapply_war_data_rows_workbook.py` writes the forty-eight rows into the
design workbook and adds its `Per Point From` column. These tests hold that the
files in the repository are in the state the scripts write, that each script is
idempotent, that the row script reproduces the repository's sheet when run on a
workbook without its rows, and that each refuses a node somebody changed since
rather than overwriting it.
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

import apply_war_data_rows_2026_11_24 as sentences  # noqa: E402
import reapply_war_data_rows_workbook as rows  # noqa: E402

DOCS = REPO_ROOT / "docs"
WORKBOOK = DOCS / "All_Things_Cataclysm.xlsx"
DATA = REPO_ROOT / "game" / "Data"


def _tree(tree: str) -> pathlib.Path:
    return DOCS / f"{tree}_Class_Tree_Final.json"


def _sentence(path: pathlib.Path, fix) -> str:
    """The sentence a fix rewrites: a node's, or a capstone option's."""
    data = json.loads(path.read_text(encoding="utf-8"))
    name = fix.capstone if isinstance(fix, sentences.OptionReword) else fix.node
    found = [node for node in data["nodes"] if node.get("data", {}).get("name") == name]
    assert len(found) == 1, f"{path.name} holds {len(found)} nodes named {name!r}"
    if not isinstance(fix, sentences.OptionReword):
        return found[0]["data"]["description"]
    options = [option for option in found[0]["data"]["options"] if option.get("name") == fix.option]
    assert len(options) == 1, f"{name!r} holds {len(options)} options named {fix.option!r}"
    return options[0]["description"]


def _swap(raw: bytes, old: str, new: str) -> bytes:
    """Replace one sentence's JSON text with another's, refusing anything but exactly one occurrence."""
    old_bytes = json.dumps(old)[1:-1].encode("utf-8")
    assert raw.count(old_bytes) == 1, f"{old!r} occurs {raw.count(old_bytes)} times"
    return raw.replace(old_bytes, json.dumps(new)[1:-1].encode("utf-8"))


ALL_FIXES = [(tree, fix) for tree, fixes in sorted(sentences.FIXES.items()) for fix in fixes]


# ---------------------------------------------------------------------------
# The sentences
# ---------------------------------------------------------------------------

def test_there_are_eleven_rewordings():
    """Nine rulings the owner accepted (Crimson Pact, R15, the one option among them), Barbed Skin's ruled wording
    and Bloodthirst's whole step. The script's docstring names each."""
    assert len(ALL_FIXES) == 11
    assert sum(isinstance(fix, sentences.OptionReword) for _, fix in ALL_FIXES) == 1


@pytest.mark.parametrize("tree, fix", ALL_FIXES, ids=[getattr(f, "node", None) or f.option for _, f in ALL_FIXES])
def test_the_tree_files_hold_the_new_sentences(tree, fix):
    assert _sentence(_tree(tree), fix) == fix.after


def test_the_sentence_script_has_nothing_left_to_do():
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "apply_war_data_rows_2026_11_24.py"),
                             "--check", str(DOCS)], capture_output=True, text=True, cwd=REPO_ROOT)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("tree", sorted(sentences.FIXES))
def test_the_sentence_script_rewrites_the_old_sentences_once(tmp_path, tree):
    """From the sentences each change was approved against, to the new ones, byte for byte, and then nothing."""
    original = _tree(tree)
    copy = tmp_path / original.name
    raw = original.read_bytes()
    for fix in sentences.FIXES[tree]:
        raw = _swap(raw, fix.after, fix.before)
    copy.write_bytes(raw)
    for fix in sentences.FIXES[tree]:
        assert _sentence(copy, fix) == fix.before

    outcome = sentences.plan_file(copy)
    assert outcome.new_bytes is not None and len(outcome.changes) == len(sentences.FIXES[tree])
    copy.write_bytes(outcome.new_bytes)
    assert copy.read_bytes() == original.read_bytes(), "the rewrite reproduces the repository's file byte for byte"
    assert sentences.plan_file(copy).new_bytes is None


def test_the_sentence_script_refuses_a_node_sentence_changed_since(tmp_path):
    original = _tree("Bulwark")
    copy = tmp_path / original.name
    fix = sentences.FIXES["Bulwark"][0]
    copy.write_bytes(_swap(original.read_bytes(), fix.after, "Somebody else's words."))
    with pytest.raises(sentences.order.Refused):
        sentences.plan_file(copy)


def test_the_sentence_script_refuses_an_option_sentence_changed_since(tmp_path):
    """Crimson Pact is found by its name inside the Third Oath, and its words are checked like a node's."""
    original = _tree("Berserker")
    copy = tmp_path / original.name
    (fix,) = [fix for fix in sentences.FIXES["Berserker"] if isinstance(fix, sentences.OptionReword)]
    copy.write_bytes(_swap(original.read_bytes(), fix.after, "Somebody else's words."))
    with pytest.raises(sentences.order.Refused, match="Crimson Pact"):
        sentences.plan_file(copy)


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def _all_rows() -> list[tuple[str, str, rows.Row]]:
    return [(node, label, row) for node, label, wanted in rows.NODES for row in wanted]


def test_there_are_forty_eight_rows_on_thirty_nodes():
    """The figure `AUTHORED_ROWS` and `AUTHORED_NODES` moved by in `test_passive_effects_match_the_node_text.py`."""
    assert len(_all_rows()) == 48
    assert len({node for node, _, _ in rows.NODES}) == len(rows.NODES) == 30


def test_each_label_is_the_nodes_own_name():
    """The `Node Name` column is a reader's label, so it has to be the name the tree file gives the node."""
    with open(DATA / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        names = {row["Name"]: row["NodeName"] for row in csv.DictReader(handle)}
    wrong = [(node, label, names.get(node)) for node, label, _ in rows.NODES if names.get(node) != label]
    assert not wrong, wrong


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    """Every column the script writes, read back from the generated table."""
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(DATA / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Condition"], number(row["ConditionValue"]), row["Scale"], number(row["ScaleStep"]),
                    number(row["Option"]), number(row["MinPoints"]), number(row["ScaleMaxSteps"]),
                    number(row["PerPointFrom"]))
                   for row in csv.DictReader(handle)]
    for node, _, row in _all_rows():
        wanted = (node, row.stat, row.kind, float(row.value), row.tags or "", row.cond or "",
                  float(row.cond_value or 0), row.scale or "", float(row.step or 0), float(row.option or 0),
                  float(row.min_points or 0), float(row.cap or 0), float(row.per_point_from or 0))
        assert written.count(wanted) == 1, wanted


def _values(sheet) -> list[tuple]:
    """The header, then every other row in sorted order. SORTED SINCE 2026-11-25: a later slice's rows (the Berserker's
    states) now follow slice D's in the repository's sheet, and the script appends slice D's at the end, so the two
    sheets hold the same rows in a different order. Every cell is still compared."""
    rows = [tuple(cell.value for cell in row) for row in sheet.iter_rows()]
    return [rows[0], *sorted(rows[1:], key=repr)]


def _workbook_without_the_rows(tmp_path) -> pathlib.Path:
    """A copy of the workbook with slice D's rows and its `Per Point From` column taken out."""
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
    sheet.delete_cols(header.index(rows.PER_POINT_FROM) + 1, 1)
    book.save(copy)
    return copy


def test_the_row_script_reproduces_the_sheet_and_then_does_nothing(tmp_path):
    """Run on a workbook without them, the script appends the 48 rows and the column, and the sheet it writes holds
    the repository's sheet's rows value for value (`_values`); run again, it writes nothing."""
    copy = _workbook_without_the_rows(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 48, first
    assert f"added the column {rows.PER_POINT_FROM!r} to {rows.SHEET!r}" in first, first
    written = _values(openpyxl.load_workbook(copy)[rows.SHEET])
    assert written == _values(openpyxl.load_workbook(WORKBOOK)[rows.SHEET])
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_row_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Berserker_trunk_003", "Stat": "crit_chance", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.ReapplyError, match="somebody has written it since"):
        rows.apply(copy)
