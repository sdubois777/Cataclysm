"""Rows sized by what one spend of Fervour carried, and a reserve held counting as a spend.

WHY THIS EXISTS. The Death trees' slice C3b (2026-11-25). Five Soul Collector nodes pay for each Fervour spent, or for
every ten: Feast heals "1% of maximum health for each Fervour spent", Spirit to Mana and Ashen Ward restore "1% ... per
point for every 10 Fervour spent", Soul Furnace's blast deals "10% of your weapon damage for each Fervour spent", and
Turn of the Wheel takes "1 second off the cooldowns of your skills that did not spend it" for every ten. Two Fraction Of
values, `for_each_of_event_amount` and `for_each_ten_of_event_amount`, multiply such a row's value by the whole units or
whole tens of its event's amount, in `UCataclysmAbilitySystemComponent::ActOnEvent` before any action reads it. And
Bound Offering counts a share of every reserve held as Fervour spent, in `UCataclysmCommand::HoldReserve`.

WHAT IS ASSERTED. From the generator: the two values are accepted on a pool, a nearby action and a cooldown reduction
on an event that carries an amount, and refused on an event that carries none, on a cooldown reset, and on an action
that takes no fraction. Read from the C++ with comments removed, because continuous integration compiles no C++:
`ActOnEvent` sizes a row before the first action is dispatched and hands a reduction the event's tags; `HoldReserve`
raises the spend; and the summon skills and Subjugate hold their reserve through it rather than writing it themselves.
`Cataclysm.SpendSized.` checks the behaviour in the engine.
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

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
EACH = "for_each_of_event_amount"
TEN = "for_each_ten_of_event_amount"


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def body_of(code: str, signature: str) -> str:
    start = code.index(signature)
    return code[start:code.index("\n}\n", start)]


def action_row(tmp_path, action, fraction_of, event="fervour_spent"):
    """A Passive Effects sheet holding one action row. A passive action row is judged by the enchantment rows' own
    checks (`_effect_row`), which hold the Fraction Of rules."""
    header = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition", "Condition Value",
              "Scale", "Scale Step", "Option", "Reach Metres", "Min Points", "Scale Max Steps",
              *gen.PASSIVE_ACTION_COLUMNS, "Per Point From"]
    cells = {"Node": "A_node", "Value Per Point": 1, "Action": action, "Action Event": event,
             "Fraction Of": fraction_of}
    row = [cells.get(name) for name in header]
    return openpyxl.load_workbook(workbook_with(tmp_path / "sized.xlsx", {"Passive Effects": [header, row]}))


@pytest.mark.parametrize("action, fraction_of", [
    ("health", EACH),                    # Feast
    ("mana", TEN),                       # Spirit to Mana
    ("energy_shield", TEN),              # Ashen Ward
    ("smite_nearby", EACH),              # Soul Furnace
    ("cooldown_reduce_others", TEN),     # Turn of the Wheel
])
def test_a_sized_row_is_accepted_where_the_value_can_be_multiplied(tmp_path, action, fraction_of):
    out = gen.passive_effects(action_row(tmp_path, action, fraction_of))
    assert [(r["Action"], r["FractionOf"]) for r in out] == [(action, fraction_of)]


@pytest.mark.parametrize("action", ["health", "smite_nearby", "cooldown_reduce_others"])
def test_a_sized_row_on_an_event_with_no_amount_is_refused(tmp_path, action):
    with pytest.raises(gen.DataError, match="carries no amount"):
        gen.passive_effects(action_row(tmp_path, action, EACH, event="block"))


def test_a_reset_and_an_action_with_no_fraction_refuse_to_be_sized(tmp_path):
    """A reset's value is a chance, and a chance multiplied is not one; a status's is a chance too."""
    with pytest.raises(gen.DataError, match="states Fraction Of"):
        gen.passive_effects(action_row(tmp_path, "cooldown_reset_others", TEN))
    with pytest.raises(gen.DataError, match="Fraction Of"):
        gen.passive_effects(action_row(tmp_path, "apply_status", EACH, event="hit_dealt"))


def test_a_name_that_is_neither_still_reads_as_a_bad_fraction(tmp_path):
    """THE CONTROL for the two above: the sizing is judged before the old refusal, and an unknown value still
    reaches it rather than passing as sized."""
    with pytest.raises(gen.DataError, match="states Fraction Of"):
        gen.passive_effects(action_row(tmp_path, "smite_nearby", "for_each_hundred"))


def test_act_on_event_sizes_a_row_before_any_action_reads_it():
    code = code_only((SOURCE / "AbilitySystem" / "CataclysmAbilitySystemComponent.cpp").read_text(encoding="utf-8"))
    body = body_of(code, "void UCataclysmAbilitySystemComponent::ActOnEvent(")
    sized = body.index("TimesSizedByEventAmount(Listed.Base, EventAmount)")
    first_dispatch = body.index("Action.CooldownReduce != ECataclysmCooldownReset::None")
    assert sized < first_dispatch, "ActOnEvent dispatches an action before sizing it"
    assert "ReduceCooldowns(Action, EventTags)" in body, "a reduction is not told which skill raised the event"


def test_a_reserve_held_raises_the_spend_and_every_maker_holds_through_it():
    command = code_only((SOURCE / "AbilitySystem" / "CataclysmCommand.cpp").read_text(encoding="utf-8"))
    hold = body_of(command, "void UCataclysmCommand::HoldReserve(")
    assert "FName(ReservedFervourSpentPercentStat)" in hold and "NoteFervourSpent(" in hold, (
        "HoldReserve no longer counts Bound Offering's share as a spend")

    templates = code_only((SOURCE / "AbilitySystem" / "CataclysmSkillTemplates.cpp").read_text(encoding="utf-8"))
    assert re.search(r"->ReservedFervour\s*=[^=]", templates) is None, (
        "a skill template writes a reserve itself, so Bound Offering never hears of that hold")
    assert templates.count("UCataclysmCommand::HoldReserve(") == 3, (
        "the two summons and Subjugate hold their reserve through HoldReserve")

    # THE CONTROL: a level lowering a reserve already held still writes it directly, and is not a hold.
    levels = code_only((SOURCE / "Character" / "CataclysmSkillLevels.cpp").read_text(encoding="utf-8"))
    assert re.search(r"->ReservedFervour\s*=[^=]", levels), "re-read this check: the lowering moved"
