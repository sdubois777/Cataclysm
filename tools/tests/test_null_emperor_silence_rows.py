"""The row shapes the Null Emperor set's first layer needs, through the generator.

Ruled 2026-10-17. Two rows land together under the set rule: the 2-piece bonus,
"Your attacks have a 25% chance to silence an enemy for 1 second", and the set's
drawback, "Your own ultimate ability is disabled". The enchantment session
authors both; THIS FILE AUTHORS NEITHER. It builds a made-up workbook holding
the two real sentences, typed Generic so the set rule (a set written whole or
not at all) does not ask for the rest of the set, as
`TestAllDotsOnTheFirstHitAndTheStunNearTheDying` in `test_generate_datatables.py`
types its set sentence.

THE 2-PIECE: `apply_status` "Silence" at a chance of 25 on `hit_dealt`, under
the condition `hit_dealt_is_not_spell`, with the 0.25 second trigger cooldown
the other `apply_status` rows on a hit carry. THE DRAWBACK: `skill_locked`,
`flat` 1, Required Tags `Slot.Ultimate`, with no condition: row 110's shape
without its condition.
"""

from __future__ import annotations

import pathlib
import sys

import openpyxl
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))

import generate_datatables as gen  # noqa: E402
from test_generate_datatables import workbook_with  # noqa: E402

TWO_PIECE_WORDS = ("Null Emperor (2-Piece Bonus): Your attacks have a 25% chance "
                   "to silence an enemy for 1 second")
TWO_PIECE = gen.row_name("Positive", TWO_PIECE_WORDS[:48])
DRAWBACK_WORDS = "Your own ultimate ability is disabled"
DRAWBACK = gen.row_name("Negative", DRAWBACK_WORDS[:48])

ENCHANTMENTS = [
    ["Positives", "Type", "Weight", "Column 4", None,
     "Negatives", "Type", "Weight", "Tags"],
    [TWO_PIECE_WORDS, "Generic", 2, "Type.Strike, Type.Melee, Keyword.CC", None,
     DRAWBACK_WORDS, "Generic", 3, "Slot.Ultimate"],
]

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: The 2-piece row, cell by cell, as the enchantment session is to write it.
TWO_PIECE_ROW = {"Enchantment": TWO_PIECE, "Effect": TWO_PIECE_WORDS,
                 "Action": "apply_status", "Action Event": "hit_dealt",
                 "Value Low": 25, "Value High": 25,
                 "Condition": "hit_dealt_is_not_spell",
                 "Trigger Cooldown": 0.25, "Ailment": "Silence"}

#: The drawback row, cell by cell.
DRAWBACK_ROW = {"Enchantment": DRAWBACK, "Effect": DRAWBACK_WORDS,
                "Stat": "skill_locked", "Value Kind": "flat",
                "Value Low": 1, "Value High": 1,
                "Required Tags": "Slot.Ultimate"}


def effects(tmp_path, *rows: dict) -> list[dict]:
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "null_emperor.xlsx",
        {"Enchantments": ENCHANTMENTS, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def two_piece(**changes) -> dict:
    row = dict(TWO_PIECE_ROW)
    row.update(changes)
    return row


# ---- the two rows pass, and say what they should

def test_the_two_piece_row_is_carried_through_as_written(tmp_path):
    out = effects(tmp_path, TWO_PIECE_ROW)
    assert len(out) == 1
    assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
            out[0]["ValueHigh"], out[0]["Condition"], out[0]["ConditionValue"],
            out[0]["TriggerCooldown"], out[0]["Ailment"], out[0]["Stat"]) == (
        "apply_status", "hit_dealt", 25.0, 25.0, "hit_dealt_is_not_spell", 0.0,
        0.25, "Silence", "")


def test_left_empty_the_trigger_cooldown_is_the_same_quarter_second(tmp_path):
    """The cell is written in the row for the table to say it, and empty gives
    the same figure on a hit-fired event."""
    out = effects(tmp_path, two_piece(**{"Trigger Cooldown": None}))
    assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN == 0.25


def test_the_drawback_row_is_row_110s_shape_without_its_condition(tmp_path):
    out = effects(tmp_path, DRAWBACK_ROW)
    assert len(out) == 1
    assert (out[0]["Stat"], out[0]["ValueKind"], out[0]["ValueLow"],
            out[0]["ValueHigh"], out[0]["RequiredTags"], out[0]["Condition"],
            out[0]["Action"]) == (
        "skill_locked", "flat", 1.0, 1.0, "Slot.Ultimate", "", "")
    assert gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", out) == []


def test_both_rows_pass_together(tmp_path):
    out = effects(tmp_path, TWO_PIECE_ROW, DRAWBACK_ROW)
    assert [row["Enchantment"] for row in out] == [TWO_PIECE, DRAWBACK]


# ---- the status name

def test_silence_is_a_status_apply_status_alone_may_name():
    assert gen.SILENCE_STATUS == "Silence"
    for other in (gen.APPLY_STATUSES, gen.APPLY_STATUSES_FOR_SECONDS,
                  gen.APPLY_STATUSES_TO_SELF_FOR_SECONDS,
                  gen.APPLY_STATUSES_TO_SELF_ONLY):
        assert gen.SILENCE_STATUS not in other


def test_silence_is_refused_on_the_seconds_action(tmp_path):
    with pytest.raises(gen.DataError, match="which apply_status_seconds cannot"):
        effects(tmp_path, two_piece(Action="apply_status_seconds",
                                    **{"Value Low": 1, "Value High": 1}))


@pytest.mark.parametrize("action, event", [
    ("apply_status_to_self", "hit_taken"),
    ("apply_status_to_self_seconds", "hit_taken"),
])
def test_silence_is_refused_on_an_action_that_lays_a_status_on_the_wearer(
        tmp_path, action, event):
    with pytest.raises(gen.DataError, match=f"which {action} cannot"):
        effects(tmp_path, two_piece(Action=action, Condition=None,
                                    **{"Action Event": event, "Value Low": 1,
                                       "Value High": 1}))


def test_a_misspelt_status_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="'Silenced', which apply_status cannot"):
        effects(tmp_path, two_piece(Ailment="Silenced"))


# ---- the condition

def test_the_condition_compares_nothing_and_a_value_beside_it_is_refused(tmp_path):
    assert gen.CONDITIONS["hit_dealt_is_not_spell"] is None
    with pytest.raises(gen.DataError, match="compares nothing"):
        effects(tmp_path, two_piece(**{"Condition Value": 1}))


@pytest.mark.parametrize("event", ["first_hit_dealt", "critical_strike"])
def test_the_condition_is_allowed_on_each_event_carrying_a_hits_tags(tmp_path, event):
    out = effects(tmp_path, two_piece(**{"Action Event": event}))
    assert (out[0]["ActionEvent"], out[0]["Condition"]) == (event, "hit_dealt_is_not_spell")


@pytest.mark.parametrize("event", ["retaliation_dealt", "deployable_hit"])
def test_the_condition_is_refused_on_an_event_that_carries_no_hit_of_the_wearers(
        tmp_path, event):
    """Both events name the other character, so `apply_status` itself accepts
    them; neither carries the tags of a hit the wearer dealt."""
    with pytest.raises(gen.DataError, match="judged from the tags of a hit"):
        effects(tmp_path, two_piece(**{"Action Event": event}))


def test_the_same_rows_without_the_condition_pass_on_those_events(tmp_path):
    """The control for the test above: the refusal is the condition's."""
    for event in ("retaliation_dealt", "deployable_hit"):
        out = effects(tmp_path, two_piece(Condition=None, **{"Action Event": event}))
        assert out[0]["ActionEvent"] == event


def test_the_condition_is_refused_on_every_stat_row():
    """No stat's asker passes an action's event, so a stat row under it would
    be refused every time it was judged and grant nothing."""
    assert gen.what_a_condition_needs("hit_dealt_is_not_spell") == "action_event"
    for stat in ("attack_damage", "damage_taken", "movement_speed"):
        problems = gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", [
            {"Name": "Made_up#1", "Stat": stat,
             "Condition": "hit_dealt_is_not_spell", "Condition2": ""}])
        assert len(problems) == 1 and "an action row's event" in problems[0], (stat, problems)
    # THE CONTROL: the same rows under a condition their askers can judge pass.
    assert gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", [
        {"Name": "Made_up#1", "Stat": "attack_damage",
         "Condition": "target_is_boss", "Condition2": ""}]) == []


def test_the_condition_is_in_neither_of_the_stat_lookups_classes():
    assert "hit_dealt_is_not_spell" in gen.CONDITIONS_OF_AN_ACTIONS_EVENT
    assert "hit_dealt_is_not_spell" not in gen.CONDITIONS_OF_A_BLOW_TAKEN
    assert "hit_dealt_is_not_spell" not in gen.CONDITIONS_OF_A_HIT_DEALT
    assert set(gen.EVENTS_CARRYING_A_HIT_DEALTS_TAGS) <= set(gen.APPLY_STATUS_EVENTS)
