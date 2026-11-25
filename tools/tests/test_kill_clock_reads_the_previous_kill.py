"""A kill's own Fervour is asked before the kill clock is stamped, and no action on a kill asks that clock.

WHY THIS EXISTS. The Death trees' slice B (2026-11-24) stamped the kill clock before asking the kill's Fervour, so
`seconds_after_kill` on `fervour_per_kill` read nought at every kill and always held: a row written as "kills within
2 seconds of each other" would have paid on every kill, and the generator accepted it. The coordinating session ruled
on 2026-11-25 (the Death trees' slice C): ask the payment before the stamp, so the condition reads the time since the
PREVIOUS kill; and refuse `seconds_after_kill` on an action row hung on the `kill` event, which acts after the stamp
and would still always hold.

WHAT IS ASSERTED. The generator refuses such an action row on either sheet and in either condition column, and accepts
the three neighbours that are fine (the same action on another event, the clock on a kill's own stat, another clock on
a kill's action); no shipped row is refused; and the kill handler in `CataclysmPlayerCharacter.cpp` calls
`GainOnOwnKill` before `NoteOwnKill`, read from the source with comments removed, because continuous integration
compiles no C++. `Cataclysm.ClassKillFervour.AKillsFervourUnderSecondsAfterAKillReadsThePreviousKill` checks the
behaviour in the engine.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

DATA = ROOT / "game" / "Data"
HANDLER = ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmPlayerCharacter.cpp"


def action_row(**changes) -> dict:
    row = {"Name": "Made_up#1", "Stat": "", "Action": "class_resource", "ActionEvent": "kill",
           "Condition": "", "Condition2": ""}
    row.update(changes)
    return row


def test_an_action_on_a_kill_under_the_kill_clock_is_refused():
    problems = gen.refuse_a_kill_clock_on_a_kill_action(
        "PassiveEffects", [action_row(Condition="seconds_after_kill")])
    assert len(problems) == 1 and "always holds" in problems[0], problems


def test_the_second_condition_column_is_refused_the_same_way():
    problems = gen.refuse_a_kill_clock_on_a_kill_action(
        "EnchantmentEffects", [action_row(Condition2="seconds_after_kill")])
    assert len(problems) == 1 and "Condition2" in problems[0], problems


def test_the_neighbours_that_mean_something_are_accepted():
    """THE CONTROL. Each differs from the refused row in one column."""
    assert gen.refuse_a_kill_clock_on_a_kill_action("PassiveEffects", [
        action_row(ActionEvent="hit_dealt", Condition="seconds_after_kill"),
        action_row(Action="", Stat="fervour_per_kill", Condition="seconds_after_kill"),
        action_row(Condition="seconds_after_movement_skill"),
    ]) == []


def test_no_shipped_row_is_refused():
    for name in ("PassiveEffects.csv", "EnchantmentEffects.csv"):
        with (DATA / name).open(encoding="utf-8-sig", newline="") as handle:
            rows = list(csv.DictReader(handle))
        assert gen.refuse_a_kill_clock_on_a_kill_action(name, rows) == [], name


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as a call."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def test_the_kill_handler_asks_the_payment_before_the_stamp():
    code = code_only(HANDLER.read_text(encoding="utf-8"))
    pay = [m.start() for m in re.finditer(r"UCataclysmFervour::GainOnOwnKill\(", code)]
    stamp = [m.start() for m in re.finditer(r"UCataclysmFervour::NoteOwnKill\(", code)]
    assert len(pay) == 1 and len(stamp) == 1, (
        f"expected one call of each in {HANDLER.name}; found {len(pay)} and {len(stamp)}")
    assert pay[0] < stamp[0], (
        "the kill handler stamps the kill clock before asking the kill's Fervour, so seconds_after_kill on "
        "fervour_per_kill reads nought at every kill and always holds")
