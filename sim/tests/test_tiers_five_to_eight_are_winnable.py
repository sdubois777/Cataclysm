"""Tiers 5 to 8 are winnable for a fully invested Explorer, and rarer each tier.

THE PROJECT OWNER, 2026-10-30, on the tiers 5-8 question: "Falling wins". The
coordinating session ruled the parts the same day:

1. The Last Stand has at most 125, 156, 188, 219 and 250 floors at difficulty
   tiers 4 to 8 (`TuningConfig.last_stand_floor_cap`). Tiers 2 and 3 have had a
   limit too since the same day's "Wins fall every tier", pinned in
   `test_wins_fall_at_every_tier.py`; tier 1 has none.
2. `Infinite Depths`' per-Cataclysm floors deepen no Cataclysm-type dungeon: not
   the earned boss dungeon and not the Last Stand, which is that dungeon moved.
3. Each active Cataclysm sends quest dungeons at its tier-1 rate
   (`Simulation.quest_chance`).
4. A new play style, `chase_quests`, goes after the quest dungeons that open the
   boss; `triage` is unchanged.

`docs/DECISIONS.md`, 2026-10-30, has the measurements and the reasons.
"""
from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (TREE_EXPLORER_AS_DESIGNED, TREE_NONE, CityTier,
                                  DungeonType, TuningConfig)
from cataclysm_sim.engine import Simulation
from cataclysm_sim.patterns import DEFAULT as PATTERN_DEFAULT, PATTERNS


def simulation(tier: int, tree=TREE_NONE, **changes) -> Simulation:
    cfg = replace(TuningConfig(), tier=tier, **changes).with_tree(tree)
    sim = Simulation(cfg, seed=0)
    sim.next_surge_day = float("inf")
    sim.dungeons.clear()
    return sim


def outpost(sim: Simulation):
    return next(c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST and not c.fallen)


def volume(sim: Simulation) -> float:
    """The wave volume `trigger_surge` works out, for this campaign's roster."""
    return (sum(PATTERNS.get(t, PATTERN_DEFAULT).count_mult for t in sim.active_types)
            ** sim.cfg.cataclysm_volume_exponent)


# -- the Last Stand's depth limit ------------------------------------------------

def test_the_last_stand_limit_grows_with_the_tier_from_tier_four_to_eight():
    """Moved 2026-10-30: tiers 2 and 3 gained a limit (110 and 120), pinned in
    `test_wins_fall_at_every_tier.py`. Tiers 4 to 8 are unchanged."""
    cfg = TuningConfig()
    assert [cfg.last_stand_floor_cap(t) for t in range(4, 9)] == [
        125, 156, 188, 219, 250]
    assert cfg.last_stand_floor_cap(1) is None


def stand_over(tier: int, standing: int):
    """A Last Stand opened over `standing` ordinary dungeons, which add five
    floors each, so it is far deeper than any limit before the limit."""
    sim = simulation(tier)
    city = outpost(sim)
    for _ in range(standing):
        sim._make_dungeon(DungeonType.BASIC, city)
    sim._open_last_stand()
    return sim, sim.last_stand


def test_a_last_stand_deeper_than_its_limit_is_cut_to_it():
    sim, stand = stand_over(6, standing=60)
    assert stand.floors == 188, (
        f"a tier 6 Last Stand over 60 dungeons has {stand.floors} floors; the "
        "limit at tier 6 is 188. Issue #1425's tiers 5-8 ruling, 2026-10-30.")
    assert stand.run_days == sim._walk_days(188, stand.subtype), (
        "the walk was not worked out from the limited depth")


def test_control_a_tier_one_last_stand_is_not_limited():
    """THE CONTROL: the same board at tier 1, where there is no limit, gives a
    Last Stand deeper than any tier's limit -- so the case above is the limit
    at work and not a board that was never deep. It was tier 3 until tier 3
    gained a limit on 2026-10-30."""
    _, stand = stand_over(1, standing=60)
    assert stand.floors > 250


# -- Infinite Depths and the boss dungeon ------------------------------------------

def floors_of(dtype: DungeonType, tree) -> int:
    sim = simulation(4, tree)
    city = (sim.empire.cities[sim.empire.pillar_id] if dtype is DungeonType.CATACLYSM
            else outpost(sim))
    return sim._make_dungeon(dtype, city).floors


def test_infinite_depths_does_not_deepen_a_cataclysm_dungeon():
    """At tier 4 the Explorer's `Infinite Depths` is worth 80 floors. A
    Cataclysm-type dungeon made with it is as deep as one made without it."""
    without = replace(TREE_EXPLORER_AS_DESIGNED, floor_delta_per_type=0.0)
    assert (floors_of(DungeonType.CATACLYSM, TREE_EXPLORER_AS_DESIGNED)
            == floors_of(DungeonType.CATACLYSM, without)), (
        "Infinite Depths deepened a Cataclysm-type dungeon. The project owner, "
        "2026-10-30: 'Not to the boss dungeon'.")


def test_control_infinite_depths_still_deepens_an_ordinary_dungeon():
    without = replace(TREE_EXPLORER_AS_DESIGNED, floor_delta_per_type=0.0)
    assert (floors_of(DungeonType.BASIC, TREE_EXPLORER_AS_DESIGNED)
            - floors_of(DungeonType.BASIC, without)) == 80


# -- quest dungeons per active Cataclysm ---------------------------------------------

def test_each_cataclysm_keeps_its_tier_one_quest_rate_at_tier_eight():
    sim = simulation(8)
    v = volume(sim)
    assert len(sim.active_types) / v > 1.5, "set-up: tier 8 waves are not shared thin"
    assert sim.quest_chance(v) == pytest.approx(
        sim.cfg.quest_dungeon_chance * len(sim.active_types) / v)


def test_control_tier_one_and_the_switch_keep_the_flat_chance():
    one = simulation(1)
    assert one.quest_chance(volume(one)) == one.cfg.quest_dungeon_chance
    off = simulation(8, quest_chance_per_cataclysm_held=False)
    assert off.quest_chance(volume(off)) == off.cfg.quest_dungeon_chance


# -- the quest-chasing play style ----------------------------------------------------

def test_it_is_registered_with_every_other_policy():
    assert policies.ALL["chase_quests"] is policies.chase_quests


def chase_board(finished: bool):
    """A tier 4 board with two safe dungeons: an ordinary one about to fell an
    Outpost, which `triage` answers, and a Quest dungeon sent by an active
    Cataclysm one clear short of its count -- or, with `finished`, by one whose
    count is already met."""
    sim = simulation(4, power_start_frac=1.0)
    city = outpost(sim)
    basic = sim._make_dungeon(DungeonType.BASIC, city)
    basic.subtype, basic.resolve_in = "Timed", 1.0
    city.defense = basic.defense_damage / 2.0
    source = sim.active_types[0]
    need = sim.cfg.quest_objectives_for(source)
    sim.objectives_by_type[source] = need if finished else need - 1
    other = next(c for c in sim.empire.cities.values()
                 if c.tier is CityTier.OUTPOST and c.cid != city.cid)
    quest = sim._make_dungeon(DungeonType.QUEST, other)
    quest.source, quest.resolve_in = source, 500.0
    for d in (basic, quest):
        assert sim.death_chance(d) <= sim.cfg.death_risk_tolerance, "set-up: unsafe"
    return sim, basic, quest


def test_it_takes_the_quest_dungeon_that_opens_the_boss_where_triage_defends():
    sim, basic, quest = chase_board(finished=False)
    dungeons = list(sim.dungeons.values())
    assert policies.triage(sim, dungeons) is basic, "set-up: triage defends"
    assert policies.chase_quests(sim, dungeons) is quest, (
        "a safe quest dungeon of a Cataclysm one clear from its count was "
        "standing, and the quest-chasing policy did not take it.")


def test_control_a_finished_cataclysms_quest_dungeon_is_not_chased():
    sim, basic, _ = chase_board(finished=True)
    dungeons = list(sim.dungeons.values())
    assert policies.chase_quests(sim, dungeons) is policies.triage(sim, dungeons) is basic
