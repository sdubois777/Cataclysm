"""A walk ends by clearing its last floor or by dying in it, and every other way out is refused.

WHY THIS EXISTS. Issue #2733 slice 3 made the main loop travel: the capital's
Walk takes the character to `L_Dungeon`, and the last floor's stairs or a death
take it back to `L_Capital`. The design, `docs/Cataclysm_GDD_v2.md`, says of a
dungeon: "There is no town portal and no way out except clearing the dungeon or
dying." The coordinating session ruled on 2026-11-17 (Q4) that the two console
commands that left a walk refuse mid-walk, with the sentence a Walk press
mid-walk shows, and no override.

WHY IT IS CHECKED FROM PYTHON. The console commands find the dungeon through
the world's authority game mode, which no automation test world can be given
(`UWorld::AuthorityGameMode` is private), so no Unreal test reaches their
mid-walk branch. And a new way out -- an `OpenLevel` somewhere else -- would
pass every Unreal test, because no test opens a level.

WHAT IS CHECKED, by reading the C++ with comments removed:

  1. `Cataclysm.LeaveDungeon` leaves nothing: it no longer calls
     `LeaveEmpireDungeon` and answers with `NoLeavingEarlyText`;
  2. `Cataclysm.EnterDungeon` refuses with `NoLeavingEarlyText` while a walk is
     under way, before it would enter anything;
  3. `FCataclysmWalkFromTheScreen::RefusalFor` refuses every press mid-walk,
     not only the dungeon being walked;
  4. every `OpenLevel` in the game's source is one of the four known ones: the
     select screen's Continue, the new-character screen's Start, a refused
     load's way back to the front end, and `ACataclysmGameMode::TravelTo`;
  5. the two ways a walk ends travel back to the capital: the last floor's
     stairs after the clear, and `Revive` after a death that ended a walk, last.

WHAT IS NOT. That a press reaches the screen, or that a level opens; the
`Cataclysm.Travel.` automation tests read what `TravelTo` asked for instead.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
CHARACTER_CPP = SOURCE / "Cataclysm" / "Character" / "CataclysmPlayerCharacter.cpp"
WALK_CPP = SOURCE / "Cataclysm" / "Interface" / "CataclysmWalkFromTheScreen.cpp"
DUNGEON_CPP = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"

#: Every `OpenLevel` call the game makes, by file, and why it is not a way out of a walk.
KNOWN_LEVEL_CHANGES = {
    # Continue on the select screen, which runs on the front end, where no walk is.
    "Cataclysm/Interface/CataclysmCharacterSelectWidget.cpp": 1,
    # Start on the new-character screen, also on the front end.
    "Cataclysm/Interface/CataclysmNewCharacterWidget.cpp": 1,
    # A refused load's way back to the front end (`LoadThePendingCharacter`), which
    # runs as a level begins, before any walk; and `TravelTo`, the capital's Walk
    # and a walk's end.
    "Cataclysm/Player/CataclysmGameMode.cpp": 2,
}


def strip_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, string literals kept."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j == -1 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def braced_from(text: str, start: int, what: str) -> str:
    open_at = text.find("{", start)
    assert open_at != -1, f"{what} has no opening brace"
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    raise AssertionError(f"{what} is never closed")


def read_code(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. If it moved, move it here too.")
    return strip_comments(path.read_text(encoding="utf-8"))


def console_command_body(code: str, command: str) -> str:
    literal = f'TEXT("{command}")'
    assert code.count(literal) == 1, f"{command} should be registered once; {literal} appears {code.count(literal)} times"
    at = code.find(literal)
    lambda_at = code.find("[](", at)
    assert lambda_at != -1, f"{command} registers no lambda after its name"
    return squeeze(braced_from(code, lambda_at, command))


def function_body(code: str, signature: str) -> str:
    assert code.count(signature) == 1, f"{signature} should be defined once; it appears {code.count(signature)} times"
    return squeeze(braced_from(code, code.find(signature), signature))


def test_leave_dungeon_leaves_nothing_and_says_why() -> None:
    body = console_command_body(read_code(CHARACTER_CPP), "Cataclysm.LeaveDungeon")
    assert "LeaveEmpireDungeon(" not in body, (
        "Cataclysm.LeaveDungeon calls LeaveEmpireDungeon again. There is no leaving a dungeon early (#2733 slice 3, "
        "ruled 2026-11-17, Q4): the command refuses mid-walk.")
    assert "FCataclysmWalkFromTheScreen::NoLeavingEarlyText(Mode->EmpireDungeonId)" in body, (
        "Cataclysm.LeaveDungeon no longer answers with NoLeavingEarlyText, the sentence every way out says.")


def test_enter_dungeon_refuses_mid_walk_before_entering_anything() -> None:
    body = console_command_body(read_code(CHARACTER_CPP), "Cataclysm.EnterDungeon")
    refusal = "if (Mode->EmpireDungeonId != INDEX_NONE) { Ar.Log(FCataclysmWalkFromTheScreen::NoLeavingEarlyText(Mode->EmpireDungeonId)); return; }"
    assert refusal in body, (
        "Cataclysm.EnterDungeon no longer refuses while a walk is under way. Entering another dungeon would leave "
        f"this one unfinished. Expected, whitespace aside:\n  {refusal}")
    assert body.index(refusal) < body.index("EnterEmpireDungeon("), (
        "Cataclysm.EnterDungeon's mid-walk refusal comes after it enters a dungeon, so it refuses nothing.")


def test_every_walk_press_mid_walk_is_refused() -> None:
    body = function_body(read_code(WALK_CPP), "FString FCataclysmWalkFromTheScreen::RefusalFor(")
    assert "if (Mode != nullptr && Mode->EmpireDungeonId != INDEX_NONE) { return NoLeavingEarlyText(Mode->EmpireDungeonId); }" in body, (
        "RefusalFor no longer refuses every press while a walk is under way.")
    assert "Mode->EmpireDungeonId == DungeonId" not in body, (
        "RefusalFor refuses only the dungeon being walked again, which until #2733 slice 3 let a press under another "
        "dungeon leave the walk.")


def test_every_level_change_is_a_known_one() -> None:
    found: dict[str, int] = {}
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.parts:
            continue
        count = read_code(path).count("UGameplayStatics::OpenLevel(")
        if count:
            found[path.relative_to(SOURCE).as_posix()] = count
    assert found == KNOWN_LEVEL_CHANGES, (
        "The game's OpenLevel calls are not the four known ones. A new one may be a way out of a walk, which the "
        f"design forbids. Found {found}; known {KNOWN_LEVEL_CHANGES}. If the new one is not a way out, add it to "
        "KNOWN_LEVEL_CHANGES with the reason.")


def test_the_last_floors_stairs_clear_and_then_go_back_to_the_capital() -> None:
    body = function_body(read_code(DUNGEON_CPP), "bool ACataclysmDungeonGameMode::GoDownOneFloor(")
    branch = "if (IsOnTheLastFloor()) { if (ClearEmpireDungeon()) { ReturnToTheCapital(); } return false; }"
    assert branch in body, (
        "GoDownOneFloor's last floor no longer clears the dungeon and then returns to the capital. Expected, "
        f"whitespace aside:\n  {branch}")


def test_a_death_that_ended_a_walk_goes_back_to_the_capital_last() -> None:
    body = function_body(read_code(CHARACTER_CPP), "void ACataclysmPlayerCharacter::Revive(")
    paid = "UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::DeathProcessed);"
    back = "if (WalkEndedIn != nullptr) { WalkEndedIn->ReturnToTheCapital(); }"
    assert paid in body and back in body, "Revive no longer writes the paid death and then returns to the capital."
    assert body.index(paid) < body.index(back), (
        "Revive returns to the capital before the paid death is written, so the hand-over would hold an unpaid death.")
    assert body.rstrip().endswith(back), (
        "Revive does something after returning to the capital; the trip is its last step, so the hand-over holds "
        "everything above it.")
