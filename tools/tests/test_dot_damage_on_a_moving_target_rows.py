"""The row shape "While the enemy is moving, your DoTs on them deal 20%-40% more
damage" needs, through the generator.

Ruled 2026-10-23 (#2235). The owner reworded the row "While moving, your DoTs
deal 20%-40% increased damage", and the coordinating session ruled the shape:
`dot_damage`, `more`, 20 to 40, under the condition `target_is_moving` (Q1),
which takes no value and is a fact of the character struck. The engine judges
it on each tick, against the character the tick lands on (Q2 (a)).

THIS FILE AUTHORS NOTHING. The workbook row, the two CSVs, the pin and the
renamed-row alias are the data commit on branch `feat/dot-target-moving`. It
builds a made-up workbook holding the new sentence, typed Generic with the one
tag Q3 keeps, as `test_null_emperor_void_rows.py` does.
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

#: The sentence as the owner reworded it, 2026-10-23.
NEW_WORDS = ("While the enemy is moving, your DoTs on them deal 20%-40% more "
             "damage")
#: The sentence as `game/Data/EnchantmentsPositive.csv` holds it today.
OLD_WORDS = "While moving, your DoTs deal 20%-40% increased damage"
NEW_NAME = gen.row_name("Positive", NEW_WORDS[:48])

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: The row, cell by cell, as the enchantment session is to write it.
THE_ROW = {"Enchantment": NEW_NAME, "Effect": NEW_WORDS, "Stat": "dot_damage",
           "Value Kind": "more", "Value Low": 20, "Value High": 40,
           "Condition": "target_is_moving"}


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [NEW_WORDS, "Generic", 4, "Keyword.DoT.Generic", None,
         None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "dot_damage_on_a_moving_target.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def the_row(**changes) -> dict:
    row = dict(THE_ROW)
    row.update(changes)
    return row


def refused(stat: str, condition: str) -> list[str]:
    return gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", [
        {"Name": "Made_up#1", "Stat": stat, "Condition": condition,
         "Condition2": ""}])


# ---- the name

def test_the_reword_renames_the_row():
    """The reword is inside the first 48 characters, so the row is renamed and
    the data commit needs the alias from the old name (#1799)."""
    assert NEW_NAME == "Positive_While_the_enemy_is_moving_your_DoTs_on_them_dea"
    assert gen.row_name("Positive", OLD_WORDS[:48]) == (
        "Positive_While_moving_your_DoTs_deal_20_40_increased_d")


# ---- the row passes, and says what it should

def test_the_row_is_carried_through_as_written(tmp_path):
    out = effects(tmp_path, THE_ROW)
    assert len(out) == 1
    assert (out[0]["Stat"], out[0]["ValueKind"], out[0]["ValueLow"],
            out[0]["ValueHigh"], out[0]["Condition"], out[0]["ConditionValue"],
            out[0]["Action"]) == (
        "dot_damage", "more", 20.0, 40.0, "target_is_moving", 0.0, "")


def test_the_condition_compares_nothing_and_a_value_beside_it_is_refused(tmp_path):
    assert gen.CONDITIONS["target_is_moving"] is None
    with pytest.raises(gen.DataError, match="compares nothing"):
        effects(tmp_path, the_row(**{"Condition Value": 1}))


def test_the_condition_is_one_of_a_hit_dealt():
    """A fact of the target in hand, as `target_is_silenced` is."""
    assert "target_is_moving" in gen.CONDITIONS_OF_A_HIT_DEALT
    assert "target_is_moving" not in gen.CONDITIONS_OF_A_BLOW_TAKEN
    assert "target_is_moving" not in gen.CONDITIONS_OF_AN_ACTIONS_EVENT
    assert gen.what_a_condition_needs("target_is_moving") == "hit_dealt"


# ---- where the condition may stand, and where it may not

def test_on_dot_damage_a_condition_about_the_target_passes():
    """`dot_damage` is asked on each tick with the character it lands on."""
    assert gen.CONDITIONED_STATS_WITH_AN_ASKER["dot_damage"] == gen.ASKER_PASSES_A_HIT_DEALT
    assert refused("dot_damage", "target_is_moving") == []
    assert refused("dot_damage", "target_is_boss") == []


def test_the_wearers_own_movement_still_passes_on_dot_damage():
    """The control: the row as shipped today, `while_moving`, is the wearer's
    state, which every asker has."""
    assert refused("dot_damage", "while_moving") == []


def test_on_a_stat_asked_with_no_target_the_condition_is_refused():
    problems = refused("movement_speed", "target_is_moving")
    assert len(problems) == 1 and "a hit being dealt" in problems[0], problems


def test_a_condition_about_a_blow_taken_is_still_refused_on_dot_damage():
    problems = refused("dot_damage", "hit_is_spell")
    assert len(problems) == 1 and "a blow being taken" in problems[0], problems


@pytest.mark.parametrize("condition", sorted(
    gen.CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS["dot_damage"]))
def test_a_condition_about_the_blow_dealt_is_refused_on_dot_damage(condition):
    """A tick is no blow: how many it struck, how far its user moved and what its
    skill cost are never handed over, so these stay refused as they were."""
    problems = refused("dot_damage", condition)
    assert len(problems) == 1 and "hands over the character struck" in problems[0], problems


def test_the_same_blow_conditions_still_pass_where_a_blow_is_priced():
    """The control on the table above: it names `dot_damage` and nothing else."""
    assert set(gen.CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS) == {"dot_damage"}
    assert refused("attack_damage", "enemies_hit_at_least") == []
    assert refused("armor_penetration", "enemies_hit_at_least") == []


def test_every_blow_condition_named_is_a_condition_of_a_hit_dealt():
    named = gen.CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS["dot_damage"]
    assert named <= gen.CONDITIONS_OF_A_HIT_DEALT, sorted(named - gen.CONDITIONS_OF_A_HIT_DEALT)
