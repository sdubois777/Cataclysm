"""The Unreal Power Score constants must match the simulation's.

WHY THIS IS NOT A DRIFT TEST LIKE THE OTHERS. The affix pool, the class stat
lines and the attribute effects all live in the design workbook and are compared
sheet against model. The Power Score anchors do not: they come from
`sim/cataclysm_sim/scoring.py`, which is a verified port of a file in a separate
repository and must never be hand-edited. Putting them in the workbook would
create a third copy of numbers that already have an authoritative source
elsewhere.

So the Unreal side carries them as literals, and this reads those literals back
out of the C++ and compares them. Parsing source is cruder than reading a
generated table, but the alternative is no guard at all: nothing else would
notice if the two lists parted company.

WHAT IS AND IS NOT COMPARED. The eight tier anchors and the four shares, because
those are the model's only inputs. The five weights are NOT compared here: they
are derived on both sides from these same inputs, and an Unreal automation test
already checks the derivation lands on the same numbers. Comparing them here
would test the arithmetic twice and the inputs once.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
          / "CataclysmPowerScore.cpp")
HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
          / "CataclysmPowerScore.h")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import player_power
    return player_power


@pytest.fixture(scope="module")
def anchors_in_unreal() -> list[int]:
    text = read(SOURCE)
    match = re.search(r"Anchors\s*=\s*\{([^}]*)\}", text)
    if not match:
        pytest.fail("could not find the tier anchor list in "
                    f"{SOURCE.name}; has it been renamed?")
    return [int(part) for part in re.findall(r"-?\d+", match.group(1))]


@pytest.fixture(scope="module")
def shares_in_unreal() -> dict[str, float]:
    text = read(HEADER)
    found = dict(re.findall(
        r"constexpr float Share(\w+)\s*=\s*([0-9.]+)f", text))
    if not found:
        pytest.fail(f"could not find the four shares in {HEADER.name}")
    return {name: float(value) for name, value in found.items()}


class TestTheTierAnchors:
    def test_unreal_carries_one_anchor_per_tier_plus_an_unused_zero(
            self, anchors_in_unreal):
        assert len(anchors_in_unreal) == 9, anchors_in_unreal
        assert anchors_in_unreal[0] == 0, "entry 0 is unused and must be zero"

    def test_every_anchor_matches_the_model(self, anchors_in_unreal, model):
        from cataclysm_sim import scoring
        for tier in range(1, 9):
            assert anchors_in_unreal[tier] == scoring.PLAYER_MAX_SCORES[tier], (
                f"tier {tier}: Unreal has {anchors_in_unreal[tier]}, the model "
                f"has {scoring.PLAYER_MAX_SCORES[tier]}")

    def test_the_anchors_climb(self, anchors_in_unreal):
        """A tier that is worth less than the one below it would make the
        difficulty comparison run backwards."""
        assert anchors_in_unreal == sorted(anchors_in_unreal)


class TestTheFourShares:
    def test_unreal_carries_all_four(self, shares_in_unreal):
        assert set(shares_in_unreal) == {"Level", "Gear", "Gems", "Resistances"}

    def test_every_share_matches_the_model(self, shares_in_unreal, model):
        expected = {
            "Level": model.SHARE_LEVEL,
            "Gear": model.SHARE_GEAR,
            "Gems": model.SHARE_GEMS,
            "Resistances": model.SHARE_RESISTANCES,
        }
        for name, value in expected.items():
            assert shares_in_unreal[name] == pytest.approx(value), (
                f"{name}: Unreal has {shares_in_unreal[name]}, the model has "
                f"{value}")

    def test_the_four_shares_add_to_one(self, shares_in_unreal):
        """Otherwise the tier 8 anchor cannot be reached, whatever the weights
        derive to."""
        assert sum(shares_in_unreal.values()) == pytest.approx(1.0)


class TestTheOtherConstants:
    """The fixed quantities the derivation divides by. A disagreement in any of
    them moves every weight."""

    @pytest.mark.parametrize("name,expected", [
        ("MaxLevel", 100),
        ("MaxRarity", 8),
        ("MaxUpgrade", 10),
        ("GearPieces", 18),
        ("TotalSockets", 45),
        ("ResistanceCount", 8),
    ])
    def test_an_integer_constant_matches(self, name, expected):
        text = read(HEADER)
        match = re.search(rf"constexpr int32 {name}\s*=\s*(\d+)", text)
        assert match, f"could not find {name} in {HEADER.name}"
        assert int(match.group(1)) == expected

    def test_the_integer_constants_match_the_model(self, model):
        text = read(HEADER)

        def constant(name: str) -> int:
            match = re.search(rf"constexpr int32 {name}\s*=\s*(\d+)", text)
            assert match, f"could not find {name} in {HEADER.name}"
            return int(match.group(1))

        assert constant("MaxLevel") == model.MAX_LEVEL
        assert constant("MaxRarity") == model.MAX_RARITY
        assert constant("MaxUpgrade") == model.MAX_UPGRADE
        assert constant("GearPieces") == model.GEAR_PIECES
        assert constant("TotalSockets") == model.TOTAL_SOCKETS
        assert constant("ResistanceCount") == model.RESISTANCE_COUNT

    def test_the_resistance_cap_matches_the_model(self, model):
        text = read(HEADER)
        match = re.search(r"constexpr float ResistanceCap\s*=\s*([0-9.]+)f", text)
        assert match, f"could not find ResistanceCap in {HEADER.name}"
        assert float(match.group(1)) == pytest.approx(model.RESISTANCE_CAP)
