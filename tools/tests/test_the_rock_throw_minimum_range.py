"""The Brute will not throw a rock at something standing against it.

WHY THIS EXISTS. Issue #475. `RockThrow.MinRangeCm` was
`DesignedMeleeReachCm`, 90 cm, which is exactly the distance at which the Brute's
48 cm capsule and the player's 42 cm capsule are already touching. A minimum
range set to the distance at which the two bodies are in contact refuses nothing,
and the project owner reported the creature throwing rocks at point blank.

WHAT THE FIGURE IS DERIVED FROM. An attack that marks a circle should not be
marking the ground its own caster is standing on. Below `marked radius + own body
radius` the Brute is inside the area it is about to hit, so the throw is a melee
attack wearing a thrown attack's telegraph. 210 + 48 is 258 centimetres.

WHAT THIS CANNOT SAY. Whether 258 is the right FEEL. The genre research that
`CLAUDE.md` asks for before proposing a mechanic was started and failed part way
through on a session token limit, so this figure is derived from the project's
own numbers and not from any comparison with a shipped game. What is checked here
is that it is still that derivation and still above the distance at which a
minimum range means nothing.

WHY IT IS A PYTHON TEST. Continuous integration never builds the C++ and never
opens the editor, so an engine test cannot run on a pull request. This reads the
source text, which is present either way.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BRUTE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmBruteCharacter.cpp")
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")
CONTROLLER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                  / "CataclysmEnemyController.cpp")


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def constant(name: str) -> float:
    """A `static constexpr float <name> = <number>f;` from the Brute's header."""
    match = re.search(
        rf"static constexpr float\s+{re.escape(name)}\s*=\s*(-?[\d.]+)f\s*;",
        source(BRUTE_HEADER))
    if match is None:
        pytest.fail(
            f"CataclysmBruteCharacter.h has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too.")
    return float(match.group(1))


def test_the_minimum_is_the_marked_circle_plus_the_creatures_own_body() -> None:
    """The derivation, checked as arithmetic rather than taken on trust."""
    minimum = constant("RockThrowMinimumRangeCm")
    marked = constant("RockThrowRadiusCm")
    body = constant("BruteCapsuleRadius")

    assert minimum == pytest.approx(marked + body), (
        f"the rock throw's minimum range is {minimum} cm and the marked circle "
        f"plus the creature's body radius is {marked} + {body} = "
        f"{marked + body}. Below that sum the Brute stands inside the circle "
        f"its own throw marks, which is what issue #475 was about. If the "
        f"minimum is meant to be something else now, the derivation in the "
        f"header comment and in docs/Cataclysm_GDD_v2.md has to change with it."
    )


def test_the_minimum_actually_refuses_something() -> None:
    """A minimum at contact distance is a minimum in name only.

    This is the failure that was there, stated as a rule: the two capsules touch
    at `DesignedMeleeReachCm`, so anything at or below that can never turn an
    ability down.
    """
    minimum = constant("RockThrowMinimumRangeCm")
    contact = constant("DesignedMeleeReachCm")

    assert minimum > contact, (
        f"the rock throw's minimum range is {minimum} cm and the two bodies "
        f"already touch at {contact} cm, so it refuses nothing. That was the "
        f"state issue #475 was filed about."
    )


def test_the_ability_is_given_the_sum_rather_than_a_literal() -> None:
    """So changing either term moves the minimum with it.

    The header carries the total for tests to read, and the ability is built
    from the two terms, and a static_assert holds them together. What this
    checks is that the ability really is built from the terms -- a literal
    written in here instead would drift the first time the marker changed size.
    """
    text = source(BRUTE_CPP)

    assert re.search(
        r"RockThrow\.MinRangeCm\s*=\s*RockThrowRadiusCm\s*\+\s*BruteCapsuleRadius\s*;",
        text), (
        "CataclysmBruteCharacter.cpp no longer builds RockThrow.MinRangeCm from "
        "RockThrowRadiusCm + BruteCapsuleRadius. If the marked circle is ever "
        "resized, a minimum written as a literal would quietly stop being the "
        "distance at which the creature clears its own blast."
    )

    assert "static_assert" in text and "RockThrowMinimumRangeCm" in text, (
        "the static_assert holding RockThrowMinimumRangeCm to the same sum is "
        "gone, so the constant tests read and the value the ability uses can "
        "now differ."
    )


def test_the_minimum_range_is_enforced_somewhere() -> None:
    """It is read when the ability is chosen, and that is the only place.

    WHY THAT IS RECORDED RATHER THAN FIXED. The wind-up runs for a second after
    the choice, and a player walking at 400 cm/s can close four metres inside
    it, so no minimum survives a determined approach. Letting the throw land
    where it was marked is the rule docs/DECISIONS.md states for every
    telegraphed attack: a player who walks inside the minimum has dodged it.

    What this guards is that the check exists at all. Losing it would make the
    minimum a number nothing reads.
    """
    text = source(CONTROLLER_CPP)

    assert "MinRangeCm" in text, (
        "CataclysmEnemyController.cpp no longer mentions MinRangeCm, so no "
        "ability's minimum range is enforced anywhere and every enemy will use "
        "every ability at contact distance."
    )

    assert re.search(r"DistanceCm\s*<\s*Ability\.MinRangeCm", text), (
        "CataclysmEnemyController.cpp no longer refuses an ability whose "
        "minimum range is further than the target. The comparison may have been "
        "rewritten; if so, rewrite this to match. If it was deleted, issue #475 "
        "records what that costs."
    )
