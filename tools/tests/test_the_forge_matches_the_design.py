"""The forge's upgrade numbers in the game, against the design and the simulation.

WHY THIS EXISTS. The owner ruled on 2026-11-19 to build the Cataclysmic Forge's
upgrade: raising an item one gear level with two upgrade stones, and combining
two stones of a level into one of the next. `UCataclysmForge`
(`game/Source/Cataclysm/Items/CataclysmForge.h`) holds its numbers as
constants. Each one is stated somewhere else first, and a copy drifts:

  1. two stones per upgrade -- the design workbook's Crafting sheet, "Uses two
     +x upgrade stones", and the operations row of `docs/Cataclysm_GDD_v2.md`;
  2. 25 residue per level -- the workbook's "25 CR per tier", and
     `sim/cataclysm_sim/residue.py`'s `UPGRADE_LEVEL_RESIDUE_PER_LEVEL`, which
     the Consumption Thresholds are derived from;
  3. one day per level -- the workbook's "1 day per tier";
  4. the time penalty's 100 -- the design's "Craft time penalty in days:
     `CR / 100`, rounded down";
  5. the stones' rows -- `game/Data/CraftingMaterials.csv` has
     `Material_Upgrade_Stone_1` to `_10`, each raising an item to its own
     number, which `UCataclysmForge::StoneFor` names.

AND THE DESIGN'S OWN TABLE. "Gear Leveling" counted one +1 stone per level until
2026-11-19 (1, 3, 7 ... 1,023) against the three places that say two are
consumed. The coordinating session ruled the table is arithmetic from them; it
now reads two per level with 2:1 combining, 2^(N+1) - 2 +1 stones to reach +N,
and this checks every row.

WHAT IS NOT CHECKED. That the game does what the constants say: the
`Cataclysm.Forge.` automation tests do.
"""
from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items" / "CataclysmForge.h"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items" / "CataclysmForge.cpp"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
MATERIALS = REPO_ROOT / "game" / "Data" / "CraftingMaterials.csv"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: The workbook's operation row, by the name its first cell carries.
UPGRADE_ROW = "Upgrade Item Level (1 Tier)"


def constant(name: str) -> float:
    """A `static constexpr` number in `UCataclysmForge`'s header."""
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(rf"static constexpr (?:int32|float) {name} = (-?\d+(?:\.\d+)?)f?;", text)
    assert match, f"{HEADER.name} declares no number {name}; it may have been renamed."
    return float(match.group(1))


def test_two_stones_one_day_and_the_penalty_are_the_designs() -> None:
    assert constant("StonesPerUpgrade") == 2, "an upgrade consumes two stones (the operations row)"
    assert constant("StonesPerCombine") == 2, "two +1 stones combine into a +2 (Gear Leveling)"
    assert constant("BaseDaysPerLevel") == 1, "one day per level (the workbook's '1 day per tier')"
    assert constant("ResiduePerPenaltyDay") == 100, "the design's craft time penalty is CR / 100"
    gdd = GDD.read_text(encoding="utf-8")
    assert "Craft time penalty in days: `CR / 100`, rounded down" in gdd, (
        "docs/Cataclysm_GDD_v2.md no longer states the time penalty this checks the 100 against")
    assert "| Upgrade Item Level | Upgrade Stone | 25 per tier | 1 per tier, maximum 10 | Consumes two upgrade " \
           "stones |" in gdd, "the design's operations row for an upgrade changed; read it again"


def test_the_residue_per_level_is_the_simulations() -> None:
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import residue

    assert constant("ResiduePerLevel") == residue.UPGRADE_LEVEL_RESIDUE_PER_LEVEL, (
        "UCataclysmForge::ResiduePerLevel and residue.py's UPGRADE_LEVEL_RESIDUE_PER_LEVEL disagree; the "
        "Consumption Thresholds are derived from the simulation's")


def test_the_workbook_row_says_two_stones_25_residue_and_a_day() -> None:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")
    book = openpyxl.load_workbook(WORKBOOK, read_only=True, data_only=True)
    rows = [row for row in book["Crafting"].iter_rows(values_only=True)
            if row and str(row[0] or "").strip() == UPGRADE_ROW]
    assert len(rows) == 1, f"the Crafting sheet holds {len(rows)} rows named {UPGRADE_ROW!r}; expected one"
    cells = [str(cell or "").strip() for cell in rows[0]]
    joined = " | ".join(cells)
    assert re.search(r"\b25 CR per tier\b", joined), f"the row no longer says '25 CR per tier': {joined}"
    assert re.search(r"\b1 day per tier\b", joined), f"the row no longer says '1 day per tier': {joined}"
    assert "two" in joined.lower(), f"the row no longer says two stones are used: {joined}"


def test_every_stone_row_exists_and_raises_to_its_own_level() -> None:
    with MATERIALS.open(encoding="utf-8", newline="") as handle:
        rows = {row["Name"]: row for row in csv.DictReader(handle)}
    for level in range(1, 11):
        name = f"Material_Upgrade_Stone_{level}"
        assert name in rows, f"{MATERIALS.name} has no {name}, which UCataclysmForge::StoneFor names"
        assert int(rows[name]["UpgradeLevel"]) == level, f"{name} raises an item to {rows[name]['UpgradeLevel']}"
    source = SOURCE.read_text(encoding="utf-8")
    assert 'TEXT("Material_Upgrade_Stone_%d")' in source, (
        "UCataclysmForge::StoneFor no longer builds the rows' names; read it against CraftingMaterials.csv")


def test_the_gear_leveling_table_counts_two_stones_per_level() -> None:
    gdd = GDD.read_text(encoding="utf-8")
    start = gdd.index("## **Gear Leveling**")
    section = gdd[start:gdd.index("## **", start + 5)]
    rows = re.findall(r"^\| \+(\d+) \| ([\d,]+) \|$", section, flags=re.MULTILINE)
    assert [int(level) for level, _ in rows] == list(range(1, 11)), (
        f"the Gear Leveling table's rows are {rows}; expected +1 to +10 in order")
    for level, stones in rows:
        expected = 2 ** (int(level) + 1) - 2
        assert int(stones.replace(",", "")) == expected, (
            f"Gear Leveling says +{level} takes {stones} +1 stones; two stones per level with 2:1 combining "
            f"takes {expected}")
