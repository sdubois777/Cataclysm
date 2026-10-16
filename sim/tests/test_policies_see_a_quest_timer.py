"""The two scoring policies see a Quest dungeon's timer.

SINCE 2026-10-13 a Quest dungeon's timer running out hits its host city with an
ordinary dungeon's hit for that city (`Simulation._quest_hit`). Until 2026-10-16
no policy asked: `Dungeon.resolves` answers False for a Quest dungeon, so
`triage` and `lane_aware` scored it by its objective value alone and left it
out of the "is a timer due inside a craft" list. The change, accepted by the
coordinating session on 2026-10-16, is the smallest one: a Quest dungeon whose
next hit would fell its host weighs what a fatal ordinary hit weighs in each
policy (5 in `triage`, 4 in `lane_aware`), and its timer counts as due.

EVERY CASE HAS A CONTROL: the same board with `quest_timer_hits_city` off, where
the timer costs the city nothing and the policy has no reason to choose.
"""
from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation

SCORING_POLICIES = ("triage", "lane_aware")

#: Everything a policy scores, copied from one dungeon to the other so that
#: only the host city differs.
FIELDS = ("floors", "run_days", "resolve_max", "resolve_in", "defense_damage",
          "population_damage", "modifier_score", "power_scale", "subtype")


def sim_for(hits: bool = True) -> Simulation:
    cfg = replace(TuningConfig(), tier=1,
                  quest_timer_hits_city=hits).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.next_surge_day = float("inf")
    return sim


def two_outposts(sim: Simulation):
    outposts = [c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST and not c.fallen]
    assert len(outposts) >= 2, "the map has fewer than two standing Outposts"
    return outposts[0], outposts[1]


def twin_quests(sim: Simulation):
    """A Quest dungeon on a weakened Outpost and its twin on a whole one.

    The weakened Outpost holds less defence than one ordinary Outpost hit, so
    the next time the Quest timer runs out the city falls. Neither carries a
    Siege, so `siege_urgency` is 1.0 for both.
    """
    whole, weak = two_outposts(sim)
    bite = sim.cfg.spec(DungeonType.BASIC, CityTier.OUTPOST).defense_damage
    weak.defense = bite / 2.0
    on_weak = sim._make_dungeon(DungeonType.QUEST, weak)
    on_whole = sim._make_dungeon(DungeonType.QUEST, whole)
    on_weak.subtype = "Timed"
    for f in FIELDS:
        setattr(on_whole, f, getattr(on_weak, f))
    assert policies.siege_urgency(sim, on_weak, weak, 5.0) == 1.0
    return whole, weak, on_whole, on_weak


@pytest.mark.parametrize("name", SCORING_POLICIES)
def test_it_answers_the_quest_dungeon_whose_next_hit_fells_its_city(name):
    """The fatal one is listed SECOND, so a policy that cannot tell them apart
    keeps the first: a strict `>` keeps the earlier of two equal scores."""
    sim = sim_for(hits=True)
    whole, weak, on_whole, on_weak = twin_quests(sim)
    assert policies._quest_hit_fells(sim, on_weak, weak)
    assert not policies._quest_hit_fells(sim, on_whole, whole)
    assert policies.ALL[name](sim, [on_whole, on_weak]) is on_weak, (
        f"{name} did not choose the Quest dungeon whose next hit fells its city")


@pytest.mark.parametrize("name", SCORING_POLICIES)
def test_control_with_the_timer_costing_nothing_it_has_no_reason_to_choose(name):
    sim = sim_for(hits=False)
    whole, weak, on_whole, on_weak = twin_quests(sim)
    assert not policies._quest_hit_fells(sim, on_weak, weak)
    assert policies.ALL[name](sim, [on_whole, on_weak]) is on_whole, (
        f"{name} preferred one of two identical Quest dungeons with the timer "
        "costing nothing, so the test above is not measuring the change")


def test_a_quest_timer_counts_as_due_only_where_it_hits() -> None:
    """What the "is a timer due inside a craft" list asks of a Quest dungeon."""
    sim = sim_for(hits=True)
    whole, weak = two_outposts(sim)
    quest = sim._make_dungeon(DungeonType.QUEST, whole)
    ordinary = sim._make_dungeon(DungeonType.BASIC, whole)
    assert policies._quest_timer_hits(sim, quest)
    assert not policies._quest_timer_hits(sim, ordinary), (
        "an ordinary dungeon is already in the list through `resolves`")

    whole.fallen = True
    assert not policies._quest_timer_hits(sim, quest), (
        "a Quest timer on a fallen city hits nothing")

    off = sim_for(hits=False)
    a, _ = two_outposts(off)
    assert not policies._quest_timer_hits(
        off, off._make_dungeon(DungeonType.QUEST, a))
