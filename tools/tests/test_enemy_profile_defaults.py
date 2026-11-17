"""An enemy with no designed figures must carry the model's baseline ones.

WHY THIS EXISTS. Issue #372. Until it landed, `ApplyStartingAttributes` in
`game/Source/Cataclysm/Character/CataclysmEnemyCharacter.cpp` wrote three
attributes out of roughly twenty, and armour, every resistance, evasion and both
crit figures sat at whatever the attribute sets happened to default to.

Four of those are now declared on `ACataclysmEnemyCharacter` itself, because the
design model takes them "unchanged from the archetype" at every rarity rather
than scaling them by the encounter. Their defaults have to be the model's
BASELINE archetype -- the stat block it gives a creature whose own figures have
not been decided -- or an undesigned enemy in the engine and an undesigned enemy
in the model are two different creatures.

WHY ARMOUR IS NOT HERE. `stats_for` in `sim/cataclysm_sim/enemy_stats.py` scales
armour by the encounter's score and the enemy's rarity, so `armor_share` is a
multiplier and not a number of points. Nothing in the engine knows a score, so
armour is supplied by whoever spawns the creature, exactly as its health and its
attack damage are. `Cataclysm.Enemy.ArmourIsSuppliedRatherThanDeclared` holds
that end.
"""

from __future__ import annotations

import dataclasses
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ENEMY_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmEnemyCharacter.h")

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: The engine property, and the archetype field it has to default to.
#:
#: EVASION IS HERE TOO even though no designed enemy uses anything but zero. A
#: default that is only correct because nothing exercises it is a default nobody
#: has checked.
PAIRS = [
    ("ResistancePercent", "resistance"),
    ("CritChancePercent", "crit_chance"),
    ("CritMultiplierPercent", "crit_multiplier"),
    ("EvasionPercent", "evasion"),
]


def property_default(name: str) -> float:
    """The value a `float <name> = <number>f;` member is declared with."""
    if not ENEMY_HEADER.is_file():
        pytest.fail(f"{ENEMY_HEADER.relative_to(REPO_ROOT)} does not exist")

    found = re.search(
        rf"\bfloat\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        ENEMY_HEADER.read_text(encoding="utf-8"))
    if found is None:
        pytest.fail(
            f"CataclysmEnemyCharacter.h no longer declares a float named {name}. "
            f"Issue #372 put it there so an enemy carries the designed figure "
            f"rather than the attribute set's own default.")
    return float(found.group(1))


def baseline_defaults() -> dict[str, float]:
    """The design model's own defaults for an undesigned creature.

    READ OFF THE DATACLASS FIELDS rather than off the Baseline entry in
    ARCHETYPES, because those are what a NEW archetype gets when nobody fills
    them in, and that is the thing the engine's defaults have to match.
    """
    from cataclysm_sim.enemy_stats import Archetype

    return {field.name: field.default
            for field in dataclasses.fields(Archetype)}


@pytest.mark.parametrize("engine_name,model_name", PAIRS)
def test_the_default_is_the_models_baseline(engine_name, model_name) -> None:
    """The whole point: two places must agree about an undesigned creature.

    THE FAILURE THIS EXISTS FOR is somebody changing a default on either side.
    Raise the model's crit multiplier and every enemy in the engine keeps the old
    one; change the engine's and every undesigned creature stops matching what
    the simulation says it is.
    """
    designed = baseline_defaults()
    assert model_name in designed, (
        f"sim/cataclysm_sim/enemy_stats.py no longer has an Archetype field "
        f"named {model_name}, so {engine_name} in the engine is defaulting to a "
        f"figure the design has stopped having.")

    assert property_default(engine_name) == pytest.approx(designed[model_name]), (
        f"CataclysmEnemyCharacter.h declares {engine_name} = "
        f"{property_default(engine_name)} and the design model's Archetype "
        f"defaults {model_name} to {designed[model_name]}. An enemy whose own "
        f"figures have not been decided must carry what the model gives an "
        f"undesigned creature, or the engine and the simulation are describing "
        f"two different animals. Issue #372.")


def test_armour_is_not_declared_on_the_class() -> None:
    """Because it cannot be. It depends on the encounter, not the creature.

    THE FAILURE THIS EXISTS FOR is somebody adding an `ArmourPercent` beside the
    four above, reasoning by symmetry. They would then have to invent a number,
    because `armor_share` of 3.00 is a multiplier on a base scaled by the
    encounter's score and rarity, and nothing in the engine knows a score. The
    invented number would look designed and be nothing of the kind.
    """
    text = ENEMY_HEADER.read_text(encoding="utf-8")

    for wrong in ("ArmourPercent", "ArmorPercent", "DesignedArmour", "DesignedArmor"):
        assert not re.search(rf"\bfloat\s+{wrong}\b", text), (
            f"CataclysmEnemyCharacter.h declares {wrong}. Armour is a share of a "
            f"score-scaled base in the design model, so a figure declared on the "
            f"class is invented rather than designed. It arrives through "
            f"SetArmour from whoever spawns the creature. Issue #372.")

    assert "void SetArmour(" in text, (
        "ACataclysmEnemyCharacter no longer has SetArmour, so nothing can give "
        "an enemy the armour its archetype designs. Issue #372.")
