"""Quitting a fight does not undo it, and the design says so in both places.

WHY THIS EXISTS. The project owner set the requirement on 2026-08-20: the game
saves automatically and often enough that a Hardcore character cannot get out of
a losing boss fight by closing the game, and a player who is cut off comes back
where they were. Issue #529 builds it.

WHY A TEST OVER A DOCUMENT RATHER THAN OVER CODE. There is no save code yet; a
search of `game/Source/` for `USaveGame` returns nothing. What exists is the
design, in two documents that have to agree, and this is the thing most likely to
be quietly lost: the requirement is unusual, it constrains what every creature
must be able to write about itself, and somebody building the save records later
who has not read it would build the ordinary thing instead.

THE THREE STATEMENTS THIS HOLDS, and each is load-bearing for a different reason:

- **What is restored and what is not.** A boss keeping the damage taken off it is
  the whole point; resuming mid-blow is deliberately excluded because it is the
  data whose shape changes every patch and would need a migration each time.
- **Death is written synchronously.** It is the one event the feature exists to
  make stick, and a routine asynchronous write would leave a window to close the
  game in.
- **The escape moves to whatever exit is left.** Path of Exile 2 removed logout
  macros and its players report the escape simply moved to pausing and taking
  "Respawn at Checkpoint". Named in the design because it is the failure this is
  most likely to have.

WHAT THIS DOES NOT CHECK. Any code, because none exists. When the save system is
built, the checks that read source belong in a file beside it; these stay,
because a document that stops saying this is a different failure from code that
stops doing it.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SAVE_DESIGN = REPO_ROOT / "docs" / "Save_System_Design.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: Written out rather than escaped, for the reason recorded in this
#: project's notes: a backslash escape does not survive every route a
#: file is edited by.
NEWLINE = chr(10)

#: The heading of this decision's own entry in the log. Everything checked
#: against the log is checked inside this entry rather than anywhere in the
#: file; see the `decisions` fixture for why that matters.
ENTRY_HEADING = ("## 2026-08-20 — The game saves itself constantly, so "
                 "quitting a fight does not undo it")


def unwrapped(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


@pytest.fixture(scope="module")
def save_design() -> str:
    return unwrapped(SAVE_DESIGN)


@pytest.fixture(scope="module")
def gdd() -> str:
    return unwrapped(GDD)


@pytest.fixture(scope="module")
def decisions() -> str:
    """This decision's own entry, and not the whole log.

    THE WHOLE FILE IS THE WRONG THING TO SEARCH, and the first version of this
    file searched it. `docs/DECISIONS.md` holds every decision this project has
    ever made, so a phrase this entry uses is very likely to appear in another
    one: "refused to serve" is also in the 2026-08-19 entry on enemy rarity,
    which hit the same Fandom refusal, and the Maxroll guide is cited by three
    separate entries. Both checks below passed with this entry's sources
    deleted, and were found by breaking them on purpose.
    """
    if not DECISIONS.is_file():
        pytest.skip("DECISIONS.md is not present")

    # CUT BY LINE AND UNWRAP AFTERWARDS, NOT THE OTHER WAY ROUND. Searching
    # the unwrapped text for the next "## " finds this entry's own "###"
    # sub-headings, because "### " contains "## ", and the entry gets cut at
    # its first sub-heading instead of at the next entry. Written that way
    # first, and found by the checks below failing on text that is plainly
    # present in the file.
    lines = DECISIONS.read_text(encoding="utf-8").splitlines()

    opening = [n for n, line in enumerate(lines)
               if " ".join(line.split()) == ENTRY_HEADING]
    assert opening, (
        f"docs/DECISIONS.md has no entry headed {ENTRY_HEADING!r}. The "
        f"decision that the game saves itself constantly is recorded there; "
        f"if the heading was reworded, reword it here too.")

    first = opening[0] + 1
    after = [n for n in range(first, len(lines))
             if lines[n].startswith("## ")]
    last = after[0] if after else len(lines)

    return " ".join(NEWLINE.join(lines[first:last]).split())


# ---------------------------------------------------------------------------
# The requirement itself
# ---------------------------------------------------------------------------

def test_the_save_design_says_there_is_no_manual_save(save_design) -> None:
    """An automatic save is only worth anything if it is the only save.

    A manual save beside it is the escape again under a different name: a player
    saves before a boss and reloads after. That is the same hole closing the game
    was supposed to close.
    """
    assert "The game saves itself, often, and there is no manual save" in save_design, (
        "docs/Save_System_Design.md no longer says that the game saves itself "
        "and that there is no manual save. Both halves matter: a manual save "
        "beside an automatic one reopens the hole the automatic one closes.")


def test_a_boss_keeps_the_damage_taken_off_it(save_design) -> None:
    """The single sentence that makes quitting worthless.

    Everything else about resuming is comfort. This is the part that removes what
    the escape is worth: a player who closes the game comes back to a boss at the
    health it had, so the fight is not reset.
    """
    assert "A boss keeps every point taken off it" in save_design, (
        "docs/Save_System_Design.md no longer says that a boss keeps the damage "
        "taken off it when a fight is resumed. Without that, quitting resets the "
        "fight and the whole feature buys nothing.")

    assert "a breather, not a reset" in save_design, (
        "docs/Save_System_Design.md no longer states what quitting mid-fight "
        "actually buys. Saying it plainly is what stops the next person "
        "assuming the fight resumes mid-blow, which it deliberately does not.")


def test_the_mid_blow_state_is_excluded_on_purpose(save_design) -> None:
    """Left out for a reason, and the reason has to survive with the decision.

    Persisted combat choreography would need a save migration every patch, for
    state nobody wants preserved. Somebody reading only the exclusion would read
    it as unfinished work and add it.
    """
    assert "Why the mid-blow state is deliberately left out" in save_design, (
        "docs/Save_System_Design.md no longer explains why a fight resumes from "
        "a still moment rather than mid-blow. Without the reason it reads as an "
        "unfinished feature rather than a decision.")


def test_death_is_written_before_anything_else(save_design) -> None:
    """The one rule that cannot be relaxed.

    Every other write may be asynchronous. Death may not: it is the event the
    feature exists to make stick, and a queued write leaves a window in which the
    game can be closed.
    """
    assert "Death is written first, and synchronously" in save_design, (
        "docs/Save_System_Design.md no longer requires that death is written "
        "synchronously, in the frame it happens. An asynchronous death write "
        "leaves exactly the window this feature exists to close.")


def test_the_other_ways_out_of_a_fight_are_named(save_design) -> None:
    """Path of Exile 2's lesson, and the failure this is most likely to have.

    It removed logout macros and its players report the escape moved rather than
    closed, to pausing in a boss fight and taking "Respawn at Checkpoint". Every
    other exit in this game has to answer the same rule or the work buys nothing.
    """
    assert "The escape moves to whatever other way out exists" in save_design, (
        "docs/Save_System_Design.md no longer warns that closing one way out of "
        "a fight moves the escape to the others. That is what happened to Path "
        "of Exile 2 and it is the most likely way this feature fails.")

    assert "Last Stand" in save_design, (
        "docs/Save_System_Design.md no longer names the Last Stand as another "
        "way out of a fight that has to answer the same rule.")


def test_the_limit_is_stated_rather_than_implied_away(save_design) -> None:
    """An offline file can be copied, and pretending otherwise is worse than the
    hole itself.

    Issue #505 accepted that local files can be edited. This feature does not
    change that, so the design has to say where enforcement actually stops.
    """
    assert ("An offline save file can be copied before a boss and put back "
            "afterwards") in save_design, (
        "docs/Save_System_Design.md no longer states that an offline save can "
        "be copied and restored. That limit is real, issue #505 accepted it, "
        "and a design that implies a guarantee it does not have is worse than "
        "one that admits the gap.")


# ---------------------------------------------------------------------------
# Where a player reads it
# ---------------------------------------------------------------------------

def test_the_difficulty_section_tells_the_player(gdd) -> None:
    """A player choosing Hardcore is buying this rule and should be told.

    The Difficulty Options section already argues that a mode is worth something
    because it cannot be switched off when it becomes inconvenient. Not being
    able to switch off a fight is the same argument, and belongs beside it.
    """
    assert ("Nor can a fight be switched off at the moment it becomes "
            "inconvenient") in gdd, (
        "docs/Cataclysm_GDD_v2.md no longer tells a player choosing a lethality "
        "mode that closing the game does not undo a fight. That is part of what "
        "they are choosing.")

    assert "an offline Hardcore character is on its honour" in gdd, (
        "docs/Cataclysm_GDD_v2.md no longer admits to the player that an "
        "offline save can be copied. Implying a guarantee the game does not "
        "have is worse than admitting the gap.")


def test_the_decisions_log_carries_the_sources(decisions) -> None:
    """Per CLAUDE.md, a mechanic proposed from research names the research."""
    for source in ("maxroll.gg/d4/resources/hardcore-guide",
                   "pathofexile.com/forum/view-thread/3828741"):
        assert source in decisions, (
            f"docs/DECISIONS.md no longer cites {source}. CLAUDE.md requires "
            f"the sources behind a mechanic taken from the genre to be recorded "
            f"with the decision.")

    assert "refused to serve" in decisions, (
        "This decision's entry in docs/DECISIONS.md no longer records that "
        "two widely quoted Path of Exile timers could not be verified because "
        "the page refused to serve. An unverified figure that quietly loses "
        "its caveat becomes a fact.")

    assert "6 second delay before a lost client is logged out" in decisions, (
        "This decision's entry no longer names which figures were not "
        "verified. Recording that something could not be checked is only "
        "useful if it says what.")
