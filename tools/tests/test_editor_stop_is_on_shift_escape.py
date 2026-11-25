"""Esc reaches the game in Play In Editor: the editor's Stop is moved to Shift+Esc, by an entry the editor can read.

WHY THIS EXISTS. The project owner, 2026-11-24: "Offline and online, pause is a must". The game pauses on Esc
(`ACataclysmPlayerController::InputKey`). In Play In Editor -- where the owner plays -- the editor binds Esc to its own
Stop command and asks its play-world commands before the player controller (`UGameViewportClient::InputKey` runs
`UEditorEngine::ProcessDebuggerCommands` first), so Esc stopped Play and the pause could never be reached.
`game/Config/DefaultEditorSettings.ini` moves Stop to Shift+Esc for this project.

WHY THE ENTRY IS CHECKED FIELD BY FIELD. The engine reads it in `FUserDefinedChords::LoadChords` and `LoadChord`
(Slate's InputBindingManager.cpp, UE 5.8), and `LoadChord` reads Control, Alt, Shift and Key without checking they
are there: an entry missing one dereferences nothing and stops the editor as it starts. `LoadKey` does the same for
BindingContext and CommandName. No automation test starts an editor with this file, so this is the only check before
the owner opens the project.

WHAT IS CHECKED:

  1. the file has exactly one `+ProjectDefinedChords=` entry in a `[ProjectDefinedChords]` section;
  2. with the engine's escapes undone (`FRemoteConfig::ReplaceIniSpecialCharWithChar`), it is a JSON object holding
     all nine fields the editor writes, of the types `LoadKey` and `LoadChord` read;
  3. it names the editor's Stop command, `PlayWorld` / `StopPlaySession`, as the primary chord, on Shift and Escape
     and no other modifier;
  4. the game's controller reads Esc itself: `InputKey` is overridden, compares the key with `EKeys::Escape`, and
     calls `PressEscape` -- the half of the chain that makes moving Stop worth anything.

WHAT IS NOT: that the editor applies it. Only opening the editor shows that; `docs/Play_Loop_Checklist.md`, step 28.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EDITOR_SETTINGS = REPO_ROOT / "game" / "Config" / "DefaultEditorSettings.ini"
CONTROLLER_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player" / "CataclysmPlayerController.cpp"
CONTROLLER_H = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player" / "CataclysmPlayerController.h"

#: The engine's escapes for a key binding written to an ini file, from `SpecialCharMap` in RemoteConfigIni.cpp.
INI_SPECIAL_CHARACTERS = {
    "~OpenBracket~": "{",
    "~CloseBracket~": "}",
    "~Quote~": '"',
    "~Backslash~": "\\",
    "~Forwardslash~": "/",
    "~Bar~": "|",
}

#: Every field the editor writes for a chord (`FUserDefinedChords::SaveChords`), and the JSON type the reader wants.
FIELDS = {
    "BindingContext": str,
    "CommandName": str,
    "ChordIndex": int,
    "Control": bool,
    "Alt": bool,
    "Shift": bool,
    "Command": bool,
    "Key": str,
}


def the_entries() -> list[str]:
    """Every `+ProjectDefinedChords=` value in the `[ProjectDefinedChords]` section, as written."""
    section = None
    entries = []
    for raw in EDITOR_SETTINGS.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section == "ProjectDefinedChords" and line.startswith("+ProjectDefinedChords="):
            entries.append(line[len("+ProjectDefinedChords="):])
    return entries


def unescaped(value: str) -> str:
    for escape, character in INI_SPECIAL_CHARACTERS.items():
        value = value.replace(escape, character)
    return value


def code(path: pathlib.Path) -> str:
    """The file with its line comments removed, so a name in a comment cannot stand in for a call."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def body(text: str, signature: str) -> str:
    start = text.index(signature)
    return text[start:text.index("\n}", start)]


def test_there_is_one_project_chord() -> None:
    entries = the_entries()
    assert len(entries) == 1, (
        f"game/Config/DefaultEditorSettings.ini holds {len(entries)} +ProjectDefinedChords entries in "
        "[ProjectDefinedChords]; it should hold exactly the one that moves Stop to Shift+Esc.")


def test_the_chord_carries_every_field_the_editor_reads() -> None:
    chord = json.loads(unescaped(the_entries()[0]))
    assert isinstance(chord, dict), "the entry is not a JSON object once the engine's escapes are undone"
    for field, kind in FIELDS.items():
        assert field in chord, (
            f"the chord has no {field}. The editor reads it without checking it is there, and stops as it starts.")
        # bool is a subclass of int in Python, so the index is checked as "an int and not a bool".
        if kind is int:
            assert isinstance(chord[field], int) and not isinstance(chord[field], bool), f"{field} is not a number"
        else:
            assert isinstance(chord[field], kind), f"{field} is {type(chord[field]).__name__}, not {kind.__name__}"


def test_the_chord_moves_the_editors_stop_to_shift_escape() -> None:
    chord = json.loads(unescaped(the_entries()[0]))
    assert (chord["BindingContext"], chord["CommandName"]) == ("PlayWorld", "StopPlaySession"), (
        "the chord does not name the editor's Stop command, `FPlayWorldCommands::StopPlaySession` in the context "
        "\"PlayWorld\"; Esc would still stop Play In Editor.")
    assert chord["ChordIndex"] == 0, "the chord is not Stop's primary chord, so plain Esc would still stop Play"
    assert chord["Key"] == "Escape"
    assert chord["Shift"] is True
    assert (chord["Control"], chord["Alt"], chord["Command"]) == (False, False, False)


def test_the_game_reads_escape_itself_and_presses_escape() -> None:
    header = code(CONTROLLER_H)
    assert "virtual bool InputKey(const FInputKeyEventArgs& Params) override;" in header, (
        "ACataclysmPlayerController no longer overrides InputKey, so nothing in the game reads Esc.")
    handler = body(code(CONTROLLER_CPP), "bool ACataclysmPlayerController::InputKey(")
    assert "Params.Key == EKeys::Escape" in handler
    assert "PressEscape();" in handler
    assert "return Super::InputKey(Params);" in handler, "every key but Esc must still reach Enhanced Input"
