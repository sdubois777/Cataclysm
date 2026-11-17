"""The row shape Reaper's Embrace's six-piece bonus needs, through the generator.

The owner's ruling #8 of 2026-10-17 (a Soul is picked up by walking within about
1.5 m and lasts the floor) and the coordinating session's rulings R1-R8 of
2026-10-18, for "When a minion or pet dies, it leaves behind a "Soul" that can be
picked up. Picking up a Soul grants a stack of Soul Strike that increases your
damage by 10% for 5 seconds. Max 10 stacks."

THREE ROWS. `minion_death_leaves_soul`, a flat 1; and Soul Strike as two
own-stack rows (R5), `attack_damage` and `spell_damage` increased 10, step 1, on
`soul_picked_up`, at most 10 steps, 5 s. The rest is the engine's
(`ACataclysmReaperSoul`, the commander's death hook, the player controller's
pass, the floor's clearing). THE ENCHANTMENT SESSION AUTHORS THE ROWS; THIS FILE
AUTHORS NOTHING. It builds a made-up workbook holding the sentence typed
Generic, so the set rule (a set written whole or not at all) does not ask for
the rest of the set, as `test_demon_kings_regalia_ten_rows.py` does.
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

WORDS = ("Reaper's Embrace (6-Piece Bonus): When a minion or pet dies, it "
         "leaves behind a \"Soul\" that can be picked up. Picking up a Soul "
         "grants a stack of Soul Strike that increases your damage by 10% for "
         "5 seconds. Max 10 stacks.")
NAME = gen.row_name("Positive", WORDS[:48])
FLAG = "minion_death_leaves_soul"
EVENT = "soul_picked_up"

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]


def row(stat: str, kind: str, value: float, **cells) -> dict:
    return dict({"Enchantment": NAME, "Effect": WORDS, "Stat": stat,
                 "Value Kind": kind, "Value Low": value,
                 "Value High": value}, **cells)


#: Soul Strike's cells, the same on both damage rows (R5).
SOUL_STRIKE = {"Scale": "own_stacks", "Scale Step": 1, "Action Event": EVENT,
               "Scale Max Steps": 10, "Stack Seconds": 5}

#: The three rows, cell by cell, as the enchantment session is to write them.
ROWS = [
    row(FLAG, "flat", 1),
    row("attack_damage", "increased", 10, **SOUL_STRIKE),
    row("spell_damage", "increased", 10, **SOUL_STRIKE),
]

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
#: The engine file that holds the flag's name and raises the event.
SOUL = SOURCE / "AbilitySystem" / "CataclysmReaperSoul.cpp"
#: The commander's death hook, which leaves the Soul.
HOOK = SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"
#: The player controller, whose every-frame pass takes it.
CONTROLLER = SOURCE / "Player" / "CataclysmPlayerController.cpp"
#: The floor's clearing, which ends it.
FLOOR = SOURCE / "Dungeon" / "CataclysmFloorContents.cpp"


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 18, "Type.Minion", None, None, None, None, None],
    ]
    sheet = [HEADER] + [[each.get(column) for column in HEADER]
                        for each in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "reapers_embrace_six.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_reaper_soul_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def flag_row(tmp_path) -> dict:
    """The carried row on the flag."""
    (found,) = [each for each in effects(tmp_path, *ROWS)
                if each["Stat"] == FLAG]
    return found


def body_of(text: str, opening: str) -> str:
    """From a definition to the closing brace at column nought, or empty."""
    start = text.find(opening)
    if start < 0:
        return ""
    end = text.find("\n}", start)
    return text[start:end] if end > start else ""


# ---- the rows pass, and say what they should

def test_the_row_name_is_the_shipped_sentences_name():
    assert NAME == "Positive_Reaper_s_Embrace_6_Piece_Bonus_When_a_minion"


def test_the_three_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *ROWS)
    assert [(each["Stat"], each["ValueKind"], each["ValueLow"],
             each["ValueHigh"], each["RequiredTags"], each["Condition"],
             each["Scale"], each["ScaleStep"], each["Action"],
             each["ActionEvent"], each["ScaleMaxSteps"],
             each["StackSeconds"], each["TriggerCooldown"])
            for each in out] == [
        (FLAG, "flat", 1.0, 1.0, "", "", "", 0.0, "", "", 0, 0.0, 0.0),
        ("attack_damage", "increased", 10.0, 10.0, "", "", "own_stacks", 1.0,
         "", EVENT, 10, 5.0, 0.0),
        ("spell_damage", "increased", 10.0, 10.0, "", "", "own_stacks", 1.0,
         "", EVENT, 10, 5.0, 0.0)]
    assert refusals(out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    # THE FLAG'S ROW THROUGH THE WHOLE VALIDATION. The two damage rows are left
    # out of this one call only because this made-up workbook has no class
    # lines to supply `attack_damage`'s and `spell_damage`'s bases, the real
    # sheet's do, and the shipped rows of this shape (EnchantmentEffects.csv
    # 307 and 308) pass it. Put through, they are refused for that reason and
    # for no other.
    assert gen.validate_enchantment_effects(
        {"EnchantmentEffects": out[:1]}, set()) == []
    unsupplied = gen.validate_enchantment_effects(
        {"EnchantmentEffects": out}, set())
    assert len(unsupplied) == 2
    assert all("is not a stat any class line" in problem
               for problem in unsupplied)


def test_the_whole_validation_reads_this_check():
    """THE CONTROL OF THE LAST LINE ABOVE: a wrong row put through the whole
    sheet's validation is refused by this check's own sentence, so the
    validation does call it."""
    wrong = {"Name": f"{NAME}#1", "Enchantment": NAME, "Stat": FLAG,
             "ValueKind": "flat", "ValueLow": 2.0, "ValueHigh": 2.0,
             "RequiredTags": "", "Action": ""}
    assert any(FLAG in problem and "states 1 and nothing else" in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_the_flag_is_a_stat_with_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert gen.REAPER_SOUL_STAT == FLAG
    names = gen.stats_with_no_attribute()
    assert FLAG in names
    assert "minion_death_leaves_souls" not in names


def test_the_generator_holds_exactly_the_name_the_engine_asks_for():
    """Read from the file that defines it. THE CONTROL is the Tyrant's Minion's
    seconds in `CataclysmMinion.cpp`, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    minion = SOURCE / "AbilitySystem" / "CataclysmMinion.cpp"
    assert ("ACataclysmMinion", "TyrantsMinionSecondsStat",
            "minion_death_summons_tyrants_minion_seconds") in re.findall(
                pattern, minion.read_text(encoding="utf-8"))
    assert ("ACataclysmReaperSoul", "MinionDeathLeavesSoulStat", FLAG) in \
        re.findall(pattern, SOUL.read_text(encoding="utf-8"))


def test_the_event_has_no_clock_and_is_on_no_narrower_list():
    assert EVENT in gen.ACTION_ONLY_EVENTS
    assert EVENT in gen.granting_events()
    assert "seconds_after_" + EVENT not in gen.CONDITIONS
    assert EVENT not in gen.HIT_FIRED_EVENTS
    assert EVENT not in gen.EVENTS_WITH_AN_AMOUNT
    assert EVENT not in gen.STRIKE_TARGET_EVENTS
    assert EVENT not in gen.EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK
    assert EVENT not in gen.CONSECUTIVE_HIT_EVENTS


def test_the_engine_raises_the_event_once_and_only_where_a_soul_is_taken():
    """Read from the C++. THE CONTROL is the function that leaves a Soul,
    which must not raise it."""
    source = SOUL.read_text(encoding="utf-8")
    call = 'ActOnEvent(FName(TEXT("soul_picked_up")));'
    assert source.count(call) == 1
    taking = body_of(source, "int32 ACataclysmReaperSoul::TakeSoulsNear(")
    leaving = body_of(source, "ACataclysmReaperSoul* ACataclysmReaperSoul::LeaveFor(")
    assert taking and leaving
    assert call in taking
    assert call not in leaving


def test_the_hook_leaves_a_soul_after_the_death_for_every_kind_it_answers():
    """R1, read from the C++: one call, after the death is handled, and not
    inside the block a Tyrant's Minion's death skips. THE CONTROL is the
    replacement, which is inside that block."""
    hook = body_of(HOOK.read_text(encoding="utf-8"),
                   "void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero()")
    call = "ACataclysmReaperSoul::LeaveFor(Commander, Where);"
    assert hook.count(call) == 1
    assert hook.index("Character->HandleDeath();") < hook.index(call)
    skipped = hook.index("if (Commander && !bWasTyrantsMinion)")
    replaced = hook.index("UCataclysmSummonSkill::ReplaceLost(")
    closing = hook.index("\n\t}\n", skipped)
    assert skipped < replaced < closing
    assert not skipped < hook.index(call) < closing


def test_the_controller_looks_every_frame_beside_the_material_pick_up():
    """R4, read from the C++: no automation test reaches the controller's
    frame. THE CONTROL is the material pass the call stands beside."""
    frame = body_of(CONTROLLER.read_text(encoding="utf-8"),
                    "void ACataclysmPlayerController::PostProcessInput(")
    materials = "CollectMaterialsNearby();"
    call = "ACataclysmReaperSoul::TakeSoulsNear(GetPawn());"
    assert frame.count(materials) == 1
    assert frame.count(call) == 1
    assert frame.index(materials) < frame.index(call)


def test_the_floors_clearing_ends_every_soul_in_both_branches():
    """R7, read from the C++: after the branch on carrying followers closes,
    beside the drops. THE CONTROL is the drops' own line."""
    clearing = body_of(FLOOR.read_text(encoding="utf-8"),
                       "int32 UCataclysmFloorContents::ClearTheFloor(")
    drops = "FloorContentsDestroyEvery<ACataclysmDroppedItem>(World);"
    souls = "FloorContentsDestroyEvery<ACataclysmReaperSoul>(World);"
    assert clearing.count(drops) == 1
    assert clearing.count(souls) == 1
    assert clearing.index(drops) < clearing.index(souls)


def test_a_soul_strike_row_on_an_event_the_game_does_not_raise_is_refused(
        tmp_path):
    strike = dict(ROWS[1], **{"Action Event": "soul_taken"})
    with pytest.raises(gen.DataError, match="to grant them"):
        effects(tmp_path, strike)


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("change, said", [
    ({"ValueKind": "increased"}, "value kind 'increased'"),
    ({"ValueKind": "more"}, "value kind 'more'"),
    ({"RequiredTags": "Type.Minion"}, "under a required tag"),
    ({"Action": "apply_status"}, "with the action 'apply_status'"),
    ({"ValueLow": 0.0, "ValueHigh": 0.0}, "states 1 and nothing else"),
    ({"ValueLow": 2.0, "ValueHigh": 2.0}, "states 1 and nothing else"),
    ({"ValueLow": 0.5, "ValueHigh": 0.5}, "states 1 and nothing else"),
])
def test_every_shape_the_game_would_give_nothing_for_is_refused(
        tmp_path, change, said):
    """One change to the carried row at a time, each refused once and by its
    own sentence. The control is the first assertion."""
    flag = flag_row(tmp_path)
    assert refusals([flag]) == []
    found = refusals([dict(flag, **change)])
    assert len(found) == 1, found
    assert said in found[0]


def test_a_range_on_the_flag_is_refused_at_its_wrong_end_only(tmp_path):
    found = refusals([dict(flag_row(tmp_path), ValueLow=1.0, ValueHigh=2.0)])
    assert len(found) == 1, found
    assert "states 2 on" in found[0]


def test_a_condition_or_a_scale_on_the_flag_is_refused_elsewhere(tmp_path):
    """Nothing asks the flag under a condition or a scale, so the general
    checks refuse both. THE CONTROL is the ruled row, which they do not."""
    flag = flag_row(tmp_path)
    assert gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", [flag]) == []
    assert gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", [dict(flag, Scale="own_stacks", ScaleStep=1.0)])
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [flag]) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [dict(flag, Condition="health_below",
                                    ConditionValue=50.0)])


def test_a_row_on_another_stat_or_a_passive_node_is_not_this_checks_business(
        tmp_path):
    flag = flag_row(tmp_path)
    assert refusals([dict(flag, Stat="attack_damage", ValueKind="increased",
                          ValueLow=10.0, ValueHigh=10.0)]) == []
    assert refusals([dict(flag, Node="Ritualist_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
