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
import subprocess
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


# --- the refusal has to reach the caller ------------------------------------

EM_DASH_HEADING = "## 2026-09-12 — an entry whose heading holds an em dash"


def test_a_refusal_naming_an_em_dash_heading_reaches_a_caller(tmp_path):
    """Run as a subprocess, because only that exercises the stream encoding.

    WHY THIS TEST EXISTS. Headings in this log contain an EM DASH, and a child
    Python on Windows writes its streams as cp1252, which encodes that character
    as one byte that is not valid UTF-8. A caller capturing with
    `encoding="utf-8"` then dies inside subprocess's reader thread and receives
    **None** for that stream, with the exit code unchanged -- so checking the
    exit code before reading does not save it.

    AND THE FIRST FIX COVERED STDOUT ALONE, WHICH LEFT THIS PATH BROKEN. Every
    refusal goes to stderr and quotes both headings, so a refusing run gave the
    caller no reason at all while a succeeding run captured cleanly. The half
    that was lost is the half you read when something has gone wrong. Found by
    another session reading the refusal messages for an unrelated purpose.
    """
    mine = [EM_DASH_HEADING.encode("utf-8"), b"", b"**Affects:** nothing.", b""]
    log_path = tmp_path / "DECISIONS.md"
    log_path.write_bytes(conflicted([THEIRS], [mine], OLDER))

    finished = subprocess.run(
        [sys.executable, str(pathlib.Path(resolver.__file__)),
         "--path", str(log_path), "resolve", "--first", "matches-neither-heading"],
        capture_output=True, text=True, encoding="utf-8",
    )

    assert finished.stderr is not None, (
        "stderr came back as None, which means it could not be decoded as UTF-8. "
        "The caller gets no reason for the refusal at all.")
    assert finished.stdout is not None, "stdout came back as None"
    assert "REFUSED" in finished.stderr, finished.stderr
    assert EM_DASH_HEADING in finished.stderr, (
        "the refusal did not carry the heading it is about")
    assert finished.returncode != 0, "a refusal must not exit 0"


def test_a_refusal_survives_a_caller_that_decodes_the_bytes_itself(tmp_path):
    """The other capture mode, which fails differently and needs its own case.

    THE SYMPTOM DEPENDS ON HOW THE CALLER CAPTURES, and the two look nothing
    alike. Measured with the stderr stream left at the console code page:

      text=True, encoding="utf-8"   the decode happens inside subprocess's
                                    reader thread, which dies, and the caller
                                    receives stderr=None -- silence
      capture_output=True, bytes    the bytes arrive and the CALLER'S OWN
                                    .decode("utf-8") raises UnicodeDecodeError

    The test above covers the first. This covers the second, which is the one
    where a caller at least gets an exception naming the encoding.

    **The return code is 1 in both**, so a caller that checks it before reading
    the message is protected by neither.
    """
    mine = [EM_DASH_HEADING.encode("utf-8"), b"", b"**Affects:** nothing.", b""]
    log_path = tmp_path / "DECISIONS.md"
    log_path.write_bytes(conflicted([THEIRS], [mine], OLDER))

    finished = subprocess.run(
        [sys.executable, str(pathlib.Path(resolver.__file__)),
         "--path", str(log_path), "resolve", "--first", "matches-neither-heading"],
        capture_output=True,          # raw bytes: the decode is ours
    )

    assert finished.returncode != 0, "a refusal must not exit 0"
    try:
        message = finished.stderr.decode("utf-8")
    except UnicodeDecodeError as bad:
        raise AssertionError(
            f"the refusal was not written as UTF-8, so a caller decoding it "
            f"raises instead of reading the reason: {bad}") from bad
    assert "REFUSED" in message, message
    assert EM_DASH_HEADING in message


def test_a_successful_run_also_reaches_a_caller(tmp_path):
    """The path the first fix did cover, kept so a regression names which half."""
    mine = [EM_DASH_HEADING.encode("utf-8"), b"", b"**Affects:** nothing.", b""]
    log_path = tmp_path / "DECISIONS.md"
    log_path.write_bytes(conflicted([THEIRS], [mine], OLDER))

    finished = subprocess.run(
        [sys.executable, str(pathlib.Path(resolver.__file__)),
         "--path", str(log_path), "resolve", "--first", "em dash"],
        capture_output=True, text=True, encoding="utf-8",
    )

    assert finished.stdout is not None, "stdout came back as None"
    assert finished.returncode == 0, finished.stderr
    assert EM_DASH_HEADING in finished.stdout
    assert separator_faults(log_path.read_bytes()) == []


# ---------------------------------------------------------------------------
# `--first` names the entry that goes on top, whichever session wrote it.
# Issues #1964 and #1990.
# ---------------------------------------------------------------------------

def run_resolve(log_path: pathlib.Path, flag: str, word: str):
    """The command line, as a session runs it."""
    return subprocess.run(
        [sys.executable, str(pathlib.Path(resolver.__file__)),
         "--path", str(log_path), "resolve", flag, word],
        capture_output=True, text=True, encoding="utf-8",
    )


def headings_in_order(data: bytes) -> list[bytes]:
    return [line for line in data.split(CRLF) if line.startswith(b"## ")]


def test_first_can_put_the_upstream_entry_on_top(tmp_path):
    """The case the old flag's help got wrong, at the command line.

    THE UPSTREAM ENTRY IS THE NEWER ONE HERE, as it is whenever the other
    session's change merged after this one's entry was written, so the word
    comes from ITS heading and it is written first. The function below the
    command line was already pinned for this by
    `test_the_other_entry_can_be_asked_for_first`; the flag that reaches it
    was not, and it told the caller to name their own entry.

    BOTH DIRECTIONS FROM ONE STARTING LOG, so a flag that always wrote the
    `<<<<<<<` side first, or always the `>>>>>>>` side, fails one of the two.
    """
    for word, expected in ((b"locked", [THEIRS[0], MINE[0], OLDER[0]]),
                           (b"staggered", [MINE[0], THEIRS[0], OLDER[0]])):
        log_path = tmp_path / f"{word.decode()}.md"
        log_path.write_bytes(conflicted([THEIRS], [MINE], OLDER))

        finished = run_resolve(log_path, "--first", word.decode())

        assert finished.returncode == 0, finished.stderr
        written = log_path.read_bytes()
        assert headings_in_order(written) == expected, (
            f"--first {word.decode()} wrote {headings_in_order(written)}")
        assert separator_faults(written) == []


def test_the_old_mine_flag_is_still_accepted_and_means_first(tmp_path):
    """`--mine` stays so older notes keep working, and it means what
    `--first` means: the named entry is written on top, whoever wrote it."""
    log_path = tmp_path / "DECISIONS.md"
    log_path.write_bytes(conflicted([THEIRS], [MINE], OLDER))

    finished = run_resolve(log_path, "--mine", "locked")

    assert finished.returncode == 0, finished.stderr
    assert headings_in_order(log_path.read_bytes()) == [
        THEIRS[0], MINE[0], OLDER[0]]


def test_the_help_no_longer_says_the_word_is_from_your_heading():
    """The sentence #1964 and #1990 found misleading, and the flag's new name."""
    finished = subprocess.run(
        [sys.executable, str(pathlib.Path(resolver.__file__)), "resolve", "--help"],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert finished.returncode == 0, finished.stderr
    assert "--first" in finished.stdout
    assert "YOUR heading" not in finished.stdout
    assert "NEWER" in finished.stdout


def test_insert_writes_one_rule_when_the_entry_file_ends_with_one():
    """An entry file ending with its own `---` used to give the log two rules in
    a row, because insert_first writes the rule below the entry as well."""
    ending_with_a_rule = CRLF.join(MINE + [b"", b"---", b""])
    out = resolver.insert_first(log(THEIRS, OLDER), ending_with_a_rule)
    assert b"---\r\n\r\n---" not in out
    assert out == resolver.insert_first(log(THEIRS, OLDER), CRLF.join(MINE))
