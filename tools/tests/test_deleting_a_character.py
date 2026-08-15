"""A player can delete a character, and what that costs.

WHY THIS EXISTS. Issue #325. `docs/Cataclysm_GDD_v2.md` never said whether a
player can delete a character. That did not matter until issue #315 settled that
nothing which happens in play destroys one, which left a rule from issue #286
with nothing to trigger it:

    When a Solo Self-Found character is lost, its private tree is held, and the
    next Solo Self-Found character created in the same lethality mode inherits
    it rather than starting from nothing.

WHAT WAS DECIDED, by the project owner on 2026-08-14. Asked whether deletion
exists and whether it triggers that rule:

    Yes you can delete a character. Your tree progression persists, but within
    game mode, with the exception of SSF. That tree is individual per ssf
    character [...]

    Weird wording. If you actually delete your character, that's when the tree is
    removed. Dying doesn't delete a character in any game mode. The run resets,
    they keep their gear/levels/passive trees/empire tree and try again.

**So the answer removed the inheritance rule rather than giving it a trigger.**
Deleting a Solo Self-Found character destroys its private tree. No character ever
inherits another's.

WHY THE ASSERTIONS CARRY WHOLE CLAUSES. The sibling file
`test_empire_tree_ownership.py` records that an adversarial review broke two of
its checks by writing a paragraph stating the opposite rule while keeping the
matched substring. "delete", "tree" and "character" are each far too common in
this document to carry a check on their own.

WHAT IS NOT ASSERTED HERE. How many character slots an account has. That was
issue #577, answered on 2026-08-14: 24 per account as one pool, with deletion the
only way to free one. It is checked in `test_character_slots.py`, which reads the
number out of the document rather than pinning it, so tuning the number does not
mean editing two test files.

WHAT IS ASSERTED HERE.

    section II has a Deleting a Character subsection
    deletion exists and is the only thing that removes a character
    it is separated from dying and from a run ending, which are what a reader
      will confuse it with
    deleting an ordinary character costs that character and nothing else
    deleting a Solo Self-Found character destroys its private tree
    no Solo Self-Found character ever inherits another's tree
    the old inheritance rule is gone from both sections that carried it
    the decision log quotes the owner, cites the genre, and labels the part that
      the genre does not settle
    every guard above fails when the document says the old thing
"""

from __future__ import annotations

import pathlib

import pytest

# `tools` is on the path via pythonpath in pyproject.toml.
from prove_guard import break_and_run

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The subsection this issue added, beside Ending a Run inside section II.
DELETION_SECTION = "### **Deleting a Character**"

#: The section that states the empire tree rules in full.
OWNERSHIP_SECTION = "## **Empire-Wide Upgrades**"

#: The section that restates them. A reader may arrive at either.
SUMMARY_SECTION = "## **Roguelike Meta Progression**"

DECISION_HEADING = ("## 2026-08-14 — A player can delete a character, and that "
                    "is the only thing that destroys an empire tree")


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str,
               markers: tuple[str, ...] = ("\n## ", "\n# ")) -> str:
    """The text under one heading, stopping at the first of `markers`.

    The deletion subsection takes `\\n### ` as well, because it is a level-three
    heading with siblings. Without that a later subsection added after it would
    be swallowed into this one and its sentences would satisfy these checks.
    """
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in markers]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


@pytest.fixture(scope="module")
def deletion(document: str) -> str:
    return unwrapped(
        section_of(document, DELETION_SECTION, ("\n## ", "\n# ", "\n### ")))


@pytest.fixture(scope="module")
def ownership(document: str) -> str:
    return unwrapped(section_of(document, OWNERSHIP_SECTION))


@pytest.fixture(scope="module")
def summary(document: str) -> str:
    return unwrapped(section_of(document, SUMMARY_SECTION))


@pytest.fixture(scope="module")
def decision_entry() -> str:
    """This entry alone. docs/DECISIONS.md is about 5,000 lines and names Solo
    Self-Found, Path of Exile and the empire tree in many entries, so a check
    run against the whole file would pass while this entry was deleted."""
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {DECISION_HEADING!r}. It is the "
        f"only place the owner's answer on issue #325 and the reasoning applied "
        f"from it are written down.")
    end = text.find("\n---", start)
    entry = text[start:end if end != -1 else len(text)]
    # Drop the blockquote marker before collapsing whitespace, or a "> " lands
    # in the middle of every quoted sentence that wraps.
    entry = "\n".join(line[2:] if line.startswith("> ") else line
                      for line in entry.splitlines())
    return unwrapped(entry)


# --------------------------------------------------------------------------
# The rule, where a reader looking for it will land
# --------------------------------------------------------------------------

def test_the_document_has_a_deleting_a_character_section(document) -> None:
    """It sits beside Ending a Run deliberately. Those are the two events a
    reader confuses, and until 2026-08-14 only one of them was described."""
    assert DELETION_SECTION in document, (
        "the design document has no Deleting a Character subsection. Whether a "
        "player can delete a character was unstated until issue #325, and the "
        "Solo Self-Found tree rule depends on the answer.")


def test_it_says_deletion_exists_and_is_the_only_thing_that_removes_a_character(
        deletion) -> None:
    """Both halves matter. That it exists answers the issue; that it is the only
    one is what makes the empire tree rules complete."""
    assert ("A player can delete a character, and that is the only thing that "
            "removes one") in deletion, (
        "the Deleting a Character section does not state the rule it exists "
        "for. A player can delete a character, and nothing else removes one. "
        "Issue #325.")


def test_it_separates_deletion_from_dying_and_from_a_run_ending(deletion) -> None:
    """The owner's answer led with this: "Dying doesn't delete a character in
    any game mode." It is the confusion the section exists to prevent, and it is
    the one a player carries in from other games in the genre."""
    assert "Nothing that happens in play does" in deletion, (
        "the Deleting a Character section does not say that nothing which "
        "happens in play removes a character. Dying does not, and a run ending "
        "does not. Issue #325.")
    assert "outside a run" in deletion, (
        "the section does not say that deletion is chosen from outside a run. "
        "That is what makes it different in kind from every other loss in the "
        "game, and it is the reason the tree rule treats it differently. "
        "Issue #325.")


def test_it_says_what_deleting_an_ordinary_character_costs(deletion) -> None:
    """The common case, and the one the genre settles. Everything shared belongs
    to the account, so none of it is at risk."""
    assert ("Deleting an ordinary character costs that character and nothing "
            "else") in deletion, (
        "the section does not say what deleting an ordinary character costs. "
        "It costs that character: the tree, stash, gold and market belong to "
        "the account. Issue #325.")
    assert "because none of them ever belonged to the character" in deletion, (
        "the section says an ordinary deletion costs nothing else without "
        "saying why. The reason is ownership: those things were never the "
        "character's. Issue #325.")


def test_it_says_what_deleting_a_solo_self_found_character_costs(deletion) -> None:
    """The case with no precedent in the genre, and the one the owner answered
    directly: "If you actually delete your character, that's when the tree is
    removed."""
    assert ("Deleting a Solo Self-Found character destroys its private empire "
            "tree with it") in deletion, (
        "the section does not say that deleting a Solo Self-Found character "
        "destroys its private empire tree. That character is the tree's only "
        "owner. Issue #325.")
    assert "no successor that takes it over" in deletion, (
        "the section destroys the tree without ruling out a successor. Between "
        "issues #286 and #325 there was one, so a returning reader will look "
        "for it. Issue #325.")


def test_it_states_the_no_inheritance_rule_as_an_absolute(deletion) -> None:
    """Stated once, without exception, because the exception is exactly what was
    removed. "Always" is doing real work here."""
    assert ("Every Solo Self-Found character starts its tree from nothing, "
            "always") in deletion, (
        "the section does not state without exception that a Solo Self-Found "
        "character starts its tree from nothing. Until 2026-08-14 there was one "
        "exception. Issue #325.")


def test_it_cites_what_the_genre_does_for_the_ordinary_case(deletion) -> None:
    """CLAUDE.md requires the genre to be looked up rather than a rule invented.
    Only the ordinary case is settled by it, and the section says which one."""
    assert "Path of Exile" in deletion, (
        "the section states what deleting an ordinary character costs without "
        "naming the shipped game that does the same. Deleting a Path of Exile "
        "character leaves the account's stash and atlas progression intact.")


# --------------------------------------------------------------------------
# The old rule is gone from both sections that carried it
# --------------------------------------------------------------------------

def test_the_inheritance_rule_is_gone_from_the_ownership_section(ownership) -> None:
    """A rule replaced in one place and left standing in another is worse than
    either version alone, because the two sections then disagree."""
    assert "inherits it rather than starting from nothing" not in ownership, (
        "the Empire-Wide Upgrades section still says a Solo Self-Found "
        "character inherits a lost one's tree. Issue #325 removed that on "
        "2026-08-14: deleting the character destroys the tree.")


def test_the_inheritance_rule_is_gone_from_the_summary_section(summary) -> None:
    assert "takes over its tree" not in summary, (
        "the Roguelike Meta Progression section still says a Solo Self-Found "
        "character takes over a lost one's tree. Issue #325.")


# --------------------------------------------------------------------------
# The reasoning is written down
# --------------------------------------------------------------------------

def test_the_decision_log_quotes_the_owner_rather_than_paraphrasing(
        decision_entry) -> None:
    """The second half of the answer is the load-bearing one and it corrects the
    first, so a paraphrase would lose the correction."""
    assert ("If you actually delete your character, that's when the tree is "
            "removed") in decision_entry, (
        "the docs/DECISIONS.md entry no longer quotes the owner's answer on "
        "issue #325. It is the whole basis of the rule.")
    assert "Dying doesn't delete a character in any game mode" in decision_entry, (
        "the entry drops the half of the answer that separates deletion from "
        "dying. That is the confusion the rule exists to prevent. Issue #325.")


def test_the_decision_log_says_which_half_the_genre_settles(
        decision_entry) -> None:
    """CLAUDE.md: say plainly which parts the research settles and which are
    specific to this game, and label the latter as a judgement rather than
    presenting it as derived. No shipped game has a character-owned
    meta-progression tree, so half of this decision has no precedent at all."""
    assert "is a judgement, not a finding" in decision_entry, (
        "the entry presents the Solo Self-Found half of the rule as though the "
        "genre supported it. No shipped game has a character-owned "
        "meta-progression tree, so there is nothing to read it off. Saying so "
        "is the project rule.")
    assert "Sources:" in decision_entry, (
        "the entry makes a claim about Path of Exile and cites nothing.")


def test_the_decision_log_records_what_argues_against_it(decision_entry) -> None:
    """A player can lose an arbitrary amount of meta-progression with one click,
    and it falls only on the harshest flag. That objection should be found
    already written down rather than discovered."""
    assert "What argues against it" in decision_entry, (
        "the entry records no case against destroying a Solo Self-Found tree "
        "on deletion. There is one, and nothing else in this design destroys "
        "empire upgrade points. Issue #325.")


def test_the_decision_log_names_what_it_leaves_open(decision_entry) -> None:
    """Two things came out of the same answer and neither is decided here."""
    assert "How many character slots an account has" in decision_entry, (
        "the entry does not name the character slot count as still open. "
        "Deletion makes it live, because freeing a slot is a reason to delete.")
    assert "#576" in decision_entry, (
        "the entry does not point at the Solo Self-Found stash issue, which "
        "came out of the same answer and contradicts two sentences the design "
        "document currently carries.")


# --------------------------------------------------------------------------
# The guards above are shown to fail when the document says the old thing
# --------------------------------------------------------------------------

#: Each anchor sits inside ONE line of the hard-wrapped Markdown. A search
#: string spanning a line wrap matches nothing, and `break_and_run` then raises
#: instead of proving anything.
_DELETION_OPENING = ("**A player can delete a character, and that is the only "
                     "thing that removes one.**")

_ORDINARY_OPENING = "**Deleting an ordinary character costs that character and"

_ABSOLUTE = "**Every Solo Self-Found character starts its tree from nothing,"


def _delete_the_deletion_section(text: str) -> str:
    start = text.find(DELETION_SECTION)
    assert start != -1, "the Deleting a Character section is not in the document"
    end = text.find("\n## ", start)
    assert end != -1, "the Deleting a Character section has no following heading"
    return text[:start] + text[end + 1:]


def _make_dying_delete_the_character(text: str) -> str:
    """The confusion the section exists to prevent, written in."""
    assert _DELETION_OPENING in text, "the deletion rule does not open as expected"
    return text.replace(
        _DELETION_OPENING,
        "**A player can delete a character, and dying in Hardcore does it too.**")


def _put_the_inheritance_rule_back(text: str) -> str:
    """The pre-#325 rule from issue #286, restored into the section that
    replaced it."""
    assert _ABSOLUTE in text, "the no-inheritance rule is not where it was"
    return text.replace(
        _ABSOLUTE,
        "**The next Solo Self-Found character created in the same lethality\n"
        "mode inherits it rather than starting from nothing, and every other one\n"
        "starts from nothing,")


def _let_an_ordinary_deletion_take_the_tree(text: str) -> str:
    assert _ORDINARY_OPENING in text, "the ordinary deletion rule is not where it was"
    return text.replace(
        _ORDINARY_OPENING,
        "**Deleting an ordinary character also clears its lethality mode's tree,\n"
        "and costs that character and")


@pytest.mark.parametrize("name, edit, expected", [
    ("the Deleting a Character section is removed",
     _delete_the_deletion_section,
     "test_the_document_has_a_deleting_a_character_section"),
    ("dying deletes a character again",
     _make_dying_delete_the_character,
     "test_it_says_deletion_exists_and_is_the_only_thing_that_removes_a_character"),
    ("the inheritance rule is put back",
     _put_the_inheritance_rule_back,
     "test_it_states_the_no_inheritance_rule_as_an_absolute"),
    ("an ordinary deletion takes the account's tree",
     _let_an_ordinary_deletion_take_the_tree,
     "test_it_says_what_deleting_an_ordinary_character_costs"),
])
def test_the_guards_fail_when_the_document_regresses(name, edit, expected) -> None:
    """A check that cannot fail is worthless. Each case writes an earlier or a
    wrong rule into the document and confirms the test that should catch it
    does, by name rather than only by the run failing."""
    result = break_and_run(
        {"docs/Cataclysm_GDD_v2.md": edit},
        ["python", "-m", "pytest", "tools/tests/test_deleting_a_character.py",
         "-q", "-k", "not regress"],
    )
    output = result.stdout + result.stderr
    assert result.failed, (
        f"breaking the document so that {name} did not fail any test. "
        f"{expected} is supposed to catch it. Output:\n{output[-3000:]}")
    assert expected in output, (
        f"breaking the document so that {name} failed, but not in {expected}. "
        f"Output:\n{output[-3000:]}")
