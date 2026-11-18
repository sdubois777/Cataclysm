"""M opens the screen `Cataclysm.EmpireMap` opens.

WHY THIS EXISTS. Until 2026-11-17 the empire overview opened only from that
console command. The coordinating session had ruled on 2026-11-03, under the
owner's standing mandate that every system needs a basic interface a player can
reach without the console: "M opens and closes the empire map", in both control
schemes. Built in issue #2733 slice 3, from the parked commit 288b45cc4, which
bound Y to the character sheet as well; that half stays parked, and so this
file checks M alone.

A KEY IS FOUR LINKS LONG, and three of them already have a check:

    a mapping in MOUSE_MAPPINGS and KEYBOARD_MAPPINGS  ->  an Input Action
        ->  a BindNativeAction in SetupInputComponent  ->  a handler
        ->  the function the console command calls

`test_native_input_actions_match_the_engine.py` joins the names and the
generator, `test_controls_table_matches_the_input_assets.py` joins the design's
tables and the generator, and the automation test
`Cataclysm.Input.TheEmpireMapIsOnMInBothSchemes` reads which key the generated
contexts hold. NOTHING CHECKS THE LAST TWO LINKS FOR ANY
SCREEN KEY, and no automation test can: a world built by `UWorld::CreateWorld`
has no local player, so `SetupInputComponent` binds nothing there. A binding
deleted, or a handler that called the wrong screen, would leave every test
passing while the key did nothing, or opened the wrong thing.

SCOPED TO THIS ONE KEY, and saying so. The inventory, character creator,
passive tree and skill-choice keys have the same four links and are not read
here.

WHAT IS CHECKED, by reading the C++ as text with comments removed:

  1. the generator puts the action on its ruled key in both schemes, and
     nothing else on that key;
  2. `SetupInputComponent` binds the action, on Started, to its handler;
  3. the handler's whole body is a call to the toggle;
  4. the console command calls the same toggle, and no longer starts the run
     itself: the toggle does, so the key and the command are one behaviour
     rather than two copies.

WHAT IS NOT. That a press reaches Enhanced Input, that the screen draws, or that
the generated assets on disk match the generator; the generator has to be run in
the editor for that (`python tools/run_editor_python.py
tools/generate_input_assets.py`).
"""

from __future__ import annotations

import ast
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
CONTROLLER_CPP = SOURCE / "Player" / "CataclysmPlayerController.cpp"
CHARACTER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
GENERATOR = REPO_ROOT / "tools" / "generate_input_assets.py"

CONTROLLER = "ACataclysmPlayerController"

#: The screen key: the Input Action asset, the C++ action name, the engine key
#: the ruling gave it, the handler the key calls, the toggle, and the console
#: command that calls the same toggle. A table of one, so a second screen key
#: is a row rather than a rewrite.
KEYS = {
    "empire map": {
        "asset": "IA_ToggleEmpireMap",
        "name": "ToggleEmpireMap",
        "key": "M",
        "handler": "Input_ToggleEmpireMap",
        "toggle": "ToggleEmpireMap",
        "command": "Cataclysm.EmpireMap",
    },
}

SCHEMES = ("MOUSE_MAPPINGS", "KEYBOARD_MAPPINGS")


def strip_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, string literals kept.

    Read character by character rather than by a line's first character, so a
    comment after code goes and a `//` inside a string literal stays.
    """
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
    """Every run of whitespace as one space, so layout cannot hide a call."""
    return re.sub(r"\s+", " ", text).strip()


def braced_from(text: str, start: int, what: str) -> str:
    """The text between the first `{` at or after `start` and its matching `}`."""
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


def function_body(text: str, name: str) -> str:
    """The body of `ACataclysmPlayerController::<name>()`, comments removed."""
    signature = f"void {CONTROLLER}::{name}()"
    assert text.count(signature) == 1, (
        f"{CONTROLLER_CPP.relative_to(REPO_ROOT)} should define {signature} "
        f"exactly once; it appears {text.count(signature)} times.")
    return braced_from(text, text.find(signature), signature)


@pytest.fixture(scope="module")
def controller() -> str:
    if not CONTROLLER_CPP.is_file():
        pytest.fail(f"{CONTROLLER_CPP.relative_to(REPO_ROOT)} does not exist. "
                    "If it moved, move it here too.")
    return strip_comments(CONTROLLER_CPP.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def character() -> str:
    if not CHARACTER_CPP.is_file():
        pytest.fail(f"{CHARACTER_CPP.relative_to(REPO_ROOT)} does not exist. "
                    "If it moved, move it here too.")
    return strip_comments(CHARACTER_CPP.read_text(encoding="utf-8"))


def generator_rows(list_name: str) -> list[tuple[str, str, list]]:
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and list_name in [
                t.id for t in node.targets if isinstance(t, ast.Name)]:
            return ast.literal_eval(node.value)
    raise AssertionError(f"tools/generate_input_assets.py has no list called {list_name}.")


def console_command_body(character: str, command: str) -> str:
    """The lambda a console command runs, comments removed."""
    literal = f'TEXT("{command}")'
    assert character.count(literal) == 1, (
        f"{CHARACTER_CPP.relative_to(REPO_ROOT)} should register {command} "
        f"exactly once; {literal} appears {character.count(literal)} times.")
    at = character.find(literal)
    lambda_at = character.find("[](", at)
    assert lambda_at != -1, f"{command} registers no lambda after its name"
    return braced_from(character, lambda_at, command)


@pytest.mark.parametrize("scheme", SCHEMES)
@pytest.mark.parametrize("screen", sorted(KEYS))
def test_each_scheme_puts_the_screen_on_its_ruled_key_and_nothing_else_there(
        screen: str, scheme: str) -> None:
    spec = KEYS[screen]
    rows = generator_rows(scheme)
    keys_of_the_action = [key for action, key, _ in rows if action == spec["asset"]]
    actions_on_the_key = [action for action, key, _ in rows if key == spec["key"]]

    assert keys_of_the_action == [spec["key"]], (
        f"{scheme} in tools/generate_input_assets.py puts {spec['asset']} on "
        f"{keys_of_the_action}; the ruling of 2026-11-03 puts the {screen} on "
        f"{spec['key']}, once, in both schemes.")
    assert actions_on_the_key == [spec["asset"]], (
        f"{scheme} in tools/generate_input_assets.py puts {actions_on_the_key} "
        f"on {spec['key']}; only {spec['asset']} may be there, or one press "
        "does two things.")


@pytest.mark.parametrize("screen", sorted(KEYS))
def test_the_controller_binds_the_key_to_its_handler(controller: str, screen: str) -> None:
    spec = KEYS[screen]
    setup = squeeze(function_body(controller, "SetupInputComponent"))
    binding = (f"Input->BindNativeAction(Config, Names::{spec['name']}, "
               f"ETriggerEvent::Started, this, &{CONTROLLER}::{spec['handler']});")
    assert squeeze(binding) in setup, (
        f"{CONTROLLER}::SetupInputComponent does not bind Names::{spec['name']} "
        f"on Started to {spec['handler']}, so the {screen}'s key does nothing "
        f"and nothing says so. Expected, whitespace aside:\n  {binding}")


@pytest.mark.parametrize("screen", sorted(KEYS))
def test_the_handler_calls_the_toggle_and_nothing_else(controller: str, screen: str) -> None:
    spec = KEYS[screen]
    body = squeeze(function_body(controller, spec["handler"]))
    assert body == f"{spec['toggle']}();", (
        f"{CONTROLLER}::{spec['handler']} should be a call to {spec['toggle']} "
        f"and nothing else, so the key does exactly what {spec['command']} "
        f"does. Its body is: {body!r}")


@pytest.mark.parametrize("screen", sorted(KEYS))
def test_the_console_command_calls_the_same_toggle(character: str, screen: str) -> None:
    spec = KEYS[screen]
    body = squeeze(console_command_body(character, spec["command"]))
    call = f"Controller->{spec['toggle']}();"
    assert body.count(call) == 1, (
        f"{spec['command']} should call {call} once, the function the key's "
        f"handler calls; it calls it {body.count(call)} times.")


def test_the_empire_map_toggle_starts_the_run_and_its_command_does_not(
        controller: str, character: str) -> None:
    """The run start lives in the toggle, so the key and the command share it.

    BEFORE 2026-11-17 `Cataclysm.EmpireMap` called
    `UCataclysmGameInstance::EmpireRunFor(World, true)` itself and then the
    toggle. A key calling the toggle alone would then open a map with no run
    where the command opened one with a run. The line moved into the toggle;
    this checks it is there, and is not ALSO left in the command, which would
    be the copy the move was made to avoid.
    """
    toggle = squeeze(function_body(controller, "ToggleEmpireMap"))
    start = "UCataclysmGameInstance::EmpireRunFor(GetWorld(), true);"
    assert toggle.count(start) == 1, (
        f"{CONTROLLER}::ToggleEmpireMap should start an empire run when there "
        f"is none, once, with {start!r} (comments aside), as the console "
        "command did before the M key. Without it M opens an empty map where "
        "Cataclysm.EmpireMap would have begun a run.")

    command = squeeze(console_command_body(character, "Cataclysm.EmpireMap"))
    assert "EmpireRunFor" not in command, (
        "Cataclysm.EmpireMap asks for the empire run itself again. The toggle "
        "does that since 2026-11-17; a second call here is a copy the key "
        "does not share, and the two can drift apart.")
