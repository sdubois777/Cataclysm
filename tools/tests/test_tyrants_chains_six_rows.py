"""The row shape Tyrant's Chains' six-piece bonus needs, through the generator.

Ruled 2026-10-17 by the project owner (#3) and 2026-10-18 by the coordinating
session (Q1-Q5). The 6-piece bonus, "When a pet or minion dies, it explodes,
dealing 50% of its maximum health as Death damage to all nearby enemies.
Additionally, a new, powerful Tyrant's Minion is summoned to fight for you for a
short duration", is FOUR rows, each `flat` with no Required Tags, no Condition,
no Scale and no Action: Shared Ruin's `minion_death_blast_percent_of_maximum_health`
at 50 and `minion_death_blast_radius_metres` at 5, `minion_death_blast_deals_death`
at 1, and `minion_death_summons_tyrants_minion_seconds` at 10. The rest is the
engine's (`ACataclysmMinion::DeathBlast` and `ACataclysmMinion::SummonTyrantsMinion`).
The enchantment session authors the rows; THIS FILE AUTHORS NOTHING. It builds a
made-up workbook holding the sentence typed Generic so the set rule (a set
written whole or not at all) does not ask for the rest of the set, as
`test_null_emperor_void_aura_rows.py` does.
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
    "Tyrant's Chains (6-Piece Bonus): When a pet or minion dies, it explodes, "
    "dealing 50% of its maximum health as Death damage to all nearby enemies. "
    "Additionally, a new, powerful Tyrant's Minion is summoned to fight for you "
    "for a short duration")
SIX_PIECE = gen.row_name("Positive", SIX_PIECE_WORDS[:48])
PERCENT = "minion_death_blast_percent_of_maximum_health"
RADIUS = "minion_death_blast_radius_metres"
DEATH = "minion_death_blast_deals_death"
SECONDS = "minion_death_summons_tyrants_minion_seconds"

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
SIX_PIECE_ROWS = [ruled(PERCENT, 50), ruled(RADIUS, 5), ruled(DEATH, 1),
                  ruled(SECONDS, 10)]

#: The engine file that holds the two new stats' names.
MINION_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmMinion.cpp")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [SIX_PIECE_WORDS, "Generic", 2, "Type.Minion, Element.Death", None,
         None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "tyrants_chains_six.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_tyrants_minion_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def row_on(tmp_path, stat: str) -> dict:
    """The carried row on one of the two new stats."""
    (row,) = [row for row in effects(tmp_path, *SIX_PIECE_ROWS)
              if row["Stat"] == stat]
    return row


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    assert SIX_PIECE == "Positive_Tyrant_s_Chains_6_Piece_Bonus_When_a_pet_or_m"


def test_the_four_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *SIX_PIECE_ROWS)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Condition"], row["Scale"],
             row["Action"], row["ActionEvent"]) for row in out] == [
        (PERCENT, "flat", 50.0, 50.0, "", "", "", "", ""),
        (RADIUS, "flat", 5.0, 5.0, "", "", "", "", ""),
        (DEATH, "flat", 1.0, 1.0, "", "", "", "", ""),
        (SECONDS, "flat", 10.0, 10.0, "", "", "", "", "")]
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
    wrong = {"Name": f"{SIX_PIECE}#3", "Enchantment": SIX_PIECE,
             "Stat": DEATH, "ValueKind": "flat", "ValueLow": 2.0,
             "ValueHigh": 2.0, "RequiredTags": "", "Action": ""}
    assert any(DEATH in problem and "a row on this flag states 1" in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_two_new_stats_are_ones_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.TYRANTS_DEATH_BLAST_DEALS_DEATH_STAT == DEATH
    assert gen.TYRANTS_MINION_SECONDS_STAT == SECONDS
    names = gen.stats_with_no_attribute()
    assert {PERCENT, RADIUS, DEATH, SECONDS} <= names
    assert "tyrants_minion_seconds" not in names


def test_the_generator_holds_exactly_the_names_the_engine_asks_for():
    """Read from the file that defines them. THE CONTROL is Shared Ruin's
    radius in the same file, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    found = re.findall(pattern, MINION_SOURCE.read_text(encoding="utf-8"))
    assert ("ACataclysmMinion", "DeathBlastRadiusMetresStat", RADIUS) in found
    assert ("ACataclysmMinion", "DeathBlastDealsDeathStat",
            gen.TYRANTS_DEATH_BLAST_DEALS_DEATH_STAT) in found
    assert ("ACataclysmMinion", "TyrantsMinionSecondsStat",
            gen.TYRANTS_MINION_SECONDS_STAT) in found


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("stat, change, said", [
    (DEATH, {"ValueKind": "increased"}, "value kind 'increased'"),
    (DEATH, {"RequiredTags": "Type.Minion"}, "under a required tag"),
    (DEATH, {"Action": "apply_status"}, "with the action 'apply_status'"),
    (DEATH, {"ValueLow": 2.0, "ValueHigh": 2.0}, "states 1 and nothing else"),
    (DEATH, {"ValueLow": 0.0, "ValueHigh": 0.0}, "states 1 and nothing else"),
    (SECONDS, {"ValueKind": "more"}, "value kind 'more'"),
    (SECONDS, {"RequiredTags": "Element.Death"}, "under a required tag"),
    (SECONDS, {"Action": "apply_status"}, "with the action 'apply_status'"),
    (SECONDS, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0"),
    (SECONDS, {"ValueLow": -10.0, "ValueHigh": -10.0}, "must be above 0"),
    (SECONDS, {"ValueLow": 12.0, "ValueHigh": 8.0}, "the range must rise"),
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


def test_a_rising_range_of_seconds_is_accepted(tmp_path):
    row = row_on(tmp_path, SECONDS)
    assert refusals([dict(row, ValueLow=8.0, ValueHigh=12.0)]) == []


def test_a_condition_or_a_scale_on_either_stat_is_refused_elsewhere(tmp_path):
    """Nothing asks either stat under a condition or a scale, so the two
    general checks refuse both. THE CONTROL is each ruled row, which neither
    refuses."""
    for stat in (DEATH, SECONDS):
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
    row = row_on(tmp_path, DEATH)
    assert refusals([dict(row, Stat="attack_damage", ValueKind="increased")]) == []
    assert refusals([dict(row, Node="Ritualist_made_up", ValueLow=2.0,
                          ValueHigh=2.0)]) == []
