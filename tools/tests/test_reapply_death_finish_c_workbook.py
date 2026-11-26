"""`tools/reapply_death_finish_c_workbook.py`, the Necromancer's detonation rows (the finishing of the Death Cataclysm,
slice C, 2026-11-26), checked on the committed workbook and on copies of it.

The committed workbook holds the rows; a copy with them taken out gets back exactly the committed sheet; a second run
writes nothing; `--check` writes nothing; a row somebody changed is refused; and every node it names is a Necromancer
node, a capstone row naming the option the tree gives that name.
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

import reapply_death_finish_c_workbook as reapply  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
NODES_CSV = REPO_ROOT / "game" / "Data" / "PassiveNodes.csv"
SHEET = reapply.EFFECTS_SHEET


def sheet_rows(path: pathlib.Path) -> list[tuple]:
    rows = [tuple(r) for r in openpyxl.load_workbook(path, read_only=True)[SHEET].iter_rows(values_only=True)]
    while rows and all(v is None for v in rows[-1]):
        rows.pop()
    return rows


def take_these_rows_out(path: pathlib.Path) -> int:
    """Delete every row of a (node, option) this script writes; answer how many went."""
    wanted = {(node.key, node.option) for node in reapply.NODES}
    book = openpyxl.load_workbook(path)
    sheet = book[SHEET]
    header = [c.value for c in sheet[1]]
    node_at, option_at = header.index("Node") + 1, header.index("Option") + 1
    doomed = [r for r in range(2, sheet.max_row + 1)
              if (sheet.cell(r, node_at).value, int(sheet.cell(r, option_at).value or 0)) in wanted]
    for row in reversed(doomed):
        sheet.delete_rows(row, 1)
    book.save(path)
    return len(doomed)


@pytest.fixture
def copy_of_the_workbook(tmp_path) -> pathlib.Path:
    target = tmp_path / "workbook.xlsx"
    shutil.copyfile(WORKBOOK, target)
    return target


def test_the_committed_workbook_holds_every_detonation_row():
    plan = reapply.base.plan_effects(openpyxl.load_workbook(WORKBOOK)[SHEET], reapply.NODES)
    assert plan.changed == 0, ("docs/All_Things_Cataclysm.xlsx does not hold the detonation rows; run "
                               "python tools/reapply_death_finish_c_workbook.py\n" + "\n".join(plan.report))


def test_the_rows_taken_out_are_put_back_as_the_committed_sheet(copy_of_the_workbook):
    taken = take_these_rows_out(copy_of_the_workbook)
    assert taken == sum(len(node.rows) for node in reapply.NODES) == 22
    assert sheet_rows(copy_of_the_workbook) != sheet_rows(WORKBOOK)
    report = reapply.apply(copy_of_the_workbook)
    assert report[-1].startswith(f"wrote {len(reapply.NODES)} change(s)"), report
    assert sheet_rows(copy_of_the_workbook) == sheet_rows(WORKBOOK)


def test_a_second_run_writes_nothing(copy_of_the_workbook):
    before = copy_of_the_workbook.read_bytes()
    assert reapply.apply(copy_of_the_workbook) == [
        "every detonation row is already as this script writes it; nothing written"]
    assert copy_of_the_workbook.read_bytes() == before


def test_check_writes_nothing_and_says_what_would_change(copy_of_the_workbook):
    take_these_rows_out(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()
    assert reapply.main(["--workbook", str(copy_of_the_workbook), "--check"]) == 1
    assert copy_of_the_workbook.read_bytes() == before


def test_a_row_somebody_changed_is_refused_and_nothing_is_written(copy_of_the_workbook):
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[SHEET]
    header = [c.value for c in sheet[1]]
    node_at, value_at = header.index("Node") + 1, header.index("Value Per Point") + 1
    row = next(r for r in range(2, sheet.max_row + 1)
               if sheet.cell(r, node_at).value == "Necromancer_keystone_ch_kC")
    sheet.cell(row, value_at).value = 3
    book.save(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()
    with pytest.raises(reapply.ReapplyError, match="Necromancer_keystone_ch_kC"):
        reapply.apply(copy_of_the_workbook)
    assert copy_of_the_workbook.read_bytes() == before


def test_every_node_is_a_necromancer_node_and_each_option_is_the_one_named():
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        nodes = {row["Name"]: row for row in csv.DictReader(handle)}
    for node in reapply.NODES:
        assert node.key.startswith("Necromancer_"), node.key
        assert node.key in nodes, node.key
        if node.option:
            assert nodes[node.key][f"Option{node.option}Name"] == node.label, node.key
        else:
            assert nodes[node.key]["NodeName"] == node.label, node.key


def test_every_charnel_node_holds_a_row_now():
    """THE CHARNEL LIMB IS WHOLE: every `Necromancer_*_ch_*` node has a row, in this script or the Death trees' own.
    THE CONTROL is that the limb has the thirteen nodes the tree draws."""
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        charnel = [row["Name"] for row in csv.DictReader(handle)
                   if row["Name"].startswith("Necromancer_") and "_ch_" in row["Name"]]
    assert len(charnel) == 13
    effects_csv = REPO_ROOT / "game" / "Data" / "PassiveEffects.csv"
    with effects_csv.open(encoding="utf-8", newline="") as handle:
        with_rows = {row["Node"] for row in csv.DictReader(handle)}
    assert [name for name in charnel if name not in with_rows] == []
