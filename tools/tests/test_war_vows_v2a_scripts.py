"""The War vow options' second slice, V2a: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v2a_workbook.py` writes seven rows on six nodes and options -- Unbridled's two,
Opportunist, Unstoppable's pin, Titan, Ragnarok's doubling and Bastion -- and takes over Unstoppable's two rows and
Ragnarok's drain row from the first slice's script, so it holds every row of those two options. `tools/generate_datatables.py`
gains the `max_class_resource` and `block_chance` scales, the `frenzy_entered` event, `pin_suppressed` and
`class_resource` as stats a row may state under a condition, and refuses a `class_resource` row scaled by
`max_class_resource`.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent,
appends to the two options it took over only what they lack, and refuses what somebody wrote since; and the refusal,
with its control. The game's half is `game/Source/Cataclysm/Tests/CataclysmWarVowsV2aTests.cpp`.
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

import generate_datatables as gen  # noqa: E402
import reapply_war_vows_v2a_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
#: Every row the script lists, the three it took over included.
ROW_COUNT = 10
#: The rows this slice added; the other three were the first slice's.
NEW_ROW_COUNT = 7
TWO_HANDED = "wielding_two_handed_weapon"


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_script_lists_every_row_of_six_nodes_and_options():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    groups = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert groups == {("Berserker_trunk_004", None), ("Berserker_capstone_25", 2), ("Berserker_capstone_50", 1),
                      ("Berserker_capstone_100", 1), ("Berserker_capstone_200", 1), ("Bulwark_capstone_100", 1)}
    assert rows.EXTENDING == {"Berserker_capstone_50", "Berserker_capstone_200"}


def test_unstoppable_holds_above_75_percent_with_two_hands():
    """Moved here from the first slice's tests with the rows. "cannot be slowed, rooted, or knocked back": three flags,
    a pin counted as the root, as ruled on 2026-11-25."""
    held = _rows_of("Berserker_capstone_50")
    assert sorted(row.stat for row in held) == [
        "knockback_suppressed", "movement_speed_reduction_suppressed", "pin_suppressed"]
    for row in held:
        assert (row.option, row.kind, row.value, row.cond, row.cond_value, row.cond2) == (
            1, "flat", 1, "class_resource_above", 75, TWO_HANDED), row


def test_titan_and_ragnaroks_doubling_hold_only_with_two_hands():
    (titan,) = _rows_of("Berserker_capstone_100")
    assert (titan.option, titan.stat, titan.kind, titan.value, titan.scale, titan.step, titan.cond) == (
        1, "max_health", "increased", 1, "max_class_resource", 2, TWO_HANDED)
    drain, doubled = _rows_of("Berserker_capstone_200")
    assert (drain.stat, drain.kind, drain.cond) == ("fervour_drain_per_second", "removed", "in_wrath")
    assert (doubled.option, doubled.stat, doubled.kind, doubled.value, doubled.cond) == (
        1, "class_resource", "more", 100, TWO_HANDED)


def test_unbridled_and_bastion():
    points, reduction = _rows_of("Berserker_trunk_004")
    assert (points.stat, points.kind, points.value, points.min_points) == ("class_resource", "flat", 10, None)
    assert (reduction.stat, reduction.kind, reduction.value, reduction.scale, reduction.step,
            reduction.min_points) == ("damage_reduction", "flat", 1, "max_class_resource", 20, 5)
    (bastion,) = _rows_of("Bulwark_capstone_100")
    assert (bastion.option, bastion.stat, bastion.kind, bastion.value, bastion.scale, bastion.step, bastion.cond) == (
        1, "max_health", "increased", 1, "block_chance", 1, None)


def test_opportunist_resets_the_movement_skill_on_entering_frenzy():
    (opportunist,) = _rows_of("Berserker_capstone_25")
    assert (opportunist.option, opportunist.stat, opportunist.action, opportunist.action_event,
            opportunist.value) == (2, None, "cooldown_reset_movement", "frenzy_entered", 100)
    assert "frenzy_entered" in gen.action_events()


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), number(row["Option"]),
                    row["Condition"], row["Scale"], number(row["ScaleStep"]), row["Action"], row["ActionEvent"])
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", row.kind, float(row.value), float(row.option or 0), row.cond or "",
                   row.scale or "", float(row.step or 0), row.action or "", row.action_event or "")
            assert written.count(key) == 1, key


# ---------------------------------------------------------------------------
# The script on a copy of the workbook
# ---------------------------------------------------------------------------

def _workbook_without(tmp_path, keep_the_first_slices: bool) -> pathlib.Path:
    """A copy of the workbook with this script's rows taken out: all ten, or only the seven it added."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    first_slice = {("Berserker_capstone_50", "movement_speed_reduction_suppressed"),
                   ("Berserker_capstone_50", "knockback_suppressed"),
                   ("Berserker_capstone_200", "fervour_drain_per_second")}
    ours = {(node, row.key()) for node, _, wanted in rows.NODES for row in wanted
            if not (keep_the_first_slices and (node, row.stat) in first_slice)}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_appends_only_what_the_two_options_it_took_over_lack(tmp_path):
    """The workbook as the first slice left it: Unstoppable and Ragnarok hold their first slice's rows."""
    copy = _workbook_without(tmp_path, keep_the_first_slices=True)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == NEW_ROW_COUNT, first
    assert sum(1 for line in first if "the rest are appended" in line) == 2, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_writes_all_ten_on_a_workbook_with_none(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_first_slices=False)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_one_of_its_options(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_first_slices=False)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Bulwark_capstone_100", "Option": 1, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' second slice"):
        rows.apply(copy)
    assert copy.read_bytes() == before


# ---------------------------------------------------------------------------
# The generator's new refusal, and its control
# ---------------------------------------------------------------------------

#: Every column the passive reader asks for, as `test_passive_rows_carry_the_action_columns.py` writes them.
HEADERS = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Reach Metres", "Min Points",
           "Scale Max Steps", "Action", "Action Event", "Fraction Of", "Stack Seconds",
           "Scale Offset", "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
           "Trigger Cooldown", "Event Value", "Ailment", "Damage Share", "Per Point From", "Max Points"]


def _read(tmp_path, stat: str) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    cells = {"Node": "Berserker_trunk_004", "Stat": stat, "Value Kind": "flat", "Value Per Point": 1,
             "Scale": "max_class_resource", "Scale Step": 20}
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_a_class_resource_row_scaled_by_its_own_maximum_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match=r"^Passive Effects row 2: .* scales `class_resource` by 'max_class_resource'"):
        _read(tmp_path, "class_resource")


def test_the_control_another_stat_scaled_by_the_maximum_is_read(tmp_path):
    (got,) = _read(tmp_path, "damage_reduction")
    assert (got["Stat"], got["Scale"], got["ScaleStep"]) == ("damage_reduction", "max_class_resource", 20.0)


def test_the_two_scales_are_the_generators():
    assert gen.SCALES["max_class_resource"][1] == 1000.0
    assert gen.SCALES["block_chance"][1] == 100.0
    assert gen.CONDITIONED_STATS_WITH_AN_ASKER["pin_suppressed"] == gen.ASKER_PASSES_NOTHING_MORE
    assert gen.CONDITIONED_STATS_WITH_AN_ASKER["class_resource"] == gen.ASKER_PASSES_NOTHING_MORE
