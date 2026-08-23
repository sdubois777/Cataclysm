"""How far the difficulty tier reaches on gear rarity, pinned across three copies.

WHY THIS EXISTS. Issue #870, and it is the test that issue asked for by name.
The code let a drop roll one rarity above the difficulty tier; the design
document said rarity was level with the tier, twice in prose and once in its
table. They were one rung apart at every tier from 1 to 7 and agreed only at
tier 8, so the disagreement was invisible until drops began respecting the tier
at all. It was found by reading. Nothing compared the two.

The other two gates the difficulty tier applies already had one --
`test_affix_tier_gate_is_stated_everywhere.py` for affix tiers and
`test_upgrade_stone_gate_is_stated_everywhere.py` for upgrade stones, whose
docstring said outright "the rarity gate has nothing". This is the rarity one.

WHAT IT IS NOW, AND IT IS NO LONGER A GATE. The project owner played the capped
version on 2026-08-23 and said it was too strict: at difficulty tier 1 only
Everyday and Quality could drop at all. **Every gear rarity now drops at every
difficulty tier.** The same figure decides where a penalty starts instead --
above it, a rarity is divided by `RARITY_PENALTY_ABOVE_THE_TIER` once per rung.

So the three copies to keep together are:

    sim/cataclysm_sim/loot.py           `highest_unpenalised_rarity` and
                                        `DROP_RARITIES_ABOVE_DIFFICULTY`
    docs/Cataclysm_GDD_v2.md            section VII's difficulty tier table,
                                        the column "Highest gear rarity a drop
                                        rolls unpenalised"
    game/Source/.../CataclysmDropRoll.h `RaritiesAboveDifficulty`, which is what
                                        the engine applies

WHAT THIS DOES NOT CHECK, because a Python test cannot run the engine: that the
engine applies its constant the way this file assumes, and that the drop site
passes the tier being played. Those are covered by
`Cataclysm.Drop.TheRarityCascadeMatchesTheModel` and
`Cataclysm.Drop.EveryRarityDropsAtEveryTier`.

AND IT DELIBERATELY DOES NOT CHECK HOW LIKELY EACH RARITY IS. That is a separate
question with a separate answer, it moves with the difficulty tier, and it is
guarded in `sim/tests/test_loot.py`.
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
def documented_reach() -> dict[int, str]:
    """The highest rarity each tier rolls unpenalised, out of the table.

    Read from the same rows the affix tier and upgrade stone tests read, which
    is the one table in the document that states all three side by side.
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
        "a drop rolls unpenalised', reading like '| 1 | T2 | Quality | +3 "
        "upgrade level |'.")
    return found


def test_the_table_covers_every_difficulty_tier(documented_reach, model) -> None:
    missing = [tier for tier in range(1, model.af.DIFFICULTY_TIERS + 1)
               if tier not in documented_reach]
    assert not missing, (
        "the design document's difficulty tier table has no gear rarity for "
        f"tier(s) {missing}")


def test_every_documented_rarity_is_a_real_rarity(documented_reach,
                                                  model) -> None:
    """A typo in the document would otherwise pass the comparison below by
    failing to match anything at all."""
    unknown = {tier: rarity for tier, rarity in documented_reach.items()
               if rarity not in model.af.RARITIES}
    assert not unknown, (
        f"the design document names gear rarities that do not exist: {unknown}. "
        f"The eight are {list(model.af.RARITIES)}")


def test_the_document_and_the_model_agree(documented_reach, model) -> None:
    """The comparison issue #870 was about, in the direction it was decided.

    The project owner ruled on 2026-08-23 that the code was right and the
    document was stale, so the document says one above the tier.
    """
    wrong = []
    for tier in range(1, model.af.DIFFICULTY_TIERS + 1):
        stated = documented_reach.get(tier)
        built = model.highest_unpenalised_rarity(tier)
        if stated != built:
            wrong.append(f"tier {tier}: document {stated}, model {built}")

    assert not wrong, (
        "the design document's gear rarity column disagrees with "
        "loot.highest_unpenalised_rarity. This is issue #870 recurring: "
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


def test_the_engine_uses_the_same_penalty_above_the_tier(model) -> None:
    """And the same divisor, which is what makes the column not a cap."""
    if not DROP_ROLL_H.is_file():
        pytest.skip(f"{DROP_ROLL_H.name} is not present")

    found = re.search(
        r"RarityPenaltyAboveTheTier\s*=\s*([0-9.]+)f?\s*;",
        DROP_ROLL_H.read_text(encoding="utf-8"))
    assert found, (
        f"{DROP_ROLL_H.name} no longer declares RarityPenaltyAboveTheTier")

    assert float(found.group(1)) == pytest.approx(
        model.RARITY_PENALTY_ABOVE_THE_TIER), (
        f"{DROP_ROLL_H.name} divides by {found.group(1)} per rarity above the "
        f"tier and loot.RARITY_PENALTY_ABOVE_THE_TIER says "
        f"{model.RARITY_PENALTY_ABOVE_THE_TIER}")


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


def test_the_document_says_the_column_is_not_a_cap(model) -> None:
    """The change of 2026-08-23, which the table cannot show on its own.

    The column looks identical whether it caps a drop or merely marks where a
    penalty starts, and the difference is the whole of what the project owner
    asked for: at difficulty tier 1 the capped version dropped only Everyday
    and Quality. A reader given the table alone would take it for a cap, which
    is what it was.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")

    body = GDD.read_text(encoding="utf-8")
    assert "Every gear rarity can drop at every difficulty tier" in body, (
        "the design document's difficulty tier table no longer says that its "
        "gear rarity column is not a cap. Without that sentence the table "
        "reads as the hard gate it stopped being on 2026-08-23.")
