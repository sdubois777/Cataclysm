"""The row shape Reaper's Embrace's ten-piece bonus needs, through the generator.

The owner's ruling #9 of 2026-10-17 (cull is a threshold of 5% of maximum health
per Soul Strike stack, capped by rarity: Common 50%, Elite and Legendary 25%,
Herald 10%, never a boss) and the coordinating session's rulings C1-C8 of
2026-10-19, for "For each stack of Soul Strike you gain 5% cull on hit. Killing
an enemy instanly spawns one of your minions, up to your max".

TWO ROWS. `cull_percent_per_soul_strike_stack`, a flat 5 (C1, C3), and
`minion_kill_spawns_minion`, a flat 1 (C6). The stacks are the six-piece's own
and the caps are the engine's, so neither is a cell. THE ENCHANTMENT SESSION
AUTHORS THE ROWS; THIS FILE AUTHORS NOTHING. It builds a made-up workbook holding
the sentence typed Generic, so the set rule (a set written whole or not at all)
does not ask for the rest of the set, as `test_reapers_embrace_six_rows.py` does.

AND IT PINS THE EXECUTE ROWS THE CULL SHARES ITS EXECUTION WITH (C1): the
shipped table holds one row carrying `execute_target`, counted here by reading
the table, cell by cell.
"""

from __future__ import annotations

import csv
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

WORDS = ("Reaper's Embrace (10-Piece Bonus): For each stack of Soul Strike you "
         "gain 5% cull on hit. Killing an enemy instanly spawns one of your "
         "minions, up to your max")
NAME = gen.row_name("Positive", WORDS[:48])
CULL = "cull_percent_per_soul_strike_stack"
SPAWN = "minion_kill_spawns_minion"

HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]


def row(stat: str, value: float) -> dict:
    return {"Enchantment": NAME, "Effect": WORDS, "Stat": stat,
            "Value Kind": "flat", "Value Low": value, "Value High": value}


#: The two rows, cell by cell, as the enchantment session is to write them.
ROWS = [row(CULL, 5), row(SPAWN, 1)]

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
ASC = SOURCE / "AbilitySystem" / "CataclysmAbilitySystemComponent.cpp"
SUMMON = SOURCE / "AbilitySystem" / "CataclysmSkillTemplates.cpp"
PAWN = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
VITAL = SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"
SHIPPED = ROOT / "game" / "Data" / "EnchantmentEffects.csv"


def effects(tmp_path, *rows: dict) -> list[dict]:
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 18, "Type.Minion", None, None, None, None, None],
    ]
    sheet = [HEADER] + [[each.get(column) for column in HEADER]
                        for each in rows]
    book = openpyxl.load_workbook(workbook_with(
        tmp_path / "reapers_embrace_ten.xlsx",
        {"Enchantments": enchantments, "Enchantment Effects": sheet}))
    return gen.enchantment_effects(book)


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_reaper_ten_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def carried(tmp_path, stat: str) -> dict:
    (found,) = [each for each in effects(tmp_path, *ROWS)
                if each["Stat"] == stat]
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
    assert NAME == "Positive_Reaper_s_Embrace_10_Piece_Bonus_For_each_stac"
    with SHIPPED.with_name("EnchantmentsPositive.csv").open(
            encoding="utf-8", newline="") as handle:
        names = {each["Name"] for each in csv.DictReader(handle)}
    assert NAME in names


def test_the_two_rows_are_carried_through_and_nothing_refuses_them(tmp_path):
    out = effects(tmp_path, *ROWS)
    assert [(each["Stat"], each["ValueKind"], each["ValueLow"],
             each["ValueHigh"], each["RequiredTags"], each["Condition"],
             each["Scale"], each["Action"], each["ActionEvent"],
             each["TriggerCooldown"])
            for each in out] == [
        (CULL, "flat", 5.0, 5.0, "", "", "", "", "", 0.0),
        (SPAWN, "flat", 1.0, 1.0, "", "", "", "", "", 0.0)]
    assert refusals(out) == []
    assert gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.validate_enchantment_effects(
        {"EnchantmentEffects": out}, set()) == []


def test_the_whole_validation_reads_this_check():
    """THE CONTROL OF THE LAST LINE ABOVE: a wrong row put through the whole
    sheet's validation is refused by this check's own sentence."""
    wrong = {"Name": f"{NAME}#1", "Enchantment": NAME, "Stat": SPAWN,
             "ValueKind": "flat", "ValueLow": 2.0, "ValueHigh": 2.0,
             "RequiredTags": "", "Action": ""}
    assert any(SPAWN in problem and "states 1 and nothing else" in problem
               for problem in gen.validate_enchantment_effects(
                   {"EnchantmentEffects": [wrong]}, set()))


def test_both_stats_have_no_attribute():
    """TESTED BY NAME. The control is a made-up name that is on no list."""
    assert (gen.REAPER_CULL_STAT, gen.REAPER_SPAWN_STAT) == (CULL, SPAWN)
    names = gen.stats_with_no_attribute()
    assert CULL in names
    assert SPAWN in names
    assert "cull_percent_per_soul_strike_stacks" not in names


def test_the_generator_holds_exactly_the_names_the_engine_asks_for():
    """Read from the files that define them. THE CONTROL is the six-piece's
    flag, read by the same pattern."""
    pattern = r'(\w+)::(\w+Stat)\s*=\s*\n?\s*TEXT\("([^"]+)"\)'
    soul = SOURCE / "AbilitySystem" / "CataclysmReaperSoul.cpp"
    assert ("ACataclysmReaperSoul", "MinionDeathLeavesSoulStat",
            "minion_death_leaves_soul") in re.findall(
                pattern, soul.read_text(encoding="utf-8"))
    assert ("UCataclysmAbilitySystemComponent",
            "CullPercentPerSoulStrikeStackStat", CULL) in re.findall(
                pattern, ASC.read_text(encoding="utf-8"))
    assert ("UCataclysmSummonSkill", "KillSpawnsMinionStat", SPAWN) in \
        re.findall(pattern, SUMMON.read_text(encoding="utf-8"))


def test_the_engine_reads_the_soul_strike_stacks_by_the_six_pieces_event():
    """C1: the stacks are the own stacks granted on the event a Soul taken
    raises. THE CONTROL is the event's name as the Soul raises it."""
    soul = (SOURCE / "AbilitySystem" / "CataclysmReaperSoul.cpp").read_text(
        encoding="utf-8")
    assert 'ActOnEvent(FName(TEXT("soul_picked_up")));' in soul
    source = ASC.read_text(encoding="utf-8")
    assert ('UCataclysmAbilitySystemComponent::SoulPickedUpEvent = '
            'TEXT("soul_picked_up");') in source
    held = body_of(source,
                   "int32 UCataclysmAbilitySystemComponent::SoulStrikeStacksHeld")
    assert held
    assert "Action.Event == SoulPicked" in held
    assert "OwnStacksHeld(Action.StackKey)" in held


def test_the_execute_and_the_cull_share_one_execution():
    """C1, read from the C++: `KillIfLeftBelow` is called by both and holds
    the one `ReduceHealthDirectly`. THE CONTROL is the execute's trigger
    cooldown, which only the execute starts."""
    source = ASC.read_text(encoding="utf-8")
    kill = body_of(source,
                   "bool UCataclysmAbilitySystemComponent::KillIfLeftBelow(")
    execute = body_of(
        source, "bool UCataclysmAbilitySystemComponent::ExecuteIfLeftBelow(")
    cull = body_of(source,
                   "bool UCataclysmAbilitySystemComponent::CullIfLeftBelow(")
    assert kill and execute and cull
    assert "ReduceHealthDirectly(By, Struck, Health);" in kill
    assert "ReduceHealthDirectly" not in execute
    assert "ReduceHealthDirectly" not in cull
    assert "KillIfLeftBelow(By, Asked.BelowPercent)" in execute
    assert "KillIfLeftBelow(By, Asked.BelowPercent)" in cull
    assert "NoteExecuteMade" in execute
    assert "NoteExecuteMade" not in cull


def test_the_cull_is_acted_on_after_the_execute_in_the_damage_branch():
    """C1 and C4, read from the C++: one call each, the execute first."""
    source = VITAL.read_text(encoding="utf-8")
    execute = "Executable->ExecuteIfLeftBelow(AskedByThisHit);"
    cull = "Executable->CullIfLeftBelow(CullByThisHit);"
    assert source.count(execute) == 1
    assert source.count(cull) == 1
    assert source.index(execute) < source.index(cull)


def test_the_pawn_asks_the_cull_on_its_own_hits_and_spawns_at_its_kills():
    """C2 and C5, read from the C++: the cull is asked before `hit_dealt` is
    raised, refused for a minion's own blow; the spawn is made beside the
    death sentence's lift, heard for the kills that raise `kill`."""
    source = PAWN.read_text(encoding="utf-8")
    hit = body_of(source, "void ACataclysmPlayerCharacter::OnSomethingWasHit(")
    died = body_of(source, "void ACataclysmPlayerCharacter::OnSomethingDied(")
    ask = "Acting->AskToCullOnHit(Notice.Target);"
    raise_hit = 'Acting->ActOnEvent(FName(TEXT("hit_dealt"))'
    assert hit.count(ask) == 1
    assert hit.index(ask) < hit.index(raise_hit)
    assert "!Cast<ACataclysmMinion>(Notice.DealtBy)" in hit
    spawn = "UCataclysmSummonSkill::SpawnOneAtAKill(this, Notice.Location);"
    lift = "Acting->NoteKillUnderDeathSentence(Notice.Victim);"
    assert died.count(spawn) == 1
    assert died.index(lift) < died.index(spawn)


# ---- the shipped execute rows, counted (C1)

def test_the_shipped_table_holds_one_execute_row_and_it_is_as_ruled():
    with SHIPPED.open(encoding="utf-8", newline="") as handle:
        executes = [each for each in csv.DictReader(handle)
                    if each["Action"].strip() == "execute_target"]
    assert [(each["Name"], each["ActionEvent"], each["RequiredTags"],
             each["Condition"], float(each["ConditionValue"]),
             float(each["ValueLow"]), float(each["ValueHigh"]),
             float(each["TriggerCooldown"])) for each in executes] == [
        ("Positive_Strike_skills_have_a_10_20_chance_to_execute_e#1",
         "hit_dealt", "Type.Strike", "target_health_below", 15.0, 10.0, 20.0,
         0.0)]


# ---- the shapes the game would give nothing for, each refused once

@pytest.mark.parametrize("stat, change, said", [
    (CULL, {"ValueKind": "increased"}, "value kind 'increased'"),
    (CULL, {"RequiredTags": "Type.Strike"}, "under a required tag"),
    (CULL, {"Action": "execute_target"}, "with the action 'execute_target'"),
    (CULL, {"ValueLow": 0.0, "ValueHigh": 0.0}, "above 0 and at most 100"),
    (CULL, {"ValueLow": 101.0, "ValueHigh": 101.0}, "above 0 and at most 100"),
    (SPAWN, {"ValueKind": "more"}, "value kind 'more'"),
    (SPAWN, {"RequiredTags": "Type.Minion"}, "under a required tag"),
    (SPAWN, {"Action": "apply_status"}, "with the action 'apply_status'"),
    (SPAWN, {"ValueLow": 0.0, "ValueHigh": 0.0}, "states 1 and nothing else"),
    (SPAWN, {"ValueLow": 2.0, "ValueHigh": 2.0}, "states 1 and nothing else"),
])
def test_every_shape_the_game_would_give_nothing_for_is_refused(
        tmp_path, stat, change, said):
    """One change to the carried row at a time, each refused once and by its
    own sentence. The control is the first assertion."""
    ruled = carried(tmp_path, stat)
    assert refusals([ruled]) == []
    found = refusals([dict(ruled, **change)])
    assert len(found) == 1, found
    assert said in found[0]


def test_a_cull_of_another_figure_inside_the_bounds_is_accepted(tmp_path):
    """The bound is the check's, not the ruled 5: 100 is the last accepted."""
    ruled = carried(tmp_path, CULL)
    assert refusals([dict(ruled, ValueLow=100.0, ValueHigh=100.0)]) == []
    assert refusals([dict(ruled, ValueLow=2.5, ValueHigh=2.5)]) == []


def test_a_condition_or_a_scale_on_either_is_refused_elsewhere(tmp_path):
    """THE CONTROL is the ruled row, which the general checks do not refuse."""
    for stat in (CULL, SPAWN):
        ruled = carried(tmp_path, stat)
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [ruled]) == []
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects",
            [dict(ruled, Scale="own_stacks", ScaleStep=1.0)])
        assert gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [ruled]) == []
        assert gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [dict(ruled, Condition="health_below",
                                        ConditionValue=50.0)])


def test_a_row_on_another_stat_or_a_passive_node_is_not_this_checks_business(
        tmp_path):
    ruled = carried(tmp_path, SPAWN)
    assert refusals([dict(ruled, Stat="attack_damage", ValueKind="increased",
                          ValueLow=10.0, ValueHigh=10.0)]) == []
    assert refusals([dict(ruled, Node="Ritualist_made_up", ValueLow=0.0,
                          ValueHigh=0.0)]) == []
