"""A press on an open screen acts once, however long the button is held.

WHY THIS EXISTS. Issue #1016. The project owner reported that equipping gear
"will often not work on the first try, I'll have to right click multiple times",
and then that it only happened when the target gear slot was already occupied.

THE CAUSE, AND WHY THAT SECOND OBSERVATION IS WHAT FOUND IT.
`UCataclysmInputComponent::BindAbilityActions` binds an ability slot's pressed
handler to `ETriggerEvent::Triggered`, which fires EVERY FRAME the button is
held. That is right for an ability -- holding the button keeps casting -- and
wrong for the branch in `ACataclysmPlayerController::Input_AbilitySlotPressed`
that diverts a press over an open screen to the inventory.

`UCataclysmWearing::WearFromCarried` empties the carried cell and returns
whatever came off the body to the FIRST FREE SLOT, which is usually the cell just
emptied. So frame one wore the clicked item, frame two found the old item sitting
in that cell and wore it back on, frame three swapped again, and so on. Whether
the player ended up wearing what they clicked depended on whether they held the
button for an odd or an even number of frames.

An empty target slot never showed it: nothing comes off, the cell stays empty,
and every later frame is refused with "There is nothing there to wear". The play
test log is full of those in runs of consecutive frames, which is what a WORKING
press looked like under this defect.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so nothing
in `game/Source/Cataclysm/Tests/` runs on a pull request. Reading the source as
text does. Same arrangement, and same reason, as
`tools/tests/test_one_input_mode.py` and
`tools/tests/test_a_dying_enemy_plays_its_death_animation.py`.

WHAT THIS CANNOT CHECK. That the game actually behaves this way when played. The
automation tests run with `-nullrhi`, nothing in the harness can see a cursor or
hold a button down for two frames, and the branch this guards needs a real
viewport and a loaded input configuration to be reached at all. What is checked is
that the three pieces the fix is made of are all still present, because losing any
one of them brings the fault back in full.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
CONTROLLER_CPP = SOURCE / "Player" / "CataclysmPlayerController.cpp"
INPUT_COMPONENT_H = SOURCE / "Input" / "CataclysmInputComponent.h"

#: What records that this press has already acted on the screen.
THE_RECORD = "SlotsAlreadyPressedOnTheInventory"

#: What acts on the screen. Calling this twice in one press is the whole defect.
THE_ACTION = "RightPressOnTheInventoryScreen()"


@pytest.fixture(scope="module")
def controller() -> str:
    if not CONTROLLER_CPP.is_file():
        pytest.skip(f"{CONTROLLER_CPP} is not present")
    return CONTROLLER_CPP.read_text(encoding="utf-8", errors="replace")


@pytest.fixture(scope="module")
def input_component() -> str:
    if not INPUT_COMPONENT_H.is_file():
        pytest.skip(f"{INPUT_COMPONENT_H} is not present")
    return INPUT_COMPONENT_H.read_text(encoding="utf-8", errors="replace")


def body_of(text: str, signature: str) -> str:
    """One function's body, from its signature to the line that closes it."""
    start = text.index(signature)
    return text[start:text.index("\n}", start)]


def test_a_press_that_already_acted_on_the_screen_does_not_act_again(controller):
    """The inventory branch checks the record before acting.

    THE FAULT THIS CATCHES IS NOT A CRASH AND NOT EVEN A REFUSAL. Every frame of
    the press does something valid; it is only the SECOND one that undoes the
    first. Nothing logs, because a swap is a success.
    """
    pressed = body_of(
        controller,
        "void ACataclysmPlayerController::Input_AbilitySlotPressed")

    assert THE_ACTION in pressed, (
        "the inventory branch no longer acts on a press at all. Issue #853 is "
        "what it is for: a right press on the bag must not fire the ability at "
        "the floor behind the panel."
    )

    # ASKED AS A PRESENCE CHECK FIRST, so removing the guard altogether fails
    # with this sentence rather than with a ValueError from the index below.
    assert THE_RECORD in pressed, (
        f"the inventory branch no longer consults {THE_RECORD} at all. The "
        "pressed handler is bound to ETriggerEvent::Triggered, which fires every "
        "frame the button is held, so acting unguarded wears an item and then "
        "wears it back off again. Issue #1016."
    )

    assert pressed.index(THE_RECORD) < pressed.index(THE_ACTION), (
        f"{THE_ACTION} is reached before {THE_RECORD} is consulted, so the "
        "guard cannot stop the second frame of a held press. Issue #1016."
    )


def test_releasing_the_button_lets_the_next_press_act(controller):
    """The record is cleared on release, or the button dies after one use.

    THE OPPOSITE FAULT TO THE ONE ABOVE, and it would be worse: the first press
    would work and every press after it would do nothing at all.
    """
    released = body_of(
        controller,
        "void ACataclysmPlayerController::Input_AbilitySlotReleased")

    assert f"{THE_RECORD}.Remove(" in released, (
        f"{THE_RECORD} is never cleared on release, so a slot acts on the "
        "inventory once and then never again for the rest of the session. "
        "Issue #1016."
    )


def test_a_cancelled_press_counts_as_a_release(input_component):
    """An ability slot binds Canceled as well as Completed.

    WHY IT MATTERS RATHER THAN BEING TIDY. A press interrupted by a mapping
    context change reports Canceled and NEVER reports Completed. Without this
    binding the release handler never runs, the record above is never cleared,
    and the button appears dead from then on -- the fault the test above
    describes, arriving by a route no release can undo.

    The move button already binds both events for this exact reason, and says so
    in `ACataclysmPlayerController::SetupInputComponent`.
    """
    binder = body_of(
        input_component,
        "void UCataclysmInputComponent::BindAbilityActions")

    for event in ("ETriggerEvent::Completed", "ETriggerEvent::Canceled"):
        assert event in binder, (
            f"BindAbilityActions no longer binds {event}. Both are needed: a "
            "cancelled press reports only Canceled, and a slot whose release "
            "never runs keeps its per-press state for ever. Issue #1016."
        )
