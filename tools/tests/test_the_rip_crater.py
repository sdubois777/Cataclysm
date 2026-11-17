"""The hole the Brute tears its rock out of.

WHY THIS EXISTS. Issue #432. `Ability_RipNToss_Rip` is an animation whose whole
content is reaching down, tearing a rock out of the ground and lifting it. Since
issue #421 the rock is visibly in the creature's hand while it does that, and the
ground it came out of was untouched.

WHAT THE ISSUE ACTUALLY ASKED FOR was four decisions, not a feature: where the
crater goes, when it appears, how long it stays, and whether it is per-Brute or
per-place. Two of those turned out to be measurements rather than judgements, and
the other two collapse into one constant.

WHAT THIS GUARDS. Continuous integration has no engine and no Paragon pack, so
nothing here can spawn an actor or open a mesh. What it holds is the arithmetic
between the constants, the reuse decision, and the measurement staying written
down. The live behaviour is covered by
`game/Source/Cataclysm/Tests/CataclysmRipCraterTests.cpp`, whose five tests run
against real actors in a real world.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")
BRUTE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmBruteCharacter.cpp")
ASSET_REFERENCE = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

#: The engine's placeholder. A crater left wearing this is the failure.
PLACEHOLDER_MATERIAL = "WorldGridMaterial"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def code(path: pathlib.Path) -> str:
    """The source with its single-line comments removed.

    WHY THIS IS NEEDED, and it is not hypothetical: during issue #422 a guard
    that searched the whole file was satisfied by a call that had been commented
    out, which is exactly how somebody disables a thing temporarily and then
    forgets.
    """
    return re.sub(r"//[^\n]*", "", source(path))


def constant(name: str) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    found = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        source(BRUTE_HEADER))
    if found is None:
        pytest.fail(
            f"CataclysmBruteCharacter.h no longer declares {name}, so the "
            f"relationship this test holds cannot be checked.")
    return float(found.group(1))


def test_a_brute_can_never_have_two_craters() -> None:
    """The invariant that replaces a crater manager.

    THIS IS THE THIRD AND FOURTH OF ISSUE #432'S DECISIONS TAKEN TOGETHER --
    how long a crater stays and whether craters are per-Brute or per-place. A
    lifetime under the throw's own cooldown means one creature's second crater
    cannot exist while its first still does. That turns accumulation from a
    thing needing a manager, a cap and an eviction rule into arithmetic.

    THE FAILURE THIS EXISTS FOR is somebody lengthening the crater because four
    seconds looked short on screen, without noticing that at six they start
    stacking, or shortening the cooldown in a balance pass for reasons that have
    nothing to do with art.
    """
    stays = constant("CraterSecondsOnTheGround")
    cooldown = constant("RockThrowCooldownSeconds")

    assert stays < cooldown, (
        f"a rip crater stays {stays} s and the Brute can throw again after "
        f"{cooldown} s, so one creature can have two craters on the floor at "
        f"once. Either bring the crater's life back under the cooldown, or "
        f"decide deliberately how they accumulate and cap them -- which is the "
        f"work issue #432 avoided by keeping this true."
    )


def test_the_crater_is_wider_than_the_rock_that_came_out_of_it() -> None:
    """A hole narrower than its rock reads as the wrong hole.

    Not a measurement -- nobody has watched this -- but it is the one
    relationship between the two numbers that can be argued for without
    watching, so it is the one worth holding.
    """
    crater = constant("CraterRadiusCm")

    projectile = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                  / "CataclysmProjectile.h")
    found = re.search(
        r"static\s+constexpr\s+float\s+DefaultBodyRadiusCm\s*=\s*"
        r"(-?\d+(?:\.\d+)?)f\s*;", source(projectile))
    if found is None:
        pytest.fail("CataclysmProjectile.h no longer declares "
                    "DefaultBodyRadiusCm, so the rock's width is unknown.")

    rock = float(found.group(1))
    assert crater > rock, (
        f"the rip crater is {crater} cm wide and the rock that came out of it "
        f"is {rock} cm. A hole narrower than the thing that came out of it "
        f"reads as a different hole."
    )


def test_the_crater_reuses_the_debris_actor_rather_than_adding_another() -> None:
    """The decision issue #432 shares with #422: where short-lived meshes live.

    `ACataclysmDebrisBurst` was built generic in #422 -- it names no project
    content and knows nothing about rocks -- precisely so that the next thing
    wanting a mesh on the floor for a few seconds would not need its own actor.
    A crater is that mechanism with one piece and no spread.

    THE FAILURE THIS EXISTS FOR is a third actor appearing the next time
    somebody wants something on the ground, and then a fourth.
    """
    text = code(BRUTE_CPP)

    assert "ACataclysmDebrisBurst::Scatter" in text, (
        "the Brute no longer leaves its rip crater through "
        "ACataclysmDebrisBurst. That actor exists so that short-lived meshes on "
        "the floor have one home rather than one per effect."
    )

    for bespoke in ("ACataclysmCrater", "ACataclysmRipCrater"):
        assert bespoke not in source(BRUTE_CPP), (
            f"{bespoke} has appeared. A crater is ACataclysmDebrisBurst with a "
            f"single piece; a second actor for it is the duplication #422 built "
            f"the first one generic to avoid."
        )


def test_the_crater_is_given_a_material_rather_than_wearing_its_own() -> None:
    """Because its own is the engine's grey checkerboard.

    Measured 2026-08-08 and recorded in `game/docs/enemy-source-assets.md`:
    `SM_Rampage_Rock_Rip_Crater` carries `WorldGridMaterial`, exactly as the
    five fragments do. A crater spawned as it comes is a large checkered lump on
    the floor, which is worse than no crater at all.
    """
    text = code(BRUTE_CPP)

    crater_call = re.search(
        r"ACataclysmDebrisBurst::Scatter\(\s*this,\s*RipCraterLocation\(\),"
        r"[^;]*?;", text, re.DOTALL)
    if crater_call is None:
        pytest.fail(
            "the Brute no longer scatters anything at RipCraterLocation(), so "
            "nothing marks the ground where the rock came out.")

    assert "RockMaterial" in crater_call.group(0), (
        f"the rip crater is spawned without RockMaterial, so it wears the "
        f"{PLACEHOLDER_MATERIAL} the pack put on it -- a large grey checkered "
        f"lump on the floor."
    )

    assert "CraterSecondsOnTheGround" in crater_call.group(0), (
        "the rip crater is spawned without a lifetime of its own, so it takes "
        "ACataclysmDebrisBurst's default. That default is tuned for impact "
        "debris, and the invariant that a Brute can never have two craters "
        "depends on this specific number."
    )


def test_the_hole_appears_after_the_hands_have_dug_it() -> None:
    """Not at the start of the wind-up, which is the second of #432's decisions.

    THE ARITHMETIC IS NOT REPEATED HERE. Recomputing in Python what the C++
    computes is a guard that cannot fail -- issue #413 has that mistake written
    up. What this holds is that the moment is taken from the measurement and
    then divided by the play rate, both of which are things somebody could
    plausibly simplify away.
    """
    text = code(BRUTE_CPP)

    assert re.search(r"RipReachesGroundSeconds\s*/\s*FMath::Max\(\s*Rate",
                     text), (
        "the moment the crater appears is no longer divided by the montage's "
        "play rate. The rock throw is compressed by about 1.67, so waiting out "
        "the authored 0.2644 s would put the hole in the ground after the "
        "creature had already lifted the rock out of it."
    )

    measured = constant("RipReachesGroundSeconds")
    assert measured == pytest.approx(0.2644, abs=0.0001), (
        f"RipReachesGroundSeconds is {measured}. It is a measurement, not a "
        f"tuning knob: hand_r bottoms out at 0.2644 s in Ability_RipNToss_Rip. "
        f"If the clip changed, re-measure it and update "
        f"game/docs/enemy-source-assets.md in the same change."
    )


def test_the_measurement_stays_written_down() -> None:
    """The next person needs the numbers and the trap that goes with them."""
    text = source(ASSET_REFERENCE)

    assert "52.9" in text, (
        "game/docs/enemy-source-assets.md no longer records how far in front of "
        "the creature the rock comes out of the ground. Without it, "
        "CraterAheadCm looks like a number somebody picked."
    )

    assert "0.2644" in text, (
        "the document no longer records when the hands reach the ground, which "
        "is when the hole appears."
    )

    assert PLACEHOLDER_MATERIAL in text, (
        f"the document no longer records that the crater mesh carries "
        f"{PLACEHOLDER_MATERIAL}, so passing it a material looks like an "
        f"arbitrary complication."
    )

    # THE AXIS IS THE PART THAT COST A RUN. Forward in the animation's own
    # space is +Y, because the mesh takes a -90 degree yaw.
    assert "+Y" in text and "-90" in text, (
        "the document no longer records that forward in the animation's own "
        "space is +Y rather than +X. That is the trap "
        "tools/measure_animation_stride.py already fell into once, and the "
        "measurement above cannot be checked or repeated without it."
    )


def test_the_measured_distance_is_the_one_the_code_uses() -> None:
    """The document and the constant are two copies of one measurement.

    They drift. `sim/cataclysm_sim/scoring.py` drifted from its own source twice
    and CLAUDE.md carries a rule about it.
    """
    ahead = constant("CraterAheadCm")

    assert ahead == pytest.approx(52.9, abs=0.05), (
        f"CraterAheadCm is {ahead} and game/docs/enemy-source-assets.md records "
        f"the hands' midpoint at 52.9 cm in front of the creature. One of the "
        f"two moved without the other."
    )
