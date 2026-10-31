"""No text file under `docs/` holds a control character other than tab, line feed and carriage return.

WHY THIS EXISTS. On 2026-10-30 `docs/DECISIONS.md` was found holding a backspace (0x08) where an entry quoted the stun
test's pattern `\\bstun`. The `\\b` had been written through a shell here-document, which turned it into the control
character, and the entry has read "its `<backspace>stun` pattern" since commit `6a9a43f1b` (the Chronomancer's Time-Lock
10-piece). Nothing noticed: a backspace is invisible in most viewers, and no check read the file for it. Every other
text file under `docs/` was clean when this check was added.

WHAT IT READS. Every `.md`, `.json` and `.txt` file under `docs/`, as bytes. The design workbook (`.xlsx`) is a zip
archive and is binary, so it is not read. C0 control characters and DEL are refused, except tab, line feed and
carriage return: the files are CRLF or LF, and tabs are legitimate.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO_ROOT / "docs"
TEXT_SUFFIXES = (".md", ".json", ".txt")

#: Every C0 control character and DEL, except tab (0x09), line feed (0x0A) and carriage return (0x0D).
CONTROL = re.compile(rb"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def text_files() -> list[pathlib.Path]:
    return sorted(p for p in DOCS.rglob("*") if p.is_file() and p.suffix.lower() in TEXT_SUFFIXES)


def control_characters(data: bytes) -> list[tuple[int, int, str]]:
    """(line number, character code, the text before it on that line) for each control character found."""
    found = []
    for number, line in enumerate(data.split(b"\n"), 1):
        for match in CONTROL.finditer(line):
            before = line[max(0, match.start() - 50):match.start()].decode("utf-8", errors="replace")
            found.append((number, match.group()[0], before))
    return found


def test_the_check_reads_the_design_documents():
    """THE POSITIVE CONTROL: a check that reads no files reports a clean tree."""
    names = {p.name for p in text_files()}
    assert "DECISIONS.md" in names and "Cataclysm_GDD_v2.md" in names, sorted(names)


def test_the_pattern_sees_a_backspace_and_passes_tab_and_both_line_endings():
    assert control_characters(b"its `\x08stun` pattern") == [(1, 8, "its `")]
    assert control_characters(b"a\tb\r\nc\n") == []


def test_no_design_document_holds_a_control_character():
    faults = []
    for path in text_files():
        for number, code, before in control_characters(path.read_bytes()):
            faults.append(f"{path.relative_to(REPO_ROOT).as_posix()} line {number}: 0x{code:02x} after {before!r}")
    assert not faults, (
        "control characters in the design documents. A backslash escape written through a shell here-document "
        "(\\b, \\t and the like) is the usual cause; write the text with an editor or a script file instead:\n"
        + "\n".join(faults))
