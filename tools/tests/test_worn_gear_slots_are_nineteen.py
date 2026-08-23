"""How many gear slots a character wears, checked from the document to the C++.

WHY THIS EXISTS. Issue #874. `docs/Save_System_Design.md` said a character record
holds "all 18 slots" and there are nineteen. Nothing failed, because nothing
reads the number: `UCataclysmEquipmentComponent::SlotCount` is the length of the
`ECataclysmGearSlot` enum rather than a written-down figure. So the document was
simply wrong, and would have stayed wrong until someone sized a record or wrote a
migration from it and came up one slot short.

WHY IT IS CHECKED FROM PYTHON RATHER THAN BY A `static_assert`. Continuous
integration compiles no C++ at all, so an assertion beside the enum would never
run on a pull request. A test that reads the enum out of the source as text does.
This is the same arrangement
`tools/tests/test_carried_inventory_is_forty_eight_slots.py` uses for the carried
bag, and that file's header says why; this is the worn half, which had no
equivalent.

WHAT IS PINNED AND WHAT IS NOT. Nineteen is not asserted anywhere below. The
number is read out of the design document and checked against the enum's length,
so changing the slots in both together passes and changing one alone fails.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SAVE_SYSTEM = REPO_ROOT / "docs" / "Save_System_Design.md"
EQUIPMENT = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
             / "CataclysmEquipmentComponent.h")

#: The sentence the count is read from, in the Character record list.
STATED = re.compile(r"Equipped items, all (\d+) slots")

#: One entry of the gear slot enum. `Count` is the enum's own length marker and
#: is not a slot, so it is excluded by name below.
ENTRY = re.compile(r"^\s*(\w+)\s+UMETA\(", re.MULTILINE)

NOT_A_SLOT = {"Count"}


def text(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def slots_in_the_enum() -> list[str]:
    source = text(EQUIPMENT)
    start = source.find("enum class ECataclysmGearSlot")
    assert start != -1, (
        f"{EQUIPMENT.name} no longer declares ECataclysmGearSlot. If it moved, "
        "point this test at the new file rather than deleting it.")
    end = source.find("};", start)
    assert end != -1, "the gear slot enum has no closing brace"

    named = [name for name in ENTRY.findall(source[start:end])
             if name not in NOT_A_SLOT]
    assert named, "no entries were read out of the gear slot enum"
    return named


@pytest.fixture(scope="module")
def stated_in_the_document() -> int:
    found = STATED.search(text(SAVE_SYSTEM))
    assert found, (
        "the Character record list in docs/Save_System_Design.md no longer says "
        "'Equipped items, all N slots'. If the wording changed deliberately, "
        "update this pattern rather than deleting the test.")
    return int(found.group(1))


def test_the_document_counts_the_slots_the_enum_declares(
        slots_in_the_enum, stated_in_the_document) -> None:
    assert stated_in_the_document == len(slots_in_the_enum), (
        f"docs/Save_System_Design.md says a character wears "
        f"{stated_in_the_document} slots; ECataclysmGearSlot in "
        f"{EQUIPMENT.name} declares {len(slots_in_the_enum)}: "
        f"{slots_in_the_enum}")


def test_the_slots_are_the_ones_the_gear_panel_and_the_item_bases_expect(
        slots_in_the_enum) -> None:
    """The eight rings and two weapons are the reason the count is not eleven,
    and they are the part a reader miscounts. Recorded here so a change to
    either is deliberate."""
    rings = [name for name in slots_in_the_enum if name.startswith("Ring")]
    weapons = [name for name in slots_in_the_enum if name.startswith("Weapon")]
    assert len(rings) == 8, (
        f"a character wears {len(rings)} ring slots: {rings}")
    assert len(weapons) == 2, (
        f"a character wears {len(weapons)} weapon slots: {weapons}")
