"""A death inside a dungeon of the empire ends the walk of it. Issue #41.

WHAT THE UNREAL TESTS CANNOT HOLD, AND THIS DOES. The automation tests in
`CataclysmDungeonCostsDaysTests.cpp` and `CataclysmEmpireRunTests.cpp` measure
what a death costs: the days, the resolve, the timer. They need the editor. The
four facts below are about where a line of code sits, which no measurement in a
test world separates, and they are read from the source here in a second:

1. THE ORDER: resolve, leave, charge. Ruled on 2026-10-09 by the coordinating
   session under the project owner's delegation. Charging before leaving would
   run the days with the dungeon's own timer stopped.
2. THE PLAYTEST SWITCH defaults to the designed behaviour and is read before
   anything else is decided.
3. THE DUNGEON IS ENDED WHEN THE CHARACTER STANDS BACK UP, and while it is still
   marked dead. Not at the killing blow, which runs inside whatever dealt it.
4. THE EMPIRE ASKS THE KIND BEFORE IT RESOLVES, so a death never reaches the
   function that relocates a Quest dungeon, and it refills the timer afterwards.

A LINE THAT IS A COMMENT DOES NOT COUNT. Each function is read with its comment
lines left out, because the comments in these functions name every call the
checks look for.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
PLAYER = SOURCE / "Cataclysm" / "Character" / "CataclysmPlayerCharacter.cpp"
EMPIRE_RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

SWITCH = "Cataclysm.DeathKeepsThePlayerInTheDungeon"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def code_of(text: str, signature: str) -> str:
    """One function's lines of code, with every line that is a comment left out."""
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"
    return "\n".join(
        line for line in text[start:end].splitlines()
        if not line.strip().startswith("//")
    )


def position_of(code: str, call: str, where: str) -> int:
    """Where one call sits in a function's code. It must be there exactly once."""
    count = code.count(call)
    assert count == 1, f"{where} holds {call!r} {count} times; this check expects one"
    return code.index(call)


def test_a_death_resolves_then_leaves_then_charges() -> None:
    """The ruled order, in the one function that does all three."""
    code = code_of(read(GAME_MODE),
                   "bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()")
    where = "ACataclysmDungeonGameMode::EndTheDungeonForADeath"

    resolve = position_of(code, "->ResolveDungeonOnDeath(", where)
    leave = position_of(code, "LeaveEmpireDungeon();", where)
    charge = position_of(code, "->AdvanceDays(", where)

    assert resolve < leave, (
        "A death leaves the dungeon before it resolves it. The ruling is resolve, "
        "leave, charge: the dungeon resolves while the player is still inside.")
    assert leave < charge, (
        "A death charges its days before the dungeon is left. The dungeon being "
        "stood in is the one timer that does not count down, so the days would "
        "pass with that timer stopped.")
    assert "->DeathDayCost()" in code, (
        "The days a death costs are no longer read from the run's lethality mode.")


def test_the_playtest_switch_defaults_to_the_designed_behaviour() -> None:
    """0 is the design, and the switch is read before anything is decided."""
    text = read(GAME_MODE)

    declared = re.search(
        r"TAutoConsoleVariable<int32>\s+CVarDeathKeepsThePlayerInTheDungeon\(\s*"
        r'TEXT\("' + re.escape(SWITCH) + r'"\),\s*(-?\d+),', text)
    assert declared is not None, (
        f"The dungeon game mode no longer declares the console variable {SWITCH}.")
    assert declared.group(1) == "0", (
        f"{SWITCH} defaults to {declared.group(1)}. The default must be 0, the "
        "designed behaviour: a death ends the walk, resolves an ordinary dungeon "
        "and costs the days.")

    code = code_of(text, "bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()")
    where = "ACataclysmDungeonGameMode::EndTheDungeonForADeath"
    switch = position_of(code, "CVarDeathKeepsThePlayerInTheDungeon", where)
    assert switch < position_of(code, "->ResolveDungeonOnDeath(", where), (
        "The playtest switch is read after the dungeon has already resolved.")


def test_the_dungeon_is_ended_when_the_character_stands_back_up() -> None:
    """In `Revive`, while the character is still marked dead, and not in `HandleDeath`."""
    text = read(PLAYER)

    reviving = code_of(text, "void ACataclysmPlayerCharacter::Revive()")
    where = "ACataclysmPlayerCharacter::Revive"
    ending = position_of(reviving, "->EndTheDungeonForADeath();", where)
    clearing = position_of(reviving, "UCataclysmSkillEffects::ClearDead(this)", where)
    asking = position_of(reviving, "if (IsAwaitingRespawn())", where)

    assert asking < ending, (
        "Revive ends the dungeon without first asking whether the character is "
        "dead, so reviving the living would end a dungeon nobody died in.")
    assert ending < clearing, (
        "Revive ends the dungeon after clearing the dead mark. The character's "
        "health is nought until the refill, and a write to health at nought kills "
        "a character that is not marked dead.")

    dying = code_of(text, "void ACataclysmPlayerCharacter::HandleDeath()")
    assert "EndTheDungeonForADeath" not in dying, (
        "HandleDeath ends the dungeon. It runs inside the blow that killed, which "
        "may be a step of one of the floor rules that leaving a dungeon empties.")


def test_the_empire_asks_the_kind_before_it_resolves_and_refills_the_timer_after() -> None:
    """A death never reaches the relocation, and the old days cannot resolve it twice."""
    code = code_of(read(EMPIRE_RUN),
                   "FCataclysmDayReport UCataclysmEmpireRun::ResolveDungeonOnDeath(")
    where = "UCataclysmEmpireRun::ResolveDungeonOnDeath"

    kind = position_of(code, "!Dungeon->Resolves()", where)
    resolve = position_of(code, "ResolveDungeon(DungeonId, Report);", where)
    refill = position_of(code, "Timer->DaysUntilResolve = Timer->ResolveDays;", where)

    assert kind < resolve, (
        "ResolveDungeonOnDeath calls ResolveDungeon before asking whether the "
        "dungeon is a kind that resolves. For a Quest dungeon that call runs the "
        "relocation, which belongs to its timer running out and not to a death.")
    assert resolve < refill, (
        "ResolveDungeonOnDeath refills the timer before the resolve. A resolve "
        "that fells the city removes the dungeon, and its timer must be looked "
        "for afterwards.")
    assert "RelocateQuestDungeon" not in code, (
        "ResolveDungeonOnDeath relocates a Quest dungeon. A death does not touch "
        "its relocation clock.")


def test_the_design_document_says_what_dying_in_a_quest_dungeon_does() -> None:
    """The sentence that said it was undecided is gone, and the rule is there."""
    design = read(DESIGN)

    assert "what dying in one does is undecided" not in design, (
        "docs/Cataclysm_GDD_v2.md still says what dying in a Quest dungeon or a "
        "Dungeon City does is undecided. It was decided on 2026-10-09 and built.")
    assert "dying in one costs the same days" in design, (
        "docs/Cataclysm_GDD_v2.md no longer says dying in a Quest dungeon or a "
        "Dungeon City costs the same days. That is what the game mode does.")
