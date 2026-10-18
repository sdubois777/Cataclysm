"""An ordinary Void dungeon adds 1 to 3 void stacks to its city instead of damage, and ten erase it.

THE OWNER, 2026-10-16, VERBATIM: "All void dungeons when they resolve add 1-3
void stacks to a city instead of doing damage to the city or population and at
10 stacks the city is erased." Ruled by the coordinating session the same day:
the stacks go by the third of the row's depth; a Void Siege's daily bite stays
damage; a city that falls with fewer than ten can be retaken, and retaking it
clears them. `TuningConfig.void_dungeons_add_stacks` and `void_stacks_to_erase`.

EVERY CASE HAS A CONTROL: the same dungeon sent by another Cataclysm.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation
from cataclysm_sim.patterns import PATTERNS


def simulation(**over) -> Simulation:
    cfg = replace(TuningConfig(), tier=1, surge_on_city_fall=False, **over).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.active_types = ("Void",)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def outpost(sim: Simulation):
    return next(c for c in sim.empire.exposed_cities() if c.tier is CityTier.OUTPOST)


def dungeon(sim: Simulation, city, source: str, natural: int | None = None):
    d = sim._make_dungeon(DungeonType.BASIC, city)
    d.source = source
    d.subtype = "None"
    if natural is not None:
        d.floors = natural
        d.tree_floors = 0
    return d


def depths(sim: Simulation, city) -> tuple[int, int, int]:
    """The shallowest, middle and deepest natural floors a Void dungeon on this
    city can roll: the row's range times the Void pattern's multiplier."""
    lo, hi = sim.cfg.spec(DungeonType.BASIC, city.tier).floors
    m = PATTERNS["Void"].floors_mult
    return round(lo * m), round((lo + hi) / 2 * m), int(hi * m)


def test_the_numbers_are_the_owners() -> None:
    cfg = TuningConfig()
    assert cfg.void_dungeons_add_stacks is True
    assert cfg.void_stacks_to_erase == 10


def test_stacks_go_by_the_third_of_the_rows_depth() -> None:
    sim = simulation()
    city = outpost(sim)
    shallow, middle, deep = depths(sim, city)
    got = [sim._void_stacks_for(dungeon(sim, city, "Void", n)) for n in (shallow, middle, deep)]
    assert got == [1, 2, 3], (shallow, middle, deep, got)


def test_the_tree_s_added_floors_add_no_stacks() -> None:
    sim = simulation()
    city = outpost(sim)
    shallow, _, _ = depths(sim, city)
    d = dungeon(sim, city, "Void", shallow)
    d.floors += 40
    d.tree_floors = 40
    assert sim._void_stacks_for(d) == 1


def test_a_void_dungeon_adds_stacks_and_does_no_damage() -> None:
    """CONTROL: a War dungeon of the same depth on another Outpost hurts it and
    adds nothing."""
    sim = simulation()
    void_city, war_city = [c for c in sim.empire.exposed_cities() if c.tier is CityTier.OUTPOST][:2]
    _, middle, _ = depths(sim, void_city)
    v = dungeon(sim, void_city, "Void", middle)
    w = dungeon(sim, war_city, "War", middle)
    before = (void_city.defense, void_city.population, war_city.defense, war_city.population)
    sim._resolve(v)
    sim._resolve(w)
    assert void_city.void_stacks == 2
    assert (void_city.defense, void_city.population) == before[:2]
    assert war_city.void_stacks == 0
    assert war_city.defense < before[2] and war_city.population < before[3]
    assert v.did not in sim.dungeons, "it hit its city, so it leaves the map as any ordinary dungeon does"
    assert sim.void_stacks_added == 2
    assert sim.resolved == 2, "a Void timer still counts as a resolve"


def test_ten_stacks_erase_the_city() -> None:
    sim = simulation()
    city = outpost(sim)
    city.void_stacks = 9
    sim._resolve(dungeon(sim, city, "Void", depths(sim, city)[0]))
    assert city.void_stacks == 10
    assert city.fallen and city.erased
    assert sim.void_erasures == 1
    assert not any(d.dtype is DungeonType.FALLEN_CITY and d.city_id == city.cid for d in sim.dungeons.values()), (
        "an erased city leaves no Fallen City dungeon to retake")


def test_nine_stacks_do_not() -> None:
    sim = simulation()
    city = outpost(sim)
    city.void_stacks = 8
    sim._resolve(dungeon(sim, city, "Void", depths(sim, city)[0]))
    assert city.void_stacks == 9
    assert not city.fallen and not city.erased


def test_a_city_that_falls_with_fewer_than_ten_can_be_retaken_and_retaking_it_clears_them() -> None:
    """CONTROL: the same fall with ten stacks erases it."""
    sim = simulation()
    city, other = [c for c in sim.empire.exposed_cities() if c.tier is CityTier.OUTPOST][:2]
    city.void_stacks = 9
    sim._fall(city)
    assert city.fallen and not city.erased
    assert any(d.dtype is DungeonType.FALLEN_CITY and d.city_id == city.cid for d in sim.dungeons.values())
    sim._retake(city)
    assert not city.fallen and city.void_stacks == 0

    other.void_stacks = 10
    sim._fall(other)
    assert other.erased


def test_a_void_siege_still_bites_damage() -> None:
    sim = simulation()
    city = outpost(sim)
    d = dungeon(sim, city, "Void")
    d.subtype = "Siege"
    before = city.defense
    sim.day = d.spawned_day + 5
    sim._apply_siege_damage()
    assert city.defense < before
    assert city.void_stacks == 0


def test_with_the_setting_off_a_void_dungeon_does_damage() -> None:
    sim = simulation(void_dungeons_add_stacks=False)
    city = outpost(sim)
    before = city.defense
    sim._resolve(dungeon(sim, city, "Void", depths(sim, city)[1]))
    assert city.defense < before and city.void_stacks == 0


def test_the_void_returns_to_the_cities_it_has_stacked() -> None:
    """Its targeting weight rises with the city's stacks, from 1x at none to 5x
    at ten, and no longer reads lost defence. CONTROL: War's weight on the same
    city does not move with the stacks."""
    sim = simulation()
    city = outpost(sim)
    void, war = PATTERNS["Void"].weight, PATTERNS["War"].weight
    plain, plain_war = void(sim.empire, city), war(sim.empire, city)
    city.void_stacks = 5
    assert void(sim.empire, city) == plain * 3.0
    city.void_stacks = 10
    assert void(sim.empire, city) == plain * 5.0
    assert war(sim.empire, city) == plain_war
    city.void_stacks = 0
    city.defense = city.max_defense / 2
    assert void(sim.empire, city) == plain, "lost defence no longer draws the Void"
