"""The War vow options' first slice: its sentences script, its rows script, and what they wrote.

2026-11-25. `tools/apply_war_vow_sentences_2026_11_25.py` writes R14, R17, R18, two ruled replacements and the
Saboteur's twelve approved options into the tree files; `tools/reapply_war_vows_v1_workbook.py` writes nine option rows
and the passive row port's last eight columns, so the War line's Passive Effects sheet carries the fourteen the Death
line's does. These tests hold the rulings' sentences and rows, that the files hold them, that each script is
idempotent, and that each refuses what somebody wrote since.
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

import apply_war_vow_sentences_2026_11_25 as sentences  # noqa: E402
import reapply_war_vows_v1_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DOCS = REPO_ROOT / "docs"
#: 9 until the second slice, V2a, took over Unstoppable's two rows and Ragnarok's drain row the same day
#: (`tools/reapply_war_vows_v2a_workbook.py`, whose tests now hold Unstoppable's conditions).
#: 6 until the third slice, V3a, took over Eternal Hunger's leech row and Wrath Incarnate's armour row the same day
#: (`tools/reapply_war_vows_v3a_workbook.py`, whose tests now hold both).
#: 4 until V3b took over Colossus Eternal's healing row the same day (`tools/reapply_war_vows_v3b_workbook.py`).
ROW_COUNT = 3


# ---------------------------------------------------------------------------
# The sentences
# ---------------------------------------------------------------------------

def test_the_five_rewordings_are_the_ruled_ones():
    by_option = {fix.option: fix for fix in sentences.OPTION_REWORDS}
    assert set(by_option) == {"Eternal Hunger", "Colossus Eternal", "Wrath Incarnate", "Shield Oath",
                              "Bloodied but Unbroken"}
    assert "The Unbreakable Wall" not in by_option, "the owner has not ruled on it"
    assert by_option["Shield Oath"].after == "Blocking an attack restores 2% of your Maximum HP."
    assert by_option["Bloodied but Unbroken"].after == (
        "Your retaliation damage is increased by 1% for every 1% of your Maximum HP you are missing.")
    assert by_option["Wrath Incarnate"].after.startswith(
        "Your retaliation damage is increased by 1% for every 100 Armor you have.")


def test_the_saboteurs_twelve_options_fill_its_four_oaths():
    names = [name for options in sentences.SABOTEUR_OPTIONS.values() for name, _ in options]
    assert list(sentences.SABOTEUR_OPTIONS) == ["capstone_25", "capstone_50", "capstone_100", "capstone_200"]
    assert names == ["Baited", "Walking Arsenal", "Lightning Rod", "Trail of Wire", "Scrap Mines", "Cat's Cradle",
                     "Creeping Mines", "Bolted Together", "Counterfire", "Where They Fall", "Magnum Opus", "Crossfire"]


def test_the_tree_files_hold_the_sentences():
    assert sentences.main(["--check", str(DOCS)]) == 0


def _copy_trees(tmp_path) -> pathlib.Path:
    for tree in ("Berserker", "Bulwark", "Saboteur"):
        shutil.copy(DOCS / f"{tree}_Class_Tree_Final.json", tmp_path / f"{tree}_Class_Tree_Final.json")
    return tmp_path


def _put_back_the_old_sentences(folder: pathlib.Path) -> None:
    for fix in sentences.OPTION_REWORDS:
        path = folder / f"{fix.tree}_Class_Tree_Final.json"
        text = path.read_bytes().decode("utf-8")
        new, old = json.dumps(fix.after)[1:-1], json.dumps(fix.before)[1:-1]
        assert text.count(new) == 1, fix.option
        path.write_bytes(text.replace(new, old).encode("utf-8"))
    saboteur = folder / "Saboteur_Class_Tree_Final.json"
    data = json.loads(saboteur.read_text(encoding="utf-8"))
    for node in data["nodes"]:
        if node["id"] in sentences.SABOTEUR_OPTIONS:
            node["data"]["options"] = []
    form = sentences.order.format_of(saboteur.read_bytes(), json.loads(saboteur.read_text(encoding="utf-8")))
    saboteur.write_bytes(form.write(data))


def test_the_sentences_script_writes_once_and_byte_for_byte(tmp_path):
    folder = _copy_trees(tmp_path)
    _put_back_the_old_sentences(folder)
    assert sentences.main(["--check", str(folder)]) == 1
    assert sentences.main([str(folder)]) == 0
    for tree in ("Berserker", "Bulwark", "Saboteur"):
        name = f"{tree}_Class_Tree_Final.json"
        assert (folder / name).read_bytes() == (DOCS / name).read_bytes(), name
    assert sentences.main(["--check", str(folder)]) == 0


def test_the_sentences_script_refuses_an_option_somebody_reworded(tmp_path):
    folder = _copy_trees(tmp_path)
    fix = sentences.OPTION_REWORDS[0]
    path = folder / f"{fix.tree}_Class_Tree_Final.json"
    text = path.read_bytes().decode("utf-8")
    path.write_bytes(text.replace(json.dumps(fix.after)[1:-1], "Somebody wrote this since.").encode("utf-8"))
    assert sentences.main([str(folder)]) == 2


def test_the_sentences_script_refuses_saboteur_options_it_did_not_write(tmp_path):
    folder = _copy_trees(tmp_path)
    path = folder / "Saboteur_Class_Tree_Final.json"
    text = path.read_bytes().decode("utf-8")
    path.write_bytes(text.replace('"name": "Baited"', '"name": "Somebody Else"').encode("utf-8"))
    assert sentences.main([str(folder)]) == 2


# ---------------------------------------------------------------------------
# The rows and the columns
# ---------------------------------------------------------------------------

def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def test_the_script_writes_three_rows_on_three_options():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    options = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert len(options) == 3
    assert ("Bulwark_capstone_200", 2) not in options, "Colossus Eternal's rows are V3b's script's"
    assert ("Berserker_capstone_50", 1) not in options and ("Berserker_capstone_200", 1) not in options, (
        "Unstoppable's and Ragnarok's rows are the second slice's script's")
    assert ("Berserker_capstone_200", 3) not in options and ("Bulwark_capstone_200", 3) not in options, (
        "Eternal Hunger's and Wrath Incarnate's rows are the third slice's script's")
    assert rows.base.PORT_EIGHT == ("Fraction Of", "Scale Offset", "Every Seconds", "Every Nth", "Trigger Cooldown",
                                    "Event Value", "Ailment", "Damage Share")


def test_shield_oath_and_flesh_oath_restore_a_share_of_health():
    shield, flesh = _rows_of("Bulwark_capstone_25")
    assert (shield.option, shield.action, shield.action_event, shield.fraction_of, shield.value) == (
        1, "health", "block", "maximum", 2)
    assert (flesh.option, flesh.action, flesh.action_event, flesh.fraction_of, flesh.every_seconds, flesh.value) == (
        2, "health", "every_seconds", "current", 1, 1)


def test_the_workbook_holds_the_rows_and_the_columns():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report
    header = [cell.value for cell in openpyxl.load_workbook(WORKBOOK, read_only=True)[rows.base.SHEET][1]]
    assert set(rows.base.PORT_EIGHT) <= set(header)


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), number(row["Option"]),
                    row["Action"], row["ActionEvent"], row["FractionOf"], number(row["EverySeconds"]))
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", row.kind, float(row.value), float(row.option or 0), row.action or "",
                   row.action_event or "", row.fraction_of or "", float(row.every_seconds or 0))
            assert written.count(key) == 1, key


def _workbook_without_ours(tmp_path, drop_columns: bool) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column, option_column = header.index("Node") + 1, header.index("Option") + 1
    ours = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    for index in range(sheet.max_row, 1, -1):
        option = sheet.cell(index, option_column).value
        if (sheet.cell(index, node_column).value, int(option) if option else None) in ours:
            sheet.delete_rows(index, 1)
    if drop_columns:
        for name in reversed(rows.base.PORT_EIGHT):
            sheet.delete_cols(header.index(name) + 1, 1)
    book.save(copy)
    return copy


def test_the_row_script_appends_the_rows_and_the_columns_once(tmp_path):
    copy = _workbook_without_ours(tmp_path, drop_columns=True)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    assert sum(1 for line in first if line.startswith("added the column")) == len(rows.base.PORT_EIGHT), first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_row_script_adds_the_columns_when_every_row_is_already_written(tmp_path):
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    # ONLY COLUMNS NO ROW HOLDS A VALUE IN, so taking them away loses nothing.
    empty = [name for name in rows.base.PORT_EIGHT
             if not any(sheet.cell(index, header.index(name) + 1).value is not None
                        for index in range(2, sheet.max_row + 1))]
    assert empty, "every port column holds a value somewhere"
    for name in sorted(empty, key=header.index, reverse=True):
        sheet.delete_cols(header.index(name) + 1, 1)
    book.save(copy)
    first = rows.apply(copy)
    assert not any(": + " in line for line in first), first
    assert sum(1 for line in first if line.startswith("added the column")) == len(empty), first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_row_script_refuses_a_row_somebody_wrote_on_one_of_its_options(tmp_path):
    copy = _workbook_without_ours(tmp_path, drop_columns=False)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Bulwark_capstone_100", "Option": 2, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' first slice writes"):
        rows.apply(copy)
    assert copy.read_bytes() == before


def test_a_column_the_machinery_does_not_add_is_refused(tmp_path):
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    with pytest.raises(rows.base.ReapplyError, match="are not columns this script adds"):
        rows.base.apply(copy, check_only=True, nodes=rows.NODES, label=rows.LABEL, columns=("Some Column",))
