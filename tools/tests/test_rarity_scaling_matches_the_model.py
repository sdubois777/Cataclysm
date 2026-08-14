"""The rarity ladder's numbers in the design document, checked against the model.

WHY THIS EXISTS. Issue #484. `docs/Cataclysm_GDD_v2.md`, the main design
document, said "Per step of rarity above Common, health is multiplied by 1.85,
damage by 1.55 and armor by 1.35". `sim/cataclysm_sim/enemy_stats.py`, which
defines every enemy archetype's combat statistics, uses **1.40** for damage. Its
own comment records the change: the growth per step was lowered from 1.55
"because raising the floor without lowering the slope would have made the boss a
guaranteed one-shot".

That sentence sits in the subsection explaining how the rarity ladder works,
which is exactly where somebody sizing a new enemy would read it.

**THE ISSUE'S SECOND CLAIM WAS WRONG, and it is worth recording why.** #484 also
says the document's "about 6 times as hard" is stale, on the grounds that 1.40 to
the fifth power is 5.38. That arithmetic does not describe this model. Every
statistic is a share of an archetype average with a floor under it, so the ratio
between the top and the bottom of the ladder is not the step multiplier
compounded. Measured at difficulty tier 8, the Cataclysm Boss hits **5.86** times
as hard as a Common enemy, which is what "about 6" means. The document's
consequence sentence was correct and its per-step figure was not.

**The two sentences contradicted each other**, which is the strongest evidence
that one was stale. Setting the constant back to 1.55 and re-measuring gives a
boss that hits **9.75** times as hard, not 6.

WHAT IS ASSERTED HERE.

    the document's three per-step multipliers equal the model's constants
    the document's boss-to-common health and damage figures match what the
      model actually produces
    the paragraph warning against raising the step multipliers to the fifth
      power is still there, because that reading is what went wrong
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: The tier the boss-to-common figures are quoted at. The ratios are stable
#: across tiers; stating one keeps the test's arithmetic reproducible.
TIER = 8

#: The dungeon type used for the measurement, matching the document's context.
DUNGEON = "Cataclysm"

#: How close the document's rounded prose has to be, as a fraction rather than
#: an absolute amount. "roughly 23 times" and "about 6 times" are rounded to a
#: whole number, so the slack a figure needs grows with the figure: 23.61 is
#: 0.61 away from 23, and 5.86 is 0.14 away from 6. An absolute tolerance
#: generous enough for the first would be four times too loose for the second.
#:
#: 5% is wide enough for rounding to the nearest whole number at both ends and
#: narrow enough to reject the stale figure: at the old 1.55 the boss hit 9.75
#: times as hard, which is 62% away from the stated 6.
RATIO_TOLERANCE = 0.05


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return " ".join(GDD.read_text(encoding="utf-8").split())


@pytest.fixture(scope="module")
def model():
    return pytest.importorskip("cataclysm_sim.enemy_stats")


def test_the_per_step_multipliers_match_the_model(document, model) -> None:
    """The whole of issue #484. The document carried 1.55 for damage where the
    model had moved to 1.40, so anyone sizing an enemy from the document rather
    than the model was 11% out per rarity step, compounding."""
    sentence = re.search(
        r"Per step of rarity above Common, health is multiplied by "
        r"([\d.]+), damage by ([\d.]+) and armor by ([\d.]+)\.", document)
    assert sentence, (
        "the design document no longer states the per-step rarity multipliers "
        "in the form this check reads. If the sentence was rewritten, update "
        "the pattern here; if it was removed, the rarity ladder's numbers are "
        "now only in the model. Issue #484.")

    stated = {
        "health": (float(sentence.group(1)), model.HEALTH_PER_STEP),
        "damage": (float(sentence.group(2)), model.DAMAGE_PER_STEP),
        "armor": (float(sentence.group(3)), model.ARMOR_PER_STEP),
    }
    wrong = {name: pair for name, pair in stated.items() if pair[0] != pair[1]}
    assert not wrong, (
        "the design document states rarity multipliers the model does not use. "
        "Statistic: document, model -- "
        + "; ".join(f"{n}: {d}, {m}" for n, (d, m) in sorted(wrong.items()))
        + ". sim/cataclysm_sim/enemy_stats.py is the fitted source; the "
          "document is a transcription of it. Issue #484.")


def test_the_boss_to_common_figures_match_what_the_model_produces(
        document, model) -> None:
    """The check that would have caught the contradiction.

    The document says a Cataclysm Boss has roughly 23 times a Common enemy's
    health and hits about 6 times as hard. Those are consequences of the
    constants, so they and the constants cannot both be free. With damage at
    1.55 the boss hits 9.75 times as hard, and the two sentences disagreed for
    as long as the stale figure stood.
    """
    common = model.stats_on_floor("Common", TIER, DUNGEON)
    boss = model.stats_on_floor("Cataclysm Boss", TIER, DUNGEON)

    measured = {
        "health": boss.health / common.health,
        "damage": boss.damage_per_hit / common.damage_per_hit,
    }

    stated = re.search(
        r"a Cataclysm Boss ends up with roughly ([\d.]+) times a Common "
        r"enemy's health and hits about ([\d.]+) times as hard", document)
    assert stated, (
        "the design document no longer states what a Cataclysm Boss comes to "
        "relative to a Common enemy. Issue #484.")

    for name, said in (("health", float(stated.group(1))),
                       ("damage", float(stated.group(2)))):
        real = measured[name]
        assert abs(said - real) <= real * RATIO_TOLERANCE, (
            f"the design document says a Cataclysm Boss has {said} times a "
            f"Common enemy's {name}, and the model produces {real:.2f} at tier "
            f"{TIER}, which is {abs(said - real) / real:.1%} away. Either the "
            f"constants moved and the prose did not, or the prose was rounded "
            f"from a different model. Issue #484.")


def test_the_document_warns_against_compounding_the_step_multipliers(
        document) -> None:
    """The misreading that produced the wrong figure in the issue itself.

    Raising 1.40 to the fifth power gives 5.38 and looks like it should be the
    boss-to-common damage ratio. It is not, because every statistic is a share
    of an archetype average with a floor under it. Somebody will try that
    arithmetic again, so the document says why it does not work.
    """
    assert ("Those two figures are not the per-step multipliers raised to the "
            "fifth power") in document, (
        "the design document states both the per-step multipliers and the "
        "boss-to-common ratios without saying that the second is not the first "
        "compounded. Issue #484 made exactly that inference and drew a wrong "
        "conclusion from it.")
