"""The row shape the Null Emperor set's third layer needs, through the generator.

Ruled 2026-10-17 (O9). The 10-piece bonus, "While you have at least 10 stacks
of Void, you gain an aura of void energy that has a 50% chance to consume enemy
projectiles", is ONE row: the stat `void_aura_projectile_consume_chance`,
`flat`, 50 to 50, with no Required Tags, no Condition, no Scale and no Action.
The ten Void, the 3 m and the once-only roll are the engine's
(`UCataclysmVoidAura`). The enchantment session authors the row; THIS FILE
AUTHORS NOTHING. It builds a made-up workbook holding the sentence typed Generic
so the set rule (a set written whole or not at all) does not ask for the rest of
the set, as `test_null_emperor_void_rows.py` does for the second layer.
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

TEN_PIECE_WORDS = ("Null Emperor (10-Piece Bonus): While you have at least 10 "
                   "stacks of Void, you gain an aura of void energy that has a "
                   "50% chance to consume enemy projectiles")
TEN_PIECE = gen.row_name("Positive", TEN_PIECE_WORDS[:48])
STAT = "void_aura_projectile_consume_chance"

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: The 10-piece row, cell by cell, as the enchantment session is to write it.
TEN_PIECE_ROW = {"Enchantment": TEN_PIECE, "Effect": TEN_PIECE_WORDS,
                 "Stat": STAT, "Value Kind": "flat",
                 "Value Low": 50, "Value High": 50}

#: The engine file that holds the stat's name and the aura's figures.
VOID_AURA_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                    / "CataclysmVoidAura.cpp")
VOID_AURA_HEADER = VOID_AURA_SOURCE.with_suffix(".h")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [TEN_PIECE_WORDS, "Generic", 2,
         "Type.AOE.Aura, Slot.Aura, Stat.Defense.Ranged", None,
         None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "null_emperor_void_aura.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_void_aura_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


# ---- the row passes, and says what it should

def test_the_row_name_is_the_shipped_sentences_name():
    assert TEN_PIECE == "Positive_Null_Emperor_10_Piece_Bonus_While_you_have_at"


def test_the_ten_piece_row_is_carried_through_and_nothing_refuses_it(tmp_path):
    out = effects(tmp_path, TEN_PIECE_ROW)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Condition"], row["Scale"],
             row["Action"], row["ActionEvent"]) for row in out] == [
        (STAT, "flat", 50.0, 50.0, "", "", "", "", "")]
    assert refusals(out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.validate_enchantment_effects(
        {"EnchantmentEffects": out}, set()) == []


def test_the_stat_is_one_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.VOID_AURA_PROJECTILE_CONSUME_CHANCE_STAT == STAT
    assert STAT in gen.stats_with_no_attribute()
    assert "void_aura_consume_chance" not in gen.stats_with_no_attribute()


def test_the_generator_holds_exactly_the_name_the_engine_asks_for():
    """Read from the file that defines it. THE CONTROL is the skill lock's name
    in the skill slots file, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*TEXT\("([^"]+)"\)'
    slots = (VOID_AURA_SOURCE.parent / "CataclysmSkillSlots.cpp").read_text(
        encoding="utf-8")
    assert ("UCataclysmSkillSlots", "LockedStat", "skill_locked") in (
        re.findall(pattern, slots))
    source = VOID_AURA_SOURCE.read_text(encoding="utf-8")
    assert ("UCataclysmVoidAura", "ConsumeChanceStat",
            gen.VOID_AURA_PROJECTILE_CONSUME_CHANCE_STAT) in re.findall(
        pattern, source)


def test_the_engine_asks_for_the_sentences_ten_stacks_of_void():
    """The row states only the chance, so the sentence's other figure and its
    stacks' name are the engine's: ten, and "Void", the name the six-piece's
    row gives them in its Ailment cell. THE CONTROL is the delegated 3 m,
    which the sentence does not state."""
    header = VOID_AURA_HEADER.read_text(encoding="utf-8")
    source = VOID_AURA_SOURCE.read_text(encoding="utf-8")
    needed = re.search(r"StacksNeeded\s*=\s*(\d+);", header)
    assert needed and int(needed.group(1)) == 10
    assert "at least 10 stacks of Void" in TEN_PIECE_WORDS
    assert re.search(r'UCataclysmVoidAura::StacksName\s*=\s*TEXT\("Void"\)',
                     source)
    radius = re.search(r"RadiusCm\s*=\s*([\d.]+)f;", header)
    assert radius and float(radius.group(1)) == 300.0
    assert "3 m" not in TEN_PIECE_WORDS and "meter" not in TEN_PIECE_WORDS


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("change, said", [
    ({"ValueKind": "increased"}, "value kind 'increased'"),
    ({"RequiredTags": "Type.AOE.Aura"}, "under a required tag"),
    ({"Action": "apply_status"}, "with the action 'apply_status'"),
    ({"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0"),
    ({"ValueLow": -50.0, "ValueHigh": -50.0}, "must be above 0"),
    ({"ValueLow": 101.0, "ValueHigh": 101.0}, "at most 100"),
    ({"ValueLow": 60.0, "ValueHigh": 40.0}, "the range must rise"),
])
def test_every_shape_the_game_would_give_nothing_for_is_refused(
        tmp_path, change, said):
    """One change to the carried row at a time, each refused once and by its
    own sentence. The control is the first assertion."""
    (row,) = effects(tmp_path, TEN_PIECE_ROW)
    assert refusals([row]) == []
    found = refusals([dict(row, **change)])
    assert len(found) == 1, found
    assert said in found[0]


def test_a_chance_of_a_hundred_is_the_top_and_is_accepted(tmp_path):
    (row,) = effects(tmp_path, TEN_PIECE_ROW)
    assert refusals([dict(row, ValueLow=100.0, ValueHigh=100.0)]) == []


def test_a_condition_or_a_scale_on_the_stat_is_refused_elsewhere(tmp_path):
    """Nothing asks the stat under a condition or a scale, so the two general
    checks refuse both. THE CONTROL is the ruled row, which neither refuses."""
    (row,) = effects(tmp_path, TEN_PIECE_ROW)
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [row]) == []
    assert gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", [row]) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [dict(row, Condition="health_at_or_above",
                                    ConditionValue=50.0)])
    assert gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", [dict(row, Scale="enemies_in_reach",
                                    ScaleStep=1.0)])


def test_a_row_on_another_stat_is_not_this_checks_business(tmp_path):
    (row,) = effects(tmp_path, TEN_PIECE_ROW)
    assert refusals([dict(row, Stat="attack_damage", ValueKind="increased")]) == []
