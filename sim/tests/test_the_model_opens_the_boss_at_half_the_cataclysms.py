"""The Cataclysm dungeon opens at half the active Cataclysms, in the model.

WHAT THIS COVERS THAT NOTHING ELSE DID. Before this file the model's half-of-
active gate was checked in two ways and neither ran it in the direction that
matters:

  * `tools/tests/test_the_cataclysm_dungeon_unlocks_at_half.py` evaluates
    `TuningConfig.cataclysms_required` -- the arithmetic -- and reads the game's
    C++ and the design document as text. It never calls
    `Simulation._maybe_open_cataclysm`.
  * The tests in `test_engine.py` and `test_cataclysm_cannot_be_a_cow_level.py`
    that are about what the boss IS once it opens force the gate through the
    `meet_the_unlock_requirement` fixture and then assert it opened. **That is
    only the permissive direction.** A gate that opened at half the requirement,
    or at none of it, would pass every one of them.

  So this file drives the gate itself and asserts what it REFUSES, which is the
  half that a mistake would live in. The game has the same pair under
  `Cataclysm.Roster`; each side is guarded on its own because neither can notice
  the other, which is the arrangement every port in this project is in.

THE RULE. The project owner ruled on 2026-09-06 that the Cataclysm dungeon
unlocks when the quest objectives of HALF the active Cataclysms have been met,
rounded up. Issue #1324 carries the ruling and issue #1357 the identity it
needed. `TuningConfig.cataclysms_required` carries the verbatim words.
"""

from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim.config import TREE_NONE, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def campaign(active: int, seed: int = 0) -> Simulation:
    """A run facing exactly `active` Cataclysms, with nothing else in the way.

    `active_cataclysms` IS SET RATHER THAN `tier` because the tier also sets the
    power scale, and this file is about the gate and not about how hard the
    fight is.

    THE DEATH ROLL IS TURNED OFF for the same reason `test_engine.py` turns it
    off: `_finish_current` rolls before it counts anything, so a test that
    counts clears would otherwise be choosing seeds that happen to survive.
    """
    cfg = replace(TuningConfig(), active_cataclysms=active, per_floor_risk=0.0,
                  boss_risk_multiplier=0.0).with_tree(TREE_NONE)
    return Simulation(cfg, seed=seed)


def finish(sim: Simulation, names) -> None:
    """Meet those Cataclysms' own objective counts, and nothing else's."""
    for name in names:
        needed = sim.cfg.quest_objectives_for(name)
        sim.objectives_by_type[name] = needed
        sim.objectives += needed


def clear_quest_dungeon(sim: Simulation, sender: str):
    """Clear one quest dungeon sent by `sender`, THROUGH THE ORDINARY PATH.

    Through `_finish_current` and not by writing the tally, which is the whole
    point: what is under test is that the model reads `Dungeon.source` and
    credits the Cataclysm named there. Writing the tally would test the tally.
    """
    city = sim.empire.cities[sim.empire.pillar_id]
    dungeon = sim._make_dungeon(DungeonType.QUEST, city)
    dungeon.source = sender
    sim.current = dungeon
    sim._finish_current()
    return dungeon


#: Every count a run can face. The odd ones are where rounding up and rounding
#: down disagree, and they are the only ones that can tell the two apart.
ACTIVE_COUNTS = (1, 2, 3, 4, 5, 6, 7, 8)


class TestTheGateRefusesBelowTheRequirement:
    """**The direction the other tests cannot see.**"""

    @pytest.mark.parametrize("active", ACTIVE_COUNTS)
    def test_one_short_of_the_requirement_does_not_open_it(self, active):
        sim = campaign(active)
        required = sim.cfg.cataclysms_required()

        finish(sim, sim.active_types[:required - 1])
        sim._maybe_open_cataclysm()

        assert sim.cataclysm is None, (
            f"facing {active} Cataclysms the boss opened with {required - 1} "
            f"of them finished, where the rule asks for {required}")

    @pytest.mark.parametrize("active", ACTIVE_COUNTS)
    def test_exactly_the_requirement_opens_it(self, active):
        """THE CONTROL FOR THE TEST ABOVE. Without it, a gate that never opened
        at all would satisfy every refusal this file asserts."""
        sim = campaign(active)
        required = sim.cfg.cataclysms_required()

        finish(sim, sim.active_types[:required])
        sim._maybe_open_cataclysm()

        assert sim.cataclysm is not None, (
            f"facing {active} Cataclysms the boss did not open with "
            f"{required} of them finished, which is the requirement")

    def test_nothing_finished_does_not_open_it(self):
        """Half of one rounded DOWN is none, so at a single active Cataclysm a
        floor would open the boss before the player had cleared anything."""
        sim = campaign(1)
        sim._maybe_open_cataclysm()

        assert sim.cataclysm is None, (
            "the boss opened on a run where nothing had been cleared at all")


class TestATotalOfObjectivesIsNotEnough:
    """**The reason the run keeps a tally per Cataclysm and not only a total.**

    This is the case a flat total cannot express, and it is not hypothetical:
    the gate compared `objectives` against a flat 8 until the ruling.
    """

    def test_enough_objectives_all_from_one_cataclysm_does_not_open_it(self):
        sim = campaign(4)
        required = sim.cfg.cataclysms_required()
        assert required == 2

        # ONE CATACLYSM FINISHED SEVERAL TIMES OVER. The total is well past the
        # flat 8 the old gate asked for, and one of the two required Cataclysms
        # is finished.
        first = sim.active_types[0]
        sim.objectives_by_type[first] = 40
        sim.objectives = 40

        sim._maybe_open_cataclysm()

        assert sim.cataclysm is None, (
            f"40 quest objectives all belonging to {first} opened the boss "
            "while three of the four active Cataclysms had none. The rule asks "
            "for two Cataclysms finished, not for a number of dungeons")

    def test_objectives_spread_too_thin_do_not_open_it(self):
        """The other shape of the same mistake: plenty of objectives, none of
        them enough to finish anybody."""
        sim = campaign(4)

        for name in sim.active_types:
            # ONE SHORT OF EACH CATACLYSM'S OWN COUNT, which differ.
            sim.objectives_by_type[name] = sim.cfg.quest_objectives_for(name) - 1
            sim.objectives += sim.objectives_by_type[name]

        assert sim.objectives >= sim.cfg.quest_objectives_required, (
            "this test needs a total past the old flat requirement to be "
            "testing anything; it has "
            f"{sim.objectives} against {sim.cfg.quest_objectives_required}")

        sim._maybe_open_cataclysm()

        assert sim.cataclysm is None, (
            f"{sim.objectives} objectives opened the boss with no Cataclysm "
            "finished at all")


class TestACataclysmIsFinishedByItsOwnCount:
    """The counts differ -- Death asks for 5 and Demonic for 10 -- so "finished"
    is a different number for each of them."""

    def test_a_cataclysm_needs_its_own_number_and_not_the_smallest(self):
        cfg = replace(TuningConfig(), active_cataclysms=8).with_tree(TREE_NONE)
        sim = Simulation(cfg, seed=0)

        cheapest = min(cfg.quest_objectives_for(n) for n in sim.active_types)
        dearest = max(cfg.quest_objectives_for(n) for n in sim.active_types)
        assert cheapest < dearest, (
            "every active Cataclysm asks for the same number, so this test "
            "cannot tell one count from another")

        for name in sim.active_types:
            sim.objectives_by_type[name] = cheapest
            sim.objectives += cheapest

        finished = set(sim.cataclysms_complete())
        expected = {n for n in sim.active_types
                    if cfg.quest_objectives_for(n) <= cheapest}

        assert finished == expected, (
            "giving every Cataclysm the cheapest count finished "
            f"{sorted(finished)}; only {sorted(expected)} ask for that few")


class TestClearingAQuestDungeonCreditsItsSender:
    """The tally the gate reads is raised by the live path, not set by hand."""

    def test_it_counts_for_the_cataclysm_that_sent_it_and_no_other(self):
        sim = campaign(4)
        sender = sim.active_types[1]

        before = dict(sim.objectives_by_type)
        clear_quest_dungeon(sim, sender)

        assert sim.objectives_by_type[sender] == before[sender] + 1, (
            f"clearing a quest dungeon sent by {sender} did not advance it")

        for name in sim.active_types:
            if name != sender:
                assert sim.objectives_by_type[name] == before[name], (
                    f"it also advanced {name}, which did not send it")

    def test_the_runs_total_rises_as_well(self):
        """The two are kept beside each other on purpose: the total is what a
        player's sense of progress reads and the map is what the gate reads."""
        sim = campaign(4)

        before = sim.objectives
        clear_quest_dungeon(sim, sim.active_types[0])

        assert sim.objectives == before + 1

    def test_a_dungeon_that_names_nobody_raises_only_the_total(self):
        """A dungeon built by hand names no Cataclysm, and the model has to say
        so rather than crediting whichever name sorts first."""
        sim = campaign(4)

        before_total = sim.objectives
        before_map = dict(sim.objectives_by_type)
        clear_quest_dungeon(sim, "")

        assert sim.objectives == before_total + 1
        assert sim.objectives_by_type == before_map, (
            "a quest dungeon that names no Cataclysm advanced one anyway")
