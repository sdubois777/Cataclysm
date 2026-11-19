"""A change the player makes to the character's build writes the character record, and a load does not.

WHY THIS EXISTS. Issue #2763. The project owner spent passive points in a dungeon on 2026-11-19 and stopped Play In
Editor 43 seconds after entering it. Their character record, last written when they pressed Walk, held no passive node
and no attribute point: nothing wrote the record when a point was spent. `ECataclysmSaveTrigger::CharacterBuildChanged`
now does, raised by the player's acts.

WHAT IS CHECKED, by reading the C++ with comments removed, so a comment naming the trigger cannot pass for a call:

  1. each of the seven acts -- a passive point, a capstone option, the passive reset, attribute points spent and reset,
     a skill chosen and a choice forgotten -- raises the trigger, after the change it saves;
  2. the functions a load and a test use to put a build back raise nothing: `AssignSkill`, `ClearSkillChoice`,
     `RestorePassiveAllocation`, `RestoreSpentAttributePoints` and `SetPassiveAllocation`, and the save apply names
     neither the trigger nor the player's skill acts;
  3. the skill screen and `Cataclysm.AssignSkill` call the player's acts, `ChooseSkill` and `ForgetSkillChoice`, and
     not `AssignSkill` or `ClearSkillChoice`;
  4. the trigger is 15, writes the character record, and is neither synchronous nor exempt from the once-a-frame rule.

WHAT IS NOT. That the write reaches the slot and reads back: the `Cataclysm.BuildSave.` automation tests check that,
each with the run record written earlier in the same frame.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))

from test_the_play_loop_is_logged_at_each_step import function_body, read_code, squeeze  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
STATE = SOURCE / "Player" / "CataclysmPlayerState.cpp"
TRIGGERS_H = SOURCE / "Save" / "CataclysmSaveTriggers.h"
TRIGGERS_CPP = SOURCE / "Save" / "CataclysmSaveTriggers.cpp"
APPLY = SOURCE / "Save" / "CataclysmSaveApply.cpp"
WIDGET = SOURCE / "Interface" / "CataclysmSkillAssignmentWidget.cpp"
CONSOLE = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"

RAISE = "UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);"
TRIGGER = "CharacterBuildChanged"

#: Each act, and the change it saves, which must come before the write.
ACTS = [
    ("bool ACataclysmPlayerState::SpendPassivePoint(", "UCataclysmPassiveTree::Spend("),
    ("bool ACataclysmPlayerState::ChoosePassiveOption(", "PassiveAllocation = Attempt;"),
    ("void ACataclysmPlayerState::ResetPassivePoints()", "PassiveAllocation.Clear();"),
    ("bool ACataclysmPlayerState::SpendAttributePoints(", "SpentAttributePoints = Wanted;"),
    ("void ACataclysmPlayerState::ResetAttributePoints()", "SpentAttributePoints = FCataclysmAttributePoints();"),
    ("bool ACataclysmPlayerState::ChooseSkill(", "AssignSkill(Slot, SkillRow, OutReason)"),
    ("bool ACataclysmPlayerState::ForgetSkillChoice(", "ClearSkillChoice(Slot)"),
]

#: What a load or a test uses to put a build back, which must save nothing.
RESTORES = [
    "bool ACataclysmPlayerState::AssignSkill(",
    "bool ACataclysmPlayerState::ClearSkillChoice(",
    "void ACataclysmPlayerState::RestorePassiveAllocation(",
    "void ACataclysmPlayerState::RestoreSpentAttributePoints(",
    "void ACataclysmPlayerState::SetPassiveAllocation(",
]


@pytest.mark.parametrize("signature, change", ACTS, ids=[a[0].split("::")[1].split("(")[0] for a in ACTS])
def test_each_act_writes_the_character_record_after_the_change(signature: str, change: str) -> None:
    body = function_body(read_code(STATE), signature)
    raise_ = squeeze(RAISE)
    assert raise_ in body, (
        f"{signature} no longer raises CharacterBuildChanged. Expected, whitespace aside:\n  {raise_}\nWithout it the "
        "change waits for the next item, death or trip to reach the disk, and stopping play first loses it (#2763).")
    assert change in body, f"{signature} no longer does {change!r}, the change its write saves"
    assert body.index(change) < body.index(raise_), (
        f"{signature} raises the write before {change!r}, so the record written holds the build before the change.")


@pytest.mark.parametrize("signature", RESTORES, ids=[s.split("::")[1].split("(")[0] for s in RESTORES])
def test_putting_a_build_back_saves_nothing(signature: str) -> None:
    body = function_body(read_code(STATE), signature)
    assert TRIGGER not in body and "NoteTriggerIn" not in body, (
        f"{signature} raises a save. A load puts the saved build back through it, and writing back what was just read "
        "is a write for nothing; the player's acts raise CharacterBuildChanged instead.")


def test_the_save_apply_uses_the_load_functions_and_raises_nothing() -> None:
    code = read_code(APPLY)
    assert "->AssignSkill(" in code, (
        "CataclysmSaveApply.cpp no longer puts the saved skill choices back through AssignSkill; if it moved, this "
        "check and the one on AssignSkill above need to follow it.")
    for name in (TRIGGER, "->ChooseSkill(", "->ForgetSkillChoice("):
        assert name not in code, f"CataclysmSaveApply.cpp names {name}, so a load would write the record it just read."


@pytest.mark.parametrize("signature, act, not_this", [
    ("bool UCataclysmSkillAssignmentWidget::PickSkill(", "State->ChooseSkill(", "AssignSkill("),
    ("bool UCataclysmSkillAssignmentWidget::ClearSelectedSlot()", "State->ForgetSkillChoice(", "ClearSkillChoice("),
], ids=["pick", "clear"])
def test_the_skill_screen_calls_the_players_acts(signature: str, act: str, not_this: str) -> None:
    body = function_body(read_code(WIDGET), signature)
    assert act in body, f"{signature} no longer calls {act}, the act that saves the choice"
    assert not_this not in body, f"{signature} calls {not_this}, which saves nothing"


def test_the_assign_skill_command_calls_the_players_acts() -> None:
    code = read_code(CONSOLE)
    start = code.find('TEXT("Cataclysm.AssignSkill")')
    assert start != -1, "Cataclysm.AssignSkill is no longer registered in CataclysmPlayerCharacter.cpp"
    end = code.find("FAutoConsoleCommandWithWorldArgsAndOutputDevice", start)
    command = squeeze(code[start:end if end != -1 else len(code)])
    for act in ("State->ChooseSkill(", "State->ForgetSkillChoice("):
        assert act in command, f"Cataclysm.AssignSkill no longer calls {act}, the act that saves"
    for load in ("State->AssignSkill(", "State->ClearSkillChoice("):
        assert load not in command, f"Cataclysm.AssignSkill calls {load}, which saves nothing"


def test_the_trigger_is_fifteen_and_writes_the_character_record_asynchronously() -> None:
    header = squeeze(read_code(TRIGGERS_H))
    assert re.search(r"\bCharacterBuildChanged = 15,", header), "ECataclysmSaveTrigger::CharacterBuildChanged is not 15"
    triggers = read_code(TRIGGERS_CPP)
    character = function_body(triggers, "bool UCataclysmSaveTriggers::WritesTheCharacterRecord(")
    assert "case ECataclysmSaveTrigger::CharacterBuildChanged:" in character, (
        "CharacterBuildChanged does not write the character record, which is the record that holds the build")
    for function in ("bool UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds(",
                     "bool UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame("):
        assert TRIGGER not in function_body(triggers, function), (
            f"{function.split('::')[1].split('(')[0]} names CharacterBuildChanged. It is an ordinary asynchronous write, "
            "and the run record holds none of the build, so it obeys the once-a-frame rule (ruled for #2763).")
    every = function_body(triggers, "TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every()")
    assert "ECataclysmSaveTrigger::CharacterBuildChanged" in every, (
        "CharacterBuildChanged is not in UCataclysmSaveTriggers::Every(), so the trigger tests walk past it")
