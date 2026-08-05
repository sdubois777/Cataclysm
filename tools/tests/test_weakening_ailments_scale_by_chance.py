"""Cripple, Weaken, Shred and Madness scale by chance to apply and nothing else.

WHY THIS EXISTS. Issue #300. The six damage over time effects have three
rollable affixes each -- a chance to apply, a damage affix and a duration affix,
the last two added by issue #205. The four weakening effects have one. The issue
asked whether that is a gap.

WHAT WAS DECIDED, 2026-08-05. It is not. No magnitude affix and no duration
affix are added, because chance to apply is already all three levers: chance up
to 100%, magnitude above that through `ailment_application` in
`sim/cataclysm_sim/affixes.py`, and duration above the magnitude cap through the
roll-over rule the design document's own table states.

    Cripple reaches its 80% cap at 267% chance, Weaken at 400%
    eleven gear pieces can carry the affix, one each, for 165%
    so affixes alone reach 0.6 and 0.4 of the cap, and one to four of the
      forty-five sockets take each past it

THE GENRE SURVEY ARGUED THE OTHER WAY and the decision record says so. Path of
Exile sells increased effect of Chill and of Withered; Diablo IV sells Crowd
Control Duration on amulets. In those games a chance to apply stops paying at
100%, so a second stat is the only way to keep scaling. Here it does not stop
paying. That difference is the whole argument, so the tests below check it is
still written down rather than only the conclusion it supports.

WHAT IS ASSERTED HERE. That the design document states the rule and the
measurement, that it does not overstate what the measurement shows, that the
decision log carries the survey that argues against it and what would reverse
it, and that the four affixes named in the model are still one per effect.

THE MEASUREMENT ITSELF IS CHECKED ELSEWHERE.
`sim/tests/test_analysis_scripts.py` recomputes what
`sim/analyse_weakening_ailments.py` prints. This file checks the documents.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The section holding the ailment tables and the new paragraphs.
AILMENT_SECTION = "**What magnitude scales depends on the effect, and it is never wasted.**"

DECISION_HEADING = ("## 2026-08-05 — Cripple, Weaken, Shred and Madness scale "
                    "by chance alone")


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return unwrapped(GDD.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def decision_entry() -> str:
    """Just this entry.

    Sliced out rather than searched whole: docs/DECISIONS.md is about 5,000
    lines and names Path of Exile, Diablo IV and Last Epoch in many entries, so
    a test searching all of it would pass while this entry's survey was
    deleted.
    """
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {DECISION_HEADING!r}. It is "
        f"the only place the measurement behind issue #300, the genre survey "
        f"that argues against it, and what would reverse it are written down.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


# --------------------------------------------------------------------------
# The design document states the decision
# --------------------------------------------------------------------------

def test_it_says_the_four_have_one_affix_each(document):
    """The decision itself. Before issue #300 the document described what
    magnitude scales and never said which affixes exist to move it."""
    assert ("Cripple, Weaken, Shred and Madness have one affix each, a chance "
            "to apply, and no separate affix for magnitude or duration"
            in document), (
        "the design document no longer states that the four weakening effects "
        "have one rollable affix. Without it the asymmetry with the six damage "
        "over time effects reads as an oversight. Issue #300.")


def test_it_says_why_one_affix_is_enough(document):
    """A rule with no reason gets reversed by the next reader who counts three
    affixes on one family and one on the other."""
    assert "Chance to apply is already all three levers" in document, (
        "the design document says the four have one affix without saying why "
        "that is enough. The reason is the overflow rule: chance above 100% "
        "becomes magnitude and magnitude above the cap becomes duration. "
        "Issue #300.")


def test_it_gives_the_measured_figures(document):
    """The claim is quantitative and the numbers are what make it checkable.
    `sim/analyse_weakening_ailments.py` prints all of them."""
    for figure in ("Eleven pieces can carry a chance to apply, one each, "
                   "for 165%",
                   "Cripple reaches its 80% cap at 267% and Weaken at 400%"):
        assert figure in document, (
            f"the design document no longer states {figure!r}. Issue #300's "
            f"answer rests on the measurement, not on the argument alone.")


def test_it_names_the_script_that_produced_them(document):
    """So a reader can re-run the measurement rather than trust the sentence.
    That is the failure issue #6 recorded for three other scripts."""
    assert "sim/analyse_weakening_ailments.py" in document, (
        "the design document states the measurement without naming the script "
        "that produces it. Issue #300.")


def test_it_does_not_overstate_what_was_measured(document):
    """CLAUDE.md: say what did not work, plainly. The measurement shows a
    second affix would add no LEVER. It says nothing about whether one lever is
    worth as much as three, and the document must not imply it does."""
    assert ("This is not a claim that the four are as strong as the six"
            in document), (
        "the design document states the one-affix rule without saying what the "
        "measurement does not cover. It does not show the four are balanced "
        "against the six; it shows a second affix would add no new way to "
        "scale them. Issue #300.")


def test_it_admits_the_comparison_games_do_the_opposite(document):
    """The strongest evidence points the other way and the document says so
    where a reader will meet it, not only in the decision log."""
    assert "The comparison games all do give their equivalents a second lever" \
        in document, (
        "the design document no longer records that Path of Exile and Diablo "
        "IV both sell a separate magnitude or duration stat for their "
        "equivalents. That is the case against this decision and hiding it "
        "makes the decision look better supported than it is. Issue #300.")
    assert "a chance to apply stops paying at 100%" in document, (
        "the design document names the genre counter-example without naming "
        "the difference that makes this design able to ignore it. Issue #300.")


# --------------------------------------------------------------------------
# The decision log carries the reasoning
# --------------------------------------------------------------------------

def test_the_decision_log_names_the_three_comparison_games(decision_entry):
    for game in ("Path of Exile", "Diablo IV", "Last Epoch"):
        assert game in decision_entry, (
            f"the docs/DECISIONS.md entry for issue #300 no longer names "
            f"{game}. All three were surveyed and all three argue against the "
            f"decision, which is why the survey is recorded.")


def test_the_decision_log_says_the_survey_argues_against_it(decision_entry):
    """A decision that records only supporting evidence is not a decision, it
    is an advertisement."""
    assert "The case against" in decision_entry
    assert "Every comparison game gives its equivalents a second lever" \
        in decision_entry, (
        "the docs/DECISIONS.md entry does not state that the genre survey went "
        "the other way. It did, and it was not close. Issue #300.")


def test_the_decision_log_says_the_sources_are_search_summaries(decision_entry):
    """`WebFetch` gets HTTP 402 from the two wikis these claims come from, so
    the evidence is a search result summary rather than the page. Every
    decision resting on that has to say so."""
    assert "web search result summaries rather than the pages themselves" \
        in decision_entry, (
        "the docs/DECISIONS.md entry cites Path of Exile and Diablo IV "
        "mechanics without saying the evidence is a search summary. The wikis "
        "cannot be fetched, and a reader checking the claim needs to know "
        "that before trying.")


def test_the_decision_log_says_what_would_reverse_it(decision_entry):
    """This is a judgement against the genre, so the conditions for changing
    it should be written down before anyone has to argue for it."""
    assert "What would reverse this" in decision_entry
    assert "Play, not argument" in decision_entry, (
        "the docs/DECISIONS.md entry does not say what evidence would change "
        "this decision. It is a feel question past the cap, so it is play. "
        "Issue #300.")


def test_the_decision_log_gives_the_measured_table(decision_entry):
    """The numbers, so a later reader can tell whether they have moved without
    re-deriving them."""
    for figure in ("267%", "400%", "165%", "45 sockets"):
        assert figure in decision_entry, (
            f"the docs/DECISIONS.md entry for issue #300 no longer states "
            f"{figure}, one of the measured figures the decision rests on.")


def test_the_decision_log_separates_this_from_issue_303(decision_entry):
    """Madness is also the subject of an open operator question. Neither
    answer depends on the other and the entry says which is which."""
    assert "#303" in decision_entry, (
        "the docs/DECISIONS.md entry does not say that whether Madness is a "
        "hard stun is a separate open question. A reader could take this entry "
        "as having settled it.")


# --------------------------------------------------------------------------
# The model still matches the decision
# --------------------------------------------------------------------------

def test_the_four_still_have_exactly_one_affix_each():
    """The decision is that no second affix was added. This is what would
    catch one being added without the documents being updated."""
    import sys
    sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import affixes as af

    weakening = {"Cripple", "Weaken", "Shred", "Madness"}
    for name in weakening:
        matching = [a for a in af.AILMENT_AFFIXES if a.ailment == name]
        assert len(matching) == 1, (
            f"{name} now has {len(matching)} affixes in "
            f"sim/cataclysm_sim/affixes.py. Issue #300 decided it has exactly "
            f"one, a chance to apply. If that changed, the paragraphs in "
            f"docs/Cataclysm_GDD_v2.md and the docs/DECISIONS.md entry both "
            f"need rewriting.")

    named = {a.ailment for a in af.AILMENT_AFFIXES}
    assert weakening <= named, (
        f"these weakening effects have no affix at all: "
        f"{sorted(weakening - named)}. Issue #300 rests on each having one.")
