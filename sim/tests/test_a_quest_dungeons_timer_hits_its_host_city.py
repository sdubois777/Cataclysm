"""A Quest dungeon's timer running out hits the city it stands on.

THE OWNER, 2026-10-08, VERBATIM: "Ordinary dungeons will do their
city/population damage, and then despawn. Quest dungeons will do their
city/population damage, and either refresh their timer and stay on that city,
or potentially move to an adjacent city. Fallen city dungeons don't have a
resolution timer as they're the enemies now."

IN THE MODEL SINCE 2026-10-13. Until then a Quest dungeon's timer cost its city
nothing, which a recommendation the owner accepted on 2026-10-09 had kept. On
2026-10-13 the owner accepted the coordinating session's recommendation, in
that session's words: a Quest dungeon HITS its city when its timer runs out, at
the size of an ordinary dungeon's hit for that city, then refreshes or moves as
before. `TuningConfig.quest_timer_hits_city` is the setting and
`Simulation._quest_hit` is the hit.

THE READINGS THIS FILE HOLDS, each a labelled judgement recorded in
`docs/DECISIONS.md`, 2026-10-13:

  * the size is the ORDINARY row of the HOST city's tier when the hit lands;
  * it is NOT scaled by the Quest dungeon's depth
    (`quest_hit_scales_with_depth` is the other reading);
  * the tree's damage reduction applies;
  * a hit that fells the city has the fall absorb the Quest dungeon, unless the
    player is inside it;
  * the hit is counted in `quest_hits` and not in `resolved`.

THE GAME DOES NOT HAVE THIS RULE.
`tools/tests/test_a_quest_dungeons_timer_hits_in_the_model_and_not_yet_in_the_game.py`
records that.

EVERY CASE HAS A CONTROL WITH THE HIT SWITCHED OFF, or a twin that differs in
the one thing the test is about.

NO WAVE LANDS. The clock is pushed out of reach and the board-empty trigger is
switched off, so nothing but the dungeon under test acts on its city.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim.config import CityTier, DungeonType, EmpireTree, TuningConfig
from cataclysm_sim.engine import Dungeon, Simulation

SEED = 3

HITS = True
COSTS_NOTHING = False


def config(hits: bool, **overrides) -> TuningConfig:
    settings = dict(surge_on_empty_board=False)
    settings.update(overrides)
    return dataclasses.replace(
        TuningConfig(), quest_timer_hits_city=hits, **settings)


def simulation(hits: bool, **overrides) -> Simulation:
    """An empty board on which no wave is due."""
    sim = Simulation(config(hits, **overrides), seed=SEED)
    sim.next_surge_day = float("inf")
    return sim


def a_city(sim: Simulation, tier: CityTier = CityTier.OUTPOST):
    """A standing city of this tier, the same one for a given seed."""
    for city in sim.empire.cities.values():
        if city.tier is tier and not city.fallen:
            return city
    raise AssertionError(f"no standing {tier}")


def a_quest_dungeon(sim: Simulation, city, subtype: str = "None") -> Dungeon:
    d = sim._make_dungeon(DungeonType.QUEST, city)
    d.subtype = subtype
    return d


def health_of(sim: Simulation) -> dict:
    return {cid: (c.defense, c.population, c.fallen)
            for cid, c in sim.empire.cities.items()}


def ordinary_row(sim: Simulation, city):
    return sim.cfg.spec(DungeonType.BASIC, city.tier)


def nobody_decides(sim, dungeons):
    return None


class TestTheDefault:
    def test_a_quest_dungeons_timer_hits_unless_somebody_switches_it_off(self):
        assert TuningConfig().quest_timer_hits_city is True, (
            "the model's default no longer has a Quest dungeon's timer hit "
            "its city. The owner's sentence of 2026-10-08 says it does its "
            "city and population damage, and the recommendation accepted on "
            "2026-10-13 returned the model to that")

    def test_the_hit_is_not_scaled_by_depth_unless_somebody_asks(self):
        assert TuningConfig().quest_hit_scales_with_depth is False, (
            "the reading taken on 2026-10-13 is 'an ordinary dungeon's size', "
            "the ordinary row at a scale of 1.0. True is the other reading "
            "and is for measurement")


class TestTheHit:
    def one_timer(self, hits: bool, **overrides):
        sim = simulation(hits, **overrides)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        before = (city.defense, city.population)
        sim._resolve(d)
        return sim, city, d, (before[0] - city.defense,
                              before[1] - city.population)

    def test_it_takes_the_ordinary_row_of_its_host_and_no_more(self):
        sim, city, d, (defence, people) = self.one_timer(HITS)
        row = ordinary_row(sim, city)

        assert row.defense_damage > 0 and row.population_damage > 0
        assert defence == pytest.approx(row.defense_damage), (
            f"a Quest dungeon's timer took {defence} defence from an "
            f"{city.tier}; an ordinary dungeon's row for that tier is "
            f"{row.defense_damage}")
        assert people == pytest.approx(row.population_damage)
        assert not city.fallen

    def test_with_the_hit_switched_off_no_city_pays(self):
        """**THE CONTROL.** The model as it was before 2026-10-13."""
        sim = simulation(COSTS_NOTHING)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        before = health_of(sim)

        sim._resolve(d)

        assert health_of(sim) == before
        assert sim.quest_hits == 0 and sim.resolved == 0

    def test_no_other_city_pays(self):
        sim = simulation(HITS)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        before = health_of(sim)

        sim._resolve(d)

        changed = [cid for cid, was in before.items()
                   if health_of(sim)[cid] != was]
        assert changed == [city.cid]

    def test_it_is_an_ordinary_dungeons_hit_at_the_middle_of_its_range(self):
        """Measured against a real ordinary dungeon rather than against a row.

        The twin's ordinary dungeon is given the midpoint of its floor range,
        which is the depth at which `_resolve` scales an ordinary hit by 1.0.
        """
        _, _, _, (defence, people) = self.one_timer(HITS)

        twin = simulation(HITS)
        city = a_city(twin)
        ordinary = twin._make_dungeon(DungeonType.BASIC, city)
        lo, hi = ordinary_row(twin, city).floors
        ordinary.floors = (lo + hi) / 2.0
        before = (city.defense, city.population)
        twin._resolve(ordinary)

        assert before[0] - city.defense == pytest.approx(defence)
        assert before[1] - city.population == pytest.approx(people)

    def test_its_depth_does_not_change_the_hit(self):
        taken = []
        for floors in (20, 30):
            sim = simulation(HITS)
            city = a_city(sim)
            d = a_quest_dungeon(sim, city)
            d.floors = floors
            before = city.defense
            sim._resolve(d)
            taken.append(before - city.defense)

        assert taken[0] == pytest.approx(taken[1]), (
            f"a 20 floor Quest dungeon took {taken[0]} and a 30 floor one "
            f"{taken[1]}. The reading taken is an ordinary dungeon's size, "
            "whatever the Quest dungeon's depth")

    def test_the_other_reading_scales_it_by_depth_over_the_ordinary_middle(self):
        """**THE CONTROL ON THE TEST ABOVE**: the setting that would make the
        depth matter does make it matter, so the test above can fail."""
        taken = []
        for floors in (20, 30):
            sim = simulation(HITS, quest_hit_scales_with_depth=True)
            city = a_city(sim)
            d = a_quest_dungeon(sim, city)
            d.floors = floors
            before = city.defense
            sim._resolve(d)
            taken.append(before - city.defense)
            row = ordinary_row(sim, city)

        lo, hi = row.floors
        middle = (lo + hi) / 2.0
        assert taken[0] == pytest.approx(row.defense_damage * 20 / middle)
        assert taken[1] == pytest.approx(row.defense_damage * 30 / middle)
        assert taken[1] > taken[0] > row.defense_damage

    def test_the_trees_damage_reduction_applies(self):
        _, _, _, (plain, _) = self.one_timer(HITS)
        _, _, _, (reduced, _) = self.one_timer(
            HITS, tree=EmpireTree(name="half damage", city_damage_mult=0.5))

        assert reduced == pytest.approx(plain * 0.5), (
            f"with a tree that halves city damage a Quest dungeon's hit took "
            f"{reduced} against {plain} without it")


class TestWhoseTierSizesTheHit:
    def relocated_onto_a_bulwark(self, hits: bool):
        """A Quest dungeon rolled on an Outpost and now standing on a Bulwark.

        Only `city_id` is moved, which is all a relocation moves.
        """
        sim = simulation(hits, quest_relocates=False)
        outpost = a_city(sim, CityTier.OUTPOST)
        bulwark = a_city(sim, CityTier.BULWARK)
        d = a_quest_dungeon(sim, outpost)
        d.city_id = bulwark.cid
        assert d.city_tier is CityTier.OUTPOST
        return sim, outpost, bulwark, d

    def test_it_is_the_tier_of_the_city_it_stands_on_when_the_hit_lands(self):
        sim, outpost, bulwark, d = self.relocated_onto_a_bulwark(HITS)
        before = (outpost.defense, bulwark.defense)

        sim._resolve(d)

        assert outpost.defense == before[0], (
            "the city the Quest dungeon was rolled on paid for a hit that "
            "landed after it had moved away")
        taken = before[1] - bulwark.defense
        want = sim.cfg.spec(DungeonType.BASIC, CityTier.BULWARK).defense_damage
        not_this = sim.cfg.spec(DungeonType.BASIC,
                                CityTier.OUTPOST).defense_damage
        assert want != not_this
        assert taken == pytest.approx(want), (
            f"a Quest dungeon standing on a Bulwark took {taken}; an ordinary "
            f"dungeon's hit for a Bulwark is {want}, and for the Outpost its "
            f"depth was rolled from it is {not_this}")

    def test_switched_off_the_bulwark_pays_nothing(self):
        """**THE CONTROL.**"""
        sim, outpost, bulwark, d = self.relocated_onto_a_bulwark(COSTS_NOTHING)
        before = health_of(sim)

        sim._resolve(d)

        assert health_of(sim) == before


class TestThenItRefreshesAndMayMove:
    def after_one_timer(self, hits: bool) -> dict:
        sim = simulation(hits)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        d.resolve_in = 0.0
        sim._resolve(d)
        return {
            "on the map": d.did in sim.dungeons,
            "full": d.resolve_in == float(d.resolve_max),
            "stands on": d.city_id,
            "stream": sim.rng.getstate(),
            "dungeons": sorted(sim.dungeons),
            "times": d.times_resolved,
            "resolved": sim.resolved,
        }

    def test_it_stays_on_the_map_with_its_timer_full(self):
        after = self.after_one_timer(HITS)

        assert after["on the map"], (
            "a Quest dungeon left the map when its timer ran out. It hits, "
            "then its timer is full again")
        assert after["full"]

    def test_the_refresh_and_the_move_are_what_they_were_without_the_hit(self):
        """The hit draws nothing from the stream when the city stands, so the
        dungeon moves, or stays, exactly as it does with the hit switched off.
        """
        hit, not_hit = self.after_one_timer(HITS), self.after_one_timer(
            COSTS_NOTHING)

        assert hit == not_hit, (
            "apart from the city's health, a Quest dungeon's timer did "
            "something with the hit on that it does not do with it off: "
            + ", ".join(k for k in hit if hit[k] != not_hit[k]))

    def test_it_is_counted_as_a_quest_hit_and_not_as_an_ordinary_detonation(
            self):
        sim = simulation(HITS)
        d = a_quest_dungeon(sim, a_city(sim))

        sim._resolve(d)
        sim._resolve(d)

        assert sim.quest_hits == 2
        assert sim.resolved == 0, (
            "`resolved` counts ordinary dungeons; a Quest dungeon's hit is "
            "counted in `quest_hits`")
        assert d.times_resolved == 0

    def test_over_two_timers_it_hits_twice_where_an_ordinary_one_hits_once(
            self):
        """Through the day loop rather than by calling `_resolve`."""
        sim = simulation(HITS, quest_relocates=False)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        before = city.defense

        for _ in range(2 * d.resolve_max + 2):
            sim.step(nobody_decides)

        assert d.did in sim.dungeons
        assert before - city.defense == pytest.approx(
            2 * ordinary_row(sim, city).defense_damage)
        assert sim.quest_hits == 2


class TestTheHitThatFellsTheCity:
    def nearly_fallen(self, hits: bool, inside: bool = False):
        sim = simulation(hits)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        other = sim._make_dungeon(DungeonType.BASIC, city)
        city.defense = 1.0
        if inside:
            sim.current = d
        full = float(d.resolve_max)
        d.resolve_in = 0.0
        stream = sim.rng.getstate()
        sim._resolve(d)
        return sim, city, d, other, full, stream

    def test_the_city_falls_and_the_fall_absorbs_the_quest_dungeon(self):
        sim, city, d, other, _, stream = self.nearly_fallen(HITS)

        assert city.fallen
        assert d.did not in sim.dungeons, (
            "a Quest dungeon's hit felled its city and the dungeon is still "
            "on the map. A fall absorbs every dungeon on the city")
        assert other.did not in sim.dungeons
        assert d.city_id == city.cid, "an absorbed dungeon was moved"
        assert d.resolve_in == 0.0, "an absorbed dungeon had its timer filled"
        standing = [x for x in sim.dungeons.values()
                    if x.city_id == city.cid]
        assert [x.dtype for x in standing] == [DungeonType.FALLEN_CITY]

    def test_switched_off_the_city_stands_and_the_dungeon_stays(self):
        """**THE CONTROL.**"""
        sim, city, d, other, full, _ = self.nearly_fallen(COSTS_NOTHING)

        assert not city.fallen and city.defense == 1.0
        assert d.did in sim.dungeons and other.did in sim.dungeons
        assert d.resolve_in == full

    def test_the_one_the_player_is_inside_is_spared_and_refreshed(self):
        """`_fall` spares the dungeon being walked, as it always has. Reached
        in play only with `timer_ticks_while_running` on."""
        sim, city, d, other, full, _ = self.nearly_fallen(HITS, inside=True)

        assert city.fallen
        assert d.did in sim.dungeons
        assert other.did not in sim.dungeons
        assert d.resolve_in == full


class TestWhatDoesNotHit:
    def test_a_quest_dungeon_on_a_city_already_fallen_hits_nothing(self):
        sim = simulation(HITS)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        sim.current = d
        sim._fall(city)
        sim.current = None
        assert d.did in sim.dungeons and city.fallen
        before = health_of(sim)

        sim._resolve(d)

        assert health_of(sim) == before
        assert sim.quest_hits == 0
        assert d.resolve_in == float(d.resolve_max)

    def test_a_death_inside_one_still_resolves_nothing(self):
        """The game's rule, unchanged: a death resolves an ordinary dungeon and
        no other kind. The control is the timer on the same board, which does
        take something."""
        sim = simulation(HITS)
        city = a_city(sim)
        d = a_quest_dungeon(sim, city)
        before = health_of(sim)

        sim._resolve_on_death(d)

        assert health_of(sim) == before and sim.quest_hits == 0

        sim._resolve(d)

        assert health_of(sim) != before

    def test_a_siege_it_carries_takes_what_it_took_before(self):
        """One day with a Quest dungeon carrying a Siege and its timer far
        off: the daily Siege damage is the same with the hit on and off."""
        taken = {}
        for hits in (HITS, COSTS_NOTHING):
            sim = simulation(hits)
            city = a_city(sim)
            d = a_quest_dungeon(sim, city, subtype="Siege")
            d.resolve_in = 50.0
            before = city.defense
            sim.step(nobody_decides)
            taken[hits] = before - city.defense
            assert sim.quest_hits == 0

        assert taken[HITS] > 0
        assert taken[HITS] == taken[COSTS_NOTHING]


class TestOverAWholeCampaign:
    def campaign(self, hits: bool):
        cfg = dataclasses.replace(
            TuningConfig(), quest_timer_hits_city=hits, max_days=600)
        return Simulation(cfg, seed=SEED).run(nobody_decides)

    def test_quest_hits_land_and_the_result_carries_the_count(self):
        result = self.campaign(HITS)

        assert result.quest_hits > 0, (
            "600 days with nobody entering anything and no Quest dungeon's "
            "timer hit a city")

    def test_switched_off_none_land(self):
        """**THE CONTROL.**"""
        assert self.campaign(COSTS_NOTHING).quest_hits == 0
