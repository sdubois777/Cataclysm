"""The Unreal dungeon sub-type weights must match the simulation's.

WHY THIS EXISTS AT ALL. `UCataclysmSurgeScheduler::RollSubType` in the
`CataclysmEmpire` module rolls one of eight sub-types for every dungeon a surge
puts on the map, and the eight weights it rolls from are a copy of
`config.SUBTYPE_SPAWN_WEIGHTS` in `sim/cataclysm_sim/config.py`. Two copies of a
number are two numbers. `test_day_clock_port.py`, `test_surge_port.py` and
`test_empire_map_port.py` beside this guard the same arrangement for the other
constants in that module, and `CLAUDE.md` carries a rule about it because the
power model in `sim/cataclysm_sim/scoring.py` drifted from its own source twice
before anyone noticed.

WHAT IT DOES NOT CHECK. That the roll behaves -- that the spread over many rolls
matches the weights, that it takes exactly one draw from the stream, that a Cow
Level's walk really is doubled and really cannot be shortened. Those are Unreal
automation tests under the `Cataclysm.Surge` prefix. A test written against a
constant cannot notice that the constant is wrong, and a test that reads the
constant out of the source cannot notice that the code ignores it, so both halves
are needed.

THE TWO SIDES ROLL IN DIFFERENT ORDERS AND THAT IS FINE. The model lists its
weights commonest first; `ECataclysmDungeonSubType` lists them in the order the
design document does, and its numbering is persisted so it cannot be reordered.
The same random fraction therefore picks different sub-types on the two sides.
The distributions are identical, which is what is compared here -- weights by
name, never a sequence of rolls.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

SURGE_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
                / "CataclysmSurge.h")

KIND_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
               / "CataclysmDungeonKind.h")

#: The name the model gives each sub-type, against the name the C++ constant and
#: the enumerator use. The model's names carry a space where C++ cannot.
NAMES = {
    "None": "None",
    "Timed": "Timed",
    "Horde": "Horde",
    "Siege": "Siege",
    "Cow Level": "CowLevel",
    "Elite": "Elite",
    "Volatile": "Volatile",
    "Sacrificial": "Sacrificial",
}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str, where: str) -> str:
    """One `static constexpr` value out of a header, by name."""
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {where}; has it been renamed?")
    return match.group(1)


def weight(text: str, cpp_name: str) -> float:
    return float(constant(text, f"SpawnWeight{cpp_name}", r"[0-9.]+f?",
                          SURGE_HEADER.name).rstrip("f"))


@pytest.fixture(scope="module")
def surge_source() -> str:
    return read(SURGE_HEADER)


@pytest.fixture(scope="module")
def kind_source() -> str:
    return read(KIND_HEADER)


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


class TestTheEightWeights:
    def test_every_sub_type_carries_the_same_weight_in_both(
            self, surge_source, model):
        for model_name, cpp_name in NAMES.items():
            assert model_name in model.SUBTYPE_SPAWN_WEIGHTS, (
                f"the model no longer has a sub-type called {model_name!r}; "
                "if it was renamed, rename it in NAMES and in "
                "ECataclysmDungeonSubType too")

            unreal = weight(surge_source, cpp_name)
            wanted = model.SUBTYPE_SPAWN_WEIGHTS[model_name]

            assert unreal == pytest.approx(wanted), (
                f"{model_name}: Unreal has {unreal}, the model has {wanted}")

    def test_neither_side_knows_a_sub_type_the_other_does_not(self, model):
        assert set(model.SUBTYPE_SPAWN_WEIGHTS) == set(NAMES), (
            "the model's sub-types and the ported ones have parted company: "
            f"model has {sorted(model.SUBTYPE_SPAWN_WEIGHTS)}, "
            f"the port covers {sorted(NAMES)}")

    def test_the_weights_add_up_to_a_hundred_on_both_sides(
            self, surge_source, model):
        """Not required by either roll, which divides by its own total, but it
        is what makes each weight readable as a percentage. A change that
        forgets to rebalance the rest shows up here."""
        unreal = sum(weight(surge_source, cpp) for cpp in NAMES.values())

        assert unreal == pytest.approx(100.0), (
            f"the Unreal weights add up to {unreal}, not 100")
        assert sum(model.SUBTYPE_SPAWN_WEIGHTS.values()) == pytest.approx(
            100.0)

    def test_no_sub_type_at_all_is_the_commonest_outcome(
            self, surge_source, model):
        """A dungeon that does something unusual is only worth noticing if most
        of them do not. `None` carries more weight than any single sub-type on
        both sides."""
        unreal = {cpp: weight(surge_source, cpp) for cpp in NAMES.values()}
        others = [value for name, value in unreal.items() if name != "None"]

        assert unreal["None"] > max(others), (
            "in Unreal a sub-type is now at least as common as no sub-type")

        model_others = [value
                        for name, value in model.SUBTYPE_SPAWN_WEIGHTS.items()
                        if name != "None"]

        assert model.SUBTYPE_SPAWN_WEIGHTS["None"] > max(model_others)


class TestTheEnumTheRollWalks:
    def test_the_enum_names_the_same_eight_sub_types(self, kind_source):
        """`RollSubType` walks `ECataclysmDungeonSubType` from 0 to
        `Sacrificial` and asks `SpawnWeightFor` for each. A sub-type added to
        the enum without a weight would never be rolled; one removed would make
        the port silently narrower."""
        block = re.search(
            r"enum\s+class\s+ECataclysmDungeonSubType\s*:\s*uint8\s*\{(.*?)\}",
            kind_source, re.DOTALL)

        assert block, ("could not find ECataclysmDungeonSubType in "
                       f"{KIND_HEADER.name}; has it been renamed?")

        found = dict(re.findall(r"(\w+)\s*=\s*(\d+)", block.group(1)))

        assert set(found) == set(NAMES.values()), (
            f"the enum names {sorted(found)}, the port covers "
            f"{sorted(NAMES.values())}")

    def test_sacrificial_is_still_the_last_one(self, kind_source):
        """`RollSubType` stops at `Sacrificial` by name. A sub-type added after
        it would be walked; one added before it and numbered higher would not,
        and would silently never spawn."""
        block = re.search(
            r"enum\s+class\s+ECataclysmDungeonSubType\s*:\s*uint8\s*\{(.*?)\}",
            kind_source, re.DOTALL)

        found = {name: int(value)
                 for name, value in re.findall(r"(\w+)\s*=\s*(\d+)",
                                               block.group(1))}

        assert found["Sacrificial"] == max(found.values()), (
            "Sacrificial is no longer the highest-numbered sub-type, so "
            "UCataclysmSurgeScheduler::RollSubType stops before the end of the "
            "enum and whatever is above it can never be rolled")


class TestWhatACowLevelCosts:
    def test_the_walk_is_doubled_on_both_sides(self, surge_source):
        """The design document: "Time to complete is doubled and cannot be
        reduced." The model writes the doubling out as a literal in
        `Simulation._make_dungeon`; Unreal has it as a named constant."""
        unreal = float(constant(surge_source, "CowLevelWalkMultiplier",
                                r"[0-9.]+f?", SURGE_HEADER.name).rstrip("f"))

        assert unreal == pytest.approx(2.0), (
            f"Unreal doubles a Cow Level's walk by {unreal}, not 2")

        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py").read_text(
            encoding="utf-8")

        assert "cfg.days_per_floor)) * 2" in source, (
            "sim/cataclysm_sim/engine.py no longer doubles a Cow Level's run "
            "days; UCataclysmSurgeScheduler::CowLevelWalkMultiplier still does")

    def test_the_model_ignores_the_reduction_for_a_cow_level_too(self):
        """Not the multiplier but the other half of the rule. The model's
        `run_days_for` is where the empire tree's reduction is applied, and the
        Cow Level branch does not call it -- which is what "cannot be reduced"
        means. Unreal skips the city upgrade in the same place and for the same
        reason."""
        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py").read_text(
            encoding="utf-8")

        branch = re.search(
            r"run_days=\(self\.run_days_for\(floors\) if subtype != "
            r"[\"']Cow Level[\"']\s*\n\s*else ([^)]*\)[^,]*),", source)

        assert branch, ("sim/cataclysm_sim/engine.py no longer chooses a Cow "
                        "Level's run days separately from run_days_for; the "
                        "port in UCataclysmSurgeScheduler::MakeDungeon still "
                        "does")

        assert "run_days_for" not in branch.group(1), (
            "the model now runs a Cow Level's walk through run_days_for, which "
            "applies the tree's reduction; Unreal still skips the city upgrade")
