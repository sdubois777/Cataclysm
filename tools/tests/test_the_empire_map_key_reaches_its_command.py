"""M opens the screen `Cataclysm.EmpireMap` opens, and C the one `Cataclysm.CharacterSheet` opens.

WHY THIS EXISTS. Until 2026-11-17 the empire overview opened only from that
console command. The coordinating session had ruled on 2026-11-03, under the
owner's standing mandate that every system needs a basic interface a player can
reach without the console: "M opens and closes the empire map", in both control
schemes. Built in issue #2733 slice 3, from the parked commit 288b45cc4, which
bound Y to the character sheet as well; that half stayed parked.

THE CHARACTER SHEET JOINED ON 2026-11-19, on C rather than Y. The project
owner ruled "C opens the sheet" on issue #2770: C opened the character creator
until then, and the parked commit chose Y only because of that. The creator's
action went with it; the last test below checks that it has no key and that
its console command still opens it. The file keeps its name because
`docs/DECISIONS.md` cites it by name.

A KEY IS FOUR LINKS LONG, and three of them already have a check:

    a mapping in MOUSE_MAPPINGS and KEYBOARD_MAPPINGS  ->  an Input Action
        ->  a BindNativeAction in SetupInputComponent  ->  a handler
        ->  the function the console command calls

`test_native_input_actions_match_the_engine.py` joins the names and the
generator, `test_controls_table_matches_the_input_assets.py` joins the design's
tables and the generator, and the automation tests
`Cataclysm.Input.TheEmpireMapIsOnMInBothSchemes` and
`Cataclysm.Input.TheCharacterSheetIsOnCInBothSchemes` read which key the
generated contexts hold. NOTHING CHECKS THE LAST TWO LINKS FOR ANY
SCREEN KEY, and no automation test can: a world built by `UWorld::CreateWorld`
has no local player, so `SetupInputComponent` binds nothing there. A binding
deleted, or a handler that called the wrong screen, would leave every test
passing while the key did nothing, or opened the wrong thing.

SCOPED TO THESE TWO KEYS, and saying so. The inventory, passive tree and
skill-choice keys have the same four links and are not read here.

WHAT IS CHECKED, by reading the C++ as text with comments removed:

  1. the generator puts the action on its ruled key in both schemes, and
     nothing else on that key;
  2. `SetupInputComponent` binds the action, on Started, to its handler;
  3. the handler's whole body is a call to the toggle;
  4. the console command calls the same toggle, and for the empire map no
     longer starts the run itself: the toggle does, so the key and the command
     are one behaviour rather than two copies.

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
CONTROLLER_H = SOURCE / "Player" / "CataclysmPlayerController.h"
CHARACTER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
GENERATOR = REPO_ROOT / "tools" / "generate_input_assets.py"

CONTROLLER = "ACataclysmPlayerController"

#: The screen keys: the Input Action asset, the C++ action name, the engine key
#: the ruling gave it, the handler the key calls, the toggle, the console
#: command that calls the same toggle, and the ruling, which a failure names.
KEYS = {
    "empire map": {
        "asset": "IA_ToggleEmpireMap",
        "name": "ToggleEmpireMap",
        "key": "M",
        "handler": "Input_ToggleEmpireMap",
        "toggle": "ToggleEmpireMap",
        "command": "Cataclysm.EmpireMap",
        "ruling": "the coordinating session's ruling of 2026-11-03",
    },
    "character sheet": {
        "asset": "IA_ToggleCharacterSheet",
        "name": "ToggleCharacterSheet",
        "key": "C",
        "handler": "Input_ToggleCharacterSheet",
        "toggle": "ToggleCharacterSheet",
        "command": "Cataclysm.CharacterSheet",
        "ruling": "the project owner's ruling of 2026-11-19 (issue #2770)",
    },
}

#: The character creator, which C opened until 2026-11-19 and which has no key
#: since: its action, the C++ name and the key's handler were removed, and its
#: console command is how it opens inside a level. Issue #2770.
CREATOR = {
    "asset": "IA_ToggleCharacterCreation",
    "name": "ToggleCharacterCreation",
    "handler": "Input_ToggleCharacterCreation",
    "toggle": "ToggleCharacterCreation",
    "command": "Cataclysm.CharacterCreation",
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
def controller_header() -> str:
    if not CONTROLLER_H.is_file():
        pytest.fail(f"{CONTROLLER_H.relative_to(REPO_ROOT)} does not exist. "
                    "If it moved, move it here too.")
    return strip_comments(CONTROLLER_H.read_text(encoding="utf-8"))


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
        f"{keys_of_the_action}; {spec['ruling']} puts the {screen} on "
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


def native_action_assets() -> list[str]:
    """The asset names in NATIVE_ACTIONS, each row's first element.

    Read row by row with `ast` rather than `ast.literal_eval`, which raises on
    this list: each row's third element is a name (`BOOLEAN`, `AXIS1D`,
    `AXIS2D`) and not a literal.
    """
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and "NATIVE_ACTIONS" in [
                t.id for t in node.targets if isinstance(t, ast.Name)]:
            return [row.elts[0].value for row in node.value.elts
                    if isinstance(row, ast.Tuple) and row.elts
                    and isinstance(row.elts[0], ast.Constant)]
    raise AssertionError("tools/generate_input_assets.py has no list called NATIVE_ACTIONS.")


def test_the_character_creator_has_no_key_and_its_command_still_opens_it(
        controller: str, controller_header: str, character: str) -> None:
    """C was the creator's until 2026-11-19; now no key is, and the console still opens it.

    THE OWNER'S RULING HAS TWO HALVES (issue #2770): C opens the character
    sheet, and the creator stays reachable from the front end's New character
    screen and from `Cataclysm.CharacterCreation`. The rows above check the
    first half. This checks the second, and that nothing of the creator's key
    was left behind: an action no key reaches fails
    `test_native_input_actions_match_the_engine.py`, and a handler nothing binds
    is code that cannot run.
    """
    assert CREATOR["asset"] not in native_action_assets(), (
        f"tools/generate_input_assets.py builds {CREATOR['asset']} again. The "
        "character creator has had no key since issue #2770.")
    for scheme in SCHEMES:
        keys = [key for action, key, _ in generator_rows(scheme) if action == CREATOR["asset"]]
        assert keys == [], (
            f"{scheme} in tools/generate_input_assets.py puts the character "
            f"creator on {keys}. C is the character sheet's since issue #2770, "
            "and the creator has no key.")

    for text, where in ((controller, CONTROLLER_CPP), (controller_header, CONTROLLER_H)):
        for gone in (CREATOR["handler"], f"Names::{CREATOR['name']}"):
            assert gone not in text, (
                f"{where.relative_to(REPO_ROOT)} still names {gone}, comments aside: "
                "the character creator's key was removed in issue #2770, so its "
                "handler and its binding went with it.")

    function_body(controller, CREATOR["toggle"])
    command = squeeze(console_command_body(character, CREATOR["command"]))
    call = f"Controller->{CREATOR['toggle']}();"
    assert command.count(call) == 1, (
        f"{CREATOR['command']} should call {call} once: it is how the character "
        "creator opens inside a level since C became the character sheet's key. "
        f"It calls it {command.count(call)} times.")
