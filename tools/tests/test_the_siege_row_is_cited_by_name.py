"""The Siege design text is cited by its row, never by a line number.

WHAT THIS GUARDS. The `Siege` sub-type's rules live in one row of the sub-type
table in `docs/Cataclysm_GDD_v2.md`, and eight places in this repository quoted
that row while pointing at a line number instead of at the row. Every one of the
eight was wrong by the time issue #1355 was filed. A reader who followed
"line 3744" landed in the dungeon floor generation prose, found nothing about a
Siege, and had to search the document by hand.

WHY A LINE NUMBER CANNOT WORK HERE. `docs/` is edited directly and grows, and
nothing renumbers a citation when a paragraph is inserted above it. The number
went stale twice inside a single day: #1355 was filed on 2026-09-06 saying the
row had moved from line 3744 to line 3801, and by the time the fix was written
that afternoon further edits had carried it to 3839. A row name does not move.

WHY A TEST AND NOT A CAREFUL READ. The eight sites are spread across a C++
header, the simulation's config, two test files and the decisions log, and the
issue that found them listed five. It missed all three in `docs/DECISIONS.md` --
two using the same stale number and a third using a different stale number,
3732, for the same sentence. A sweep is the only way to know.

WHAT THIS DOES NOT GUARD. Line-number citations of any OTHER passage. There are
sixteen more in the repository, in nine files, and every one checked was also
stale; issue #1366 has the list and the plan. This test is deliberately scoped to
the Siege so that it can pass on the tree as it stands rather than being written
and then disabled. Widening it is the last commit of #1366: drop SIEGE_WINDOW
and the filter in `citations_in`, and it becomes general.

`tools/tests/test_surge_port.py::TestWhatASiegeCostsItsHost` is where the
convention this holds in place is explained at length.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

#: Trees swept, plus the files outside them that cite the design.
SEARCHED_TREES = ("game", "sim", "tools", "docs")
SEARCHED_FILES = ("CLAUDE.md",)

SUFFIXES = {".py", ".h", ".cpp", ".md"}

#: Build output, engine intermediates and content. None is source.
SKIPPED_DIRECTORIES = {"Binaries", "Intermediate", "Saved", "DerivedDataCache",
                       "Content", "__pycache__", ".git"}

#: A line's indent and whatever comment marker it opens with: Python `#` and
#: `#:`, C++ `//`, a Doxygen block's ` * `.
LEADER = re.compile(r"^[ \t]*(?:#+:?|//+|\*+)?[ \t]*")

DESIGN_DOCUMENT = "docs/Cataclysm_GDD_v2.md"

#: A citation of the design document that names a line number, in either order
#: and with or without the backticks the project usually puts round a path.
#: Both orders are covered because both are in use: "`X.md` line 3744" in most
#: places, and "line 2627 of `X.md`" in `CataclysmEquipmentTests.cpp`.
CITATION = re.compile(
    r"docs/Cataclysm_GDD_v2\.md`?[,:]?\s+(?:at\s+|around\s+)?lines?\s+\d+"
    r"|lines?\s+\d+\s+(?:of|in)\s+`?docs/Cataclysm_GDD_v2\.md",
    re.IGNORECASE,
)

#: How far either side of a citation the word "Siege" may sit and still make
#: the citation one of the Siege's.
#:
#: MEASURED IN BOTH DIRECTIONS against `origin/development` at e058f75, and
#: not guessed. The floor is the furthest of the eight sites this test was
#: written for: the rules table in `docs/DECISIONS.md` names the Siege 598
#: characters before its citation, and at 300 this guard caught seven of the
#: eight and reported the tree as clean. The ceiling is enormous -- not one
#: of the sixteen line-number citations of OTHER passages is even in a file
#: containing the word "Siege", and the nearest citation that is, further
#: down `docs/DECISIONS.md`, sits 88231 characters from one.
#:
#: IT IS A SCOPE AND NOT A TUNING KNOB. Raising it far enough to swallow
#: those other citations makes the guard below FAIL rather than pass, which
#: was confirmed by raising it to 300000 and watching it fail.
SIEGE_WINDOW = 700

#: The heading the row lives under, and the sentences the citations quote. If
#: the row is renamed or moved out of this table, the citations that replaced
#: the line numbers become dangling in their turn, and
#: `test_the_siege_row_is_where_the_citations_say_it_is` is what notices.
SUBTYPE_TABLE_HEADING = "## **Dungeon Sub-Types**"
SIEGE_ROW_OPENS = "| Siege |"
SIEGE_ROW_SENTENCES = (
    "Deals 1% damage to city defenses and population per day while active.",
    "Increases in power by 2.5 points per day.",
    "Max 1 per city.",
)


def flattened(text: str) -> tuple[str, list[int]]:
    """The file as one long line, plus the source line number of each character.

    Leading indent and comment markers are removed from every line and the lines
    are joined with single spaces. Every file here is hard-wrapped, so a citation
    written as "`docs/Cataclysm_GDD_v2.md`" on one line and "line 3744" on the
    next is invisible to a raw search -- which is how several of the eight sites
    this test was written for were wrapped.
    """
    pieces: list[str] = []
    line_of: list[int] = []
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = LEADER.sub("", line).rstrip()
        if pieces:
            pieces.append(" ")
            line_of.append(number)
        pieces.append(stripped)
        line_of.extend([number] * len(stripped))
    return "".join(pieces), line_of


def citations_in(text: str) -> list[tuple[int, str]]:
    """Every line-number citation in `text` that belongs to the Siege.

    Split out from the sweep so that both halves of the filter can be checked
    against text this file owns, rather than only against whatever the
    repository happens to contain today.
    """
    flat, line_of = flattened(text)
    found = []
    for match in CITATION.finditer(flat):
        window = flat[max(0, match.start() - SIEGE_WINDOW):
                      match.end() + SIEGE_WINDOW]
        if "siege" not in window.lower():
            continue
        found.append((line_of[match.start()], match.group(0)))
    return found


def searched_paths() -> list[pathlib.Path]:
    """Every file this test reads, this file itself excluded.

    EXCLUDING ITSELF MATTERS. The patterns above spell the offending citation
    out, so a sweep that included this file would report it and never pass.
    """
    here = pathlib.Path(__file__).resolve()
    found: list[pathlib.Path] = []

    for relative in SEARCHED_FILES:
        path = REPO_ROOT / relative
        if path.is_file():
            found.append(path)

    for tree in SEARCHED_TREES:
        for path in sorted((REPO_ROOT / tree).rglob("*")):
            if path.suffix.lower() not in SUFFIXES:
                continue
            if SKIPPED_DIRECTORIES & set(path.parts):
                continue
            if path.resolve() == here:
                continue
            found.append(path)
    return found


def siege_citations_by_line_number() -> list[tuple[str, int, str]]:
    """Every line-number citation of the Siege design text in the repository."""
    found = []
    for path in searched_paths():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line, cited in citations_in(text):
            found.append((path.relative_to(REPO_ROOT).as_posix(), line, cited))
    return found


def test_the_sweep_actually_reads_files() -> None:
    """Without this, a wrong path would make the guard below pass on nothing."""
    paths = searched_paths()
    assert len(paths) > 200, (
        f"the sweep found only {len(paths)} files under "
        f"{', '.join(SEARCHED_TREES)}. It is meant to read every source file "
        "and every design document; check REPO_ROOT and SUFFIXES."
    )
    for relative in SEARCHED_FILES:
        assert (REPO_ROOT / relative).is_file(), (
            f"{relative} is not there, so it is not being swept. If it moved, "
            "point SEARCHED_FILES at the new location."
        )


def test_the_citation_pattern_matches_the_wording_that_was_wrong() -> None:
    """A POSITIVE CONTROL, because the guard below asserts an absence.

    Every string here is a citation this repository actually carried, copied
    from the eight sites issue #1355 corrected and from the sixteen issue #1366
    still has to. If the pattern stopped matching them, the guard would pass
    while checking for nothing at all.
    """
    for wording in (
        "`docs/Cataclysm_GDD_v2.md` line 3744 and `docs/DECISIONS.md` both",
        "exactly one sentence about it, `docs/Cataclysm_GDD_v2.md` line 3732:",
        "See `docs/Cataclysm_GDD_v2.md` lines 1747 and 1776.",
        "docs/Cataclysm_GDD_v2.md lines 858 and 866. A character holds six",
        "the monetisation section already rules out selling it, at line 3109 of"
        " `docs/Cataclysm_GDD_v2.md`",
    ):
        assert CITATION.search(wording), (
            f"the citation pattern no longer matches {wording!r}, which is a "
            "citation this repository actually carried. Widen CITATION."
        )

    flat, line_of = flattened(
        " * See `docs/Cataclysm_GDD_v2.md`\n * line 3744 for the Siege.")
    assert CITATION.search(flat), (
        "the citation pattern no longer finds a citation split across two "
        "comment lines, which is how several of the eight sites were wrapped."
    )
    assert line_of[CITATION.search(flat).start()] == 1, (
        "a citation split across lines is reported at the wrong line."
    )


def test_only_the_sieges_citations_are_reported() -> None:
    """BOTH HALVES OF THE FILTER, on text this test owns.

    The guard below is an absence, so it would pass just as well if the filter
    reported nothing at all -- and it is scoped to the Siege on purpose, so it
    also has to keep ignoring the sixteen citations issue #1366 covers. Neither
    property shows up in a passing sweep, so both are checked here directly.
    """
    reported = citations_in(
        "A Siege takes a share of the city every day it stands.\n"
        "`docs/Cataclysm_GDD_v2.md` line 3744 says so.")
    assert reported == [(2, "docs/Cataclysm_GDD_v2.md` line 3744")], (
        "a line-number citation of the Siege row is no longer reported, so the "
        f"guard below is checking for nothing. Got {reported!r}."
    )

    assert not citations_in(
        "Critical strike chance belongs to the skill, not the character.\n"
        "`docs/Cataclysm_GDD_v2.md` lines 858 and 866 say so."), (
        "a citation with no Siege anywhere near it is being counted as the "
        "Siege's, so this file no longer covers only the row it is named for. "
        "Widening the sweep to every passage is issue #1366, and it needs the "
        "sixteen citations corrected first."
    )


def test_the_siege_row_is_where_the_citations_say_it_is() -> None:
    """The replacement citation resolves, which a line number stopped doing.

    "The Siege row of the sub-type table" is only better than a line number for
    as long as there is such a row in such a table. This is what notices if the
    table is renamed or the row is reworded, rather than the next reader.
    """
    document = REPO_ROOT / DESIGN_DOCUMENT
    assert document.is_file(), f"{DESIGN_DOCUMENT} is not there"
    text = document.read_text(encoding="utf-8", errors="replace")

    assert SUBTYPE_TABLE_HEADING in text, (
        f"{DESIGN_DOCUMENT} no longer has a {SUBTYPE_TABLE_HEADING!r} heading, "
        "so every citation naming 'the sub-type table' now dangles. Rename the "
        "citations along with the heading."
    )

    section = text.split(SUBTYPE_TABLE_HEADING, 1)[1]
    # Stop at the next heading. `| Siege |` opens a row in two later tables as
    # well -- its spawn weight and its power -- so a search over the rest of the
    # document finds three rows and not one. That is exactly why the citation
    # names the table and not just the row, and the count below is what proves
    # this sectioning still works.
    lines = section.splitlines()
    for index, line in enumerate(lines):
        if line.startswith("#"):
            section = "\n".join(lines[:index])
            break

    rows = [line for line in section.splitlines()
            if line.startswith(SIEGE_ROW_OPENS)]
    assert len(rows) == 1, (
        f"expected exactly one {SIEGE_ROW_OPENS!r} row in the table under "
        f"{SUBTYPE_TABLE_HEADING!r} in {DESIGN_DOCUMENT}, found {len(rows)}."
    )

    for sentence in SIEGE_ROW_SENTENCES:
        assert sentence in rows[0], (
            f"the Siege row no longer says {sentence!r}. Several places in this "
            "repository quote that sentence as the source of a constant; if the "
            "design changed, they all have to change with it."
        )


def test_no_file_cites_the_siege_design_text_by_line_number() -> None:
    """THIS IS THE ONE THAT FOUND SOMETHING.

    Written for issue #1355, which named five sites. Run against `development`
    at e058f75 it found eight, because `docs/DECISIONS.md` carried three the
    issue had not looked for.
    """
    offenders = siege_citations_by_line_number()

    listed = "\n".join(f"  {name}:{line}  {text!r}"
                       for name, line, text in offenders)
    assert not offenders, (
        f"{len(offenders)} place(s) cite the Siege design text by line "
        f"number:\n{listed}\n"
        f"{DESIGN_DOCUMENT} is edited directly and grows, so a line number "
        "rots without anything noticing -- it went stale twice in one day "
        "during issue #1355. Cite the row instead: 'the Siege row of the "
        f"sub-type table in `{DESIGN_DOCUMENT}`'."
    )
