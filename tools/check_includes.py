"""Find project types a .cpp file uses whose definition its own includes do not reach.

    python tools/check_includes.py <file.cpp> [<file.cpp> ...]
    python tools/check_includes.py --changed          # the .cpp files git has changed against HEAD
    python tools/check_includes.py --since <ref>      # the .cpp files changed since <ref>, committed or not
    python tools/check_includes.py --all              # every .cpp under game/Source

Exits 0 when no file uses a type it cannot see, 1 when one does, and 2 when it read no file at all -- a check over
nothing is not a pass.

WHY THIS EXISTS. Unreal builds each module as a unity build: it joins many .cpp files into one, so a header one file
includes is seen by every file after it in the same unit. A file that uses a type whose header it never includes then
compiles, until the files are grouped differently or the file is compiled alone, as a build window's second build
does. The forge window of 2026-11-25 failed that way: `CataclysmForgeWidget.cpp` used
`FCataclysmEmpireTreePlayerBonuses` by value with only a forward declaration in view. The compile-only slot the War
line had that day was a unity build, so it could not have seen such a fault; this check can, without the machine.

WHAT IT DOES. For each .cpp named:
- it follows the file's own `#include "..."` lines that resolve to a project header under `game/Source` (relative to
  the including file, or to a module's root), transitively, and adds the types the file defines itself;
- it indexes every project type definition in every header: a struct or class with a body, an enum, a `using` alias,
  and a delegate declared by a `DECLARE_..._DELEGATE...` macro;
- it reads the file with comments and string literals removed, keeping every line break, so a line number is the
  file's own;
- and it reports each project type the file names whose definition that graph does not reach.

WHAT FAILS AND WHAT IS A NOTE. A use that needs the complete type FAILS: a declaration by value, `Type::`, `sizeof`,
a construction `Type(` or `Type{`, and the type as the argument of `Cast`, `CastChecked`, `ExactCast`, `NewObject`,
`SpawnActor`, `CreateDefaultSubobject`, `GetSubsystem`, `FindComponentByClass`, `IsA`, `TArray`, `TSet` or `TOptional`.
Every other use -- a pointer or a reference, a forward declaration, a weak pointer -- is a NOTE and does not fail,
because a forward declaration is enough for it. A note still deserves a look: MEMBER ACCESS THROUGH SUCH A POINTER
needs the header, and this check cannot see which variable holds which type.

WHAT IT DOES NOT DO, said in every report: engine headers are not followed (a project type is never defined in one);
an include under a preprocessor condition is followed as if the condition held; and headers themselves are not
checked, since nothing compiles a header alone.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / "game" / "Source"

LIMITS = ("Not followed: engine headers. Not modelled: preprocessor conditions on includes, and member access through "
          "a pointer to a type seen only by a forward declaration. Headers themselves are not checked.")

_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
_LINE_COMMENT = re.compile(r"//[^\n]*")
_TEXT_LITERAL = re.compile(r'TEXT\("(?:[^"\\\n]|\\.)*"\)')
_STRING_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
_DEFINITION = re.compile(
    r"\b(?:struct|class)\s+(?:\w+_API\s+)?([FUAIE][A-Z]\w*)\s*(?:final\s*)?(?::[^;{]*)?\{"
    r"|\benum\s+(?:class\s+)?(E[A-Z]\w*)\s*(?::\s*\w+\s*)?\{"
    r"|\busing\s+([FUAIE][A-Z]\w*)\s*="
    r"|DECLARE_\w*DELEGATE\w*\(\s*(F[A-Z]\w*)")
_NAME = re.compile(r"\b([FUAIE][A-Z][A-Za-z0-9_]*)\b")
_NEEDS_THE_TYPE_AS_ARGUMENT = ("Cast", "CastChecked", "ExactCast", "NewObject", "SpawnActor", "CreateDefaultSubobject",
                               "GetSubsystem", "FindComponentByClass", "IsA", "TArray", "TSet", "TOptional")


def _key(path: pathlib.Path | str) -> str:
    """`path` as an absolute, normalised string, case folded where the file system folds case."""
    return os.path.normcase(os.path.normpath(os.path.abspath(str(path))))


def strip(text: str) -> str:
    """`text` with comments and string literals removed, every line break kept, so line numbers stay the file's."""
    text = _BLOCK_COMMENT.sub(lambda match: "\n" * match.group(0).count("\n") or " ", text)
    text = _LINE_COMMENT.sub(" ", text)
    text = _TEXT_LITERAL.sub('""', text)
    return _STRING_LITERAL.sub('""', text)


def needs_the_complete_type(line: str, start: int, end: int) -> bool:
    """Whether the name at `line[start:end]` is used in a way a forward declaration cannot serve."""
    before = line[:start].rstrip()
    after = line[end:].lstrip()
    if re.search(r"\b(?:class|struct|enum|friend\s+class)\s*$", before):
        return False
    if after.startswith("::") or after.startswith("(") or after.startswith("{"):
        return True
    if re.search(r"\bsizeof\s*\(\s*$", before):
        return True
    argument = re.search(r"\b(\w+)\s*<\s*(?:const\s+)?$", before)
    if argument and argument.group(1) in _NEEDS_THE_TYPE_AS_ARGUMENT:
        return not after.startswith("*") and not after.startswith("&")
    if after.startswith("*") or after.startswith("&") or after.startswith(">") or after.startswith(","):
        return False
    # A DECLARATION BY VALUE: the type, then a name. `const` before it changes nothing.
    return re.match(r"[A-Za-z_]\w*", after) is not None


@dataclasses.dataclass(frozen=True)
class Use:
    name: str
    line: int
    fails: bool


@dataclasses.dataclass
class FileReport:
    path: pathlib.Path
    uses: list[Use]

    def failing(self) -> list[Use]:
        return [use for use in self.uses if use.fails]

    def notes(self) -> list[Use]:
        return [use for use in self.uses if not use.fails]


class Index:
    """Every header under `source`, parsed once: what it includes and which types it defines.

    PATHS ARE KEPT AS NORMALISED STRINGS, and an include is resolved against the set of headers found, never by asking
    the file system: `pathlib.Path.resolve` on every lookup was half of a whole-tree run's time (measured 2026-11-25).
    """

    def __init__(self, source: pathlib.Path = SOURCE) -> None:
        self.source = source
        self.headers: dict[str, pathlib.Path] = {_key(path): path for path in source.rglob("*.h")}
        self._roots = [_key(path) for path in source.iterdir() if path.is_dir()] if source.is_dir() else []
        self._parsed: dict[str, tuple[list[str], set[str]]] = {}
        self._closures: dict[str, frozenset[str]] = {}
        self.definitions: dict[str, set[str]] = {}
        for key in self.headers:
            for name in self._parse(key)[1]:
                self.definitions.setdefault(name, set()).add(key)

    def resolve(self, include: str, here: str) -> str | None:
        """The project header `#include "include"` in the file `here` names, or None for an engine header."""
        beside = _key(os.path.join(os.path.dirname(here), include))
        if beside in self.headers:
            return beside
        for root in self._roots:
            candidate = _key(os.path.join(root, include))
            if candidate in self.headers:
                return candidate
        return None

    def _parse(self, key: str) -> tuple[list[str], set[str]]:
        if key not in self._parsed:
            text = pathlib.Path(key).read_text(encoding="utf-8", errors="replace")
            includes = [found for found in (self.resolve(name, key) for name in _INCLUDE.findall(text)) if found]
            defined = {next(group for group in match.groups() if group) for match in _DEFINITION.finditer(strip(text))}
            self._parsed[key] = (includes, defined)
        return self._parsed[key]

    def closure(self, key: str) -> frozenset[str]:
        """Every project header the file `key` reaches through its includes, transitively."""
        if key in self._closures:
            return self._closures[key]
        seen: set[str] = set()
        stack = list(self._parse(key)[0])
        while stack:
            current = stack.pop()
            if current in seen:
                continue
            seen.add(current)
            if current in self._closures:
                seen |= self._closures[current]
                continue
            stack.extend(self._parse(current)[0])
        result = frozenset(seen)
        if key in self.headers:
            self._closures[key] = result
        return result

    def where(self, name: str) -> str:
        """The headers defining `name`, relative to the source root."""
        return ", ".join(sorted(self.headers[key].relative_to(self.source).as_posix()
                                for key in self.definitions[name]))

    def check(self, cpp: pathlib.Path) -> FileReport:
        """The project types `cpp` names that its own include graph does not define."""
        key = _key(cpp)
        reachable = set(self._parse(key)[1])
        for header in self.closure(key):
            reachable |= self._parse(header)[1]
        text = strip(cpp.read_text(encoding="utf-8", errors="replace"))
        uses: list[Use] = []
        for number, line in enumerate(text.splitlines(), 1):
            for match in _NAME.finditer(line):
                name = match.group(1)
                if name in self.definitions and name not in reachable:
                    uses.append(Use(name, number, needs_the_complete_type(line, match.start(1), match.end(1))))
        return FileReport(cpp, uses)


def _git_names(*arguments: str) -> list[pathlib.Path]:
    names = subprocess.run(["git", *arguments], cwd=REPO_ROOT, capture_output=True, text=True,
                           check=True).stdout.split()
    return [REPO_ROOT / name for name in names if name.endswith(".cpp") and (REPO_ROOT / name).is_file()]


def describe(report: FileReport, index: Index, root: pathlib.Path = REPO_ROOT) -> list[str]:
    """The lines printed for one file with something to say."""
    try:
        shown = report.path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        shown = report.path.as_posix()
    lines = [f"{shown}:"]
    for kind, uses in (("FAILS", report.failing()), ("note", report.notes())):
        by_name: dict[str, list[int]] = {}
        for use in uses:
            by_name.setdefault(use.name, []).append(use.line)
        for name, numbers in sorted(by_name.items()):
            lines.append(f"  {kind}: {name} on line(s) {', '.join(str(n) for n in sorted(set(numbers)))}"
                         f"; defined in {index.where(name)}, which this file's includes do not reach")
    return lines


def main(argv: list[str] | None = None, source: pathlib.Path = SOURCE) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--changed", action="store_true", help="the .cpp files git reports as changed against HEAD")
    group.add_argument("--since", metavar="REF", help="the .cpp files changed since REF, committed or not")
    group.add_argument("--all", action="store_true", help="every .cpp under game/Source")
    parser.add_argument("paths", nargs="*", type=pathlib.Path)
    args = parser.parse_args(argv)

    if args.changed:
        files = _git_names("diff", "--name-only", "HEAD")
    elif args.since:
        files = _git_names("diff", "--name-only", args.since)
    elif args.all:
        files = sorted(source.rglob("*.cpp"))
    else:
        files = [path for path in args.paths if path.suffix == ".cpp" and path.is_file()]
    if not files:
        print("No .cpp file was read, so nothing was checked.")
        return 2

    index = Index(source)
    failing = notes = 0
    for path in files:
        report = index.check(path)
        if report.uses:
            print("\n".join(describe(report, index)))
        failing += len({use.name for use in report.failing()})
        notes += len({use.name for use in report.notes()})
    print(f"{len(files)} .cpp file(s) read: {failing} type(s) used that the file cannot see, {notes} note(s). {LIMITS}")
    return 1 if failing else 0


if __name__ == "__main__":
    sys.exit(main())
