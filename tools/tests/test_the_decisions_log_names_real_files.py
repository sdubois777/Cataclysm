"""Every file path named in `docs/DECISIONS.md` still exists.

WHY THIS EXISTS. Each entry in the decisions log opens with an `**Affects:**`
line listing the files it changed. Nothing checked that those files are still
there, so a rename or a deletion left the lines pointing at nothing and no test
anywhere failed. `game/README.md` has had such a test for a while; the log the
project treats as the authority on why things are the way they are had none.

THE LOG IS THE HARDER ONE TO NOTICE ROT IN. Nobody reads an old entry until they
need it, and by then a wrong path reads as a change that was reverted rather than
as a file that moved. Issue #1689.

WHAT IT DOES NOT CHECK. Entries cite function and class names inside their
reasoning as well as in the `Affects:` line, and a rename leaves those pointing at
nothing too. Those are not mechanically checkable the way a path is, and this file
deliberately does not try.

MEASURED ON e194482, 2026-09-13: 351 `Affects:` lines, 343 of which wrap onto
following lines, naming 234 distinct paths. **Every one of them resolved**, so
this arrives with no allowance list — which is the state to keep it in.

THE COMMIT IS NAMED BECAUSE THOSE ARE CLAIMS ABOUT A TREE, and a tree moves.
A figure written into a test that no longer matches the file it describes is
exactly the rot this test exists to catch, so it should not be the first thing
to rot. The checks below read the file rather than these numbers; the numbers
are here to say what the file looked like when the parser was fitted to it.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: Where a path in the log is written relative to, when it is not written from the
#: repository root.
#:
#: FOUR ENTRIES DO THIS TODAY, naming things like `Character/CataclysmFoo.cpp`.
#: They are real files written short rather than mistakes, so this resolves them
#: rather than excusing them -- an allowance list would stop checking them, and
#: they are exactly as capable of going stale as any other path.
IMPLIED_PREFIX = "game/Source/Cataclysm/"

#: The top-level directories a path in this repository can start with. Anything
#: backticked that does not start with one of these and does not resolve under
#: IMPLIED_PREFIX is not a path -- it is a symbol, a column name or a quoted
#: phrase, and there are far more of those in an `Affects:` line than paths.
TOP_LEVEL = ("game/", "tools/", "sim/", "docs/", ".github/", ".claude/")

#: A floor, not a count.
#:
#: IT EXISTS SO A PARSER THAT MATCHES NOTHING FAILS LOUDLY. Every check below is
#: "each thing found must exist", which a parser finding nothing passes
#: perfectly. This is deliberately far below the real number so that adding or
#: removing entries never touches it; only a broken parser trips it.
FEWEST_PATHS_A_WORKING_PARSER_FINDS = 100


def affects_blocks(text: str) -> list[str]:
    """Every `Affects:` line, joined with the lines it wraps onto.

    THEY ALMOST ALWAYS WRAP. Of 351 such lines, 343 continue onto at least one
    more line, and a path can be split across the break -- so reading only the
    line that starts with the marker finds a minority of what is named.
    """
    lines = text.replace("\r\n", "\n").split("\n")
    blocks: list[str] = []

    for index, line in enumerate(lines):
        if not line.startswith("**Affects:**"):
            continue

        block = [line]
        following = index + 1
        while (
            following < len(lines)
            and lines[following].strip()
            and not lines[following].startswith("**")
            and not lines[following].startswith("#")
        ):
            block.append(lines[following])
            following += 1

        blocks.append(" ".join(block))

    return blocks


def named_paths(text: str) -> list[str]:
    """Everything backticked in an `Affects:` block that is a path."""
    inside_backticks = re.findall(r"`([^`]+)`", " ".join(affects_blocks(text)))

    paths = []
    for item in inside_backticks:
        if "/" not in item or item.startswith("http"):
            continue
        paths.append(item)

    return sorted(set(paths))


def resolves(path: str) -> bool:
    """True when this names something that is in the repository."""
    # A PATTERN RATHER THAN A PATH. One entry names every class tree at once.
    # Matching nothing is the failure; matching one thing is enough.
    if "*" in path or "?" in path:
        return any(REPO_ROOT.glob(path))

    if path.startswith(TOP_LEVEL):
        return (REPO_ROOT / path).exists()

    return (REPO_ROOT / IMPLIED_PREFIX / path).exists()


def test_the_parser_finds_the_affects_lines() -> None:
    """A parser that matched nothing would pass every other test in this file."""
    found = named_paths(DECISIONS.read_text(encoding="utf-8"))

    assert len(found) >= FEWEST_PATHS_A_WORKING_PARSER_FINDS, (
        f"Only {len(found)} paths were found in the Affects lines of "
        f"{DECISIONS.name}, which is fewer than a working parser has ever found. "
        "The format of those lines has probably changed; fix the parser rather "
        "than lowering this floor."
    )


def test_every_path_named_in_an_affects_line_exists() -> None:
    """A renamed or deleted file leaves the entries that cite it pointing at nothing."""
    missing = [
        path
        for path in named_paths(DECISIONS.read_text(encoding="utf-8"))
        if not resolves(path)
    ]

    assert not missing, (
        f"{len(missing)} path(s) named in {DECISIONS.name} do not exist:\n  "
        + "\n  ".join(missing)
        + "\n\nIf a file moved, correct the entries that name it. Do not delete "
        "the path from the entry -- the entry is a record of what that decision "
        "changed, and a decision about a file that no longer exists is still a "
        "decision that was made."
    )
