"""The row shape Tyrant's Chains' ten-piece bonus needs, through the generator.

Ruled 2026-10-17 by the coordinating session (#7: the Tyrant's Minion's
ultimate cast is a free start, as the skill gadget's) and 2026-10-18 (T1-T7).
The 10-piece bonus, "Your Tyrant's Minion now casts your ultimate ability every
3 seconds", is ONE row, `flat` with no Required Tags, no Condition, no Scale and
no Action: `tyrants_minion_casts_ultimate_every_seconds` at 3, the sentence's
own number. The rest is the engine's (`ACataclysmMinion::TyrantsMinionStep`
and `ACataclysmMinion::CastPlayersUltimateFromHere`). The enchantment session
authors the row; THIS FILE AUTHORS NOTHING. It builds a made-up workbook holding
the sentence typed Generic so the set rule (a set written whole or not at all)
does not ask for the rest of the set, as `test_tyrants_chains_six_rows.py` does.
The sentence's own column-4 tags, `Type.Minion, Slot.Ultimate`, are kept on the
made-up sheet: they are the enchantment's, and not the row's Required Tags.
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

TEN_PIECE_WORDS = (
    "Tyrant's Chains (10-Piece Bonus): Your Tyrant's Minion now casts your "
    "ultimate ability every 3 seconds")
TEN_PIECE = gen.row_name("Positive", TEN_PIECE_WORDS[:48])
EVERY = "tyrants_minion_casts_ultimate_every_seconds"

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: The one row, cell by cell, as the enchantment session is to write it.
TEN_PIECE_ROW = {"Enchantment": TEN_PIECE, "Effect": TEN_PIECE_WORDS,
                 "Stat": EVERY, "Value Kind": "flat", "Value Low": 3,
                 "Value High": 3}

#: The engine file that holds the stat's name.
MINION_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmMinion.cpp")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [TEN_PIECE_WORDS, "Generic", 6, "Type.Minion, Slot.Ultimate", None,
         None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "tyrants_chains_ten.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_tyrants_ultimate_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def carried(tmp_path) -> dict:
    (row,) = effects(tmp_path, TEN_PIECE_ROW)
    return row


# ---- the row passes, and says what it should

def test_the_row_name_is_the_shipped_sentences_name():
    assert TEN_PIECE == "Positive_Tyrant_s_Chains_10_Piece_Bonus_Your_Tyrant_s"


def test_the_row_is_carried_through_and_nothing_refuses_it(tmp_path):
    out = effects(tmp_path, TEN_PIECE_ROW)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Condition"], row["Scale"],
             row["Action"], row["ActionEvent"]) for row in out] == [
        (EVERY, "flat", 3.0, 3.0, "", "", "", "", "")]
    assert refusals(out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.validate_enchantment_effects(
        {"EnchantmentEffects": out}, set()) == []


def test_the_whole_validation_reads_this_check():
    """THE CONTROL OF THE LAST LINE ABOVE: a wrong row put through the whole
    sheet's validation is refused by this check's own sentence, so the
    validation does call it."""
    wrong = {"Name": f"{TEN_PIECE}#1", "Enchantment": TEN_PIECE,
             "Stat": EVERY, "ValueKind": "flat", "ValueLow": 0.0,
             "ValueHigh": 0.0, "RequiredTags": "", "Action": ""}
    assert any(EVERY in problem and "the Tyrant's Minion casts nothing"
               in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_stat_is_one_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.TYRANTS_MINION_CASTS_ULTIMATE_STAT == EVERY
    names = gen.stats_with_no_attribute()
    assert EVERY in names
    assert "tyrants_minion_casts_ultimate_seconds" not in names


def test_the_generator_holds_exactly_the_name_the_engine_asks_for():
    """Read from the file that defines it. THE CONTROL is the six-piece's
    seconds in the same file, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    found = re.findall(pattern, MINION_SOURCE.read_text(encoding="utf-8"))
    assert ("ACataclysmMinion", "TyrantsMinionSecondsStat",
            "minion_death_summons_tyrants_minion_seconds") in found
    assert ("ACataclysmMinion", "TyrantsMinionCastsUltimateEverySecondsStat",
            gen.TYRANTS_MINION_CASTS_ULTIMATE_STAT) in found


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("change, said", [
    ({"ValueKind": "increased"}, "value kind 'increased'"),
    ({"RequiredTags": "Slot.Ultimate"}, "under a required tag"),
    ({"RequiredTags": "Type.Minion"}, "under a required tag"),
    ({"Action": "apply_status"}, "with the action 'apply_status'"),
    ({"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0"),
    ({"ValueLow": -3.0, "ValueHigh": -3.0}, "must be above 0"),
    ({"ValueLow": 61.0, "ValueHigh": 61.0}, "at most 60"),
    ({"ValueLow": 2.0, "ValueHigh": 4.0}, "must not rise"),
])
def test_every_shape_the_game_would_give_nothing_for_is_refused(
        tmp_path, change, said):
    """One change to the carried row at a time, each refused once and by its
    own sentence. The control is the first assertion."""
    row = carried(tmp_path)
    assert refusals([row]) == []
    found = refusals([dict(row, **change)])
    assert len(found) == 1, found
    assert said in found[0]


def test_a_falling_range_of_seconds_is_accepted(tmp_path):
    row = carried(tmp_path)
    assert refusals([dict(row, ValueLow=4.0, ValueHigh=2.0)]) == []


def test_a_condition_or_a_scale_on_the_stat_is_refused_elsewhere(tmp_path):
    """Nothing asks the stat under a condition or a scale, so the two general
    checks refuse both. THE CONTROL is the ruled row, which neither refuses."""
    row = carried(tmp_path)
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


def test_a_row_on_another_stat_or_a_passive_node_is_not_this_checks_business(
        tmp_path):
    row = carried(tmp_path)
    assert refusals([dict(row, Stat="attack_damage", ValueKind="increased")]) == []
    assert refusals([dict(row, Node="Ritualist_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
