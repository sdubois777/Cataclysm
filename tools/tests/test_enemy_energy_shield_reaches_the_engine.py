"""An enemy's designed energy shield, checked against the model. Issue #485.

WHY THIS EXISTS. The fraction reached `game/Data/EnemyArchetypes.csv`, the
generated table of enemy archetype statistics, and reached
`FCataclysmEnemyArchetypeRow` in `game/Source/Cataclysm/Data/CataclysmDataRows.h`,
the struct those rows are read into. Then it stopped:
`ACataclysmEnemyCharacter` had no property for it and `ApplyStartingAttributes`
never wrote `MaxEnergyShield`, so every enemy in the editor had a shield of zero
whatever the design said.

TWO OF THE SEVEN SLICE ENEMIES CARRY ONE AND NEITHER IS BUILT. The Succubus is
designed at 0.50 and the Corrupted Sentinel at 0.35, and neither has a C++ class
-- only the Brute and the Abyssal Warden do, and both are designed at 0.00. So
the engine change built the route rather than altering any creature, and the last
test here is what makes that route self-enforcing: it fails the moment either
creature's class appears, and its message says what to add.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++ at all, so
an assertion inside the engine would not run on a pull request. A test that reads
the constants out of the source as text does. Same arrangement as
`tools/tests/test_warden_matches_the_model.py`.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
ENEMY_HEADER = CHARACTER_DIR / "CataclysmEnemyCharacter.h"
ENEMY_SOURCE = CHARACTER_DIR / "CataclysmEnemyCharacter.cpp"
WARDEN_HEADER = CHARACTER_DIR / "CataclysmAbyssalWardenCharacter.h"

#: The archetypes the design gives an energy shield, and the class file each
#: would live in if it were built. Neither exists today, which is what
#: `test_a_shielded_enemy_that_gains_a_class_must_set_its_fraction` is about.
SHIELDED_ARCHETYPES = {
    "Succubus": CHARACTER_DIR / "CataclysmSuccubusCharacter.h",
    "Corrupted Sentinel": CHARACTER_DIR / "CataclysmCorruptedSentinelCharacter.h",
}


def constant(header: pathlib.Path, name: str) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    if not header.is_file():
        pytest.fail(f"{header.relative_to(REPO_ROOT)} does not exist")

    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        header.read_text(encoding="utf-8"))
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, the figure is "
            f"unguarded and continuous integration compiles no C++ to notice.")
    return float(match.group(1))


def archetypes() -> dict:
    from cataclysm_sim.enemy_stats import ARCHETYPES
    return ARCHETYPES


# --------------------------------------------------------------------------
# The route exists at all
# --------------------------------------------------------------------------

def test_the_base_enemy_class_can_express_an_energy_shield() -> None:
    """THE DEFECT ITSELF. Searching this class for "EnergyShield" returned
    nothing at all before issue #485: no property, no setter, and no write."""
    header = ENEMY_HEADER.read_text(encoding="utf-8")
    assert "EnergyShieldFraction" in header, (
        "ACataclysmEnemyCharacter has no EnergyShieldFraction property, so no "
        "enemy can carry an energy shield whatever the design says.")

    source = ENEMY_SOURCE.read_text(encoding="utf-8")
    assert "GetMaxEnergyShieldAttribute" in source, (
        "ApplyStartingAttributes in CataclysmEnemyCharacter.cpp no longer writes "
        "MaxEnergyShield. The fraction would reach the class and stop there, "
        "which is exactly the state issue #485 describes.")


def test_the_shield_is_a_fraction_of_health_rather_than_a_stored_figure() -> None:
    """`stats_for` in `sim/cataclysm_sim/enemy_stats.py` computes the shield as
    `health x energy_shield_fraction`, so the engine does the same arithmetic
    from the same two inputs. A second absolute number stored beside the health
    could disagree with it."""
    source = ENEMY_SOURCE.read_text(encoding="utf-8")
    # THE FRACTION IS A LOCAL SINCE 2026-09-05, because the Shielder enemy
    # modifier adds half the creature's health to whatever its archetype
    # gives. So the expression is `Health * Fraction` where `Fraction` is the
    # archetype's own plus the modifiers'. What this test is about is
    # unchanged: the shield is still computed FROM the health rather than
    # stored as an absolute figure that could disagree with it.
    assert re.search(r"Health\s*\*\s*(EnergyShieldFraction|Fraction)",
                     source), (
        "ApplyStartingAttributes no longer computes the shield as the health "
        "times the fraction. If it now stores an absolute figure, that figure "
        "can disagree with the health beside it.")

    # AND THE FRACTION IS STILL BUILT FROM THE ARCHETYPE'S OWN FIELD, which
    # is what stops the looser pattern above passing against a local holding
    # an absolute figure under the same name.
    assert re.search(r"Fraction\s*=\s*EnergyShieldFraction", source), (
        "ApplyStartingAttributes no longer builds the shield fraction from "
        "the archetype's own EnergyShieldFraction. The creature's designed "
        "share has to be in it, or five of the seven vertical slice enemies "
        "silently gain a shield they were designed without.")


# --------------------------------------------------------------------------
# Which creatures the design gives one
# --------------------------------------------------------------------------

def test_the_designed_fractions_are_the_two_the_issue_names() -> None:
    """Written as a comparison against the model rather than as two literals, so
    a third shielded creature appearing in the design fails here and is noticed
    rather than silently having no route."""
    shielded = {name: kind.energy_shield_fraction
                for name, kind in archetypes().items()
                if kind.energy_shield_fraction > 0.0}

    assert set(shielded) == set(SHIELDED_ARCHETYPES), (
        f"the archetypes with an energy shield are now {sorted(shielded)}. "
        f"SHIELDED_ARCHETYPES in this file lists {sorted(SHIELDED_ARCHETYPES)}, "
        f"and each entry needs a class file named there so the guard below can "
        f"tell whether it has been built.")

    assert shielded["Succubus"] == pytest.approx(0.50)
    assert shielded["Corrupted Sentinel"] == pytest.approx(0.35)


def test_the_wardens_designed_fraction_matches_the_model() -> None:
    """It is zero, and it is written out rather than left to the base class's
    default for the same reason its designed evasion of 0.0 is: the zero is
    visibly designed rather than visibly forgotten."""
    assert constant(WARDEN_HEADER, "DesignedEnergyShieldFraction") == \
        pytest.approx(archetypes()["Abyssal Warden"].energy_shield_fraction), (
        "DesignedEnergyShieldFraction in CataclysmAbyssalWardenCharacter.h has "
        "drifted from ARCHETYPES['Abyssal Warden'].energy_shield_fraction in "
        "sim/cataclysm_sim/enemy_stats.py, which is authoritative.")


def test_a_shielded_enemy_that_gains_a_class_must_set_its_fraction() -> None:
    """THE GUARD THAT MAKES THE ROUTE SELF-ENFORCING.

    Neither creature that needs an energy shield is built. This fails as soon as
    one of them is, so the number cannot be forgotten at the only moment it can
    be set. Issue #39 builds the seven slice enemies.
    """
    for name, header in SHIELDED_ARCHETYPES.items():
        fraction = archetypes()[name].energy_shield_fraction
        if not header.is_file():
            continue

        text = header.read_text(encoding="utf-8")
        assert "EnergyShieldFraction" in text, (
            f"the {name} now has a class at "
            f"{header.relative_to(REPO_ROOT)} and does not set its energy "
            f"shield. Its designed fraction is {fraction}, which is a large "
            f"share of what keeps it alive. Add a "
            f"'static constexpr float DesignedEnergyShieldFraction = "
            f"{fraction}f;' beside its other designed figures, assign it to "
            f"EnergyShieldFraction in the constructor the way "
            f"ACataclysmAbyssalWardenCharacter does, and replace this branch "
            f"with a check of that constant against the model.")
