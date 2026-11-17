"""The craft residue costs mirrored in the simulation, against the workbook.

WHY THIS EXISTS. `sim/cataclysm_sim/residue.py` derives the Consumption
Threshold from what each craft adds to an item's Cataclysmic Residue. Those five
numbers live in the Crafting sheet of `docs/All_Things_Cataclysm.xlsx`, and the
simulation mirrors them as constants the way it mirrors every other stored
value: the simulation is pure standard library Python and does not read the
workbook.

A mirrored number drifts. This project has been bitten by exactly that twice
with `sim/cataclysm_sim/scoring.py`, which is why `sim/verify_scoring_port.py`
exists, and the same comparison is made for the drop weights, the socket maxima
and the affix tier weights by their own test files.

WHAT DRIFT WOULD DO HERE, and it is quieter than most. Every threshold would
move, every test in `sim/tests/test_residue.py` would still pass -- they check
that the threshold is 80% to 90% of the path, and it would be, of the wrong path
-- and the design document's table would be silently wrong. Nothing else
compares these two.

WHICH IS AUTHORITATIVE. The workbook, as everywhere else. When this fails the
fix is to change the constant in `residue.py`, not the sheet.

WHAT THE SHEET IS. Three tables stacked in one sheet; see
`tools/tests/test_crafting_section_matches_the_sheet.py`, which documents the
shape in full. Only the third matters here: the operations, under a row whose
first cell is the literal word "Action", with the residue each adds in the
"CR Impact" column.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: The first cell of the row separating the materials from the operations.
ACTIONS_HEADER = "Action"

#: Column index of the residue an operation adds, in the operations table.
CR_IMPACT = 2

#: The operation each mirrored constant comes from, and the name of the
#: constant. Keyed on what the sheet calls the operation.
MIRRORED = {
    "Imprint Enchantment": "IMPRINT_ENCHANTMENT_RESIDUE",
    "Deterministic Affix": "DETERMINISTIC_AFFIX_RESIDUE_PER_TIER",
    "Upgrade Item Level (1 Tier)": "UPGRADE_LEVEL_RESIDUE_PER_LEVEL",
    "Add Socket": "ADD_SOCKET_RESIDUE",
    "Socket Gem": "SOCKET_GEM_RESIDUE",
}

#: The two operations that REMOVE residue, which the derivation deliberately
#: does not use. Checked anyway, because the derivation's own claim that one
#: Purified Essence is enough rests on the halving being a halving.
MITIGATIONS = {
    "CR Cleanse (Partial)": "PURIFIED_ESSENCE_REMOVES",
    "Reduce CR Gain (1 Craft)": None,   # not mirrored; only the share matters
}


@pytest.fixture(scope="module")
def operations() -> dict[str, str]:
    """Operation name against the raw text of its CR Impact cell."""
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")

    book = openpyxl.load_workbook(WORKBOOK, read_only=True, data_only=True)
    grid = [row for row in book["Crafting"].iter_rows(values_only=True)
            if any(cell is not None and str(cell).strip() for cell in row)]

    for index, row in enumerate(grid):
        if str(row[0] or "").strip() == ACTIONS_HEADER:
            found = {str(r[0]).strip(): str(r[CR_IMPACT] or "").strip()
                     for r in grid[index + 1:] if r and r[0]}
            assert found, (
                "the Crafting sheet has an 'Action' header row and nothing "
                "under it")
            return found

    raise AssertionError(
        "The Crafting sheet no longer has a row whose first cell is "
        f"{ACTIONS_HEADER!r}. That row is the only thing separating the "
        "materials from the operations, so this file cannot find the residue "
        "costs. Fix the parser rather than deleting this test.")


@pytest.fixture(scope="module")
def model():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import residue
    return residue


def leading_number(text: str, operation: str) -> float:
    """The residue figure at the front of a CR Impact cell.

    The cells read "5 CR", "25 CR per tier", "5 CR per tier of affix". The
    number is always first and the rest says what it is per.
    """
    match = re.match(r"\s*(-?\d+(?:\.\d+)?)", text)
    assert match, (
        f"the CR Impact cell for {operation!r} reads {text!r}, and no number "
        "could be read off the front of it. Either the sheet changed shape or "
        "the operation stopped costing a fixed amount.")
    return float(match.group(1))


def test_the_parser_found_the_operations(operations) -> None:
    """Without this a parser that matched nothing would make every comparison
    below vacuous, and the file would read as coverage while providing none."""
    assert len(operations) >= len(MIRRORED) + len(MITIGATIONS), (
        f"only {len(operations)} operation(s) were parsed out of the Crafting "
        f"sheet: {sorted(operations)}")


def test_every_mirrored_cost_is_named_by_the_sheet(operations) -> None:
    """An operation renamed in the workbook would leave the constant mirroring
    nothing, and the comparison below would silently have nothing to compare."""
    missing = sorted(name for name in MIRRORED if name not in operations)
    assert not missing, (
        f"the Crafting sheet has no operation called {missing}, and "
        "sim/cataclysm_sim/residue.py mirrors its residue cost. Either it was "
        "renamed in the workbook or removed.")


def test_every_mirrored_cost_matches_the_sheet(operations, model) -> None:
    """The comparison this file exists for."""
    wrong = []
    for operation, constant in MIRRORED.items():
        stated = leading_number(operations[operation], operation)
        mirrored = getattr(model, constant)
        if stated != pytest.approx(mirrored):
            wrong.append(f"{operation}: sheet {stated:g}, "
                         f"residue.{constant} {mirrored:g}")
    assert not wrong, (
        "sim/cataclysm_sim/residue.py and the Crafting sheet of "
        "docs/All_Things_Cataclysm.xlsx disagree about what a craft adds to an "
        "item's residue: " + "; ".join(wrong)
        + ". Every Consumption Threshold is derived from these, and every test "
          "in sim/tests/test_residue.py would still pass with the wrong ones, "
          "because they check the threshold's share of a path rather than the "
          "path. The workbook is authoritative; change the constant.")


def test_purified_essence_still_halves_the_total(operations, model) -> None:
    """The derivation's claim that one use clears every threshold rests on this
    being a halving. The sheet writes it as a percentage rather than a figure,
    so it is read differently from the five above."""
    stated = operations["CR Cleanse (Partial)"]
    assert "50%" in stated, (
        f"the CR Cleanse (Partial) operation now reads {stated!r} rather than "
        "removing 50% of accumulated residue. sim/cataclysm_sim/residue.py "
        "asserts on import that one use of it clears the Consumption Threshold "
        "at every tier, and that assertion is built on the halving.")
    assert model.PURIFIED_ESSENCE_REMOVES == pytest.approx(0.5)


def test_the_derivation_uses_no_residue_reducing_material(model) -> None:
    """The threshold is what the mitigating materials exist to be spent
    against, so counting one in the path it is derived from would make it
    define itself.

    Checked by arithmetic rather than by reading the code: the cheapest path
    must equal what the five ADDING costs produce, with nothing taken off.
    """
    for tier in range(1, 9):
        start, cheapest = model.cheapest_path(tier)
        halved = cheapest * (1.0 - model.PURIFIED_ESSENCE_REMOVES)
        assert cheapest > halved, (
            f"tier {tier} starting from {start}: the cheapest path already has "
            "residue taken off it")
        assert model.consumption_threshold(tier) > halved, (
            f"at tier {tier} the threshold {model.consumption_threshold(tier)} "
            f"is at or below the {halved:.0f} that one Purified Essence leaves, "
            "which would mean the path had mitigation counted into it already")
