"""The two scoring policies value a Quest dungeon by the half-of-Cataclysms rule.

ISSUE #1388. The project owner ruled on 2026-09-06 that the Cataclysm dungeon
opens when the quest objectives of HALF the active Cataclysms are met, rounded
up, each by its own count. `triage` and `lane_aware` went on valuing a Quest
dungeon by `quest_objectives_required - sim.objectives` -- a flat 8, now only a
fallback, minus every quest dungeon cleared from every Cataclysm -- so neither
could tell a dungeon whose Cataclysm was finished from one whose Cataclysm was
part way through. `policies.quest_dungeons_to_finish` reads the rule instead.

Until this file no test checked how either policy values a Quest dungeon: the
whole fast suite passed with the old reading and with the new one.

THE CONTROL is a board where the old reading and the rule agree -- one active
Cataclysm whose own count is 8 -- so the new distance must equal the old one
there, and the change moves nothing it had no reason to move.
"""
from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation

SCORING_POLICIES = ("triage", "lane_aware")

#: Everything a policy scores, copied from one dungeon to the other so that
#: only the Cataclysm that sent it differs. As in `test_policies_see_a_quest_timer.py`.
FIELDS = ("floors", "run_days", "resolve_max", "resolve_in", "defense_damage",
          "population_damage", "modifier_score", "power_scale", "subtype")


def campaign(active: int, **settings) -> Simulation:
    """A run facing exactly `active` Cataclysms, with no surge and no death roll.

    `active_cataclysms` IS SET RATHER THAN `tier`, as in
    `test_the_model_opens_the_boss_at_half_the_cataclysms.py`, so the power scale
    is tier 1's and only the gate differs.
    """
    cfg = replace(TuningConfig(), active_cataclysms=active, per_floor_risk=0.0,
                  boss_risk_multiplier=0.0, **settings).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.next_surge_day = float("inf")
    return sim


def finish(sim: Simulation, name: str) -> None:
    """Meet that Cataclysm's own objective count."""
    needed = sim.cfg.quest_objectives_for(name)
    sim.objectives_by_type[name] = needed
    sim.objectives += needed


def twin_quests(sim: Simulation, first: str, second: str):
    """Two identical Quest dungeons on the same whole Outpost, sent by `first`
    and by `second`. Neither carries a Siege and neither timer can fell the
    city, so the Cataclysm that sent each is the only thing that differs."""
    city = next(c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST and not c.fallen)
    a = sim._make_dungeon(DungeonType.QUEST, city)
    b = sim._make_dungeon(DungeonType.QUEST, city)
    a.subtype = "Timed"
    for f in FIELDS:
        setattr(b, f, getattr(a, f))
    a.source, b.source = first, second
    assert policies.siege_urgency(sim, a, city, 5.0) == 1.0
    assert not policies._quest_hit_fells(sim, a, city)
    return a, b


@pytest.mark.parametrize("name", SCORING_POLICIES)
def test_a_quest_dungeon_from_an_unfinished_cataclysm_is_worth_more(name):
    """Four active, so two must be finished. One is finished and one is three
    quest dungeons in. The dungeon from the unfinished one is listed SECOND,
    so a policy that cannot tell them apart keeps the first: a strict `>` keeps
    the earlier of two equal scores."""
    sim = campaign(4)
    done, part_way = sim.active_types[0], sim.active_types[1]
    finish(sim, done)
    sim.objectives_by_type[part_way] = 3
    sim.objectives += 3
    assert sim.cataclysms_complete() == (done,)
    assert sim.cfg.cataclysms_required() == 2

    from_done, from_part_way = twin_quests(sim, done, part_way)
    assert policies.quest_dungeons_to_finish(sim, from_done) is None, (
        f"{done} is finished, so clearing another of its quest dungeons brings "
        "the boss no nearer and must carry no bonus")
    assert policies.quest_dungeons_to_finish(sim, from_part_way) == (
        sim.cfg.quest_objectives_for(part_way) - 3)
    assert policies.ALL[name](sim, [from_done, from_part_way]) is from_part_way, (
        f"{name} did not prefer the Quest dungeon whose Cataclysm is not yet "
        "finished. Issue #1388.")


@pytest.mark.parametrize("name", SCORING_POLICIES)
def test_once_the_boss_is_open_no_quest_dungeon_brings_it_nearer(name):
    """Two of four finished opens the boss, so neither of the two unfinished
    Cataclysms' dungeons carries a bonus, and the policy has no reason to prefer
    the second of two identical ones."""
    sim = campaign(4)
    for done in sim.active_types[:2]:
        finish(sim, done)
    open_a, open_b = sim.active_types[2], sim.active_types[3]
    first, second = twin_quests(sim, open_a, open_b)
    assert policies.quest_dungeons_to_finish(sim, first) is None
    assert policies.quest_dungeons_to_finish(sim, second) is None
    assert policies.ALL[name](sim, [first, second]) is first, (
        f"{name} preferred one of two Quest dungeons that both count towards "
        "nothing, so something other than the gate is deciding")


def test_control_where_the_old_reading_and_the_rule_agree_they_give_one_distance():
    """One active Cataclysm whose own count is set to 8, with every objective
    cleared from it: the flat 8 minus the run's total is then the same number
    as that Cataclysm's own count minus its own clears, at every step."""
    probe = campaign(1)
    only = probe.active_types[0]
    counts = dict(probe.cfg.QUEST_OBJECTIVES, **{only: 8})
    sim = campaign(1, QUEST_OBJECTIVES=counts)
    assert sim.cfg.quest_objectives_for(only) == sim.cfg.quest_objectives_required == 8

    city = next(c for c in sim.empire.cities.values() if not c.fallen)
    d = sim._make_dungeon(DungeonType.QUEST, city)
    d.source = only
    for cleared in range(8):
        sim.objectives_by_type[only] = cleared
        sim.objectives = cleared
        old = max(1, sim.cfg.quest_objectives_required - sim.objectives)
        assert policies.quest_dungeons_to_finish(sim, d) == old, (
            f"with {cleared} of 8 cleared from the only Cataclysm, the rule and "
            "the old reading should agree")
    finish(sim, only)
    assert policies.quest_dungeons_to_finish(sim, d) is None


def test_a_dungeon_naming_no_cataclysm_is_measured_against_the_fallback():
    """Only a dungeon built by hand names none. It is measured against
    `quest_objectives_for("")`, the documented fallback, rather than raising."""
    sim = campaign(4)
    city = next(c for c in sim.empire.cities.values() if not c.fallen)
    d = sim._make_dungeon(DungeonType.QUEST, city)
    assert d.source == ""
    assert policies.quest_dungeons_to_finish(sim, d) == sim.cfg.quest_objectives_for("")
