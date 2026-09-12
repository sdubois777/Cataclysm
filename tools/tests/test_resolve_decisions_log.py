"""`tools/resolve_decisions_log.py` puts the two entries in the order asked for.

WHAT MAKES THESE WORTH HAVING RATHER THAN A SMOKE TEST. The module exists to stop
one specific defect: an entry boundary in `docs/DECISIONS.md` left without its
rule and blank line. So the central test here does not re-implement that rule --
it imports `boundaries_missing_the_separator` out of
`test_decisions_entries_are_separated.py` and asserts the resolver's own output
satisfies the real check. A test that checked its own idea of the rule could pass
while the suite went red.

EVERY LOG HERE IS BUILT AS BYTES WITH EXPLICIT CRLF. The real file is CRLF, and a
fixture written with Python's text mode would be LF on some machines and CRLF on
others, so these tests would pass or fail by platform rather than by behaviour.
"""

import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import resolve_decisions_log as resolver  # noqa: E402
from test_decisions_entries_are_separated import (  # noqa: E402
    boundaries_missing_the_separator,
)

CRLF = b"\r\n"

PREAMBLE = [
    b"# Design decisions",
    b"",
    b"Decisions made outside the Google Drive documents, newest first.",
    b"",
]

MINE = [b"## 2026-09-12 - A blow records whether the target is staggered",
        b"",
        b"**Affects:** the stat pipeline. Applied.",
        b""]
THEIRS = [b"## 2026-09-12 - A skill can be locked, and the basic attack is exempt",
          b"",
          b"**Affects:** the skill slots. Applied.",
          b""]
OLDER = [b"## 2026-09-11 - An attacker can read how far away its target is",
         b"",
         b"**Affects:** the stat pipeline. Applied.",
         b""]
OLDEST = [b"## 2026-09-10 - Void Splinter takes a share of current health",
          b"",
          b"**Affects:** the skill templates. Applied.",
          b""]

RULE = [b"", b"---", b""]


def log(*entries: list[bytes]) -> bytes:
    """A clean log: the preamble, then entries separated by a rule."""
    lines = list(PREAMBLE)
    for index, entry in enumerate(entries):
        if index:
            lines += RULE
        lines += [line for line in entry if line != b"" or True]
        while lines and lines[-1] == b"":
            lines.pop()
    return CRLF.join(lines) + CRLF


def conflicted(above: list[list[bytes]], below: list[list[bytes]],
               *after: list[bytes]) -> bytes:
    """A log with one conflict hunk at the top.

    `above` is the `<<<<<<<` side, which during a REBASE is the upstream's, and
    `below` the `>>>>>>>` side. The module must not care which is which.
    """
    def joined(sides: list[list[bytes]]) -> list[bytes]:
        out: list[bytes] = []
        for index, entry in enumerate(sides):
            if index:
                out += RULE
            out += entry
        while out and out[-1] == b"":
            out.pop()
        return out

    lines = list(PREAMBLE)
    lines += [b"<<<<<<< HEAD"] + joined(above)
    lines += [b"======="] + joined(below)
    lines += [b">>>>>>> 0123456 (a commit being replayed)"]
    lines += RULE
    for index, entry in enumerate(after):
        if index:
            lines += RULE
        lines += entry
    while lines and lines[-1] == b"":
        lines.pop()
    return CRLF.join(lines) + CRLF


def headings(data: bytes) -> list[str]:
    return [line.decode("utf-8") for line in data.split(CRLF)
            if resolver.HEADING.match(line)]


def separator_faults(data: bytes) -> list[tuple[int, str]]:
    """The REAL check, imported rather than re-implemented. See the docstring."""
    text = data.decode("utf-8").replace("\r\n", "\n")
    lines = text.split("\n")
    entries = [(index, line) for index, line in enumerate(lines)
               if resolver.HEADING.match(line.encode("utf-8"))]
    return boundaries_missing_the_separator(lines, entries)


# --- the order, which is the whole interface --------------------------------

def test_my_entry_goes_first_when_it_is_on_the_rebase_side():
    """A REBASE puts the upstream behind `<<<<<<<` and mine behind `>>>>>>>`."""
    data = conflicted([THEIRS], [MINE], OLDER)
    out = resolver.resolve(data, MINE[0], THEIRS[0])
    assert headings(out) == [MINE[0].decode(), THEIRS[0].decode(),
                             OLDER[0].decode()]


def test_my_entry_goes_first_when_it_is_on_the_merge_side():
    """A MERGE puts them the other way round, and the answer must not change.

    THIS IS THE TEST THE ORDER-TAKING INTERFACE EXISTS FOR. A resolver written
    against "ours" and "theirs" passes the case above and fails this one.
    """
    data = conflicted([MINE], [THEIRS], OLDER)
    out = resolver.resolve(data, MINE[0], THEIRS[0])
    assert headings(out) == [MINE[0].decode(), THEIRS[0].decode(),
                             OLDER[0].decode()]


def test_the_other_entry_can_be_asked_for_first():
    """The argument decides, not the marker. Reversing it reverses the output."""
    data = conflicted([THEIRS], [MINE], OLDER)
    out = resolver.resolve(data, THEIRS[0], MINE[0])
    assert headings(out) == [THEIRS[0].decode(), MINE[0].decode(),
                             OLDER[0].decode()]


def test_a_side_holding_two_entries_keeps_the_rule_between_them():
    """Trimming must come off the END only.

    A side can hold two entries when one change merged while another was in
    flight. The rule BETWEEN them is already correct and must survive; trimming
    from anywhere else would join them into one entry, which is the exact defect
    this module exists to prevent.
    """
    data = conflicted([THEIRS, OLDER], [MINE], OLDEST)
    out = resolver.resolve(data, MINE[0], THEIRS[0])
    assert headings(out) == [MINE[0].decode(), THEIRS[0].decode(),
                             OLDER[0].decode(), OLDEST[0].decode()]
    assert separator_faults(out) == []


# --- the defect this exists to stop -----------------------------------------

def test_the_resolved_file_passes_the_real_separator_check():
    data = conflicted([THEIRS], [MINE], OLDER, OLDEST)
    out = resolver.resolve(data, MINE[0], THEIRS[0])
    assert separator_faults(out) == [], (
        "the resolver left an entry boundary without its rule and blank line, "
        "which is the defect it exists to prevent")


def test_inserting_gives_the_old_first_entry_the_rule_it_now_needs():
    """Rule 2: the first entry is exempt, so the OLD first entry stops being.

    An insert that only adds the new entry leaves the previous one failing a
    check it passed a moment earlier, and nothing about the edit hints at it.
    """
    before = log(THEIRS, OLDER)
    assert separator_faults(before) == []
    out = resolver.insert_first(before, CRLF.join(MINE))
    assert headings(out) == [MINE[0].decode(), THEIRS[0].decode(),
                             OLDER[0].decode()]
    assert separator_faults(out) == []


def test_both_operations_keep_every_line_ending():
    """Rule 3. A bare line feed anywhere makes the whole file look changed."""
    conflict = conflicted([THEIRS], [MINE], OLDER)
    resolved = resolver.resolve(conflict, MINE[0], THEIRS[0])
    inserted = resolver.insert_first(log(THEIRS, OLDER), CRLF.join(MINE))
    for name, out in (("resolve", resolved), ("insert_first", inserted)):
        bare = out.count(b"\n") - out.count(CRLF)
        assert bare == 0, f"{name} wrote {bare} bare line feed(s)"


def test_no_conflict_marker_survives_a_resolution():
    out = resolver.resolve(conflicted([THEIRS], [MINE], OLDER), MINE[0], THEIRS[0])
    for marker in (b"<<<<<<< ", b"=======", b">>>>>>> "):
        assert marker not in out, f"{marker!r} left in the resolved file"


# --- refusing rather than guessing ------------------------------------------

def test_it_refuses_text_that_is_not_crlf():
    """The shape a caller gets from `git show`, which is LF-normalised."""
    data = conflicted([THEIRS], [MINE], OLDER).replace(CRLF, b"\n")
    with pytest.raises(resolver.RefusedError, match="CRLF"):
        resolver.resolve(data, MINE[0], THEIRS[0])


def test_it_refuses_two_conflict_hunks():
    one = conflicted([THEIRS], [MINE], OLDER)
    two = one + CRLF.join([b"<<<<<<< HEAD", b"x", b"=======", b"y",
                           b">>>>>>> 0123456"]) + CRLF
    with pytest.raises(resolver.RefusedError, match="one conflict hunk"):
        resolver.resolve(two, MINE[0], THEIRS[0])


def test_it_refuses_when_both_headings_are_on_one_side():
    data = conflicted([THEIRS, MINE], [OLDER], OLDEST)
    with pytest.raises(resolver.RefusedError, match="one on each side"):
        resolver.resolve(data, MINE[0], THEIRS[0])


def test_it_refuses_when_no_entry_follows_the_hunk():
    lines = list(PREAMBLE) + [b"<<<<<<< HEAD"] + THEIRS + [b"======="] + MINE \
        + [b">>>>>>> 0123456"]
    with pytest.raises(resolver.RefusedError, match="not a dated entry heading"):
        resolver.resolve(CRLF.join(lines) + CRLF, MINE[0], THEIRS[0])


def test_insert_refuses_an_entry_that_is_already_there():
    before = log(MINE, THEIRS)
    with pytest.raises(resolver.RefusedError, match="already in the log"):
        resolver.insert_first(before, CRLF.join(MINE))


def test_insert_refuses_an_entry_that_is_not_a_dated_heading():
    with pytest.raises(resolver.RefusedError, match="not a dated heading"):
        resolver.insert_first(log(THEIRS, OLDER), b"Some prose" + CRLF)


def test_insert_refuses_an_unexpected_preamble():
    before = log(THEIRS, OLDER).replace(b"# Design decisions", b"# Notes", 1)
    with pytest.raises(resolver.RefusedError, match="preamble"):
        resolver.insert_first(before, CRLF.join(MINE))


def test_conflict_headings_reports_both_sides_in_marker_order():
    data = conflicted([THEIRS], [MINE], OLDER)
    assert resolver.conflict_headings(data) == (THEIRS[0].decode(),
                                                MINE[0].decode())
