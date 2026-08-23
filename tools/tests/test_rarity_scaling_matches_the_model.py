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


# --------------------------------------------------------------------------
# How big a creature is, and where it is allowed to stand. Issue #849.
# --------------------------------------------------------------------------

import csv  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
RARITIES_CSV = REPO / "game" / "Data" / "EnemyRarities.csv"
ENEMY_CHARACTER = (REPO / "game" / "Source" / "Cataclysm" / "Character"
                   / "CataclysmEnemyCharacter.cpp")
PLAYER_CHARACTER = (REPO / "game" / "Source" / "Cataclysm" / "Character"
                    / "CataclysmPlayerCharacter.cpp")
FLOOR_GENERATOR = (REPO / "game" / "Source" / "Cataclysm" / "Dungeon"
                   / "CataclysmFloorGenerator.h")


def a_constant(path: pathlib.Path, name: str) -> float:
    """One `constexpr float name = <number>;` read out of a C++ file.

    READ RATHER THAN RESTATED, because the whole point of the check below is
    that it stays true when somebody widens a corridor or resizes a creature.
    """
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    found = re.search(rf"{name}\s*=\s*([0-9.]+)f?\s*;",
                      path.read_text(encoding="utf-8"))
    assert found, f"{path.name} no longer declares {name}"
    return float(found.group(1))


@pytest.fixture(scope="module")
def rarities() -> list[dict]:
    if not RARITIES_CSV.is_file():
        pytest.skip(f"{RARITIES_CSV.name} is not present")
    with RARITIES_CSV.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def test_every_body_scale_matches_the_model(rarities, model) -> None:
    """The data is generated from the model, so this only fails if the
    generator stops carrying the figure through or somebody edits the CSV."""
    wrong = []
    for row in rarities:
        step = int(row["Step"])
        expected = (model.BODY_SCALE_AT_COMMON
                    * model.BODY_SCALE_PER_STEP ** step)
        if abs(float(row["BodyScale"]) - expected) > 1e-6:
            wrong.append(f"{row['RarityName']}: data {row['BodyScale']}, "
                         f"model {expected:.6f}")
    assert not wrong, (
        "game/Data/EnemyRarities.csv disagrees with BODY_SCALE_PER_STEP in "
        f"sim/cataclysm_sim/enemy_stats.py: {wrong}")


def test_a_common_creature_is_its_own_size(rarities) -> None:
    """Step 0 has to be exactly 1, or every creature in the game changes size
    at once and the three stat columns stop describing the same creature the
    size column does."""
    common = [row for row in rarities if int(row["Step"]) == 0]
    assert len(common) == 1, "there is not exactly one step 0 rarity"
    assert float(common[0]["BodyScale"]) == 1.0, (
        f"a Common creature's body scale is {common[0]['BodyScale']}, not 1")


def test_anything_too_wide_for_a_corridor_never_spawns_in_one(rarities) -> None:
    """THE CHECK THAT MAKES THE COMPOUNDING SAFE, and the reason it is allowed.

    The body scale compounds, so a Cataclysm Boss is 2.49 times a Common and
    239 cm across. The narrowest corridor the floor generator will build is two
    cells, and a player is 84 cm wide, so a creature wider than the corridor
    less the player traps them: there is no way past it.

    NOTHING IS TOO WIDE TODAY, AND THAT IS NEW. Until issue #885 the step was
    50% rather than 20%, which put a Cataclysm Boss at 729 cm -- wider than
    the 800 cm corridor once an 84 cm player is in it as well. What allowed
    that was the project owner's answer on 2026-08-23: a Cataclysm Boss fights
    in its own final arena at the end of its dungeon and never stands in a
    corridor, which is true in the data because its spawn weight is zero. That
    is still the design, and the size rule no longer depends on it.

    SO THIS HAS SLACK IN IT NOW RATHER THAN NOTHING TO CATCH, AND SAY SO
    PLAINLY: at 20% a step, giving a Cataclysm Boss a spawn weight would pass
    here. What still fails is making a rung that already spawns wide enough to
    block a passage, which is what this is for. The other half -- that the
    biggest rung fits at all, spawn weight or no spawn weight -- is checked in
    C++ by `Cataclysm.EnemyRarity.ARarerCreatureIsPhysicallyBigger`, which
    fails at 50% where this test passes.
    """
    body = a_constant(ENEMY_CHARACTER, "EnemyCapsuleRadius") * 2.0
    player = a_constant(PLAYER_CHARACTER, "CapsuleRadius") * 2.0
    cell = a_constant(FLOOR_GENERATOR, "CellSizeCm")
    cells = re.search(r"LeastConnectionWidth\s*=\s*(\d+)\s*;",
                      FLOOR_GENERATOR.read_text(encoding="utf-8"))
    assert cells, "the floor generator no longer states LeastConnectionWidth"
    corridor = cell * int(cells.group(1))

    trapped = []
    for row in rarities:
        wide = body * float(row["BodyScale"])
        if wide + player > corridor and float(row["SpawnWeight"]) > 0.0:
            trapped.append(
                f"{row['RarityName']} is {wide:.0f} cm across and spawns at "
                f"weight {row['SpawnWeight']}; with an {player:.0f} cm player "
                f"that needs {wide + player:.0f} cm and a corridor is "
                f"{corridor:.0f} cm")

    assert not trapped, (
        "a creature too wide to get past in the narrowest corridor can be "
        f"rolled onto an ordinary floor: {trapped}. Either make it smaller or "
        "give it a spawn weight of zero and place it in an arena, which is "
        "what the Cataclysm Boss does.")


def test_the_largest_creature_that_actually_spawns_still_fits(rarities) -> None:
    """The other half, and it would fail if every rung were given weight zero
    to satisfy the check above. Something has to spawn, and the biggest thing
    that does has to be passable."""
    body = a_constant(ENEMY_CHARACTER, "EnemyCapsuleRadius") * 2.0
    player = a_constant(PLAYER_CHARACTER, "CapsuleRadius") * 2.0
    cell = a_constant(FLOOR_GENERATOR, "CellSizeCm")
    cells = re.search(r"LeastConnectionWidth\s*=\s*(\d+)\s*;",
                      FLOOR_GENERATOR.read_text(encoding="utf-8"))
    corridor = cell * int(cells.group(1))

    spawning = [row for row in rarities if float(row["SpawnWeight"]) > 0.0]
    assert spawning, "no rarity spawns at all, so a floor would be empty"

    widest = max(spawning, key=lambda row: float(row["BodyScale"]))
    wide = body * float(widest["BodyScale"])
    assert wide + player <= corridor, (
        f"the widest creature that spawns is a {widest['RarityName']} at "
        f"{wide:.0f} cm, and with an {player:.0f} cm player that does not fit "
        f"a {corridor:.0f} cm corridor")
