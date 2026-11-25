"""The scripts for the Saboteur's trap timing, and what they wrote.

2026-11-25, the C1 plan, accepted as written by the coordinating session. `tools/reapply_war_saboteur_trap_timing_workbook.py`
writes every row of six nodes, three of which it took over from `tools/reapply_war_saboteur_rows_workbook.py`; and
`tools/apply_saboteur_rewordings_2026_11_25.py` rewords the four sentences the game cannot have. These tests hold the
shapes the rulings give the rows, the hand-over, that the workbook, the generated table and the tree file hold what
was written, that each script is idempotent, and that each refuses what somebody wrote since.
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

import apply_saboteur_rewordings_2026_11_25 as reword  # noqa: E402
import reapply_war_saboteur_rows_workbook as earlier  # noqa: E402
import reapply_war_saboteur_trap_timing_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
TREE = REPO_ROOT / "docs" / "Saboteur_Class_Tree_Final.json"
ROW_COUNT = 11
#: The rows the three taken-over nodes held from the earlier script.
HELD_BEFORE = 3


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def test_the_script_writes_eleven_rows_on_six_nodes():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    assert len({node for node, _, _ in rows.NODES}) == 6


def test_the_three_nodes_taken_over_are_extended_and_no_longer_the_earlier_scripts():
    ours = {node for node, _, _ in rows.NODES}
    theirs = {node for node, _, _ in earlier.NODES}
    assert rows.EXTENDING <= ours
    assert rows.EXTENDING == {"Saboteur_basic_trunk_001", "Saboteur_basic_trap_stem_002", "Saboteur_basic_trap_qty_001"}
    assert not ours & theirs, ours & theirs


def test_hair_trigger_arms_instantly_from_five_points_and_hair_wire_arms_faster_from_four():
    instant = [row for row in _rows_of("Saboteur_basic_trunk_001") if row.stat == "traps_arm_instantly"]
    faster = [row for row in _rows_of("Saboteur_basic_trap_stem_002") if row.stat == "trap_arming_speed"]
    assert [(row.kind, row.value, row.min_points) for row in instant] == [("flat", 1, 5)]
    assert [(row.kind, row.value, row.min_points) for row in faster] == [("increased", 20, 4)]


def test_the_reworded_speeds_are_arming_speed_and_gadget_attack_speed():
    (casing,) = _rows_of("Saboteur_basic_trap_stem_003")
    assert (casing.stat, casing.kind, casing.value) == ("trap_arming_seconds_less_per_other_trap", "flat", 0.1)
    assert ("trap_arming_speed", "increased", 10, None) in {
        (row.stat, row.kind, row.value, row.min_points) for row in _rows_of("Saboteur_basic_trap_qty_001")}
    (engineer,) = _rows_of("Saboteur_basic_gadget_stem_001")
    assert (engineer.stat, engineer.kind, engineer.value, engineer.tags) == (
        "minion_attack_speed", "increased", 12, "Type.Gadget")


def test_delayed_fuse_is_trap_damage_an_extra_second_and_a_flag():
    shapes = {(row.stat, row.kind, row.value, row.tags, row.min_points) for row in _rows_of("Saboteur_basic_trap_ql_003")}
    assert shapes == {("minion_damage", "increased", 15, "Type.Trap", None),
                      ("trap_arming_seconds_added", "flat", 1, None, 1),
                      ("traps_not_set_off_while_arming", "flat", 1, None, 1)}


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    number(row["MinPoints"])) for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat, row.kind, float(row.value), row.tags or "", float(row.min_points or 0))
            assert written.count(key) == 1, key


def _workbook_without(tmp_path, keep) -> pathlib.Path:
    """A copy of the workbook with this script's nodes' rows deleted, except those `keep(stat)` keeps."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    stat_column = header.index("Stat") + 1
    ours = {node for node, _, _ in rows.NODES}
    for index in range(sheet.max_row, 1, -1):
        node = sheet.cell(index, node_column).value
        if node in ours and not keep(node, sheet.cell(index, stat_column).value):
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def _held_before(node: str, stat: str) -> bool:
    """Whether a row is one the earlier script wrote: the radius on two nodes, and Minefield's reserve."""
    return (node, stat) in {("Saboteur_basic_trunk_001", "trap_trigger_radius"),
                            ("Saboteur_basic_trap_stem_002", "trap_trigger_radius"),
                            ("Saboteur_basic_trap_qty_001", "minion_reserve_percent_less")}


def test_the_row_script_appends_the_rows_once(tmp_path):
    copy = _workbook_without(tmp_path, lambda node, stat: False)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    second = rows.apply(copy)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_row_script_keeps_the_rows_the_earlier_script_wrote_and_appends_the_rest(tmp_path):
    copy = _workbook_without(tmp_path, _held_before)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT - HELD_BEFORE, first
    assert sum(1 for line in first if "the rest are appended" in line) == len(rows.EXTENDING), first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_row_script_refuses_a_row_somebody_wrote_on_a_node_it_extends(tmp_path):
    copy = _workbook_without(tmp_path, _held_before)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_basic_trunk_001", "Stat": "evasion", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the Saboteur's trap timing writes") as refusal:
        rows.apply(copy)
    # REFUSED BEFORE ANYTHING WAS SAVED: the read-back after a save quotes the same words, so the words alone would
    # pass a script that appended beside the foreign row and then failed its read-back (found by the guard proof of
    # 2026-11-25, which this assertion is for).
    assert "WAS SAVED" not in str(refusal.value), refusal.value
    assert copy.read_bytes() == before


def test_a_sentence_reworded_here_that_an_earlier_fix_wrote_is_that_fixs_later_sentence():
    """The order fixes of 2026-11-24 wrote Minefield's sentence before this script reworded it; the earlier script
    must read the new one as fixed, or rerunning it over `docs/` refuses."""
    import apply_passive_order_fixes_2026_11_24 as fixes

    earlier_fixes = {fix.node: fix for fix in fixes.SABOTEUR_CAPS}
    overlapping = [fix for fix in reword.REWORDS if fix.node in earlier_fixes]
    assert [fix.node for fix in overlapping] == ["Minefield"]
    for fix in overlapping:
        assert fix.before == earlier_fixes[fix.node].after
        assert fix.after in earlier_fixes[fix.node].later


def test_the_tree_file_holds_the_reworded_sentences():
    data = json.loads(TREE.read_text(encoding="utf-8"))
    for fix in reword.REWORDS:
        assert reword.order.node_named(data, fix.node, reword.TREE)["data"]["description"] == fix.after, fix.node
    assert reword.main(["--check", str(TREE)]) == 0


def test_the_reword_script_rewords_the_ruled_sentences_once(tmp_path):
    copy = tmp_path / TREE.name
    raw = TREE.read_bytes()
    text = raw.decode("utf-8")
    for fix in reword.REWORDS:
        assert text.count(json.dumps(fix.after)[1:-1]) == 1, fix.node
        text = text.replace(json.dumps(fix.after)[1:-1], json.dumps(fix.before)[1:-1])
    copy.write_bytes(text.encode("utf-8"))
    assert reword.main(["--check", str(copy)]) == 1
    assert reword.main([str(copy)]) == 0
    assert copy.read_bytes() == raw
    assert reword.main(["--check", str(copy)]) == 0


def test_the_reword_script_refuses_a_sentence_somebody_changed(tmp_path):
    copy = tmp_path / TREE.name
    fix = reword.REWORDS[0]
    text = TREE.read_bytes().decode("utf-8").replace(json.dumps(fix.after)[1:-1], "Somebody wrote this since.")
    copy.write_bytes(text.encode("utf-8"))
    assert reword.main([str(copy)]) == 2
