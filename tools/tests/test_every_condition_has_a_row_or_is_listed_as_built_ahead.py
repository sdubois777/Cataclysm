"""Every condition the generator knows is named by an effect row, or is listed here
as built ahead of its row on purpose.

WHY THIS EXISTS. Issue #1650. A condition is cheap to add and lands with its own
tests; its data row usually needs the design workbook, which one session may
edit at a time; and nothing anywhere failed when the row never followed. Six of
the twenty conditions were named by no row on 2026-09-12, from at least two
sessions, and a session finding one cold could not tell "built ahead on
purpose" from "forgotten". This keeps the two apart: a session that lands a
mechanism before its row adds the name to `BUILT_AHEAD_OF_THEIR_ROWS` with the
reason, and one that forgets is told.

IMPORTED, NOT GREPPED. `CONDITIONS` is a dict whose keys are the names; a
pattern reading snake_case words out of the file's text also collects stat
names and sentence fragments from the comments beside them, which is how a
count of 33 was once reported for a dict of 20. Reading the keys gives the
number.

THE SAME SHAPE AS THE COVERAGE PINS in
`tools/tests/test_enchantment_effects_match_the_row_text.py`: an expected set,
held both ways. A name that gains a row must leave the list, so the list cannot
rot into an allowance that excuses everything.

THE CONTROL. `target_within_metres` is named by several enchantment effect rows.
If it ever reads as unused, the reader is broken and the rest of this file
means nothing.
"""

from __future__ import annotations

import csv
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import generate_datatables as gen  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EFFECT_TABLES = (REPO_ROOT / "game" / "Data" / "EnchantmentEffects.csv",
                 REPO_ROOT / "game" / "Data" / "PassiveEffects.csv")

#: Conditions the generator knows that no effect row names today, each with
#: why it was landed ahead of its row. Measured on `origin/development` at
#: 1aacbb32, 2026-09-16: 38 conditions, 35 named by a row, these three not.
#: `enemies_hit_at_least` was added and left again the same day, when
#: Sundering's row was authored: measured on branch
#: `feat/ritualist-nodes-say-minion`, 39 conditions, 36 named by a row, these
#: three not.
#: Measured on branch `feat/six-enumerators-conditions-and-scales`,
#: 2026-09-18: 45 conditions, 39 named by a row, these six not.
#: `metres_moved_before_attack` left on 2026-09-16, when Headlong's second
#: clause took a row: measured on branch `feat/ravager-fervour-per-enemy-hit`,
#: 39 conditions, 37 named by a row, these two not.
#: FOUR LEFT ON 2026-09-18, when the thirteen enchantment rows were written:
#: `opponent_within_metres`, `moved_within_seconds`, `class_resource_above` and
#: `energy_shield_above_zero` each now have the row whose sentence asked for
#: them. Measured on branch `data/thirteen-enchantment-rows`: 45 conditions,
#: 43 named by a row, these two not.
#: ONE JOINED ON 2026-09-18, `seconds_after_striking_a_boss`, the window a Boss
#: strike opens: measured on branch `feat/a-boss-strike-opens-a-cooldown-window`,
#: 47 conditions, 44 named by a row, these three not. The count rose by two and
#: only one of them is the name above; the other is Set Against It's points
#: threshold, which never passed through this list because its rows landed in
#: the same change as the condition.
#: AND IT LEFT ON 2026-09-23, when its row landed with issue #1994.
BUILT_AHEAD_OF_THEIR_ROWS = {
    # Landed with the movement conditions for the dungeon-modifier work; the
    # enchantment rows that want it are among the 388 counted in issue #1815
    # and wait on the workbook.
    "not_attacked_for_seconds",
    # Landed with the state conditions in pull request #1803 for the Demonic
    # trees; the row that reads it is in the same queue.
    "target_carries_void_splinter",
    # Landed with the dungeon floor rule `Famine_Desperate_Measures`, which asks
    # it from C++ rather than through a row. The enchantment "Take 10%-40% more
    # damage when on low mana" is to take a row on it; it leaves this list then.
    "mana_below",
}

#: A condition several enchantment effect rows name. The control.
KNOWN_USED = "target_within_metres"


def conditions_named_by_a_row() -> set[str]:
    used: set[str] = set()
    for table in EFFECT_TABLES:
        with table.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                name = (row.get("Condition") or "").strip().lower()
                if name:
                    used.add(name)
    return used


def test_the_reader_sees_a_condition_that_rows_do_name() -> None:
    """The control: a reader finding nothing would make every name look unused."""
    assert KNOWN_USED in gen.CONDITIONS, f"{KNOWN_USED} is no longer a condition"
    assert KNOWN_USED in conditions_named_by_a_row(), (
        f"{KNOWN_USED} is named by effect rows and was not read as used; the "
        f"reader is broken, so the check below cannot be trusted")


def test_every_condition_is_named_by_a_row_or_listed_as_built_ahead() -> None:
    unused = set(gen.CONDITIONS) - conditions_named_by_a_row()
    forgotten = unused - BUILT_AHEAD_OF_THEIR_ROWS
    assert not forgotten, (
        f"no row in EnchantmentEffects.csv or PassiveEffects.csv names "
        f"{sorted(forgotten)}. A condition nothing names cannot be reached in "
        f"play. Author its row, or add the name to BUILT_AHEAD_OF_THEIR_ROWS in "
        f"this file with the reason it landed first. Issue #1650.")


def test_the_built_ahead_list_holds_nothing_a_row_now_names() -> None:
    """A name that gained its row must leave the list, or the list rots."""
    now_used = BUILT_AHEAD_OF_THEIR_ROWS & conditions_named_by_a_row()
    assert not now_used, (
        f"{sorted(now_used)} are listed as built ahead of their rows and a row "
        f"now names them; delete them from BUILT_AHEAD_OF_THEIR_ROWS.")
    unknown = BUILT_AHEAD_OF_THEIR_ROWS - set(gen.CONDITIONS)
    assert not unknown, (
        f"{sorted(unknown)} are listed here and are not conditions the generator "
        f"knows; delete them from BUILT_AHEAD_OF_THEIR_ROWS.")
