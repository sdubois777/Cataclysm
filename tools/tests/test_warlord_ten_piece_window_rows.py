"""The row shape Warlord's Will's ten-piece bonus needs, through the generator.

"Warlord's Will (10-Piece Bonus): Every 30 seconds, you gain a stack of
'Tactical Advantage.' When you use a movement ability, you consume all stacks of
'Tactical Advantage' and gain 100% more movement speed and 50% more damage for 5
seconds. This bonus scales with the number of enemies in the area"

Built as Divine Retribution's ten-piece was, by the precedent the owner
accepted on 2026-10-08 and 2026-10-09: no stack is counted, and the 30 seconds
is a trigger cooldown. Three rows scaled by their own stacks on
`movement_skill`. The owner ruled on 2026-10-17 that the last sentence is "+10%
more damage per enemy within 8 m, capped at 5 enemies", and that the movement
speed half does not scale: two more rows, scaled by
`enemies_in_reach_while_own_stack`, one beside each damage row.

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
from test_generate_datatables import (  # noqa: E402
    TestARowCountingNearbyEnemiesCarriesItsOwnRadius,
    workbook_with,
)

WORDS = ("Warlord's Will (10-Piece Bonus): Every 30 seconds, you gain a stack of "
         "'Tactical Advantage.' When you use a movement ability, you consume all "
         "stacks of 'Tactical Advantage' and gain 100% more movement speed and "
         "50% more damage for 5 seconds. This bonus scales with the number of "
         "enemies in the area")
NAME = gen.row_name("Positive", WORDS[:48])

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]


def window_row(stat: str, value: float) -> dict:
    """One of the three rows that open the 5 s window on a movement use."""
    return {"Enchantment": NAME, "Effect": WORDS, "Stat": stat,
            "Value Kind": "more", "Value Low": value, "Value High": value,
            "Scale": "own_stacks", "Scale Step": 1,
            "Action Event": "movement_skill", "Scale Max Steps": 1,
            "Stack Seconds": 5, "Trigger Cooldown": 30}


def enemy_row(stat: str) -> dict:
    """One of the two rows that add 10% more per enemy while the window is open."""
    return {"Enchantment": NAME, "Effect": WORDS, "Stat": stat,
            "Value Kind": "more", "Value Low": 10, "Value High": 10,
            "Scale": gen.WINDOW_SCALE, "Scale Step": 1,
            "Scale Max Steps": 5}


#: The five rows, cell by cell, as the enchantment session is to write them.
ROWS = [window_row("movement_speed", 100), window_row("attack_damage", 50),
        window_row("spell_damage", 50), enemy_row("attack_damage"),
        enemy_row("spell_damage")]


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 2, "Stat.Utility.Movespeed, Slot.Movement", None,
         None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "warlord_ten_piece.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_window_scale_with_no_own_stack_row(
        "EnchantmentEffects", rows)


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    assert NAME == "Positive_Warlord_s_Will_10_Piece_Bonus_Every_30_second"


def test_the_five_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *ROWS)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["Scale"],
             row["ActionEvent"], row["ScaleMaxSteps"], row["StackSeconds"],
             row["TriggerCooldown"], row["RequiredTags"], row["Condition"])
            for row in out] == [
        ("movement_speed", "more", 100.0, "own_stacks", "movement_skill", 1,
         5.0, 30.0, "", ""),
        ("attack_damage", "more", 50.0, "own_stacks", "movement_skill", 1,
         5.0, 30.0, "", ""),
        ("spell_damage", "more", 50.0, "own_stacks", "movement_skill", 1,
         5.0, 30.0, "", ""),
        ("attack_damage", "more", 10.0, gen.WINDOW_SCALE, "", 5, 0.0, 0.0,
         "", ""),
        ("spell_damage", "more", 10.0, gen.WINDOW_SCALE, "", 5, 0.0, 0.0,
         "", "")]
    assert refusals(out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.refuse_a_reach_scale_a_row_cannot_state(
        "EnchantmentEffects", out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []
    # THE WHOLE VALIDATION IS NOT RUN HERE: it asks the class sheets for every
    # stat's base, and a made-up workbook has none. All three stats are on
    # merged rows of game/Data/EnchantmentEffects.csv already (the Demon King
    # 6-piece states both damage stats).


def test_the_scale_is_one_the_engine_names_and_counts_one_enemy_a_step():
    """THE CONTROL is `enemies_in_reach`, which this scale is not: that one
    needs a reach column the sheet does not have and is refused on it."""
    assert gen.SCALES[gen.WINDOW_SCALE][:2] == (1.0, 1.0)
    assert gen.WINDOW_SCALE not in gen.SCALES_THAT_COUNT_ENEMIES_IN_A_RADIUS
    assert "enemies_in_reach" in gen.SCALES_THAT_COUNT_ENEMIES_IN_A_RADIUS
    source = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmStatPipeline.cpp").read_text(encoding="utf-8")
    assert (f'{{ TEXT("{gen.WINDOW_SCALE}"), '
            f'ECataclysmStatScale::PerEnemyInReachWhileOwnStackHeld }}') in source


def test_the_loader_states_the_rulings_eight_metres():
    """The sheet has no reach column, so the 8 m is the loader's constant. THE
    CONTROL: the 5 enemies is not in the engine; it is the row's cap."""
    header = (ROOT / "game" / "Source" / "Cataclysm" / "Items"
              / "CataclysmItem.h").read_text(encoding="utf-8")
    assert "static constexpr float OwnStackWindowReachMetres = 8.0f;" in header
    assert "OwnStackWindowReachMetres = 5" not in header


# ---- wrong rows are refused

def test_an_enemy_row_with_no_window_row_beside_it_is_refused(tmp_path):
    """THE CONTROL is the whole set of five, refused by nothing above."""
    out = effects(tmp_path, enemy_row("attack_damage"))
    problems = refusals(out)
    assert len(problems) == 1 and "own_stacks row" in problems[0]


def test_a_window_row_on_another_stat_does_not_open_the_enemy_rows_window(
        tmp_path):
    out = effects(tmp_path, window_row("movement_speed", 100),
                  enemy_row("attack_damage"))
    problems = refusals(out)
    assert len(problems) == 1 and "'attack_damage'" in problems[0]


def test_an_enemy_row_with_no_cap_is_refused(tmp_path):
    out = effects(tmp_path, window_row("attack_damage", 50),
                  dict(enemy_row("attack_damage"), **{"Scale Max Steps": None}))
    problems = refusals(out)
    assert len(problems) == 1 and "no Scale Max Steps" in problems[0]


def test_an_enemy_row_stating_its_own_window_is_refused(tmp_path):
    """It grants no stack, so Stack Seconds, an event and a cooldown on it would
    be dropped. Each against the control that the plain row passes."""
    assert effects(tmp_path, window_row("attack_damage", 50),
                   enemy_row("attack_damage"))
    with pytest.raises(gen.DataError, match="Stack Seconds"):
        effects(tmp_path, dict(enemy_row("attack_damage"), **{"Stack Seconds": 5}))
    with pytest.raises(gen.DataError, match="names the event"):
        effects(tmp_path, dict(enemy_row("attack_damage"),
                               **{"Action Event": "movement_skill"}))
    with pytest.raises(gen.DataError, match="Trigger"):
        effects(tmp_path, dict(enemy_row("attack_damage"),
                               **{"Trigger Cooldown": 30}))


def test_a_step_of_two_enemies_is_refused(tmp_path):
    with pytest.raises(gen.DataError, match="scaling step"):
        effects(tmp_path, dict(enemy_row("attack_damage"), **{"Scale Step": 2}))


def test_the_scale_is_refused_on_a_passive_row(tmp_path):
    """A passive node has no own stacks. THE CONTROL is the same row scaled by
    a count the passive sheet does carry."""
    passive = TestARowCountingNearbyEnemiesCarriesItsOwnRadius()
    gen.passive_effects(passive.book(tmp_path, [
        ["A_node", "armor", "increased", 3,
         None, None, "seconds_in_combat", 1, None]]))
    with pytest.raises(gen.DataError, match="has no own stacks"):
        gen.passive_effects(passive.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             None, None, gen.WINDOW_SCALE, 1, None]]))
