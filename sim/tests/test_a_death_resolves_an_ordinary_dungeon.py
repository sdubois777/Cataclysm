"""A death inside an ordinary dungeon resolves it at once. Issue #41.

THE GAME HAS DONE THIS SINCE 2026-10-10 AND THE MODEL DID NOT. In
`Simulation._finish_current` a death left the dungeon, charged
`death_day_cost` days and touched no city, so dying was cheaper here than in
play. `TuningConfig.death_resolves_dungeon` now turns the game's rule on, and it
defaults to on:

  * an ORDINARY dungeon resolves through `Simulation._resolve`, the call a timer
    running out makes, so its consequence lands on its city once and whether it
    then stays is `dungeon_persists_after_resolve`, as for a timer;
  * a QUEST dungeon and a FALLEN CITY dungeon resolve nothing: the days are
    charged and no city pays;
  * the CATACLYSM dungeon ends the run, as before;
  * the order is resolve, leave, charge the days.

EVERY DEATH HERE IS CERTAIN AND NONE IS ROLLED. `death_chance` is replaced on
the simulation, the way `test_population_scales_empire_points.py` replaces it to
make a clear certain, and the death is reached through `_finish_current`, which
is the only place the model can die. No test waits for a campaign to kill
somebody.

EVERY CASE HAS A CONTROL WITH THE SETTING OFF, built from the same seed, so a
test that would pass with the new call deleted says so by its control failing
to differ.

`tools/tests/test_a_death_resolves_in_the_model_as_in_the_game.py` reads this
function and the game's as text and holds the order and the kind-question in
both. This file measures what comes out.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim.config import (
    LETHALITY_RULES, DungeonType, LethalityMode, TuningConfig,
)
from cataclysm_sim.engine import Dungeon, Simulation

SEED = 3


def config(resolves: bool, **overrides) -> TuningConfig:
    return dataclasses.replace(TuningConfig(), death_resolves_dungeon=resolves,
                               **overrides)


def simulation(resolves: bool, **overrides) -> Simulation:
    """A simulation in which every attempt kills the player.

    NO SURGE IS DUE. The first `step` of a fresh simulation lands a wave, and a
    wave's dungeons on the city under test would be a second thing acting on
    it. The clock is pushed out of reach instead; the board is never empty in
    these tests, so the board-empty trigger has nothing to fire on.
    """
    sim = Simulation(config(resolves, **overrides), seed=SEED)
    sim.death_chance = lambda d: 1.0        # type: ignore[method-assign]
    sim.next_surge_day = float("inf")
    return sim


def an_outpost(sim: Simulation):
    """A standing city that is not the Pillar, the same one for a given seed."""
    for city in sim.empire.cities.values():
        if not city.fallen and city.cid != sim.empire.pillar_id:
            return city
    raise AssertionError("no standing city but the Pillar")


def a_dungeon(sim: Simulation, dtype: DungeonType, city) -> Dungeon:
    """One dungeon of a kind, with no sub-type.

    A SIEGE BITES ITS CITY EVERY DAY, which would be a second thing taking from
    the city in the tests that let days pass.
    """
    d = sim._make_dungeon(dtype, city)
    d.subtype = "None"
    return d


def die_in(sim: Simulation, d: Dungeon) -> None:
    """Walk into `d` and die there, through the path a campaign takes."""
    sim.current = d
    sim.days_remaining = 0
    sim._finish_current()


def health_of(sim: Simulation) -> dict[int, tuple[float, float, bool]]:
    """Every city's defence, population and whether it has fallen."""
    return {cid: (c.defense, c.population, c.fallen)
            for cid, c in sim.empire.cities.items()}


def nobody_decides(sim, dungeons):
    """A policy that never enters anything, so days pass and nothing else."""
    return None


def what_a_timer_takes(**overrides) -> tuple[float, float]:
    """What `_resolve` takes from the city for the dungeon these tests build.

    MEASURED ON A TWIN AND NOT WORKED OUT. The twin has the same seed, makes
    the same dungeon on the same city and runs its timer out, so the figure is
    whatever `_resolve` does today and no arithmetic is copied into this file.
    """
    twin = simulation(False, **overrides)
    city = an_outpost(twin)
    d = a_dungeon(twin, DungeonType.BASIC, city)
    before = (city.defense, city.population)
    twin._resolve(d)
    return (before[0] - city.defense, before[1] - city.population)


class TestADeathInAnOrdinaryDungeon:
    def test_its_consequence_lands_on_its_city_once(self):
        sim = simulation(True)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        before = (city.defense, city.population)

        die_in(sim, d)

        taken = (before[0] - city.defense, before[1] - city.population)
        timer = what_a_timer_takes()

        assert timer[0] > 0 and timer[1] > 0, (
            "a timer running out on the twin took nothing from the city, so "
            "the comparison below would pass for a death that took nothing")
        assert taken == pytest.approx(timer), (
            f"a death in an ordinary dungeon took {taken} from its city and "
            f"its timer running out takes {timer}. The two are one rule: the "
            "death resolves the dungeon through Simulation._resolve")
        assert sim.resolved == 1, (
            f"one death raised the tally of detonations by {sim.resolved}")
        assert d.times_resolved == 1
        assert sim.deaths == 1

    def test_no_other_city_pays(self):
        sim = simulation(True)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        before = health_of(sim)

        die_in(sim, d)

        after = health_of(sim)
        moved = [cid for cid in before if before[cid] != after[cid]]
        assert moved == [city.cid], (
            f"a death in a dungeon on city {city.cid} changed cities {moved}")

    def test_with_the_setting_off_the_city_pays_nothing(self):
        """**THE CONTROL.** The model as it was before 2026-10-12."""
        sim = simulation(False)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        d.resolve_in = 3.0
        before = health_of(sim)

        die_in(sim, d)

        assert health_of(sim) == before, (
            "with death_resolves_dungeon off a death still took something "
            "from a city")
        assert sim.resolved == 0
        assert d.times_resolved == 0
        assert d.resolve_in == 3.0, (
            "with death_resolves_dungeon off a death still moved the timer")
        assert d.did in sim.dungeons
        assert sim.deaths == 1
        assert sim.dying and sim.days_remaining == sim.cfg.death_day_cost

    def test_the_setting_is_on_unless_somebody_turns_it_off(self):
        assert TuningConfig().death_resolves_dungeon is True, (
            "the game resolves an ordinary dungeon on a death, and the model's "
            "default no longer does")

    def test_its_timer_is_full_afterwards_and_it_stays(self):
        """With a resolved dungeon staying, which was the default until
        2026-10-12 and is what the game still does. At the default now the
        dungeon leaves: the test below, and
        `test_an_ordinary_dungeon_leaves_after_it_hits_its_city.py`."""
        sim = simulation(True, dungeon_persists_after_resolve=True)
        assert sim.cfg.dungeon_persists_after_resolve is True
        d = a_dungeon(sim, DungeonType.BASIC, an_outpost(sim))
        d.resolve_in = 2.0
        assert d.resolve_max > 2, "the dungeon's full timer is not above 2 days"

        die_in(sim, d)

        assert d.did in sim.dungeons, (
            "a resolved dungeon is kept by dungeon_persists_after_resolve and "
            "the death removed it")
        assert d.resolve_in == float(d.resolve_max), (
            f"after the death the timer reads {d.resolve_in} of "
            f"{d.resolve_max}. The days a death costs would then run against "
            "what the timer had left")

    def test_it_is_gone_afterwards_when_a_resolved_dungeon_does_not_stay(self):
        """The default since 2026-10-12, and still set by name here."""
        assert TuningConfig().dungeon_persists_after_resolve is False
        sim = simulation(True, dungeon_persists_after_resolve=False)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        before = city.defense

        die_in(sim, d)

        assert d.did not in sim.dungeons, (
            "dungeon_persists_after_resolve is off, so a timer running out "
            "removes the dungeon, and a death left it standing")
        assert city.defense < before
        assert not city.fallen
        assert sim.current is None

    def test_the_player_has_left_and_is_reviving(self):
        sim = simulation(True)
        d = a_dungeon(sim, DungeonType.BASIC, an_outpost(sim))

        die_in(sim, d)

        assert sim.current is None
        assert sim.dying
        assert not sim.lost and not sim.won
        assert sim.cleared == 0, "a death was counted as a clear"


class TestADeathWhoseResolveFellsTheCity:
    @pytest.mark.parametrize("persists", [True, False])
    def test_the_city_falls_and_the_dungeon_is_absorbed(self, persists):
        sim = simulation(True, dungeon_persists_after_resolve=persists)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        other = a_dungeon(sim, DungeonType.BASIC, city)
        city.defense = 0.001

        die_in(sim, d)

        assert city.fallen, (
            "a city with almost no defence left survived the resolve a death "
            "brings")
        assert d.did not in sim.dungeons, (
            "the death's resolve felled the city and the dungeon the player "
            "died in is still on the map. A fallen city absorbs every dungeon "
            "standing on it; the game's CityFell spares none")
        assert other.did not in sim.dungeons
        assert sim.current is None
        assert sim.dying and sim.days_remaining == sim.cfg.death_day_cost
        assert sim.resolved == 1

        ordinary = [x for x in sim.dungeons.values()
                    if x.city_id == city.cid and x.dtype is DungeonType.BASIC]
        assert ordinary == [], (
            "an ordinary dungeon still stands on the city that fell")
        if not city.erased:
            assert [x for x in sim.dungeons.values()
                    if x.city_id == city.cid
                    and x.dtype is DungeonType.FALLEN_CITY], (
                "the city fell and no Fallen City dungeon stands on it")

    def test_the_days_then_pass_without_an_error(self):
        sim = simulation(True)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        city.defense = 0.001

        die_in(sim, d)
        for _ in range(sim.cfg.death_day_cost):
            sim.step(nobody_decides)

        assert city.fallen
        assert not sim.dying
        assert d.did not in sim.dungeons

    def test_with_the_setting_off_the_city_stands(self):
        """**THE CONTROL.**"""
        sim = simulation(False)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        city.defense = 0.001

        die_in(sim, d)

        assert not city.fallen
        assert d.did in sim.dungeons


class TestADeathThatResolvesNothing:
    def both(self, build):
        """The same death with the setting on and off.

        Returns, for each, everything a resolve could have moved: every city,
        the dungeon's place and timer, the tallies, and the state of the random
        stream, which a Quest dungeon's relocation draws from.
        """
        out = []
        for resolves in (True, False):
            sim = simulation(resolves)
            d = build(sim)
            d.resolve_in = 3.0
            die_in(sim, d)
            out.append({
                "cities": health_of(sim),
                "city": d.city_id,
                "timer": d.resolve_in,
                "times": d.times_resolved,
                "resolved": sim.resolved,
                "on the map": d.did in sim.dungeons,
                "stream": sim.rng.getstate(),
                "dying": sim.dying,
                "days": sim.days_remaining,
                "inside": sim.current,
                "deaths": sim.deaths,
            })
        return out

    def test_a_quest_dungeon_charges_the_days_and_takes_nothing(self):
        def build(sim):
            return a_dungeon(sim, DungeonType.QUEST, an_outpost(sim))

        on, off = self.both(build)

        assert on == off, (
            "a death in a Quest dungeon did something with "
            "death_resolves_dungeon on that it does not do with it off: "
            + ", ".join(k for k in on if on[k] != off[k])
            + ". A Quest dungeon does not resolve; a death in one moves no "
            "timer, relocates nothing and draws nothing from the stream")
        assert on["timer"] == 3.0
        assert on["times"] == 0 and on["resolved"] == 0
        assert on["dying"] and on["days"] == TuningConfig().death_day_cost
        assert on["inside"] is None and on["on the map"]

    def test_a_quest_dungeons_death_leaves_every_city_as_it_was(self):
        sim = simulation(True)
        d = a_dungeon(sim, DungeonType.QUEST, an_outpost(sim))
        before = health_of(sim)

        die_in(sim, d)

        assert health_of(sim) == before

    def test_a_fallen_city_dungeon_charges_the_days_and_takes_nothing(self):
        def build(sim):
            city = an_outpost(sim)
            sim._fall(city)
            found = [x for x in sim.dungeons.values()
                     if x.city_id == city.cid
                     and x.dtype is DungeonType.FALLEN_CITY]
            assert len(found) == 1, (
                f"felling a city left {len(found)} Fallen City dungeons on it")
            return found[0]

        on, off = self.both(build)

        assert on == off, (
            "a death in a Fallen City dungeon did something with "
            "death_resolves_dungeon on that it does not do with it off: "
            + ", ".join(k for k in on if on[k] != off[k]))
        assert on["timer"] == 3.0, (
            "a death in a Fallen City dungeon moved its timer")
        assert on["times"] == 0 and on["resolved"] == 0
        assert on["dying"] and on["days"] == TuningConfig().death_day_cost
        assert on["inside"] is None and on["on the map"]

    def test_an_ordinary_dungeon_on_a_city_already_fallen_takes_nothing(self):
        """The walk began, the city fell, the player died.

        `_fall` spares the dungeon being walked, so this is reachable. The
        game's answer is the model's: the resolve finds nothing left to bite,
        counts no detonation and the timer is full again.
        """
        sim = simulation(True)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        sim.current = d
        sim._fall(city)
        assert d.did in sim.dungeons, (
            "_fall no longer spares the dungeon being walked, so this case "
            "cannot be reached and the test measures nothing")
        d.resolve_in = 3.0
        before = health_of(sim)

        die_in(sim, d)

        assert health_of(sim) == before
        assert sim.resolved == 0
        assert d.resolve_in == float(d.resolve_max)
        assert sim.dying and sim.current is None

    def test_a_dungeon_already_off_the_map_is_still_charged(self):
        """It detonated and left while the player was inside it."""
        sim = simulation(True, dungeon_persists_after_resolve=False)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        sim.current = d
        sim._resolve(d)
        assert d.did not in sim.dungeons
        before = health_of(sim)

        die_in(sim, d)

        assert health_of(sim) == before, (
            "a dungeon that had already left the map resolved a second time "
            "on the death")
        assert sim.resolved == 1
        assert sim.dying and sim.days_remaining == sim.cfg.death_day_cost
        assert sim.current is None


class TestADeathInTheCataclysmDungeon:
    @pytest.mark.parametrize("resolves", [True, False])
    def test_it_ends_the_run(self, resolves):
        sim = simulation(resolves)
        d = a_dungeon(sim, DungeonType.CATACLYSM,
                      sim.empire.cities[sim.empire.pillar_id])
        before = health_of(sim)

        die_in(sim, d)

        assert sim.lost, "dying in the Cataclysm dungeon no longer ends the run"
        assert not sim.won
        assert not sim.dying, (
            "a death that ended the run was also charged days")
        assert sim.current is None
        assert health_of(sim) == before
        assert sim.resolved == 0


class TestTheDaysADeathCosts:
    def days_spent_reviving(self, sim: Simulation, d: Dungeon) -> int:
        """Whole days from the death to the first day the player is free."""
        die_in(sim, d)
        spent = 0
        while sim.dying:
            sim.step(nobody_decides)
            spent += 1
            assert spent < 1000, "the player never finished reviving"
        return spent

    @pytest.mark.parametrize("mode", list(LethalityMode))
    @pytest.mark.parametrize("resolves", [True, False])
    @pytest.mark.parametrize("dtype", [DungeonType.BASIC, DungeonType.QUEST])
    def test_they_are_the_lethality_modes_days_either_way(
            self, mode, resolves, dtype):
        cfg = config(resolves).with_lethality(mode)
        sim = Simulation(cfg, seed=SEED)
        sim.death_chance = lambda d: 1.0    # type: ignore[method-assign]
        sim.next_surge_day = float("inf")
        d = a_dungeon(sim, dtype, an_outpost(sim))
        day_of_the_death = sim.day

        spent = self.days_spent_reviving(sim, d)

        assert spent == LETHALITY_RULES[mode].death_day_cost, (
            f"a {mode.value} death cost {spent} days with "
            f"death_resolves_dungeon={resolves}")
        assert sim.day == day_of_the_death + spent

    def test_the_consequence_lands_before_the_days_pass(self):
        """Its last line reads the refilled timer, so the dungeon is kept:
        `dungeon_persists_after_resolve` is set on, as the default was until
        2026-10-12."""
        sim = simulation(True, dungeon_persists_after_resolve=True)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        before = city.defense
        day = sim.day

        die_in(sim, d)

        assert city.defense < before, (
            "the death did not land the consequence at once")
        assert sim.day == day, "a day passed inside the death itself"
        assert sim.days_remaining == sim.cfg.death_day_cost, (
            "the consequence landed and some of the days had already been "
            "spent: the order is resolve, leave, charge the days")
        assert d.resolve_in == float(d.resolve_max)

    def test_the_full_timer_counts_down_through_the_days(self):
        """The player has left, so this dungeon's timer is no longer paused.

        The days charged run against the timer the death refilled, which is why
        the refill comes first. Standard's 5 days are fewer than any timer a
        dungeon here can have, so nothing resolves a second time.

        WITH A RESOLVED DUNGEON STAYING, set by name since 2026-10-12, when
        the default became that it leaves and there is no timer to count down.
        """
        sim = simulation(True, dungeon_persists_after_resolve=True)
        city = an_outpost(sim)
        d = a_dungeon(sim, DungeonType.BASIC, city)
        cost = sim.cfg.death_day_cost
        assert d.resolve_max > cost, (
            f"the dungeon's full timer, {d.resolve_max} days, is not above the "
            f"{cost} days a death costs, so it would resolve again inside them")

        die_in(sim, d)
        after_the_death = city.defense
        for _ in range(cost):
            sim.step(nobody_decides)

        assert d.resolve_in == float(d.resolve_max) - cost, (
            f"after the {cost} days the timer reads {d.resolve_in} of "
            f"{d.resolve_max}")
        assert city.defense == after_the_death, (
            "the city paid a second time inside the days the death cost")
        assert sim.resolved == 1
