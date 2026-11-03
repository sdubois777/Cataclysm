"""A message the game prints that names a `Cataclysm.` console command names one that is registered.

The refusal `Cataclysm.LoadCharacter` gives for a run saved inside an empire dungeon, loaded in a level with no
dungeon, told the player to load it "in a dungeon level, where Cataclysm.WalkDungeon works". No command of that name
was ever registered: the one that walks an empire dungeon is `Cataclysm.EnterDungeon`. Corrected on 2026-11-03, in
issue #1324's slice 6c branch, and this check was added beside it.

A message here is every string literal inside the arguments of an `Ar.Log`, `Ar.Logf`, `Refused` or `FString::Printf`
call in the game's non-test C++ (`Ar` is the output device a console command writes to; `Refused` builds the reason a
load is refused; `FString::Printf` builds most of what a screen shows, such as the empire map's line for the Cataclysm
boss dungeon, which names the command that walks it). `UE_LOG` is left out: one names a configuration section,
`/Script/Cataclysm.CataclysmPlayerController`, which is no command.
A registered name is the first argument of an `FAutoConsoleCommand...`, `FAutoConsoleVariable...` or
`TAutoConsoleVariable<...>` declaration anywhere under `game/Source`.
"""
import functools
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source"

# Measured on 2026-11-03, when this was written: 19 distinct `Cataclysm.` names (25 mentions) in the messages, and 138
# registered names. A count far below either means the scan stopped finding them, and the check would then pass on
# nothing.
FEWEST_MESSAGE_NAMES = 15
FEWEST_REGISTERED = 100

REGISTERED = re.compile(
    r"(?:FAutoConsole\w+|TAutoConsoleVariable<[^>]*>)[^(;]*\(\s*TEXT\(\"(Cataclysm\.[\w.]+)\"")
MESSAGE_CALL = re.compile(r"\b(?:Ar\.Logf?|Refused|FString::Printf)\s*\(")
STRING_LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
CHARACTER_LITERAL = re.compile(r"'(?:\\.|[^\\'\n])'")
CONSOLE_NAME = re.compile(r"Cataclysm\.[A-Z][\w.]*\w")


def sources():
    return [path for path in sorted(SOURCE.rglob("*")) if path.suffix in (".cpp", ".h")]


@functools.lru_cache(maxsize=1)
def registered_names():
    names = set()
    for path in sources():
        names.update(REGISTERED.findall(path.read_text(encoding="utf-8", errors="replace")))
    return frozenset(names)


def call_end(text, open_index):
    """The index just past the parenthesis that closes the one at `open_index`, stepping over string literals,
    character literals and comments, whose brackets and semicolons are text: the boss dungeon's line holds a
    semicolon, and a scan that stopped at the first one missed the command it names."""
    depth = 0
    index = open_index
    while index < len(text):
        char = text[index]
        if char == '"':
            literal = STRING_LITERAL.match(text, index)
            index = literal.end() - 1 if literal else index
        elif char == "'" and CHARACTER_LITERAL.match(text, index):
            index = CHARACTER_LITERAL.match(text, index).end() - 1
        elif text.startswith("//", index):
            line_end = text.find("\n", index)
            index = line_end if line_end != -1 else len(text)
        elif text.startswith("/*", index):
            comment_end = text.find("*/", index + 2)
            index = comment_end + 1 if comment_end != -1 else len(text)
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    return len(text)


@functools.lru_cache(maxsize=1)
def names_in_messages():
    """(file, line, name) for every `Cataclysm.` name inside a message, outside the tests. A literal inside two
    message calls, as a `Refused(FString::Printf(...))` holds, is counted once."""
    found = []
    for path in sources():
        if "Tests" in path.relative_to(SOURCE).parts:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        seen = set()
        for call in MESSAGE_CALL.finditer(text):
            for literal in STRING_LITERAL.finditer(text, call.end(), call_end(text, call.end() - 1)):
                if literal.start() in seen:
                    continue
                seen.add(literal.start())
                for name in CONSOLE_NAME.findall(literal.group(1)):
                    line = text.count("\n", 0, literal.start()) + 1
                    found.append((path.relative_to(ROOT).as_posix(), line, name))
    return tuple(found)


def test_the_scan_finds_the_registered_names_and_the_messages():
    registered = registered_names()
    assert len(registered) >= FEWEST_REGISTERED, sorted(registered)
    assert "Cataclysm.EnterDungeon" in registered
    assert "Cataclysm.LoadCharacter" in registered
    named = {name for _, _, name in names_in_messages()}
    assert len(named) >= FEWEST_MESSAGE_NAMES, sorted(named)
    # The message this check was written for is among those scanned, and so is the empire map's line for the
    # Cataclysm boss dungeon.
    for source in ("Save/CataclysmSaveLoad.cpp", "Interface/CataclysmEmpireMapLayout.cpp"):
        assert any(path.endswith(source) and name == "Cataclysm.EnterDungeon"
                   for path, _, name in names_in_messages()), source


def test_every_console_command_a_message_names_is_registered():
    registered = registered_names()
    unknown = [f"{path}:{line}: {name}" for path, line, name in names_in_messages() if name not in registered]
    assert not unknown, "a message names a console command that is not registered:\n" + "\n".join(unknown)
