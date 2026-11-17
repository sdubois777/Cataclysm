"""The game's Consumption Threshold for each difficulty tier is the design document's.

WHY THIS EXISTS. Issue #1251. `docs/Cataclysm_GDD_v2.md`, "The Consumption Threshold, one per difficulty tier", gives
eight numbers, derived there from the drop and Forge figures; `UCataclysmEquipmentComponent::ConsumptionThresholdFor`
in `game/Source/Cataclysm/Items/CataclysmEquipmentComponent.cpp` holds a copy, one per line, which the character sheet
shows beside Worn Residue. Two copies of one table drift unless something compares them. Continuous integration
compiles no C++, so the comparison is made here, reading both as text.
"""

from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
DESIGN = REPO / "docs" / "Cataclysm_GDD_v2.md"
SOURCE = REPO / "game" / "Source" / "Cataclysm" / "Items" / "CataclysmEquipmentComponent.cpp"

#: The heading sentence the design's table follows, quoted as the document writes it.
DESIGN_LEAD = "**The Consumption Threshold, one per difficulty tier.**"

#: A row of that table: `| 3 | 2,900 | 3,415 |`.
DESIGN_ROW = re.compile(r"^\|\s*(\d)\s*\|\s*([\d,]+)\s*\|\s*[\d,]+\s*\|\s*$")

#: A line of the C++ table: `2900.0f, // tier 3`.
SOURCE_ROW = re.compile(r"^\s*(\d+(?:\.\d+)?)f,\s*// tier (\d)\s*$")


def design_thresholds() -> dict[int, float]:
    text = DESIGN.read_text(encoding="utf-8")
    start = text.index(DESIGN_LEAD)
    found: dict[int, float] = {}
    for line in text[start:].splitlines()[1:]:
        row = DESIGN_ROW.match(line.strip())
        if row:
            found[int(row.group(1))] = float(row.group(2).replace(",", ""))
        elif found and line.strip() and not line.strip().startswith("|"):
            break
    return found


def source_thresholds() -> dict[int, float]:
    text = SOURCE.read_text(encoding="utf-8")
    block = text[text.index("constexpr float ByTier[] = {"):]
    block = block[:block.index("};")]
    return {int(row.group(2)): float(row.group(1))
            for row in map(SOURCE_ROW.match, block.splitlines()) if row}


def test_the_design_and_the_game_hold_the_same_eight_thresholds() -> None:
    design = design_thresholds()
    game = source_thresholds()
    assert sorted(design) == list(range(1, 9)), f"the design's table lists tiers {sorted(design)}, not 1 to 8"
    assert sorted(game) == list(range(1, 9)), f"the C++ table lists tiers {sorted(game)}, not 1 to 8"
    differing = {tier: (design[tier], game[tier]) for tier in design if design[tier] != game[tier]}
    assert not differing, f"tier: (design, game) differ: {differing}"


def test_the_readers_find_a_changed_tier() -> None:
    """The CONTROL: a copy of the C++ table with one tier changed is read as different, so the comparison above can
    fail and does not pass by reading nothing."""
    changed = {tier: value + (1.0 if tier == 3 else 0.0) for tier, value in source_thresholds().items()}
    assert changed != design_thresholds()
    assert len(design_thresholds()) == 8 and len(source_thresholds()) == 8
