"""The record of Fervour spent: raised where a skill pays its Fervour cost, never for a charge, and a row on it may ask
for N or more at once.

WHY THIS EXISTS. The Death trees' slice C3a (2026-11-25). `resource_consumed`, the only event a spend raised, also fires
with nothing for every next-use charge spent, so the Soul Collector's "after you spend Fervour" nodes could not be
written on it. `UCataclysmAbilitySystemComponent::NoteFervourSpent` stamps `seconds_after_fervour_spent` and raises
`fervour_spent` with the amount; a row on that event whose Event Value is N acts only when N or more was spent at once
(Spent Breath's "10 or more Fervour at once").

WHAT IS ASSERTED. Read from the C++ with comments removed, because continuous integration compiles no C++: the cost
path calls `NoteFervourSpent` and the two charge spends do not. And from the generator: an Event Value on
`fervour_spent` is accepted from nought to its bound and refused outside it, an Event Value on another event is still
refused, the event carries its amount, and an action on `fervour_spent` under its own clock is refused, as one on
`kill` under the kill clock is. `Cataclysm.FervourSpent.` checks the behaviour in the engine.
"""

from __future__ import annotations

import pathlib
import re
import sys

import openpyxl
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))

import generate_datatables as gen  # noqa: E402
from test_generate_datatables import workbook_with  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def body_of(code: str, signature: str) -> str:
    start = code.index(signature)
    return code[start:code.index("\n}\n", start)]


def test_the_cost_path_records_the_spend():
    code = code_only((SOURCE / "CataclysmGameplayAbility.cpp").read_text(encoding="utf-8"))
    body = body_of(code, "void UCataclysmGameplayAbility::ApplyCost(")
    assert "NoteFervourSpent(Fervour" in body, (
        "ApplyCost pays a Fervour cost without recording the spend, so no 'after you spend Fervour' row ever acts")


def test_a_charge_spent_is_not_a_spend_of_fervour():
    """THE CONTROL: the two places that raise `resource_consumed` for a next-use charge record no spend."""
    code = code_only((SOURCE / "CataclysmAbilitySystemComponent.cpp").read_text(encoding="utf-8"))
    for signature in ("UCataclysmAbilitySystemComponent::SpendNextSpellCooldownSeconds(",
                      "UCataclysmAbilitySystemComponent::SpendNextUseCharges("):
        body = body_of(code, signature)
        assert "NoteResourceConsumed(" in body, f"{signature} no longer raises resource_consumed; re-read this check"
        assert "NoteFervourSpent(" not in body, f"{signature} records a spend of Fervour for a charge"


def spend_row(tmp_path, event_value, event="fervour_spent"):
    """A Passive Effects sheet holding one action row on `event` with this Event Value. A passive action row is
    judged by the enchantment rows' own checks (`_effect_row`), which hold the Event Value rule."""
    header = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition", "Condition Value",
              "Scale", "Scale Step", "Option", "Reach Metres", "Min Points", "Scale Max Steps",
              *gen.PASSIVE_ACTION_COLUMNS, "Per Point From"]
    cells = {"Node": "A_node", "Value Per Point": 1, "Action": "health", "Action Event": event,
             "Fraction Of": "maximum", "Event Value": event_value}
    row = [cells.get(name) for name in header]
    return openpyxl.load_workbook(workbook_with(tmp_path / "spent.xlsx", {"Passive Effects": [header, row]}))


def test_an_event_value_on_a_spend_is_accepted_up_to_its_bound(tmp_path):
    for value in (10, gen.MAX_FERVOUR_SPENT_AT_ONCE):
        out = gen.passive_effects(spend_row(tmp_path, value))
        assert [(r["ActionEvent"], r["EventValue"]) for r in out] == [("fervour_spent", float(value))]


def test_an_event_value_past_its_bound_or_on_another_event_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="Fervour spent at once"):
        gen.passive_effects(spend_row(tmp_path, gen.MAX_FERVOUR_SPENT_AT_ONCE + 1))
    with pytest.raises(gen.DataError, match="states an Event Value on the event 'block'"):
        gen.passive_effects(spend_row(tmp_path, 10, event="block"))


def test_the_event_carries_its_amount_and_is_derived_from_its_clock():
    assert "fervour_spent" in gen.EVENTS_WITH_AN_AMOUNT
    assert "seconds_after_fervour_spent" in gen.CONDITIONS
    assert "fervour_spent" in gen.action_events()
    assert "fervour_spent" not in gen.ACTION_ONLY_EVENTS


def test_an_action_on_a_spend_under_its_own_clock_is_refused():
    row = {"Name": "Made_up#1", "Stat": "", "Action": "health", "ActionEvent": "fervour_spent",
           "Condition": "seconds_after_fervour_spent", "Condition2": ""}
    problems = gen.refuse_a_kill_clock_on_a_kill_action("PassiveEffects", [row])
    assert len(problems) == 1 and "always holds" in problems[0], problems
    # THE CONTROL: the same row on another event is fine.
    assert gen.refuse_a_kill_clock_on_a_kill_action("PassiveEffects", [dict(row, ActionEvent="hit_dealt")]) == []
