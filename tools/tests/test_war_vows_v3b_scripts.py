"""The War vow options' third slice, second half, V3b: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v3b_workbook.py` writes four rows -- Colossus Eternal's flag, save and bonus from
current health, and Thousand Deaths' chain -- and takes over Colossus Eternal's healing row from V1's script, so it holds
every row of that option. `tools/generate_datatables.py` gains an action, `frenzy_chain` on `hit_dealt`, its radius in
Event Value, and a scale, `current_health`.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent,
appends to the taken-over option only what it lacks and refuses what somebody wrote since; and each new generator rule,
with its control. The game's half is `game/Source/Cataclysm/Tests/CataclysmWarVowsV3bTests.cpp`.
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
import reapply_war_vows_v1_workbook as v1  # noqa: E402
import reapply_war_vows_v3b_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: Every row the script lists, the one earlier row included.
ROW_COUNT = 5

#: The rows this slice added.
NEW_ROW_COUNT = 4

#: The row V1's script wrote and this one took over: (node, stat).
EARLIER = {("Bulwark_capstone_200", "healing_received")}


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_script_lists_every_row_of_two_options():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    groups = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert groups == {("Bulwark_capstone_200", 2), ("Berserker_capstone_200", 2)}
    assert rows.EXTENDING == {"Bulwark_capstone_200"}


def test_v1s_script_no_longer_lists_colossus_eternals_row():
    listed = {(node, row.stat, row.option) for node, _, wanted in v1.NODES for row in wanted}
    assert ("Bulwark_capstone_200", "healing_received", 2) not in listed
    assert ("Bulwark_capstone_100", "retaliation", 2) in listed, "control: Bloodied but Unbroken's row stays V1's"


def test_colossus_eternal_doubles_saves_and_scales_by_current_health():
    healing, twice, save, bonus = _rows_of("Bulwark_capstone_200")
    assert (healing.option, healing.stat, healing.kind, healing.value) == (2, "healing_received", "more", 100)
    assert (twice.option, twice.stat, twice.kind, twice.value) == (2, "max_health_increases_count_twice", "flat", 1)
    assert (save.option, save.stat, save.kind, save.value, save.cond) == (
        2, "lethal_hit_survived_above_health", "flat", 50, None)
    assert (bonus.option, bonus.stat, bonus.kind, bonus.value, bonus.scale, bonus.step, bonus.cond,
            bonus.cond_value) == (2, "attack_damage", "flat", 2, "current_health", 100, "health_above", 75)


def test_thousand_deaths_chains_in_frenzy_while_dual_wielding():
    (chain,) = _rows_of("Berserker_capstone_200")
    assert (chain.option, chain.action, chain.action_event, chain.value, chain.event_value) == (
        2, "frenzy_chain", "hit_dealt", 60, 5)
    assert (chain.cond, chain.cond2) == ("in_frenzy", "dual_wielding")


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], number(row["ValuePerPoint"]), number(row["Option"]), row["Action"],
                    row["ActionEvent"], number(row["EventValue"]), row["Condition"], number(row["ConditionValue"]),
                    row["Condition2"], row["Scale"], number(row["ScaleStep"]), row["FractionOf"])
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", float(row.value), float(row.option or 0), row.action or "",
                   row.action_event or "", float(row.event_value or 0), row.cond or "", float(row.cond_value or 0),
                   row.cond2 or "", row.scale or "", float(row.step or 0), "")
            assert written.count(key) == 1, key


# ---------------------------------------------------------------------------
# The script on a copy of the workbook
# ---------------------------------------------------------------------------

def _workbook_without(tmp_path, keep_the_earlier_rows: bool) -> pathlib.Path:
    """A copy of the workbook with this script's rows taken out: all five, or only the four it added."""
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    ours = {(node, row.key()) for node, _, wanted in rows.NODES for row in wanted
            if not (keep_the_earlier_rows and (node, row.stat) in EARLIER)}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_appends_only_what_colossus_eternal_lacks(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=True)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == NEW_ROW_COUNT, first
    assert sum(1 for line in first if "the rest are appended" in line) == 1, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_writes_all_five_on_a_workbook_with_none(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=False)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_one_of_its_options(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=False)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Berserker_capstone_200", "Option": 2, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' third slice, second half"):
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

CHAIN = {"Node": "Berserker_capstone_200", "Option": 2, "Value Per Point": 60, "Action": "frenzy_chain",
         "Action Event": "hit_dealt", "Condition": "in_frenzy", "Condition 2": "dual_wielding", "Event Value": 5}

BONUS = {"Node": "Bulwark_capstone_200", "Option": 2, "Stat": "attack_damage", "Value Kind": "flat",
         "Value Per Point": 2, "Scale": "current_health", "Scale Step": 100, "Condition": "health_above",
         "Condition Value": 75}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_a_chain_is_read_with_its_radius_and_no_fraction(tmp_path):
    (got,) = _read(tmp_path, CHAIN)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["ValuePerPoint"], got["FractionOf"],
            got["Condition2"]) == ("frenzy_chain", "hit_dealt", 5.0, 60.0, "", "dual_wielding")


def test_a_chain_with_no_radius_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="chains an attack and states no Event Value"):
        _read(tmp_path, {**CHAIN, "Event Value": None})


def test_a_chain_beyond_twenty_metres_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="The radius is above 0 and up to 20"):
        _read(tmp_path, {**CHAIN, "Event Value": 21})


def test_a_chain_on_an_event_that_is_not_a_hit_dealt_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="Only a hit dealt names the enemy struck"):
        _read(tmp_path, {**CHAIN, "Action Event": "critical_strike"})


def test_the_control_the_current_health_scale_is_read(tmp_path):
    (got,) = _read(tmp_path, BONUS)
    assert (got["Stat"], got["Scale"], got["ScaleStep"], got["Condition"], got["ConditionValue"]) == (
        "attack_damage", "current_health", 100.0, "health_above", 75.0)


def test_a_current_health_step_beyond_its_bound_is_refused(tmp_path):
    with pytest.raises(gen.DataError):
        _read(tmp_path, {**BONUS, "Scale Step": 5001})


def test_the_names_are_the_generators():
    assert gen.FRENZY_CHAIN_ACTION == "frenzy_chain"
    assert gen.FRENZY_CHAIN_EVENTS == ("hit_dealt",)
    assert "current_health" in gen.SCALES
