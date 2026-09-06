"""No file states that a dungeon floor costs a day as though it were a law.

WHAT THIS GUARDS. One floor costing one day is a **starting rate**. City upgrades
and the empire upgrade tree lower the days a dungeon takes to walk while its
floor count stays where it is, so depth and reward are one axis and depth and
time are not. `CLAUDE.md` states this at length and `docs/DECISIONS.md` records
the 2026-09-05 correction that established it.

WHY A TEST AND NOT A CAREFUL READ. Three issues in a row have been filed for the
same rule because each search missed places the last one did not reach.

- #1281 corrected nine sites. Its search named `sim/cataclysm_sim/` rather than
  `sim/`, so `sim/experiments.py` was never looked at, and it listed four exact
  phrases, so a restatement in other words slipped past.
- #1322 was filed for three more. Re-searching for it found ten, because the
  phrase can be written "costs a day" as well as "costs one day".
- The first search written for #1322 matched against the raw file and missed
  `sim/cataclysm_sim/engine.py`, where the sentence is split across two comment
  lines as "one floor costs" / "# one day".

That last one is why `flattened` exists. Every file here is hard-wrapped, in
prose and in comments, so a claim has to be matched against text with the
wrapping taken out or the guard reports a clean tree that is not clean.

WHAT THIS DOES NOT GUARD. Whether a qualified statement is *well* qualified. A
sentence that says "to begin with" and then draws a conclusion only true at the
starting rate passes here and still needs reading.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

#: Trees that must never state the rule as law, plus the two files outside them
#: that carry it. `docs/Cataclysm_GDD_v2.md` is the design and is included;
#: `CLAUDE.md` is where the corrected rule is stated at length.
SEARCHED_TREES = ("game", "sim", "tools")
SEARCHED_FILES = ("CLAUDE.md", "docs/Cataclysm_GDD_v2.md")

#: `docs/DECISIONS.md` IS DELIBERATELY NOT SEARCHED, and must not be corrected.
#: It is the log of what was decided and when, and three of its entries quote the
#: superseded wording as the thing that was got wrong -- one of them begins
#: "Where I went wrong. I read 'One floor costs exactly one day of empire time'
#: as an invariant." Rewriting those would destroy the record of the correction
#: this test exists to hold in place.
NOT_SEARCHED = "docs/DECISIONS.md"

SUFFIXES = {".py", ".h", ".cpp", ".md"}

#: Build output, engine intermediates and content. None is source and the
#: sweep is slow enough without them.
SKIPPED_DIRECTORIES = {"Binaries", "Intermediate", "Saved", "DerivedDataCache",
                       "Content", "__pycache__", ".git"}

#: A line's indent and whatever comment marker it opens with: Python `#`,
#: C++ `//`, a Doxygen block's ` * `.
LEADER = re.compile(r"^[ \t]*(?:#+|//+|\*+)?[ \t]*")

#: The claim, in every phrasing found in this repository: "one floor costs one
#: day", "a dungeon floor costs a day", "One floor costs exactly one day".
CLAIM = re.compile(
    r"(?:one |a |each )?(?:dungeon )?floors? costs? (?:exactly )?(?:one|a) day",
    re.IGNORECASE,
)

#: Any of these, appearing soon after the claim, makes it a statement of the
#: starting rate rather than of a law. Taken from the wording already used in
#: `sim/README.md`, `CataclysmSurge.h` and the design document, so a correction
#: written in the project's own voice passes without needing this list extended.
QUALIFIERS = (
    "to begin with",
    "starting rate",
    "by default",
    "at first",
    "not an invariant",
    "until a player has invested",
    "before anybody has invested",
    "this model",
)

#: Statements judged true as written, so the sweep does not report them. Each
#: entry is the sentence as it reads once wrapping is taken out.
#:
#: THE ONE ENTRY IS THE ONE ISSUE #1322 EXAMINED AND KEPT. The design document
#: gives depth tension as the REASON the starting rate is a day. The starting
#: rate is still a day, so the sentence is true with no qualifier; adding one
#: would make it say something it is not trying to say. Anything added here
#: needs the same kind of reason written beside it.
JUDGED_TRUE_AS_WRITTEN = (
    "it is why one dungeon floor costs one day",
)

#: How far after the claim a qualifier may sit and still be read as belonging to
#: it. Long enough for the sentence that explains it, measured against the
#: existing corrections: the longest gap on `development` at 7cb161f is 214
#: characters, in `game/Source/CataclysmEmpire/Empire/CataclysmCityUpgrade.h`.
QUALIFIER_WINDOW = 340


def flattened(text: str) -> tuple[str, list[int]]:
    """The file as one long line, plus the source line number of each character.

    Leading indent and comment markers are removed from every line and the lines
    are joined with single spaces, so a claim split across two comment lines is
    found and can still be reported at the line it starts on.
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


def searched_paths() -> list[pathlib.Path]:
    """Every file this test reads, this file itself excluded.

    EXCLUDING ITSELF MATTERS. The patterns above spell the claim out, so a sweep
    that included this file would report it and never pass.
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


def unqualified_claims() -> list[tuple[str, int, str]]:
    """Every statement of the rule with no qualifier near it."""
    found = []
    for path in searched_paths():
        text = path.read_text(encoding="utf-8", errors="replace")
        flat, line_of = flattened(text)
        lowered = flat.lower()
        kept = []
        for phrase in JUDGED_TRUE_AS_WRITTEN:
            start = lowered.find(phrase.lower())
            while start != -1:
                kept.append((start, start + len(phrase)))
                start = lowered.find(phrase.lower(), start + 1)
        for match in CLAIM.finditer(flat):
            if any(a <= match.start() < b for a, b in kept):
                continue
            window = flat[match.start():match.end() + QUALIFIER_WINDOW].lower()
            if any(qualifier in window for qualifier in QUALIFIERS):
                continue
            found.append((path.relative_to(REPO_ROOT).as_posix(),
                          line_of[match.start()], match.group(0)))
    return found


def test_the_sweep_actually_reads_files() -> None:
    """Without this, a wrong path would make the guard below pass on nothing.

    It also pins the two files named individually, so moving either is reported
    here rather than silently dropping them from the sweep.
    """
    paths = searched_paths()
    assert len(paths) > 200, (
        f"the sweep found only {len(paths)} files under "
        f"{', '.join(SEARCHED_TREES)}. It is meant to read every source file in "
        "the repository; check REPO_ROOT and SUFFIXES."
    )
    for relative in SEARCHED_FILES:
        assert (REPO_ROOT / relative).is_file(), (
            f"{relative} is not there, so it is not being swept. If it moved, "
            "point SEARCHED_FILES at the new location."
        )


def test_the_claim_pattern_finds_the_wording_the_project_uses() -> None:
    """The pattern matches the phrasings that are actually in the tree.

    A POSITIVE CONTROL, because every other assertion here is an absence. If the
    pattern stopped matching, `unqualified_claims` would return nothing and the
    guard below would pass while checking for nothing at all.
    """
    for wording in ("One dungeon floor costs one day",
                    "a dungeon floor costs a day",
                    "One floor costs exactly one day",
                    "each floor costs a day"):
        assert CLAIM.search(wording), (
            f"the claim pattern no longer matches {wording!r}, which is how this "
            "rule is written in this repository. Widen CLAIM."
        )

    flat, line_of = flattened("# one floor costs\n        # one day, they said")
    assert CLAIM.search(flat), (
        "the claim pattern no longer finds a sentence split across two comment "
        "lines, which is the miss that made issue #1322 need a second search."
    )
    assert line_of[CLAIM.search(flat).start()] == 1, (
        "a claim split across lines is reported at the wrong line."
    )


def test_no_file_states_the_floor_cost_rule_as_a_law() -> None:
    """THIS IS THE ONE THAT FOUND SOMETHING.

    Written for issue #1322, which named three sites. Run against `development`
    at 7cb161f it found ten, in `game/`, `sim/` and `tools/` alike.
    """
    offenders = unqualified_claims()

    listed = "\n".join(f"  {name}:{line}  {text!r}"
                       for name, line, text in offenders)
    assert not offenders, (
        f"{len(offenders)} place(s) state that a dungeon floor costs a day "
        "without saying it is a starting rate:\n"
        f"{listed}\n"
        "A floor costs a day to begin with; city upgrades and the empire tree "
        "lower the walk while the floor count stays where it is. Add one of "
        f"{', '.join(QUALIFIERS)} within {QUALIFIER_WINDOW} characters, or "
        "reword so the sentence is true after a player has invested. "
        f"{NOT_SEARCHED} is deliberately outside this sweep and must not be "
        "corrected -- it records what was decided and when."
    )
