"""An ordinary dungeon leaves the map after it hits its city.

THE OWNER'S RULE OF 2026-10-08, VERBATIM: "Ordinary dungeons will do their
city/population damage, and then despawn. Quest dungeons will do their
city/population damage, and either refresh their timer and stay on that city,
or potentially move to an adjacent city. Fallen city dungeons don't have a
resolution timer as they're the enemies now."

IN THE MODEL SINCE 2026-10-12. `TuningConfig.dungeon_persists_after_resolve`
now defaults to False, and its one read is the last statement of
`Simulation._resolve`, which is reached only for an ordinary dungeon on a city
that was standing when the hit landed. Set it True to measure the model as it
was: the dungeon stays with its timer full again.

THE GAME DOES NOT HAVE THIS RULE YET, ON PURPOSE.
`tools/tests/test_an_ordinary_dungeon_leaves_in_the_model_and_not_yet_in_the_game.py`
records that and says what to do when it gains it.

EVERY CASE HAS A CONTROL WITH THE SETTING ON "STAYS", built from the same
seed, so a test that would pass with the rule absent says so by its control
failing to differ.

NO WAVE LANDS UNLESS A TEST ASKS FOR ONE. The clock is pushed out of reach and
the board-empty trigger is switched off in every test but the one about it; a
dungeon leaving empties these small boards, and a wave landing on the city
under test would be a second thing acting on it.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim.config import DungeonType, LethalityMode, TuningConfig
from cataclysm_sim.engine import Dungeon, Simulation

SEED = 3

LEAVES = False
STAYS = True


def config(persists: bool, **overrides) -> TuningConfig:
    settings = dict(surge_on_empty_board=False)
    settings.update(overrides)
    return dataclasses.replace(
        TuningConfig(), dungeon_persists_after_resolve=persists, **settings)


def simulation(persists: bool, **overrides) -> Simulation:
    """An empty board on which no wave is due."""
    sim = Simulation(config(persists, **overrides), seed=SEED)
    sim.next_surge_day = float("inf")
    return sim


def an_outpost(sim: Simulation):
    """A standing city that is not the Pillar, the same one for a given seed."""
    for city in sim.empire.cities.values():
        if not city.fallen and city.cid != sim.empire.pillar_id:
            return city
    raise AssertionError("no standing city but the Pillar")


def a_dungeon(sim: Simulation, dtype: DungeonType, city,
              subtype: str = "None") -> Dungeon:
    d = sim._make_dungeon(dtype, city)
    d.subtype = subtype
    return d


def nobody_decides(sim, dungeons):
    """A policy that never enters anything, so days pass and nothing else."""
    return None


def pass_days(sim: Simulation, days: int) -> None:
    for _ in range(days):
        sim.step(nobody_decides)


def one_hit(**overrides) -> float:
    """The defence one resolve of the dungeon these tests build takes.

    MEASURED ON A TWIN AND NOT WORKED OUT, so no arithmetic of `_resolve` is
    copied into this file.
    """
    twin = simulation(STAYS, **overrides)
    city = an_outpost(twin)
    d = a_dungeon(twin, DungeonType.BASIC, city)
    before = city.defense
    twin._resolve(d)
    assert city.defense < before, "a resolve on the twin took no defence"
    return before - city.defense


class TestTheDefault:
    def test_a_resolved_ordinary_dungeon_leaves_unless_somebody_says_stay(self):
        assert TuningConfig().dungeon_persists_after_resolve is False, (
            "the owner ruled on 2026-10-08 that an ordinary dungeon does its "
            "damage and then despawns, and the model's default no longer "
            "removes it")


class TestAnOrdinaryDungeonOnAStandingCity:
    def run_past_two_timers(self, persists: bool):
        """One dungeon, nobody enters it, and enough days for two timers."""
        sim = simulation(persists)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        before = city.defense
        pass_days(sim, 2 * d.resolve_max + 2)
        return sim, city, d, before - city.defense

    def test_it_hits_once_and_is_gone(self):
        sim, city, d, taken = self.run_past_two_timers(LEAVES)

        assert d.did not in sim.dungeons, (
            "an ordinary dungeon hit its city and is still on the map")
        assert not city.fallen, (
            "the city fell, so this is the absorbed case and not this one")
        assert taken == pytest.approx(one_hit()), (
            f"over two of its timers the dungeon took {taken} defence and one "
            f"hit takes {one_hit()}. A dungeon that leaves hits once")
        assert sim.resolved == 1
        assert d.times_resolved == 1

    def test_with_the_setting_on_stays_it_hits_twice_and_is_still_there(self):
        """**THE CONTROL.** The model as it was before 2026-10-12."""
        sim, city, d, taken = self.run_past_two_timers(STAYS)

        assert d.did in sim.dungeons
        assert not city.fallen
        assert taken == pytest.approx(2 * one_hit()), (
            f"with the setting on 'stays' the dungeon took {taken} defence "
            f"over two of its timers and two hits take {2 * one_hit()}")
        assert sim.resolved == 2
        assert d.times_resolved == 2

    def test_with_the_setting_on_stays_its_timer_is_full_again(self):
        sim = simulation(STAYS)
        d = a_dungeon(sim, DungeonType.BASIC, an_outpost(sim))
        d.resolve_in = 1.0
        assert d.resolve_max > 1

        pass_days(sim, 1)

        assert d.did in sim.dungeons
        assert d.resolve_in == float(d.resolve_max)

    def test_it_takes_people_once_as_well(self):
        took = {}
        for persists in (LEAVES, STAYS):
            sim = simulation(persists)
            city = an_outpost(sim)
            d = a_dungeon(sim, DungeonType.BASIC, city)
            before = city.population
            pass_days(sim, 2 * d.resolve_max + 2)
            took[persists] = before - city.population

        assert took[LEAVES] > 0
        assert took[STAYS] == pytest.approx(2 * took[LEAVES]), (
            f"a dungeon that leaves took {took[LEAVES]} people and one that "
            f"stays took {took[STAYS]} over two timers")

    def test_no_other_city_pays_and_no_other_dungeon_leaves(self):
        sim = simulation(LEAVES)
        city = an_outpost(sim)
        others = [c for c in sim.empire.cities.values()
                  if not c.fallen and c.cid not in (city.cid,
                                                    sim.empire.pillar_id)]
        other = a_dungeon(sim, DungeonType.BASIC, others[0])
        beside = a_dungeon(sim, DungeonType.BASIC, city)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        d.resolve_in = 1.0
        assert other.resolve_in > 1 and beside.resolve_in > 1
        before = {c.cid: c.defense for c in sim.empire.cities.values()}

        pass_days(sim, 1)

        moved = [cid for cid, was in before.items()
                 if sim.empire.cities[cid].defense != was]
        assert moved == [city.cid]
        assert d.did not in sim.dungeons
        assert other.did in sim.dungeons and beside.did in sim.dungeons, (
            "one dungeon's timer ran out and another dungeon left the map")

    def test_leaving_is_not_a_clear(self):
        """A ruling of 2026-10-12: a dungeon removed by resolving does not
        count toward the Cataclysm boss's floors. The boss grows on dungeons
        defeated, and nobody defeated this one."""
        sim = simulation(LEAVES)
        d = a_dungeon(sim, DungeonType.BASIC, an_outpost(sim))
        d.resolve_in = 1.0

        pass_days(sim, 1)

        assert d.did not in sim.dungeons
        assert sim.basic_cleared == 0, (
            "a dungeon that hit its city and left was counted toward the "
            "Cataclysm boss's floors")
        assert sim.cleared == 0 and sim.floors_cleared == 0
        assert sim.empire_points == 0.0 and sim.materials == 0.0

    def test_a_clear_does_count(self):
        """**THE CONTROL** on the test above, which a tally that never rose
        would pass."""
        sim = simulation(LEAVES)
        sim.death_chance = lambda d: 0.0        # type: ignore[method-assign]
        d = a_dungeon(sim, DungeonType.BASIC, an_outpost(sim))
        sim.current = d
        sim.days_remaining = 0

        sim._finish_current()

        assert sim.basic_cleared == 1 and sim.cleared == 1

    def test_the_dungeon_being_walked_does_not_leave_by_its_timer(self):
        """Its timer is paused while the player is inside, so it cannot run
        out, hit and leave underneath them."""
        sim = simulation(LEAVES)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        d.resolve_in = 1.0
        sim.current = d
        sim.days_remaining = 5
        before = city.defense

        pass_days(sim, 3)

        assert d.did in sim.dungeons
        assert d.resolve_in == 1.0
        assert city.defense == before


class TestTheKindsThatDoNotChange:
    def after_a_timer(self, build):
        """The same timer running out with the setting on each value.

        Returns, for each, everything a resolve could have moved, and the
        state of the random stream a Quest dungeon's relocation draws from.
        """
        out = []
        for persists in (LEAVES, STAYS):
            sim = simulation(persists)
            d = build(sim)
            d.resolve_in = 1.0
            pass_days(sim, 1)
            out.append({
                "cities": {cid: (c.defense, c.population, c.fallen)
                           for cid, c in sim.empire.cities.items()},
                "on the map": d.did in sim.dungeons,
                "city": d.city_id,
                "timer": d.resolve_in,
                "full": d.resolve_in == float(d.resolve_max),
                "times": d.times_resolved,
                "resolved": sim.resolved,
                "stream": sim.rng.getstate(),
                "dungeons": sorted(sim.dungeons),
            })
        return out

    def test_a_quest_dungeon_stays_with_its_timer_full(self):
        def build(sim):
            return a_dungeon(sim, DungeonType.QUEST, an_outpost(sim))

        leaves, stays = self.after_a_timer(build)

        assert leaves == stays, (
            "a Quest dungeon's timer running out did something with "
            "dungeon_persists_after_resolve off that it does not do with it "
            "on: " + ", ".join(k for k in leaves if leaves[k] != stays[k]))
        assert leaves["on the map"], (
            "a Quest dungeon left the map when its timer ran out. Only an "
            "ordinary dungeon leaves")
        assert leaves["full"]
        # NOT COUNTED AS AN ORDINARY DETONATION. Since 2026-10-13 the timer
        # does hit the city; that is counted in `quest_hits` and tested in
        # `test_a_quest_dungeons_timer_hits_its_host_city.py`.
        assert leaves["resolved"] == 0

    def test_a_fallen_city_dungeon_stays_with_its_timer_full(self):
        def build(sim):
            city = an_outpost(sim)
            sim._fall(city)
            found = [x for x in sim.dungeons.values()
                     if x.city_id == city.cid
                     and x.dtype is DungeonType.FALLEN_CITY]
            assert len(found) == 1, (
                f"felling a city left {len(found)} Fallen City dungeons on it")
            return found[0]

        leaves, stays = self.after_a_timer(build)

        assert leaves == stays, (
            "a Fallen City dungeon's timer running out did something with "
            "dungeon_persists_after_resolve off that it does not do with it "
            "on: " + ", ".join(k for k in leaves if leaves[k] != stays[k]))
        assert leaves["on the map"], (
            "a Fallen City dungeon left the map when its timer ran out")
        assert leaves["full"]
        assert leaves["times"] == 1 and leaves["resolved"] == 0

    def test_an_ordinary_dungeon_on_a_fallen_city_is_not_removed(self):
        """The rule is about a hit, and this timer hits nothing.

        `_fall` spares the dungeon being walked, so an ordinary dungeon can
        stand on a fallen city. Its timer running out finds nothing left to
        hit, counts no detonation and refills, as before; the game returns at
        the same place.
        """
        leaves, stays = self.after_a_timer(self.walked_on_a_fallen_city)

        assert leaves == stays
        assert leaves["on the map"] and leaves["full"]
        assert leaves["resolved"] == 0

    @staticmethod
    def walked_on_a_fallen_city(sim):
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        sim.current = d
        sim._fall(city)
        assert d.did in sim.dungeons, (
            "_fall no longer spares the dungeon being walked, so this case "
            "cannot be reached and the test measures nothing")
        sim.current = None
        return d


class TestTheHitThatFellsTheCity:
    @pytest.mark.parametrize("persists", [LEAVES, STAYS])
    def test_the_fall_absorbs_the_dungeon_under_either_setting(self, persists):
        sim = simulation(persists, surge_on_city_fall=False)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        other = a_dungeon(sim, DungeonType.BASIC, city)
        quest = a_dungeon(sim, DungeonType.QUEST, city)
        d.resolve_in = 1.0
        city.defense = 0.001

        pass_days(sim, 1)

        assert city.fallen
        assert d.did not in sim.dungeons
        assert other.did not in sim.dungeons and quest.did not in sim.dungeons, (
            "a fallen city absorbs every dungeon standing on it")
        assert sim.resolved == 1
        standing = [x for x in sim.dungeons.values() if x.city_id == city.cid]
        if city.erased:
            assert standing == []
        else:
            assert [x.dtype for x in standing] == [DungeonType.FALLEN_CITY], (
                "the city fell and what stands on it is not one Fallen City "
                "dungeon")

    def test_the_two_settings_leave_the_same_board(self):
        """**THE CONTROL.** The fall runs before the setting is read, so the
        dungeon that fells its city is absorbed by the fall, as it was."""
        boards = []
        for persists in (LEAVES, STAYS):
            sim = simulation(persists, surge_on_city_fall=False)
            city = an_outpost(sim)
            d = a_dungeon(sim, DungeonType.BASIC, city)
            d.resolve_in = 1.0
            city.defense = 0.001
            pass_days(sim, 1)
            boards.append(sorted((x.did, x.dtype, x.city_id, x.resolve_in)
                                 for x in sim.dungeons.values()))

        assert boards[0] == boards[1]
        assert boards[0], "nothing stands on the fallen city in either"


class TestAnOrdinaryDungeonCarryingASiege:
    def besieged(self, persists: bool):
        sim = simulation(persists)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city, subtype="Siege")
        d.resolve_in = 1.0
        assert d.resolve_max > 4, (
            "the dungeon's full timer is not above the days this test waits, "
            "so under 'stays' it would hit a second time inside them")
        assert sim.sieges_on(city.cid) == 1
        pass_days(sim, 1)            # the Siege's day's share, then the hit
        after_the_hit = city.defense
        pass_days(sim, 3)
        return sim, city, d, after_the_hit

    def test_it_leaves_when_its_timer_runs_out_and_the_daily_damage_stops(self):
        sim, city, d, after_the_hit = self.besieged(LEAVES)

        assert not city.fallen
        assert d.did not in sim.dungeons, (
            "an ordinary dungeon carrying a Siege hit its city and stayed. "
            "The owner's sentence has no exception for a Siege")
        assert sim.sieges_on(city.cid) == 0
        assert city.defense == after_the_hit, (
            f"three days after the besieging dungeon left, its city's defence "
            f"went from {after_the_hit} to {city.defense}")

    def test_with_the_setting_on_stays_the_daily_damage_goes_on(self):
        """**THE CONTROL.**"""
        sim, city, d, after_the_hit = self.besieged(STAYS)

        assert d.did in sim.dungeons
        assert sim.sieges_on(city.cid) == 1
        assert city.defense < after_the_hit, (
            "with the setting on 'stays' a Siege took nothing in three days, "
            "so the test above would pass for a Siege that never bites")
        assert sim.resolved == 1, (
            "the defence lost over the three days was a second hit and not "
            "the Siege")


class TestADeathInAnOrdinaryDungeon:
    def die(self, persists: bool):
        """A Heretic death, 15 days, in a dungeon whose full timer is 3.

        SO UNDER "STAYS" THE DAYS CHARGED RUN THE TIMER OUT AGAIN. That is the
        second hit the Heretic entry of 2026-10-09 describes, and it is what
        makes the control differ.
        """
        cfg = config(persists).with_lethality(LethalityMode.HERETIC)
        sim = Simulation(cfg, seed=SEED)
        sim.next_surge_day = float("inf")
        sim.death_chance = lambda d: 1.0        # type: ignore[method-assign]
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        d.resolve_max = 3
        before = city.defense
        cost = sim.cfg.death_day_cost
        assert cost > 2 * d.resolve_max

        sim.current = d
        sim.days_remaining = 0
        sim._finish_current()
        at_the_death = before - city.defense
        on_the_map = d.did in sim.dungeons
        pass_days(sim, cost)

        assert not sim.dying, "the death's days did not finish"
        assert not city.fallen
        return sim, d, at_the_death, before - city.defense, on_the_map

    def test_it_hits_once_and_the_dungeon_is_gone(self):
        sim, d, at_the_death, in_all, on_the_map = self.die(LEAVES)

        assert at_the_death == pytest.approx(one_hit())
        assert not on_the_map, (
            "a death resolved an ordinary dungeon and it is still on the map")
        assert sim.current is None
        assert in_all == pytest.approx(at_the_death), (
            f"the death took {at_the_death} defence and by the end of the "
            f"days it cost the city had lost {in_all}. Nothing is left to hit "
            "during a death's days")
        assert sim.resolved == 1
        assert sim.deaths == 1

    def test_with_the_setting_on_stays_it_hits_again_inside_the_days(self):
        """**THE CONTROL.**"""
        sim, d, at_the_death, in_all, on_the_map = self.die(STAYS)

        assert on_the_map and d.did in sim.dungeons
        assert at_the_death == pytest.approx(one_hit())
        assert in_all > at_the_death, (
            "with the setting on 'stays' a dungeon whose timer is 3 days hit "
            "nothing more inside a 15-day death, so the test above would pass "
            "whatever the rule did")
        assert sim.resolved > 1


class TestTheBoardEmptiedByAHit:
    def last_timer_runs_out(self, persists: bool) -> Simulation:
        """The only dungeon on the map hits its city, 999 days after a wave."""
        sim = Simulation(dataclasses.replace(
            TuningConfig(), dungeon_persists_after_resolve=persists), seed=SEED)
        assert sim.cfg.surge_on_empty_board is True
        sim.day = 1000
        sim.next_surge_day = 1e9
        sim.surge_log = [(1, sim.cfg.surge_interval_days, 4)]
        d = a_dungeon(sim, DungeonType.BASIC, an_outpost(sim))
        d.resolve_in = 1.0
        pass_days(sim, 1)
        assert d.times_resolved == 1
        return sim

    def test_it_fires_the_empty_board_surge(self):
        sim = self.last_timer_runs_out(LEAVES)

        assert sim.surges_from_empty_board == 1, (
            "the last dungeon hit its city and left, the board was empty, and "
            "no surge followed. The owner's rule of 2026-09-07: \"Anytime "
            "there are no longer dungeons on the board, a surge happens.\"")
        assert sim.dungeons, "the surge that fired landed nothing"

    def test_with_the_setting_on_stays_the_board_is_not_empty(self):
        """**THE CONTROL.**"""
        sim = self.last_timer_runs_out(STAYS)

        assert sim.surges_from_empty_board == 0
        assert len(sim.dungeons) == 1
