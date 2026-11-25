"""`tools/reapply_demonic_tree_rework_workbook.py`, the Demonic tree rework's
Passive Effects rows, checked on the committed workbook and on copies of it.

THE WORKBOOK IN `docs/` HOLDS THE REWORK, and the first test says so: if a branch
takes `development`'s workbook without re-running the script, the rows are
missing and this fails before anything else. The rest put a copy BACK to the
rows the rework was approved against -- the script's own changes, read the other
way -- and require the script to turn that back into the committed sheet.
"""

from __future__ import annotations

import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import reapply_demonic_tree_rework_workbook as reapply  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def sheet_rows(path: pathlib.Path) -> list[tuple]:
    sheet = openpyxl.load_workbook(path, read_only=True)[reapply.SHEET]
    rows = [tuple(r) for r in sheet.iter_rows(values_only=True)]
    while rows and all(v is None for v in rows[-1]):
        rows.pop()
    return rows


def reverse(changes=reapply.CHANGES) -> tuple:
    """Every change the other way: from the rows it writes to the first state it
    was approved against."""
    return tuple(reapply.Change(c.node, c.option, (c.after,), c.before[0], None)
                 for c in changes)


def put_back(path: pathlib.Path) -> None:
    book = openpyxl.load_workbook(path)
    sheet = book[reapply.SHEET]
    plan = reapply.plan(sheet, reverse())
    assert plan.changed == len(reapply.CHANGES), (
        "the committed workbook does not hold every change, so it cannot be put "
        "back to the rows the rework was approved against")
    reapply.write(sheet, plan)
    book.save(path)


@pytest.fixture
def copy_of_the_workbook(tmp_path) -> pathlib.Path:
    target = tmp_path / "workbook.xlsx"
    shutil.copyfile(WORKBOOK, target)
    return target


def test_the_committed_workbook_holds_every_change():
    book = openpyxl.load_workbook(WORKBOOK, read_only=False)
    plan = reapply.plan(book[reapply.SHEET])
    assert plan.changed == 0 and not plan.add_cap_column, (
        "docs/All_Things_Cataclysm.xlsx does not hold the Demonic tree rework's rows. "
        "If development's workbook was taken in a merge, run "
        "python tools/reapply_demonic_tree_rework_workbook.py\n" + "\n".join(plan.report))


def test_the_rows_put_back_are_turned_into_the_committed_sheet(copy_of_the_workbook):
    put_back(copy_of_the_workbook)
    assert sheet_rows(copy_of_the_workbook) != sheet_rows(WORKBOOK)

    report = reapply.apply(copy_of_the_workbook)
    assert report[-1].startswith(f"wrote {len(reapply.CHANGES)} change(s)"), report
    assert sheet_rows(copy_of_the_workbook) == sheet_rows(WORKBOOK)


def test_a_second_run_writes_nothing(copy_of_the_workbook):
    before = copy_of_the_workbook.read_bytes()
    assert reapply.apply(copy_of_the_workbook) == [
        "every row is already as the rework writes it; nothing written"]
    assert copy_of_the_workbook.read_bytes() == before


def test_check_writes_nothing_and_says_what_would_change(copy_of_the_workbook):
    put_back(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()
    assert reapply.main(["--workbook", str(copy_of_the_workbook), "--check"]) == 1
    assert copy_of_the_workbook.read_bytes() == before


def test_no_other_sheet_changes(copy_of_the_workbook):
    put_back(copy_of_the_workbook)
    reapply.apply(copy_of_the_workbook)
    ours = openpyxl.load_workbook(copy_of_the_workbook, read_only=True)
    theirs = openpyxl.load_workbook(WORKBOOK, read_only=True)
    assert ours.sheetnames == theirs.sheetnames
    for name in ours.sheetnames:
        if name == reapply.SHEET:
            continue
        assert list(ours[name].iter_rows(values_only=True)) == \
            list(theirs[name].iter_rows(values_only=True)), name


def test_a_row_somebody_changed_since_is_refused_and_nothing_is_written(copy_of_the_workbook):
    put_back(copy_of_the_workbook)
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[reapply.SHEET]
    header = [c.value for c in sheet[1]]
    node, stat, value = (header.index(n) + 1 for n in ("Node", "Stat", "Value Per Point"))
    for row in range(2, sheet.max_row + 1):
        if sheet.cell(row, node).value == "Ravager_basic_a_b0":
            assert sheet.cell(row, stat).value == "damage_reduction"
            sheet.cell(row, value).value = 9
    book.save(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()

    with pytest.raises(reapply.ReapplyError, match="Ravager_basic_a_b0"):
        reapply.apply(copy_of_the_workbook)
    assert copy_of_the_workbook.read_bytes() == before
    assert reapply.main(["--workbook", str(copy_of_the_workbook), "--no-generate"]) == 2


def test_the_two_order_fix_positions_accept_either_starting_state(copy_of_the_workbook):
    """Blood Price and Deeper Cuts trade places in the tree; the rows may be found
    unswapped (development today) or already swapped by another branch."""
    put_back(copy_of_the_workbook)
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[reapply.SHEET]
    swap = (reapply.Change("Masochist_basic_spine_007", 0, (reapply.BLOOD_PRICE_ROWS,),
                           reapply.DEEPER_CUTS_ROWS),
            reapply.Change("Masochist_basic_bt_stem1", 0, (reapply.DEEPER_CUTS_ROWS,),
                           reapply.BLOOD_PRICE_ROWS))
    reapply.write(sheet, reapply.plan(sheet, swap))
    book.save(copy_of_the_workbook)

    reapply.apply(copy_of_the_workbook)
    assert sheet_rows(copy_of_the_workbook) == sheet_rows(WORKBOOK)


def test_the_cap_column_is_added_when_missing(copy_of_the_workbook):
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[reapply.SHEET]
    header = [c.value for c in sheet[1]]
    assert reapply.CAP_COLUMN in header
    plan = reapply.plan(sheet, reverse())
    reapply.write(sheet, plan)
    sheet.delete_cols(header.index(reapply.CAP_COLUMN) + 1, 1)
    book.save(copy_of_the_workbook)

    reapply.apply(copy_of_the_workbook)
    # COMPARED BY COLUMN NAME, NOT POSITION, AND WITHOUT THE DEATH ROWS. The
    # script adds the cap column after the sheet's last one, which was its place
    # until the Death trees' slice C1 (2026-11-25) added the passive row port's
    # fourteen columns after it; put back, it now lands after those, and the
    # generator reads every column by its header. And deleting the column takes
    # the Death rows' caps with it (Kill Chain's 10 and others), which
    # `tools/reapply_death_trees_workbook.py` writes and this script does not.
    assert sheet_rows_by_name(copy_of_the_workbook) == sheet_rows_by_name(WORKBOOK)


#: The Death trees' row prefixes, whose rows `tools/reapply_death_trees_workbook.py` writes.
DEATH_PREFIXES = ("SoulCollector_", "Necromancer_", "Shadow_")


def sheet_rows_by_name(path: pathlib.Path) -> list[dict]:
    """Each row but the Death trees' as its cells keyed by header, the empty ones left out."""
    rows = sheet_rows(path)
    header = rows[0]
    keyed = [{name: value for name, value in zip(header, row, strict=False) if value is not None}
             for row in rows[1:]]
    return [row for row in keyed if not str(row.get("Node") or "").startswith(DEATH_PREFIXES)]


def test_every_for_every_n_points_series_is_whole_and_counts_from_n():
    """The steps helper: one row at each multiple of N up to the node's points."""
    rows = reapply.steps("life_leech", "flat", 1, every=2, most=10)
    assert [r.min_points for r in rows] == [2, 4, 6, 8, 10]
    assert {r.value for r in rows} == {1}
    assert [r.min_points for r in reapply.steps("fervour_per_cast", "flat", 1, every=3,
                                                most=12)] == [3, 6, 9, 12]


def test_no_change_is_listed_twice_and_every_change_alters_something():
    keys = [(c.node, c.option) for c in reapply.CHANGES]
    assert len(keys) == len(set(keys))
    for change in reapply.CHANGES:
        for state in change.before:
            assert reapply._multiset(state) != reapply._multiset(change.after), change.node
