"""The row shape the Demon King's Regalia's ten-piece bonus needs, through the generator.

Ruled 2026-10-17 by the coordinating session (the window to make the kill is
the 3 s of immunity; "Rare or higher" is Elite and above) and 2026-10-18
(Q1-Q7), for "If you would be killed during Demonic Rage, instead gain 100% crit
chance and crit multiplier and become immune to all damage for 3 seconds.
Killing a Rare or higher enemy instanly heals you to full, failing to results
in your death" (the two typos are the sentence's).

FIVE ROWS. `lethal_hit_reprieve_every_seconds`, a flat 10 scaled by its own
stacks on the Demonic Rage's own event, threshold, length and cooldown
(`health_falls_below`, 25, one stack for 10 s, 300 s), so the reprieve is held
only while the Rage is and at most once a Rage; `lethal_hit_reprieve_seconds`, a
flat 3; `lethal_hit_reprieve_kill_rarity_at_least`, a flat 1, Elite; and
`crit_chance` and `crit_multiplier`, each a flat 100 scaled by its own stacks on
`lethal_hit_reprieved`, one stack for 3 s. The rest is the engine's
(`UCataclysmAbilitySystemComponent::NoteLethalHitReprieved` and the sentence
beside it). THE ENCHANTMENT SESSION AUTHORS THE ROWS; THIS FILE AUTHORS NOTHING.
It builds a made-up workbook holding the sentence typed Generic, so the set rule
(a set written whole or not at all) does not ask for the rest of the set, as
`test_tyrants_chains_six_rows.py` does.
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

WORDS = ("Demon King's Regalia (10-Piece Bonus): If you would be killed during "
         "Demonic Rage, instead gain 100% crit chance and crit multiplier and "
         "become immune to all damage for 3 seconds. Killing a Rare or higher "
         "enemy instanly heals you to full, failing to results in your death")
NAME = gen.row_name("Positive", WORDS[:48])
EVERY = "lethal_hit_reprieve_every_seconds"
SECONDS = "lethal_hit_reprieve_seconds"
RARITY = "lethal_hit_reprieve_kill_rarity_at_least"
EVENT = "lethal_hit_reprieved"

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]


def flat(stat: str, value: float, **cells) -> dict:
    return dict({"Enchantment": NAME, "Effect": WORDS, "Stat": stat,
                 "Value Kind": "flat", "Value Low": value,
                 "Value High": value}, **cells)


#: The five rows, cell by cell, as the enchantment session is to write them.
ROWS = [
    flat(EVERY, 10, **{"Scale": "own_stacks", "Scale Step": 1,
                       "Action Event": "health_falls_below",
                       "Scale Max Steps": 1, "Stack Seconds": 10,
                       "Trigger Cooldown": 300, "Event Value": 25}),
    flat(SECONDS, 3),
    flat(RARITY, 1),
    flat("crit_chance", 100, **{"Scale": "own_stacks", "Scale Step": 1,
                                "Action Event": EVENT, "Scale Max Steps": 1,
                                "Stack Seconds": 3}),
    flat("crit_multiplier", 100, **{"Scale": "own_stacks", "Scale Step": 1,
                                    "Action Event": EVENT,
                                    "Scale Max Steps": 1, "Stack Seconds": 3}),
]

#: The engine file that holds the three new stats' names and raises the event.
ABILITY_SYSTEM = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                  / "CataclysmAbilitySystemComponent.cpp")


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 11, "Stat.Offense.Crit, Trigger.LowLife, "
         "Trigger.OnKill", None, None, None, None, None],
    ]
    sheet = [HEADER] + [[row.get(column) for column in HEADER] for row in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "demon_kings_regalia_ten.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_lethal_hit_reprieve_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def row_on(tmp_path, stat: str) -> dict:
    """The carried row on one of the five stats."""
    (row,) = [row for row in effects(tmp_path, *ROWS) if row["Stat"] == stat]
    return row


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    assert NAME == "Positive_Demon_King_s_Regalia_10_Piece_Bonus_If_you_wo"


def test_the_five_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *ROWS)
    assert [(row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"],
             row["RequiredTags"], row["Condition"], row["Scale"],
             row["Action"], row["ActionEvent"], row["ScaleMaxSteps"],
             row["StackSeconds"], row["TriggerCooldown"], row["EventValue"])
            for row in out] == [
        (EVERY, "flat", 10.0, 10.0, "", "", "own_stacks", "",
         "health_falls_below", 1, 10.0, 300.0, 25.0),
        (SECONDS, "flat", 3.0, 3.0, "", "", "", "", "", 0, 0.0, 0.0, 0.0),
        (RARITY, "flat", 1.0, 1.0, "", "", "", "", "", 0, 0.0, 0.0, 0.0),
        ("crit_chance", "flat", 100.0, 100.0, "", "", "own_stacks", "", EVENT,
         1, 3.0, 0.0, 0.0),
        ("crit_multiplier", "flat", 100.0, 100.0, "", "", "own_stacks", "",
         EVENT, 1, 3.0, 0.0, 0.0)]
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
    wrong = {"Name": f"{NAME}#3", "Enchantment": NAME, "Stat": RARITY,
             "ValueKind": "flat", "ValueLow": 0.5, "ValueHigh": 0.5,
             "RequiredTags": "", "Action": ""}
    assert any(RARITY in problem and "one whole rung" in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_rows_were_the_old_rule_refused_by_the_scale_check():
    """WHY TWO STATS JOINED `STATS_WITH_AN_ASKER`. Without them the scale
    check refuses the interval's row and the multiplier's. THE CONTROL is the
    chance's row, which it accepted before."""
    rows = [{"Name": f"{NAME}#{index}", "Stat": stat, "Scale": "own_stacks"}
            for index, stat in enumerate((EVERY, "crit_multiplier",
                                          "crit_chance"), start=1)]
    assert {EVERY, "crit_multiplier", "crit_chance"} <= gen.STATS_WITH_AN_ASKER
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", rows) == []
    unasked = [dict(row, Stat=f"{row['Stat']}_made_up") for row in rows]
    assert len(gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", unasked)) == 3


def test_the_three_new_stats_are_ones_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.LETHAL_HIT_REPRIEVE_STATS == (EVERY, SECONDS, RARITY)
    names = gen.stats_with_no_attribute()
    assert {EVERY, SECONDS, RARITY} <= names
    assert "lethal_hit_reprieve" not in names


def test_the_generator_holds_exactly_the_names_the_engine_asks_for():
    """Read from the file that defines them. THE CONTROL is Nothing Stops It's
    interval in the same file, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    found = re.findall(pattern, ABILITY_SYSTEM.read_text(encoding="utf-8"))
    owner = "UCataclysmAbilitySystemComponent"
    assert (owner, "LethalHitSurvivedEverySecondsStat",
            "lethal_hit_survived_every_seconds") in found
    assert (owner, "LethalHitReprieveEverySecondsStat", EVERY) in found
    assert (owner, "LethalHitReprieveSecondsStat", SECONDS) in found
    assert (owner, "LethalHitReprieveKillRarityStat", RARITY) in found


def test_the_event_has_no_clock_and_is_on_no_narrower_list():
    assert EVENT in gen.ACTION_ONLY_EVENTS
    assert EVENT in gen.granting_events()
    assert "seconds_after_" + EVENT not in gen.CONDITIONS
    assert EVENT not in gen.HIT_FIRED_EVENTS
    assert EVENT not in gen.EVENTS_WITH_AN_AMOUNT
    assert EVENT not in gen.STRIKE_TARGET_EVENTS
    assert EVENT not in gen.EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK
    assert EVENT not in gen.CONSECUTIVE_HIT_EVENTS


def test_the_engine_raises_the_event_once_and_from_the_reprieve_alone():
    """Read from the C++. THE CONTROL is Nothing Stops It's note, which must
    not raise it: a wearer holding both gains the critical strike only from
    the reprieve."""
    source = ABILITY_SYSTEM.read_text(encoding="utf-8")
    call = 'ActOnEvent(FName(TEXT("lethal_hit_reprieved")));'
    assert source.count(call) == 1
    reprieve = source.index(
        "void UCataclysmAbilitySystemComponent::NoteLethalHitReprieved()")
    survived = source.index(
        "void UCataclysmAbilitySystemComponent::NoteLethalHitSurvived(")
    at = source.index(call)
    assert reprieve < at < source.index("\n}\n", reprieve)
    assert not survived < at < source.index("\n}\n", survived)


def test_the_same_row_on_an_event_the_game_does_not_raise_is_refused(tmp_path):
    crit = dict(ROWS[3], **{"Action Event": "lethal_hit_survived"})
    with pytest.raises(gen.DataError, match="to grant them"):
        effects(tmp_path, crit)


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("stat, change, said", [
    (EVERY, {"ValueKind": "more"}, "value kind 'more'"),
    (EVERY, {"RequiredTags": "Type.Melee"}, "under a required tag"),
    (EVERY, {"Scale": "enemies_in_reach"}, "by its own stacks and by nothing"),
    (EVERY, {"ValueLow": 0.0, "ValueHigh": 0.0}, "must be above 0"),
    (EVERY, {"ValueLow": 12.0, "ValueHigh": 8.0}, "the range must rise"),
    (SECONDS, {"ValueKind": "increased"}, "value kind 'increased'"),
    (SECONDS, {"Action": "apply_status"}, "with the action 'apply_status'"),
    (SECONDS, {"ValueLow": -3.0, "ValueHigh": -3.0}, "must be above 0"),
    (RARITY, {"ValueLow": 0.0, "ValueHigh": 0.0}, "one whole rung"),
    (RARITY, {"ValueLow": 6.0, "ValueHigh": 6.0}, "one whole rung"),
    (RARITY, {"ValueLow": 1.5, "ValueHigh": 1.5}, "one whole rung"),
    (RARITY, {"ValueLow": 1.0, "ValueHigh": 2.0}, "not a range"),
    (RARITY, {"RequiredTags": "Type.Melee"}, "under a required tag"),
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


def test_an_unscaled_interval_and_a_rising_range_of_seconds_are_accepted(
        tmp_path):
    every = row_on(tmp_path, EVERY)
    assert refusals([dict(every, Scale="")]) == []
    seconds = row_on(tmp_path, SECONDS)
    assert refusals([dict(seconds, ValueLow=2.0, ValueHigh=4.0)]) == []
    rarity = row_on(tmp_path, RARITY)
    assert refusals([dict(rarity, ValueLow=5.0, ValueHigh=5.0)]) == []


def test_a_scale_on_the_seconds_or_the_rung_is_refused_elsewhere(tmp_path):
    """Nothing asks either under a scale, so the general check refuses both.
    THE CONTROL is each ruled row, which it does not refuse."""
    for stat in (SECONDS, RARITY):
        row = row_on(tmp_path, stat)
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [row]) == []
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [dict(row, Scale="own_stacks",
                                        ScaleStep=1.0)])


def test_a_row_on_another_stat_or_a_passive_node_is_not_this_checks_business(
        tmp_path):
    row = row_on(tmp_path, RARITY)
    assert refusals([dict(row, Stat="attack_damage", ValueKind="increased")]) == []
    assert refusals([dict(row, Node="Ravager_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
