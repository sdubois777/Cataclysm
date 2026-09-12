"""Resolve a conflict at the top of docs/DECISIONS.md, or insert a new first entry.

    python tools/resolve_decisions_log.py resolve --mine <a word in YOUR heading>
    python tools/resolve_decisions_log.py insert <a file holding the entry>
    python tools/resolve_decisions_log.py --help

WHY THIS EXISTS. `docs/DECISIONS.md` is the most conflict-prone file in this
repository and every resolution is by hand.
`tools/tests/test_decisions_entries_are_separated.py` measured the cost directly
out of git across five commits -- 60 bad entry boundaries, then 61, 62, 63, then
61 after a repair -- which is roughly **one new bad boundary per hand
resolution**. On 2026-09-12 three changes were in flight at once, each adding a
new FIRST entry, so two of them had to resolve this same conflict.

=== THE THREE RULES IT ENCODES, NONE OF WHICH IS OBVIOUS ===

**1. A REBASE INVERTS THE CONFLICT SIDES.** The `<<<<<<<` half is the UPSTREAM's
entry, not yours, because a rebase replays your commit onto the upstream. A
resolver taking "ours" and "theirs" is right for a merge and backwards for a
rebase. That happened, and put an older entry above a newer one until somebody
undid it by hand. **So `resolve` takes the ORDER to write the entries in**, chosen
from a word that appears in one heading and not the other -- an interface that
cannot be got wrong in either direction.

**2. A NEW FIRST ENTRY REMOVES THE OLD FIRST ENTRY'S EXEMPTION.** The separator
test requires `---` and a blank line above every dated entry EXCEPT the first. So
putting an entry on top means the entry that used to be first now needs a rule it
did not need a moment ago. **Appending and stopping breaks a check that was green
before you started**, and nothing about the conflict hints at it. Both functions
here write that rule.

**3. THE FILE IS CRLF THROUGHOUT.** Anything writing LF makes the whole file look
changed and buries the real entry in the diff. Everything here works in bytes.

=== ONE TRAP FOR CALLERS, WHICH THIS MODULE CANNOT FIX ===

**Do not feed it text from `git show`.** Git stores every file marked `text` with
LF, so `git show <rev>:docs/DECISIONS.md` returns LF whatever the working copy
holds -- splitting that on CRLF yields the whole file as one element. Measured
2026-09-12: the working copy held 40,134 CRLF pairs and `git show` returned zero.
Read the working tree.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

#: The log, relative to the repository root.
DEFAULT_PATH = pathlib.Path("docs/DECISIONS.md")

#: What every file in `docs/` uses. Kept as bytes so nothing can translate it.
CRLF = b"\r\n"

#: The rule and blank line that must sit above every dated entry but the first.
RULE = [b"", b"---", b""]

#: The three lines the log opens with, before its first dated entry.
PREAMBLE = [
    b"# Design decisions",
    b"",
    b"Decisions made outside the Google Drive documents, newest first.",
    b"",
]

#: A dated entry heading, which is what both operations arrange.
HEADING = re.compile(rb"^## 20\d\d-\d\d-\d\d ")


class RefusedError(Exception):
    """The file is not in a shape this module is willing to rewrite.

    RAISED RATHER THAN GUESSED, in every case. A resolver that does something
    plausible with an unexpected file is worse than one that stops: the result
    looks resolved and the damage is found later, in a 39,000-line file whose
    diff nobody reads in full.
    """


def _split(data: bytes) -> list[bytes]:
    """The file as CRLF-separated lines, refusing anything that is not CRLF."""
    if CRLF not in data:
        raise RefusedError("the log has no CRLF line endings at all. If this "
                           "text came from `git show`, read the working tree "
                           "instead: git stores it LF-normalised.")
    return data.split(CRLF)


def conflict_headings(data: bytes) -> tuple[str, str]:
    """The two headings in the conflict, as (upstream side, other side).

    THE NAMES SAY WHICH MARKER, NOT WHOSE WORK. During a rebase the `<<<<<<<`
    half is the upstream's and the `>>>>>>>` half is the commit being replayed;
    during a merge it is the other way round. Callers should decide by reading
    the headings, which is what `resolve` makes them do.
    """
    lines = _split(data)
    starts = [i for i, line in enumerate(lines) if line.startswith(b"<<<<<<< ")]
    mids = [i for i, line in enumerate(lines) if line == b"======="]
    ends = [i for i, line in enumerate(lines) if line.startswith(b">>>>>>> ")]
    if not (len(starts) == len(mids) == len(ends) == 1):
        raise RefusedError(
            f"expected exactly one conflict hunk, found {len(starts)} start, "
            f"{len(mids)} middle and {len(ends)} end markers")
    return (lines[starts[0] + 1].decode("utf-8"),
            lines[mids[0] + 1].decode("utf-8"))


def _trim_end(side: list[bytes]) -> list[bytes]:
    """Drop trailing blank and rule lines, so the rule is written exactly once.

    ONLY FROM THE END. A side holding two entries carries a rule BETWEEN them,
    and that one must survive: trimming from anywhere else would join two
    entries into one and produce exactly the defect this module exists to stop.
    """
    side = list(side)
    while side and side[-1] in (b"", b"---"):
        side.pop()
    return side


def resolve(data: bytes, first: bytes, second: bytes) -> bytes:
    """Write the conflict's two sides out in the ORDER given, newest first.

    `first` and `second` are matched against whichever side actually holds them,
    so which marker a heading sits behind does not matter. See rule 1 above.
    """
    lines = _split(data)
    starts = [i for i, line in enumerate(lines) if line.startswith(b"<<<<<<< ")]
    mids = [i for i, line in enumerate(lines) if line == b"======="]
    ends = [i for i, line in enumerate(lines) if line.startswith(b">>>>>>> ")]
    if not (len(starts) == len(mids) == len(ends) == 1):
        raise RefusedError(
            f"expected exactly one conflict hunk, found {len(starts)} start, "
            f"{len(mids)} middle and {len(ends)} end markers")

    start, mid, end = starts[0], mids[0], ends[0]
    above = list(lines[start + 1:mid])
    below = list(lines[mid + 1:end])

    def opens_with(side: list[bytes], heading: bytes) -> bool:
        return bool(side) and side[0].startswith(heading)

    if opens_with(above, first) and opens_with(below, second):
        ordered = [above, below]
    elif opens_with(below, first) and opens_with(above, second):
        ordered = [below, above]
    else:
        raise RefusedError(
            f"the two headings are not one on each side.\n"
            f"  <<<<<<< side starts: {above[:1]!r}\n"
            f"  >>>>>>> side starts: {below[:1]!r}\n"
            f"  asked for first:     {first!r}\n"
            f"  asked for second:    {second!r}")

    rest = list(lines[end + 1:])
    while rest and rest[0] == b"":
        rest.pop(0)
    if rest and rest[0] == b"---":
        rest.pop(0)
        while rest and rest[0] == b"":
            rest.pop(0)
    if not rest or not HEADING.match(rest[0]):
        raise RefusedError(
            f"after the hunk comes {rest[:1]!r}, not a dated entry heading")

    out = (lines[:start] + _trim_end(ordered[0]) + RULE
           + _trim_end(ordered[1]) + RULE + rest)
    return CRLF.join(out)


def insert_first(data: bytes, entry: bytes) -> bytes:
    """Put a new first entry at the top, giving the old first entry its rule.

    THE RULE IS THE WHOLE POINT OF USING THIS RATHER THAN AN EDITOR. See rule 2
    above: the entry that used to be first stops being exempt the moment this
    one goes in front of it.
    """
    lines = _split(data)
    if lines[:4] != PREAMBLE:
        raise RefusedError(f"the preamble is {lines[:4]!r}, not {PREAMBLE!r}")

    entry_lines = entry.replace(CRLF, b"\n").rstrip(b"\n").split(b"\n")
    if not HEADING.match(entry_lines[0]):
        raise RefusedError(
            f"the entry opens with {entry_lines[0]!r}, not a dated heading")
    if entry_lines[0] in lines:
        raise RefusedError(f"{entry_lines[0]!r} is already in the log")

    rest = lines[len(PREAMBLE):]
    while rest and rest[0] == b"":
        rest.pop(0)
    if not rest or not HEADING.match(rest[0]):
        raise RefusedError(
            f"after the preamble comes {rest[:1]!r}, not a dated entry heading")

    return CRLF.join(PREAMBLE + entry_lines + RULE + rest)


def _main(argv: list[str] | None = None) -> int:
    # WRITES UTF-8 WHATEVER THE CONSOLE CODE PAGE IS. Every heading printed here
    # contains an EM DASH, and a child Python on Windows writes cp1252, which
    # encodes it as one byte that is not valid UTF-8. A caller capturing with
    # `encoding="utf-8"` then fails inside subprocess's reader thread and sees
    # `AttributeError: 'NoneType' object has no attribute 'strip'` -- naming
    # nothing about the cause -- while the EXIT CODE IS STILL 0.
    #
    # BOTH STREAMS, AND STDERR IS THE ONE THAT MATTERS MORE. The first version of
    # this reconfigured stdout alone, which fixed the path being tested and left
    # the path that was not: every REFUSED message goes to stderr and quotes the
    # two headings. Measured -- with stdout alone reconfigured, a refusing run
    # gives the caller `stderr is None` and no reason at all. The message you
    # need when something has gone wrong is exactly the one that was lost.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--path", type=pathlib.Path, default=DEFAULT_PATH,
                        help="the log to rewrite (default docs/DECISIONS.md)")
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing")
    sub = parser.add_subparsers(dest="command", required=True)

    resolving = sub.add_parser(
        "resolve", help="resolve one conflict at the top of the log")
    resolving.add_argument(
        "--mine", required=True,
        help="a word appearing in YOUR heading and not in the other one. Chosen "
             "this way rather than by conflict side because a rebase inverts the "
             "sides; see rule 1 in this module's docstring.")

    inserting = sub.add_parser(
        "insert", help="insert a new first entry, with no conflict present")
    inserting.add_argument("entry", type=pathlib.Path,
                           help="a file holding the entry, opening with its heading")

    args = parser.parse_args(argv)
    data = args.path.read_bytes()

    if args.command == "resolve":
        upstream, other = conflict_headings(data)
        word = args.mine.lower()
        holding = [h for h in (upstream, other) if word in h.lower()]
        if len(holding) != 1:
            raise RefusedError(
                f"{args.mine!r} appears in {len(holding)} of the two headings, "
                f"not exactly one.\n  <<<<<<< side: {upstream}\n"
                f"  >>>>>>> side: {other}")
        mine = holding[0]
        theirs = other if mine == upstream else upstream
        print(f"  <<<<<<< side: {upstream}")
        print(f"  >>>>>>> side: {other}")
        print(f"  writing FIRST:  {mine}")
        print(f"  writing SECOND: {theirs}")
        out = resolve(data, mine.encode("utf-8"), theirs.encode("utf-8"))
    else:
        out = insert_first(data, args.entry.read_bytes())
        print(f"  inserted: {args.entry.read_bytes().split(CRLF)[0][:90]!r}")

    if args.check:
        print("  --check given: nothing written")
        return 0

    args.path.write_bytes(out)
    back = args.path.read_bytes()
    bare = back.count(b"\n") - back.count(CRLF)
    markers = sum(back.count(m) for m in (b"<<<<<<< ", b"=======", b">>>>>>> "))
    print(f"  written: {len(back):,} bytes, bare LF {bare}, markers left {markers}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(_main())
    except RefusedError as refusal:
        sys.exit(f"REFUSED: {refusal}")
