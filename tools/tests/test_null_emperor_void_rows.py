"""The row shape the Null Emperor set's second layer needs, through the generator.

Ruled 2026-10-17 (O7, O8, O10). The 6-piece bonus, "Every time you hit a
silenced enemy, you gain a stack of 'Void.' At 10 stacks, your next ability
costs no mana and deals 100% more damage", is ONE row: the action
`kept_stacks_free_next_ability` on `hit_dealt` under the condition
`target_is_silenced`, valued 100 (the "100% more"), capped at 10 by Scale Max
Steps, its stacks named Void in the Ailment cell. The enchantment session
authors it; THIS FILE AUTHORS NOTHING. It builds a made-up workbook holding the
sentence as O10 rewords it, with 'Void.' in single quotes, typed Generic so the
set rule (a set written whole or not at all) does not ask for the rest of the
set, as `test_null_emperor_silence_rows.py` does for the first layer.
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

#: The sentence as O10 rewords it: the stack's name between single quotes.
SIX_PIECE_WORDS = ("Null Emperor (6-Piece Bonus): Every time you hit a silenced "
                   "enemy, you gain a stack of 'Void.' At 10 stacks, your next "
                   "ability costs no mana and deals 100% more damage")
#: The sentence as `game/Data/EnchantmentsPositive.csv` holds it today, with the
#: name in double quotes.
SIX_PIECE_WORDS_AS_SHIPPED = SIX_PIECE_WORDS.replace("'Void.'", '"Void."')
SIX_PIECE = gen.row_name("Positive", SIX_PIECE_WORDS[:48])

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: The 6-piece row, cell by cell, as the enchantment session is to write it.
SIX_PIECE_ROW = {"Enchantment": SIX_PIECE, "Effect": SIX_PIECE_WORDS,
                 "Action": "kept_stacks_free_next_ability",
                 "Action Event": "hit_dealt",
                 "Value Low": 100, "Value High": 100,
                 "Condition": "target_is_silenced",
                 "Scale Max Steps": 10, "Ailment": "Void"}


def effects(tmp_path, *rows: dict, words: str = SIX_PIECE_WORDS) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [words, "Generic", 2, "Stat.Resource.Mana, Keyword.Consume", None,
         None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "null_emperor_void.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def six_piece(**changes) -> dict:
    row = dict(SIX_PIECE_ROW)
    row.update(changes)
    return row


# ---- the row passes, and says what it should

def test_the_row_name_is_the_shipped_sentences_name_after_the_reword():
    """O10 rewords the quotes and not the first 48 characters, so the row name
    the shipped table carries is unchanged."""
    assert SIX_PIECE == "Positive_Null_Emperor_6_Piece_Bonus_Every_time_you_hit"
    assert gen.row_name("Positive", SIX_PIECE_WORDS_AS_SHIPPED[:48]) == SIX_PIECE


def test_the_six_piece_row_is_carried_through_as_written(tmp_path):
    out = effects(tmp_path, SIX_PIECE_ROW)
    assert len(out) == 1
    assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
            out[0]["ValueHigh"], out[0]["Condition"], out[0]["ConditionValue"],
            out[0]["ScaleMaxSteps"], out[0]["Ailment"], out[0]["Stat"],
            out[0]["FractionOf"], out[0]["StackSeconds"],
            out[0]["EverySeconds"]) == (
        "kept_stacks_free_next_ability", "hit_dealt", 100.0, 100.0,
        "target_is_silenced", 0.0, 10, "Void", "", "", 0.0, 0.0)


def test_a_row_that_counts_takes_no_trigger_cooldown(tmp_path):
    """Ruled 2026-09-30: a row that counts or stacks never waits. Empty is
    none on this action, though the event is hit-fired; a stated one is
    refused."""
    out = effects(tmp_path, SIX_PIECE_ROW)
    assert out[0]["TriggerCooldown"] == 0.0
    assert not gen.takes_a_trigger_cooldown(gen.KEPT_STACKS_FREE_NEXT_ABILITY_ACTION)
    with pytest.raises(gen.DataError, match="Trigger Cooldown"):
        effects(tmp_path, six_piece(**{"Trigger Cooldown": 0.25}))


# ---- the cells the action refuses or requires

def test_the_cap_is_required(tmp_path):
    with pytest.raises(gen.DataError, match="states no Scale Max Steps"):
        effects(tmp_path, six_piece(**{"Scale Max Steps": None}))


@pytest.mark.parametrize("value", [0, -100, 1001])
def test_the_more_is_above_nought_and_bounded(tmp_path, value):
    with pytest.raises(gen.DataError, match="per cent more"):
        effects(tmp_path, six_piece(**{"Value Low": value, "Value High": value}))


def test_the_stacks_must_be_named(tmp_path):
    with pytest.raises(gen.DataError, match="states no name for them in Ailment"):
        effects(tmp_path, six_piece(Ailment=None))


def test_a_name_the_sentence_does_not_quote_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="do not write between single quotes"):
        effects(tmp_path, six_piece(Ailment="Stacks"))


def test_the_sentence_as_shipped_with_double_quotes_is_refused_which_is_why_o10_rewords_it(tmp_path):
    """The control for the reword: the same row on the sentence as the CSV
    holds it today is refused for its name, and on the reworded one passes."""
    with pytest.raises(gen.DataError, match="do not write between single quotes"):
        effects(tmp_path, six_piece(Effect=SIX_PIECE_WORDS_AS_SHIPPED),
                words=SIX_PIECE_WORDS_AS_SHIPPED)
    assert effects(tmp_path, SIX_PIECE_ROW)[0]["Ailment"] == "Void"


@pytest.mark.parametrize("column, value", [("Stack Seconds", 5),
                                           ("Every Seconds", 30)])
def test_a_lifetime_or_a_clock_is_refused(tmp_path, column, value):
    """A kept stack has no lifetime, and this action gains on its event."""
    with pytest.raises(gen.DataError, match=column):
        effects(tmp_path, six_piece(**{column: value}))


@pytest.mark.parametrize("event", ["skill_use", "kill", "timed"])
def test_another_event_is_refused(tmp_path, event):
    with pytest.raises(gen.DataError, match="gains kept stacks on the event"):
        effects(tmp_path, six_piece(**{"Action Event": event}))


@pytest.mark.parametrize("column, value", [("Fraction Of", "maximum"),
                                           ("Value Kind", "flat"),
                                           ("Scale", "own_stacks")])
def test_a_fraction_a_kind_or_a_scale_is_refused(tmp_path, column, value):
    with pytest.raises(gen.DataError, match=f"states {column}"):
        effects(tmp_path, six_piece(**{column: value}))


def test_a_stat_beside_the_action_is_refused(tmp_path):
    with pytest.raises(gen.DataError):
        effects(tmp_path, six_piece(Stat="attack_damage", **{"Value Kind": "more"}))


# ---- the condition

def test_the_condition_compares_nothing_and_a_value_beside_it_is_refused(tmp_path):
    assert gen.CONDITIONS["target_is_silenced"] is None
    with pytest.raises(gen.DataError, match="compares nothing"):
        effects(tmp_path, six_piece(**{"Condition Value": 1}))


def test_the_row_without_its_condition_passes_too(tmp_path):
    """The action does not require the condition: the sentence's "a silenced
    enemy" is the row's choice, which the enchantment session writes."""
    out = effects(tmp_path, six_piece(Condition=None))
    assert out[0]["Condition"] == ""


def test_the_condition_is_one_of_a_hit_dealt():
    """A fact of the target in hand, as `target_is_boss` is: judged against the
    enemy an action's event names, and on a stat asked while a hit is priced."""
    assert "target_is_silenced" in gen.CONDITIONS_OF_A_HIT_DEALT
    assert "target_is_silenced" not in gen.CONDITIONS_OF_A_BLOW_TAKEN
    assert "target_is_silenced" not in gen.CONDITIONS_OF_AN_ACTIONS_EVENT
    assert gen.what_a_condition_needs("target_is_silenced") == "hit_dealt"


def test_on_a_stat_row_it_passes_where_a_hit_is_priced_and_is_refused_where_none_is():
    passes = gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", [
        {"Name": "Made_up#1", "Stat": "attack_damage",
         "Condition": "target_is_silenced", "Condition2": ""}])
    assert passes == []
    refused = gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", [
        {"Name": "Made_up#1", "Stat": "movement_speed",
         "Condition": "target_is_silenced", "Condition2": ""}])
    assert len(refused) == 1 and "a hit being dealt" in refused[0], refused
