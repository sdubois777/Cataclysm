"""Every dated entry in the decisions log is preceded by a rule and a blank line.

WHY THIS EXISTS. Issue #1402. `docs/DECISIONS.md` separates one dated entry from
the next with a horizontal rule, then a blank line, then the heading:

    ---

    ## 2026-09-06 - A Quest dungeon moves on a coin of 0.5

Sixty-one boundaries do not. The separator is not decoration: when two entries
join with no gap the file reads as one entry with a stray heading inside it, and
the next person resolving a conflict there is more likely to drop one. That is
how it reached sixty-three before pull request #1416 repaired two of them.

THE DEFECT IS NOT SITTING STILL, which is why the check is worth more than a
one-off tidy-up. This is the most conflict-prone file in the repository and every
resolution is by hand, so it gains roughly one bad boundary per hand resolution.
Counted directly out of the file rather than reported:

    at 3d1b288, when #1402 filed    362 entries, 60 wrong
    at commit 3fe0b08               363 entries, 61 wrong
    at commit 7d29ee4               364 entries, 62 wrong
    at commit f687744               365 entries, 63 wrong
    at commit 70c5718               365 entries, 61 wrong

Issue #1402 records 61 for the first of those rows rather than 60. Every row
above was re-measured here, reading each commit's blob out of git and applying
the rule this file implements; the other three agree with the issue exactly.
The difference on the first row is the first dated entry, which is exempt and
which that earliest count had not yet excluded. The direction of travel, which
is what the table is for, is the same either way.

THE LAST ROW IS THE FIRST FALL IN THE SERIES. Pull request #1416 put a rule
and a blank line above two headings while repairing something else, so this
check landed at 61 rather than 63. Those two are gone from `ALREADY_WRONG`,
which is the list doing what it is for.

WHY THIS LANDS WITHOUT THE CLEANUP. The obvious objection to a check that arrives
first is that it goes red over sixty-one faults it did not cause. `ALREADY_WRONG`
is the answer: it lists those boundaries by heading text, so the suite is green
today and goes red the moment a sixty-second appears. The project owner ruled for
this sequencing on issue #1402 after two sessions gave opposite advice; both had
independently named the merge window as the hard part, and an allowance list is
the one approach that needs no merge window. The cleanup follows in small pieces,
each shrinking `ALREADY_WRONG`, whenever the file is quiet.

`test_the_allowance_list_holds_nothing_that_is_now_correct` is what makes the
list shrink rather than rot. Repair a boundary without deleting its line here and
the suite tells you which line to delete.

FOUR TRAPS, EACH OF WHICH COST A SESSION A CYCLE ON THIS FILE.

1. THE FILE IS CRLF. A pattern written against a bare line feed matches nothing
   and reports a clean file. One session's separator check printed "0 correct, 0
   wrong" and nearly believed it. `decisions_lines` reads bytes and normalises
   explicitly, and `test_the_file_parses_at_all` is the positive control that
   stops any later count of zero from reading as good news.

2. THE FIRST DATED ENTRY IS CORRECT AS IT STANDS. It is preceded by the
   "Decisions made outside the Google Drive documents, newest first." line and a
   blank, not by a rule, and it is where the log starts. A check that does not
   exempt it reports sixty-two problems where there are sixty-one.

3. ONLY DATED HEADINGS ARE ENTRIES. The file has 427 headings starting with two
   hashes and only 365 are entries; the other 62 are section headings inside an
   entry. A check keyed on the hashes alone flags all 62.

4. A HEADING QUOTED INSIDE A FENCED CODE BLOCK IS NOT AN ENTRY. None is today,
   but an entry describing this very convention would quote a bad boundary to
   show what one looks like, and that is a false failure waiting to happen. The
   risk in skipping fences is going blind if one is never closed, so
   `decisions_entries` refuses to finish inside one.

WHAT THIS DELIBERATELY DOES NOT CHECK. Four conforming boundaries have their rule
sitting directly under the previous entry's last line of text with no blank
between, which makes Markdown render that line as a heading. That is a different
defect with a different fix, and it is issue #1408.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: An entry heading: two hashes, a space, and an ISO date. Anchored on the date
#: rather than on the hashes, because 62 of the file's 427 `## ` headings are
#: sections inside an entry rather than entries. Trap 3 above.
DATED_HEADING = re.compile(r"^## \d{4}-\d{2}-\d{2}\b")

#: What opens and closes a fenced code block. The file uses backticks only --
#: no tildes, and no indented fences -- so a plain toggle is exact here.
FENCE = "```"

#: A horizontal rule standing on its own line.
RULE = "---"

#: The line the log opens with, above the first dated entry. Named so that the
#: exemption in `decisions_entries` is anchored to something real rather than to
#: an index.
PREAMBLE = "Decisions made outside the Google Drive documents, newest first."

#: THE BOUNDARIES THAT WERE ALREADY WRONG when this check landed, keyed by the
#: full heading line. Keyed on the text and not on a line number because entries
#: are added at the top of the file, so every line number moves constantly.
#:
#: THIS LIST IS MEANT TO SHRINK. Repairing a boundary means inserting the rule
#: and the blank line above the heading in `docs/DECISIONS.md` AND deleting its
#: line from here. Doing only the first makes
#: `test_the_allowance_list_holds_nothing_that_is_now_correct` fail and name the
#: line to delete. Nothing may ever be added to this list: a new bad boundary is
#: the thing this file exists to stop.
#:
#: Measured at commit 70c5718 by reading the file's bytes, normalising the line
#: endings, and taking every dated heading after the first whose two preceding
#: lines are not a rule and a blank. Fifty-seven have a blank line but no rule
#: above it, three have neither, and one has the rule pressed straight against
#: the heading.
ALREADY_WRONG = frozenset({
    '## 2026-09-06 — The game is balanced around a player fully invested in the Explorer tree',
    '## 2026-09-06 — The empire layer learns which Cataclysm is running, and the Cataclysm dungeon unlocks at half of them',
    '## 2026-09-06 — What the game counts when a dungeon is beaten, and the one number it refuses to invent',
    "## 2026-09-06 — The Cow Level stays at 7 and only the five take the Siege's slack, so a Cow Level is the rarest thing again",
    "## 2026-09-06 — What a Quest dungeon's move means: the rim counts, the dungeon keeps everything, and moving is a chance",
    "## 2026-09-06 — Halve the Siege's rate and cut its growth, so the earned win route survives",
    '## 2026-09-06 — A Quest dungeon moves to an ADJACENT city, and the simulation was wrong',
    '## 2026-09-06 — Pestilence asks for 5 quest objectives and The Void for 5',
    "## 2026-09-06 — The population multiplier's shape: base points × living over maximum, per dungeon, no floor",
    '## 2026-09-06 — A Cataclysm dungeon cannot roll the Cow Level sub-type',
    '## 2026-09-06 — The Cataclysm draw is per character and a failed run replays the same ones',
    '## 2026-09-06 — Population kept alive scales empire progression',
    '## 2026-09-06 — The order the Cataclysms are added in is randomised per character',
    '## 2026-09-06 — The Siege sub-type is modelled, and keeps percentage damage on purpose',
    "## 2026-09-06 — City damage is a number of points, not a share of the city's own maximum",
    '## 2026-09-06 — A fallen city becomes a dungeon, and comes back at half',
    '## 2026-09-06 — The Last Stand happens twice as often and is harder to win than the figures on record',
    '## 2026-09-06 — Only ordinary dungeons deepen the Cataclysm boss, and the Last Stand takes none of it',
    '## 2026-09-06 — Every dungeon a surge creates has a sub-type; the no-sub-type outcome is removed',
    "## 2026-09-05 — The Architect preset's city damage multiplier is the eleven damage-reduction nodes, and nothing else",
    '## 2026-09-05 — The Cataclysm boss dungeon grows on dungeons DEFEATED, not on dungeons failed',
    '## 2026-09-05 — The dungeon modifier Weight column is a danger score, not a spawn frequency',
    '## 2026-09-05 — The Corrupted Stalker is granted separately and does not take a modifier slot',
    '## 2026-09-05 — The empire tree comparison runs at two surge sizes, because that is the axis its ordering turns on',
    '## 2026-09-05 — A near-unwinnable Last Stand is the intended design, and its four constants are not to be tuned',
    '## 2026-09-05 — OVERTURNED: a Generic dungeon modifier is drawn from every pool, and it takes a slot',
    "## 2026-09-05 — A Siege takes 1% of a city's MAXIMUM per day, not of what is left",
    '## 2026-09-05 — The Architect empire tree branch gets nothing added, and a defensive branch is worth what the surges make it worth',
    '## 2026-09-05 — The remaining six Demonic enemy modifiers, and a debuff that stacks',
    '## 2026-09-05 — The ten Generic enemy modifiers all do something, and Abyssal Aura works',
    '## 2026-09-05 — Crowd control resistance shortens a stun and a shove, and at 100 stops them',
    "## 2026-09-05 — A creature's modifiers are drawn by a spawner, not by the rarity setter",
    "## 2026-09-05 — A status effect's strength says which stat it moves, in data",
    '## 2026-09-05 — Thorns of Glass reflects half a hit, and five enemy modifiers now do something',
    '## 2026-09-05 — A creature is given its modifiers, and which ones it may draw',
    "## 2026-09-05 — The character sheet shows the model's 46 stats, in the model's groups",
    '## 2026-09-05 — An evaded attack applies nothing it was carrying',
    '## 2026-09-04 — Movement speed is a boots modifier worth 35%',
    '## 2026-09-04 — The four skills that state a stun duration now stun',
    '## 2026-09-04 — An energy shield refills in five seconds, whatever built it',
    '## 2026-09-04 — A stated floor per affix, and seven tops moved to make room for one',
    '## 2026-09-04 — Durable Modifications is a multiplicative damage reduction node',
    '## 2026-09-04 — The item tool tip says which numbers are percentages, and one affix is renamed',
    '## 2026-09-04 — The difficulty tier takes resistance off the player, so resistance affixes can carry real numbers',
    '## 2026-09-03 — Retaliation is a share of the blow taken, not a flat amount',
    '## 2026-09-03 — The basic attack fires on the left mouse button, and WASD is the default, which supersedes two earlier entries',
    '## 2026-09-03 — Three weapon damage questions from a play test, and two of the three answers are "the design is right"',
    '## 2026-09-02 — A charge shoves for 2 metres, and the audit that found it is now a test',
    '## 2026-08-25 — A node that reads like a rule change can still be a stat, and that is worth trying first',
    "## 2026-08-25 — A skill's health cost and a character's are added, and they are measured against different things",
    '## 2026-08-25 — A bonus that grows with a state counts whole steps, rounded down',
    '## 2026-08-25 — A timed window is named for the event that opens it, and it includes its last instant',
    '## 2026-08-25 — "Increased damage" on a passive node means attack damage and spell damage, and not damage over time',
    "## 2026-08-25 — A passive bonus can depend on the character's state, and it is never written onto the stat",
    "## 2026-08-24 — A damage over time effect's stated number is per tick, not a total",
    '## 2026-08-23 — Every gear rarity drops at every difficulty tier; the cap becomes a penalty',
    '## 2026-08-22 — The projectile head is an octahedron, not a round dot, and it is as wide as the projectile hits',
    '## 2026-08-22 — The cast effect fires when the skill fires, not before it, because no skill has a cast time',
    "## 2026-08-22 — A hit's damage type answers two questions, and they have different answers",
    '## 2026-08-22 — Effect colours are used at a per-emitter gain, not at the value in the table',
    '## 2026-08-22 — A melee swing gets its own effect shape, and it is the ninth',
})


@pytest.fixture(scope="module")
def decisions_lines() -> list[str]:
    """The log's lines, with the line endings normalised.

    READ AS BYTES AND NORMALISED BY HAND. Trap 1 above: the file is CRLF, and a
    check that searches it for a bare line feed finds nothing and calls it
    clean. Doing the conversion here rather than relying on Python's universal
    newlines makes the trap visible to the next reader.
    """
    if not DECISIONS.is_file():
        pytest.skip(f"{DECISIONS.name} is not present")
    text = DECISIONS.read_bytes().decode("utf-8")
    return text.replace("\r\n", "\n").replace("\r", "\n").split("\n")


@pytest.fixture(scope="module")
def decisions_entries(decisions_lines) -> list[tuple[int, str]]:
    """Every dated entry heading, as a line index and the heading line itself.

    Headings quoted inside a fenced code block are not entries and are skipped.
    Trap 4 above: an unclosed fence would make this skip the rest of the file and
    report a clean bill of health over unread text, so it refuses to finish
    inside one.
    """
    found: list[tuple[int, str]] = []
    inside_a_fence = False
    for index, line in enumerate(decisions_lines):
        if line.startswith(FENCE):
            inside_a_fence = not inside_a_fence
            continue
        if not inside_a_fence and DATED_HEADING.match(line):
            found.append((index, line))

    assert not inside_a_fence, (
        f"{DECISIONS.name} ends inside a fenced code block, so this check "
        "skipped everything after the last unclosed ``` and cannot say whether "
        "those entries are separated. Close the fence.")
    return found


def boundaries_missing_the_separator(
        lines: list[str], entries: list[tuple[int, str]]) -> list[tuple[int, str]]:
    """The entries not preceded by a rule, a blank line, and then the heading.

    The first dated entry is skipped. Trap 2 above: it opens the log under the
    preamble line rather than after another entry, and is correct as it stands.
    """
    wrong: list[tuple[int, str]] = []
    for index, heading in entries[1:]:
        above = lines[index - 1]
        above_that = lines[index - 2]
        if above == "" and above_that.strip() == RULE:
            continue
        wrong.append((index, heading))
    return wrong


def test_the_file_parses_at_all(decisions_lines, decisions_entries):
    """The positive control, and the reason any later count of zero is believable.

    Trap 1's failure mode is a check that finds nothing and passes. Every count
    below is only evidence because this one found the things it was looking for.
    """
    assert len(decisions_lines) > 1000, (
        f"{DECISIONS.name} parsed to {len(decisions_lines)} lines, which is far "
        "too few for the decisions log. The line ending handling is probably "
        "wrong, and every count in this file is meaningless until it is fixed.")
    assert len(decisions_entries) > 100, (
        f"Only {len(decisions_entries)} dated entries were found in "
        f"{DECISIONS.name}. There were 365 at commit 70c5718 and the log only "
        "grows, so this is a parsing failure rather than a shrinking file.")
    assert any(line.strip() == RULE for line in decisions_lines), (
        f"No horizontal rule was found anywhere in {DECISIONS.name}. There were "
        "313 at commit 70c5718.")
    assert PREAMBLE in decisions_lines, (
        f"{DECISIONS.name} no longer opens with the preamble line this check "
        "exempts the first entry against. Check that the first entry is still "
        "correct as it stands before changing the exemption.")


def test_the_first_entry_is_the_one_under_the_preamble(
        decisions_lines, decisions_entries):
    """The exemption is anchored to something, not just to being first.

    Trap 2 above. If the log ever gains an entry above the preamble, the
    exemption would silently cover the wrong heading, and this says so.
    """
    first_index, first_heading = decisions_entries[0]
    above = decisions_lines[first_index - 1]
    above_that = decisions_lines[first_index - 2]
    assert above == "" and above_that == PREAMBLE, (
        f"The first dated entry, {first_heading!r}, is no longer the one sitting "
        f"under the preamble line. Above it is {above_that!r} then {above!r}. "
        "This check exempts the first entry from needing a rule above it because "
        "it opens the log; that exemption is now covering a different heading.")


def test_every_dated_entry_is_preceded_by_a_rule_and_a_blank_line(
        decisions_lines, decisions_entries):
    """The check itself. A new entry must carry its separator."""
    wrong = boundaries_missing_the_separator(decisions_lines, decisions_entries)
    unlisted = [(index, heading) for index, heading in wrong
                if heading not in ALREADY_WRONG]

    assert not unlisted, (
        f"{len(unlisted)} entries in {DECISIONS.name} are not preceded by a "
        f"horizontal rule and a blank line:\n"
        + "\n".join(f"  line {index + 1}: {heading}" for index, heading in unlisted)
        + "\n\nPut a line holding only --- and then a blank line above each "
        "heading. Do not add them to ALREADY_WRONG: that list records the "
        "boundaries that were already wrong when this check landed, and stopping "
        "it growing is the whole point of the check. Issue #1402.")


def test_the_allowance_list_holds_nothing_that_is_now_correct(
        decisions_lines, decisions_entries):
    """What makes ALREADY_WRONG shrink rather than rot.

    A boundary repaired in `docs/DECISIONS.md` without its line being deleted
    from `ALREADY_WRONG` leaves the list excusing a fault that no longer exists,
    and the list stops being a record of anything.
    """
    still_wrong = {heading for _, heading
                   in boundaries_missing_the_separator(decisions_lines, decisions_entries)}
    stale = sorted(ALREADY_WRONG - still_wrong)

    assert not stale, (
        f"{len(stale)} headings in ALREADY_WRONG are no longer wrong, or no "
        f"longer in {DECISIONS.name} under that text:\n"
        + "\n".join(f"  {heading}" for heading in stale)
        + f"\n\nDelete those lines from ALREADY_WRONG in {pathlib.Path(__file__).name}. "
        "The list is meant to shrink as the boundaries are repaired.")
