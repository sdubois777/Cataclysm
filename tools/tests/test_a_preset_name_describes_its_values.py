"""A preset whose name states a number holds that number.

WHY THIS FILE EXISTS. Three of the six presets in `sim/cataclysm_sim/config.py`
put their own numbers in their names:

    Explorer via floors (-25 floors)
    Explorer via floors (+30 floors)
    Proposed budget (x0.85 time, x0.55 dmg)

Those names are what `sim/experiments.py` prints in every table, what
`docs/DECISIONS.md` quotes, and what `sim/tests/test_sweep_tier.py` writes out as
string literals in its fixtures. **Nothing checked any of them against the field
beside it.** Issue #1413 changed `floor_delta` on the -25 preset to -17 and ran
the whole fast suite: nothing failed. The preset went on calling itself
"(-25 floors)" while removing 17.

A NAME IS A PROMISE AND IT IS THE ONLY DOCUMENTATION THESE THREE PRESETS HAVE.
The two "as designed" presets carry twenty lines of comment naming every node
they are built from. These three carry one line each, and the name is it.

**CHECKED THROUGH THE ACCESSOR AND NOT THE FIELD.** Since issue #1397 a preset's
floor count and damage share depend on the difficulty tier, so "(-25 floors)" is
a claim at every tier and not only at tier 1. A per-type field added to one of
these without changing its name would make the name true at tier 1 and false
everywhere else, which is the defect issue #1386 found and issue #1397 named.
"""

from __future__ import annotations

import dataclasses
import re

import pytest

from cataclysm_sim.config import (
    TREE_NONE, TREE_PRESETS, EmpireTree,
)

#: Every number of active Cataclysm types a campaign can face.
ACTIVE_COUNTS = (1, 2, 3, 4, 5, 6, 7, 8)

#: `(+30 floors)` and `(-25 floors)`. The sign is required, so a bare count in
#: some future name is not silently read as a delta.
FLOORS = re.compile(r"\(([+-]\d+(?:\.\d+)?) floors\)")

#: `x0.85 time` and `x0.55 dmg`, the two levers `Proposed budget` names.
TIME_MULT = re.compile(r"x(\d+(?:\.\d+)?) time")
DAMAGE_MULT = re.compile(r"x(\d+(?:\.\d+)?) dmg")

#: Which presets are expected to state a number, and how many they state.
#: WRITTEN OUT so that a preset renamed to hide its numbers, or a new one that
#: states numbers nothing parses, fails rather than passing quietly.
NAMES_THAT_STATE_NUMBERS = {
    "Explorer via floors (-25 floors)": 1,
    "Explorer via floors (+30 floors)": 1,
    "Proposed budget (x0.85 time, x0.55 dmg)": 2,
}

#: A preset calling itself "maxed" that grants nothing would be a lie of a
#: different shape, so those are named here too.
NAMES_THAT_PROMISE_INVESTMENT = {
    "Explorer maxed (as designed)",
    "Architect maxed (as designed)",
}


def stated_numbers(name: str) -> dict[str, float]:
    """Every number a preset's own name claims, by lever."""
    found: dict[str, float] = {}
    for match in FLOORS.finditer(name):
        found["floors"] = float(match.group(1))
    for match in TIME_MULT.finditer(name):
        found["run_days_mult"] = float(match.group(1))
    for match in DAMAGE_MULT.finditer(name):
        found["damage"] = float(match.group(1))
    return found


def by_name(name: str) -> EmpireTree:
    """The preset with this name, or a failure naming what went missing.

    **THIS LOOKUP IS LOAD-BEARING FOR SOMETHING IT WAS NOT WRITTEN FOR, AND THE
    NEXT PERSON TO REFACTOR IT NEEDS TO KNOW.** It was written to fetch a preset.
    It also happens to be the only thing in the repository that notices a preset
    being REMOVED from `TREE_PRESETS` altogether.

    Issue #1413 measured that: deleting `TREE_ARCHITECT_AS_DESIGNED` from the
    list and running the whole fast suite failed nothing at all before this file
    existed. Section 7 of `sim/experiments.py` sweeps that list, so the report
    would simply stop covering the Architect branch and say nothing about it.

    **A MISSING ROW IS WORSE THAN A WRONG NUMBER.** A wrong number is visible to
    anyone who checks it against `docs/Empire_Development_Tree_Final.json`. A
    preset silently absent produces a report that looks complete -- there is no
    gap on the page, just one fewer row -- so there is nothing for a reader to
    notice.

    `test_a_deleted_preset_is_noticed` below makes that deliberate rather than
    accidental. If this helper is ever rewritten to return `None` instead of
    failing, that test is the one that will object.
    """
    for preset in TREE_PRESETS:
        if preset.name == name:
            return preset
    pytest.fail(
        f"no preset in TREE_PRESETS is called {name!r}. If it was renamed, "
        "rename it here; if it was deleted, delete this entry -- but check "
        "first that deleting it was meant, because `experiments.PRESETS` "
        "sweeps this list and a preset missing from it drops out of section 7 "
        "silently. Issue #1413.")


class TestADeletedPresetIsNoticed:
    """**MADE DELIBERATE, HAVING BEEN DISCOVERED BY ACCIDENT.**

    Every preset this file names must still be in `TREE_PRESETS`. That was
    already true as a side effect of `by_name`; asserting it directly means a
    later refactor of that helper cannot quietly remove the only guard the
    repository has against a preset vanishing from the tuning report.
    """

    @pytest.mark.parametrize(
        "name", sorted(set(NAMES_THAT_STATE_NUMBERS)
                       | NAMES_THAT_PROMISE_INVESTMENT | {TREE_NONE.name}))
    def test_it_is_still_in_the_list_the_report_sweeps(self, name):
        assert name in {preset.name for preset in TREE_PRESETS}, (
            f"{name!r} is no longer in TREE_PRESETS. `experiments.PRESETS` is "
            "built from that list and section 7 sweeps it, so this preset has "
            "dropped out of the tuning report. Nothing else in the repository "
            "notices that. Issue #1413.")

    def test_the_list_still_holds_every_preset_this_file_knows_about(self):
        """The count as well as the membership, so a preset added to the file's
        tables without being added to the list is caught too."""
        known = (set(NAMES_THAT_STATE_NUMBERS)
                 | NAMES_THAT_PROMISE_INVESTMENT | {TREE_NONE.name})
        listed = {preset.name for preset in TREE_PRESETS}
        assert known <= listed, (
            f"this file names presets that are not in TREE_PRESETS: "
            f"{sorted(known - listed)}")

    def test_the_check_would_notice_a_missing_preset(self):
        """**THE CONTROL.** A membership test against a list that happened to
        contain everything would pass whatever it was given."""
        listed = {preset.name for preset in TREE_PRESETS}
        assert "Explorer via floors (-999 floors)" not in listed


class TestTheParserWorks:
    """**THE CONTROL.** A parser that matched nothing would report every name
    as consistent, which is the failure this whole file is about."""

    def test_it_finds_what_the_three_names_state(self):
        for name, count in NAMES_THAT_STATE_NUMBERS.items():
            found = stated_numbers(name)
            assert len(found) == count, (
                f"{name!r} states {len(found)} numbers this parser can read "
                f"and should state {count}: {found}")

    def test_it_reads_the_sign_and_the_value(self):
        assert stated_numbers("Explorer via floors (-25 floors)") == {"floors": -25.0}
        assert stated_numbers("Explorer via floors (+30 floors)") == {"floors": 30.0}
        assert stated_numbers("Proposed budget (x0.85 time, x0.55 dmg)") == {
            "run_days_mult": 0.85, "damage": 0.55}

    def test_it_does_not_invent_numbers_in_a_name_that_has_none(self):
        assert stated_numbers("Architect maxed (as designed)") == {}
        assert stated_numbers("No tree") == {}

    def test_it_would_notice_a_wrong_number(self):
        """A parser that returned a constant would satisfy everything above."""
        assert stated_numbers("Explorer via floors (-17 floors)") == {"floors": -17.0}

    def test_exactly_these_presets_state_numbers(self):
        """A new preset whose name states a number nothing checks is caught
        here rather than shipping unguarded, which is how the three above got
        into this state.

        **THIS TEST IS MEANT TO FAIL WHEN A PRESET IS ADDED**, if the new one's
        name states a number. That is the point of it and not a defect in it:
        the three presets it covers reached `development` with their names
        unchecked, and this is what stops a fourth doing the same. Adding a
        preset should cost the author one line here. The failure message says
        which line.
        """
        stating = {p.name for p in TREE_PRESETS if stated_numbers(p.name)}
        added = sorted(stating - set(NAMES_THAT_STATE_NUMBERS))
        gone = sorted(set(NAMES_THAT_STATE_NUMBERS) - stating)
        assert stating == set(NAMES_THAT_STATE_NUMBERS), (
            "the set of presets whose own names state a number has changed.\n"
            + (f"  NEW, and unchecked by anything: {added}\n" if added else "")
            + (f"  GONE from TREE_PRESETS: {gone}\n" if gone else "")
            + "\nIf you have just added a preset: put its name in "
              "`NAMES_THAT_STATE_NUMBERS` at the top of this file, with the "
              "count of numbers its name states, and the rest of the file will "
              "check that those numbers match the fields it holds. That is the "
              "whole change.\n"
              "If you would rather not, give the preset a name that states no "
              "number.\n\n"
              "Do not delete this test to make the failure go away. Three "
              "presets in this project state their numbers in their names and "
              "nothing checked any of them against the field beside it; issue "
              "#1413 measured that changing one of those numbers left the whole "
              "fast suite passing with exit code 0.")


#: (preset name, active count) for every preset whose name states that lever.
#: DERIVED FROM THE NAMES rather than written out, so a preset renamed to state
#: a floor count is covered the moment it is renamed. A skip would look the same
#: as a pass in the run output, so there are none here.
def cases_for(lever: str) -> list[tuple[str, int]]:
    return [(name, active)
            for name in sorted(NAMES_THAT_STATE_NUMBERS)
            if lever in stated_numbers(name)
            for active in ACTIVE_COUNTS]


FLOOR_CASES = cases_for("floors")
DAMAGE_CASES = cases_for("damage")
TIME_CASES = [name for name in sorted(NAMES_THAT_STATE_NUMBERS)
              if "run_days_mult" in stated_numbers(name)]


class TestTheNameIsTrue:
    """**NO SKIPS.** Each parametrisation lists only the presets whose names
    state that lever, so every case in the run output is a real check."""

    def test_there_is_something_to_check(self):
        """The control on the three lists above. A `cases_for` that returned
        nothing would make every test below vacuous and the run would look
        identical."""
        assert len(FLOOR_CASES) == 2 * len(ACTIVE_COUNTS), FLOOR_CASES
        assert len(DAMAGE_CASES) == 1 * len(ACTIVE_COUNTS), DAMAGE_CASES
        assert TIME_CASES == ["Proposed budget (x0.85 time, x0.55 dmg)"]

    @pytest.mark.parametrize("name,active", FLOOR_CASES)
    def test_the_floors_it_names_are_the_floors_it_adds(self, name, active):
        stated = stated_numbers(name)["floors"]
        preset = by_name(name)
        assert preset.floors_added(active) == pytest.approx(stated), (
            f"{name!r} adds {preset.floors_added(active)} floors at {active} "
            f"active Cataclysm types and its own name says {stated:+g}. Read "
            "through `floors_added`, so a per-type field added without changing "
            "the name fails here even though the raw `floor_delta` still matches.")

    @pytest.mark.parametrize("name,active", DAMAGE_CASES)
    def test_the_damage_share_it_names_is_the_share_it_takes(self, name, active):
        stated = stated_numbers(name)["damage"]
        preset = by_name(name)
        assert preset.damage_taken(active) == pytest.approx(stated), (
            f"{name!r} takes {preset.damage_taken(active):.4f} of a dungeon's "
            f"damage at {active} active Cataclysm types and its own name says "
            f"{stated}.")

    @pytest.mark.parametrize("name", TIME_CASES)
    def test_the_time_multiplier_it_names_is_the_one_it_holds(self, name):
        stated = stated_numbers(name)["run_days_mult"]
        assert by_name(name).run_days_mult == pytest.approx(stated), (
            f"{name!r} holds a run-time multiplier of "
            f"{by_name(name).run_days_mult} and its own name says {stated}.")


class TestTheOtherThreeNamesAreTrueToo:

    def test_the_no_tree_preset_grants_nothing(self):
        """`TREE_NONE` is the baseline every preset in section 7 is compared
        against, so a lever set on it moves every comparison at once and none
        of them would look wrong."""
        defaults = EmpireTree(name=TREE_NONE.name)
        differing = [f.name for f in dataclasses.fields(EmpireTree)
                     if getattr(TREE_NONE, f.name) != getattr(defaults, f.name)]
        assert differing == [], (
            f"the 'No tree' preset sets {differing}. It is the control row: "
            "`experiments.BASELINE_PRESET` is its name and every preset's "
            "verdict is measured against it.")

    def test_the_no_tree_preset_is_neutral_through_the_accessors(self):
        """Not the same statement as the one above. A default that stopped
        being neutral -- a `city_damage_mult_per_type` default of 0.9, say --
        would leave every field at its default and still change the baseline."""
        for active in ACTIVE_COUNTS:
            assert TREE_NONE.days_removed(active) == 0.0
            assert TREE_NONE.floors_added(active) == 0.0
            assert TREE_NONE.damage_taken(active) == 1.0
        assert TREE_NONE.run_days_mult == 1.0
        assert TREE_NONE.city_health_mult == 1.0
        assert TREE_NONE.resolve_bonus_days == 0.0
        assert TREE_NONE.surge_bonus_days == 0.0

    @pytest.mark.parametrize("name", sorted(NAMES_THAT_PROMISE_INVESTMENT))
    def test_a_preset_called_maxed_grants_something(self, name):
        """A word-shaped promise rather than a number-shaped one. These two are
        the presets the whole empire tree question rests on."""
        preset = by_name(name)
        defaults = EmpireTree(name=name)
        differing = [f.name for f in dataclasses.fields(EmpireTree)
                     if getattr(preset, f.name) != getattr(defaults, f.name)]
        assert differing != [], (
            f"{name!r} calls itself maxed and holds nothing but its name.")
