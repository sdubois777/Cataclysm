"""Check C++ files for the faults a hand-resolved merge conflict leaves behind.

    python tools/check_resolved_cpp.py <file> [<file> ...]
    python tools/check_resolved_cpp.py --changed     # the C++ files git has changed
    python tools/check_resolved_cpp.py --control     # every .h/.cpp under game/Source

Exits 0 when no file has a complaint, 1 when any has, and 2 when it read no file
at all -- a check over nothing is not a pass.

WHY THIS EXISTS. Issue #1610. On 2026-09-12 four hand-resolved conflicts, on four
branches, broke C++ in the same way, and nothing in the repository looked for it:

- **A documentation comment left with no opening.** Every entry of an enum such
  as `ECataclysmStatCondition` carries a `/** ... */` above it. When two changes
  both append an entry after the same one, git leaves that `/**` OUTSIDE the
  conflict as shared context, and both sides supply a comment body and a closing
  `*/`. Keeping both leaves the second body with nothing opening it: its prose is
  read as code and its `*/` closes nothing. **Braces still balance**, because a
  comment carries none, so only the compiler notices, and the compiler runs only
  in a build window on the one machine.
- **A conflict boundary inside a function body**, which joins the first half of
  one function to the second half of another. That shows as unbalanced braces.

WHAT IT REPORTS, per file:

- a conflict marker line (`<<<<<<< `, `=======`, `>>>>>>> `), with the first line;
- **a `*/` outside any comment**, which is the first fault above;
- a block comment that is opened and never closed;
- a string or character literal left open at the end of its line, which is
  usually the first fault seen from the other side: prose read as code, where an
  apostrophe such as "THE EFFECT'S CAUSER" opens a character literal;
- more `{` than `}`, or `(` than `)`, or the reverse, **counted after comments
  and literals are removed**.

`--control` prints how many files it read. The raw counts of `/*` and `*/` are
printed for a file with a complaint, as information only: they differ between
branches of the same healthy file, so they are never compared with a
remembered number.

THE TWO TRAPS IN STRIPPING, both measured by #1610 on the real tree:

- **a digit separator is not a character literal.** `10'000'000.0f` is legal
  C++14. A scanner that opens a literal at its first apostrophe discards the
  rest of the line, brackets included; that alone gave 28 false complaints over
  439 healthy files. An apostrophe inside a numeric literal -- a token that
  began with a digit -- is a separator;
- **a quote or bracket written as a character literal**, such as `TEXT('"')` or
  `TEXT('(')`, is one character and must not open a string or count as a bracket.

BEFORE A REBASE, a conflict can be predicted without starting one:
`git merge-tree --write-tree <development> <branch>` merges in memory, names the
conflicting files and writes a tree whose conflicted blobs can be read with
`git show <tree>:<path>` -- and checked with this tool through a file.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from dataclasses import dataclass, field

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / "game" / "Source"
SUFFIXES = {".h", ".cpp", ".inl"}
MARKERS = ("<<<<<<< ", ">>>>>>> ")


@dataclass
class Report:
    """What one file's text was found to hold."""
    complaints: list[str] = field(default_factory=list)
    raw_opens: int = 0
    raw_closes: int = 0


def _in_number(text: str, index: int) -> bool:
    """Whether the apostrophe at `index` sits inside a numeric literal.

    Walks back over the characters a number can hold. The apostrophe is a digit
    separator when that run began with a digit and a digit or hex digit follows.
    """
    if index + 1 >= len(text) or not text[index + 1].isalnum():
        return False
    start = index
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] in "'."):
        start -= 1
    return start < index and text[start].isdigit()


def check_text(text: str) -> Report:
    """Scan one file's text and return what it holds."""
    report = Report(raw_opens=text.count("/*"), raw_closes=text.count("*/"))
    complaints = report.complaints

    for number, line in enumerate(text.splitlines(), start=1):
        bare = line.rstrip("\r")
        if bare.startswith(MARKERS) or bare == "=======":
            complaints.append(f"line {number}: a conflict marker, {bare[:20]!r}")
            break

    braces = parens = 0
    line = 1
    index, length = 0, len(text)
    while index < length:
        char = text[index]
        if char == "\n":
            line += 1
            index += 1
        elif text.startswith("//", index):
            end = text.find("\n", index)
            index = length if end < 0 else end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                complaints.append(f"line {line}: a block comment opened and "
                                  f"never closed")
                break
            line += text.count("\n", index, end)
            index = end + 2
        elif text.startswith("*/", index):
            complaints.append(f"line {line}: a '*/' outside any comment, so a "
                              f"comment's opening '/*' is missing above it")
            index += 2
        elif char == "R" and text.startswith('"', index + 1):
            opening = text.find("(", index + 2)
            delimiter = text[index + 2:opening] if opening >= 0 else ""
            closing = text.find(")" + delimiter + '"', opening + 1)
            if opening < 0 or closing < 0:
                complaints.append(f"line {line}: a raw string never closed")
                break
            line += text.count("\n", index, closing)
            index = closing + len(delimiter) + 2
        elif char == '"' or (char == "'" and not _in_number(text, index)):
            end = index + 1
            while end < length and text[end] != char and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            if end >= length or text[end] != char:
                kind = "string" if char == '"' else "character"
                complaints.append(f"line {line}: a {kind} literal left open at "
                                  f"the end of its line")
                index = end
            else:
                index = end + 1
        else:
            braces += (char == "{") - (char == "}")
            parens += (char == "(") - (char == ")")
            index += 1

    if braces:
        complaints.append(f"{abs(braces)} more {'{' if braces > 0 else '}'} "
                          f"than its partner, outside comments and literals")
    if parens:
        complaints.append(f"{abs(parens)} more {'(' if parens > 0 else ')'} "
                          f"than its partner, outside comments and literals")
    return report


def check_file(path: pathlib.Path) -> Report:
    return check_text(path.read_text(encoding="utf-8", errors="replace"))


def changed_files() -> list[pathlib.Path]:
    """The C++ files git reports as changed against HEAD, staged or not."""
    names = subprocess.run(
        ["git", "diff", "--name-only", "HEAD"], cwd=REPO_ROOT,
        capture_output=True, text=True, check=True).stdout.split()
    return [REPO_ROOT / name for name in names
            if pathlib.Path(name).suffix in SUFFIXES
            and (REPO_ROOT / name).is_file()]


def control_files() -> list[pathlib.Path]:
    return sorted(path for path in SOURCE.rglob("*")
                  if path.suffix in SUFFIXES and path.is_file())


def main(argv: list[str] | None = None) -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("paths", nargs="*", type=pathlib.Path, default=[])
    group.add_argument("--changed", action="store_true",
                       help="the C++ files git reports as changed against HEAD")
    group.add_argument("--control", action="store_true",
                       help="every .h, .cpp and .inl under game/Source")
    args = parser.parse_args(argv)

    if args.control:
        paths = control_files()
    elif args.changed:
        paths = changed_files()
    else:
        paths = args.paths

    if not paths:
        print("read no files, so nothing was checked")
        return 2

    faulty = 0
    for path in paths:
        report = check_file(path)
        if report.complaints:
            faulty += 1
            print(f"{path}: raw '/*' {report.raw_opens} against '*/' "
                  f"{report.raw_closes}")
            for complaint in report.complaints:
                print(f"  {complaint}")
    print(f"{len(paths)} files read, {faulty} with complaints")
    return 1 if faulty else 0


if __name__ == "__main__":
    sys.exit(main())
