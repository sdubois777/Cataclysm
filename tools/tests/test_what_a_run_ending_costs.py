"""What a run ending costs, and what "consumed" means.

WHY THIS EXISTS. Issue #315. `docs/Cataclysm_GDD_v2.md`, the main design
document, described a run ending in two ways that could not both be true. The
Worn Residue section said a character killed by its own corrupted double "is
consumed" and that the run ends "exactly as dying in the Last Stand ends a run".
The Cataclysmic Forge introduction priced the whole residue system on that being
permanent: residue was "the only way the Forge can cost a player anything
permanent". Nowhere did the document say, in one place, what a run ending costs.

WHAT WAS DECIDED, by the project owner on 2026-08-05, on issue #315:

    For the consume character part, it says the run ends. Which means you restart
    the tier you're currently on. You just keep your character/gear/passive
    trees/empire tree. [...] The worn residue trigger doesn't actually consume
    the character, it consumes it in the sense that their character will get
    added to the pool of Nemesis characters for that dungeon modifier.

Three rulings, and the tests below check each of them landed:

    a run ending never costs the character, in any of the four ways a run ends
    a failed run replays the same tier; only a boss win adds a Cataclysm
    "consumed" names where a snapshot of the character goes, not what is taken
      from the player

WHAT IS ASSERTED HERE. That section II states the rule in one place, that the two
sections which contradicted it now agree with it, and that the reasoning is in
`docs/DECISIONS.md` rather than only in the issue. The last group of tests checks
the knock-on: three other passages in the document gave losing a character as a
reason for something, and a character can no longer be lost.

WHAT IS NOT ASSERTED HERE. Whether a failed run should cost anything permanent.
The design now has no permanent cost for failure of any kind. That is a balance
question that needs play rather than argument; `docs/DECISIONS.md` records it and
no issue is open on it.
"""

from __future__ import annotations

import pathlib

import pytest

# `tools` is on the path via pythonpath in pyproject.toml.
from prove_guard import break_and_run

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The subsection added by this issue. Level three, inside section II.
ENDING_SECTION = "### **Ending a Run**"

#: The introduction to section VII, which used to call residue the Forge's only
#: permanent cost.
FORGE_SECTION = "# **VII. Crafting — The Cataclysmic Forge**"

#: The section that says what being consumed does.
CONSUMPTION_SECTION = "## **Worn Residue and Consumption**"

#: The dungeon modifier that draws from the pool a consumed character joins.
CORRUPTED_SECTION = "## **Corrupted Stalker (Dungeon Modifier)**"

#: The section holding the empire tree survival rule from issue #286.
OWNERSHIP_SECTION = "## **Empire-Wide Upgrades**"

DECISION_HEADING = ("## 2026-08-05 — A run ending costs the run, "
                    "not the character")

#: The issue #286 entry, which deferred this question and must now point at it.
EARLIER_DECISION_HEADING = ("## 2026-08-05 — A Solo Self-Found empire tree "
                            "survives the character that earned it")


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str) -> str:
    """The text under one heading, stopping at whichever comes first of the next
    level-one and next level-two heading.

    The same two markers work for all five fixtures below. A level-three heading
    inside section II stops at the section's next `## `; a level-one section
    heading stops at its own first `## ` subheading, which is what makes the
    Cataclysmic Forge fixture the introduction rather than all of section VII.
    """
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in ("\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


def entry_of(heading: str, why: str) -> str:
    """One entry from docs/DECISIONS.md.

    Sliced out rather than searched whole, because that file is about 5,000
    lines and names the same mechanics in many entries. A test that searched all
    of it would pass while the entry it cares about was deleted.
    """
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(heading)
    assert start != -1, f"docs/DECISIONS.md has no entry headed {heading!r}. {why}"
    end = text.find("\n---", start)
    entry = text[start:end if end != -1 else len(text)]
    # Drop the blockquote marker before collapsing whitespace. The owner's
    # answer is quoted, so without this a "> " lands in the middle of every
    # quoted sentence that wraps and no quote can be matched.
    entry = "\n".join(line[2:] if line.startswith("> ") else line
                      for line in entry.splitlines())
    return unwrapped(entry)


@pytest.fixture(scope="module")
def ending(document: str) -> str:
    return unwrapped(section_of(document, ENDING_SECTION))


@pytest.fixture(scope="module")
def forge(document: str) -> str:
    return unwrapped(section_of(document, FORGE_SECTION))


@pytest.fixture(scope="module")
def consumption(document: str) -> str:
    return unwrapped(section_of(document, CONSUMPTION_SECTION))


@pytest.fixture(scope="module")
def corrupted(document: str) -> str:
    return unwrapped(section_of(document, CORRUPTED_SECTION))


@pytest.fixture(scope="module")
def ownership(document: str) -> str:
    return unwrapped(section_of(document, OWNERSHIP_SECTION))


@pytest.fixture(scope="module")
def decision_entry() -> str:
    return entry_of(
        DECISION_HEADING,
        "It is the only place the owner's answer on issue #315 and the "
        "reasoning applied from it are written down.")


@pytest.fixture(scope="module")
def earlier_entry() -> str:
    return entry_of(
        EARLIER_DECISION_HEADING,
        "It is the entry that deferred this question, and it has to point at "
        "the answer or a reader stops there believing it is still open.")


# --------------------------------------------------------------------------
# The rule is stated once, in section II, where a run is defined
# --------------------------------------------------------------------------

def test_the_document_has_a_section_saying_what_a_run_ending_costs(document):
    """Before issue #315 the cost of a run ending was never stated. Section II
    said when a run ends and section VII said a character was consumed, and a
    reader had to infer the rest."""
    assert ENDING_SECTION in document, (
        "the design document has no Ending a Run subsection. Without it, what a "
        "run ending costs is spread across section II, the lethality mode table "
        "and the Worn Residue section, and those three disagreed. Issue #315.")


def test_it_says_a_run_ending_does_not_cost_the_character(ending):
    """The ruling itself. This is the sentence the whole issue turned on."""
    assert "A run ending never costs the character" in ending, (
        "the Ending a Run section does not state the rule it exists for: a run "
        "ending never costs the character. Issue #315.")


def test_it_names_every_way_a_run_can_end(ending):
    """A rule stated for "a run ending" is worth nothing if a reader cannot tell
    which events are run endings. Being killed by the residue double is the one
    that was in doubt, so all four are listed together."""
    for way in ("defeating the Cataclysm boss dungeon",
                "losing the capital",
                "dying in the Last Stand dungeon",
                "being killed by the corrupted double"):
        assert way in ending, (
            f"the Ending a Run section does not name {way!r} as a way a run "
            f"ends. All four have to be listed, because the rule that a run "
            f"ending costs nothing permanent is only readable if the set of run "
            f"endings is. Issue #315.")


def test_it_names_what_the_character_keeps(ending):
    """The owner's answer listed four things by name. Naming them is what stops
    a later reader deciding that, say, passive tree points are the exception."""
    for kept in ("levels", "equipment", "class passive trees",
                 "empire upgrade tree"):
        assert kept in ending, (
            f"the Ending a Run section does not say the character keeps its "
            f"{kept}. The owner's answer on issue #315 named all four.")


def test_it_says_a_failed_run_replays_the_same_tier(ending):
    """"You restart the tier you're currently on." The counterpart matters as
    much: a win is the only thing that advances, and section II already said so
    from the other direction."""
    assert "replays the same tier" in ending, (
        "the Ending a Run section does not say a failed run replays the same "
        "tier. That is half of what the owner answered on issue #315; the other "
        "half is that the character survives.")
    assert "Only defeating the boss dungeon adds a Cataclysm" in ending, (
        "the Ending a Run section does not say that only a win advances the "
        "tier. Without it, 'replays the same tier' does not say what a tier "
        "is measured in. Issue #315.")


def test_it_says_what_a_failed_run_does_cost(ending):
    """A rule that only lists what is kept reads as "failure is free". It is
    not: the empire is the thing that is lost, and the empire is most of the
    game."""
    for cost in ("The empire map", "the cities", "the days elapsed"):
        assert cost in ending, (
            f"the Ending a Run section does not name {cost!r} among what a "
            f"failed run costs. Issue #315.")


def test_it_separates_ordinary_death_from_a_run_ending(ending):
    """The lethality mode table in section II prices dying at 5, 10 or 15 days.
    That is a different event from a run ending and the document never said so
    in either place."""
    assert "Ordinary death inside a dungeon is not a run ending" in ending, (
        "the Ending a Run section does not distinguish dying in a dungeon, "
        "which costs days and continues the run, from a run ending. The "
        "lethality mode table prices the first and this section prices the "
        "second, and nothing connected them. Issue #315.")


# --------------------------------------------------------------------------
# The two passages that contradicted it
# --------------------------------------------------------------------------

def test_the_forge_no_longer_claims_a_permanent_cost(forge):
    """WHAT THIS USED TO ASSERT: nothing -- this claim had no test, which is how
    it survived. The Cataclysmic Forge introduction said residue was "the only
    way the Forge can cost a player anything permanent". Under the answer on
    issue #315 the Forge has no permanent cost at all."""
    assert "only way the Forge can cost a player anything permanent" not in forge, (
        "the Cataclysmic Forge introduction still calls Worn Residue the only "
        "way the Forge can cost a player anything permanent. Since issue #315 "
        "being consumed ends the run and leaves the character intact, so the "
        "Forge costs nothing permanent.")
    assert "The Forge cannot cost a player anything permanent" in forge, (
        "the Cataclysmic Forge introduction no longer states what its worst "
        "outcome is. Deleting the old claim is half the change; the section has "
        "to say what replaced it. Issue #315.")


def test_the_forge_still_says_what_its_worst_outcome_is(forge):
    """Removing the permanent-cost claim must not remove the warning with it.
    Residue is still the thing that can end a run."""
    assert "losing that fight ends the run" in forge, (
        "the Cataclysmic Forge introduction no longer says that residue can end "
        "a run. The cost is smaller than the section used to claim, but it is "
        "not nothing, and this is the reader's first warning of it. Issue #315.")


def test_the_consumption_section_defines_consumed(consumption):
    """The word did all the work and was never defined. The owner's answer says
    it names where a snapshot goes, not what is taken from the player."""
    assert "Consumed does not mean destroyed" in consumption, (
        "the Worn Residue section says a character 'is consumed' without saying "
        "what that means. It means a snapshot joins the corrupted-character "
        "pool; the character is untouched. Issue #315.")
    assert "shared library of corrupted characters" in consumption, (
        "the Worn Residue section does not say where a consumed character goes. "
        "That is the whole content of the word. Issue #315.")


def test_the_consumption_section_says_the_character_survives(consumption):
    """Defining the word is not enough on its own -- the reader arrives at this
    section from the sentence "the character is consumed", so the correction has
    to be next to it."""
    assert "The character survives with its levels" in consumption, (
        "the Worn Residue section defines 'consumed' without saying plainly "
        "that the character survives. Issue #315.")


def test_the_rejected_alternative_is_labelled_as_one(consumption):
    """The paragraph headed "Why the run ends rather than the character being
    replaced mid-run" was read on issue #315 as evidence the character is
    destroyed. The owner said it is the opposite: it is the argument against
    replacing the character. Labelling it is the fix."""
    assert "This is the alternative the design rejected" in consumption, (
        "the paragraph explaining why the run ends rather than the character "
        "being replaced is not labelled as the rejected alternative. Read as a "
        "description of what happens, it says a character is replaced by a "
        "fresh one, which is the reading issue #315 corrected.")
    assert "No character is replaced, and none is destroyed" in consumption, (
        "the rejected-alternative paragraph does not end by saying what does "
        "happen. Issue #315.")


def test_the_consumption_section_prices_being_consumed(consumption):
    """CLAUDE.md: say what did not work, plainly and first. This change makes a
    mechanic cheaper than its presentation implies, and the section should admit
    that rather than leave a reader to notice."""
    assert "What being consumed actually costs" in consumption, (
        "the Worn Residue section does not say what being consumed costs now "
        "that it does not cost the character. It costs the run. Issue #315.")
    assert "smaller than the Consumption Threshold warning makes it sound" in consumption, (
        "the Worn Residue section does not admit that the consequence is "
        "smaller than the threshold warning implies. It is, and saying so is "
        "what stops the next reader treating the warning as evidence the "
        "character is destroyed -- which is exactly how issue #315 started.")


# --------------------------------------------------------------------------
# The knock-on: three passages gave losing a character as a reason
# --------------------------------------------------------------------------

def test_the_tree_survival_rule_no_longer_rests_on_residue(ownership):
    """WHAT THIS USED TO BE. The Empire-Wide Upgrades section justified the
    tree-survives-its-owner rule with "Worn Residue can consume a character
    outright". Since issue #315 it cannot."""
    assert "Worn Residue can consume a character outright" not in ownership, (
        "the Empire-Wide Upgrades section still says Worn Residue can consume a "
        "character outright. Since issue #315 being consumed ends the run and "
        "leaves the character intact.")
    assert "never destroyed by anything that happens in play" in ownership, (
        "the Empire-Wide Upgrades section does not say that nothing which "
        "happens in play destroys a tree. That is the rule issue #315 produced. "
        "Issue #325 narrowed it from 'ever' to 'in play', because deleting a "
        "Solo Self-Found character does destroy one.")


def test_the_only_thing_that_destroys_a_tree_is_deletion(ownership):
    """WHAT THIS USED TO ASSERT. Until 2026-08-14 this was
    test_the_inheritance_rule_is_labelled_a_safeguard, and it required the
    section to call the rule a safeguard with no trigger, because at that point
    nothing in the design could lose a character.

    Issue #325 was answered on 2026-08-14. A player can delete a character, and
    the owner's words were: "If you actually delete your character, that's when
    the tree is removed." So there is now something that loses a character, the
    safeguard language would be false, and the property to hold is the real
    rule."""
    assert ("The one thing that destroys a tree is the player deleting the "
            "character that owns it, and only under Solo Self-Found") in ownership, (
        "the Empire-Wide Upgrades section does not say what destroys a tree. "
        "Deleting a Solo Self-Found character takes its private tree with it; "
        "deleting an ordinary character takes nothing, because the tree never "
        "belonged to it. Issue #325.")
    assert "safeguard covering any later rule" not in ownership, (
        "the section still calls the rule a safeguard with no trigger. Since "
        "issue #325 it has one: the player deleting the character.")


def test_the_corrupted_section_no_longer_says_a_character_is_lost(corrupted):
    """WHAT THIS USED TO BE. The Scaling rule justified itself with "a player
    could lose a high-tier character on purpose". Nothing is lost; the exploit
    is now cheaper, not impossible, so the rule matters more."""
    assert "lose a high-tier character on purpose" not in corrupted, (
        "the Corrupted Stalker section still says a player could lose a high-tier "
        "character on purpose. Since issue #315 the character is not lost -- "
        "only the run is.")
    assert "consumed on purpose" in corrupted, (
        "the Corrupted Stalker section no longer gives a reason for the Scaling rule. "
        "The reason still holds and is stronger than before: getting a "
        "character consumed on purpose now costs only the run. Issue #315.")


def test_the_document_no_longer_claims_any_permanent_failure_cost(document):
    """The claim appeared once, but it is the kind that gets restated. This
    searches the whole document on purpose, unlike the fixtures above."""
    assert "only way the Forge can cost a player anything permanent" not in document, (
        "somewhere in the design document, Worn Residue is still called the "
        "only way the Forge can cost a player anything permanent. Since issue "
        "#315 nothing about the Forge is permanent.")


# --------------------------------------------------------------------------
# The reasoning is written down where a reader will find it
# --------------------------------------------------------------------------

def test_the_decision_log_quotes_the_owner_rather_than_paraphrasing(
        decision_entry):
    """The answer contained three separate rulings in five sentences, and one of
    them ("it consumes it in the sense that...") is the one that made the rest
    make sense. A paraphrase would lose it."""
    assert ("added to the pool of Nemesis characters for that dungeon modifier"
            in decision_entry), (
        "the docs/DECISIONS.md entry no longer quotes the owner's answer on "
        "issue #315. The sentence defining what 'consumed' means is the whole "
        "basis of the change and the exact words matter.")


def test_the_decision_log_records_the_case_against(decision_entry):
    """This change makes the game's harshest-sounding mechanic cost a run. That
    objection should be found already written down rather than raised again."""
    assert "The case against" in decision_entry, (
        "the docs/DECISIONS.md entry for issue #315 records no case against. "
        "There is one: the Consumption Threshold warning now guards a smaller "
        "stake than its presentation implies.")
    assert "Deliberately being consumed is cheap" in decision_entry, (
        "the docs/DECISIONS.md entry does not record that a player can now feed "
        "a character into the corrupted-character pool for the price of a run. "
        "Issue #315.")


def test_the_decision_log_names_the_rule_left_without_a_trigger(decision_entry):
    """The tree inheritance rule from issue #286 survives this change with
    nothing to trigger it. That is a loose end and it has an issue."""
    assert "#325" in decision_entry, (
        "the docs/DECISIONS.md entry does not name issue #325, which asks "
        "whether a player can delete a character. That is the only thing that "
        "would give the issue #286 inheritance rule a trigger again.")
    assert "no trigger" in decision_entry


def test_the_decision_log_says_what_it_does_not_decide(decision_entry):
    """The design now has no permanent cost for failure of any kind. That is a
    real consequence of a decision that was not about it."""
    assert "no permanent cost for failure of any kind" in decision_entry, (
        "the docs/DECISIONS.md entry does not record that the design now has no "
        "permanent failure cost at all. Issue #315 decided what a run ending "
        "costs; whether that is the right amount is a balance question.")


def test_the_earlier_entry_points_at_this_one(earlier_entry):
    """The issue #286 entry said this question "was not decided here". A reader
    landing there must be able to find where it was."""
    assert "It has since been answered" in earlier_entry, (
        "the docs/DECISIONS.md entry for issue #286 still says the question of "
        "what a run ending costs was not decided, without saying it has since "
        "been. A reader stops there believing it is open. Issue #315.")
    assert "A run ending costs the run, not the character" in earlier_entry, (
        "the issue #286 entry does not name the entry that answers the question "
        "it deferred. Issue #315.")


# --------------------------------------------------------------------------
# The guards above are shown to fail when the document says the old thing
# --------------------------------------------------------------------------

def _restore_the_permanent_cost_claim(text: str) -> str:
    return text.replace(
        "**The Forge cannot cost a player anything permanent.** Its worst "
        "outcome is that residue",
        "There is one exception, and it is the only way the Forge can cost a "
        "player anything permanent: residue")


def _delete_the_ending_section(text: str) -> str:
    start = text.find(ENDING_SECTION)
    assert start != -1, "the Ending a Run section is not in the document"
    end = text.find("\n## ", start)
    assert end != -1, "the Ending a Run section has no following heading"
    return text[:start] + text[end + 1:]


#: The line that opens the tree-survival rule. Every anchor in this block is
#: chosen to sit inside ONE line of the hard-wrapped Markdown, because a search
#: string spanning a line wrap matches nothing and `break_and_run` then raises
#: instead of proving anything.
_SURVIVAL_OPENING = ("**A tree is never destroyed by anything that happens in "
                     "play, in any mode,")

#: The line that opens the deletion rule, added for issue #325.
_DELETION_OPENING = ("**The one thing that destroys a tree is the player "
                     "deleting the character that")


def _put_the_residue_reason_back(text: str) -> str:
    """The pre-#315 wording, which blamed Worn Residue for losing a character."""
    assert _SURVIVAL_OPENING in text, (
        "the tree-survival rule does not open as expected")
    return text.replace(
        _SURVIVAL_OPENING,
        "**A tree is never destroyed, in any mode,")


def _blame_residue_in_the_body(text: str) -> str:
    """The other half of the same regression: the reason, not the claim."""
    assert "consumed by Worn Residue keeps it:" in text, (
        "the tree-survival rule no longer names Worn Residue at all")
    return text.replace(
        "consumed by Worn Residue keeps it:",
        "Worn Residue can consume a character outright:")


def _restore_the_safeguard_wording(text: str) -> str:
    """The pre-#325 wording, which said nothing could ever lose a character so
    the inheritance rule was a safeguard with no trigger. Issue #325 gave it
    one."""
    assert _DELETION_OPENING in text, (
        "the deletion rule does not open as expected")
    return text.replace(
        _DELETION_OPENING,
        "The rule below is a safeguard covering any later rule that does lose a\n"
        "character, rather than a case that arises today. The next character that")


@pytest.mark.parametrize("name, edit, expected", [
    ("the Forge calls residue a permanent cost again",
     _restore_the_permanent_cost_claim,
     "test_the_forge_no_longer_claims_a_permanent_cost"),
    ("the Ending a Run section is deleted",
     _delete_the_ending_section,
     "test_the_document_has_a_section_saying_what_a_run_ending_costs"),
    ("the survival rule drops the 'in play' qualifier",
     _put_the_residue_reason_back,
     "test_the_tree_survival_rule_no_longer_rests_on_residue"),
    ("the ownership section blames Worn Residue again",
     _blame_residue_in_the_body,
     "test_the_tree_survival_rule_no_longer_rests_on_residue"),
    ("the deletion rule is called a safeguard with no trigger again",
     _restore_the_safeguard_wording,
     "test_the_only_thing_that_destroys_a_tree_is_deletion"),
])
def test_the_guards_fail_when_the_document_regresses(name, edit, expected):
    """A check that cannot fail is worthless. Each case puts an earlier wording
    back and confirms the test that should catch it does."""
    result = break_and_run(
        {"docs/Cataclysm_GDD_v2.md": edit},
        ["python", "-m", "pytest", "tools/tests/test_what_a_run_ending_costs.py",
         "-x", "-q", "-k", "not regress"],
    )
    output = result.stdout + result.stderr
    assert result.failed, (
        f"breaking the document so that {name} did not fail any test. "
        f"{expected} is supposed to catch it. Output:\n{output[-3000:]}")
    assert expected in output, (
        f"breaking the document so that {name} failed, but not in {expected}. "
        f"Output:\n{output[-3000:]}")
