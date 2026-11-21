"""A wound-up enemy ability lands at its own length, checked every frame.

WHY THIS EXISTS. Issue #2808, the owner's choice of 2026-11-21: "each one lands
exactly on time instead of waiting up to a quarter second extra". Until then an
ability landed on a quarter-second thinking pass, counted and rounded up, so the
Brute's 1.4 second stomp landed at 1.5 and the Succubus's 1.3 second bolt at 1.5.

WHAT IT REVERSED, AND WHY THAT IS SAFE. Issue #413 counted passes because
comparing the clock on a pass turned a frame of timer overshoot into a whole
quarter second for a telegraph sitting exactly on a pass boundary: the rock
throw's 1.0 second landed at 1.00 or 1.25 by a coin toss. The coin toss came
from checking four times a second. `ACataclysmEnemyCharacter::Tick` now asks
`ACataclysmEnemyController::LandWindUpIfDue` every frame, so the lateness is at
most one frame at any length and nothing rounds. In a test the landing is a
comparison of two numbers the test set, so it is as deterministic as the count.

WHY IT IS A PYTHON TEST. Continuous integration never builds the C++. The
automation tests in `game/Source/Cataclysm/Tests/CataclysmWindUpTimingTests.cpp`
check the landing against a live controller and only run on a developer's
machine. These read the source, so the mechanism cannot quietly go back.
`tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`
pins the frame's call.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CHARACTER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
CONTROLLER_H = CHARACTER / "CataclysmEnemyController.h"
CONTROLLER_CPP = CHARACTER / "CataclysmEnemyController.cpp"
BRUTE_CPP = CHARACTER / "CataclysmBruteCharacter.cpp"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def function_body(path: pathlib.Path, signature: str) -> str:
    """The text of one function, from its signature to its closing brace."""
    match = re.search(re.escape(signature) + r".*?\n\}", source(path), re.S)
    if match is None:
        pytest.fail(f"{path.relative_to(REPO_ROOT)} has no {signature!r}.")
    return match.group(0)


def code_only(text: str) -> str:
    """The text with // comments removed, so a name in a comment is not code."""
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def test_the_landing_is_due_at_its_own_length_and_nothing_rounds_it() -> None:
    """The comparison itself: the clock against the deadline, with no pass in it.

    Rounding to a pass is what issue #2808 removed. If it came back the attack
    would still land, a quarter second late at worst, and nothing would error.
    """
    body = code_only(function_body(
        CONTROLLER_CPP, "bool ACataclysmEnemyController::IsWindUpDue("))
    assert re.search(r"Now\s*\+\s*WindUpLandingToleranceSeconds\s*>=\s*LandsAt",
                     body), (
        "ACataclysmEnemyController::IsWindUpDue no longer compares the clock "
        "with the deadline as Now + WindUpLandingToleranceSeconds >= LandsAt.")
    for rounding in ("ThinkIntervalSeconds", "CeilTo", "RoundTo", "FloorTo"):
        assert rounding not in body, (
            f"ACataclysmEnemyController::IsWindUpDue uses {rounding}, so a "
            "wind-up is rounded to a thinking pass again and lands late.")


def test_a_thinking_pass_asks_the_same_question_and_counts_nothing() -> None:
    """`ContinueWindUp` lands by `IsWindUpDue` too, and no pass count is left."""
    body = code_only(function_body(
        CONTROLLER_CPP, "bool ACataclysmEnemyController::ContinueWindUp("))
    assert "IsWindUpDue(Now, WindUpLandsAt)" in body, (
        "ContinueWindUp no longer lands a wind-up by IsWindUpDue, so a thinking "
        "pass and the frame could disagree about when one is due.")
    for text in (source(CONTROLLER_CPP), source(CONTROLLER_H)):
        assert "WindUpPassesLeft =" not in code_only(text), (
            "A count of thinking passes is set again. Issue #2808 removed it "
            "with the rounding it carried.")


def test_the_frame_lands_one_only_for_a_creature_that_can_still_make_it() -> None:
    """`LandWindUpIfDue` leaves a dead, stopped or fleeing creature to `Think`.

    The frame comes before the next thinking pass, so without this a stun that
    began half way through a stomp would have the stomp land anyway.
    `Cataclysm.AI.TheFrameDoesNotLandAWindUpForACreatureThatCannotAct` is the
    live check; this one runs in continuous integration.
    """
    body = code_only(function_body(
        CONTROLLER_CPP, "bool ACataclysmEnemyController::LandWindUpIfDue("))
    for asked in ("UCataclysmSkillEffects::IsDead(Driven)",
                  "UCataclysmSkillEffects::CannotAct(Driven)",
                  "UCataclysmFear::FleeSourceOf(Driven"):
        assert asked in body, (
            f"LandWindUpIfDue no longer asks {asked}, so the frame can land an "
            "attack the creature can no longer make.")
    assert "ContinueWindUp(Driven)" in body, (
        "LandWindUpIfDue no longer lands through ContinueWindUp, so there are "
        "two ways to land a wind-up that can drift apart.")


def test_the_tolerance_is_under_a_frame() -> None:
    match = re.search(
        r"static\s+constexpr\s+float\s+WindUpLandingToleranceSeconds\s*=\s*"
        r"(\d+(?:\.\d+)?)f\s*;", source(CONTROLLER_H))
    assert match is not None, (
        "CataclysmEnemyController.h has no WindUpLandingToleranceSeconds.")
    tolerance = float(match.group(1))
    assert 0.0 < tolerance <= 0.001, (
        f"WindUpLandingToleranceSeconds is {tolerance}. It is how early a "
        "landing may be, so it must stay far under a frame (16 ms at 60 frames "
        "a second); a millisecond is the figure.")


def test_the_brute_times_its_montage_to_the_telegraph_itself() -> None:
    """The Brute's montage aims its strike at `LandsAtSecondsFor`. That must be
    the telegraph now, or the fists would land a tenth of a second after the
    stomp's damage."""
    body = code_only(function_body(
        BRUTE_CPP, "float ACataclysmBruteCharacter::LandsAtSecondsFor("))
    for rounding in ("ThinkIntervalSeconds", "CeilTo"):
        assert rounding not in body, (
            f"ACataclysmBruteCharacter::LandsAtSecondsFor uses {rounding}, so "
            "the montage is timed to a thinking pass the attack no longer "
            "lands on.")


def test_the_marker_outlasts_the_landing() -> None:
    """The marker's own lifespan is a backstop and must not end before the
    landing, which may come up to a frame after the wind-up's length."""
    body = code_only(function_body(
        CONTROLLER_CPP, "void ACataclysmEnemyController::ShowWindUpMarker("))
    assert re.search(r"ShownForSeconds\s*=\s*Ability\.WindUpSeconds\s*\+\s*"
                     r"ThinkIntervalSeconds", body), (
        "ShowWindUpMarker no longer gives the marker the wind-up plus a "
        "thinking interval, so it may leave the floor before the attack lands.")


def test_a_quick_cast_draws_no_marker() -> None:
    """The flag is read before any marker is drawn. Issue #2808."""
    body = code_only(function_body(
        CONTROLLER_CPP, "void ACataclysmEnemyController::ShowWindUpMarker("))
    flag = body.find("Ability.bDrawsNoMarker")
    first_marker = body.find("ACataclysmTelegraphMarker::Show")
    assert flag != -1, "ShowWindUpMarker no longer reads bDrawsNoMarker."
    assert flag < first_marker, (
        "ShowWindUpMarker draws a marker before it reads bDrawsNoMarker.")
