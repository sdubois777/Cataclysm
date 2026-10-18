"""The row shape Warlord's Will's six-piece bonus needs, through the generator.

Ruled 2026-10-17 by the project owner (#6: the Warhound's hits place a 3 s taunt
status on enemies, crowd control, shortened by resistance, bosses immune) and
2026-10-18 by the coordinating session (Q1-Q6). The 6-piece bonus, "When you
take damage, you have a 10% chance to summon a "Warhound" to fight for you for a
short duration. The Warhound taunts enemies and deals damage based on your
armor", is FOUR rows, each `flat` with no Required Tags, no Condition, no Scale
and no Action: `warhound_summon_chance` at 10, `warhound_seconds` at 10,
`warhound_percent_of_armor` at 20 and `warhound_taunt_seconds` at 3. The rest is
the engine's (`ACataclysmMinion::RollForWarhoundOnHitTaken`, `SummonWarhound`,
`WarhoundBlow`, and `UCataclysmTaunt`). The enchantment session authors the
rows; THIS FILE AUTHORS NOTHING. It builds a made-up workbook holding the
sentence typed Generic so the set rule (a set written whole or not at all) does
not ask for the rest of the set, as `test_tyrants_chains_six_rows.py` does.
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

SIX_PIECE_WORDS = (
    "Warlord's Will (6-Piece Bonus): When you take damage, you have a 10% "
    "chance to summon a \"Warhound\" to fight for you for a short duration. "
    "The Warhound taunts enemies and deals damage based on your armor")
SIX_PIECE = gen.row_name("Positive", SIX_PIECE_WORDS[:48])
CHANCE = "warhound_summon_chance"
SECONDS = "warhound_seconds"
ARMOUR = "warhound_percent_of_armor"
TAUNT = "warhound_taunt_seconds"
STATS = (CHANCE, SECONDS, ARMOUR, TAUNT)

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]


def ruled(stat: str, value: float) -> dict:
    return {"Enchantment": SIX_PIECE, "Effect": SIX_PIECE_WORDS, "Stat": stat,
            "Value Kind": "flat", "Value Low": value, "Value High": value}


#: The four rows, cell by cell, as the enchantment session is to write them.
SIX_PIECE_ROWS = [ruled(CHANCE, 10), ruled(SECONDS, 10), ruled(ARMOUR, 20),
                  ruled(TAUNT, 3)]

#: The engine files that hold the four names and the quarter second.
MINION_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmMinion.cpp")
MINION_HEADER = MINION_SOURCE.with_suffix(".h")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [SIX_PIECE_WORDS, "Generic", 2, "Type.Minion, Stat.Defense.Armor",
         None, None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "warlords_will_six.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_warhound_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def row_on(tmp_path, stat: str) -> dict:
    """The carried row on one of the four new stats."""
    (row,) = [row for row in effects(tmp_path, *SIX_PIECE_ROWS)
              if row["Stat"] == stat]
    return row


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    assert SIX_PIECE == "Positive_Warlord_s_Will_6_Piece_Bonus_When_you_take_da"


def test_the_four_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *SIX_PIECE_ROWS)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Condition"], row["Scale"],
             row["Action"], row["ActionEvent"]) for row in out] == [
        (CHANCE, "flat", 10.0, 10.0, "", "", "", "", ""),
        (SECONDS, "flat", 10.0, 10.0, "", "", "", "", ""),
        (ARMOUR, "flat", 20.0, 20.0, "", "", "", "", ""),
        (TAUNT, "flat", 3.0, 3.0, "", "", "", "", "")]
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
    wrong = {"Name": f"{SIX_PIECE}#1", "Enchantment": SIX_PIECE,
             "Stat": CHANCE, "ValueKind": "flat", "ValueLow": 150.0,
             "ValueHigh": 150.0, "RequiredTags": "", "Action": ""}
    assert any(CHANCE in problem and "at nought the Warhound gets nothing"
               in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_four_new_stats_are_ones_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.WARHOUND_STATS == STATS
    names = gen.stats_with_no_attribute()
    assert set(STATS) <= names
    assert "warhound_armor_percent" not in names


def test_the_generator_holds_exactly_the_names_the_engine_asks_for():
    """Read from the file that defines them. THE CONTROL is the Tyrant's
    Minion's seconds in the same file, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    found = re.findall(pattern, MINION_SOURCE.read_text(encoding="utf-8"))
    assert ("ACataclysmMinion", "TyrantsMinionSecondsStat",
            gen.TYRANTS_MINION_SECONDS_STAT) in found
    assert ("ACataclysmMinion", "WarhoundChanceStat",
            gen.WARHOUND_CHANCE_STAT) in found
    assert ("ACataclysmMinion", "WarhoundSecondsStat",
            gen.WARHOUND_SECONDS_STAT) in found
    assert ("ACataclysmMinion", "WarhoundArmourPercentStat",
            gen.WARHOUND_ARMOUR_PERCENT_STAT) in found
    assert ("ACataclysmMinion", "WarhoundTauntSecondsStat",
            gen.WARHOUND_TAUNT_SECONDS_STAT) in found


def test_the_engines_quarter_second_is_the_generators_trigger_cooldown():
    """Q4: "with its 0.25 s trigger cooldown", the wait every hit-fired row
    takes. THE CONTROL is the generator's own figure, read first."""
    assert gen.DEFAULT_TRIGGER_COOLDOWN == 0.25
    (written,) = re.findall(
        r"WarhoundSummonCooldownSeconds\s*=\s*([0-9.]+)f;",
        MINION_HEADER.read_text(encoding="utf-8"))
    assert float(written) == gen.DEFAULT_TRIGGER_COOLDOWN


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("stat, change, said", [
    (CHANCE, {"ValueKind": "increased"}, "value kind 'increased'"),
    (CHANCE, {"RequiredTags": "Type.Minion"}, "under a required tag"),
    (CHANCE, {"Action": "apply_status"}, "with the action 'apply_status'"),
    (CHANCE, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0"),
    (CHANCE, {"ValueLow": 120.0, "ValueHigh": 120.0}, "at most 100"),
    (SECONDS, {"ValueKind": "more"}, "value kind 'more'"),
    (SECONDS, {"ValueLow": -10.0, "ValueHigh": -10.0}, "must be above 0"),
    (SECONDS, {"ValueLow": 12.0, "ValueHigh": 8.0}, "the range must rise"),
    (ARMOUR, {"RequiredTags": "Stat.Defense.Armor"}, "under a required tag"),
    (ARMOUR, {"ValueLow": 1200.0, "ValueHigh": 1200.0}, "at most 1000"),
    (TAUNT, {"Action": "apply_status"}, "with the action 'apply_status'"),
    (TAUNT, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0"),
    (TAUNT, {"ValueLow": 12.0, "ValueHigh": 12.0}, "at most 10"),
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


def test_a_rising_range_of_each_is_accepted(tmp_path):
    for stat, low, high in ((CHANCE, 5.0, 15.0), (SECONDS, 8.0, 12.0),
                            (ARMOUR, 10.0, 30.0), (TAUNT, 2.0, 4.0)):
        row = row_on(tmp_path, stat)
        assert refusals([dict(row, ValueLow=low, ValueHigh=high)]) == [], stat


def test_a_condition_or_a_scale_on_any_of_the_four_is_refused_elsewhere(
        tmp_path):
    """Nothing asks any of them under a condition or a scale, so the two
    general checks refuse each. THE CONTROL is each ruled row, which neither
    refuses."""
    for stat in STATS:
        row = row_on(tmp_path, stat)
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
    row = row_on(tmp_path, CHANCE)
    assert refusals([dict(row, Stat="armor", ValueKind="increased")]) == []
    assert refusals([dict(row, Node="Ravager_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
