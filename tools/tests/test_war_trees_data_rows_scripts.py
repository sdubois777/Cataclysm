"""The two scripts of the War trees' data rows (2026-11-27), and what they wrote.

`tools/reapply_war_trees_data_rows_workbook.py` writes 75 rows on 43 War nodes with slice D's machinery: 39 on the 23
data-only nodes and 36 on the 20 nodes engine slice E1 made writable. `tools/apply_war_trees_sentences_2026_11_27.py`
writes five sentences into the Berserker and Bulwark tree files: R8, R9, R10, R13, and Outnumbered's 4 metres.
These tests hold that the workbook, the generated table and the tree file hold what the scripts write, that each script
is idempotent, and that each refuses rather than overwrites something written since.
"""
from __future__ import annotations

import csv
import json
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_war_trees_sentences_2026_11_27 as sentences  # noqa: E402
import reapply_war_trees_data_rows_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DATA = REPO_ROOT / "game" / "Data"


def _all_rows():
    return [(node, row) for node, _, wanted in rows.NODES for row in wanted]


def test_there_are_seventy_five_rows_on_forty_three_nodes():
    """The name is the first slice's; since engine slices E2, E3 and the riders (2026-11-27) it is 174 rows on 90,
    since window 2's Saboteur nodes (E4, 2026-11-28) 224 rows on 117, and since the War weapon skills' slice WS1 (issue
    #2972, 2026-11-28) 225 on 118, with Colossus."""
    assert len(_all_rows()) == 225
    assert len({node for node, _, _ in rows.NODES}) == len(rows.NODES) == 118
    groups = (rows.DATA_NODES, rows.E1_NODES, rows.E2_NODES, rows.E3_NODES, rows.E4_NODES, rows.WS1_NODES)
    assert tuple(len(group) for group in groups) == (23, 20, 26, 21, 27, 1)
    assert [sum(len(wanted) for _, _, wanted in group) for group in groups] == [39, 36, 57, 42, 50, 1]


def test_the_nodes_split_by_tree():
    data = [node.split("_")[0] for node, _, _ in rows.DATA_NODES]
    engine = [node.split("_")[0] for node, _, _ in rows.E1_NODES]
    assert (data.count("Berserker"), data.count("Bulwark"), data.count("Saboteur")) == (6, 12, 5)
    assert (engine.count("Berserker"), engine.count("Bulwark"), engine.count("Saboteur")) == (10, 10, 0)


def test_the_six_nodes_moved_to_the_engine_are_written_with_it_and_not_as_data():
    """War Cry and Rapid Fire needed the full-pool attack fact; four Bulwark nodes needed an asker the generator
    lacked. Each is written whole with engine slice E1, so none may appear half written among the data rows."""
    moved = {"Berserker_trunk_010", "Berserker_dw_009", "Bulwark_basic_ic_stem_005", "Bulwark_basic_ic_t3_002",
             "Bulwark_keystone_ic_t2", "Bulwark_basic_tw_final_001"}
    assert moved.isdisjoint(node for node, _, _ in rows.DATA_NODES)
    assert moved <= {node for node, _, _ in rows.E1_NODES}


def test_vital_strike_is_at_full_health_and_unbroken_at_fifty_fervour():
    """Two values the text check reads only as words: Vital Strike's "While your health is full" is 100, and Unbroken's
    "above 50 Fervour" is 50 points."""
    wanted = {node: rows_ for node, _, rows_ in rows.E1_NODES}
    (_, crit_fervour) = wanted["Berserker_leech_003"]
    assert (crit_fervour.cond, crit_fervour.cond_value) == ("health_at_or_above", 100)
    (unbroken,) = wanted["Bulwark_keystone_trunk_001"]
    assert (unbroken.cond, unbroken.cond_value) == ("class_resource_points_at_least", 50)


def test_each_label_is_the_nodes_own_name():
    with open(DATA / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        names = {row["Name"]: row["NodeName"] for row in csv.DictReader(handle)}
    assert [(node, label) for node, label, _ in rows.NODES if names.get(node) != label] == []


def test_every_node_had_no_row_before_this_script():
    """The plan's group was nodes with NO effect row; a node another slice already wrote would be refused by the
    machinery, and this says why up front."""
    with open(DATA / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        effects = list(csv.DictReader(handle))
    for node, _, wanted in rows.NODES:
        assert sum(1 for row in effects if row["Node"] == node) == len(wanted), node


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_every_row_once():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(DATA / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Condition"], number(row["ConditionValue"]), row["Scale"], number(row["ScaleMaxSteps"]),
                    row["Action"], row["ActionEvent"], number(row["StackSeconds"]), number(row["MinPoints"]),
                    row["Ailment"], row["Condition2"], number(row["MaxPoints"]))
                   for row in csv.DictReader(handle)]
    for node, row in _all_rows():
        # WITH THE CONDITION VALUES, THE SECOND CONDITION AND THE UPPER LIMIT SINCE E2 (2026-11-27): The Inevitable's five
        # rows differ only in their condition value, and Open Wound's only in their point limits.
        wanted = (node, row.stat or "", row.kind, float(row.value), row.tags or "", row.cond or "",
                  float(row.cond_value or 0), row.scale or "", float(row.cap or 0), row.action or "",
                  row.action_event or "", float(row.stack_seconds or 0), float(row.min_points or 0), row.ailment or "",
                  row.cond2 or "", float(row.max_points or 0))
        assert written.count(wanted) == 1, wanted


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


def test_the_script_writes_the_rows_once(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 225, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Bulwark_basic_tw_t1_002", "Stat": "dot_damage", "Value Kind": "increased",
                   "Value Per Point": 6}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="the War trees' data rows"):
        rows.apply(copy)


# ---- the sentences ----

def _sentence_of(folder: pathlib.Path, tree: str, node: str) -> str:
    data = json.loads((folder / f"{tree}_Class_Tree_Final.json").read_text(encoding="utf-8"))
    (found,) = [each for each in data["nodes"] if each["id"] == node]
    return found["data"]["description"]


def test_the_tree_files_hold_the_ruled_sentences():
    for each in sentences.SENTENCES:
        assert _sentence_of(sentences.DOCS, each.tree, each.node) == each.after, each.ruling
    assert sentences.apply(check_only=True)[-1] == "every sentence is already written; nothing written"


def test_the_ruled_sentences_are_the_decisions_entrys_words():
    """Each sentence is quoted in `docs/DECISIONS.md` -- R8, R9, R10 and R13 from the rulings pass, Outnumbered's from
    the War trees entry -- so a sentence typed differently here would be a sentence nobody ruled."""
    decisions = (REPO_ROOT / "docs" / "DECISIONS.md").read_text(encoding="utf-8")
    for each in sentences.SENTENCES:
        assert f'"{each.after}"' in decisions, each.ruling


def test_the_generated_node_table_holds_the_ruled_sentences():
    with open(DATA / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        described = {row["Name"]: row["Description"] for row in csv.DictReader(handle)}
    for each in sentences.SENTENCES:
        assert described[f"{each.tree}_{each.node}"] == each.after, each.ruling


def _tree_files_before(tmp_path) -> pathlib.Path:
    folder = tmp_path / "docs"
    folder.mkdir()
    for tree in {each.tree for each in sentences.SENTENCES}:
        path = sentences.DOCS / f"{tree}_Class_Tree_Final.json"
        text = path.read_bytes().decode("utf-8")
        for each in sentences.SENTENCES:
            if each.tree == tree:
                text = text.replace(json.dumps(each.after)[1:-1], json.dumps(each.before)[1:-1])
        (folder / path.name).write_bytes(text.encode("utf-8"))
    return folder


def test_the_sentence_script_writes_each_once_and_keeps_the_rest_of_the_file(tmp_path):
    folder = _tree_files_before(tmp_path)
    before = (folder / "Bulwark_Class_Tree_Final.json").read_bytes()
    first = sentences.apply(folder)
    # 26 SINCE THE RIDERS' SENTENCES (2026-11-27); 37 SINCE WINDOW 2'S ELEVEN SABOTEUR SENTENCES (2026-11-28).
    assert first[-1] == "written" and sum(1 for line in first if line.endswith(": written")) == 37, first
    after = (folder / "Bulwark_Class_Tree_Final.json").read_bytes()
    assert after == (sentences.DOCS / "Bulwark_Class_Tree_Final.json").read_bytes() != before
    assert sentences.apply(folder)[-1] == "every sentence is already written; nothing written"


def test_the_sentence_script_refuses_a_sentence_written_since(tmp_path):
    folder = _tree_files_before(tmp_path)
    path = folder / "Bulwark_Class_Tree_Final.json"
    each = sentences.SENTENCES[1]
    path.write_bytes(path.read_bytes().decode("utf-8").replace(
        json.dumps(each.before)[1:-1], "+6% bleed damage per point.").encode("utf-8"))
    untouched = path.read_bytes()
    with pytest.raises(sentences.SentenceError, match="somebody has written it since"):
        sentences.apply(folder)
    assert path.read_bytes() == untouched
