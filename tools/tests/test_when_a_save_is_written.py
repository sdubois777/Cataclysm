"""A death is the one save written before the frame ends, and it is written first.

WHY THIS EXISTS. Issue #750. The project owner set the rule on 2026-08-20: the
game saves itself, often, so that a Hardcore character cannot leave a losing boss
fight by closing the game. `docs/Save_System_Design.md` section 6 names one part
of it that cannot be relaxed:

    for a Hardcore character, death is the event this whole feature exists to
    make stick, so it is written in the same frame the health reaches zero,
    through the synchronous write, before the death is otherwise processed.
    Everything else may be written asynchronously.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmSaveWriterTests.cpp`
never run on a pull request. Reading the source as text does.

WHAT BREAKING IT COSTS, and this is the whole point of guarding it: **an
asynchronous death write still writes.** It finishes a moment later, on a
background thread, and nothing looks wrong. That moment is exactly the window a
player closing the game would use, which is the escape the feature was built to
close. There is no crash, no log line, and no way to see it except by playing
Hardcore and getting away with it.

AND WHERE THE WRITE SITS IN `HandleDeath` MATTERS AS MUCH AS HOW IT IS WRITTEN.
The gather skips a character it can see is dead, so a write placed after the
character is marked dead records a floor with nobody standing on it -- and
putting that back would restore the fight without the death.

WHAT IS DELIBERATELY NOT CHECKED. Which triggers exist, and what each one writes.
Those are covered by `Cataclysm.SaveTriggers.*`, which can walk the enum. This
file checks the two things that can be read off the text and that fail silently.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
TRIGGERS_CPP = SOURCE / "Save" / "CataclysmSaveTriggers.cpp"
TRIGGERS_H = SOURCE / "Save" / "CataclysmSaveTriggers.h"
WRITER_CPP = SOURCE / "Save" / "CataclysmSaveWriter.cpp"
GAME_MODE_CPP = SOURCE / "Player" / "CataclysmGameMode.cpp"
PLAYER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
SAVE_DESIGN = REPO_ROOT / "docs" / "Save_System_Design.md"

#: `static constexpr float SecondsBetweenClockWrites = 15.0f;`
CONSTANT = re.compile(
    r"static\s+constexpr\s+float\s+(?P<name>\w+)\s*=\s*(?P<value>[-\d.]+)f?\s*;")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. When a save "
            f"is written is issue #750; if a file was renamed, rename it here too.")
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The code with its comments removed.

    NEEDED FOR EVERY "MUST NOT CONTAIN" CHECK IN THIS PROJECT, because every rule
    here is written down beside the code in a comment naming the very thing being
    searched for. Every one of the searches below would match a comment
    explaining the rule. This project has recorded that trap five times.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def function_body(text: str, signature: str) -> str:
    """One function's code, with its comments stripped out."""
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"
    return without_comments(text[start:end])


def test_the_design_still_asks_for_a_synchronous_death_write() -> None:
    """The rule this file enforces is read from the design, not restated here."""
    design = read(SAVE_DESIGN)

    assert "Death is written first, and synchronously" in design, (
        "docs/Save_System_Design.md section 6 no longer heads a section 'Death is "
        "written first, and synchronously'. If the design changed, this file is "
        "enforcing a rule that no longer exists and should be deleted rather than "
        "made to pass.")

    assert "Everything else may be written asynchronously" in design, (
        "the design no longer says that everything except death may be written "
        "asynchronously, which is the other half of the rule below.")


def test_only_the_character_dying_is_written_before_the_frame_ends() -> None:
    """One trigger is synchronous, and the source says which."""
    body = function_body(read(TRIGGERS_CPP),
                         "bool UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds(")

    assert "CharacterDied" in body, (
        "UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds no longer mentions "
        "CharacterDied. Section 6 names the character dying as the one write that "
        "cannot be relaxed, and an asynchronous death write hands the player back "
        "the escape this feature closes.")

    # NO OTHER TRIGGER MAY APPEAR IN IT. A second synchronous write would stall
    # the frame for nothing, and the one most likely to be added by mistake is a
    # creature dying, which reads most like death.
    others = ("Clock", "FightStarted", "CreatureDied", "HealthCrossedThreshold",
              "ChangedFloor", "InventoryChanged", "AccountChanged")
    for other in others:
        assert other not in body, (
            f"MustBeWrittenBeforeTheFrameEnds mentions {other}. Section 6: "
            f"'Everything else may be written asynchronously.' Only the character "
            f"dying is written before the frame ends.")


def test_a_death_is_written_before_the_character_is_marked_dead() -> None:
    """Where the write sits in HandleDeath decides what the record holds.

    THE GATHER SKIPS A CHARACTER IT CAN SEE IS DEAD. A write one line lower would
    record a floor with nobody standing on it, and restoring that would give back
    the fight without the death.
    """
    body = function_body(read(PLAYER_CPP), "void ACataclysmPlayerCharacter::HandleDeath()")

    write = body.find("ECataclysmSaveTrigger::CharacterDied")
    mark = body.find("MarkDead")

    assert write != -1, (
        "ACataclysmPlayerCharacter::HandleDeath does not write a save at all. "
        "Section 6 makes the character dying the one event that must be written "
        "in the same frame health reaches zero.")
    assert mark != -1, (
        "ACataclysmPlayerCharacter::HandleDeath no longer marks the character "
        "dead, so this check cannot tell where the write sits relative to it.")

    assert write < mark, (
        "ACataclysmPlayerCharacter::HandleDeath writes the save AFTER marking the "
        "character dead. FCataclysmSaveGather::FloorFrom skips a character that is "
        "already marked, so the record would hold a floor with nobody on it -- and "
        "restoring that would put the fight back without the death.")


def test_the_writer_uses_the_synchronous_call_for_a_death() -> None:
    """The writer branches on the rule rather than always doing one thing."""
    body = function_body(read(WRITER_CPP), "bool UCataclysmSaveWriter::Write(")

    assert "MustBeWrittenBeforeTheFrameEnds" in body, (
        "UCataclysmSaveWriter::Write no longer asks whether the trigger has to be "
        "written before the frame ends, so every write takes the same route.")
    assert "WriteToSlotAsync" in body, (
        "UCataclysmSaveWriter::Write never calls WriteToSlotAsync, so every write "
        "stalls the frame. Section 6: writing must not stall the frame.")
    assert "FCataclysmSaveStorage::WriteToSlot(" in body, (
        "UCataclysmSaveWriter::Write never calls the synchronous WriteToSlot, so "
        "nothing is written before the frame ends -- including a death.")


def test_the_interval_and_the_threshold_are_real_numbers() -> None:
    """A constant left at zero switches its rule off and nothing says so.

    AN INTERVAL OF ZERO IS REFUSED BY `ClockHasElapsed`, so the clock would never
    write and every automation test of it would still pass. A threshold of zero
    is refused by `HealthCrossedTheThreshold` for the same reason.
    """
    header = read(TRIGGERS_H)
    found = {match["name"]: float(match["value"]) for match in CONSTANT.finditer(header)}

    interval = found.get("SecondsBetweenClockWrites")
    assert interval is not None, (
        "UCataclysmSaveTriggers no longer declares SecondsBetweenClockWrites. "
        "Section 6 requires a clock, and section 7 says the number is a tuning "
        "constant rather than a derived one.")
    assert interval > 0.0, (
        f"the clock's interval is {interval}, and ClockHasElapsed refuses anything "
        f"at or below zero, so the clock would never write at all.")

    threshold = found.get("HealthFractionThatForcesAWrite")
    assert threshold is not None, (
        "UCataclysmSaveTriggers no longer declares HealthFractionThatForcesAWrite.")
    assert 0.0 < threshold < 1.0, (
        f"the health threshold is {threshold}, and it has to be a share of maximum "
        f"health between none and all of it. Zero switches the check off and one "
        f"fires on the first scratch.")


def test_starting_play_turns_the_save_writer_on() -> None:
    """The one line no automation test can reach.

    `ACataclysmGameMode::StartPlay` wants a player controller and a pawn, so it
    cannot be called from an automation test -- which is why
    `CataclysmSandboxTests.cpp` calls the three spawners directly rather than
    through it. `BeginSavingThisRun` exists as its own method for the same
    reason and IS covered, by
    `Cataclysm.SaveWriter.TheGameModeIsWhatTurnsTheWriterOn`.

    WHAT IS LEFT UNCOVERED IS THE CALL ITSELF, and this is what covers it.
    Without the call the save system is built, tested, and switched off in the
    running game, and every other test in the project goes on passing.
    """
    body = function_body(read(GAME_MODE_CPP), "void ACataclysmGameMode::StartPlay()")

    assert "BeginSavingThisRun" in body, (
        "ACataclysmGameMode::StartPlay no longer calls BeginSavingThisRun, so the "
        "game never tells the save writer which run it is playing and nothing is "
        "ever written. docs/Save_System_Design.md section 6: the game saves "
        "itself, often, and there is no manual save.")
