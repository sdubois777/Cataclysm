"""The carried inventory's size, checked from the design document to the C++.

WHY THIS EXISTS. Issue #714 built `UCataclysmInventoryComponent`, and its
capacity is the one number in it that the design fixes rather than leaves to be
tuned in play: "48 slots, four rows of twelve, and nothing increases it", from
the Storage section of `docs/Cataclysm_GDD_v2.md` and the decision of 2026-08-05
in `docs/DECISIONS.md`.

WHY IT IS CHECKED FROM PYTHON RATHER THAN BY A `static_assert`. Continuous
integration compiles no C++ at all, so an assertion beside the constant would
never run on a pull request. A test that reads the number out of the source as
text does. That is the same arrangement
`tools/tests/test_difficulty_tier_matches_the_model.py` uses for the difficulty
tier range, and its header says why.

WHAT IS PINNED AND WHAT IS NOT. The number is not asserted as 48 anywhere below.
It is read out of the design document, and everything else -- the three C++
constants, the grid arithmetic, the save system's record size -- is checked
against whatever that sentence says. So changing 48 in the design and in the
engine together passes, which is right, because the decision calls 48 "a tuning
value". Changing it in one place and not the others fails, and so does losing the
rule that nothing increases it.

WHAT IS NOT CHECKED HERE. Anything about how the grid is drawn, which is issue
#49, and anything about the stash, which is a different container with its own
600 slots and needs issue #529 to persist.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SAVE_SYSTEM = REPO_ROOT / "docs" / "Save_System_Design.md"
INVENTORY_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
                    / "CataclysmInventoryComponent.h")

#: The sentence the size is read from. Every other check reads its numbers, so
#: the count and the grid shape live in one place.
SIZE = re.compile(
    r"The carried inventory is (\d+) slots, (\w+) rows of (\w+), "
    r"and nothing increases it")

#: Written-out numbers the sentence above uses for the grid.
WORDS = {"two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
         "eight": 8, "ten": 10, "twelve": 12, "sixteen": 16, "twenty": 20}


def flat(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


def constant(name: str) -> int:
    """The value of a `static constexpr int32 <name> = <number>;` line."""
    if not INVENTORY_HEADER.is_file():
        pytest.fail(
            f"{INVENTORY_HEADER.relative_to(REPO_ROOT)} does not exist. The "
            f"carried inventory is issue #714; if the component was renamed, "
            f"rename it here too.")

    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        INVENTORY_HEADER.read_text(encoding="utf-8"))
    if match is None:
        pytest.fail(
            f"{INVENTORY_HEADER.relative_to(REPO_ROOT)} has no "
            f"'static constexpr int32 {name} = <number>;' line. If it became an "
            f"expression such as 'Rows * Columns', write it as a number again: "
            f"continuous integration compiles no C++, so this text read is the "
            f"only thing that checks the capacity on a pull request.")
    return int(match.group(1))


@pytest.fixture(scope="module")
def designed() -> tuple[int, int, int]:
    """The slot count, rows and columns, read out of the design document."""
    match = SIZE.search(flat(GDD))
    if match is None:
        pytest.fail(
            "The Storage section of docs/Cataclysm_GDD_v2.md no longer says "
            "'The carried inventory is N slots, R rows of C, and nothing "
            "increases it'. That sentence is where this test reads the size "
            "from, and the decision of 2026-08-05 in docs/DECISIONS.md is what "
            "put it there.")

    count = int(match.group(1))
    rows = WORDS.get(match.group(2).lower())
    columns = WORDS.get(match.group(3).lower())
    assert rows is not None, f"unrecognised row count word: {match.group(2)!r}"
    assert columns is not None, \
        f"unrecognised column count word: {match.group(3)!r}"
    return count, rows, columns


def test_the_grid_multiplies_out_to_the_slot_count(designed) -> None:
    """Four rows of twelve is 48. The document has to be self-consistent first,
    or there is no single number for the engine to be checked against."""
    count, rows, columns = designed
    assert rows * columns == count, (
        f"docs/Cataclysm_GDD_v2.md says {rows} rows of {columns}, which is "
        f"{rows * columns}, but calls the inventory {count} slots.")


def test_the_engine_carries_what_the_design_says(designed) -> None:
    count, rows, columns = designed

    assert constant("SlotCount") == count, (
        f"SlotCount in game/Source/Cataclysm/Items/CataclysmInventoryComponent.h "
        f"is {constant('SlotCount')} and docs/Cataclysm_GDD_v2.md says {count}. "
        f"The design calls the number a tuning value and calls the rule that it "
        f"does not change; move both together or a character carries a "
        f"different amount than the design says it does.")
    assert constant("Rows") == rows
    assert constant("Columns") == columns


def test_the_engines_own_grid_multiplies_out(designed) -> None:
    """The `static_assert` in the header says this too, and nothing on a pull
    request compiles it."""
    assert constant("Rows") * constant("Columns") == constant("SlotCount")


def test_the_save_record_holds_the_same_number(designed) -> None:
    """`docs/Save_System_Design.md` sizes a character record from this count. It
    is prose today, and issue #529 will build against it."""
    count, _, _ = designed
    text = flat(SAVE_SYSTEM)

    assert f"{count} inventory slots" in text, (
        f"docs/Save_System_Design.md does not mention '{count} inventory "
        f"slots'. It sizes an ordinary character record from the carried "
        f"inventory, so the two documents have to agree on how big one is.")


def test_nothing_grants_a_slot(designed) -> None:
    """The rule, not the number. This is the half the decision of 2026-08-05
    calls fixed, and an empire node or affix granting slots would contradict it
    without changing any number this file reads."""
    text = flat(GDD)

    assert ("No empire upgrade node grants slots, no affix grants slots, and "
            "no city upgrade grants slots.") in text, (
        "The Storage section of docs/Cataclysm_GDD_v2.md no longer rules out "
        "every source of extra inventory slots. The decision of 2026-08-05 in "
        "docs/DECISIONS.md is that the size is a tuning value and that nothing "
        "increasing it is the rule.")


def test_an_item_that_does_not_fit_stays_on_the_floor() -> None:
    """What a full inventory means, which is what makes the engine's refusal
    correct rather than a limitation. Decision of 2026-08-14, issue #323."""
    text = flat(GDD)

    assert ("an item that will not fit stays on the floor") in text, (
        "docs/Cataclysm_GDD_v2.md no longer says an item that will not fit "
        "stays on the floor. UCataclysmInventoryComponent::AddItem answers "
        "INDEX_NONE rather than making room, and this sentence is why.")
