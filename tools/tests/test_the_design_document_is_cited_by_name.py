"""The design document is cited by the name of the passage, never by a line number.

WHAT THIS GUARDS. `docs/Cataclysm_GDD_v2.md` is the design, it is edited
directly, and it grows. Nothing renumbers a citation when a paragraph is
inserted above it, so a line number rots silently and a reader who follows one
lands somewhere unrelated -- and may conclude the design text is gone. A row
name, a heading, or the bolded lead sentence this document uses as its section
headings does not move.

WHERE IT CAME FROM. Written for issue #1355 scoped to the Siege sub-type alone,
where eight sites cited one row by line number and every one was stale. The
number went stale twice inside a single day: #1355 was filed on 2026-09-06
saying the row had moved from line 3744 to line 3801, and by the time the fix
was written that afternoon further edits had carried it to 3839.

Issue #1366 widened it to the whole document. That issue listed sixteen further
citations in nine files; the sweep below found nineteen in the same nine, for two
reasons the issue's own hand sweep could not see:

- `docs/DECISIONS.md` writes "`Cataclysm_GDD_v2.md` ... Line 4503", naming the
  document without the `docs/` prefix that #1355's pattern required;
- `sim/cataclysm_sim/player_damage.py` twice wrote a bare "Line 2378:" or "at
  line 2401" a sentence after naming the document, with no path at all.

Both shapes are now matched. See CITATION_WINDOW for how the second is bounded.

WHY A TEST AND NOT A CAREFUL READ. #1355 listed five sites and there were eight;
#1366 listed sixteen and there were nineteen. Three separate hand sweeps each
missed something. A sweep in the test suite is the only way to know.

THE SWEEP IS HALF OF IT. An absence proves nothing on its own -- a citation that
names a passage is only better than a line number while that passage still
exists and still says what the citation quotes. CITED_PASSAGES is the other
half, and `test_every_cited_passage_is_still_in_the_document` is what notices
when the design moves under a citation rather than the next reader.

WHAT THIS DOES NOT GUARD. Whether a quoted figure is current. The
`sim/` comments about the two-handed damage advantage quote "1.33 times" from a
sentence that has said 1.29 since issue #633, and this test does not look at
figures inside a quote. Issue #1381 has that.

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

#: The document's name, with the `docs/` prefix OPTIONAL.
#:
#: OPTIONAL BECAUSE ONE SITE OMITTED IT. `docs/DECISIONS.md` wrote "Three
#: statements in `Cataclysm_GDD_v2.md` about longest range ... Line 4503 says",
#: and #1355's pattern, which required the prefix, could not see it. It was the
#: only line-number citation in the repository that neither that pattern nor
#: issue #1366's hand sweep found.
DOCUMENT_NAME = re.compile(r"(?:docs/)?Cataclysm_GDD_v2\.md", re.IGNORECASE)

#: A reference to a line number, in any of the shapes this repository used.
LINE_REFERENCE = re.compile(r"\blines?\s+\d+(?:\s+and\s+\d+)?", re.IGNORECASE)

#: Any file path, used to tell "line 40" of THIS document from "line 40" of some
#: other file mentioned in between. `docs/DECISIONS.md` has a real example:
#: "It is not in `Cataclysm_GDD_v2.md`. It is in the generated enchantment
#: tables: `game/Data/EnchantmentsPositive.csv` line 40 reads ...". That is not
#: a citation of the design and must not be reported as one.
PATH_TOKEN = re.compile(
    r"[A-Za-z0-9_./\\-]+\.(?:md|py|h|cpp|csv|xlsx|uasset|tsx|cs|json|txt|ini|"
    r"log|bat|yml|yaml)",
    re.IGNORECASE,
)

#: How far from the document's name a line number may sit and still be a
#: citation of it, once the flattening below has removed the line wrapping.
#:
#: MEASURED IN BOTH DIRECTIONS against the tree issue #1366 was fixed on, and
#: not guessed. The floor is the furthest true positive: the two bare references
#: in `sim/cataclysm_sim/player_damage.py` sat 77 and 170 characters after the
#: name, with the ordinary adjacent form at 2. The ceiling is the nearest false
#: positive: `tools/tests/test_surge_port.py` discusses the old bad citations on
#: purpose -- 'seven as "line 3744" and one as "line 3732"' -- 428 and 451
#: characters after naming the document, and reporting those would fail the
#: guard on a comment that exists to explain the convention.
#:
#: 250 leaves 47% headroom above the furthest true positive and 41% below the
#: nearest false positive. IT IS A SCOPE AND NOT A TUNING KNOB: raising it past
#: about 430 makes the guard fail on that comment.
CITATION_WINDOW = 250

#: The heading the Siege row lives under, and the sentences the citations quote.
SUBTYPE_TABLE_HEADING = "## **Dungeon Sub-Types**"
SIEGE_ROW_OPENS = "| Siege |"
SIEGE_ROW_SENTENCES = (
    "Deals 1% damage to city defenses and population per day while active.",
    "Increases in power by 2.5 points per day.",
    "Max 1 per city.",
)

#: Every passage this repository now cites by name, and the sentences the
#: citations quote out of it.
#:
#: (what the citation calls it, the lead that must be unique in the document,
#: the sentences that must sit in the SAME paragraph as that lead).
#:
#: A CITATION IS ONLY BETTER THAN A LINE NUMBER WHILE THIS HOLDS. If the design
#: is reworded, the citations that quote it have to be reworded with it, and
#: this is what says so.
CITED_PASSAGES: tuple[tuple[str, str, tuple[str, ...]], ...] = (
    (
        "the three-channel rule, cited by CataclysmDamageCalculation.h and "
        "CataclysmSkillEffects.h",
        "**A minion reaches its summoner through exactly three channels, and "
        "nothing else crosses.**",
        (),
    ),
    (
        "the rule that nothing else crosses, cited by "
        "CataclysmDamageCalculation.h",
        '**Everything else is blocked unless a modifier says "minion".**',
        ("A minion does not take the summoner's weapon damage, flat added "
         "damage, attack speed, critical strike chance or multiplier, "
         "penetration",),
    ),
    (
        "the minion damage attribute paragraph, cited by "
        "CataclysmDamageCalculation.h and CataclysmSkillEffects.h",
        "**Each grants 1.0% increased minion damage per point**",
        ("a minion takes neither the summoner's critical strike chance nor its "
         "multiplier.",),
    ),
    (
        "the stat source table, cited by CataclysmDataRows.h and "
        "CataclysmCriticalStrikeTests.cpp",
        "| The skill being used | Critical strike chance",
        (),
    ),
    (
        "the rule that critical strike chance is the skill's, cited by "
        "CataclysmDataRows.h and CataclysmCriticalStrikeTests.cpp",
        "**Critical strike chance belongs to the skill, not the character.**",
        ("A character has no critical strike chance in the abstract.",),
    ),
    (
        "the dual wielder the damage target describes, cited by "
        "CataclysmEquipmentTests.cpp",
        "**That 87 is two one-handed weapons, not one weapon.**",
        ("an Axe with an Axe at 92 and an Axe with a Sword at 86",),
    ),
    (
        "the rule that two one-handers sum their base damage, cited by "
        "player_damage.py and test_player_damage.py",
        "**It has to reach the implicits, not only the affixes.**",
        ("Two one-handed weapons **sum** their base damage, so an Axe and a "
         "Sword give 86 against a Greatsword's stated 78.",),
    ),
    (
        "the section stating the two-handed advantage, cited by "
        "player_damage.py and test_player_damage.py",
        "### **A Two-Handed Weapon Is Worth Double, Per Implicit and Per "
        "Affix**",
        (),
    ),
    (
        "the reason the two-handed multiplier is 2, cited by player_damage.py",
        "**The figure is derived, not chosen.**",
        ("Two is the multiplier that makes the two loadouts worth the same in "
         "affixes.",),
    ),
    (
        "the rule that attack speed averages, cited by player_damage.py and "
        "test_player_damage.py",
        "**Attack speed is the average of the two weapons.**",
        ("Not the sum, and not the slower.",),
    ),
    (
        "the rule that a minion has its own stats, cited by "
        "generate_datatables.py",
        "**Every minion type has its own stats.**",
        ("A minion is not a percentage of its summoner.",),
    ),
    (
        "the monetisation promise, cited by docs/DECISIONS.md",
        "**Buying the game buys all of it.**",
        ("no stash or storage fees of any kind",),
    ),
    (
        "the Corrupted Sentinel's reach, cited by docs/DECISIONS.md",
        "#### **Its reach is the longest in the game, because reach is the "
        "only tool it has**",
        (),
    ),
    (
        "the longest range in the game, cited by docs/DECISIONS.md",
        "**14 metres**, which is the longest range any player attack reaches",
        ("and no attack states more",),
    ),
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


def _another_file_between(flat: str, start: int, end: int) -> bool:
    """Is some OTHER file named between these two points?

    A mention of the design document itself does not count, so two citations of
    it standing close together do not hide each other.
    """
    for token in PATH_TOKEN.finditer(flat, start, end):
        if not DOCUMENT_NAME.search(token.group(0)):
            return True
    return False


def citations_in(text: str) -> list[tuple[int, str]]:
    """Every line-number citation of the design document in `text`.

    Split out from the sweep so that the filter can be checked against text this
    file owns, rather than only against whatever the repository happens to
    contain today.
    """
    flat, line_of = flattened(text)
    names = list(DOCUMENT_NAME.finditer(flat))
    found: list[tuple[int, str]] = []

    for hit in LINE_REFERENCE.finditer(flat):
        for name in names:
            if name.end() <= hit.start():
                gap, low, high = hit.start() - name.end(), name.end(), hit.start()
            elif hit.end() <= name.start():
                gap, low, high = name.start() - hit.end(), hit.end(), name.start()
            else:
                continue
            if gap > CITATION_WINDOW or _another_file_between(flat, low, high):
                continue
            quoted = flat[min(name.start(), hit.start()):
                          max(name.end(), hit.end())]
            found.append((line_of[hit.start()], quoted[:160]))
            break
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


def citations_by_line_number() -> list[tuple[str, int, str]]:
    """Every line-number citation of the design document in the repository."""
    found = []
    for path in searched_paths():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line, cited in citations_in(text):
            found.append((path.relative_to(REPO_ROOT).as_posix(), line, cited))
    return found


def document_blocks() -> list[str]:
    """The design document as paragraphs, each with its wrapping flattened.

    A PARAGRAPH AND NOT THE WHOLE FILE, so that "this sentence sits under that
    heading" can be checked rather than only "both strings are in there
    somewhere". The document separates paragraphs with a blank line, sometimes
    carrying two spaces.

    FLATTENED, because the document is hard-wrapped in places: the sentence
    "a minion takes neither the summoner's critical strike chance nor its
    multiplier" is split across two lines, and a raw search reports it missing.
    """
    document = REPO_ROOT / DESIGN_DOCUMENT
    text = document.read_text(encoding="utf-8", errors="replace")
    return [" ".join(block.split())
            for block in re.split(r"(?:\r?\n[ \t]*){2,}", text)
            if block.strip()]


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
    from the eight sites issue #1355 corrected and the nineteen issue #1366 did.
    If the pattern stopped matching them, the guard would pass while checking
    for nothing at all.
    """
    for wording in (
        "`docs/Cataclysm_GDD_v2.md` line 3744 and `docs/DECISIONS.md` both",
        "exactly one sentence about it, `docs/Cataclysm_GDD_v2.md` line 3732:",
        "See `docs/Cataclysm_GDD_v2.md` lines 1747 and 1776.",
        "docs/Cataclysm_GDD_v2.md lines 858 and 866. A character holds six",
        "the monetisation section already rules out selling it, at line 3109 of"
        " `docs/Cataclysm_GDD_v2.md`",
        # Named without the `docs/` prefix, and the number 58 characters later.
        "Three statements in `Cataclysm_GDD_v2.md` about longest range are in "
        "conflict with the data. Line 4503 says 14 metres",
        # A bare reference, a sentence after the document was named.
        "which is exactly the two-handed advantage `docs/Cataclysm_GDD_v2.md` "
        "line 2382 already states. THIS PAIR IS THE DESIGN DOCUMENT'S OWN "
        "EXAMPLE. Line 2378: \"Two one-handed weapons\"",
    ):
        assert citations_in(wording), (
            f"the citation sweep no longer matches {wording!r}, which is a "
            "citation this repository actually carried. Widen DOCUMENT_NAME, "
            "LINE_REFERENCE or CITATION_WINDOW."
        )

    reported = citations_in(
        " * See `docs/Cataclysm_GDD_v2.md`\n * line 3744 for the Siege.")
    assert reported, (
        "the sweep no longer finds a citation split across two comment lines, "
        "which is how several of the eight sites of #1355 were wrapped."
    )
    assert reported[0][0] == 2, (
        f"a citation split across lines is reported at line {reported[0][0]}, "
        "not at the line the number is on."
    )


def test_a_line_number_belonging_to_another_file_is_not_reported() -> None:
    """BOTH HALVES OF THE FILTER, on text this test owns.

    The guard below is an absence, so it would pass just as well if the sweep
    reported nothing at all, and it would FAIL the repository as it stands if
    the sweep reported every "line N" near the document's name. Neither property
    shows up in a passing sweep, so both are checked here directly.
    """
    assert citations_in(
        "Critical strike chance belongs to the skill.\n"
        "`docs/Cataclysm_GDD_v2.md` lines 858 and 866 say so."), (
        "a plain line-number citation is no longer reported, so the guard "
        "below is checking for nothing."
    )

    # The real sentence from `docs/DECISIONS.md`, shortened. The line number
    # belongs to the CSV, and the design document is named only to say the
    # value is NOT in it.
    assert not citations_in(
        "Where the base block value came from. It is not in "
        "`Cataclysm_GDD_v2.md`. It is in the generated enchantment tables: "
        "`game/Data/EnchantmentsPositive.csv` line 40 reads \"You block for "
        "65%-75% of damage\"."), (
        "a line number belonging to another file is being reported as a "
        "citation of the design document. _another_file_between is what stops "
        "that; check PATH_TOKEN still matches the intervening path."
    )

    # The ceiling on CITATION_WINDOW, which `tools/tests/test_surge_port.py`
    # relies on: a comment may discuss the old bad line numbers at length.
    assert not citations_in(
        "`docs/Cataclysm_GDD_v2.md`: \"Deals 1% damage to city defenses.\" "
        + "Padding that is not a path. " * 12
        + "Eight places cited that sentence by line number -- seven as "
          "\"line 3744\" and one as \"line 3732\"."), (
        "a line number 400-odd characters from the document's name is being "
        "reported. That is how `tools/tests/test_surge_port.py` explains the "
        f"convention, so CITATION_WINDOW ({CITATION_WINDOW}) is now too wide."
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


def test_the_paragraph_split_actually_splits() -> None:
    """A POSITIVE CONTROL for `document_blocks`.

    If the split stopped working the document would come back as one enormous
    block, and the check below would degrade to "both strings are in the file
    somewhere" without failing.
    """
    blocks = document_blocks()
    assert len(blocks) > 500, (
        f"the design document split into only {len(blocks)} paragraphs, so the "
        "check below is no longer asking whether a sentence sits under the "
        "lead that cites it."
    )
    assert max(len(block) for block in blocks) < 5000, (
        "one paragraph came back longer than 5000 characters, which means the "
        "split is not finding the blank lines."
    )


def test_every_cited_passage_is_still_in_the_document() -> None:
    """THE OTHER HALF OF THE GUARD.

    A citation that names a passage beats a line number only while the passage
    is there and still says what the citation quotes. Issue #1366 replaced
    nineteen line numbers with the names below; this is what notices when the
    design moves under one of them.
    """
    blocks = document_blocks()

    for what, lead, quotes in CITED_PASSAGES:
        matching = [block for block in blocks if lead in block]
        assert len(matching) == 1, (
            f"{what} is cited as {lead!r}, and {len(matching)} paragraphs of "
            f"{DESIGN_DOCUMENT} contain that. A citation needs exactly one. "
            "If the design was reworded, reword the citations with it."
        )
        for quote in quotes:
            assert quote in matching[0], (
                f"{DESIGN_DOCUMENT} no longer says {quote!r} in the paragraph "
                f"led by {lead!r}. {what} quotes that sentence, so if the "
                "design changed the citation has to change with it."
            )


def test_no_file_cites_the_design_document_by_line_number() -> None:
    """THIS IS THE ONE THAT FOUND SOMETHING.

    Written for issue #1355, which named five sites; run against `development`
    at e058f75 it found eight. Widened for issue #1366, which named sixteen; run
    against `development` at e8b33c2 it found nineteen.
    """
    offenders = citations_by_line_number()

    listed = "\n".join(f"  {name}:{line}  {text!r}"
                       for name, line, text in offenders)
    assert not offenders, (
        f"{len(offenders)} place(s) cite the design document by line "
        f"number:\n{listed}\n"
        f"{DESIGN_DOCUMENT} is edited directly and grows, so a line number "
        "rots without anything noticing -- it went stale twice in one day "
        "during issue #1355. Cite what the passage IS instead: a table row, as "
        "in 'the Siege row of the sub-type table', or the bolded lead sentence "
        "this document uses as its section headings."
    )
