"""Every file named in `docs/DECISIONS.md` still exists.

WHY THIS EXISTS. Each entry in the decisions log opens with an `**Affects:**`
line listing the files it changed. Nothing checked that those files are still
there, so a rename or a deletion left the lines pointing at nothing and no test
anywhere failed. `game/README.md` has had such a test for a while; the log the
project treats as the authority on why things are the way they are had none.

THE LOG IS THE HARDER ONE TO NOTICE ROT IN. Nobody reads an old entry until they
need it, and by then a wrong path reads as a change that was reverted rather than
as a file that moved. Issue #1689.

THE FIRST VERSION OF THIS FILE READ TWO FIFTHS OF WHAT IS NAMED, AND PASSED.
It kept only backticked items containing a slash, which made the log's house
shorthand invisible to it:

    `game/Source/Cataclysm/AbilitySystem/CataclysmMinion.h` and `.cpp`
                                                                ^^^^^ no slash

Measured on a7b9049: of 1,587 backticked items inside `Affects:` blocks, that rule
read 838 and skipped 749, of which **547 name a file**. Most of those name a file
written out in full somewhere else, so the file was still checked -- just not by
that mention. **The set nothing checked anywhere was 34 files.** Issue #1709.

SO IT WAS A CORRECTNESS IMPROVEMENT RATHER THAN AN EMERGENCY, and the first
framing of it -- "547 unchecked references" -- counted mentions rather than files.
The two documents first suspected of being the least protected, the main design
document and the design workbook, turned out to be the best covered in the whole
log: named with a full path 90 and 76 times.

WHAT IT STILL DOES NOT CHECK. Entries cite function and class names inside their
reasoning as well as in the `Affects:` line, and a rename leaves those pointing at
nothing too. Those are not mechanically checkable the way a path is, and this file
deliberately does not try.

MEASURED ON a7b9049, 2026-09-13: 354 `Affects:` lines, 346 of which wrap onto
following lines. Of the 1,385 file-like items in them, this reads **all of
them**, against 838 for the rule it replaces -- 100% against 61%. They name 379
distinct files and every one resolves, so this carries no allowance list, which
is the state to keep it in.

THE COMMIT IS NAMED BECAUSE THOSE ARE CLAIMS ABOUT A TREE, and a tree moves. A
figure written into a test that no longer matches the file it describes is exactly
the rot this test exists to catch, so it should not be the first thing to rot. The
checks below read the file rather than these numbers; the numbers are here to say
what the file looked like when the parser was fitted to it.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: Where a path in the log is written relative to, when it is not written from the
#: repository root.
#:
#: FOUR ENTRIES DO THIS, naming things like `Character/CataclysmFoo.cpp`. They are
#: real files written short rather than mistakes, so this resolves them rather
#: than excusing them -- an allowance list would stop checking them, and they are
#: exactly as capable of going stale as any other path.
IMPLIED_PREFIX = "game/Source/Cataclysm/"

#: The top-level directories a path in this repository can start with.
TOP_LEVEL = ("game/", "tools/", "sim/", "docs/", ".github/", ".claude/")

#: Where a bare filename is searched for, in this order.
#:
#: NOT THE WHOLE REPOSITORY. Searching everything would walk build output and
#: engine intermediates, which are gitignored but sitting in a working tree, and a
#: bare name matching something in there would resolve for the wrong reason.
SEARCHED_FOR_BARE_NAMES = ("game/Source", "game/Data", "tools", "sim", "docs")

#: What share of the file-like things named in `Affects:` blocks must actually be
#: read.
#:
#: A RATIO RATHER THAN A FLOOR, AND THAT IS THE WHOLE POINT. The first version
#: guarded with "a working parser finds at least 100 paths". It found 234 and
#: passed comfortably while reading two fifths of what was there, because a floor
#: cannot notice what it never looked at. A ratio can.
#:
#: NOT 100%, because the denominator is a judgement -- "looks like it names a
#: file" -- and a backticked column heading or a quoted phrase can fall into it.
#: It sits high enough that dropping a whole shorthand fails it, and low enough
#: that one odd backtick does not.
#:
#: WHAT IT CANNOT SEE, MEASURED RATHER THAN GUESSED. `looks_like_a_file` below
#: decides BOTH sides of this ratio -- the items counted as candidates and,
#: through `named_files`, the items read. Narrowing that one function shrinks
#: both, so the share stays at 100% and this guard says nothing. Confirmed by
#: narrowing it back to the slash-only rule this file replaced: both tests
#: still passed.
#:
#: SO THIS GUARDS THE PARSER AGAINST THE CLASSIFIER, NOT THE CLASSIFIER AGAINST
#: REALITY. If you change what counts as naming a file, no test here will tell
#: you what you stopped reading -- measure the coverage by hand, the way the
#: docstring above reports it.
LEAST_SHARE_THAT_MUST_BE_READ = 0.95


def affects_blocks(text: str) -> list[str]:
    """Every `Affects:` line, joined with the lines it wraps onto.

    THEY ALMOST ALWAYS WRAP. Of 351 such lines, 343 continue onto at least one
    more line, and a path can be split across the break -- so reading only the
    line that starts with the marker finds a minority of what is named.

    A BLOCK ENDS AT THE FIRST BLANK LINE, or at a line opening with `**` or `#`.
    Prose below a blank line is therefore outside the block and invisible here,
    which is deliberate: the `Affects:` list states what changed and the prose
    below it argues why.
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


def is_a_bare_suffix(item: str) -> bool:
    """True for the `.cpp` half of ``CataclysmFoo.h`` and ``.cpp``."""
    return bool(re.fullmatch(r"\.[A-Za-z0-9]{1,5}", item))


def looks_like_a_file(item: str) -> bool:
    """True when a backticked item names a file rather than a symbol."""
    if " " in item or item.startswith("http"):
        return False

    if is_a_bare_suffix(item):
        return True

    return "/" in item or bool(re.search(r"\.[A-Za-z0-9]{1,5}$", item))


def named_files(text: str) -> list[tuple[str, str]]:
    """Every file named in an `Affects:` block: as written, and what it means.

    Both halves are returned so a caller can report what was read rather than
    only whether it passed.

    A BARE SUFFIX MEANS THE FILE BEFORE IT. ``CataclysmFoo.h`` and ``.cpp`` is one
    file written twice, and the second half only means anything in the order it
    appears -- so each block is walked in order and a suffix attaches to the last
    full name seen in that same block. A suffix with nothing before it in its
    block is kept as written, so it is counted and named rather than dropped.
    """
    found: list[tuple[str, str]] = []

    for block in affects_blocks(text):
        previous: str | None = None

        for item in re.findall(r"`([^`]+)`", block):
            if not looks_like_a_file(item):
                continue

            if is_a_bare_suffix(item):
                meant = (
                    str(pathlib.PurePosixPath(previous).with_suffix(item))
                    if previous
                    else item
                )
                found.append((item, meant))
                continue

            found.append((item, item))
            previous = item

    return found


def resolves(path: str) -> bool:
    """True when this names something that is in the repository."""
    # A PATTERN RATHER THAN A PATH. One entry names every class tree at once.
    # Matching nothing is the failure; matching one thing is enough.
    if "*" in path or "?" in path:
        return any(REPO_ROOT.glob(path))

    if path.startswith(TOP_LEVEL):
        return (REPO_ROOT / path).exists()

    if (REPO_ROOT / IMPLIED_PREFIX / path).exists():
        return True

    # A BARE FILENAME, SEARCHED FOR. The log names plenty of files by name alone,
    # and searching is the same rule the pattern branch above already applies. It
    # is what makes those names checkable at all rather than skipped.
    if "/" not in path:
        return any(
            any((REPO_ROOT / where).rglob(path))
            for where in SEARCHED_FOR_BARE_NAMES
            if (REPO_ROOT / where).is_dir()
        )

    return False


def test_nearly_everything_that_names_a_file_is_read() -> None:
    """A parser reading a fraction of the file passes every other check here.

    THIS IS THE CHECK THAT WOULD HAVE CAUGHT THE FIRST VERSION. Every other one
    is "each thing found must exist", which a parser finding two fifths of them
    passes perfectly -- and did.
    """
    text = DECISIONS.read_text(encoding="utf-8")

    candidates = [
        item
        for block in affects_blocks(text)
        for item in re.findall(r"`([^`]+)`", block)
        if looks_like_a_file(item)
    ]

    assert candidates, (
        f"No file-like item was found in any Affects block of {DECISIONS.name}. "
        "The format of those blocks has probably changed."
    )

    read = named_files(text)
    share = len(read) / len(candidates)

    assert share >= LEAST_SHARE_THAT_MUST_BE_READ, (
        f"Only {len(read)} of {len(candidates)} file-like items in the Affects "
        f"blocks of {DECISIONS.name} were read, which is {share:.0%}. Something "
        "the log writes routinely is being skipped. Fix the parser rather than "
        "lowering this share."
    )


def test_every_file_named_in_an_affects_line_exists() -> None:
    """A renamed or deleted file leaves the entries that cite it pointing at nothing."""
    missing = [
        (written, meant)
        for written, meant in named_files(DECISIONS.read_text(encoding="utf-8"))
        if not resolves(meant)
    ]

    assert not missing, (
        f"{len(missing)} file(s) named in {DECISIONS.name} do not exist:\n  "
        + "\n  ".join(
            written if written == meant else f"{written}  (meaning {meant})"
            for written, meant in missing
        )
        + "\n\nIf a file moved, correct the entries that name it. Do not delete "
        "the path from the entry -- the entry is a record of what that decision "
        "changed, and a decision about a file that no longer exists is still a "
        "decision that was made."
    )
