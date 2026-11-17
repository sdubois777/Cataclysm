"""The Unreal empire map's constants must match the simulation's.

WHY THIS EXISTS AT ALL. `UCataclysmEmpireMap` in the `CataclysmEmpire` module is
a port of `sim/cataclysm_sim/world.py` and the parts of `config.py` it reads. Two
copies of a number are two numbers, and this repository has already learned what
happens next: the power model in `sim/cataclysm_sim/scoring.py` silently drifted
from its own source twice, which is why `CLAUDE.md` carries a rule about it and
why `sim/verify_scoring_port.py` exists.

This is `test_day_clock_port.py` beside it, for the second thing ported into the
same module, and it reads the C++ source for the same reason: these numbers have
an authoritative home in `sim/cataclysm_sim/`, and putting them in the design
workbook as well would make a third copy.

WHAT IT DOES NOT CHECK. That the map behaves -- that a lane opens when a city
falls, that retaking one seals it again, that the loss condition fires when a
Sanctuary is lost. Those are Unreal automation tests under the
`Cataclysm.EmpireMap` prefix. A test written against a constant cannot notice
that the constant is wrong, and a test that reads the constant out of the source
cannot notice that the code ignores it, so both halves are needed.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

MAP_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
              / "CataclysmEmpireMap.h")

MAP_SOURCE = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
              / "CataclysmEmpireMap.cpp")

SIM_ENGINE = REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str) -> str:
    """One `static constexpr` value out of a header, by name."""
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {MAP_HEADER.name}; "
                    "has it been renamed?")
    return match.group(1)


def number(text: str, name: str) -> float:
    return float(constant(text, name, r"-?[0-9.]+f?").rstrip("f"))


@pytest.fixture(scope="module")
def map_header() -> str:
    return read(MAP_HEADER)


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


@pytest.fixture(scope="module")
def empire(model):
    from cataclysm_sim.world import build_empire
    return build_empire(model)


class TestTheShape:
    """The lattice's radius decides five numbers at once: how many cities each
    tier holds, and how many have to fall in a line before the Pillar is
    reachable. A radius that differed between the two would make every figure
    the simulation produced meaningless for the game."""

    def test_the_map_reaches_the_same_number_of_rings(self, map_header):
        from cataclysm_sim.world import RADIUS

        unreal = int(constant(map_header, "Radius", r"-?\d+"))
        assert unreal == RADIUS, (
            f"Unreal reaches {unreal} rings, the model reaches {RADIUS}")

    def test_the_map_holds_the_same_number_of_cities(self, map_header, empire):
        unreal = int(constant(map_header, "CityCount", r"-?\d+"))
        assert unreal == len(empire.cities), (
            f"Unreal holds {unreal} cities, the model builds "
            f"{len(empire.cities)}")

    def test_that_count_is_what_the_rings_add_up_to(self, empire):
        """Ring N holds exactly 4N cells, and ring 0 holds the Pillar alone. The
        design document's 12/8/4/1 is a property of that geometry rather than a
        separate decision, so this checks the geometry rather than the four
        numbers."""
        from cataclysm_sim.world import RADIUS

        expected = 1 + sum(4 * ring for ring in range(1, RADIUS + 1))
        assert len(empire.cities) == expected

        for ring in range(RADIUS + 1):
            found = sum(1 for c in empire if c.ring == ring)
            assert found == (1 if ring == 0 else 4 * ring), (
                f"ring {ring} holds {found}")


class TestTheTiers:
    """`ECataclysmCityTier`'s numbering is `Radius - Ring`, so the Pillar is 3
    and an Outpost is 0. That orders the tiers weakest to strongest, which is
    the order `config.TIER_ORDER` uses."""

    def test_the_enum_orders_the_tiers_the_way_the_model_does(self, map_header):
        from cataclysm_sim.config import TIER_ORDER

        block = re.search(
            r"enum\s+class\s+ECataclysmCityTier\s*:\s*uint8\s*\{(.*?)\}",
            map_header, re.DOTALL)
        if not block:
            pytest.fail("could not find ECataclysmCityTier in "
                        f"{MAP_HEADER.name}; has it been renamed?")

        rungs = dict(
            (name, int(value))
            for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", block.group(1)))

        expected = {tier.value: index for index, tier in enumerate(TIER_ORDER)}

        assert rungs == expected, (
            f"Unreal numbers the tiers {rungs}, the model orders them "
            f"{expected}")

    def test_the_ring_a_tier_sits_at_is_the_radius_minus_its_number(
            self, map_header, empire):
        """The identity `UCataclysmEmpireMap::TierForRing` is built on. If the
        enum were renumbered without the map being changed, every city would be
        laid out at the wrong tier and no constant would look wrong."""
        from cataclysm_sim.config import TIER_ORDER
        from cataclysm_sim.world import RADIUS

        for number_in_enum, tier in enumerate(TIER_ORDER):
            rings = {c.ring for c in empire if c.tier is tier}
            assert rings == {RADIUS - number_in_enum}, (
                f"{tier.value} is numbered {number_in_enum}, so it should sit "
                f"at ring {RADIUS - number_in_enum}; the model puts it at "
                f"{sorted(rings)}")


class TestWhatACityIsWorth:
    """`config.TIER_STATS` is one of the simulation's five named unknowns -- the
    design documents do not specify city defence or population -- so the game
    has no independent source for these and a drift would be invisible."""

    TIERS = {
        "Outpost": ("OutpostMaxDefence", "OutpostMaxPopulation"),
        "Bulwark": ("BulwarkMaxDefence", "BulwarkMaxPopulation"),
        "Sanctuary": ("SanctuaryMaxDefence", "SanctuaryMaxPopulation"),
        "Pillar": ("PillarMaxDefence", "PillarMaxPopulation"),
    }

    def test_every_tiers_defence_and_population_match(self, map_header, model):
        from cataclysm_sim.config import CityTier

        for name, (defence_name, population_name) in self.TIERS.items():
            stats = model.TIER_STATS[CityTier(name)]

            defence = number(map_header, defence_name)
            population = number(map_header, population_name)

            assert defence == pytest.approx(stats.max_defense), (
                f"{defence_name}: Unreal has {defence}, the model has "
                f"{stats.max_defense}")
            assert population == pytest.approx(stats.max_population), (
                f"{population_name}: Unreal has {population}, the model has "
                f"{stats.max_population}")

    def test_a_bigger_city_is_worth_more_than_a_smaller_one(self, map_header):
        """Not a copy of anything: the ladder has to rise, or the lattice's
        inner rings would be cheaper to lose than its outer ones and the whole
        shape of the strategy layer would invert."""
        defences = [number(map_header, names[0])
                    for names in self.TIERS.values()]
        populations = [number(map_header, names[1])
                       for names in self.TIERS.values()]

        assert defences == sorted(defences), defences
        assert populations == sorted(populations), populations
        assert defences[0] > 0, "every city has some defence"

    def test_the_tier_counts_and_stats_give_the_stated_population(
            self, map_header, empire):
        """610,000 people, which is what the Unreal test asserts by hand. This
        is the arithmetic behind that figure, so the two cannot part without one
        of them failing."""
        total = sum(c.max_population for c in empire)
        assert total == pytest.approx(610_000)

        counts = {"Outpost": 12, "Bulwark": 8, "Sanctuary": 4, "Pillar": 1}
        from_unreal = sum(
            counts[name] * number(map_header, names[1])
            for name, names in self.TIERS.items())

        assert from_unreal == pytest.approx(total), (
            f"Unreal's tiers add up to {from_unreal}, the model's to {total}")


class TestWhatRetakingACityGivesBack:
    """A retaken city comes back with half of what it had, which is what makes
    retaking a repair rather than an undo. The model has the number inline in
    `Simulation._retake` rather than in `config`, so this reads it out of there.
    """

    def test_the_share_matches(self, map_header):
        unreal = number(map_header, "RetakenFraction")

        text = read(SIM_ENGINE)
        body = re.search(r"def _retake\(self, city: City\) -> None:(.*?)\n    def ",
                         text, re.DOTALL)
        if not body:
            pytest.fail("could not find Simulation._retake in engine.py; "
                        "has it been renamed?")

        shares = {float(share) for share in re.findall(
            r"city\.max_\w+ \* ([0-9.]+)", body.group(1))}

        assert shares, (
            "Simulation._retake no longer sets defence and population to a "
            "share of their maximum; the Unreal side still does")
        assert len(shares) == 1, (
            f"_retake uses more than one share: {sorted(shares)}. The Unreal "
            "side has a single RetakenFraction for both")
        assert unreal == pytest.approx(shares.pop()), (
            f"Unreal gives back {unreal}, the model gives back {shares}")

    def test_it_gives_back_some_but_not_all(self, map_header):
        unreal = number(map_header, "RetakenFraction")
        assert 0.0 < unreal < 1.0, (
            "a retaken city that came back full would make losing one free, "
            "and one that came back empty would make retaking pointless")


class TestTheMapIsDrawnTheSameWay:
    """`Render` is compared against a golden string in the Unreal test. That
    string was copied out of a run of `Empire.render`, and this is what notices
    if the model's rendering changes afterwards -- the Unreal test would keep
    passing against a picture that no longer matches anything."""

    INTACT = (
        "    O!\n"
        "   O! B. O!\n"
        "  O! B. S. B. O!\n"
        " O! B. S. P. S. B. O!\n"
        "  O! B. S. B. O!\n"
        "   O! B. O!\n"
        "    O!"
    )

    def test_the_model_still_draws_what_the_unreal_test_expects(self, empire):
        assert empire.render() == self.INTACT

    def test_the_unreal_source_carries_the_same_picture(self):
        """The header's sketch and the test's golden string are two more copies
        of the same drawing. This is the cheap check that the one in the test
        did not drift; the one in the header is prose and is not checked."""
        tests = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Tests"
                 / "CataclysmEmpireMapTests.cpp")
        text = read(tests)

        for line in self.INTACT.split("\n"):
            assert f'TEXT("{line}' in text, (
                f"the row {line!r} is not in {tests.name}; the golden map there "
                "no longer matches what the model draws")
