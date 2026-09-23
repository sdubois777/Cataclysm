"""Every scale source the generator knows is named by an effect row, or is listed
here as built ahead of its row on purpose.

WHY THIS EXISTS. Issue #1988. A scale source is the other half of the same
vocabulary as a condition: a condition decides WHETHER a bonus applies and a
scale source decides HOW BIG it is, both are read out of the same two tables by
the same generator, and both are cheap to add ahead of the data row that needs
them. `tools/tests/test_every_condition_has_a_row_or_is_listed_as_built_ahead.py`
has held the condition half since issue #1650. Nothing held this half.

WHAT THAT COST. On 2026-09-18 two scale sources landed with no row naming them,
`metres_to_target` and `seconds_stationary` -- the first time the project had
had any. Their rows followed the same day, so nothing shipped wrong, but that
was a schedule rather than a check: the condition half of the very same change
was caught by the existing check and had to list four names, and the scale half
was noticed only because somebody looked.

WHY A SEPARATE FILE RATHER THAN ONE FILE READING BOTH COLUMNS. Issue #1988 asked
for that to be decided rather than assumed. Two files, because this repository
names a test file after the invariant it holds, and a failure should name the
vocabulary that is wrong without the reader having to work out which half fired.
The condition file also carries a long comment history of measurements about
conditions; folding scales into it would put two histories under one heading.
The duplicated part is about fifteen lines of reader.

IMPORTED, NOT GREPPED. `SCALES` is a dict whose keys are the names. A pattern
reading snake_case words out of the file's text also collects stat names and
sentence fragments from the comments beside them, which is how a count of 33 was
once reported for a dict of 20 on the condition side.

THE CONTROL. `debuffs_carried` is named by fifteen effect rows. If it ever reads
as unused, the reader is broken and the rest of this file means nothing.

THE BUILT-AHEAD LIST STARTS EMPTY, AND THAT MAKES ONE TEST HERE VACUOUS TODAY.
`test_the_built_ahead_list_holds_nothing_a_row_now_names` passes over an empty
set without reading anything. It is here because the list will not stay empty:
the moment somebody lands a scale source ahead of its row they add a name, and
that test is what makes them take it out again when the row arrives. The test
that is doing work today is
`test_every_scale_source_is_named_by_a_row_or_listed_as_built_ahead`.

IT DID NOT STAY EMPTY FOR LONG. On 2026-09-23 the two combat clocks of issue
#1815 were listed, and left it with their rows in the same change; three more
scales from the same issue followed them.
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

#: Scale sources the generator knows that no effect row names today, each with
#: why it was landed ahead of its row.
#:
#: EMPTY, AND MEASURED EMPTY. On `origin/development` at `0e606610`, 2026-09-18:
#: 16 scale sources, 16 named by a row, none unnamed. The two that had no row
#: earlier that day, `metres_to_target` and `seconds_stationary`, gained theirs
#: with the thirteen enchantment rows.
#:
#: TWO JOINED ON 2026-09-23, the combat clocks for issue #1815, and LEFT with
#: their rows in the same change. THREE MORE JOINED THE SAME DAY for issue
#: #1815's section C, and left with theirs.
BUILT_AHEAD_OF_THEIR_ROWS: set[str] = set()

#: A scale source many effect rows name. The control.
KNOWN_USED = "debuffs_carried"

#: What the control expects to find, so a reader that returns a handful of names
#: rather than all of them is caught as well as one that returns none.
#: Measured with the figures above: 16 of 16 named.
#:
#: AND 17 OF 17 ON 2026-09-18, when `max_health` was added for
#: `Ravager_keystone_a_kC` Weight Bearing -- "1 Armor for every 10 maximum
#: health you have". It is named by that node's row in the same change that adds
#: it, so it never needed a place on the built-ahead list.
#:
#: AND 19 OF 19 ON 2026-09-23, when the two combat clocks of issue #1815 gained
#: their rows: `seconds_in_combat` and `seconds_out_of_combat`.
#:
#: AND 22 OF 22 THE SAME DAY, when section C's three gained their rows:
#: `target_debuffs`, `buffs_held` and `mana_held_percent`.
EXPECTED_NAMED_BY_A_ROW = 22


def scales_named_by_a_row() -> set[str]:
    used: set[str] = set()
    for table in EFFECT_TABLES:
        with table.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                name = (row.get("Scale") or "").strip().lower()
                if name:
                    used.add(name)
    return used


def test_the_reader_sees_a_scale_source_that_rows_do_name() -> None:
    """The control: a reader finding nothing would make every name look unused."""
    assert KNOWN_USED in gen.SCALES, f"{KNOWN_USED} is no longer a scale source"
    assert KNOWN_USED in scales_named_by_a_row(), (
        f"{KNOWN_USED} is named by effect rows and was not read as used; the "
        f"reader is broken, so the check below cannot be trusted")


def test_the_reader_finds_the_measured_number_of_used_names() -> None:
    """A reader that returns SOME names passes the control above and still hides
    most of the vocabulary. This says how many it should find."""
    named = set(gen.SCALES) & scales_named_by_a_row()
    assert len(named) == EXPECTED_NAMED_BY_A_ROW, (
        f"{len(named)} of the generator's {len(gen.SCALES)} scale sources are "
        f"named by a row, and {EXPECTED_NAMED_BY_A_ROW} were measured. If a row "
        f"was added or removed on purpose, move this number with it; if not, "
        f"the reader or the tables changed underneath this file.")


def test_every_scale_source_is_named_by_a_row_or_listed_as_built_ahead() -> None:
    unused = set(gen.SCALES) - scales_named_by_a_row()
    forgotten = unused - BUILT_AHEAD_OF_THEIR_ROWS
    assert not forgotten, (
        f"no row in EnchantmentEffects.csv or PassiveEffects.csv names "
        f"{sorted(forgotten)}. A scale source nothing names cannot be reached in "
        f"play. Author its row, or add the name to BUILT_AHEAD_OF_THEIR_ROWS in "
        f"this file with the reason it landed first. Issue #1988.")


def test_the_built_ahead_list_holds_nothing_a_row_now_names() -> None:
    """A name that gained its row must leave the list, or the list rots."""
    now_used = BUILT_AHEAD_OF_THEIR_ROWS & scales_named_by_a_row()
    assert not now_used, (
        f"{sorted(now_used)} are listed as built ahead of their rows and a row "
        f"now names them; delete them from BUILT_AHEAD_OF_THEIR_ROWS.")
    unknown = BUILT_AHEAD_OF_THEIR_ROWS - set(gen.SCALES)
    assert not unknown, (
        f"{sorted(unknown)} are listed here and are not scale sources the "
        f"generator knows; delete them from BUILT_AHEAD_OF_THEIR_ROWS.")
