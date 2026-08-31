"""The Unreal surge scheduler's constants must match the simulation's.

WHY THIS EXISTS AT ALL. `UCataclysmSurgeScheduler` in the `CataclysmEmpire`
module is a port of `Simulation.surge_count`, `surge_gap` and `trigger_surge` in
`sim/cataclysm_sim/engine.py`, and of the surge constants in `config.py`. Two
copies of a number are two numbers, and the power model in
`sim/cataclysm_sim/scoring.py` silently drifted from its own source twice, which
is why `CLAUDE.md` carries a rule about it.

This is the third of these, after `test_day_clock_port.py` and
`test_empire_map_port.py`, for the third thing ported into that module.

WHY IT MATTERS MORE HERE THAN FOR THE OTHER TWO. How surges escalate is an OPEN
TUNING QUESTION rather than a settled design: `sim/experiments.py` sweeps the four
modes against each other, and the answer it eventually gives is only worth
anything if the game runs the same arithmetic the sweep did.

WHAT IT DOES NOT CHECK. That the scheduler behaves -- that an accelerating run
really shortens its gap and stops at the floor, that Heretic really brings more
dungeons at the cap, that a wave lands only on the frontier. Those are Unreal
automation tests under the `Cataclysm.Surge` prefix. A test written against a
constant cannot notice that the constant is wrong, and a test that reads the
constant out of the source cannot notice that the code ignores it.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

SURGE_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
                / "CataclysmSurge.h")

SURGE_TESTS = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Tests"
               / "CataclysmSurgeTests.cpp")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str) -> str:
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {SURGE_HEADER.name}; "
                    "has it been renamed?")
    return match.group(1)


def number(text: str, name: str) -> float:
    return float(constant(text, name, r"-?[0-9.]+f?").rstrip("f"))


def flag(text: str, name: str) -> bool:
    return constant(text, name, r"true|false") == "true"


@pytest.fixture(scope="module")
def surge_header() -> str:
    return read(SURGE_HEADER)


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


class TestTheCadence:
    NUMBERS = {
        "IntervalDays": "surge_interval_days",
        "DungeonsPerSurge": "surge_dungeon_count",
        "IntervalDecay": "surge_interval_decay",
        "LeastIntervalDays": "surge_interval_min",
        "CountGrowthPerSurge": "surge_count_growth",
        "MostDungeonsPerSurge": "surge_count_max",
    }

    def test_every_cadence_number_matches(self, surge_header, model):
        for unreal_name, model_name in self.NUMBERS.items():
            unreal = number(surge_header, unreal_name)
            expected = float(getattr(model, model_name))

            assert unreal == pytest.approx(expected), (
                f"{unreal_name}: Unreal has {unreal}, the model has {expected}")

    def test_the_decay_shortens_and_the_growth_lengthens(self, surge_header):
        """Not a copy of anything: a decay above 1 would make an accelerating
        surge slower each time, and a growth below 0 would make a swelling one
        smaller. Either would still pass the comparison above if the simulation
        had the same mistake."""
        assert 0.0 < number(surge_header, "IntervalDecay") < 1.0
        assert number(surge_header, "CountGrowthPerSurge") > 0.0

    def test_the_floor_is_below_the_interval_and_the_ceiling_above_the_count(
            self, surge_header):
        """A floor above the starting gap would make an accelerating run slower
        than a static one from its first surge, and a ceiling below the starting
        count would make a swelling one smaller than a static one."""
        assert (number(surge_header, "LeastIntervalDays")
                < number(surge_header, "IntervalDays"))
        assert (number(surge_header, "MostDungeonsPerSurge")
                > number(surge_header, "DungeonsPerSurge"))


class TestWhatACityFallingDoes:
    def test_a_fall_fires_a_surge_in_both(self, surge_header, model):
        assert flag(surge_header, "bSurgeOnCityFall") == model.surge_on_city_fall

    def test_a_fall_escalates_in_both(self, surge_header, model):
        assert (flag(surge_header, "bCityFallAdvancesEscalation")
                == model.city_fall_advances_escalation)


class TestTheLethalityModes:
    def test_only_heretic_changes_the_wave(self):
        """The Unreal side has one constant, for Heretic, and answers 1.0 for
        anything else. That is only correct while the other two modes really are
        1.0 in the model."""
        from cataclysm_sim.config import LETHALITY_RULES, LethalityMode

        assert LETHALITY_RULES[
            LethalityMode.STANDARD].surge_dungeon_multiplier == 1.0
        assert LETHALITY_RULES[
            LethalityMode.HARDCORE].surge_dungeon_multiplier == 1.0

    def test_heretics_multiplier_matches(self, surge_header):
        from cataclysm_sim.config import LETHALITY_RULES, LethalityMode

        unreal = number(surge_header, "HereticDungeonMultiplier")
        expected = LETHALITY_RULES[
            LethalityMode.HERETIC].surge_dungeon_multiplier

        assert unreal == pytest.approx(expected), (
            f"Unreal has {unreal}, the model has {expected}")

    def test_the_multiplier_is_applied_after_the_cap_in_both(self, model):
        """THE ORDER, NOT THE NUMBER, and it cannot be read off a constant.

        The simulation caps the count and then multiplies. Doing it the other way
        makes Heretic identical to Standard at every surge that reaches the cap,
        which is where the extra dungeons would hurt most. This is what says the
        model still does it that way, so the Unreal side copying the order is
        still copying something true.
        """
        from cataclysm_sim.config import (LethalityMode, SurgeMode,
                                          TuningConfig)
        from cataclysm_sim.engine import Simulation

        heretic = Simulation(
            TuningConfig(surge_mode=SurgeMode.SWELLING)
            .with_lethality(LethalityMode.HERETIC), seed=0)
        standard = Simulation(
            TuningConfig(surge_mode=SurgeMode.SWELLING)
            .with_lethality(LethalityMode.STANDARD), seed=0)

        # Far past the point where a swelling count reaches its ceiling.
        heretic.surge_index = standard.surge_index = 40

        assert standard.surge_count() == model.surge_count_max, (
            "a swelling Standard run no longer reaches the ceiling at surge 40, "
            "so this test is no longer asking about the cap at all")
        assert heretic.surge_count() > standard.surge_count(), (
            "the model now caps AFTER multiplying, so Heretic and Standard send "
            "the same wave at the ceiling. The Unreal side still multiplies last")


class TestWhereAWaveLands:
    WEIGHTS = {
        "OutpostTargetWeight": "Outpost",
        "BulwarkTargetWeight": "Bulwark",
        "SanctuaryTargetWeight": "Sanctuary",
        "PillarTargetWeight": "Pillar",
    }

    def test_every_tiers_weight_matches(self, surge_header, model):
        from cataclysm_sim.config import CityTier

        for unreal_name, tier_name in self.WEIGHTS.items():
            unreal = number(surge_header, unreal_name)
            expected = model.SURGE_TARGET_WEIGHT[CityTier(tier_name)]

            assert unreal == pytest.approx(expected), (
                f"{unreal_name}: Unreal has {unreal}, the model has {expected}")

    def test_the_pillar_is_never_a_target(self, surge_header, model):
        """A rule rather than a preference: the Pillar is only ever attacked in
        the Last Stand, when the Cataclysm comes to the player."""
        from cataclysm_sim.config import CityTier

        assert number(surge_header, "PillarTargetWeight") == 0.0
        assert model.SURGE_TARGET_WEIGHT[CityTier.PILLAR] == 0.0

    def test_a_smaller_city_is_hit_more_often(self, surge_header):
        """The frontier is Outposts before anything else, which is what makes the
        empire crumble from the outside in rather than at random."""
        weights = [number(surge_header, name) for name in self.WEIGHTS]
        assert weights == sorted(weights, reverse=True), weights


class TestWhatADungeonIs:
    def test_the_resolve_jitter_matches(self, surge_header, model):
        unreal = number(surge_header, "ResolveJitter")
        assert unreal == pytest.approx(model.resolve_jitter), (
            f"Unreal has {unreal}, the model has {model.resolve_jitter}")

    def test_the_jitter_cannot_reorder_two_depths(self, surge_header, model):
        """A jitter of 50% would let an 8-floor dungeon outlast a 15-floor one on
        the same city, and the trade the whole strategy layer rests on -- deeper
        is slower to clear AND slower to bite -- would stop being reliable."""
        jitter = number(surge_header, "ResolveJitter")

        shallow = (model.resolve_base_days + 8 * model.resolve_floor_ratio)
        deep = (model.resolve_base_days + 15 * model.resolve_floor_ratio)

        assert shallow * (1 + jitter) < deep * (1 - jitter), (
            f"at a jitter of {jitter} an 8-floor dungeon can outlast a 15-floor "
            "one on the same Outpost")

    SPECS = {
        "Outpost": (8, 15, 0.10, 0.05),
        "Bulwark": (15, 25, 0.09, 0.05),
        "Sanctuary": (25, 40, 0.08, 0.04),
        "Pillar": (40, 60, 0.06, 0.03),
    }

    def test_every_basic_dungeon_spec_matches(self, model):
        """The Unreal side writes these into a switch rather than a table, so
        this compares the four the switch names against the model's."""
        from cataclysm_sim.config import CityTier, DungeonType

        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        for tier_name, (least, most, defence, population) in self.SPECS.items():
            spec = model.spec(DungeonType.BASIC, CityTier(tier_name))

            assert spec.floors == (least, most), (
                f"{tier_name}: this test expects {(least, most)} floors, the "
                f"model has {spec.floors}")
            assert spec.defense_bite == pytest.approx(defence)
            assert spec.population_bite == pytest.approx(population)

            # And the C++ carries the same four numbers for that tier.
            for value in (f"LeastFloors = {least};",
                          f"MostFloors = {most};",
                          f"DefenceBite = {defence:.2f}f;",
                          f"PopulationBite = {population:.2f}f;"):
                assert value in source, (
                    f"{tier_name}: CataclysmSurge.cpp does not contain "
                    f"{value!r}")


class TestTheEscalationModes:
    def test_the_enum_names_the_same_four_modes(self, surge_header):
        from cataclysm_sim.config import SurgeMode

        block = re.search(
            r"enum\s+class\s+ECataclysmSurgeMode\s*:\s*uint8\s*\{(.*?)\}",
            surge_header, re.DOTALL)
        if not block:
            pytest.fail("could not find ECataclysmSurgeMode in "
                        f"{SURGE_HEADER.name}; has it been renamed?")

        names = {name.lower()
                 for name, _ in re.findall(r"(\w+)\s*=\s*(\d+)",
                                           block.group(1))}

        expected = {mode.value.lower() for mode in SurgeMode}

        assert names == expected, (
            f"Unreal names {sorted(names)}, the model names {sorted(expected)}")


class TestTheHandWorkedFiguresInTheUnrealTest:
    """`CataclysmSurgeTests.cpp` asserts a ladder of counts and gaps that was
    read out of the simulation by running it. This is what notices if the model
    changes afterwards -- the Unreal test would keep passing against figures that
    no longer describe anything.
    """

    def gap_at(self, mode, index: float) -> float:
        from cataclysm_sim.config import SurgeMode, TuningConfig
        from cataclysm_sim.engine import Simulation

        simulation = Simulation(TuningConfig(surge_mode=SurgeMode(mode)), seed=0)
        simulation.surge_index = index
        return simulation.surge_gap()

    def count_at(self, mode, lethality, index: int) -> int:
        from cataclysm_sim.config import LethalityMode, SurgeMode, TuningConfig
        from cataclysm_sim.engine import Simulation

        simulation = Simulation(
            TuningConfig(surge_mode=SurgeMode(mode))
            .with_lethality(LethalityMode(lethality)), seed=0)
        simulation.surge_index = index
        return simulation.surge_count()

    def test_the_accelerating_ladder_still_holds(self):
        assert self.gap_at("accelerating", 1) == pytest.approx(105.6, abs=0.01)
        assert self.gap_at("accelerating", 2) == pytest.approx(92.928, abs=0.01)
        assert self.gap_at("accelerating", 3) == pytest.approx(81.7766, abs=0.01)
        assert self.gap_at("accelerating", 12) == pytest.approx(25.8805, abs=0.01)
        assert self.gap_at("accelerating", 13) == pytest.approx(25.0, abs=0.01)

    def test_the_swelling_ladder_still_holds(self):
        assert self.count_at("swelling", "standard", 0) == 4
        assert self.count_at("swelling", "standard", 2) == 5
        assert self.count_at("swelling", "standard", 4) == 6
        assert self.count_at("swelling", "standard", 19) == 13
        assert self.count_at("swelling", "standard", 20) == 14

    def test_heretic_at_the_ceiling_is_still_seventeen(self):
        """The figure the ordering argument turns on. If it ever equals the
        Standard ceiling again, the cap has moved back in front of the
        multiplier."""
        assert self.count_at("swelling", "heretic", 20) == 17
        assert self.count_at("swelling", "standard", 20) == 14

    def test_the_unreal_test_carries_those_same_figures(self):
        text = read(SURGE_TESTS)

        for figure in ("105.6f", "92.928f", "81.7766f", "25.8805f"):
            assert figure in text, (
                f"CataclysmSurgeTests.cpp no longer asserts {figure}; the "
                "accelerating ladder there and the model's have parted")
