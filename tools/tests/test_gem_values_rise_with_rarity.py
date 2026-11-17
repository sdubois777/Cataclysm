"""Every gem is worth more at a rarer tier, checked in the table the game loads.

WHY THIS EXISTS. Issue #246. The gem Of The Goblin shipped with an Everyday value
of 0.05 against its own description of ".5%", which is 0.005. It was ten times the
stated figure and larger than the six rarity tiers above it, so an Everyday Of The
Goblin beat every one below Cataclysmic and tied the Cataclysmic one.

The cause was in `tools/generate_datatables.py`. A gem's Everyday value is not
stored in `docs/All_Things_Cataclysm.xlsx` at all — it is read out of the effect
text with a regular expression, and that expression required a digit before the
decimal point, so on ".5%" it matched "5%". The generator now refuses a gem whose
values do not rise, and `tools/tests/test_generate_datatables.py` covers the
parsing against fixture workbooks.

WHY THIS SECOND FILE EXISTS AS WELL. Those tests run the generator. This one reads
`game/Data/Gems.csv`, which is what the game actually loads, so it catches a
committed table that has gone stale against the generator — someone editing the
CSV by hand, or committing before regenerating.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GEMS_CSV = REPO_ROOT / "game" / "Data" / "Gems.csv"

#: The eight gear rarities, in order, which are also the eight gem tiers.
RARITIES = ("Everyday", "Quality", "Superb", "Masterful",
            "Legendary", "Mythical", "Ascendant", "Cataclysmic")


@pytest.fixture(scope="module")
def gems() -> list[dict[str, str]]:
    if not GEMS_CSV.is_file():
        pytest.skip(f"{GEMS_CSV.name} is not present")
    with GEMS_CSV.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def values(row: dict[str, str]) -> list[float]:
    return [float(row[r]) for r in RARITIES]


def test_there_are_gems_to_check(gems):
    """A file that failed to parse would make every check below pass while
    checking nothing."""
    assert len(gems) >= 20, f"only {len(gems)} gems in {GEMS_CSV.name}"


def test_the_eight_rarities_are_all_columns(gems):
    for rarity in RARITIES:
        assert rarity in gems[0], (
            f"{GEMS_CSV.name} has no {rarity} column, so this file is checking "
            "something other than the rarity ladder")


def test_every_gem_is_worth_more_at_every_rarer_tier(gems):
    """Stated as a STRICT increase. Allowing ties would let a rarity step exist
    that a player pays for and gets nothing from, and the test would still be
    called 'rises'."""
    failures = []
    for row in gems:
        ladder = values(row)
        for i, (lower, higher) in enumerate(
                zip(ladder, ladder[1:], strict=False)):
            if higher <= lower:
                failures.append(
                    f"{row['GemName']}: {RARITIES[i + 1]} is {higher}, "
                    f"not above {RARITIES[i]}'s {lower}  {ladder}")
    assert not failures, (
        "gems that do not improve with rarity:\n  " + "\n  ".join(failures))


def test_every_gem_starts_at_the_percentage_its_own_description_states(gems):
    """The Everyday value IS the number in the effect text, extracted by the
    generator. This checks the committed table still agrees with the prose a
    player reads, which is the pair that disagreed in issue #246."""
    failures = []
    for row in gems:
        match = re.search(r"(\d*\.?\d+)\s*%", row["Effect"])
        if not match:
            failures.append(f"{row['GemName']}: no percentage in "
                            f"{row['Effect']!r}")
            continue
        stated = float(match.group(1)) / 100.0
        actual = float(row["Everyday"])
        if abs(stated - actual) > 1e-9:
            failures.append(
                f"{row['GemName']}: says {row['Effect']!r} which is {stated}, "
                f"but its Everyday value is {actual}")
    assert not failures, (
        "gems whose Everyday value contradicts their own description:\n  "
        + "\n  ".join(failures))


def test_the_gem_that_had_the_wrong_value_now_has_the_right_one(gems):
    """Named directly, because a regression here is the exact bug returning and
    a generic message would not say so."""
    goblin = next((g for g in gems if g["GemName"] == "Of The Goblin"), None)
    assert goblin is not None, f"{GEMS_CSV.name} has no Of The Goblin row"
    assert float(goblin["Everyday"]) == pytest.approx(0.005), (
        "Of The Goblin's Everyday value is not 0.005. Its description says "
        ".5%. If it reads 0.05 the percentage-reading pattern in "
        "tools/generate_datatables.py has lost its handling of a decimal "
        "point with no leading digit. See issue #246.")
    assert values(goblin) == [0.005, 0.01, 0.015, 0.02, 0.025, 0.03, 0.035,
                              0.05]


def test_no_gem_is_worth_nothing_at_any_tier(gems):
    """A zero anywhere on the ladder is a gem that does nothing at that rarity,
    which the rising check alone would not catch at the first tier."""
    for row in gems:
        for rarity, value in zip(RARITIES, values(row), strict=True):
            assert value > 0.0, (
                f"{row['GemName']} is worth {value} at {rarity}")
