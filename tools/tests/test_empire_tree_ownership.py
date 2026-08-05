"""Who owns the empire upgrade tree: the account, or the character.

WHY THIS EXISTS. Issue #273. `docs/Cataclysm_GDD_v2.md`, the main design
document, said twice that empire upgrade points "persist through all runs —
including failed ones" and never mentioned characters. So it was unstated
whether making a new character starts the primary meta-progression system over,
and two other decisions were priced against the unknown answer.

WHAT WAS DECIDED, 2026-08-05, by the project owner: "account wide, unless solo
self found." Every character on the account shares one empire upgrade tree. A
Solo Self-Found character has its own, shared with nothing.

WHY THE EXCEPTION FOLLOWS. A Solo Self-Found character already has no auction
house and no shared stash. Inheriting a mature account's empire tree would be a
larger handout than either, and the one shared resource the flag failed to close
off.

WHY IT MATTERS ELSEWHERE. Issue #255 locked the lethality mode and the Solo
Self-Found flag at character creation. The argument for locking was that
rerolling is affordable, which is only true because the empire tree survives a
new character. The two rules hold each other up, so this file checks that the
document still states both together.

WHAT IS NOT ASSERTED HERE. How the shared tree is scoped. The rest of the
project owner's answer was that sharing "should only apply to the same difficulty
tier", and the game has two difficulty axes — the T1 to T8 content tier and the
Standard/Hardcore/Heretic lethality mode. Which one was meant is carried by issue
#277 and nothing is written for it, so this file asserts that the document says
the question is open rather than asserting an answer.

WHAT IS ASSERTED HERE.

    the tree belongs to the account and a new character inherits it
    Solo Self-Found is stated as the one exception, in both places
    both sections that describe the tree carry the ownership rule, because a
      reader arriving at either one must not be told only half of it
    the document still connects this to the character-creation lock from #255
    the open scoping question is named, with its issue number
    the decision log records the reasoning and what was left unwritten
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The section that states the rule in full.
OWNERSHIP_SECTION = "## **Empire-Wide Upgrades**"

#: The section that restates it and points back. A reader may arrive at either.
SUMMARY_SECTION = "## **Roguelike Meta Progression**"


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str) -> str:
    """One `## ` section, from its heading to the next heading of any level."""
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in ("\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


@pytest.fixture(scope="module")
def ownership(document: str) -> str:
    return unwrapped(section_of(document, OWNERSHIP_SECTION))


@pytest.fixture(scope="module")
def summary(document: str) -> str:
    return unwrapped(section_of(document, SUMMARY_SECTION))


# --------------------------------------------------------------------------
# Who owns it
# --------------------------------------------------------------------------

def test_the_tree_belongs_to_the_account_not_the_character(ownership):
    """The whole of issue #273. Before this the document said only that points
    persist "through all runs", which says nothing about characters."""
    assert "belongs to the account, not to the character" in ownership, (
        "the Empire-Wide Upgrades section no longer says who owns the empire "
        "upgrade tree. Answered 2026-08-05: the account. Issue #273.")


def test_it_says_what_a_new_character_inherits(ownership):
    """Stating the owner is not enough on its own; a reader wants the
    consequence, which is what rerolling costs."""
    assert "Every character on the account shares one tree" in ownership
    assert "levels and gear and nothing else" in ownership, (
        "the section no longer says what making a new character costs. It costs "
        "that character's levels and gear, not the empire progression. "
        "Issue #273.")


def test_solo_self_found_gets_its_own_tree(ownership):
    """The one exception, and the only part of the answer that was not the
    recommendation the issue offered."""
    assert "own empire tree" in ownership and "shared with nothing" in ownership, (
        "the section no longer states the Solo Self-Found exception. A Solo "
        "Self-Found character has its own empire tree and inherits nothing. "
        "Issue #273.")


def test_the_exception_says_why_rather_than_only_what(ownership):
    """A rule with no reason gets deleted by the next person who reads it and
    thinks it is an oversight."""
    assert "no auction house and no shared stash" in ownership, (
        "the Solo Self-Found exception no longer says why it exists. It exists "
        "because the flag already closes off the auction house and the shared "
        "stash, and the empire tree is the larger of the three. Issue #273.")


# --------------------------------------------------------------------------
# Both sections carry it
# --------------------------------------------------------------------------

def test_the_meta_progression_summary_also_states_the_ownership_rule(summary):
    """Two sections describe this system and a reader may arrive at either. The
    summary section is the one that makes the "no run is wasted" claim, so a
    reader who lands there and not on the other must still learn that the tree
    survives a new character."""
    assert "persists across characters as well as across runs" in summary, (
        f"the {SUMMARY_SECTION} section no longer says the empire tree survives "
        "a new character. It is the section that promises no run is wasted, so "
        "it is the one that must say across what. Issue #273.")


def test_the_summary_section_also_names_the_exception(summary):
    """Half a rule is worse than none: a reader who sees only "it belongs to the
    account" would build a Solo Self-Found character expecting to inherit."""
    assert "Solo Self-Found" in summary, (
        f"the {SUMMARY_SECTION} section states the ownership rule without its "
        "one exception. Issue #273.")


# --------------------------------------------------------------------------
# What it holds up, and what it does not settle
# --------------------------------------------------------------------------

def test_it_connects_to_the_character_creation_lock(ownership):
    """Issue #255 locked the lethality mode and the Solo Self-Found flag at
    character creation, and the argument for locking was that rerolling is
    affordable. That is only true because of this rule. If either rule is
    changed alone the pair stops making sense, so the document says so."""
    assert "locked at character creation" in ownership, (
        "the Empire-Wide Upgrades section no longer connects account-wide "
        "progression to the character-creation lock. Making a new character is "
        "the only way to change either flag, which is why the cost of a new "
        "character matters. Issues #255 and #273.")


def test_the_open_scoping_question_is_named_with_its_issue(ownership):
    """The rest of the answer scoped the sharing to "the same difficulty tier",
    and the game has two difficulty axes. Rather than guess, the document says
    the question is open. This check exists so that sentence is not quietly
    dropped, leaving the section reading as if the rule were complete."""
    assert "#277" in ownership, (
        "the Empire-Wide Upgrades section no longer records that how the shared "
        "tree is scoped is undecided. Either issue #277 has been answered and "
        "the answer belongs here in place of this sentence, or the sentence "
        "should not have been removed.")
    assert "still open" in ownership


def test_no_scoping_rule_was_written_while_the_question_is_open(ownership):
    """A check that the document did not quietly acquire an answer. If someone
    settles #277 they should replace the open-question sentence, and this test
    is what tells them to. It fails when both appear together."""
    if "#277" not in ownership:
        pytest.skip("issue #277 has been answered; this check no longer applies")
    claims = ("scoped to the lethality mode", "scoped to the difficulty tier",
              "one tree per lethality mode", "one tree per difficulty tier")
    for claim in claims:
        assert claim not in ownership, (
            f"the section says {claim!r} while still saying the scoping "
            "question is open. Remove the open-question sentence and its "
            "reference to #277 when the answer is written.")


def test_the_decision_log_records_the_reasoning(document):
    """The design document states rules; the reasoning lives in the decision
    log, which is where this project keeps it."""
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = unwrapped(DECISIONS.read_text(encoding="utf-8"))
    assert "belongs to the account, except under Solo Self-Found" in text, (
        "docs/DECISIONS.md has no entry for who owns the empire upgrade tree. "
        "Issue #273.")
    assert "Issue #273" in text


def test_the_decision_log_records_what_was_left_unwritten(document):
    """The decision was applied in half. A log entry that does not say which
    half is a log entry that will be read as complete."""
    text = unwrapped(DECISIONS.read_text(encoding="utf-8"))
    assert "#277" in text, (
        "the docs/DECISIONS.md entry for issue #273 no longer says that the "
        "scoping half of the answer was not written, or no longer names the "
        "issue that carries it.")
