"""The difficulty tier range in the C++, checked against the model.

WHY THIS EXISTS. Issue #514: every hit in the running game resolved at
difficulty tier 1, because nothing in the project held a tier at all and
`UCataclysmVitalAttributeSet::PostGameplayEffectExecute` passed a literal. The
tier now lives on `ACataclysmGameMode` and the range it is clamped to is two
`static constexpr int32` constants in that header.

WHY IT IS CHECKED FROM PYTHON RATHER THAN BY A `static_assert`. Continuous
integration compiles no C++ at all, so an assertion beside the constant would not
run on a pull request. A test that reads the number out of the source as text
does. That is the same arrangement `tools/tests/test_warden_matches_the_model.py`
uses for the Abyssal Warden's charge speed, and its header says why.

WHAT THE TIER DECIDES, AND IT IS ONLY ONE THING: what armour is worth.
`UCataclysmDamageCalculation::ArmorReduction` is `armor / (armor + 800 x tier)`
capped at 75%, mirroring `armor_reduction` in `sim/cataclysm_sim/damage.py`. The
last test below recomputes the figures issue #514 measured, so the size of the
defect is recorded by something that runs rather than only by prose.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")

#: The Abyssal Warden's designed armour at difficulty tier 8, quoted from issue
#: #514. The most armoured creature in the vertical slice, so the difference
#: between the tiers is at its largest here.
WARDEN_ARMOR_AT_TIER_EIGHT = 5954.0


def constant(header: pathlib.Path, name: str) -> int:
    """The value of a `static constexpr int32 <name> = <number>;` line."""
    if not header.is_file():
        pytest.fail(f"{header.relative_to(REPO_ROOT)} does not exist")

    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        header.read_text(encoding="utf-8"))
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'static constexpr int32 {name} = <number>;' line. If it was "
            f"renamed, rename it here too; if it was deleted, the tier range is "
            f"unguarded and continuous integration compiles no C++ to notice.")
    return int(match.group(1))


def test_the_highest_tier_matches_the_model() -> None:
    from cataclysm_sim import affixes as af

    assert constant(GAME_MODE_HEADER, "HighestDifficultyTier") == \
        af.DIFFICULTY_TIERS, (
        "HighestDifficultyTier in game/Source/Cataclysm/Player/"
        "CataclysmGameMode.h has drifted from DIFFICULTY_TIERS in "
        "sim/cataclysm_sim/affixes.py, which is authoritative. A game mode "
        "clamping to the wrong ceiling makes the top of the game resolve hits "
        "at a tier the design does not have.")


def test_the_lowest_tier_is_one() -> None:
    """The model refuses a tier below 1, so the engine must not allow one."""
    from cataclysm_sim import affixes as af

    assert constant(GAME_MODE_HEADER, "LowestDifficultyTier") == 1

    with pytest.raises(ValueError, match="outside 1 to"):
        af.max_affix_tier_on_a_drop(0)


def test_the_sandbox_starts_at_the_lowest_tier() -> None:
    """A new character stands at tier 1, so the sandbox does too. It is the
    start of the game rather than a placeholder, and the value it defaults to is
    what every play session sees until somebody changes it."""
    text = GAME_MODE_HEADER.read_text(encoding="utf-8")
    assert re.search(r"int32\s+DifficultyTier\s*=\s*LowestDifficultyTier\s*;",
                     text), (
        "ACataclysmGameMode::DifficultyTier no longer defaults to "
        "LowestDifficultyTier. If the sandbox is meant to start somewhere else "
        "that is a design decision and belongs in docs/DECISIONS.md.")


def test_the_defect_issue_514_measured_is_what_the_model_computes() -> None:
    """The size of the problem, recomputed rather than restated.

    Every hit resolved at tier 1. Against the Abyssal Warden's designed armour
    that is the 75% cap, where its own tier gives 48.19%, so 25% of a hit landed
    instead of 51.81% -- 2.07 times harder to hurt than the design says.

    THIS IS THE MODEL'S OWN ARITHMETIC, not the engine's. The two are held
    together by `Cataclysm.Damage.ArmorUsesACurveNotASubtraction` in
    `game/Source/Cataclysm/Tests/CataclysmDamageCalculationTests.cpp`.
    """
    from cataclysm_sim import damage as dm

    at_tier_one = dm.armor_reduction(WARDEN_ARMOR_AT_TIER_EIGHT, tier=1)
    at_tier_eight = dm.armor_reduction(WARDEN_ARMOR_AT_TIER_EIGHT, tier=8)

    assert at_tier_one == pytest.approx(dm.ARMOR_REDUCTION_CAP)
    assert at_tier_eight == pytest.approx(48.19, abs=0.01)

    lands_at_tier_one = 1.0 - at_tier_one / 100.0
    lands_at_tier_eight = 1.0 - at_tier_eight / 100.0
    assert lands_at_tier_eight / lands_at_tier_one == pytest.approx(2.07,
                                                                   abs=0.01)
