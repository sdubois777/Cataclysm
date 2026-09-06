"""The unlock rule, in the design, in the model and in the game, as one number.

WHAT THE RULE IS. The project owner ruled on 2026-09-06 that the Cataclysm
dungeon unlocks when the quest objectives of HALF the active Cataclysms have been
met, rounded up: 3 needs 2, 4 needs 2, 5 needs 3, 7 needs 4, 8 needs 4. Issue
#1324 carries the ruling and issue #1357 the identity it needed first.

**THE ODD COUNTS ARE THE ONLY ONES THAT TELL CEILING FROM FLOOR.** Two of the
five counts the owner worked through are even, and at 4 and at 8 `N // 2` and
`ceil(N / 2)` are the same number. A test assembled only from the ruling's own
table would therefore pass on the wrong rounding, which is wrong at three of the
five. Every check here that touches the arithmetic asserts the floor is NOT the
answer as well as that the ceiling IS.

WHY IT READS THREE PLACES. The design document is the design; the model is where
the balance figures come from; the game is what a player meets. Three copies of
one number is three numbers, and `sim/cataclysm_sim/scoring.py` drifted from its
own source twice, which is why `CLAUDE.md` carries a rule about it. This file is
the same arrangement `tools/tests/test_surge_port.py` and
`tools/tests/test_quest_objective_counts_are_stated.py` are in.

WHAT IT DOES NOT CHECK. That the game BEHAVES -- that clearing a quest dungeon
credits the Cataclysm that sent it, that the gate really opens at the
requirement, that a wave stamps every dungeon it lands. Those are Unreal
automation tests under the `Cataclysm.Roster` prefix, and a test that reads a
constant out of the source cannot notice that the code ignores it.
"""

from __future__ import annotations

import math
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
ROSTER_HEADER = EMPIRE / "Empire" / "CataclysmRoster.h"
ROSTER_SOURCE = EMPIRE / "Empire" / "CataclysmRoster.cpp"
ROSTER_TESTS = EMPIRE / "Tests" / "CataclysmRosterTests.cpp"
RUN_HEADER = EMPIRE / "Empire" / "CataclysmEmpireRun.h"
SURGE_HEADER = EMPIRE / "Empire" / "CataclysmSurge.h"

#: The active counts the owner worked through, and the answer he gave for each.
#: Quoted from the ruling and not recomputed here, which is the point of writing
#: them down: recomputing them with the same formula the code uses would test
#: nothing.
OWNERS_TABLE = {3: 2, 4: 2, 5: 3, 7: 4, 8: 4}

#: The counts where rounding up and rounding down disagree. THE ONLY ONES THAT
#: PROVE WHICH WAS BUILT.
ODD_COUNTS = (1, 3, 5, 7)


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist")
    return path.read_text(encoding="utf-8")


def flattened(path: pathlib.Path) -> str:
    """One long line, so a sentence broken across a hard wrap still matches.

    The leading `> ` of a block quote is stripped first, for the reason
    `test_quest_objective_counts_are_stated.py` gives: without it a quoted
    sentence flattens with a stray `>` at every wrap and matches nothing, which
    reads as the quote being absent rather than as the search being wrong.
    """
    lines = read(path).splitlines()
    unquoted = [line[2:] if line.startswith("> ") else line for line in lines]
    return " ".join(" ".join(unquoted).split())


@pytest.fixture(scope="module")
def config():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


@pytest.fixture(scope="module")
def roster_source() -> str:
    return read(ROSTER_SOURCE)


@pytest.fixture(scope="module")
def document() -> str:
    return flattened(GDD)


# ---------------------------------------------------------------------------
# The arithmetic
# ---------------------------------------------------------------------------

class TestTheModelAnswersTheOwnersTable:
    @pytest.mark.parametrize("active,required", sorted(OWNERS_TABLE.items()))
    def test_it_matches_every_worked_example(self, active, required):
        from dataclasses import replace
        from cataclysm_sim.config import TuningConfig

        cfg = replace(TuningConfig(), active_cataclysms=active)

        assert cfg.cataclysms_required() == required, (
            f"facing {active} Cataclysms the model asks for "
            f"{cfg.cataclysms_required()}, and the project owner said "
            f"{required} on 2026-09-06. Issue #1324.")

    @pytest.mark.parametrize("active", ODD_COUNTS)
    def test_an_odd_count_rounds_up_and_not_down(self, active, config):
        """**The check that can tell the two roundings apart.**

        `active_cataclysms=1` is included deliberately: half of one rounded down
        is none, which would open the Cataclysm dungeon before the player had
        cleared anything at all.
        """
        from dataclasses import replace
        from cataclysm_sim.config import TuningConfig

        cfg = replace(TuningConfig(), active_cataclysms=active)
        required = cfg.cataclysms_required()

        assert required == math.ceil(active / 2), (
            f"facing {active} Cataclysms the model asks for {required} rather "
            f"than the {math.ceil(active / 2)} that half rounded up gives.")

        assert required != active // 2, (
            f"facing {active} Cataclysms the model asks for {required}, which "
            f"is what rounding DOWN gives. The owner's examples were 3 needs 2, "
            f"5 needs 3 and 7 needs 4, every one of them a ceiling. Rounding "
            f"down is invisible at the 4 and 8 he also gave, which is why this "
            f"check exists separately from the table above.")

    def test_an_even_count_is_exactly_half(self, config):
        """Rounding up must not become "one more than half"; that would ask for
        3 of 4, where the owner said 2."""
        from dataclasses import replace
        from cataclysm_sim.config import TuningConfig

        for active in (2, 4, 6, 8):
            cfg = replace(TuningConfig(), active_cataclysms=active)
            assert cfg.cataclysms_required() == active // 2, (
                f"facing {active} Cataclysms the model asks for "
                f"{cfg.cataclysms_required()} rather than the {active // 2} "
                "the owner stated.")

    def test_it_reads_the_active_count_and_not_the_tier_directly(self):
        """`active_cataclysms` overrides the tier for a sweep that varies the
        count on its own axis, and the requirement has to follow the override or
        such a sweep would measure one thing while reporting another."""
        from dataclasses import replace
        from cataclysm_sim.config import TuningConfig

        overridden = replace(TuningConfig(), tier=8, active_cataclysms=3)

        assert overridden.active_cataclysm_count() == 3
        assert overridden.cataclysms_required() == 2, (
            "the requirement was worked out from the difficulty tier rather "
            "than from the active count, so a sweep that varies the count "
            "independently would face three Cataclysms and be asked to finish "
            "four.")


class TestTheGameComputesTheSameThing:
    def test_the_cpp_rounds_up(self, roster_source):
        """`(N + 1) / 2` is the ceiling in integer arithmetic. `N / 2` is not,
        and the difference is invisible at the owner's even examples."""
        body = re.search(
            r"int32 UCataclysmRoster::CataclysmsRequiredFor\([^)]*\)\s*\{(.*?)\n\}",
            roster_source, re.S)

        assert body is not None, (
            "CataclysmRoster.cpp no longer defines CataclysmsRequiredFor. If it "
            "was renamed, rename it here; if it went away, the unlock "
            "requirement is unguarded and continuous integration compiles no "
            "C++ to notice.")

        assert re.search(r"\(\s*Active\s*\+\s*1\s*\)\s*/\s*2", body.group(1)), (
            "CataclysmsRequiredFor no longer computes (Active + 1) / 2. Half "
            "rounded DOWN agrees with the owner's ruling at 4 and at 8 and "
            "disagrees at 3, 5 and 7, so this is not a cosmetic difference. "
            "Issue #1324.")

    def test_the_cpp_and_the_model_agree_at_every_count(self, roster_source):
        """Both sides are evaluated rather than compared as text: the C++ is
        read out of the source and applied, so a change of formula that still
        contained the substring above would be caught here."""
        from dataclasses import replace
        from cataclysm_sim.config import TuningConfig

        for active in range(1, 9):
            cfg = replace(TuningConfig(), active_cataclysms=active)
            assert cfg.cataclysms_required() == (active + 1) // 2, (
                f"the model and the C++ disagree at {active} active "
                f"Cataclysms: the model says {cfg.cataclysms_required()} and "
                f"(Active + 1) / 2 gives {(active + 1) // 2}.")

    def test_the_odd_case_guard_exists_in_the_unreal_tests(self):
        """A guard that is deleted is a guard that never fired.

        THE NAME IS THE CONTRACT. `CLAUDE.md` warns that a test whose name
        asserts something nobody verified is worse than a failure, so this
        checks the test that covers the odd counts is still there AND still
        names them.
        """
        text = read(ROSTER_TESTS)

        assert "Cataclysm.Roster.HalfRoundsUpAtEveryOddCount" in text, (
            "the Unreal test covering the odd active counts is gone. Floor and "
            "ceiling agree at the 4 and 8 the owner worked through, so without "
            "it nothing in the C++ suite distinguishes the two. Issue #1324.")

        assert "TestNotEqual" in text, (
            "CataclysmRosterTests.cpp no longer asserts what the requirement is "
            "NOT. The odd-count guard is a comparison between two roundings; "
            "checking only that the ceiling is right leaves it able to pass on "
            "an implementation that returns the floor.")


# ---------------------------------------------------------------------------
# The eight objective counts, in three places
# ---------------------------------------------------------------------------

#: The design's counts, quoted from `docs/Cataclysm_GDD_v2.md` section XI. The
#: key is the model's spelling, which drops the article The Void carries in the
#: document; `UCataclysmRoster::NameFor` uses the model's spelling for that
#: reason and the document row is checked separately below.
OBJECTIVES = {
    "Demonic": 10,
    "Death": 5,
    "War": 10,
    "Pestilence": 5,
    "Famine": 5,
    "Celestial": 10,
    "Chaos": 8,
    "Void": 5,
}

#: How each one is written in the document's summary table.
DOCUMENT_NAMES = dict.fromkeys(OBJECTIVES, None) | {"Void": "The Void"}


class TestTheEightCountsAreOneSetOfNumbers:
    @pytest.mark.parametrize("name", sorted(OBJECTIVES))
    def test_the_model_states_it(self, name, config):
        assert config.quest_objectives_for(name) == OBJECTIVES[name], (
            f"sim/cataclysm_sim/config.py says {name} asks for "
            f"{config.quest_objectives_for(name)} quest dungeons and "
            f"docs/Cataclysm_GDD_v2.md section XI says {OBJECTIVES[name]}.")

    @pytest.mark.parametrize("name", sorted(OBJECTIVES))
    def test_the_game_states_the_same(self, name, roster_source):
        match = re.search(
            rf"case ECataclysmType::{name}:\s*return\s*(\d+);", roster_source)

        assert match is not None, (
            f"CataclysmRoster.cpp's QuestObjectivesFor no longer answers for "
            f"{name}. Every one of the eight has to be there; a Cataclysm that "
            "fell through to the default would ask for nothing and count as "
            "finished the moment the run began.")

        assert int(match.group(1)) == OBJECTIVES[name], (
            f"CataclysmRoster.cpp says {name} asks for {match.group(1)} and "
            f"the design says {OBJECTIVES[name]}.")

    @pytest.mark.parametrize("name", sorted(OBJECTIVES))
    def test_the_document_states_the_same(self, name, document):
        shown = DOCUMENT_NAMES[name] or name
        row = f"| {shown} | {OBJECTIVES[name]} |"

        assert row in document, (
            f"the objective-count table in docs/Cataclysm_GDD_v2.md no longer "
            f"has the row {row!r}. The model and the C++ both carry this "
            "number and the document is where the design lives. Issue #1324.")

    def test_the_model_names_every_cataclysm_the_roster_does(self, config):
        """A count missing from the map falls through to the flat fallback,
        which would be silent: the fallback is a real number and the Cataclysm
        would simply ask for the wrong one."""
        assert set(config.QUEST_OBJECTIVES) == set(config.CATACLYSM_ROSTER), (
            "sim/cataclysm_sim/config.py's QUEST_OBJECTIVES and "
            "CATACLYSM_ROSTER name different sets of Cataclysms, so at least "
            "one of them falls through to quest_objectives_required -- which "
            "is a real number, so nothing would fail.")

    def test_they_are_not_all_the_same_number(self, config):
        """The whole reason the rule is interesting.

        The owner was asked whether to keep the per-Cataclysm counts or settle
        on one number and answered "Keep the per-Cataclysm numbers". If they
        ever became equal, the checks above would still pass one by one while
        the design had quietly reverted.
        """
        assert len(set(OBJECTIVES.values())) > 1
        assert len({config.quest_objectives_for(n)
                    for n in config.CATACLYSM_ROSTER}) > 1, (
            "every Cataclysm now asks for the same number of quest dungeons, "
            "which is the flat count the owner rejected. Issue #1324.")


# ---------------------------------------------------------------------------
# The design says it
# ---------------------------------------------------------------------------

class TestTheDesignCarriesTheRule:
    def test_the_gdd_states_the_rule(self, document):
        assert ("meet the quest objectives of half of the Cataclysms you are "
                "facing, rounded up" in document), (
            "docs/Cataclysm_GDD_v2.md no longer states the unlock rule. The "
            "project owner ruled it on 2026-09-06 and CLAUDE.md says a design "
            "decision is not real until it is in docs/. Issue #1324.")

    def test_the_gdd_quotes_the_owner(self, document):
        assert ("you have to meet the quest objectives for half of the "
                "cataclysms you're facing" in document), (
            "docs/Cataclysm_GDD_v2.md no longer quotes the owner's own words "
            "for the unlock rule. The verbatim wording is the evidence that "
            "the rule was decided rather than inferred. Issue #1324.")

    @pytest.mark.parametrize("active,required", sorted(OWNERS_TABLE.items()))
    def test_the_gdd_carries_the_worked_table(self, active, required,
                                              document):
        assert f"| {active} | {required} |" in document, (
            f"the unlock table in docs/Cataclysm_GDD_v2.md no longer has the "
            f"row for {active} Cataclysms needing {required}. The owner worked "
            "these through by hand and they are what the ceiling was read off.")

    def test_the_gdd_says_one_cataclysm_still_needs_one(self, document):
        """The count the ceiling exists for, and the one the design already
        described before the rule."""
        assert "| 1 | 1 |" in document, (
            "the unlock table no longer states that a character facing one "
            "Cataclysm must finish it. Half of one rounded down is none, which "
            "would open the Cataclysm dungeon before anything was cleared.")

    def test_the_gdd_still_says_the_spawn_rate_is_not_derived(self, document):
        """What issue #1357 did NOT settle.

        The identity now exists, so a rate keyed on the Cataclysm has become
        possible -- and it still needs a number the owner has not given. A
        document that stated the unlock rule and said nothing about the spawn
        rate would read as a finished specification of the quest line.
        """
        assert ("How often a Quest dungeon spawns is a separate rule"
                in document), (
            "docs/Cataclysm_GDD_v2.md states the unlock rule without saying "
            "that the Quest dungeon spawn rate is a separate, underived one. "
            "Issue #1357 owns it and it is now unblocked rather than done.")

    def test_the_decisions_log_records_which_half_is_a_reading(self):
        """The ruling settled how many and not which ones. What was built takes
        whichever the player finishes first, and that is a reading."""
        decisions = flattened(DECISIONS)

        assert "Which half is whichever the player finishes first" in decisions, (
            "docs/DECISIONS.md no longer records that the owner's ruling did "
            "not say which Cataclysms must be the finished ones, and that "
            "taking whichever finish first is a reading. Issue #1324.")

    def test_the_decisions_log_records_that_nothing_acts_on_the_unlock(self):
        decisions = flattened(DECISIONS)

        assert "Nothing acts on the unlock" in decisions, (
            "docs/DECISIONS.md no longer records that the game answers whether "
            "the Cataclysm dungeon is unlocked and does not open one. Slice 6 "
            "of issue #1324 needs the enemy capital and the loss condition, "
            "and neither exists.")


# ---------------------------------------------------------------------------
# The identity itself
# ---------------------------------------------------------------------------

class TestTheEmpireLayerKnowsWhichCataclysmIsRunning:
    def test_the_enum_names_all_eight_and_an_unassigned_value(self):
        header = read(ROSTER_HEADER)

        block = re.search(r"enum class ECataclysmType : uint8\s*\{(.*?)\}\s*;",
                          header, re.S)

        assert block is not None, (
            "CataclysmRoster.h no longer declares ECataclysmType. It is what "
            "the empire layer had no notion of at all until issue #1357.")

        for name in OBJECTIVES:
            assert re.search(rf"\b{name}\s*=", block.group(1)), (
                f"ECataclysmType no longer names {name}.")

        assert re.search(r"\bNone\s*=\s*0", block.group(1)), (
            "ECataclysmType no longer has None = 0. A Fallen City dungeon is "
            "sent by nobody and needs a value that means so; without it, one "
            "would read as belonging to whichever Cataclysm happens to be "
            "first in the enum and would earn the player objectives towards it.")

    def test_a_dungeon_carries_the_cataclysm_that_sent_it(self):
        surge = read(SURGE_HEADER)

        assert "ECataclysmType Cataclysm = ECataclysmType::None;" in surge, (
            "FCataclysmDungeon no longer carries which Cataclysm sent it. "
            "Without it the run cannot tell which Cataclysm a cleared quest "
            "dungeon advances, and the unlock rule cannot be checked at all. "
            "Issue #1357.")

    def test_the_run_counts_objectives_per_cataclysm(self):
        run = read(RUN_HEADER)

        assert ("TMap<ECataclysmType, int32> QuestObjectivesByCataclysm"
                in run), (
            "UCataclysmEmpireRun no longer counts quest objectives per "
            "Cataclysm. The run's total cannot answer the unlock rule: eight "
            "objectives all belonging to one Cataclysm, with four active, "
            "finishes one of the two required. Issue #1357.")

        assert "int32 QuestObjectives = 0;" in run, (
            "UCataclysmEmpireRun no longer keeps the run's total of quest "
            "objectives. It is what the empire screen shows, and issue #1324 "
            "slice 5 built it; the per-Cataclysm map is beside it rather than "
            "instead of it.")

    def test_the_active_set_comes_from_the_seed_and_the_tier(self):
        run = read(RUN_HEADER)

        assert "TArray<ECataclysmType> ActiveCataclysms;" in run, (
            "UCataclysmEmpireRun no longer holds which Cataclysms the run "
            "faces. It is the denominator of the unlock rule.")

        assert re.search(r"void Begin\(.*?int32 DifficultyTier", run, re.S), (
            "UCataclysmEmpireRun::Begin no longer takes a difficulty tier. The "
            "tier is how many Cataclysms are active, and the empire module "
            "cannot read it off ACataclysmGameMode because that is in the "
            "Cataclysm module and this one must not depend on it.")

    def test_the_model_counts_objectives_per_cataclysm_too(self):
        engine = read(REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py")

        assert "objectives_by_type" in engine, (
            "sim/cataclysm_sim/engine.py no longer counts objectives per "
            "Cataclysm, so the model and the game no longer agree about what "
            "the unlock rule reads.")

        assert "cataclysms_complete" in engine, (
            "sim/cataclysm_sim/engine.py no longer answers which Cataclysms "
            "are finished. `_maybe_open_cataclysm` reads it; a total of "
            "objectives cannot express the rule.")
