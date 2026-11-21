"""Subjugate costs 30 Fervour, stated in the workbook's Weapon Skills sheet.

Issue #2801. The owner had taken Subjugate's 30 Fervour reserve for its cost,
while every Ultimate costs the slot's 50, and ruled on 2026-11-21: "I think we
should actually change it to cost 30 fervor". The ruling routes it through a
Fervour Cost column on the Weapon Skills sheet, blank (-1, the slot's figure)
on every other row, as the Damage Percent, Cooldown and Mana Cost columns
before it are. The game reads it in `UCataclysmWeaponSkills` and stamps it as
`UCataclysmGameplayAbility::FervourCostOverride`, which `FervourCostFor` reads
before the slot's figure.

These checks read the committed CSVs and the C++ source as text; continuous
integration never builds the game, so the Unreal tests in
`CataclysmSkillCostTests.cpp` are what show the press paying 30.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import openpyxl
import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import generate_datatables as gen  # noqa: E402

DATA = REPO / "game" / "Data"
SOURCE = REPO / "game" / "Source" / "Cataclysm"
SUBJUGATE = "Demonic_Staff_Ultimate"

HEADER = ["Weapon Type", "Damage Type", "Slot", "Skill Name",
          "Skill Description", "Tags", "Shape", "Shape Params", "Crit Chance",
          "Damage Percent", "Cooldown", "Mana Cost", "Fervour Cost"]


def rows_of(name: str) -> list[dict[str, str]]:
    with open(DATA / name, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def book_with(tmp_path: pathlib.Path, rows: list[list]):
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Weapon Skills")
    for row in [HEADER, *rows]:
        sheet.append(row)
    tags = book.create_sheet("Tags")
    tags.append(["Tag Name", "Description"])
    tags.append(["Slot.Ultimate", "Ultimates"])
    path = tmp_path / "book.xlsx"
    book.save(path)
    return openpyxl.load_workbook(path, data_only=True)


def test_subjugates_row_states_30_and_no_other_row_states_any():
    by_name = {row["Name"]: row for row in rows_of("WeaponSkills.csv")}
    assert float(by_name[SUBJUGATE]["FervourCost"]) == 30.0
    assert by_name[SUBJUGATE]["SkillName"] == "Subjugate"
    stating = sorted(name for name, row in by_name.items()
                     if float(row["FervourCost"]) != gen.UNSTATED_SKILL_NUMBER)
    assert stating == [SUBJUGATE], (
        "the owner's ruling names Subjugate alone; every other row takes its "
        f"slot's Fervour cost, and these state their own: {stating}")


def test_the_ultimate_slot_still_costs_50():
    """CONTROL: the ruling moved Subjugate, not the slot every other Ultimate
    reads."""
    slots = {row["Slot"]: row for row in rows_of("SkillSlots.csv")}
    assert float(slots["Ultimate"]["FervourCost"]) == 50.0


def test_the_generator_reads_the_column_and_blank_is_the_slots(tmp_path):
    book = book_with(tmp_path, [
        ["Staff", "Demonic", "Ultimate", "Take", "Takes.", "", "", "",
         None, None, None, None, 30],
        ["Staff", "Fire", "Ultimate", "Burn", "Burns.", "", "", "",
         None, None, None, None, None]])
    costs = {row["SkillName"]: row["FervourCost"]
             for row in gen.weapon_skills(book)}
    assert costs == {"Take": 30.0, "Burn": gen.UNSTATED_SKILL_NUMBER}


def test_a_negative_fervour_cost_is_refused(tmp_path):
    """-1 is the sentinel for "states nothing", so a written -1 would be read
    as nothing with nothing reporting it."""
    book = book_with(tmp_path, [
        ["Staff", "Demonic", "Ultimate", "Take", "Takes.", "", "", "",
         None, None, None, None, -1]])
    with pytest.raises(gen.DataError, match="Fervour cost"):
        gen.weapon_skills(book)


def test_the_game_carries_the_row_to_the_skill_it_grants():
    """The row's field, its copy onto the skill, and the stamp onto the
    template, each the line that moves the figure one step."""
    rows = (SOURCE / "Data" / "CataclysmDataRows.h").read_text(encoding="utf-8")
    skills = (SOURCE / "AbilitySystem" / "CataclysmWeaponSkills.cpp").read_text(
        encoding="utf-8")
    row_struct = rows[rows.index("struct FCataclysmWeaponSkillRow"):]
    row_struct = row_struct[:row_struct.index("\n};")]
    assert "float FervourCost = -1.0f;" in row_struct
    assert "Skill.FervourCost = Row.FervourCost;" in skills
    assert "Template.FervourCostOverride = Skill.FervourCost;" in skills
