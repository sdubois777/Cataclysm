"""The generator refuses a Skill Slots sheet whose "Mana Reserved" column is wrong. Ruled 2026-11-27.

Auras reserve maximum mana rather than drain it (the owner's choice "Reserve, don't drain"), and the Skill Slots
sheet's "Mana Reserved" column holds the figure: 50 on the Aura, nought elsewhere. `generate_datatables.skill_slots`
refuses three mistakes: a reservation below nought, a reservation on a slot other than the Aura, and a slot that both
reserves and costs mana. Each test changes one cell of the real sheet's rows; THE CONTROL is the same rows unchanged,
which the generator accepts with the Aura reserving 50.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as generator  # noqa: E402

openpyxl = pytest.importorskip("openpyxl")


class _Sheet:
    """Stands in for an openpyxl worksheet: `skill_slots` reads only `iter_rows(values_only=True)`."""

    def __init__(self, rows):
        self._rows = rows

    def iter_rows(self, values_only=True):
        return iter(self._rows)


@pytest.fixture(scope="module")
def sheet_rows():
    book = openpyxl.load_workbook(REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx", read_only=True, data_only=True)
    try:
        return [tuple(row) for row in book["Skill Slots"].iter_rows(values_only=True)]
    finally:
        book.close()


def _with(rows, slot, column, value):
    """The rows with one cell changed: `column` of the row whose Slot is `slot`."""
    headers = list(rows[0])
    slot_at = headers.index("Slot")
    column_at = headers.index(column)
    changed = [rows[0]]
    for row in rows[1:]:
        cells = list(row)
        if cells[slot_at] == slot:
            cells[column_at] = value
        changed.append(tuple(cells))
    return changed


def _slots(rows):
    return generator.skill_slots({"Skill Slots": _Sheet(rows)})


def test_control_the_real_sheet_is_accepted_and_the_aura_reserves_fifty(sheet_rows):
    by_slot = {row["Slot"]: row for row in _slots(sheet_rows)}
    assert by_slot["Aura"]["ManaReserved"] == 50.0
    assert by_slot["Aura"]["ManaCost"] == 0.0
    assert all(row["ManaReserved"] == 0.0 for slot, row in by_slot.items() if slot != "Aura")


def test_a_reservation_below_nothing_is_refused(sheet_rows):
    with pytest.raises(generator.DataError, match="reserves -5"):
        _slots(_with(sheet_rows, "Aura", "Mana Reserved", -5))


def test_a_reservation_on_a_slot_other_than_the_aura_is_refused(sheet_rows):
    with pytest.raises(generator.DataError, match=r"Heavy reserves 10.*Only \['Aura'\]"):
        _slots(_with(sheet_rows, "Heavy", "Mana Reserved", 10))


def test_a_slot_that_reserves_and_costs_mana_is_refused(sheet_rows):
    with pytest.raises(generator.DataError, match="reserves 50.*and costs 20"):
        _slots(_with(sheet_rows, "Aura", "Mana Cost", 20))
