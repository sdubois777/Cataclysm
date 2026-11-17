"""The game sets one input mode, in one place, and every screen restores it.

WHY THIS EXISTS. Issue #1015. The project owner reported in a play test on
2026-08-26 that opening the passive tree, spending a point and closing it left
them unable to move; reopening the tree and clicking outside a node let them move
again, and closing it stopped them again.

That report is decisive on its own, because the three states differ only in the
input mode. The game booted into whatever the engine defaults to -- **nothing in
the project set one** -- opening a screen set `FInputModeGameAndUI`, and closing
one set `FInputModeGameOnly`, a mode the game had never otherwise run in.

WHY GameOnly BREAKS MOVEMENT HERE SPECIFICALLY. Every order a player gives is
read off the cursor: `UpdateCachedDestination` traces under it,
`CursorIsOverInterface` and `InventoryPressTarget` read its position.
`FInputModeGameOnly` captures the mouse permanently and has no option to say "do
not hide the cursor during capture" -- which is exactly what the two GameAndUI
call sites already passed by hand, and why they worked.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so nothing
in `game/Source/Cataclysm/Tests/` runs on a pull request. Reading the source as
text does. That is the same arrangement
`tools/tests/test_a_dying_enemy_plays_its_death_animation.py` uses and its header
says why.

WHAT THIS CANNOT CHECK, and it is worth being plain about: **whether the mode is
the right one.** An input mode is a property of the Slate viewport, the automation
tests run with `-nullrhi` and no real viewport, and nothing in the harness can see
a cursor. Confirming the mode is right takes a person playing the game. What this
guards is that there is exactly ONE mode and one place that sets it, so the next
screen cannot reintroduce a second one the way the last three did.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
CONTROLLER_CPP = SOURCE / "Player" / "CataclysmPlayerController.cpp"

#: The function every screen and the startup path must call.
THE_ONE_FUNCTION = "ApplyPlayingInputMode"

#: The mode that caused issue #1015. It is not "discouraged"; it is wrong for
#: this game, because it offers no way to keep the cursor visible during capture
#: and every order this game takes is read off the cursor.
THE_BROKEN_MODE = "FInputModeGameOnly"


def game_sources() -> list[pathlib.Path]:
    """Every C++ file in the game module except its tests.

    TESTS ARE EXCLUDED DELIBERATELY. A test that wanted to prove the broken mode
    is refused would have to name it, and this file would then refuse the test.
    """
    return [path for path in sorted(SOURCE.rglob("*.cpp")) + sorted(SOURCE.rglob("*.h"))
            if "Tests" not in path.parts]


@pytest.fixture(scope="module")
def sources() -> dict[pathlib.Path, str]:
    files = game_sources()
    if not files:
        pytest.skip(f"no C++ sources under {SOURCE}")
    return {path: path.read_text(encoding="utf-8", errors="replace")
            for path in files}


def one_function_body(text: str) -> str:
    """The body of `ApplyPlayingInputMode`, from its signature to its close."""
    start = text.index(f"void ACataclysmPlayerController::{THE_ONE_FUNCTION}")
    return text[start:text.index("\n}", start)]


def test_the_one_mode_the_game_applies_is_not_the_one_that_stops_movement(sources):
    """The single input mode this game builds is not `FInputModeGameOnly`.

    THE FAULT THIS CATCHES IS SILENT AND IS NOT A CRASH. The mode is applied,
    the widget closes, and the game looks exactly right until the player tries
    to walk. Nothing logs, nothing asserts, and no automation test can see it.

    SCOPED TO THE FUNCTION BODY RATHER THAN THE WHOLE MODULE, AND THAT IS
    DELIBERATE. Refusing the NAME anywhere refuses the comments that explain why
    this exists, and three of them name it. What matters is that nothing
    CONSTRUCTS it. The check below, that `SetInputMode` is called exactly once,
    is what makes this function body the only place it could be constructed.
    """
    body = one_function_body(sources[CONTROLLER_CPP])

    assert THE_BROKEN_MODE not in body, (
        f"{THE_ONE_FUNCTION} now builds {THE_BROKEN_MODE}. It captures the "
        "mouse permanently and cannot keep the cursor visible during that "
        "capture, and every order this game takes is read off the cursor. "
        "Issue #1015 is what happens next: the player cannot move."
    )

    # AND IT STILL SAYS BOTH THE THINGS THAT MAKE IT WORK. Either option missing
    # is the same symptom by a different route -- a cursor that cannot leave the
    # window, or one that vanishes while the move button is held.
    for option in ("DoNotLock", "SetHideCursorDuringCapture(false)"):
        assert option in body, (
            f"{THE_ONE_FUNCTION} no longer passes {option}. See its comment: "
            "both options are load-bearing, and without them click-to-move "
            "steers nowhere while the button is held. Issue #1015."
        )


def test_only_one_place_in_the_game_sets_an_input_mode(sources):
    """`SetInputMode(` is called exactly once, and it is inside the one function.

    ONE MODE IN ONE PLACE IS THE WHOLE POINT. Three call sites each built their
    own mode by hand, which is how opening and closing came to disagree.
    """
    calls = {str(path.relative_to(REPO_ROOT)): text.count("SetInputMode(")
             for path, text in sources.items() if "SetInputMode(" in text}

    assert calls == {str(CONTROLLER_CPP.relative_to(REPO_ROOT)): 1}, (
        f"SetInputMode is called from {calls}. It belongs in exactly one place, "
        f"{THE_ONE_FUNCTION} in "
        f"{CONTROLLER_CPP.relative_to(REPO_ROOT)}, and everything else calls "
        "that. Issue #1015."
    )

    body = sources[CONTROLLER_CPP]
    start = body.index(f"void ACataclysmPlayerController::{THE_ONE_FUNCTION}")
    assert "SetInputMode(" in body[start:], (
        f"the one SetInputMode call is not inside {THE_ONE_FUNCTION}."
    )


def test_the_game_sets_its_input_mode_at_startup(sources):
    """`BeginPlay` applies it, so the game does not inherit an engine default.

    WITHOUT THIS THE REST IS HALF A FIX. Every screen could restore the same
    mode and the game would still boot into a mode nothing in the project chose,
    which is what made the first close look like a change rather than a restore.
    """
    body = sources[CONTROLLER_CPP]
    assert "void ACataclysmPlayerController::BeginPlay()" in body, (
        "the player controller no longer overrides BeginPlay, so nothing sets "
        "the input mode at startup. Issue #1015."
    )

    start = body.index("void ACataclysmPlayerController::BeginPlay()")
    end = body.index("\n}", start)
    assert THE_ONE_FUNCTION in body[start:end], (
        f"BeginPlay no longer calls {THE_ONE_FUNCTION}, so the game starts in "
        "whatever the engine defaults to. Issue #1015."
    )


def test_every_screen_that_closes_restores_the_playing_mode(sources):
    """All three closing paths call it, and none builds a mode of its own.

    THE THREE ARE NOT INTERCHANGEABLE and all three have to be named. The
    character creator can be closed two ways -- with its key and with its
    confirm button -- and those live in different files, which is exactly how
    they came to be able to disagree.
    """
    creation_widget = (SOURCE / "Interface"
                       / "CataclysmCharacterCreationWidget.cpp")

    expected = {
        # BeginPlay, plus opening and closing each of the two screens the
        # controller owns: the character creator and the passive tree.
        CONTROLLER_CPP: 5,
        # The character creator's confirm button, which closes it a second way
        # from a different file.
        creation_widget: 1,
    }

    # THE SEMICOLON IS LOAD-BEARING AND WAS MISSING AT FIRST. Counting
    # `ApplyPlayingInputMode(` also counts the function's own DEFINITION, which
    # made the controller's total 6 rather than 5 and left this check one call
    # slacker than its own message claimed -- it passed with a call removed. The
    # guard proof for this file is what found that, and it is the reason to run
    # one rather than to assume a new check works.
    call = f"{THE_ONE_FUNCTION}();"

    for path, least in expected.items():
        found = sources[path].count(call)
        assert found >= least, (
            f"{path.relative_to(REPO_ROOT)} calls {THE_ONE_FUNCTION} {found} "
            f"times and needs at least {least}. A screen that closes without "
            "restoring the playing mode leaves the player in whatever mode the "
            "screen wanted. Issue #1015."
        )
