"""`tools/reapply_death_trees_workbook.py`, the Death trees' rows and class lines,
checked on the committed workbook and on copies of it.

THE WORKBOOK IN `docs/` HOLDS THE DEATH ROWS, and the first test says so: if a
branch takes another branch's workbook in a merge without re-running the script,
the rows are missing and this fails before anything else. The rest take the
Death rows OUT of a copy -- the state every workbook was in before 2026-11-24 --
and require the script to put back exactly the committed sheets.
"""

from __future__ import annotations

import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import reapply_death_trees_workbook as reapply  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
SHEETS = (reapply.EFFECTS_SHEET, reapply.CLASS_SHEET)


def sheet_rows(path: pathlib.Path, title: str) -> list[tuple]:
    sheet = openpyxl.load_workbook(path, read_only=True)[title]
    rows = [tuple(r) for r in sheet.iter_rows(values_only=True)]
    while rows and all(v is None for v in rows[-1]):
        rows.pop()
    return rows


def take_the_death_rows_out(path: pathlib.Path) -> None:
    """Delete every Death node's effect row and every Death class's stat line."""
    book = openpyxl.load_workbook(path)
    for title, column, belongs in (
            (reapply.EFFECTS_SHEET, "Node",
             lambda v: str(v or "").startswith(reapply.DEATH_PREFIXES)),
            (reapply.CLASS_SHEET, "Class",
             lambda v: str(v or "").strip() in reapply.DEATH_CLASSES)):
        sheet = book[title]
        index = [c.value for c in sheet[1]].index(column) + 1
        doomed = [r for r in range(2, sheet.max_row + 1)
                  if belongs(sheet.cell(r, index).value)]
        assert doomed, f"{title} holds no Death rows to take out"
        for row in reversed(doomed):
            sheet.delete_rows(row, 1)
    book.save(path)


@pytest.fixture
def copy_of_the_workbook(tmp_path) -> pathlib.Path:
    target = tmp_path / "workbook.xlsx"
    shutil.copyfile(WORKBOOK, target)
    return target


def test_the_committed_workbook_holds_every_death_row():
    book = openpyxl.load_workbook(WORKBOOK)
    effects = reapply.plan_effects(book[reapply.EFFECTS_SHEET])
    classes = reapply.plan_class_stats(book[reapply.CLASS_SHEET])
    assert effects.changed == 0 and classes.changed == 0, (
        "docs/All_Things_Cataclysm.xlsx does not hold the Death trees' rows. If "
        "another branch's workbook was taken in a merge, run "
        "python tools/reapply_death_trees_workbook.py\n"
        + "\n".join(effects.report + classes.report))


def test_the_rows_taken_out_are_put_back_as_the_committed_sheets(copy_of_the_workbook):
    take_the_death_rows_out(copy_of_the_workbook)
    for title in SHEETS:
        assert sheet_rows(copy_of_the_workbook, title) != sheet_rows(WORKBOOK, title)

    report = reapply.apply(copy_of_the_workbook)
    changes = len(reapply.NODES) + len(reapply.CLASS_STATS)
    assert report[-1].startswith(f"wrote {changes} change(s)"), report
    for title in SHEETS:
        assert sheet_rows(copy_of_the_workbook, title) == sheet_rows(WORKBOOK, title), title


def test_a_second_run_writes_nothing(copy_of_the_workbook):
    before = copy_of_the_workbook.read_bytes()
    assert reapply.apply(copy_of_the_workbook) == [
        "every Death row is already as this script writes it; nothing written"]
    assert copy_of_the_workbook.read_bytes() == before


def test_check_writes_nothing_and_says_what_would_change(copy_of_the_workbook):
    take_the_death_rows_out(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()
    assert reapply.main(["--workbook", str(copy_of_the_workbook), "--check"]) == 1
    assert copy_of_the_workbook.read_bytes() == before


def test_no_other_sheet_changes(copy_of_the_workbook):
    take_the_death_rows_out(copy_of_the_workbook)
    reapply.apply(copy_of_the_workbook)
    ours = openpyxl.load_workbook(copy_of_the_workbook, read_only=True)
    theirs = openpyxl.load_workbook(WORKBOOK, read_only=True)
    assert ours.sheetnames == theirs.sheetnames
    for name in ours.sheetnames:
        if name in SHEETS:
            continue
        assert list(ours[name].iter_rows(values_only=True)) == \
            list(theirs[name].iter_rows(values_only=True)), name


def test_a_death_row_somebody_changed_is_refused_and_nothing_is_written(copy_of_the_workbook):
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[reapply.EFFECTS_SHEET]
    header = [c.value for c in sheet[1]]
    node, stat, value = (header.index(n) + 1 for n in ("Node", "Stat", "Value Per Point"))
    changed = 0
    for row in range(2, sheet.max_row + 1):
        if sheet.cell(row, node).value == "Shadow_basic_b_stem0":
            assert sheet.cell(row, stat).value == "crit_chance"
            sheet.cell(row, value).value = 9
            changed += 1
    assert changed == 1
    book.save(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()

    with pytest.raises(reapply.ReapplyError, match="Shadow_basic_b_stem0"):
        reapply.apply(copy_of_the_workbook)
    assert copy_of_the_workbook.read_bytes() == before
    assert reapply.main(["--workbook", str(copy_of_the_workbook), "--no-generate"]) == 2


def test_a_death_class_line_somebody_changed_is_refused(copy_of_the_workbook):
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[reapply.CLASS_SHEET]
    header = [c.value for c in sheet[1]]
    klass, stat, base = (header.index(n) + 1 for n in ("Class", "Stat", "Base"))
    changed = 0
    for row in range(2, sheet.max_row + 1):
        if (sheet.cell(row, klass).value, sheet.cell(row, stat).value) == ("Shadow", "area_of_effect"):
            sheet.cell(row, base).value = 50
            changed += 1
    assert changed == 1
    book.save(copy_of_the_workbook)

    with pytest.raises(reapply.ReapplyError, match="Shadow area_of_effect"):
        reapply.apply(copy_of_the_workbook)


def test_it_writes_death_rows_only():
    """A row for another tree, or a line for another class, is refused before
    anything is read, so the script cannot reach past the Death trees."""
    book = openpyxl.load_workbook(WORKBOOK)
    stray = reapply.Node("Ravager_basic_spine_001", "Stray",
                         (reapply.Row("max_health", "increased", 1),))
    with pytest.raises(reapply.ReapplyError, match="not a Death node"):
        reapply.plan_effects(book[reapply.EFFECTS_SHEET], reapply.NODES + (stray,))
    with pytest.raises(reapply.ReapplyError, match="not a Death class"):
        reapply.plan_class_stats(book[reapply.CLASS_SHEET],
                                 reapply.CLASS_STATS + (("Ravager", "evasion", 1, 0),))


def test_no_node_or_class_stat_is_listed_twice():
    keys = [(n.key, n.option) for n in reapply.NODES]
    assert len(keys) == len(set(keys))
    lines = [(c, s) for c, s, _, _ in reapply.CLASS_STATS]
    assert len(lines) == len(set(lines))


def test_every_node_named_is_a_node_of_its_tree_and_a_capstone_row_names_an_option():
    """A node key the tree files do not hold would write rows the generator then
    refuses; an option on a node that is not a capstone would never be offered."""
    import generate_datatables as gen

    nodes = {row["Name"]: row for row in gen.passive_nodes()}
    for node in reapply.NODES:
        assert node.key in nodes, f"{node.key} is not a node of any tree"
        kind = nodes[node.key]["Kind"]
        if node.option:
            assert kind == "capstone", f"{node.key} names option {node.option}"
            assert nodes[node.key][f"Option{node.option}Name"] == node.label, node.key
        else:
            # A CAPSTONE ROW WITH NO OPTION only where the capstone's own sentence
            # states a clause beside its choice: the coordinating session's ruling
            # 4 of 2026-11-25, the Soul Collector vows' Fervour thresholds.
            assert kind != "capstone" or gen.capstone_states_a_clause(nodes[node.key]["Description"]), (
                f"{node.key} is a capstone and names no option")
            assert nodes[node.key]["NodeName"] == node.label, node.key


def test_the_port_columns_are_added_when_the_sheet_lacks_them(copy_of_the_workbook):
    """The passive row port's fourteen columns, added after the sheet's last one by
    the first run that writes an action row (the Death trees' slice C1,
    2026-11-25). A sheet without them, and without the Death rows, comes back as
    the committed one."""
    take_the_death_rows_out(copy_of_the_workbook)
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[reapply.EFFECTS_SHEET]
    header = [c.value for c in sheet[1]]
    port = [column for column, _ in reapply.OPTIONAL_EFFECT_COLUMNS if column != "Scale Max Steps"]
    assert header[-len(port):] == port, header
    sheet.delete_cols(len(header) - len(port) + 1, len(port))
    book.save(copy_of_the_workbook)
    assert "Action" not in sheet_rows(copy_of_the_workbook, reapply.EFFECTS_SHEET)[0]

    report = reapply.apply(copy_of_the_workbook)
    assert any("added the column(s)" in line for line in report), report
    for title in SHEETS:
        assert sheet_rows(copy_of_the_workbook, title) == sheet_rows(WORKBOOK, title), title


def test_an_action_row_reads_back_as_it_was_written():
    """An action row has no stat and no value kind; every action column it states
    comes back from its cells, so a second run finds it unchanged."""
    node = reapply.Node("Shadow_basic_spine_008", "Envenom", ())
    for row in (reapply.A("apply_status", 6, event="critical_strike", ailment="Poison",
                          trigger_cooldown=0),
                reapply.A("execute_target", 100, event="hit_dealt", cond="target_health_below",
                          cond_value=15),
                reapply.R("attack_damage", "increased", 1, scale="own_stacks", step=1, cap=10,
                          event="kill", stack_seconds=2, min_points=2)):
        cells = reapply.cells_from_row(node, row)
        assert reapply.row_from_cells(cells).key() == row.key(), row
