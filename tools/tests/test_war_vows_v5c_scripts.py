"""The War vow options' last slice, V5c: Bolted Together's rows script.

2026-11-25. `tools/reapply_war_vows_v5c_workbook.py` writes two flat rows on the Saboteur's Third Oath, option 2:
`gadget_bolt_reach_metres` 4 and `bolted_more_percent_per_gadget` 10. No generator rule is added; they are ordinary
flat stats with no gameplay attribute, listed by `CataclysmPlayerClassStats.cpp`.

These tests hold the rows, that the workbook and the generated table hold them, that the script is idempotent and
refuses what somebody wrote since, and that the engine reads the two names. The game's half is
`game/Source/Cataclysm/Tests/CataclysmWarVowsV5cTests.cpp`.
"""
from __future__ import annotations

import csv
import pathlib
import re
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import reapply_war_vows_v5c_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
MINION_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmMinion.cpp"
CLASS_STATS_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmPlayerClassStats.cpp"

WANTED = [(2, "gadget_bolt_reach_metres", "flat", 4), (2, "bolted_more_percent_per_gadget", "flat", 10)]


def test_the_script_lists_bolted_togethers_two_rows():
    ((node, _, wanted),) = rows.NODES
    assert node == "Saboteur_capstone_100"
    assert [(row.option, row.stat, row.kind, row.value) for row in wanted] == WANTED


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = sorted((row["Stat"], row["ValueKind"], float(row["ValuePerPoint"]))
                         for row in csv.DictReader(handle)
                         if row["Node"] == "Saboteur_capstone_100"
                         and row["Stat"] in ("gadget_bolt_reach_metres", "bolted_more_percent_per_gadget"))
    assert written == [("bolted_more_percent_per_gadget", "flat", 10.0), ("gadget_bolt_reach_metres", "flat", 4.0)]


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


def test_the_script_writes_the_two_rows_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 2, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_its_option(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Saboteur_capstone_100", "Option": 2, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' last slice"):
        rows.apply(copy)
    assert copy.read_bytes() == before


def test_the_engine_reads_the_two_names():
    """The names the rows state are the ones `ACataclysmMinion` reads, and each is listed as having no attribute."""
    minion = MINION_CPP.read_text(encoding="utf-8")
    listed = CLASS_STATS_CPP.read_text(encoding="utf-8")
    for _, stat, _, _ in WANTED:
        assert re.search(r'ACataclysmMinion::\w+Stat = TEXT\("' + stat + r'"\)', minion), stat
        assert f'TEXT("{stat}")' in listed, stat
