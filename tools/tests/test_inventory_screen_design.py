"""The inventory screen design's arithmetic, checked against the real item bases.

WHY THIS EXISTS. `docs/Inventory_Screen_Design.md` picks a grid of 240 cells and
justifies it with a calculation: the average item footprint over every base in
`game/Data/ItemBases.csv` is 4.24 cells, so the 48 items the Storage section of
`docs/Cataclysm_GDD_v2.md` tuned need about 203 cells packed perfectly and about
239 at a realistic packing efficiency. Issue #854.

WHAT WOULD GO WRONG WITHOUT IT. Every number in that argument depends on data
that changes. Adding six more two-handed weapon bases raises the average
footprint and 240 cells quietly stops holding 48 items, with nothing anywhere
saying so. Adding a new equipment slot leaves an item with no footprint at all.
Changing 48 in the Storage section leaves the grid sized for a capacity the
design no longer asks for. None of those produce an error; they produce a
document whose reasoning has stopped being true.

WHAT IS PINNED AND WHAT IS NOT. Nothing here asserts 240, or 4.24, or 48. Every
figure is read out of a document and checked against what the data comes to, so
changing the design deliberately and consistently passes, and changing one number
without the others fails.

WHY THE PROSE IS MATCHED AGAINST FLATTENED TEXT. The document is wrapped, so a
sentence stating a figure is regularly split across two lines and a pattern
written with single spaces in it does not match. The table rows are matched
against the raw text instead, because those depend on the line starts.

WHAT THIS DELIBERATELY DOES NOT CHECK. Anything about the screen as built. The
game still draws 48 one-cell slots and will until issue #855 lands; this file
checks that the design is coherent, not that it is implemented.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN = REPO_ROOT / "docs" / "Inventory_Screen_Design.md"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
ITEM_BASES = REPO_ROOT / "game" / "Data" / "ItemBases.csv"

#: The grid, from the sentence that states it.
GRID = re.compile(
    r"\*\*(\d+) cells wide by (\d+) cells high, which is (\d+) cells\.\*\*")

#: One row of the footprint table: name, width, height, and the cell count.
FOOTPRINT_ROW = re.compile(
    r"^\|\s*([^|]+?)\s*\|\s*(\d+) by (\d+)\s*\|\s*(\d+)\s*\|$", re.MULTILINE)

#: The capacity argument.
AVERAGE = re.compile(
    r"average footprint over all (\d+) item bases is ([\d.]+) cells, so "
    r"(\d+) items need (\d+) cells packed perfectly")
PACKING = re.compile(
    r"at a realistic (\d+) per cent, \d+ items need about (\d+)")

#: The Storage section's tuned item count, read the same way
#: `test_carried_inventory_is_forty_eight_slots.py` reads it, so the design
#: cannot be sized for a capacity the design document no longer states.
STORAGE_SIZE = re.compile(r"The carried inventory is (\d+) slots,")

#: Footprint table rows that are not a Slot value in ItemBases.csv. Weapons are
#: split three ways because the sheet's Slot column calls all fourteen "Weapon".
NOT_A_SLOT_NAME = {
    "One-handed weapon", "Shield", "Two-handed weapon", "Crafting material",
}


def text(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def flatten(source: str) -> str:
    """Every run of whitespace as one space, so a wrapped sentence matches."""
    return re.sub(r"\s+", " ", source)


@pytest.fixture(scope="module")
def raw() -> str:
    """The document as written, for the patterns that depend on line starts."""
    return text(DESIGN)


@pytest.fixture(scope="module")
def prose(raw: str) -> str:
    return flatten(raw)


@pytest.fixture(scope="module")
def footprints(raw: str) -> dict[str, tuple[int, int, int]]:
    """Name to (width, height, stated cells), from the footprint table."""
    found = {}
    for name, wide, high, cells in FOOTPRINT_ROW.findall(raw):
        found[name] = (int(wide), int(high), int(cells))
    assert found, (
        "no footprint table rows were found in "
        f"{DESIGN.name}. The rows are expected to read '| Slot | W by H | N |'. "
        "If the table was reformatted, fix this pattern rather than deleting "
        "the test.")
    return found


@pytest.fixture(scope="module")
def bases() -> list[dict]:
    if not ITEM_BASES.is_file():
        pytest.skip(f"{ITEM_BASES.name} is not present")
    with ITEM_BASES.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def footprint_of(row: dict, footprints: dict) -> tuple[int, int]:
    """The width and height a base occupies, resolving the three weapon cases."""
    slot = row["Slot"].strip()
    if slot != "Weapon":
        wide, high, _ = footprints[slot]
        return wide, high
    if row["WeaponType"].strip() == "Shield":
        wide, high, _ = footprints["Shield"]
    elif int(row["Hands"]) == 2:
        wide, high, _ = footprints["Two-handed weapon"]
    else:
        wide, high, _ = footprints["One-handed weapon"]
    return wide, high


def total_cells(bases: list[dict], footprints: dict) -> int:
    return sum(wide * high
               for row in bases
               for wide, high in [footprint_of(row, footprints)])


# --------------------------------------------------------------------------
# The grid
# --------------------------------------------------------------------------

def test_the_grid_multiplies_out(prose: str) -> None:
    stated = GRID.search(prose)
    assert stated, (
        f"{DESIGN.name} no longer states the grid in the form "
        "'**20 cells wide by 12 cells high, which is 240 cells.**'")
    wide, high, cells = (int(g) for g in stated.groups())
    assert wide * high == cells, (
        f"the design says {wide} wide by {high} high, which is {wide * high} "
        f"cells, and then says {cells}")


def test_the_grid_is_tall_enough_for_the_tallest_item(prose, footprints) -> None:
    """A two-handed weapon is the tallest thing carried. A grid shorter than it
    could not hold one at all, which is the constraint that set the row count."""
    high = int(GRID.search(prose).group(2))
    tallest = max(height for _, height, _ in footprints.values())
    assert high >= tallest, (
        f"the grid is {high} rows high and the tallest footprint is {tallest}, "
        "so that item could never be picked up")


def test_every_footprint_multiplies_out(footprints) -> None:
    wrong = [f"{name}: {wide} by {high} is {wide * high}, table says {cells}"
             for name, (wide, high, cells) in footprints.items()
             if wide * high != cells]
    assert not wrong, (
        f"the footprint table in {DESIGN.name} states a cell count that is not "
        f"its width times its height: {'; '.join(wrong)}")


def test_the_two_handed_footprint_is_the_one_the_owner_decided(footprints) -> None:
    """2 wide by 6 high, decided by the project owner on 2026-08-23 in issue
    #855. Everything else in the table was derived; this one was given."""
    assert footprints["Two-handed weapon"][:2] == (2, 6), (
        "issue #855 records the project owner deciding a two-handed weapon is "
        f"2 wide by 6 high; the design says "
        f"{footprints['Two-handed weapon'][0]} by "
        f"{footprints['Two-handed weapon'][1]}")


# --------------------------------------------------------------------------
# The footprint table against the real item bases
# --------------------------------------------------------------------------

def test_every_item_base_has_a_footprint(bases, footprints) -> None:
    """A slot added to the item bases with no footprint in the design is an item
    that cannot be placed in the bag."""
    slots = {row["Slot"].strip() for row in bases}
    covered = set(footprints) - NOT_A_SLOT_NAME
    missing = sorted(slots - covered - {"Weapon"})
    assert not missing, (
        f"game/Data/ItemBases.csv has slots the footprint table in "
        f"{DESIGN.name} does not give a size: {missing}")


def test_the_footprint_table_invents_no_slot(bases, footprints) -> None:
    """The other direction, so a slot renamed in the data leaves a row behind
    that quietly stops applying to anything."""
    slots = {row["Slot"].strip() for row in bases}
    named = set(footprints) - NOT_A_SLOT_NAME
    invented = sorted(named - slots)
    assert not invented, (
        f"the footprint table in {DESIGN.name} gives a size for slots that are "
        f"not in game/Data/ItemBases.csv: {invented}")


def test_the_stated_base_count_is_the_real_one(prose, bases) -> None:
    stated = AVERAGE.search(prose)
    assert stated, (
        f"{DESIGN.name} no longer states the average footprint in the form "
        "'average footprint over all 55 item bases is 4.24 cells, so 48 items "
        "need 203 cells packed perfectly'")
    assert int(stated.group(1)) == len(bases), (
        f"the design says {stated.group(1)} item bases; "
        f"game/Data/ItemBases.csv has {len(bases)}")


def test_the_stated_average_footprint_is_what_the_bases_come_to(
        prose, bases, footprints) -> None:
    """THE LOAD-BEARING NUMBER. The grid size was solved backwards from it, so a
    change to the item bases that moves it invalidates the grid."""
    said = float(AVERAGE.search(prose).group(2))

    total = total_cells(bases, footprints)
    real = total / len(bases)

    assert abs(real - said) < 0.01, (
        f"the design says the average footprint is {said} cells; the footprint "
        f"table applied to game/Data/ItemBases.csv comes to {real:.2f} "
        f"({total} cells over {len(bases)} bases)")


def test_the_capacity_arithmetic_holds(prose, bases, footprints) -> None:
    """48 items at the average footprint, at the stated packing efficiency, has
    to fit in the grid. This is the whole argument for 240 cells."""
    stated = AVERAGE.search(prose)
    packing = PACKING.search(prose)
    assert packing, (
        f"{DESIGN.name} no longer states a packing efficiency in the form "
        "'at a realistic 85 per cent, 48 items need about 239'")

    items = int(stated.group(3))
    perfect_said = int(stated.group(4))
    efficiency = int(packing.group(1)) / 100.0
    with_slack_said = int(packing.group(2))

    average = total_cells(bases, footprints) / len(bases)
    perfect = items * average
    with_slack = perfect / efficiency

    assert abs(perfect - perfect_said) < 2, (
        f"the design says {items} items need {perfect_said} cells packed "
        f"perfectly; at {average:.2f} cells each that is {perfect:.0f}")
    assert abs(with_slack - with_slack_said) < 3, (
        f"the design says {items} items need about {with_slack_said} cells at "
        f"{efficiency:.0%} packing; the arithmetic gives {with_slack:.0f}")

    cells = int(GRID.search(prose).group(3))
    assert cells >= with_slack, (
        f"the grid is {cells} cells and {items} items need about "
        f"{with_slack:.0f} at {efficiency:.0%} packing, so the grid no longer "
        "holds the capacity the Storage section tuned")


def test_the_design_carries_the_item_count_the_storage_section_states(
        prose) -> None:
    """The 48 the grid was sized for is not a number this design chose. It comes
    from the Storage section of the game design document, and if that changes
    this grid is sized for a capacity nothing asks for any more."""
    in_design = int(AVERAGE.search(prose).group(3))

    storage = STORAGE_SIZE.search(flatten(text(GDD)))
    assert storage, (
        "the Storage section of docs/Cataclysm_GDD_v2.md no longer says 'The "
        "carried inventory is N slots,'")
    in_storage = int(storage.group(1))

    assert in_design == in_storage, (
        f"{DESIGN.name} sizes the grid to hold {in_design} items; the Storage "
        f"section of docs/Cataclysm_GDD_v2.md says the bag holds {in_storage}")
