"""The drop distributions pinned by hand in the Unreal test, against the model.

WHY THIS EXISTS. `game/Source/Cataclysm/Tests/CataclysmDropRollTests.cpp` pins
what fraction of drops at difficulty tier 8 is each rarity, and what fraction of
affixes is each tier. Both were printed by `sim/cataclysm_sim` and typed in by
hand, which is the right way to write them -- a figure recomputed in C++ by the
same reasoning that produced the C++ would not test anything.

**Nothing on a pull request runs that file.** Continuous integration builds no
C++ at all. So changing a drop weight in the design workbook leaves those
literals stale, the pull request merges, and the staleness is found later by
whoever next runs the engine tests on a Windows machine.

This is the same gap, and the same cure, as
`tools/tests/test_unreal_pinned_row_counts.py`, whose docstring records two
tables going stale in one afternoon on 2026-08-14. Reading the pinned numbers
and comparing them is strictly better than restating them: it needs no
maintenance and cannot itself go stale.

WHAT IS NOT CHECKED HERE. Whether the C++ actually produces those fractions.
Only the automation test can decide that, because only it can run the engine.
This checks that what the automation test will compare against is still what the
model says.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
UNREAL_TEST = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
               / "CataclysmDropRollTests.cpp")

#: The difficulty tier both pinned arrays are written for. Named here because it
#: appears in the C++ array names as well, so changing one without the other
#: would be caught by the count check rather than passing quietly.
TIER = 8


def pinned_floats(name: str) -> list[float]:
    """The float literals of one `const float <name>[] = { ... };` array."""
    if not UNREAL_TEST.is_file():
        pytest.skip(f"{UNREAL_TEST.name} is not present")

    text = UNREAL_TEST.read_text(encoding="utf-8")
    block = re.search(
        r"const\s+float\s+" + re.escape(name) + r"\s*\[\s*\]\s*=\s*\{(.*?)\}",
        text, re.S)
    assert block, (
        f"{UNREAL_TEST.name} has no `const float {name}[]` array. Either it was "
        f"renamed, in which case fix the name in this file, or the pinned "
        f"figures are gone. Every assertion below would otherwise pass having "
        f"compared nothing.")

    found = [float(literal)
             for literal in re.findall(r"([0-9]*\.[0-9]+)f", block.group(1))]
    assert found, (
        f"no float literals could be parsed out of {name}[] in "
        f"{UNREAL_TEST.name}.")
    return found


@pytest.fixture(scope="module")
def loot():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import loot as module
    return module


@pytest.fixture(scope="module")
def affixes():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import affixes as module
    return module


def test_the_pinned_rarity_shares_match_the_model(loot, affixes) -> None:
    """One number per rarity, weakest first, from loot.rarity_distribution."""
    pinned = pinned_floats(f"RarityShareAtTier{TIER}")
    expected = loot.rarity_distribution(TIER, 0.0)

    assert len(pinned) == len(affixes.RARITIES), (
        f"the C++ pins {len(pinned)} rarity share(s) and there are "
        f"{len(affixes.RARITIES)} rarities.")

    # The literals are written to ten places, which is more than a float holds,
    # so the comparison is relative. The shares run from 0.61 down to 0.0000392
    # and one absolute tolerance cannot serve both ends.
    wrong = [
        (rarity, pinned[index], expected[rarity])
        for index, rarity in enumerate(affixes.RARITIES)
        if abs(pinned[index] - expected[rarity]) > expected[rarity] * 1e-6
    ]
    assert not wrong, (
        f"{UNREAL_TEST.name} pins rarity shares at difficulty tier {TIER} that "
        "the model no longer produces. Rarity: pinned, model -- "
        + "; ".join(f"{r}: {p:.10f}, {m:.10f}" for r, p, m in wrong)
        + ". The automation test will fail on the next machine that runs it. "
          "Reprint them with "
          "`python -c \"from cataclysm_sim import loot; "
          "print(loot.rarity_distribution(8, 0.0))\"` from sim/.")


def test_the_pinned_affix_tier_shares_match_the_model(affixes) -> None:
    """One number per affix tier, T1 first, from affix_tier_distribution."""
    pinned = pinned_floats(f"AffixTierShareAtTier{TIER}")
    expected = affixes.affix_tier_distribution(TIER)

    assert len(pinned) == len(affixes.AFFIX_TIERS), (
        f"the C++ pins {len(pinned)} affix tier share(s) and there are "
        f"{len(affixes.AFFIX_TIERS)} affix tiers.")

    wrong = [
        (tier, pinned[index], expected[tier])
        for index, tier in enumerate(affixes.AFFIX_TIERS)
        if abs(pinned[index] - expected[tier]) > expected[tier] * 1e-6
    ]
    assert not wrong, (
        f"{UNREAL_TEST.name} pins affix tier shares at difficulty tier {TIER} "
        "that the model no longer produces. Tier: pinned, model -- "
        + "; ".join(f"T{t}: {p:.10f}, {m:.10f}" for t, p, m in wrong))


def test_the_pinned_headline_odds_match_the_model(loot) -> None:
    """The two figures the weighting decision was argued on, which the C++ test
    states in words as well as in the array: one Cataclysmic drop in 25,531 and
    one Masterful in 26.

    Read out of the C++ comments rather than restated, so a decision that is
    revisited cannot leave the sentence there saying the old number.
    """
    if not UNREAL_TEST.is_file():
        pytest.skip(f"{UNREAL_TEST.name} is not present")
    text = UNREAL_TEST.read_text(encoding="utf-8")

    shares = loot.rarity_distribution(TIER, 0.0)
    for rarity, stated in (("Cataclysmic", "25,531"), ("Masterful", "3")):
        one_in = 1.0 / shares[rarity]
        assert f"one in {stated}" in text, (
            f"{UNREAL_TEST.name} no longer says a {rarity} drop is one in "
            f"{stated}. The model puts it at one in {one_in:.1f}.")
        assert abs(one_in - float(stated.replace(",", ""))) < one_in * 0.03, (
            f"{UNREAL_TEST.name} says a {rarity} drop is one in {stated} and "
            f"the model puts it at one in {one_in:.1f}.")
