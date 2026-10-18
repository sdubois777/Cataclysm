"""The row shape Brute's Heart's six-piece taunt aura needs, through the generator.

Ruled 2026-10-17 by the project owner (#6: Warlord's Will 6's taunt status
"also completes Brute's Heart 6's taunt") and 2026-10-18 by the coordinating
session (Q1-Q4). The 6-piece bonus, "When your health falls below 50%, you gain
a powerful aura that taunts all nearby enemies and increases your armor and
resistances by 50%", already has one row, `armor` increased 50 under
`health_below` 50. The aura is FOUR more, each `flat` with no Required Tags, no
Condition, no Scale and no Action: `taunt_aura_below_health_percent` at 50,
`taunt_aura_every_seconds` at 1 (Q2), `taunt_aura_radius_metres` at 5 (Q1) and
`taunt_aura_taunt_seconds` at 3 (Q2). The rest is the engine's
(`UCataclysmTauntAura`). The resistances half is not built. The enchantment
session authors the rows; THIS FILE AUTHORS NOTHING. It builds a made-up
workbook holding the sentence typed Generic so the set rule (a set written
whole or not at all) does not ask for the rest of the set, as
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

SIX_PIECE_WORDS = (
    "Brute's Heart (6-Piece Bonus): When your health falls below 50%, you gain "
    "a powerful aura that taunts all nearby enemies and increases your armor "
    "and resistances by 50%")
SIX_PIECE = gen.row_name("Positive", SIX_PIECE_WORDS[:48])
SHARE = "taunt_aura_below_health_percent"
EVERY = "taunt_aura_every_seconds"
RADIUS = "taunt_aura_radius_metres"
TAUNT = "taunt_aura_taunt_seconds"
FOUR = (SHARE, EVERY, RADIUS, TAUNT)

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


#: The row already on the sheet, as `game/Data/EnchantmentEffects.csv` holds it.
ARMOUR_ROW = {"Enchantment": SIX_PIECE, "Effect": SIX_PIECE_WORDS,
              "Stat": "armor", "Value Kind": "increased", "Value Low": 50,
              "Value High": 50, "Condition": "health_below",
              "Condition Value": 50}

#: The four rows, cell by cell, as the enchantment session is to write them,
#: after the armour row.
AURA_ROWS = [ruled(SHARE, 50), ruled(EVERY, 1), ruled(RADIUS, 5),
             ruled(TAUNT, 3)]

#: The engine file that holds the four stats' names.
TAUNT_AURA_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                     / "CataclysmTauntAura.cpp")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [SIX_PIECE_WORDS, "Generic", 9,
         "Stat.Defense.Life, Type.AOE.Aura, Slot.Aura, Stat.Defense.Armor, "
         "Stat.Defense.Resist, Trigger.LowLife, Keyword.CC",
         None, None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "brutes_heart_six.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_taunt_aura_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def row_on(tmp_path, stat: str) -> dict:
    """The carried row on one of the four stats."""
    (row,) = [row for row in effects(tmp_path, ARMOUR_ROW, *AURA_ROWS)
              if row["Stat"] == stat]
    return row


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    assert SIX_PIECE == "Positive_Brute_s_Heart_6_Piece_Bonus_When_your_health"


def test_the_armour_row_and_the_four_are_carried_through_and_nothing_refuses_them(
        tmp_path):
    out = effects(tmp_path, ARMOUR_ROW, *AURA_ROWS)
    assert [(row["Name"], row["Stat"], row["ValueKind"], row["ValueLow"],
             row["ValueHigh"], row["RequiredTags"], row["Condition"],
             row["Scale"], row["Action"], row["ActionEvent"]) for row in out] == [
        (f"{SIX_PIECE}#1", "armor", "increased", 50.0, 50.0, "",
         "health_below", "", "", ""),
        (f"{SIX_PIECE}#2", SHARE, "flat", 50.0, 50.0, "", "", "", "", ""),
        (f"{SIX_PIECE}#3", EVERY, "flat", 1.0, 1.0, "", "", "", "", ""),
        (f"{SIX_PIECE}#4", RADIUS, "flat", 5.0, 5.0, "", "", "", "", ""),
        (f"{SIX_PIECE}#5", TAUNT, "flat", 3.0, 3.0, "", "", "", "", "")]
    assert refusals(out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    # THE FOUR AURA ROWS THROUGH THE WHOLE VALIDATION. The armour row is left
    # out of this one call only because this made-up workbook has no class
    # lines to supply `armor`'s base; the real sheet's do.
    assert gen.validate_enchantment_effects(
        {"EnchantmentEffects": out[1:]}, set()) == []


def test_the_whole_validation_reads_this_check():
    """THE CONTROL OF THE LAST LINE ABOVE: a wrong row put through the whole
    sheet's validation is refused by this check's own sentence, so the
    validation does call it."""
    wrong = {"Name": f"{SIX_PIECE}#4", "Enchantment": SIX_PIECE,
             "Stat": RADIUS, "ValueKind": "flat", "ValueLow": 0.0,
             "ValueHigh": 0.0, "RequiredTags": "", "Action": ""}
    assert any(RADIUS in problem and "the taunt aura gets nothing" in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_four_stats_are_ones_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.TAUNT_AURA_STATS == FOUR
    names = gen.stats_with_no_attribute()
    assert set(FOUR) <= names
    assert "taunt_aura_seconds" not in names


def test_the_generator_holds_exactly_the_names_the_engine_asks_for():
    """Read from the file that defines them. THE CONTROL is that the pattern
    finds four definitions there and no more."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    found = re.findall(pattern, TAUNT_AURA_SOURCE.read_text(encoding="utf-8"))
    assert len(found) == 4, found
    assert sorted(name for _, _, name in found) == sorted(FOUR)
    assert {owner for owner, _, _ in found} == {"UCataclysmTauntAura"}


def test_the_engine_judges_the_share_with_the_armour_rows_predicate():
    """Q2: "while health is below 50% (health_below 50, as the armour row reads
    it)". The engine asks the pipeline's own `HealthBelowPercent`, the
    condition the generator maps `health_below` to. THE CONTROL is that the
    generator's vocabulary does map that name to that predicate, read from the
    pipeline's own table."""
    pipeline = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                / "CataclysmStatPipeline.cpp").read_text(encoding="utf-8")
    assert re.search(r'TEXT\("health_below"\),\s*ECataclysmStatCondition::'
                     r'HealthBelowPercent', pipeline)
    source = TAUNT_AURA_SOURCE.read_text(encoding="utf-8")
    code = "\n".join(line for line in source.splitlines()
                     if not line.lstrip().startswith("//"))
    assert ("ConditionHolds(ECataclysmStatCondition::HealthBelowPercent, Share"
            in code)


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("stat, change, said", [
    (SHARE, {"ValueKind": "increased"}, "value kind 'increased'"),
    (SHARE, {"RequiredTags": "Type.AOE.Aura"}, "under a required tag"),
    (SHARE, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0 and at most 100"),
    (SHARE, {"ValueLow": 150.0, "ValueHigh": 150.0},
     "must be above 0 and at most 100"),
    (SHARE, {"ValueLow": 60.0, "ValueHigh": 40.0}, "the range must rise"),
    (EVERY, {"ValueKind": "more"}, "value kind 'more'"),
    (EVERY, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0 and at most 60"),
    (EVERY, {"ValueLow": 90.0, "ValueHigh": 90.0}, "must be above 0 and at most 60"),
    (EVERY, {"ValueLow": 1.0, "ValueHigh": 2.0}, "the pair must not rise"),
    (RADIUS, {"ValueLow": -5.0, "ValueHigh": -5.0}, "must be above 0:"),
    (RADIUS, {"Action": "smite_nearby_by_armor"},
     "with the action 'smite_nearby_by_armor'"),
    (TAUNT, {"ValueLow": 12.0, "ValueHigh": 12.0}, "must be above 0 and at most 10"),
    (TAUNT, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0 and at most 10"),
    (TAUNT, {"RequiredTags": "Keyword.CC"}, "under a required tag"),
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
    assert refusals([dict(row_on(tmp_path, EVERY), ValueLow=2.0,
                          ValueHigh=1.0)]) == []
    assert refusals([dict(row_on(tmp_path, RADIUS), ValueLow=4.0,
                          ValueHigh=6.0)]) == []


def test_a_condition_or_a_scale_on_any_of_the_four_is_refused_elsewhere(
        tmp_path):
    """Nothing asks any of them under a condition or a scale, so the two
    general checks refuse each: the share is a stat of its own, not a
    `health_below` on the other three. THE CONTROL is each ruled row, which
    neither refuses."""
    for stat in FOUR:
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


def test_a_resistance_row_under_health_below_is_still_refused(tmp_path):
    """THE RESISTANCES HALF IS NOT BUILT, and the generator says so rather than
    accepting a row that would grant nothing: a blow reads a resistance from
    its attribute (`UCataclysmDamageCalculation`'s `ResistanceFor`). THE
    CONTROL is the armour row, which the same check accepts, because the armour
    step asks the pipeline."""
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [dict(row_on(tmp_path, SHARE), Stat="armor",
                                    ValueKind="increased",
                                    Condition="health_below",
                                    ConditionValue=50.0)]) == []
    for typed in ("resistance_war", "resistance_void"):
        found = gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [dict(row_on(tmp_path, SHARE), Stat=typed,
                                        ValueKind="increased",
                                        Condition="health_below",
                                        ConditionValue=50.0)])
        assert len(found) == 1, found
        assert "nothing is known to ask for that stat" in found[0]


def test_a_row_on_another_stat_or_a_passive_node_is_not_this_checks_business(
        tmp_path):
    row = row_on(tmp_path, RADIUS)
    assert refusals([dict(row, Stat="armor", ValueKind="increased")]) == []
    assert refusals([dict(row, Node="Ravager_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
