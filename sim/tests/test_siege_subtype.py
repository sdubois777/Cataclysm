"""The Siege sub-type takes a share of its host every day it stands.

WHY THIS FILE EXISTS. Issue #1329. The model gave a sub-type no behaviour beyond
Cow Level's doubled walk and Sacrificial's doubled modifiers, so the Siege --
about 13 in every 100 dungeons that reach the map -- did nothing at all. The
project owner ruled on 2026-09-06, verbatim: "Yes — add it as part of this work".

WHY IT COULD NOT WAIT. Issue #1327 turned every other city damage number into
points so that raising a city's health would mean something. The Siege keeps a
share of the maximum, by the same owner's separate ruling, so it is the one
threat city health does not protect against. A measurement of how safe a fully
invested empire is that leaves the Siege out overstates that safety by
construction, and the combined defensive ceiling is exactly what #1327 is
measuring.

THIS IS AN ADDITION AND NOT A PORT, WHICH IS THE REVERSE OF THIS PROJECT'S USUAL
DIRECTION. There was no model code to copy. Every rule comes from
`docs/Cataclysm_GDD_v2.md` line 3744 and from the C++ that already implements it
in `game/Source/CataclysmEmpire/Empire/CataclysmEmpireRun.cpp`, so for once the
game is the reference and the model is the follower. Each test below names where
its rule came from.

WHAT IS DELIBERATELY NOT MODELLED. "Pauses city upgrades." The model has no city
upgrade system to pause -- `LethalityRules.city_upgrade_slots` is set by nothing
and read by nothing, which issue #318 records. `TestTheRuleThatIsNotModelled`
holds that omission in place so it stays known rather than silent.
"""

from __future__ import annotations

from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_NONE, CityTier, DungeonType, EmpireTree, TuningConfig,
)
from cataclysm_sim.engine import Simulation


def a_siege(sim: Simulation, tier: CityTier = CityTier.SANCTUARY,
            stood_for: int = 0):
    """Put one Siege on a city of that tier and return (dungeon, city)."""
    city = next(c for c in sim.empire.cities.values() if c.tier is tier)
    d = sim._make_dungeon(DungeonType.BASIC, city)
    d.subtype = "Siege"
    d.spawned_day = sim.day - stood_for
    return d, city


def sim_for(tree: EmpireTree = TREE_NONE, tier: int = 1) -> Simulation:
    return Simulation(replace(TuningConfig(), tier=tier).with_tree(tree),
                      seed=0)


class TestWhatItTakesEachDay:
    """`docs/Cataclysm_GDD_v2.md` line 3744: "Deals 1% damage to city defenses
    and population per day while active. Increases in power by 10 points per
    day.\""""

    def test_on_the_day_it_arrives_it_takes_the_flat_share_alone(self):
        """The growth counts from the day the Siege arrived, so its first day
        carries none of it. `FCataclysmDungeon::SpawnedDay` is the same rule."""
        sim = sim_for()
        d, city = a_siege(sim, stood_for=0)
        before = city.defense
        sim._apply_siege_damage()
        assert before - city.defense == pytest.approx(city.max_defense * 0.01)

    @pytest.mark.parametrize("days", [0, 1, 5, 20])
    def test_each_later_day_adds_ten_points(self, days):
        sim = sim_for()
        d, city = a_siege(sim, stood_for=days)
        before = city.defense
        sim._apply_siege_damage()
        assert before - city.defense == pytest.approx(
            city.max_defense * 0.01 + 10.0 * days)

    def test_it_takes_the_same_from_the_population(self):
        """Both halves apply to both pools. The design sentence names both, and
        the sentence about the growth has the damage as its subject."""
        sim = sim_for()
        d, city = a_siege(sim, stood_for=7)
        before = city.population
        sim._apply_siege_damage()
        assert before - city.population == pytest.approx(
            city.max_population * 0.01 + 70.0)

    def test_a_spawn_day_after_the_clock_does_not_heal_the_city(self):
        """A dungeon built by hand in a test can carry a spawn day later than
        the clock. A negative age would turn the growth into a repair."""
        sim = sim_for()
        d, city = a_siege(sim)
        d.spawned_day = sim.day + 50
        before = city.defense
        sim._apply_siege_damage()
        assert city.defense < before

    def test_a_city_with_no_siege_on_it_is_untouched(self):
        sim = sim_for()
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.SANCTUARY)
        before = city.defense, city.population
        sim._apply_siege_damage()
        assert (city.defense, city.population) == before

    def test_a_fallen_city_is_not_bitten_again(self):
        sim = sim_for()
        d, city = a_siege(sim, stood_for=3)
        city.fallen = True
        before = city.defense
        sim._apply_siege_damage()
        assert city.defense == before


class TestTheDeliberateException:
    """THE PERCENTAGE HERE IS NOT AN OVERSIGHT, AND THIS FILE IS WHERE THAT IS
    ENFORCED RATHER THAN ONLY EXPLAINED.

    Issue #1327 turned every other city damage number into points, precisely
    because a share of the city's own maximum divided out of how long the city
    survived and made every city-health upgrade worthless. The Siege keeps the
    share. The project owner ruled on 2026-09-05, verbatim: "Keep it as a
    deliberate exception (Recommended)", on the reasoning that a siege does not
    care how thick your walls are.

    Somebody measuring this later will find a city-health investment buying
    nothing against a Siege and it will look exactly like the defect #1327 was
    opened about. These tests say it is intended.
    """

    def days_to_empty(self, tree: EmpireTree) -> int:
        """Days of Siege a Sanctuary survives, driven through the engine."""
        sim = sim_for(tree)
        d, city = a_siege(sim)
        days = 0
        while city.defense > 0 and days < 100_000:
            sim._apply_siege_damage()
            sim.day += 1
            days += 1
        return days

    def test_the_flat_share_ignores_city_health_entirely(self):
        """WITHOUT THE GROWTH, a city with a hundred times the defence lasts
        exactly as long, because 1% of a bigger pool is a bigger bite. This is
        the property the owner chose."""
        no_growth = replace(TuningConfig(), tier=1,
                            siege_damage_growth_per_day=0.0)
        days = []
        for mult in (1.0, 5.9, 100.0):
            sim = Simulation(
                no_growth.with_tree(EmpireTree(name="t", city_health_mult=mult)),
                seed=0)
            d, city = a_siege(sim)
            n = 0
            while city.defense > 0 and n < 10_000:
                sim._apply_siege_damage()
                n += 1
            days.append(n)
        assert days[0] == days[1] == days[2] == 100, (
            f"city health changed how long a Siege takes: {days}. Either the "
            "owner's deliberate exception has been removed, or the flat share "
            "is no longer a share of the maximum.")

    def test_but_the_growth_half_is_protected_by_city_health(self):
        """The ten points a day ARE absolute, so a bigger pool absorbs them for
        longer. A Siege is therefore not wholly immune to city health -- only
        its percentage half is, which is what makes the exception a situation
        rather than a blanket."""
        plain = self.days_to_empty(EmpireTree(name="none"))
        sturdy = self.days_to_empty(EmpireTree(name="t", city_health_mult=5.9))
        assert sturdy > plain, (
            f"{sturdy} days against {plain}: the growth is no longer in points")

    def test_damage_reduction_still_works_against_a_siege(self):
        """A READING, NOT A RULING, and recorded as one in `docs/DECISIONS.md`.
        The owner's exception is that city HEALTH does not protect against a
        siege. Reducing the damage is a different claim from thickening the
        wall, so the eleven damage-reduction nodes still apply."""
        plain = self.days_to_empty(EmpireTree(name="none"))
        reduced = self.days_to_empty(
            EmpireTree(name="d", city_damage_mult=0.5))
        assert reduced > plain


class TestOnePerCity:
    """`docs/Cataclysm_GDD_v2.md` line 3744: "Max 1 per city.\""""

    def test_a_second_siege_is_never_rolled_onto_one_city(self):
        sim = sim_for()
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.OUTPOST)
        d = sim._make_dungeon(DungeonType.BASIC, city)
        d.subtype = "Siege"
        for _ in range(400):
            assert sim._roll_subtype(DungeonType.BASIC, city) != "Siege"

    def test_a_refused_siege_becomes_another_subtype_rather_than_none(self):
        """Every dungeon has a sub-type since issue #1293, so a refusal has to
        be redistributed. A dropped one would quietly reintroduce the no
        sub-type outcome that issue removed."""
        sim = sim_for()
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.OUTPOST)
        d = sim._make_dungeon(DungeonType.BASIC, city)
        d.subtype = "Siege"
        allowed = set(sim.cfg.SUBTYPE_SPAWN_WEIGHTS) - {"Siege"}
        for _ in range(400):
            assert sim._roll_subtype(DungeonType.BASIC, city) in allowed

    def test_the_cap_is_per_city_and_not_per_empire(self):
        sim = sim_for()
        cities = [c for c in sim.empire.cities.values()
                  if c.tier is CityTier.OUTPOST][:2]
        d = sim._make_dungeon(DungeonType.BASIC, cities[0])
        d.subtype = "Siege"
        assert sim.sieges_on(cities[0].cid) == 1
        assert sim.sieges_on(cities[1].cid) == 0
        # The second city may still be given one.
        assert any(sim._roll_subtype(DungeonType.BASIC, cities[1]) == "Siege"
                   for _ in range(400))


class TestTheShareThatReachesTheMap:
    def test_fewer_sieges_arrive_than_are_rolled(self):
        """THE CHECK ON THE PORT, and the reason it is worth running. The C++
        rolls Siege at 15 in 100 and lands about 12.7 in 100 on the map,
        measured over twenty campaigns, because a refused roll is
        redistributed. If the model's share did not move off 15 the cap would
        not be being reached and the rule would be present but idle.

        Measured here at 13.1% over twenty campaigns. The bound is loose on
        purpose -- this is a campaign statistic, not arithmetic.
        """
        cfg = replace(TuningConfig(), tier=1).with_tree(TREE_NONE)
        rolled = (100.0 * cfg.SUBTYPE_SPAWN_WEIGHTS["Siege"]
                  / sum(cfg.SUBTYPE_SPAWN_WEIGHTS.values()))
        seen = total = 0
        for seed in range(8):
            sim = Simulation(cfg, seed=seed)
            made = []
            original = sim._make_dungeon

            def wrapped(dtype, city, floors_mult=1.0, _o=original, _m=made):
                d = _o(dtype, city, floors_mult)
                _m.append(d)
                return d

            sim._make_dungeon = wrapped
            sim.run(policies.triage)
            seen += sum(1 for d in made if d.subtype == "Siege")
            total += len(made)

        arriving = 100.0 * seen / total
        assert rolled == pytest.approx(15.0)
        assert 10.0 < arriving < rolled, (
            f"Siege is rolled at {rolled:.1f}% and reaches the map at "
            f"{arriving:.1f}%. Below 10% something other than the cap is "
            f"removing them; at or above {rolled:.1f}% the cap is never "
            "reached and the one-per-city rule is not doing anything.")


class TestTheRuleThatIsNotModelled:
    def test_there_is_still_no_city_upgrade_system_to_pause(self):
        """"Pauses city upgrades" is the fourth rule on the design line and the
        one this model cannot express. Issue #318. Held here so that the
        omission stays known: if a city upgrade system is added and this test
        starts failing, the Siege needs the rule it is currently missing.
        """
        import pathlib

        root = pathlib.Path(__file__).resolve().parents[1] / "cataclysm_sim"
        readers = [p.name for p in root.glob("*.py")
                   if "city_upgrade_slots" in p.read_text(encoding="utf-8")
                   and p.name != "config.py"]
        assert readers == [], (
            f"{readers} now read city_upgrade_slots, so the model has a city "
            "upgrade system and the Siege must pause it. Issue #1329.")


class TestItReachesTheDayLoop:
    def test_a_siege_costs_a_city_defence_over_a_campaign(self):
        """A model field that nothing calls would pass every test above."""
        cfg = replace(TuningConfig(), tier=1).with_tree(TREE_NONE)
        without = replace(cfg, siege_defence_bite_per_day=0.0,
                          siege_population_bite_per_day=0.0,
                          siege_damage_growth_per_day=0.0)
        lost_with = sum(Simulation(cfg, seed=s).run(policies.triage).cities_lost
                        for s in range(30))
        lost_without = sum(
            Simulation(without, seed=s).run(policies.triage).cities_lost
            for s in range(30))
        assert lost_with > lost_without, (
            f"{lost_with} cities lost with Siege damage against "
            f"{lost_without} without it, so `_apply_siege_damage` is not "
            "reaching the day loop")
