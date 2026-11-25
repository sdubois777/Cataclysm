"""The War vow options' third slice, V3a: its rows script, what it wrote, and the generator rules it added.

2026-11-25. `tools/reapply_war_vows_v3a_workbook.py` writes five rows -- Thornwall's stack, Wrath Incarnate's pulse,
Absorb and Return's share for big hits, and Eternal Hunger's cap and decay -- and takes over Wrath Incarnate's and
Eternal Hunger's earlier rows from V1's script, so it holds every row of both options. `tools/generate_datatables.py`
gains a placed action, `attacker_retaliation_taken` on `hit_taken`; an event, `big_hit_taken`, which carries an
amount; and an action on it, `retaliation_pulse`, its radius in Event Value.

These tests hold the rulings' rows, that the workbook and the generated table hold them, that the script is idempotent,
appends to the two taken-over options only what they lack and refuses what somebody wrote since; and each new generator
rule, with its control. The game's half is `game/Source/Cataclysm/Tests/CataclysmWarVowsV3aTests.cpp`.
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
import reapply_war_vows_v3a_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: Every row the script lists, the two earlier ones included.
ROW_COUNT = 7

#: The rows this slice added.
NEW_ROW_COUNT = 5

#: The two rows V1's script wrote and this one took over: (node, stat).
EARLIER = {("Bulwark_capstone_200", "retaliation"), ("Berserker_capstone_200", "life_leech")}


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


# ---------------------------------------------------------------------------
# The rows
# ---------------------------------------------------------------------------

def test_the_script_lists_every_row_of_three_options_and_one_node():
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT
    groups = {(node, row.option) for node, _, wanted in rows.NODES for row in wanted}
    assert groups == {("Bulwark_capstone_50", 3), ("Bulwark_capstone_200", 3), ("Bulwark_basic_tw_t2_001", None),
                      ("Berserker_capstone_200", 3)}
    assert rows.EXTENDING == {"Bulwark_capstone_200", "Berserker_capstone_200"}


def test_v1s_script_no_longer_lists_the_two_rows_taken_over():
    listed = {(node, row.stat, row.option) for node, _, wanted in v1.NODES for row in wanted}
    assert ("Bulwark_capstone_200", "retaliation", 3) not in listed
    assert ("Berserker_capstone_200", "life_leech", 3) not in listed
    assert ("Bulwark_capstone_200", "healing_received", 2) in listed, "control: Colossus Eternal's row stays V1's"


def test_thornwall_places_a_stack_on_the_attacker_of_a_hit_taken():
    (thorns,) = _rows_of("Bulwark_capstone_50")
    assert (thorns.option, thorns.action, thorns.action_event, thorns.value) == (
        3, "attacker_retaliation_taken", "hit_taken", 5)
    assert (thorns.stack_seconds, thorns.cap) == (10, 10), "10 seconds refreshed by each hit, up to 10 stacks"


def test_wrath_incarnate_keeps_its_armour_row_and_pays_a_pulse_on_a_big_hit():
    armour, pulse = _rows_of("Bulwark_capstone_200")
    assert (armour.option, armour.stat, armour.kind, armour.value, armour.scale, armour.step) == (
        3, "retaliation", "increased", 1, "armor", 100)
    assert (pulse.option, pulse.action, pulse.action_event, pulse.value, pulse.event_value) == (
        3, "retaliation_pulse", "big_hit_taken", 200, 4)


def test_absorb_and_return_adds_three_a_point_for_big_hits():
    (share,) = _rows_of("Bulwark_basic_tw_t2_001")
    assert (share.stat, share.kind, share.value, share.option) == ("retaliation_for_big_hits", "flat", 3, None)


def test_eternal_hunger_holds_leech_overheal_up_to_its_maximum_and_decays_at_one_percent():
    leech, cap, decay = _rows_of("Berserker_capstone_200")
    assert (leech.option, leech.stat, leech.kind, leech.value) == (3, "life_leech", "increased", 50)
    assert (cap.option, cap.stat, cap.kind, cap.value) == (
        3, "leech_overheal_absorb_percent_of_maximum_health", "flat", 100)
    assert (decay.option, decay.stat, decay.kind, decay.value) == (
        3, "leech_overheal_absorb_decay_percent_per_second", "flat", 1)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(REPO_ROOT / "game" / "Data" / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], number(row["ValuePerPoint"]), number(row["Option"]), row["Action"],
                    row["ActionEvent"], number(row["EventValue"]), number(row["StackSeconds"]),
                    number(row["ScaleMaxSteps"]), row["FractionOf"])
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", float(row.value), float(row.option or 0), row.action or "",
                   row.action_event or "", float(row.event_value or 0), float(row.stack_seconds or 0),
                   float(row.cap or 0), "")
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
    ours = {(node, row.key()) for node, _, wanted in rows.NODES for row in wanted
            if not (keep_the_earlier_rows and (node, row.stat) in EARLIER)}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_appends_only_what_the_two_taken_over_options_lack(tmp_path):
    copy = _workbook_without(tmp_path, keep_the_earlier_rows=True)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == NEW_ROW_COUNT, first
    assert sum(1 for line in first if "the rest are appended" in line) == 2, first
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
    sheet.append([{"Node": "Bulwark_capstone_50", "Option": 3, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="the War vow options' third slice"):
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

PULSE = {"Node": "Bulwark_capstone_200", "Option": 3, "Value Per Point": 200, "Action": "retaliation_pulse",
         "Action Event": "big_hit_taken", "Event Value": 4}

THORNS = {"Node": "Bulwark_capstone_50", "Option": 3, "Value Per Point": 5, "Action": "attacker_retaliation_taken",
          "Action Event": "hit_taken", "Stack Seconds": 10, "Scale Max Steps": 10}


def _read(tmp_path, cells: dict) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(HEADERS)
    sheet.append([cells.get(name) for name in HEADERS])
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


def test_the_control_a_pulse_is_read_with_its_radius_and_no_fraction(tmp_path):
    (got,) = _read(tmp_path, PULSE)
    assert (got["Action"], got["ActionEvent"], got["EventValue"], got["ValuePerPoint"], got["FractionOf"]) == (
        "retaliation_pulse", "big_hit_taken", 4.0, 200.0, "")


def test_a_pulse_with_no_radius_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="pays a retaliation pulse and states no Event Value"):
        _read(tmp_path, {**PULSE, "Event Value": None})


def test_a_pulse_beyond_twenty_metres_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="The radius is above 0 and up to 20"):
        _read(tmp_path, {**PULSE, "Event Value": 21})


def test_a_pulse_on_an_event_that_is_not_a_big_hit_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="Only a big hit carries a blow"):
        _read(tmp_path, {**PULSE, "Action Event": "hit_taken"})


def test_a_pulse_with_a_fraction_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="pays a retaliation pulse and states Fraction Of"):
        _read(tmp_path, {**PULSE, "Fraction Of": "maximum"})


def test_the_control_thornwalls_stack_is_read_on_a_hit_taken(tmp_path):
    (got,) = _read(tmp_path, THORNS)
    assert (got["Action"], got["ActionEvent"], got["StackSeconds"], got["ScaleMaxSteps"], got["ValuePerPoint"]) == (
        "attacker_retaliation_taken", "hit_taken", 10.0, 10, 5.0)


def test_thornwalls_stack_on_an_event_naming_nobody_who_struck_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="Only an event naming the character"):
        _read(tmp_path, {**THORNS, "Action Event": "block"})


def test_the_names_are_the_generators():
    assert gen.PLACED_ACTIONS["attacker_retaliation_taken"] == ("hit_taken",)
    assert gen.RETALIATION_PULSE_ACTION == "retaliation_pulse"
    assert gen.RETALIATION_PULSE_EVENTS == ("big_hit_taken",)
    assert "big_hit_taken" in gen.action_events()
    assert "big_hit_taken" in gen.EVENTS_WITH_AN_AMOUNT
