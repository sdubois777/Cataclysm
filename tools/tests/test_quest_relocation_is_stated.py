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

**AND TWO MORE RULINGS OF THE SAME DAY HAVE SINCE BEEN ADDED**, both of which
changed behaviour after this file was first written:

- **"A chance each time"**, answered with **"0.5"** once the curve was measured.
  `TestTheDocumentStatesTheMoveChance` checks the chance, that the draw is fresh
  each time, **and** the 38% of timers it works out to in play -- because a
  quarter of quest timers have nowhere adjacent to go regardless and a reader
  given only one of those two numbers is surprised by the other. Issue #1324.
- **"Check the limit on arrival too".** A relocating Quest dungeon carrying a
  Siege refuses a city that already has one. `TestTheDocumentStatesTheSiegeArrival
  Rule` checks the rule, that the dungeon is redirected rather than stopped, and
  that nothing else is refused a besieged city. Issue #1371.

**THIS FILE USED TO ASSERT THE OPPOSITE OF THE FIRST OF THOSE**, in
`test_it_does_not_promise_a_probability_it_cannot_state`: that the document
stated no percentage, because none had been decided. That guard was right when it
was written and is inverted rather than deleted now the ruling exists.

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

    def test_it_still_says_a_quest_dungeon_may_move(self, document):
        """The word the whole rule hangs off. A document that promised it
        always moves would contradict the owner's "a chance each time"."""
        assert "**may move to an adjacent city**" in document, (
            "the design no longer says a Quest dungeon MAY move. The owner "
            "ruled on 2026-09-06, verbatim \"A chance each time\"; a document "
            "that promises it always moves contradicts that ruling")


class TestTheDocumentStatesTheMoveChance:
    """**THE OWNER RULED THE MECHANIC AND THEN THE NUMBER**, both on 2026-09-06:
    verbatim *"A chance each time"*, and then verbatim *"0.5"*. Issue
    [#1324](https://github.com/sdubois777/Cataclysm/issues/1324).

    **THIS CLASS REPLACES `test_it_does_not_promise_a_probability_it_cannot_
    state`**, which asserted the document stated no percentage *because none had
    been decided*. Its own docstring said the document "does not foreclose the
    ruling"; the ruling has since arrived, so the guard is inverted rather than
    deleted.

    **BOTH NUMBERS ARE CHECKED AND THAT IS THE POINT OF THE CLASS.** "A chance
    of 0.5" and "moves on 38% of its timers" are both true, because about a
    quarter of quest timers fire with nowhere adjacent to go whatever the coin
    says. A document stating only one of them surprises the reader with the
    other, which is what the owner asked to be avoided in as many words.
    """

    def test_it_states_the_chance(self, document):
        assert "takes it **half the time**" in document, (
            "the design no longer states how likely a Quest dungeon with "
            "somewhere to go is to move. The owner ruled on 2026-09-06, "
            "verbatim \"A chance each time\", and chose 0.5; see "
            "config.quest_move_chance and "
            "UCataclysmSurgeScheduler::QuestMoveChance. Issue #1324")

    def test_it_says_the_draw_is_fresh_every_time(self, document):
        """"A chance each time" is the ruling's own wording, and a reader who
        took it for a pity timer -- more likely after each stay -- would have a
        different game in mind. Neither implementation has any memory of it."""
        assert "The draw is fresh on every timer" in document, (
            "the design no longer says the move chance is drawn fresh on every "
            "timer. Nothing in either implementation remembers a previous "
            "stay, so a document implying otherwise promises a mechanic that "
            "does not exist")

    def test_it_states_the_consequence_in_timers_as_well(self, document):
        """**THE HALF A READER WOULD OTHERWISE BE SURPRISED BY.** The owner
        asked for it by name: "'a chance of 0.5' and 'moves 37% of the time'
        are both true and a reader will otherwise be surprised by one of
        them".

        **THE NUMBER IS 38 AND NOT THE 37 THE RULING RECORDED**, and that is a
        change in the game rather than a correction to the ruling. The 37% was
        measured before the active Cataclysm count was tied to the difficulty
        tier and before the Cataclysm dungeon opened at half of them; both
        shorten a campaign, and a shorter campaign spends proportionally more
        of itself in territory a Quest dungeon can still move through.
        Re-measured on the shipped code over four disjoint blocks of 1,000
        campaigns it is 38.2% overall, 37.6% to 38.7% by block. The coin is
        untouched: take-up over the timers that had a choice is 49.8%.
        """
        assert "38% of its timers" in document, (
            "the design states the coin without stating what it works out to "
            "in play. About a quarter of quest timers have nowhere adjacent to "
            "go regardless, so a Quest dungeon moves on roughly 38% of its "
            "timers rather than 50%. Both numbers are true and the owner asked "
            "for both to be stated")

    def test_it_states_the_conditions_the_figure_was_measured_at(self, document):
        """A campaign figure without its conditions is not a figure, and this
        project has paid for that. The share of timers with somewhere to go was
        measured at the balance report's settings and not at the model's
        defaults, where it is higher.

        **AND THE RANGE IS FOUR BLOCKS RATHER THAN TWO**, because two disjoint
        blocks give one difference and not a spread. The document says so in as
        many words.
        """
        assert "75.2% to 77.5%" in document, (
            "the design no longer states the measured share of quest timers "
            "that had somewhere adjacent to go, which is where the 38% comes "
            "from. Without it the 38% is an assertion rather than a result")

        assert "four disjoint blocks of" in document, (
            "the design states a measured range without saying how many "
            "disjoint seed blocks it spans. Two blocks give a difference and "
            "not a spread, and this project has read one as the other")

        assert "static surges of five dungeons every 120 days" in document, (
            "the design states a measured share without the conditions it was "
            "measured under. Surge size, difficulty tier and policy all move "
            "it, and the same run at the model's defaults gives a different "
            "number")


class TestTheDocumentStatesTheSiegeArrivalRule:
    """**THE OWNER RULED, VERBATIM: "Check the limit on arrival too".** Issue
    [#1371](https://github.com/sdubois777/Cataclysm/issues/1371).

    The Siege row says "Max 1 per city". Both implementations enforced that when
    a dungeon was created and neither enforced it when one moved, so a Quest
    dungeon that had rolled Siege could walk onto a besieged city.

    **THE NARROWNESS IS GUARDED TOO.** The cap counts Sieges and not dungeons,
    and a document that said a besieged city refuses every dungeon would
    describe a much larger rule than the one ruled -- and one neither
    implementation obeys.
    """

    def test_it_says_a_wandering_siege_refuses_a_besieged_city(self, document):
        assert ("A Quest dungeon carrying a Siege will not move onto a city "
                "that already has one") in document, (
            "the design no longer says that a relocating Siege refuses a city "
            "that already has one. The owner ruled it on 2026-09-06, verbatim "
            "\"Check the limit on arrival too\"; issue #1371")

    def test_it_says_the_cap_is_about_standing_and_not_only_creation(
            self, document):
        """The sentence that makes the Sub-Types table's "Max 1 per city" mean
        the same thing everywhere rather than only at spawn. Without it the two
        readings are both defensible, which is the state that produced the
        hole."""
        assert "governs where a\nSiege may **stand**".replace("\n", " ") \
            in document, (
            "the design no longer says that \"Max 1 per city\" is about where a "
            "Siege may stand rather than only where one may be created. That "
            "sentence is the whole difference between the rule the owner chose "
            "and the one they rejected")

    def test_it_says_the_dungeon_is_redirected_rather_than_stopped(
            self, document):
        assert "takes\none of its other neighbours instead".replace("\n", " ") \
            in document, (
            "the design no longer says that a refused Siege takes another "
            "neighbour. The owner ruled it must \"pick another adjacent city\"; "
            "a rule that merely cancelled the move is a different one")

        assert "stays where it is when none of them is\nfree".replace(
            "\n", " ") in document, (
            "the design no longer says what happens when every neighbour is "
            "besieged. The owner named that outcome: \"or stay where it is if "
            "there is none\"")

    def test_it_says_nothing_else_is_refused(self, document):
        """**THE NARROW READING, STATED.** A check written against the
        destination rather than against what the mover carries would stop every
        dungeon entering a besieged city. Both implementations have a control
        test for exactly this, and the document has to agree with them."""
        assert "**Nothing else is refused a besieged city.**" in document, (
            "the design no longer says that only a Siege is refused a besieged "
            "city. The cap counts Sieges, not dungeons; without this sentence "
            "the rule reads as a general blockade")


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

    @pytest.mark.parametrize("ruling", [
        "0.5",
        "Check the limit on arrival too",
    ])
    def test_the_later_rulings_of_the_same_day_are_recorded_too(
            self, decisions, ruling):
        """The two the owner gave after the curve and after issue #1371 was
        opened. Both changed behaviour, so both belong in the log beside the
        three above."""
        assert ruling in decisions, (
            f"docs/DECISIONS.md no longer records the owner's ruling of "
            f"2026-09-06, verbatim “{ruling}”. Issues #1324 and #1371")

    def test_the_superseded_not_built_entry_says_it_was_superseded(
            self, decisions):
        """**THIS TEST USED TO ASSERT THE OPPOSITE**, and it said so: it
        required the log to record the move chance as "ruled and **not built**",
        because nothing implemented it. It is built now, and the paragraph that
        said otherwise is the one thing on this page that would be actively
        wrong if it were left alone.

        **THE PARAGRAPH IS KEPT RATHER THAN DELETED.** It is the record of a day
        on which the ruling really was unbuilt, which is worth having; what it
        must not do is read as current. So the guard checks that the sentence
        marking it superseded is there, not that the old wording is gone.
        """
        assert "kept as the record of it" in decisions.lower(), (
            "docs/DECISIONS.md still records the move chance as ruled and not "
            "built with nothing saying that has changed. It was ruled 0.5 on "
            "2026-09-06 and is built in both implementations; a reader taking "
            "that paragraph as current would believe the game always moves a "
            "Quest dungeon that can move")

    def test_the_measurement_keeps_its_conditions(self, decisions):
        """**A SIM CAMPAIGN FIGURE WITHOUT ITS CONDITIONS IS NOT A FIGURE**, and
        the curve behind 0.5 is the sort that gets quoted onward. Surge size
        alone moves it: the run used 5 dungeons a surge and the model's own
        default is 4."""
        for condition in ("static surges every 120 days for **5**",
                          "10,000 campaigns in all"):
            assert condition in decisions, (
                f"docs/DECISIONS.md no longer states {condition!r} beside the "
                "move-chance curve. Every figure in that table depends on it, "
                "and a figure quoted without its conditions is not a figure")

    def test_it_says_balance_did_not_choose_the_number(self, decisions):
        """**THE MOST MISREADABLE THING ON THE PAGE.** A table of doses beside a
        chosen dose reads as a measurement that picked a winner. It did not:
        every response was flat and the number came from the ruling. A later
        session that believed otherwise would re-derive the curve to defend it,
        which issue #1324 asks in as many words that nobody does.
        """
        assert "No dose is significant" in decisions, (
            "docs/DECISIONS.md no longer says that no dose was significant. "
            "Without it the table reads as balance having chosen 0.5, and the "
            "next session to touch the constant will look for a measurement "
            "that does not exist")

        assert "p = 0.08" in decisions, (
            "docs/DECISIONS.md no longer states the p-value behind \"no dose "
            "is significant\". The claim is the whole reason the number is a "
            "feel decision rather than a measured one")
