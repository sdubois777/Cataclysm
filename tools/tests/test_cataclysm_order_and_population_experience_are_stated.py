"""Two rules the project owner stated on 2026-09-06 that nothing else checks.

WHY THIS FILE EXISTS. Both rules were stated by the owner and written down
nowhere, which `CLAUDE.md` says means they were not decisions at all: a design
decision is not real until it is in `docs/`. Issues #1338 and #1348 recorded
that, and the pull request that adds this file wrote both into
`docs/Cataclysm_GDD_v2.md` and `docs/DECISIONS.md`.

- **The order the Cataclysms are added in is randomised for each new
  character**, so Void is not necessarily the last one added. Issue #1338.
- **The population the empire keeps alive scales what a defeated dungeon is
  worth** in empire upgrade points. Issue #1348.

A rule stated in the design document and implemented nowhere is the easiest kind
to lose. Nothing fails when it is deleted, the next person to read the section
sees no sign it was ever agreed, and in both of these cases the code currently
does something else -- `sim/cataclysm_sim/engine.py` takes a fixed prefix of a
fixed roster, and nothing anywhere scales empire points by population. So a
reader comparing the document against the code and finding no rule would
reasonably conclude the code is right. These tests fail if either sentence goes
away.

WHAT IS DELIBERATELY ALSO CHECKED: that each rule still says what it does NOT
fix. Neither statement is a formula. #1338 does not say whether every ordering
is equally likely or whether the order is drawn once per character or once per
run; #1348 does not say whether the bonus is a fraction or an absolute count,
per dungeon or per run, floored or capped. `CLAUDE.md` requires the genre to be
researched and the sources named before any formula is proposed, and that
research has not been done. If the "what the rule does not fix" paragraphs are
deleted without those questions being answered, the document starts reading as
a complete specification of something that was never specified, which is worse
than the silence it replaced.

WHAT IS NOT CHECKED HERE. That anything implements either rule. Nothing does
yet, in the game or in the simulation, and both halves are open on their issues.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"


def flattened(path: pathlib.Path) -> str:
    """One long line, so a sentence broken across a hard wrap still matches.

    The leading `> ` of a block quote is stripped first. Without that, a quoted
    sentence in `docs/DECISIONS.md` flattens with a stray `>` at every wrap and
    matches nothing -- which reads as the quote being absent rather than as the
    search being wrong.
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


class TestTheCataclysmOrderIsRandomisedPerCharacter:
    """Issue #1338."""

    def test_the_design_says_the_order_is_drawn_per_character(self, document):
        assert ("The order the Cataclysms are added in is randomised for each "
                "new character.") in document, (
            "docs/Cataclysm_GDD_v2.md no longer says the order the Cataclysms "
            "are added in is randomised per character. That is the rule the "
            "project owner stated on 2026-09-06, and the simulation's fixed "
            "roster contradicts it, so the document is the only place it "
            "exists. Issue #1338.")

    def test_it_says_void_is_not_the_last_one_added(self, document):
        """The specific misreading the owner was correcting."""
        assert "Void is not necessarily the last one added" in document, (
            "the design no longer says Void is not necessarily the last "
            "Cataclysm added. That sentence is what the owner's statement was "
            "correcting, and without it a reader meeting the fixed roster in "
            "sim/cataclysm_sim/config.py has nothing to contradict it. "
            "Issue #1338.")

    def test_it_says_why_the_order_is_randomised(self, document):
        """A rule with no reason beside it reads as arbitrary and gets tidied
        away by the next person who wants a deterministic order."""
        assert "variety, and so that the opening cannot be solved" in document, (
            "the design no longer says why the Cataclysm order is randomised. "
            "The reason is half the rule: variety, and stopping a "
            "speed-running opening from being solved once and repeated.")

    def test_it_still_says_which_parts_are_not_fixed(self, document):
        assert ("It says the order is drawn per character and no more."
                in document), (
            "the design no longer says what the randomisation rule leaves "
            "unfixed. Nothing states whether every ordering is equally likely, "
            "or whether the order is drawn once per character or once per run. "
            "Deleting that paragraph turns an unfinished rule into one that "
            "looks finished. Issue #1338.")

    def test_the_decisions_log_carries_the_owners_words(self, decisions):
        assert ("Every time a player starts a new character, the order in which "
                "cataclysms are added per tier is randomized.") in decisions, (
            "docs/DECISIONS.md no longer quotes the project owner's statement "
            "of 2026-09-06. The verbatim wording is the evidence that the rule "
            "was decided rather than inferred. Issue #1338.")


class TestPopulationKeptAliveScalesEmpireProgression:
    """Issue #1348."""

    def test_the_design_says_population_scales_a_dungeon_defeat(self, document):
        assert ("The population the empire keeps alive scales what a defeated "
                "dungeon is worth.") in document, (
            "docs/Cataclysm_GDD_v2.md no longer says the surviving population "
            "scales what a defeated dungeon is worth in empire upgrade points. "
            "The Empire-Wide Upgrades section said points come from defeating "
            "dungeons and nothing more until 2026-09-06. Issue #1348.")

        assert ("the more population a player maintains, the more empire "
                "upgrade points a dungeon defeat awards") in document, (
            "the design states the rule as a heading without stating the rule. "
            "The direction -- more population, more points -- is the part a "
            "reader needs. Issue #1348.")

    def test_it_says_the_reward_is_paid_between_runs(self, document):
        """The sentence that stops a within-run measurement being read as a
        verdict on defending. Issue #1327 measured exactly that and the reading
        it produced was too broad by this much."""
        assert ("it is paid between runs rather than inside one" in document), (
            "the design no longer says the population reward is paid between "
            "runs rather than inside one. Without it, a measurement covering a "
            "single campaign looks like it settles whether defending is worth "
            "anything, and it cannot. Issues #1348 and #1327.")

    def test_the_summary_section_agrees_with_the_full_rule(self, document):
        """The Roguelike Meta Progression section restates the same rule. Two
        statements of one fact is the duplication CLAUDE.md names as a failure
        this project has already had twice, so both have to move together."""
        assert ("How many points a dungeon defeat is worth depends on how much "
                "population is still alive") in document, (
            "the Roguelike Meta Progression section states that empire upgrade "
            "points are earned by defeating dungeons without mentioning that "
            "the surviving population scales them. It is the second statement "
            "of the same rule and it is now incomplete. Issue #1348.")

    def test_it_still_says_which_parts_are_not_fixed(self, document):
        assert "The direction is settled and the shape is not." in document, (
            "the design no longer says what the population rule leaves "
            "unfixed. Whether the bonus is a fraction of maximum or an "
            "absolute count, per dungeon or per run, floored or capped, are "
            "all open and none of them may be invented -- CLAUDE.md requires "
            "the genre to be researched and the sources named first. "
            "Issue #1348.")

    def test_the_decisions_log_carries_the_owners_words(self, decisions):
        assert ("the more population you maintain, the more experience you get "
                "for your empire whenever you defeat a dungeon") in decisions, (
            "docs/DECISIONS.md no longer quotes the project owner's statement "
            "of 2026-09-06. Issue #1348.")

    def test_the_decisions_log_records_that_a_reading_was_chosen(self,
                                                                decisions):
        """The owner said "experience"; the design has one currency, the empire
        upgrade point. Writing the minimal reading into the document was a
        choice, and a choice that is not recorded is indistinguishable later
        from a rule that was stated."""
        assert ("Whether \"experience\" is a second quantity that converts into "
                "empire upgrade points, or another word for the points "
                "themselves") in decisions, (
            "docs/DECISIONS.md no longer records that the design was written "
            "as the minimal reading of the owner's words. Issue #1348.")


def test_the_decisions_log_corrects_the_claim_that_the_model_models_nothing(
        decisions):
    """Issue #1348 as filed says the simulation "has no concept of empire
    progression at all". It does: `empire_points_per_dungeon` in
    `sim/cataclysm_sim/config.py`, accumulated in `engine.py` and reported on
    the campaign result. What it does not do is scale that by population.

    The correction is in the log rather than only in a comment on the issue,
    because the wrong version is the one a reader meets first."""
    assert "what it does not do is scale them by" in decisions.lower(), (
        "docs/DECISIONS.md no longer corrects issue #1348's claim that the "
        "simulation models no empire progression. It models the points and not "
        "the population scaling, and the difference decides how much work the "
        "code half is.")
