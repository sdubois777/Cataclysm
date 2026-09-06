"""Adjacency, and what a relocated Quest dungeon keeps, in the design document.

WHY THIS FILE EXISTS. Two owner rulings of 2026-09-06 on issue
[#1324](https://github.com/sdubois777/Cataclysm/issues/1324) had been built into
both codebases and written into neither document:

- **"Include the perimeter links".** What "adjacent" means.
- **"Keeps everything, fix the size".** What a Quest dungeon carries with it
  when it moves.

`CLAUDE.md` says a design decision is not real until it is in `docs/`, so until
the pull request that adds this file neither of them was a decision.

**THE FIRST WAS NOT A SILENCE, IT WAS A CONTRADICTION, AND THAT IS THE PART
WORTH GUARDING.** Section IX said, in as many words, "Adjacency is orthogonal in
lattice space". That excludes the rim's perimeter links, which is the reading the
owner rejected, and it was the ONLY definition of adjacency the document had. So
this file checks both directions: that the new definition is there, and that the
old sentence has not come back. A test for the new sentence alone would pass with
both of them present, which is the state the C++ was in for a whole slice --
`UCataclysmEmpireMap` defined adjacency twice, in ways that did not agree, and
nothing read either.

WHY THE MEASURED FIGURES ARE GUARDED TOO. The perimeter links are worth about ten
percentage points of relocation frequency, and that measurement is the whole
argument for the decision. A later edit that keeps the rule and drops the number
leaves the next reader with a rule and no reason, which is how a rule gets
"tidied" back out.

WHAT IS NOT CHECKED HERE. That anything implements a CHANCE of staying. Nothing
does. The owner ruled on 2026-09-06, verbatim "A chance each time", gave no
number, and `sim/analyse_quest_move_chance.py` is the dose-response curve that
exists to get one. Until it is answered the document deliberately says only
"may", and this file asserts that the document does not promise a probability it
cannot state.

THE IMPLEMENTATIONS are checked against each other by
`tools/tests/test_surge_port.py`, and against real campaigns by
`sim/tests/test_quest_relocation_is_adjacent.py` and
`Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity`. This file is about the
document.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"


def flattened(path: pathlib.Path) -> str:
    """One long line, so a sentence broken across a hard wrap still matches.

    EVERY FILE IN `docs/` IS HARD-WRAPPED, so a raw search for a multi-word
    phrase reports it absent when it is present and split across two lines.
    Three sessions on this project published a wrong negative result for want of
    this, which is why every check below goes through it and why
    `test_the_search_can_find_things` exists.

    The leading `> ` of a block quote is stripped first, the way
    `test_quest_objective_counts_are_stated.py` strips it: without that, a quoted
    sentence in `docs/DECISIONS.md` flattens with a stray `>` at every wrap and
    matches nothing.
    """
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    lines = path.read_text(encoding="utf-8").splitlines()
    unquoted = [line[2:] if line.startswith("> ") else line for line in lines]
    return " ".join(" ".join(unquoted).split())


@pytest.fixture(scope="module")
def document() -> str:
    return flattened(GDD)


@pytest.fixture(scope="module")
def decisions() -> str:
    return flattened(DECISIONS)


#: Phrases that are in the document for reasons that have nothing to do with
#: this issue. THE POSITIVE CONTROL on every search below: if one of these goes
#: missing the search is broken, not the design.
CONTROLS = (
    "Does not resolve",
    "Required to challenge the Cataclysm",
    "diamond lattice of 25 cities",
    "Distance is counted in rings",
    "Pauses city upgrades",
)


def test_the_search_can_find_things(document):
    """A guard that cannot fail is worthless, and so is one that cannot pass."""
    for phrase in CONTROLS:
        assert phrase in document, (
            f"the control phrase {phrase!r} is missing from the design "
            "document, so every other check in this file is searching a "
            "document it no longer understands and their failures mean "
            "nothing")

    assert "zzz not a phrase in any design document zzz" not in document, (
        "the flattened search matches a string that cannot be there")


class TestTheDocumentDefinesAdjacency:
    """The owner ruled, verbatim: **"Include the perimeter links"**."""

    def test_it_says_adjacency_is_more_than_the_lanes(self, document):
        assert ("Two cities are adjacent when the map links them, which is not "
                "only the lanes") in document, (
            "the design document no longer says what makes two cities "
            "adjacent. It is read by Quest dungeon relocation and by anything "
            "that says \"and its neighbours\"; the owner ruled on 2026-09-06, "
            "verbatim \"Include the perimeter links\". Issue #1324")

    def test_it_names_all_three_kinds_of_link(self, document):
        """One ring out, one ring in, and the rim. Naming only the rim would
        leave the ordinary case undefined, which is how the C++ ended up with
        two disagreeing definitions."""
        for phrase in ("the cities one ring further out",
                       "the cities one ring further in",
                       "the Outposts beside it along the rim"):
            assert phrase in document, (
                f"the design's definition of adjacency no longer names "
                f"{phrase!r}. All three links are adjacency in this project; "
                "see docs/DECISIONS.md, 2026-09-06")

    def test_the_orthogonal_only_definition_is_gone(self, document):
        """**THE HALF A TEST FOR THE NEW SENTENCE WOULD MISS.** This document
        said "Adjacency is orthogonal in lattice space" until 2026-09-06, and
        that sentence and the new one can both be present at once -- which is
        exactly the state `UCataclysmEmpireMap` was in, defining adjacency twice
        and contradicting itself, with nothing reading either.
        """
        assert "Adjacency is orthogonal" not in document, (
            "the design document says adjacency is orthogonal again. That "
            "excludes the rim's perimeter links, which is the reading the "
            "owner rejected on 2026-09-06, verbatim \"Include the perimeter "
            "links\". Lanes are orthogonal; adjacency is not. Issue #1324")

    def test_it_separates_adjacency_from_exposure(self, document):
        """The reason the two definitions could coexist for so long: the
        orthogonal one is about which cities SHIELD which, which is a different
        question. Saying so is what stops the rim links being read as a hole in
        the frontier rule."""
        assert "they take no part in exposure" in document, (
            "the design no longer says that the rim's perimeter links take no "
            "part in exposure. Without that they read as a way for a lane to "
            "run sideways, and the frontier rule stops making sense")

    def test_it_states_what_the_decision_is_worth(self, document):
        """The measurement is the argument. A rule with its reason deleted is
        the one that gets tidied away."""
        for figure in ("79.5%", "69.1%"):
            assert figure in document, (
                f"the design no longer states {figure}, which is what "
                "including the rim's perimeter links is worth: the share of "
                "quest timers with somewhere to go, with and without them. "
                "Measured over 926 quest timers in 30 campaigns")

        assert "could never move at all without them" in document, (
            "the design no longer says that a Quest dungeon on an intact rim "
            "could never move without the perimeter links. That is the "
            "strongest single reason for the decision")


class TestTheDocumentSaysWhatARelocatedDungeonKeeps:
    """The owner ruled, verbatim: **"Keeps everything, fix the size"**."""

    def test_it_names_the_three_things_that_survive_a_move(self, document):
        """Floor count, resolve timer, sub-type. Both implementations already
        kept all three and the document said nothing about any of them, so
        nothing would have been wrong to change them."""
        assert "A dungeon that moves is the same dungeon" in document, (
            "the design no longer says that a relocated Quest dungeon is the "
            "same dungeon. The owner ruled it on 2026-09-06, verbatim \"Keeps "
            "everything, fix the size\"")

        # **THE WHOLE SENTENCE, NOT THE THREE FRAGMENTS.** The first version of
        # this test asserted "**floor count**", "**resolve timer**" and
        # "**sub-type**" each appeared somewhere in the document. Two of the
        # three are unique; `**sub-type**` is NOT -- section VI uses it of a
        # weapon's physical sub-type, at line 3148. So deleting the promise from
        # section VIII left that assertion passing against a document that no
        # longer made it. Proved by breaking it: the run reported no failure.
        # Matching the sentence they sit in fixes that and is stronger anyway,
        # because it also catches the three being separated.
        assert ("It keeps its **floor count**, its **resolve timer** and its "
                "**sub-type**.") in document, (
            "the design no longer says a relocated Quest dungeon keeps its "
            "floor count, its resolve timer and its sub-type. All three were "
            "built before they were written down, which is the wrong way "
            "round; the owner ruled on 2026-09-06, verbatim \"Keeps "
            "everything, fix the size\"")

    def test_it_says_the_depth_comparison_does_not_move(self, document):
        """**THE FIX, AND THE ONE NOBODY WOULD GUESS.** A dungeon's damage
        scales by its depth against a typical dungeon of its kind on the tier
        that spawned it. Both implementations moved that tier with the dungeon
        while leaving the floor count alone, so the two halves of the comparison
        came from different specification rows.
        """
        assert ("on the\ntier of city that spawned it".replace("\n", " ")
                in document), (
            "the design no longer says that a dungeon's depth is measured "
            "against the tier of city that SPAWNED it. That is the half of "
            "\"keeps everything, fix the size\" that was a defect rather than "
            "a description: the recorded city size used to move with the "
            "dungeon while its floor count did not")

        assert "it is as deep as it always was" in document, (
            "the design no longer states the consequence in plain words. The "
            "rule without it reads as bookkeeping rather than as a promise to "
            "the player")

    def test_it_says_where_a_dungeon_may_move_to(self, document):
        """Exposed, not fallen, not the Pillar -- and that it stays when
        nothing qualifies. The staying case is the one that makes the design's
        own word "may" true today."""
        assert "a city a surge could have put a dungeon on" in document, (
            "the design no longer says which cities a Quest dungeon may move "
            "onto. It is the same filter a surge uses, and saying so is what "
            "keeps the two rules from drifting apart")

        assert "When no neighbour qualifies it stays where it is" in document, (
            "the design no longer says that a Quest dungeon with nowhere to go "
            "stays put")

    def test_it_does_not_promise_a_probability_it_cannot_state(self, document):
        """**WHAT IS DELIBERATELY ABSENT.** The owner ruled on 2026-09-06,
        verbatim "A chance each time", and gave no number. `CLAUDE.md` forbids
        inventing one, `sim/analyse_quest_move_chance.py` is the measurement
        that exists to get one, and nothing implements a chance yet.

        A document that stated a percentage here would be stating a decision
        nobody made, and a document that said "always moves" would contradict
        the ruling. It says "may", which is true of what is built and does not
        foreclose the ruling.
        """
        assert "**may move to an adjacent city**" in document, (
            "the design no longer says a Quest dungeon MAY move. The owner "
            "ruled on 2026-09-06, verbatim \"A chance each time\"; a document "
            "that promises it always moves contradicts that ruling")

        for invented in ("chance to move", "% chance of moving",
                         "always moves to an adjacent"):
            assert invented not in document, (
                f"the design states {invented!r}. The owner ruled \"A chance "
                "each time\" and gave no number; see "
                "sim/analyse_quest_move_chance.py, which is the curve that "
                "exists to get one. Nothing may be recorded before the run "
                "that proves it")


class TestTheDecisionsLogCarriesBothRulings:
    """`docs/DECISIONS.md` is where the reasoning lives; the design document
    states the rule. Both, because the design document is read by someone
    building against it and the log by someone asking why."""

    @pytest.mark.parametrize("ruling", [
        "Include the perimeter links",
        "Keeps everything, fix the size",
        "A chance each time",
    ])
    def test_the_owners_words_are_recorded_verbatim(self, decisions, ruling):
        assert ruling in decisions, (
            f"docs/DECISIONS.md no longer records the owner's ruling of "
            f"2026-09-06, verbatim “{ruling}”. Issue #1324")

    def test_the_unbuilt_ruling_is_recorded_as_unbuilt(self, decisions):
        """**NOTHING MAY BE RECORDED BEFORE THE RUN THAT PROVES IT.** "A chance
        each time" is ruled and not built. A log that recorded it beside the two
        that were built would read as three landed decisions."""
        assert "ruled and **not built**" in decisions, (
            "docs/DECISIONS.md no longer says that the move chance is ruled "
            "but not built. It is the one of the three rulings of 2026-09-06 "
            "that shipped no behaviour, and a reader has no way to tell them "
            "apart otherwise")

        # MATCHED WITHOUT CASE, because this log's emphasis style is a whole
        # sentence in capitals and the sentence carrying this one is emphasised.
        # A case-sensitive search here failed against the text it was written
        # for, which is a guard failing for a reason that has nothing to do with
        # what it guards.
        assert "nothing implements a chance of staying" in decisions.lower(), (
            "docs/DECISIONS.md no longer says that nothing implements a chance "
            "of staying. \"A chance each time\" was ruled on 2026-09-06 and no "
            "number was given; see sim/analyse_quest_move_chance.py")
