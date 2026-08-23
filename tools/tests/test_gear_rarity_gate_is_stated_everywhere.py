"""The gear rarity drop gate, pinned across the model, the document and the engine.

WHY THIS EXISTS. Issue #870, and it is the test that issue asked for by name.
The code let a drop roll one rarity above the difficulty tier; the design
document said rarity was level with the tier, twice in prose and once in its
table. They were one rung apart at every tier from 1 to 7 and agreed only at
tier 8, so the disagreement was invisible until drops began respecting the tier
at all. It was found by reading. Nothing compared the two.

The other two gates the difficulty tier applies already had one --
`test_affix_tier_gate_is_stated_everywhere.py` for affix tiers and
`test_upgrade_stone_gate_is_stated_everywhere.py` for upgrade stones, whose
docstring says outright "the rarity gate has nothing". This is the rarity gate's.

The rule is `min(8, difficulty tier + 1)`, stated in three places:

    sim/cataclysm_sim/loot.py           `best_rarity_on_a_drop` and
                                        `DROP_RARITIES_ABOVE_DIFFICULTY`
    docs/Cataclysm_GDD_v2.md            section VII's difficulty tier table,
                                        the column "Highest gear rarity a drop
                                        can roll"
    game/Source/.../CataclysmDropRoll.h `RaritiesAboveDifficulty`, which is what
                                        the engine enforces

WHAT THIS DOES NOT CHECK, because a Python test cannot run the engine: that
`BestRarityOnADrop` applies its constant the way this file assumes, and that the
drop site passes the tier being played. Those are covered by
`Cataclysm.Drop.TheRarityCascadeMatchesTheModel` and
`Cataclysm.Drop.RollingRespectsTheTierCap`.

WHAT THIS DELIBERATELY DOES NOT CHECK EITHER: how LIKELY each rarity is. That is
a separate question with a separate answer, it moves with the difficulty tier
since issue #886, and it is guarded in `sim/tests/test_loot.py`. A cap is not a
distribution.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DROP_ROLL_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
               / "CataclysmDropRoll.h")


@pytest.fixture(scope="module")
def model():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import loot
    return loot


@pytest.fixture(scope="module")
def documented_caps() -> dict[int, str]:
    """The highest rarity each difficulty tier may roll, out of the table.

    Read from the same rows the affix tier and upgrade stone tests read, which
    is the one table in the document that states all three gates side by side.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")

    found: dict[int, str] = {}
    for line in GDD.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\|\s*(\d+)\s*\|\s*T\d+\s*\|\s*(\w+)\s*\|\s*\+\d+",
                         line.strip())
        if match:
            found[int(match.group(1))] = match.group(2)

    assert found, (
        "no difficulty tier row in the design document states a gear rarity. "
        "Section VII's table should have a column headed 'Highest gear rarity "
        "a drop can roll', reading like '| 1 | T2 | Quality | +3 upgrade "
        "level |'.")
    return found


def test_the_table_covers_every_difficulty_tier(documented_caps, model) -> None:
    missing = [tier for tier in range(1, model.af.DIFFICULTY_TIERS + 1)
               if tier not in documented_caps]
    assert not missing, (
        f"the design document's difficulty tier table has no gear rarity for "
        f"tier(s) {missing}")


def test_every_documented_cap_is_a_real_rarity(documented_caps, model) -> None:
    """A typo in the document would otherwise pass the comparison below by
    failing to match anything at all."""
    unknown = {tier: rarity for tier, rarity in documented_caps.items()
               if rarity not in model.af.RARITIES}
    assert not unknown, (
        f"the design document names gear rarities that do not exist: {unknown}. "
        f"The eight are {list(model.af.RARITIES)}")


def test_the_document_and_the_model_agree(documented_caps, model) -> None:
    """The comparison issue #870 was about, in the direction it was decided.

    The project owner ruled on 2026-08-23 that the code was right and the
    document was stale, so the document now says one above the tier.
    """
    wrong = []
    for tier in range(1, model.af.DIFFICULTY_TIERS + 1):
        stated = documented_caps.get(tier)
        built = model.best_rarity_on_a_drop(tier)
        if stated != built:
            wrong.append(f"tier {tier}: document {stated}, model {built}")

    assert not wrong, (
        "the design document's gear rarity column disagrees with "
        "loot.best_rarity_on_a_drop. This is issue #870 recurring: "
        + "; ".join(wrong))


def test_the_engine_uses_the_same_step_above_the_tier(model) -> None:
    """`RaritiesAboveDifficulty` in the C++ is the model's constant.

    Read out of the header rather than restated here, so a change to one copy
    that skips the other fails instead of drifting the way #870 did.
    """
    if not DROP_ROLL_H.is_file():
        pytest.skip(f"{DROP_ROLL_H.name} is not present")

    found = re.search(
        r"RaritiesAboveDifficulty\s*=\s*(\d+)\s*;",
        DROP_ROLL_H.read_text(encoding="utf-8"))
    assert found, (
        f"{DROP_ROLL_H.name} no longer declares RaritiesAboveDifficulty")

    assert int(found.group(1)) == model.DROP_RARITIES_ABOVE_DIFFICULTY, (
        f"{DROP_ROLL_H.name} puts a drop {found.group(1)} rarities above the "
        f"difficulty tier and loot.DROP_RARITIES_ABOVE_DIFFICULTY says "
        f"{model.DROP_RARITIES_ABOVE_DIFFICULTY}")


def test_the_document_no_longer_says_rarity_equals_the_tier(model) -> None:
    """The two sentences #870 named, which the table alone would not catch.

    They said "Gear and gem rarity equal the difficulty tier" in sections IV and
    VII. The table can be right while the prose beside it still states the old
    rule, which is how the disagreement survived long enough to be shipped.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")

    body = GDD.read_text(encoding="utf-8")
    assert "rarity equal the difficulty tier" not in body, (
        "the design document still says gear rarity equals the difficulty "
        "tier. Issue #870 decided it is one rarity above; the table was "
        "changed and this sentence has to say the same thing.")
