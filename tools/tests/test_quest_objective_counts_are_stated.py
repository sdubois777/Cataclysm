"""The eight quest objective counts, and the rule that a Quest dungeon moves.

WHY THIS FILE EXISTS. Two numbers the project owner supplied on 2026-09-06 --
Pestilence 5 and The Void 5 -- lived only in a comment on issue #1324 for the
rest of that day. `CLAUDE.md` says a design decision is not real until it is in
`docs/`, so until the pull request that adds this file they were not decisions
at all. Issue #1357 named that as one of the two things blocking the Quest
dungeon spawn-rate rule, and slice 5 of #1324 counts objectives and cannot be
built without them.

THE SIX THAT WERE ALREADY THERE ARE GUARDED TOO, and that is the point rather
than padding. The two new numbers were derived from the six -- Pestilence 5
because Famine is 5 and shares the property that its penalty worsens over time
-- so a Famine quietly becoming 8 would leave Pestilence's 5 resting on an
argument that no longer holds while nothing failed. All eight move together or
none of them do.

WHAT IS DELIBERATELY ALSO CHECKED: that the section still says what it does NOT
fix. How often a Quest dungeon spawns is a separate rule, the owner ruled it
should depend on the Cataclysm rather than be one number, and it is not derived.
A design that stated eight counts and said nothing about the spawn rate would
read as a complete specification of the quest line, which it is not. That is the
failure `test_cataclysm_order_and_population_experience_are_stated.py` was
written to prevent and this file follows it.

WHAT IS NOT CHECKED HERE. That anything implements a per-Cataclysm count. Nothing
does: `sim/cataclysm_sim/config.py` requires a flat 8 for every Cataclysm and its
own comment admits that is the midpoint of the stated numbers rather than a
ruling, and the game has no notion of which Cataclysm is running at all. This
file is about the document.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"


def flattened(path: pathlib.Path) -> str:
    """One long line, so a sentence broken across a hard wrap still matches.

    The leading `> ` of a block quote is stripped first, for the same reason
    `test_cataclysm_order_and_population_experience_are_stated.py` strips it:
    without that, a quoted sentence in `docs/DECISIONS.md` flattens with a stray
    `>` at every wrap and matches nothing, which reads as the quote being absent
    rather than as the search being wrong.
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


# The whole set, and the phrase in section XI's prose that states each one.
# The count and the prose are checked separately on purpose: the summary table
# and the section it summarises are two statements of one fact, which is the
# duplication `CLAUDE.md` warns about, so both have to move together.
OBJECTIVES = {
    "Demonic": (10, "seal 10 Rifts to challenge the enemy capital"),
    "Death": (5, "collect 5 Seeds of Undeath and unlock the enemy capital"),
    "War": (10, "collect 10 Essences of War from quest dungeons"),
    "Pestilence": (5, "defeat 5 quest dungeons to progress toward a vaccine"),
    "Famine": (5, "defeat 5 quest dungeons to unlock the enemy capital"),
    "Celestial": (10, "seal 10 gates by destroying their Heavenly Cores"),
    "Chaos": (8, "reactivate 8 Pillars of Order"),
    "The Void": (5, "complete 5 Sealing Rituals"),
}


class TestEveryCataclysmStatesAnObjectiveCount:
    @pytest.mark.parametrize("name", sorted(OBJECTIVES))
    def test_the_prose_states_the_count(self, document, name):
        """The sentence a player-facing reader meets, in section XI."""
        count, phrase = OBJECTIVES[name]
        assert phrase in document, (
            f"docs/Cataclysm_GDD_v2.md no longer says {name!r} asks for "
            f"{count} objectives. The expected wording is {phrase!r}. Six of "
            "these eight were in the document already and two -- Pestilence "
            "and The Void -- were supplied by the project owner on 2026-09-06 "
            "and derived from the six, so a change to any of them needs the "
            "other seven rechecked. Issues #1324 and #1357.")

    @pytest.mark.parametrize("name", sorted(OBJECTIVES))
    def test_the_summary_table_states_the_same_count(self, document, name):
        """The one place all eight are stated together, which is what slice 5
        of issue #1324 has to read. A table that drifted from the prose above
        it would be worse than no table."""
        count, _ = OBJECTIVES[name]
        row = f"| {name} | {count} |"
        assert row in document, (
            f"the objective-count table in docs/Cataclysm_GDD_v2.md no longer "
            f"has the row {row!r}. All eight counts are stated together there "
            "because slice 5 of issue #1324 counts objectives and needs the "
            "whole set in one place.")

    def test_the_two_the_owner_supplied_are_the_two_that_were_missing(
            self, document):
        """A guard against the table being kept and the prose losing the two
        numbers that were added to it. Before 2026-09-06 the Pestilence and
        Void sections named no number at all, and that is the exact state this
        change ended.

        SEARCHED INSIDE SECTION XI AND NOT THE WHOLE DOCUMENT, because
        `## **Pestilence**` appears twice: once in the Cataclysm roster in
        section III and once here. The first version of this test read the
        roster entry, which mentions the Plague Lord and no number, and failed
        for the right reason on the wrong text -- so it would have kept failing
        after the count was added.
        """
        eleven = re.search(
            r"# \*\*XI\. Cataclysm Quest Mechanics\*\*(.*?)"
            r"# \*\*XII\. Progression System\*\*", document)

        assert eleven is not None, (
            "docs/Cataclysm_GDD_v2.md no longer has a section XI running to "
            "section XII. Every count in this file is read out of it.")

        for name in ("Pestilence", "The Void"):
            section = re.search(
                r"## \*\*" + re.escape(name) + r"\*\*(.*?)(?=## \*\*|$)",
                eleven.group(1))
            assert section is not None, (
                f"docs/Cataclysm_GDD_v2.md no longer has a section XI heading "
                f"for {name}.")
            assert re.search(r"\b5\b", section.group(1)), (
                f"the {name} section states no objective count again. The "
                "project owner supplied 5 on 2026-09-06 and that number is "
                "derivable from nothing, so losing it loses the decision. "
                "Issue #1324.")


class TestTheSectionSaysWhatOneObjectiveIs:
    def test_it_says_one_cleared_quest_dungeon_is_one_objective(self, document):
        """Without this the varying names -- seals, seeds, essences, cores,
        rituals -- read as five different mechanics rather than one."""
        assert "One cleared quest dungeon is one objective." in document, (
            "docs/Cataclysm_GDD_v2.md no longer says one cleared quest dungeon "
            "is one objective. The project owner ruled that on 2026-09-06, "
            "verbatim \"Yes -- one dungeon, one objective\", and without it the "
            "seals, seeds and rituals the sections name read as separate "
            "systems. Issue #1324.")

    def test_it_says_the_varying_counts_are_deliberate(self, document):
        """A table of 10, 5, 10, 5, 5, 10, 8, 5 with no note reads like an
        oversight, and the obvious tidy is to make them all the same."""
        assert ("The count differs per Cataclysm and that is deliberate."
                in document), (
            "the objective-count table no longer says the varying counts are "
            "deliberate. The owner was asked whether to keep them or pick one "
            "number and answered \"Keep the per-Cataclysm numbers\". "
            "Issue #1324.")


class TestTheSectionStillSaysWhatItDoesNotFix:
    def test_it_says_the_spawn_rate_is_not_derived_here(self, document):
        assert ("How often a Quest dungeon spawns is a separate rule"
                in document), (
            "docs/Cataclysm_GDD_v2.md states the eight objective counts "
            "without saying that the Quest dungeon spawn rate is a separate, "
            "underived rule. The owner ruled it \"should depend on the "
            "Cataclysm\"; issue #1357 owns it and is blocked on the empire "
            "layer having no Cataclysm identity. Deleting that paragraph turns "
            "an unfinished specification into one that looks finished.")


class TestTheDecisionsLogCarriesTheOwnersWords:
    def test_it_quotes_the_ruling_to_keep_the_per_cataclysm_numbers(
            self, decisions):
        assert ("Keep the per-Cataclysm numbers; I will supply the two missing"
                in decisions), (
            "docs/DECISIONS.md no longer quotes the project owner's ruling of "
            "2026-09-06 on the objective counts. The verbatim wording is the "
            "evidence that the rule was decided rather than inferred. "
            "Issue #1324.")

    def test_it_quotes_the_void_answer(self, decisions):
        """The Void's was answered outside the options offered, which is
        exactly the kind of answer that gets paraphrased into something it did
        not say."""
        assert "go ahead with 5" in decisions, (
            "docs/DECISIONS.md no longer quotes the owner's answer for The "
            "Void. Issue #1324.")

    def test_it_records_why_pestilence_is_five(self, decisions):
        """A number with no argument beside it is re-opened by the next person
        who wants a rounder set."""
        assert ("Pestilence at 5** matches Famine, which is also 5" in decisions
                or "Pestilence at 5 matches Famine, which is also 5"
                in decisions), (
            "docs/DECISIONS.md no longer records why Pestilence asks for 5: it "
            "matches Famine, which is also 5 and shares the property that the "
            "penalty worsens the longer quest dungeons stand. Issue #1324.")

    def test_it_records_why_the_void_is_five(self, decisions):
        assert ("the only unrecoverable loss in the game" in decisions), (
            "docs/DECISIONS.md no longer records why The Void asks for 5: a "
            "Void-erased city is permanently removed, which is the only "
            "unrecoverable loss in the game, so a short campaign limits how "
            "much is destroyed for good. Issue #1324.")

    def test_it_records_the_argument_the_owner_rejected(self, decisions):
        """The Void was argued for a HIGHER count as the last Cataclysm added,
        and the owner corrected the premise rather than the number. Recording
        only the accepted argument invites the rejected one to be made again."""
        assert "There is no last one" in decisions, (
            "docs/DECISIONS.md no longer records that the argument for giving "
            "The Void a higher count -- that it is the last Cataclysm added -- "
            "was rejected because the order is randomised per character. "
            "Issues #1324 and #1338.")

    def test_it_says_the_model_still_requires_a_flat_eight(self, decisions):
        """The document now states eight different counts and every balance
        figure this project holds was measured against one. A reader who does
        not know that will read a sweep result as evidence about a per-Cataclysm
        game."""
        assert "The simulation still requires a flat 8" in decisions, (
            "docs/DECISIONS.md no longer records that the simulation requires "
            "a flat 8 objectives for every Cataclysm, so every measured figure "
            "predates the per-Cataclysm counts. Issue #1357.")


class TestTheDecisionsLogCarriesTheRelocationRuling:
    """The other half of what landed with these counts: issue #1324 slice 4."""

    def test_it_quotes_the_owners_words(self, decisions):
        assert "Adjacent, and fix the simulation" in decisions, (
            "docs/DECISIONS.md no longer quotes the project owner's ruling of "
            "2026-09-06 that a Quest dungeon moves to an adjacent city and "
            "that the simulation's move-anywhere rule is the defect. "
            "Issue #1324 slice 4.")

    def test_it_records_that_adjacency_had_to_be_read_off_the_map(
            self, decisions):
        """The design never defines adjacency and the map defines it twice, in
        ways that do not agree. Which reading was taken is a choice, and a
        choice that is not recorded is indistinguishable later from a rule that
        was stated."""
        assert "Every city the map links this one to" in decisions, (
            "docs/DECISIONS.md no longer records what \"adjacent\" was taken "
            "to mean. UCataclysmEmpireMap's class comment says adjacency is "
            "orthogonal and excludes the rim's perimeter links; "
            "FCataclysmCity::Perimeter's own comment says those links exist "
            "for adjacency effects. The second reading was taken. Issue #1324.")

    def test_it_records_what_the_choice_is_worth(self, decisions):
        """A judgement recorded without its measurement is an opinion."""
        assert "69.1% of the time under the orthogonal-only reading" in decisions, (
            "docs/DECISIONS.md no longer carries the measurement behind the "
            "adjacency reading. It is worth about ten percentage points of "
            "relocation frequency, and without the figure the choice cannot be "
            "argued with. Issue #1324.")

    def test_it_says_whether_movement_is_certain_is_still_open(self, decisions):
        """The design says a Quest dungeon "MAY move". The owner was offered a
        chance-based variant and did not take it, but did not say movement is
        certain either. What was built is a reading, and it is labelled one."""
        assert ("Whether a Quest dungeon that has somewhere to go always goes"
                in decisions), (
            "docs/DECISIONS.md no longer records that the design's \"may "
            "move\" is unsettled. What is built moves it whenever an adjacent "
            "exposed city exists, which satisfies \"may\" through the map "
            "rather than through a die roll -- a reading, not a ruling. "
            "Issue #1324.")

    def test_it_says_the_balance_figures_predate_the_rule(self, decisions):
        assert ("Every figure this project holds was measured with a Quest "
                "dungeon relocating to anywhere on the map." in decisions), (
            "docs/DECISIONS.md no longer records that the sweeps have not been "
            "re-run against the adjacency rule. A dungeon's position decides "
            "which cities it threatens, so the figures moved. Issue #1324.")
