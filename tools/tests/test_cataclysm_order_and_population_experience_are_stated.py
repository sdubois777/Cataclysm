"""Two rules the project owner stated on 2026-09-06 that nothing else checks.

WHY THIS FILE EXISTS. Both rules were stated by the owner and written down
nowhere, which `CLAUDE.md` says means they were not decisions at all: a design
decision is not real until it is in `docs/`. Issues #1338 and #1348 recorded
that, and the pull request that adds this file wrote both into
`docs/Cataclysm_GDD_v2.md` and `docs/DECISIONS.md`.

- **The order the Cataclysms are added in is randomised for each new
  character**, so Void is not necessarily the last one added. Issue #1338.
- **The draw is per character and it is held, so a failed run replays the same
  tier against the same Cataclysms.** The owner ruled that later the same day,
  after this file's first version recorded the question as open. Issue #1338.
- **The population the empire keeps alive scales what a defeated dungeon is
  worth** in empire upgrade points. Issue #1348.

A rule stated in the design document and implemented nowhere is the easiest kind
to lose. Nothing fails when it is deleted, the next person to read the section
sees no sign it was ever agreed, and when this file was written the code did
something else in both cases -- `sim/cataclysm_sim/engine.py` took a fixed
prefix of a fixed roster, and nothing anywhere scaled empire points by
population. So a reader comparing the document against the code and finding no
rule would reasonably conclude the code is right. These tests fail if either
sentence goes away.

BOTH HAVE SINCE BEEN IMPLEMENTED IN THE MODEL, and that does not retire these
tests. The document is still the only place the rules are stated as rules; the
code can only ever show what it happens to do. #1338's half is guarded by
`sim/tests/test_the_cataclysms_drawn_belong_to_the_character.py` and #1348's by
`sim/tests/test_population_scales_empire_points.py`.

WHAT IS DELIBERATELY ALSO CHECKED: that each rule still says what it does NOT
fix. #1338 does not say whether every ordering is equally likely or whether any
pairing is constrained. #1348's shape was open in exactly the same way until the
owner ruled it on 2026-09-06 -- a fraction of maximum, per dungeon defeated,
linear, no floor, no cap, every type -- and the tests for it changed direction
that day: they now assert the formula is stated and that the sentence calling
the shape unsettled is gone. What #1348 still leaves open is the owner's own
sixth question, whether keeping cities should ALSO advance the win condition
inside a run. If the "what the rule does not fix" paragraphs are deleted without
those remaining questions being answered, the document starts reading as a
complete specification of something that was never specified, which is worse
than the silence it replaced. The reverse failure costs just as much: a settled
question still listed as open is read as work to do.

ONE OF THOSE OPEN QUESTIONS WAS ANSWERED, and the tests for it are the third
class below. When this file was first written the document said both "each run
begins with a randomly selected Cataclysm" and that the order is drawn "every
time a player starts a new character", and a character plays many runs. The
owner settled it later the same day in favour of the per-character reading, and
went further: a failed run replays the same tier against the same Cataclysms.
The first sentence was reworded and the Ending a Run section, which promised
only the same NUMBER of simultaneous Cataclysms, now promises the same ones.

WHAT IS NOT CHECKED HERE. That the GAME implements any of this; nothing in the
C++ does yet, for either rule. Both simulation halves have landed and are
guarded in `sim/tests/` rather than here -- this file is about the document.
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
            "or whether any pairing is constrained. Deleting that paragraph "
            "turns an unfinished rule into one that looks finished. "
            "Issue #1338.")

    def test_the_decisions_log_carries_the_owners_words(self, decisions):
        assert ("Every time a player starts a new character, the order in which "
                "cataclysms are added per tier is randomized.") in decisions, (
            "docs/DECISIONS.md no longer quotes the project owner's statement "
            "of 2026-09-06. The verbatim wording is the evidence that the rule "
            "was decided rather than inferred. Issue #1338.")


class TestTheDrawIsHeldAndAFailedRunReplaysTheSameCataclysms:
    """Issue #1338, the owner's second ruling of 2026-09-06.

    THE SENTENCE THIS REPLACED IS WHY THIS CLASS EXISTS. Game Start read "Each
    run begins with a randomly selected Cataclysm", which describes a fresh
    draw at the start of every run. Under the ruling that is wrong, and it is
    wrong in a way that is invisible unless something checks: a reader meeting
    it takes the whole rule to be per-run, and a per-run game is a different
    game -- retrying a failed run would be a fresh problem each time rather
    than a second attempt at the same one.
    """

    def test_the_design_says_the_selection_is_made_at_character_creation(
            self, document):
        assert ("A character's first Cataclysm is selected when the character "
                "is created, and it stays selected.") in document, (
            "docs/Cataclysm_GDD_v2.md no longer says the Cataclysm is selected "
            "at character creation and held. That is the correction the owner "
            "ruled on 2026-09-06; the sentence it replaced said each RUN begins "
            "with a randomly selected Cataclysm. Issue #1338.")

    def test_the_old_per_run_sentence_is_gone(self, document):
        """The specific wording the ruling made wrong. Adding the new rule
        while leaving the old sentence in place would leave the section saying
        both, which is the state the ruling was called for to end."""
        assert "Each run begins with a randomly selected Cataclysm" not in document, (
            "docs/Cataclysm_GDD_v2.md still contains 'Each run begins with a "
            "randomly selected Cataclysm'. The owner ruled on 2026-09-06 that "
            "the draw is made once per character and held, so that sentence "
            "describes a game the design no longer specifies. Issue #1338.")

    def test_the_design_states_the_failed_run_replay_rule(self, document):
        assert ("A failed run replays the same tier against the same "
                "Cataclysms.") in document, (
            "docs/Cataclysm_GDD_v2.md no longer states that a failed run "
            "replays the same tier against the same Cataclysms. Issue #1338.")

    def test_it_names_both_ways_of_failing(self, document):
        """The owner named both, and a rule that names one reads as though the
        other is excluded."""
        assert ("losing in the Cataclysm boss dungeon, or losing the Last "
                "Stand") in document, (
            "the design states the failed-run replay rule without saying what "
            "failing is. The owner named two forms on 2026-09-06 -- losing in "
            "the Cataclysm boss dungeon and losing the Last Stand -- and both "
            "count. Issue #1338.")

    def test_it_keeps_the_owners_worked_example(self, document):
        """The clearest statement of the rule the owner gave, and the one a
        reader can check their understanding against."""
        assert "restarts against Demonic, War and Death" in document, (
            "the design no longer carries the owner's worked example: a "
            "character at difficulty tier 3 facing Demonic, War and Death who "
            "fails restarts against those same three. Issue #1338.")

    def test_the_ending_a_run_section_promises_the_same_ones_not_just_as_many(
            self, document):
        """The section already said a failed run replays "the same tier -- the
        same number of simultaneous Cataclysms". The COUNT was never the
        question. Leaving it at that would have left the document promising a
        weaker rule than the one that was decided, in the section a reader goes
        to for what a failure costs."""
        assert ("The same tier means the same Cataclysms, by name and not only "
                "by count.") in document, (
            "the Ending a Run section states the replay rule in terms of the "
            "number of simultaneous Cataclysms without saying they are the "
            "same ones. The count was never in doubt; the identity is what the "
            "owner ruled on 2026-09-06. Issue #1338.")

    def test_it_says_the_two_halves_of_the_rule_do_not_compete(self, document):
        """Randomising for variety and holding the draw for a replay read as
        opposites unless the document says why they are not."""
        assert ("the variety is between characters and the consistency is "
                "within one character's attempts") in document, (
            "the design states both halves of the rule -- the order is "
            "randomised per character, and a failed run faces the same ones -- "
            "without saying how they fit together. Issue #1338.")

    def test_the_decisions_log_carries_the_owners_words(self, decisions):
        assert ("if a player fails a run, meaning either they fail the "
                "cataclysm dungeon or the last stand, they restart that run"
                ) in decisions, (
            "docs/DECISIONS.md no longer quotes the project owner's ruling of "
            "2026-09-06 on when the draw happens. The verbatim wording is the "
            "evidence that the rule was decided rather than inferred, and this "
            "one was answered outside the options that were offered. "
            "Issue #1338.")

    def test_the_log_no_longer_lists_the_answered_question_as_open(
            self, decisions):
        """The earlier entry listed four things the rule left open and this was
        the fourth. An entry that still calls a settled question open is worse
        than one that never raised it: the next reader takes it for work to do.
        """
        assert ("The two have not been reconciled" not in decisions), (
            "docs/DECISIONS.md still says the per-character and per-run "
            "readings have not been reconciled. The owner reconciled them on "
            "2026-09-06. Issue #1338.")


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

    def test_it_states_the_settled_formula(self, document):
        """The owner ruled the shape on 2026-09-06 after the research he
        ordered reported. Until then this section said "The direction is
        settled and the shape is not", and this test asserted that sentence."""
        assert ("points awarded = base points for the dungeon type × (living "
                "population ÷ total maximum population)") in document, (
            "docs/Cataclysm_GDD_v2.md no longer states the formula the owner "
            "ruled on 2026-09-06: the base points for the dungeon type times "
            "the living population over the total maximum. The direction "
            "without the shape is what this section said before the ruling, "
            "and the model now implements the shape. Issue #1348.")

    def test_it_says_the_denominator_counts_fallen_cities(self, document):
        """The half of the formula a reader would otherwise guess wrong, and
        the half that decides whether a wiped-out empire reads as ruined or as
        perfectly intact."""
        assert "every city, fallen or not" in document, (
            "the design states the population formula without saying that the "
            "denominator counts every city, fallen or not. A denominator that "
            "shrank with the empire would report a destroyed empire as fully "
            "populated. Issue #1348.")

    def test_it_says_the_award_is_measured_at_each_defeat(self, document):
        assert ("measured at the instant each dungeon is defeated, not once at "
                "the end of a run") in document, (
            "the design no longer says when the population multiplier is "
            "measured. Per defeat and at the end of a run are different games, "
            "and the no-floor decision depends on which one it is. "
            "Issue #1348.")

    def test_it_says_the_per_defeat_timing_is_what_makes_no_floor_safe(
            self, document):
        """THE COUPLING, and the reason it is written into the design rather
        than only into the code. Someone moving the award to the end of a run
        would remove a floor they did not know was there. The measurement is
        quoted so the claim can be rechecked rather than believed."""
        assert "that is what makes the absence of a floor safe" in document, (
            "the design states the per-defeat timing and the absence of a "
            "floor as two separate facts. They are one decision: awarding per "
            "defeat is what supplies the floor. The worst of 500 simulated "
            "campaigns kept 51% of the flat award per defeat and 0.25 applied "
            "at the end of a run. Issue #1348.")
        assert "45% of the flat award on average against 24%" in document, (
            "the design asserts that the per-defeat timing supplies a floor "
            "without the measurement behind it. The number is what makes it "
            "checkable rather than a belief. Issue #1348.")

    def test_it_says_the_figure_the_no_floor_ruling_rested_on_did_not_hold(
            self, document):
        """The owner ruled no floor on a measured worst case of 51%. That does
        not reproduce on the current model -- 9% at the same condition, and 0%
        at tier 8 surge 8. The shape is still what was ruled, but a design that
        quoted only the number the decision was made on would be citing an
        argument that has since failed."""
        assert ("the figure the ruling was made on no longer holds"
                in document), (
            "docs/Cataclysm_GDD_v2.md states the no-floor rule without "
            "recording that the measurement it was ruled on does not "
            "reproduce. The worst case behind the ruling was 51% of the flat "
            "award; it is 9% now, and 0% at tier 8 with surge size 8. Deleting "
            "that leaves the design resting on an argument nobody can "
            "reproduce. Issue #1348.")

    def test_it_still_says_which_parts_are_not_fixed(self, document):
        """The shape is settled; the owner's sixth question is not. An entry
        that reads as a complete specification of something still open is the
        failure this class of test exists to prevent -- and so is one that
        still calls a settled question open, which is why the sentence this
        test used to assert is gone."""
        assert ("whether keeping cities should **also** advance the win "
                "condition inside a single run is untouched by this and "
                "remains open") in document, (
            "the design no longer says what the population rule leaves "
            "unfixed. The owner's own words ended \"I'm not sure if there "
            "should be anything more than that\", so whether keeping cities "
            "also advances the win condition WITHIN a run is still open even "
            "though the shape of the between-run reward is settled. "
            "Issue #1348.")

    def test_the_old_open_shape_sentence_is_gone(self, document):
        """The specific wording the ruling made wrong. Leaving it beside the
        formula would leave the section saying both that the shape is settled
        and that it is not."""
        assert "The direction is settled and the shape is not." not in document, (
            "docs/Cataclysm_GDD_v2.md still says the shape of the population "
            "bonus is unsettled. The owner ruled it on 2026-09-06 and the "
            "simulation implements it, so that sentence describes a game the "
            "design no longer specifies. A settled question still listed as "
            "open is read as work to do. Issue #1348.")

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
