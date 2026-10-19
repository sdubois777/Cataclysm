"""The row shape Chronomancer's Time-Lock's ten-piece bonus needs, through the generator.

Ruled 2026-10-08 by the project owner (the freeze "acts like a stun"),
2026-10-17 (#10: the rift lengthens 1 s per enemy caught, at most 10 s, and an
enemy already caught in it is not frozen again) and 2026-10-19 by the
coordinating session (K1-K9). The 10-piece bonus, "Every 30 seconds, a
"temporal rift" appears around you. All enemies that enter the rift are frozen
in time for 5 seconds and take 500% more damage from all sources. Every enemy
caught in it extends the duration by 1s", is SEVEN rows, each `flat` with no
Required Tags, no Condition, no Scale and no Action:
`time_lock_rift_every_seconds` at 30, `time_lock_rift_radius_metres` at 5 (K3),
`time_lock_rift_seconds` at 5 (K2), `time_lock_rift_seconds_per_enemy` at 1,
`time_lock_rift_most_seconds` at 10 (#10, K1), `time_lock_rift_stun_seconds` at
5 and `time_lock_rift_damage_taken_more_percent` at 500. The rest is the
engine's (`UCataclysmTimeLockRift`, `UCataclysmDamageTakenMultiplier`). The
enchantment session authors the rows; THIS FILE AUTHORS NOTHING. It builds a
made-up workbook holding the sentence typed Generic so the set rule (a set
written whole or not at all) does not ask for the rest of the set, as
`test_brutes_heart_ten_rows.py` does.
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
    "Chronomancer's Time-Lock (10-Piece Bonus): Every 30 seconds, a "
    '"temporal rift" appears around you. All enemies that enter the rift are '
    "frozen in time for 5 seconds and take 500% more damage from all sources. "
    "Every enemy caught in it extends the duration by 1s")
TEN_PIECE = gen.row_name("Positive", TEN_PIECE_WORDS[:48])
EVERY = "time_lock_rift_every_seconds"
RADIUS = "time_lock_rift_radius_metres"
BASE = "time_lock_rift_seconds"
PER_ENEMY = "time_lock_rift_seconds_per_enemy"
MOST = "time_lock_rift_most_seconds"
STUN = "time_lock_rift_stun_seconds"
MORE = "time_lock_rift_damage_taken_more_percent"
SEVEN = (EVERY, RADIUS, BASE, PER_ENEMY, MOST, STUN, MORE)

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]


def ruled(stat: str, value: float) -> dict:
    return {"Enchantment": TEN_PIECE, "Effect": TEN_PIECE_WORDS, "Stat": stat,
            "Value Kind": "flat", "Value Low": value, "Value High": value}


#: The seven rows, cell by cell, as the enchantment session is to write them.
TEN_PIECE_ROWS = [ruled(EVERY, 30), ruled(RADIUS, 5), ruled(BASE, 5),
                  ruled(PER_ENEMY, 1), ruled(MOST, 10), ruled(STUN, 5),
                  ruled(MORE, 500)]

#: The engine file that holds the seven stats' names.
RIFT_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
               / "CataclysmTimeLockRift.cpp")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [TEN_PIECE_WORDS, "Generic", 7,
         "Type.Debuff, Keyword.CC, Type.AOE.Persistent",
         None, None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "chronomancers_time_lock_ten.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_time_lock_rift_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def row_on(tmp_path, stat: str) -> dict:
    """The carried row on one of the seven stats."""
    (row,) = [row for row in effects(tmp_path, *TEN_PIECE_ROWS)
              if row["Stat"] == stat]
    return row


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    """THE CONTROL is the shipped table, which holds the sentence under this
    name."""
    assert TEN_PIECE == "Positive_Chronomancer_s_Time_Lock_10_Piece_Bonus_Every"
    shipped = (ROOT / "game" / "Data" / "EnchantmentsPositive.csv").read_text(
        encoding="utf-8")
    assert f"\n{TEN_PIECE}," in shipped.replace("\r\n", "\n")


def test_the_seven_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *TEN_PIECE_ROWS)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Condition"], row["Scale"],
             row["Action"], row["ActionEvent"]) for row in out] == [
        (EVERY, "flat", 30.0, 30.0, "", "", "", "", ""),
        (RADIUS, "flat", 5.0, 5.0, "", "", "", "", ""),
        (BASE, "flat", 5.0, 5.0, "", "", "", "", ""),
        (PER_ENEMY, "flat", 1.0, 1.0, "", "", "", "", ""),
        (MOST, "flat", 10.0, 10.0, "", "", "", "", ""),
        (STUN, "flat", 5.0, 5.0, "", "", "", "", ""),
        (MORE, "flat", 500.0, 500.0, "", "", "", "", "")]
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
    wrong = {"Name": f"{TEN_PIECE}#2", "Enchantment": TEN_PIECE,
             "Stat": RADIUS, "ValueKind": "flat", "ValueLow": 0.0,
             "ValueHigh": 0.0, "RequiredTags": "", "Action": ""}
    assert any(RADIUS in problem
               and "the temporal rift gets nothing" in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_seven_stats_are_ones_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.TIME_LOCK_RIFT_STATS == SEVEN
    names = gen.stats_with_no_attribute()
    assert set(SEVEN) <= names
    assert "time_lock_rift_length_seconds" not in names


def test_the_generator_holds_exactly_the_names_the_engine_asks_for():
    """Read from the file that defines them. THE CONTROL is that the pattern
    finds seven definitions there and no more."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    found = re.findall(pattern, RIFT_SOURCE.read_text(encoding="utf-8"))
    assert len(found) == 7, found
    assert sorted(name for _, _, name in found) == sorted(SEVEN)
    assert {owner for owner, _, _ in found} == {"UCataclysmTimeLockRift"}


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("stat, change, said", [
    (EVERY, {"ValueKind": "increased"}, "value kind 'increased'"),
    (EVERY, {"RequiredTags": "Type.Debuff"}, "under a required tag"),
    (EVERY, {"Action": "stun_near_the_dying"},
     "with the action 'stun_near_the_dying'"),
    (EVERY, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0 and at most 60"),
    (EVERY, {"ValueLow": 90.0, "ValueHigh": 90.0}, "must be above 0 and at most 60"),
    (EVERY, {"ValueLow": 25.0, "ValueHigh": 30.0}, "the pair must not rise"),
    (RADIUS, {"ValueLow": -5.0, "ValueHigh": -5.0}, "must be above 0:"),
    (RADIUS, {"RequiredTags": "Type.AOE.Persistent"}, "under a required tag"),
    (BASE, {"ValueKind": "more"}, "value kind 'more'"),
    (BASE, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0:"),
    (PER_ENEMY, {"ValueLow": 2.0, "ValueHigh": 1.0}, "the range must rise"),
    (MOST, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0:"),
    (STUN, {"ValueLow": 12.0, "ValueHigh": 12.0}, "must be above 0 and at most 10"),
    (STUN, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0 and at most 10"),
    (MORE, {"ValueKind": "more"}, "value kind 'more'"),
    (MORE, {"Action": "apply_status"}, "with the action 'apply_status'"),
])
def test_every_shape_the_game_would_give_nothing_for_is_refused(
        tmp_path, stat, change, said):
    """One change to a carried row at a time, each refused once and by its
    own sentence. The control is the first assertion."""
    row = row_on(tmp_path, stat)
    assert refusals([row]) == []
    found = refusals([dict(row, **change)])
    assert len(found) == 1, found
    assert said in found[0]


def test_a_falling_range_of_seconds_and_a_rising_range_of_the_rest_are_accepted(
        tmp_path):
    assert refusals([dict(row_on(tmp_path, EVERY), ValueLow=30.0,
                          ValueHigh=25.0)]) == []
    assert refusals([dict(row_on(tmp_path, MORE), ValueLow=400.0,
                          ValueHigh=600.0)]) == []


def test_a_condition_or_a_scale_on_any_of_the_seven_is_refused_elsewhere(
        tmp_path):
    """Nothing asks any of them under a condition or a scale, so the two
    general checks refuse each. THE CONTROL is each ruled row, which neither
    refuses."""
    for stat in SEVEN:
        row = row_on(tmp_path, stat)
        assert gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [row]) == []
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [row]) == []
        assert gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [dict(row, Condition="health_below",
                                        ConditionValue=50.0)])
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [dict(row, Scale="enemies_in_reach",
                                        ScaleStep=1.0)])


def test_a_row_on_another_stat_or_a_passive_node_is_not_this_checks_business(
        tmp_path):
    row = row_on(tmp_path, RADIUS)
    assert refusals([dict(row, Stat="armor", ValueKind="increased")]) == []
    assert refusals([dict(row, Node="Chronomancer_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
