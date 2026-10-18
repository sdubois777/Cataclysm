"""The row shapes of Spellblade's Will's six-piece bonus, Dervish and Spellslinger, through the generator.

"Spellblade's Will (6-Piece Bonus): When you use a melee attack, you gain a stack
of Spellslinger which reduces the mana cost of your spells by 10% per stack, and
when you use a spell, you gain a stack of Dervish which increases your attack
speed for melee attacks by 10% per stack"

Ruled 2026-10-06: 5 stacks lasting 5 s, and "Dervish raises every swing", so the
attack speed is read with no tags. DERVISH IS ONE ROW AND NEEDS NO GAME CHANGE:
`attack_speed`, increased 10, scaled by its own stacks, granted on `spell`, the
event a paid use of a skill tagged `Type.Spell` raises.

SPELLSLINGER, ruled 2026-10-17 (R1): `mana_cost`, more -10, Required Tags
`Type.Spell`, scaled by its own stacks, granted on `melee_attack_use`, the new
event a paid use tagged `Type.Melee` raises, the basic attack included; five
stacks lasting 5 s. `mana_cost` joined `STATS_WITH_AN_ASKER` for it.

THE ENCHANTMENT SESSION AUTHORS THE ROWS; THIS FILE AUTHORS NOTHING. It builds a
made-up workbook holding the sentence typed Generic, so the set rule (a set
written whole or not at all) does not ask for the rest of the set, as
`test_null_emperor_void_aura_rows.py` does.
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

WORDS = ("Spellblade's Will (6-Piece Bonus): When you use a melee attack, you "
         "gain a stack of Spellslinger which reduces the mana cost of your "
         "spells by 10% per stack, and when you use a spell, you gain a stack of "
         "Dervish which increases your attack speed for melee attacks by 10% per "
         "stack")
NAME = gen.row_name("Positive", WORDS[:48])

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: The Dervish row, cell by cell, as the enchantment session is to write it.
DERVISH_ROW = {"Enchantment": NAME, "Effect": WORDS, "Stat": "attack_speed",
               "Value Kind": "increased", "Value Low": 10, "Value High": 10,
               "Scale": "own_stacks", "Scale Step": 1, "Action Event": "spell",
               "Scale Max Steps": 5, "Stack Seconds": 5}


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 10, "Type.Strike, Stat.Resource.Mana, Type.Spell, "
         "Stat.Offense.Speed", None, None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "spellblade_six_piece.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def test_the_row_name_is_the_shipped_sentences_name():
    assert NAME == "Positive_Spellblade_s_Will_6_Piece_Bonus_When_you_use"


def test_the_dervish_row_is_carried_through_and_nothing_refuses_it(tmp_path):
    out = effects(tmp_path, DERVISH_ROW)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Scale"], row["ActionEvent"],
             row["ScaleMaxSteps"], row["StackSeconds"], row["TriggerCooldown"])
            for row in out] == [
        ("attack_speed", "increased", 10.0, 10.0, "", "own_stacks", "spell", 5,
         5.0, 0.0)]
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []


def test_spell_is_the_event_of_a_clock_the_game_raises():
    """`spell` is granted on because `seconds_after_spell` is a clock, which is
    how every clock's event becomes one a row may name. THE CONTROL is a name
    with no clock."""
    assert "seconds_after_spell" in gen.CONDITIONS
    assert "spell" in gen.granting_events()
    assert "seconds_after_spell_cast" not in gen.CONDITIONS
    assert "spell_cast" not in gen.granting_events()


def test_the_same_row_on_an_event_the_game_does_not_raise_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="to grant them"):
        effects(tmp_path, dict(DERVISH_ROW, **{"Action Event": "spell_cast"}))


def test_the_row_with_no_cap_or_no_seconds_is_refused(tmp_path):
    """"5 stacks lasting 5 s", ruled: both are required on an own stack."""
    with pytest.raises(gen.DataError, match="no cap"):
        effects(tmp_path, dict(DERVISH_ROW, **{"Scale Max Steps": None}))
    with pytest.raises(gen.DataError, match="no Stack Seconds"):
        effects(tmp_path, dict(DERVISH_ROW, **{"Stack Seconds": None}))


#: The Spellslinger row, cell by cell, as the enchantment session is to write it.
SPELLSLINGER_ROW = {"Enchantment": NAME, "Effect": WORDS, "Stat": "mana_cost",
                    "Value Kind": "more", "Value Low": -10, "Value High": -10,
                    "Required Tags": "Type.Spell", "Scale": "own_stacks",
                    "Scale Step": 1, "Action Event": "melee_attack_use",
                    "Scale Max Steps": 5, "Stack Seconds": 5}


def test_the_spellslinger_row_is_carried_through_and_nothing_refuses_it(tmp_path):
    out = effects(tmp_path, SPELLSLINGER_ROW, DERVISH_ROW)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Scale"], row["ActionEvent"],
             row["ScaleMaxSteps"], row["StackSeconds"], row["TriggerCooldown"])
            for row in out] == [
        ("mana_cost", "more", -10.0, -10.0, "Type.Spell", "own_stacks",
         "melee_attack_use", 5, 5.0, 0.0),
        ("attack_speed", "increased", 10.0, 10.0, "", "own_stacks", "spell", 5,
         5.0, 0.0)]
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []


def test_mana_cost_is_now_a_stat_something_asks_for():
    """THE CONTROL is the same row on a stat nothing asks for, which the scale
    check still refuses."""
    assert "mana_cost" in gen.STATS_WITH_AN_ASKER
    row = {"Name": "A#1", "Stat": "mana_cost", "Scale": "own_stacks"}
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", [row]) == []
    unasked = dict(row, Stat="an_invented_stat")
    assert len(gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", [unasked])) == 1


def test_melee_attack_use_is_an_event_with_no_clock_and_on_no_narrower_list():
    event = "melee_attack_use"
    assert event in gen.ACTION_ONLY_EVENTS
    assert event in gen.action_events()
    assert event in gen.granting_events()
    assert "seconds_after_" + event not in gen.CONDITIONS
    assert event not in gen.HIT_FIRED_EVENTS
    assert event not in gen.EVENTS_WITH_AN_AMOUNT
    assert event not in gen.STRIKE_TARGET_EVENTS
    assert event not in gen.EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK
    assert event not in gen.CONSECUTIVE_HIT_EVENTS
    assert gen.REPEAT_SKILL_EVENTS == ("skill_use", "attack_use")


def test_the_engine_raises_the_event_for_a_melee_use_after_attack_use():
    """Read from the C++, the file `test_pool_action_names_match_the_engine.py`
    reads. THE CONTROL is `attack_use`, raised just before it."""
    source = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmAbilitySystemComponent.cpp").read_text(encoding="utf-8")
    attack = source.index('ActOnEvent(FName(TEXT("attack_use")), SkillTags);')
    melee = source.index('ActOnEvent(FName(TEXT("melee_attack_use")), SkillTags);')
    assert attack < melee
    between = source[attack:melee]
    assert 'TEXT("Type.Melee")' in between
    assert "bBasicAttack" not in between, (
        "the event must be raised for the basic attack too, as ruled 2026-10-17")


def test_the_row_on_attack_use_would_be_accepted_and_is_not_the_row(tmp_path):
    """Why the event exists: the generator accepts the row on `attack_use`,
    where every paid use, spells included, would grant it. The row to write
    names `melee_attack_use`."""
    out = effects(tmp_path, dict(SPELLSLINGER_ROW,
                                 **{"Action Event": "attack_use"}))
    assert out[0]["ActionEvent"] == "attack_use"
    assert SPELLSLINGER_ROW["Action Event"] == "melee_attack_use"
