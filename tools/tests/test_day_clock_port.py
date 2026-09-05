"""The Unreal day clock's constants must match the simulation's.

WHY THIS EXISTS AT ALL. `UCataclysmDayClock` in the `CataclysmEmpire` module is a
port of `sim/cataclysm_sim/engine.py` and `config.py`. Two copies of a number are
two numbers, and this repository has already learned what happens next: the power
model in `sim/cataclysm_sim/scoring.py` silently drifted from its own source
twice, which is why `CLAUDE.md` carries a rule about it and why
`sim/verify_scoring_port.py` exists.

WHY IT READS THE C++ RATHER THAN A GENERATED TABLE. The same reason
`test_power_score_port.py` does: these numbers have an authoritative home in
`sim/cataclysm_sim/config.py` and putting them in the design workbook as well
would make a third copy. Parsing source is cruder than reading a table, and the
alternative is no guard at all.

WHAT IT DOES NOT CHECK. That the clock behaves -- that a day moves every timer
but one, that a resolve happens once. Those are Unreal automation tests under the
`Cataclysm.DayClock` prefix. A test written against a constant cannot notice that
the constant is wrong, and a test that reads the constant out of the source
cannot notice that the code ignores it, so both halves are needed.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

CLOCK_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "DayClock"
                / "CataclysmDayClock.h")

LETHALITY_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                    / "CataclysmLethality.h")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str) -> str:
    """One `static constexpr` value out of a header, by name."""
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {CLOCK_HEADER.name}; "
                    "has it been renamed?")
    return match.group(1)


@pytest.fixture(scope="module")
def clock_source() -> str:
    return read(CLOCK_HEADER)


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


class TestWhatAFloorCosts:
    def test_a_floor_costs_the_same_number_of_days_in_both(
            self, clock_source, model):
        """What a floor costs before anybody has invested. One day is the
        starting rate on both sides, and neither side lowers it: the game's
        reductions live on a city, and the model has no upgrades at all."""
        unreal = float(constant(clock_source, "DaysPerFloor", r"[0-9.]+f?")
                       .rstrip("f"))
        assert unreal == pytest.approx(model.days_per_floor), (
            f"Unreal has {unreal}, the model has {model.days_per_floor}")

    def test_a_run_is_clamped_to_the_same_range_in_both(
            self, clock_source, model):
        least = int(constant(clock_source, "LeastRunDays", r"-?\d+"))
        most = int(constant(clock_source, "MostRunDays", r"-?\d+"))

        assert least == model.run_days_min, (
            f"Unreal's floor is {least}, the model's is {model.run_days_min}")
        assert most == model.run_days_max, (
            f"Unreal's ceiling is {most}, the model's is {model.run_days_max}")


class TestTheResolveTimer:
    def test_the_base_matches(self, clock_source, model):
        unreal = float(constant(clock_source, "ResolveBaseDays", r"[0-9.]+f?")
                       .rstrip("f"))
        assert unreal == pytest.approx(model.resolve_base_days), (
            f"Unreal has {unreal}, the model has {model.resolve_base_days}")

    def test_the_ratio_matches(self, clock_source, model):
        """The single most important number in the strategy layer, in the
        simulation's own words."""
        unreal = float(constant(clock_source, "ResolveFloorRatio", r"[0-9.]+f?")
                       .rstrip("f"))
        assert unreal == pytest.approx(model.resolve_floor_ratio), (
            f"Unreal has {unreal}, the model has {model.resolve_floor_ratio}")

    def test_the_ratio_is_above_one(self, clock_source):
        """At exactly 1.0 the timer and the walk are the same length, so every
        dungeon is barely savable and nothing else can be. The margin above 1 is
        where triage lives."""
        unreal = float(constant(clock_source, "ResolveFloorRatio", r"[0-9.]+f?")
                       .rstrip("f"))
        assert unreal > 1.0


class TestTheTwoStructuralRules:
    def test_the_dungeon_being_walked_does_not_tick_in_either(
            self, clock_source, model):
        unreal = constant(clock_source, "bTimerTicksWhileRunning", r"true|false")
        assert (unreal == "true") == model.timer_ticks_while_running, (
            f"Unreal has {unreal}, the model has "
            f"{model.timer_ticks_while_running}")

    def test_a_resolved_dungeon_persists_in_both(self, clock_source, model):
        unreal = constant(clock_source, "bDungeonPersistsAfterResolve",
                          r"true|false")
        assert (unreal == "true") == model.dungeon_persists_after_resolve, (
            f"Unreal has {unreal}, the model has "
            f"{model.dungeon_persists_after_resolve}")


class TestWhatDyingCosts:
    NAMES = {
        "DeathDayCostStandard": "STANDARD",
        "DeathDayCostHardcore": "HARDCORE",
        "DeathDayCostHeretic": "HERETIC",
    }

    def test_every_mode_costs_the_same_days_in_both(self, clock_source):
        from cataclysm_sim.config import LETHALITY_RULES, LethalityMode

        for unreal_name, model_name in self.NAMES.items():
            unreal = int(constant(clock_source, unreal_name, r"-?\d+"))
            expected = LETHALITY_RULES[LethalityMode[model_name]].death_day_cost

            assert unreal == expected, (
                f"{unreal_name}: Unreal has {unreal}, the model has {expected}")

    def test_a_harsher_mode_costs_more(self, clock_source):
        costs = [int(constant(clock_source, name, r"-?\d+"))
                 for name in self.NAMES]
        assert costs == sorted(costs), costs
        assert costs[0] > 0, "dying is never free in any mode"


class TestTheLethalityRungs:
    """The day clock takes a mode as a number rather than as
    `ECataclysmLethality`, because that enum lives in the `Cataclysm` module and
    the empire layer must not depend on it -- its build file says the dependency
    runs one way.

    THAT LEAVES A NUMBERING TO KEEP IN STEP, AND NOTHING ELSE WOULD NOTICE. The
    enum is explicitly numbered because it is persisted in a character record, so
    it is safe to rely on; this is what proves the reliance stays true.
    """

    def test_the_enum_numbers_the_three_modes_the_clock_expects(self):
        text = read(LETHALITY_HEADER)

        block = re.search(
            r"enum\s+class\s+ECataclysmLethality\s*:\s*uint8\s*\{([^}]*)\}", text)
        if not block:
            pytest.fail("could not find ECataclysmLethality in "
                        f"{LETHALITY_HEADER.name}; has it been renamed?")

        rungs = dict(
            (name, int(value))
            for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", block.group(1)))

        assert rungs == {"Standard": 0, "Hardcore": 1, "Heretic": 2}, rungs

    def test_the_clock_reads_those_rungs_in_that_order(self, clock_source):
        """`DeathDayCostFor` documents 0, 1 and 2. If the enum is renumbered, the
        test above fails; this one fails if the clock's own note stops matching
        what it is told."""
        assert "0 Standard, 1 Hardcore, 2 Heretic" in clock_source, (
            "the clock's note about which number means which mode has changed; "
            "check DeathDayCostFor against ECataclysmLethality")
