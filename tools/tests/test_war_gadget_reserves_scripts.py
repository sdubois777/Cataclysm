"""The War machines' count caps taken off their rows, for issue #1934's Fervour reserves.

2026-11-25. `tools/apply_war_gadget_reserves_2026_11_25.py` removes `MaxActive` and the sentence stating the cap from
Bolt Turret, Ballista and Iron Fortress in the design workbook's Weapon Skills sheet, as the coordinating session ruled
(Q2) under the owner's ruling of 2026-11-20 on issue #2787. These tests hold what it writes, that the workbook and the
generated table hold it, that it is idempotent, and that it refuses a cell somebody wrote since. The generator's
refusal of a cap on a Deployable row is in `test_minion_skill_shapes.py`; the game's half is in
`game/Source/Cataclysm/Tests/CataclysmCommandTests.cpp`.
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

import apply_war_gadget_reserves_2026_11_25 as caps  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
ROWS = {"War_Crossbow_Special": "Bolt Turret", "War_Spear_Special": "Ballista", "War_Spear_Ultimate": "Iron Fortress"}


def test_each_edit_takes_away_the_cap_and_nothing_else():
    assert len(caps.EDITS) == 6
    for edit in caps.EDITS:
        removed = edit.before.replace(edit.after, "", 1) if edit.after in edit.before else None
        if edit.column == "Shape Params":
            assert edit.before.startswith(edit.after) or "MaxActive" not in edit.after, edit
            assert "MaxActive" in edit.before and "MaxActive" not in edit.after, edit
            assert "FervourReserve=5" in edit.after, edit
        else:
            assert removed is None or "stand" in removed, edit
            assert "may stand" in edit.before and "may stand" not in edit.after, edit
            assert "reserves 5 Fervour" in edit.after, edit


def test_the_workbook_holds_the_edits():
    assert caps.apply(WORKBOOK, check_only=True)[-1] == "every cell is already written; nothing written"


def test_the_generated_csv_holds_them():
    with open(REPO_ROOT / "game" / "Data" / "WeaponSkills.csv", encoding="utf-8", newline="") as handle:
        rows = {row["Name"]: row for row in csv.DictReader(handle) if row["Name"] in ROWS}
    assert set(rows) == set(ROWS)
    for name, row in rows.items():
        assert "MaxActive" not in row["ShapeParams"], name
        assert "FervourReserve=5" in row["ShapeParams"], name
        assert "may stand" not in row["SkillDescription"], name


def _with_the_old_text(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[caps.SHEET]
    header = [cell.value for cell in sheet[1]]
    for edit in caps.EDITS:
        for index in range(2, sheet.max_row + 1):
            if sheet.cell(index, header.index("Skill Name") + 1).value == edit.skill \
                    and sheet.cell(index, header.index("Shape") + 1).value == "Deployable":
                sheet.cell(index, header.index(edit.column) + 1).value = edit.before
    book.save(copy)
    return copy


def test_the_script_writes_the_six_cells_once(tmp_path):
    copy = _with_the_old_text(tmp_path)
    assert caps.main(["--check", "--workbook", str(copy)]) == 1
    first = caps.apply(copy)
    assert first[-1] == "6 cell(s) written", first
    assert caps.apply(copy)[-1] == "every cell is already written; nothing written"


def test_the_script_refuses_a_cell_somebody_wrote_since(tmp_path):
    copy = _with_the_old_text(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[caps.SHEET]
    header = [cell.value for cell in sheet[1]]
    for index in range(2, sheet.max_row + 1):
        if sheet.cell(index, header.index("Skill Name") + 1).value == "Ballista":
            sheet.cell(index, header.index("Shape Params") + 1).value = "Count=1; Duration=9; Minions=Ballista:1"
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(caps.RewordError, match="somebody has written it since"):
        caps.apply(copy)
    assert copy.read_bytes() == before
