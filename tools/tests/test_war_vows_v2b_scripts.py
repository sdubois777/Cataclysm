"""The War vow options' second slice, V2b: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v2b_workbook.py` writes five rows -- Warlord, Pure Carnage, Ragnarok's shockwaves,
Blood Oath and Spiteful Guard -- and takes over Ragnarok's two earlier rows from V2a's script, so it holds all three of
that option's rows. `tools/generate_datatables.py` gains two actions, `enemy_armor_removed_nearby` and
`smite_nearby_by_hit` (its radius in Event Value, on the events that carry a hit), and lets an action carry a second
condition, an execute excepted.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent,
appends to Ragnarok's option only what it lacks and refuses what somebody wrote since; and each new generator rule, with
its control. The game's half is `game/Source/Cataclysm/Tests/CataclysmWarVowsV2bTests.cpp`.
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
import reapply_war_vows_v2b_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
#: Every row the script lists, Ragnarok's two earlier ones included.
ROW_COUNT = 7
#: The rows this slice added.
NEW_ROW_COUNT = 5
TWO_HANDED = "wielding_two_handed_weapon"


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_script_lists_every_row_of_five_options():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    groups = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert groups == {("Berserker_capstone_25", 1), ("Berserker_capstone_100", 2), ("Berserker_capstone_200", 1),
                      ("Bulwark_capstone_25", 3), ("Bulwark_capstone_100", 3)}
    assert rows.EXTENDING == {"Berserker_capstone_200"}


def test_warlord_removes_armour_nearby_above_half_fervour_with_two_hands():
    (warlord,) = _rows_of("Berserker_capstone_25")
    assert (warlord.option, warlord.action, warlord.action_event, warlord.value, warlord.tags) == (
        1, "enemy_armor_removed_nearby", "hit_dealt", 15, "Type.Melee")
    assert (warlord.cond, warlord.cond_value, warlord.cond2) == ("class_resource_above", 50, TWO_HANDED)
    assert (warlord.stack_seconds, warlord.cap) == (3, 1), "3 seconds, one stack refreshed on each hit"


def test_pure_carnage_and_ragnaroks_shockwaves_are_novas_sized_by_the_hit():
    (carnage,) = _rows_of("Berserker_capstone_100")
    assert (carnage.option, carnage.action, carnage.action_event, carnage.cond, carnage.value, carnage.event_value) == (
        2, "smite_nearby_by_hit", "critical_strike", "in_frenzy", 30, 4)
    drain, doubled, shockwaves = _rows_of("Berserker_capstone_200")
    assert (drain.stat, drain.kind, drain.cond) == ("fervour_drain_per_second", "removed", "in_wrath")
    assert (doubled.stat, doubled.kind, doubled.value, doubled.cond) == ("class_resource", "more", 100, TWO_HANDED)
    assert (shockwaves.option, shockwaves.action, shockwaves.action_event, shockwaves.cond, shockwaves.value,
            shockwaves.event_value) == (1, "smite_nearby_by_hit", "hit_dealt", "in_wrath", 50, 6)
    assert rows.MELEE_RANGE_METRES == 4


def test_blood_oath_and_spiteful_guard():
    (blood,) = _rows_of("Bulwark_capstone_25")
    assert (blood.option, blood.stat, blood.kind, blood.value) == (3, "retaliation_penetration", "flat", 25)
    (spite,) = _rows_of("Bulwark_capstone_100")
    assert (spite.option, spite.stat, spite.kind, spite.value) == (
        3, "blocked_damage_added_to_next_retaliation", "flat", 1)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], number(row["ValuePerPoint"]), number(row["Option"]), row["Condition"],
                    row["Condition2"], row["Action"], row["ActionEvent"], number(row["EventValue"]),
                    number(row["StackSeconds"]))
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", float(row.value), float(row.option or 0), row.cond or "", row.cond2 or "",
                   row.action or "", row.action_event or "", float(row.event_value or 0),
                   float(row.stack_seconds or 0))
            assert written.count(key) == 1, key


# ---------------------------------------------------------------------------
# The script on a copy of the workbook
# ---------------------------------------------------------------------------

def _workbook_without(tmp_path, keep_the_earlier_rows: bool) -> pathlib.Path:
    """A copy of the workbook with this script's rows taken out: all seven, or only the five it added."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    earlier = {("Berserker_capstone_200", "fervour_drain_per_second"), ("Berserker_capstone_200", "class_resource")}
    ours = {(node, row.key()) for node, _, wanted in rows.NODES for row in wanted
            if not (keep_the_earlier_rows and (node, row.stat) in earlier)}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_appends_only_what_ragnaroks_option_lacks(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=True)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == NEW_ROW_COUNT, first
    assert sum(1 for line in first if "the rest are appended" in line) == 1, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_writes_all_seven_on_a_workbook_with_none(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=False)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_one_of_its_options(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=False)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Bulwark_capstone_25", "Option": 3, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' second slice"):
        rows.apply(copy)
    assert copy.read_bytes() == before


# ---------------------------------------------------------------------------
# The generator's new rules, each with its control
# ---------------------------------------------------------------------------

#: Every column the passive reader asks for, as `test_passive_rows_carry_the_action_columns.py` writes them.
HEADERS = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Reach Metres", "Min Points",
           "Scale Max Steps", "Action", "Action Event", "Fraction Of", "Stack Seconds",
           "Scale Offset", "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
           "Trigger Cooldown", "Event Value", "Ailment", "Damage Share", "Per Point From", "Max Points"]

NOVA = {"Node": "Berserker_capstone_100", "Option": 2, "Value Per Point": 30, "Action": "smite_nearby_by_hit",
        "Action Event": "critical_strike", "Condition": "in_frenzy", "Event Value": 4}
WARLORD = {"Node": "Berserker_capstone_25", "Option": 1, "Value Per Point": 15,
           "Action": "enemy_armor_removed_nearby", "Action Event": "hit_dealt", "Required Tags": "Type.Melee",
           "Condition": "class_resource_above", "Condition Value": 50, "Condition 2": TWO_HANDED,
           "Stack Seconds": 3, "Scale Max Steps": 1}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_a_nova_sized_by_a_hit_is_read_with_its_radius(tmp_path):
    (got,) = _read(tmp_path, NOVA)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["ValuePerPoint"]) == (
        "smite_nearby_by_hit", "critical_strike", 4.0, 30.0)


def test_a_nova_with_no_radius_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="states no Event Value. It is the radius in metres"):
        _read(tmp_path, {**NOVA, "Event Value": None})


def test_a_nova_on_an_event_that_carries_no_hit_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="which carries no hit"):
        _read(tmp_path, {**NOVA, "Action Event": "block"})


def test_the_control_an_action_with_a_second_condition_is_read(tmp_path):
    """Warlord's shape: an action under two conditions. Refused as "An action carries one condition" until V2b."""
    (got,) = _read(tmp_path, WARLORD)
    assert (got["Action"], got["Condition"], got["Condition2"], got["StackSeconds"], got["ScaleMaxSteps"]) == (
        "enemy_armor_removed_nearby", "class_resource_above", TWO_HANDED, 3.0, 1)


def test_the_nearby_armour_removal_on_an_event_naming_nobody_struck_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="Only an event naming the character"):
        _read(tmp_path, {**WARLORD, "Action Event": "block"})


def test_the_names_are_the_generators():
    assert "smite_nearby_by_hit" in gen.NEARBY_ACTIONS
    assert gen.PLACED_ACTIONS["enemy_armor_removed_nearby"] == ("hit_dealt",)
    assert set(gen.SMITE_NEARBY_BY_HIT_EVENTS) <= gen.action_events()
