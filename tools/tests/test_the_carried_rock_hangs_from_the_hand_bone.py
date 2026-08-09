"""The rock the Brute carries hangs from a bone the animation moves.

WHY THIS EXISTS. Issue #421. The rock throw played `Ability_RipNToss_Rip`, an
animation whose whole content is tearing a rock out of the ground, with nothing in
the creature's hands. The rock then appeared in mid-air and flew off.

WHAT THE MEASUREMENT SETTLED. The issue expected where the rock sits in the hand
to be a judgement somebody had to make by eye. It is not. The Rampage mesh has no
sockets at all, so the rock attaches to a bone, and attaching with no offset puts
it where the animator put it. The figures are in
`game/docs/enemy-source-assets.md`.

AND THE MEASUREMENT WAS READ WRONG THE FIRST TIME. Issue #470. This file asserted
`weapon_r` until then, on the strength of a single sample taken 0.433 seconds into
`Ability_RipNToss_Toss`, at which the two candidate bones are about a metre apart.
`weapon_r` is the rig's PROP bone and the animator drives it as THE ROCK: it keeps
going after that sample and reaches 1253 cm from the creature, against 255 cm for
`hand_r`. The THROWN rock leaves the same bone, so it was being spawned 6.68
metres in front of the Brute and flying backwards to reach a nearer target. The
project owner reported it from play on 2026-08-09.

`tools/tests/test_rock_launch_bone.py` is the test that asks how far the bone
gets from the creature, which is the question this file could not ask.

WHAT THIS GUARDS. Continuous integration has no Paragon packs and no engine, so
nothing here can look at a skeleton. What it can hold is that the bone name in the
C++ is the measured one, that the measurement stays written down, and that the
rock is not attached to something that does not move with the hand.

The automation tests in `game/Source/Cataclysm/Tests/CataclysmCarriedRockTests.cpp`
check the live component, including that the rock leaves the hand when the throw
lands and when a stun cancels it.
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
ASSET_REFERENCE = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

#: The bone the rock hangs from, and the bone the thrown rock leaves. Measured
#: out of the asset on 2026-08-08 and corrected on 2026-08-09 by issue #470.
#: Written here as its own copy so that changing the C++ constant fails this
#: rather than moving both sides of a comparison together.
MEASURED_HAND_BONE = "hand_r"

#: The bone this used to name. Kept so the test below can say what is wrong when
#: somebody puts it back, rather than only that the name differs.
THE_PROP_BONE_THAT_FLIES_AWAY = "weapon_r"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def test_the_hand_bone_is_the_measured_one() -> None:
    """The bone was read out of the asset, so the C++ must name that one.

    `weapon_r` is the obvious guess and is wrong. It is the rig's prop bone, and
    through `Ability_RipNToss_Toss` the animator drives it as the rock rather
    than as the hand, so it leaves the creature entirely.
    """
    found = re.search(
        r"RockHoldBoneName\s*=\s*TEXT\(\"([^\"]+)\"\)", source(BRUTE_CPP))
    if found is None:
        pytest.fail(
            "CataclysmBruteCharacter.cpp no longer defines RockHoldBoneName. If "
            "it was renamed, rename it here; if it was deleted, the rock hangs "
            "from nothing in particular.")

    if found.group(1) == THE_PROP_BONE_THAT_FLIES_AWAY:
        pytest.fail(
            f"the rock is back on {THE_PROP_BONE_THAT_FLIES_AWAY!r}, which is "
            f"the state issue #470 was filed about. That bone is the rig's prop "
            f"bone and the throw animation takes it 1253 cm from the creature, "
            f"so the rock in the hand streaks away before the throw and the "
            f"thrown rock is spawned 6.68 metres in front of the Brute -- "
            f"behind any target nearer than that, which it then flies backwards "
            f"to reach. Use {MEASURED_HAND_BONE!r}. See "
            f"game/docs/enemy-source-assets.md and "
            f"tools/measure_rock_launch_point.py."
        )

    assert found.group(1) == MEASURED_HAND_BONE, (
        f"the carried rock hangs from {found.group(1)!r}. The bone measured out "
        f"of the Rampage skeleton is {MEASURED_HAND_BONE!r}. See "
        f"game/docs/enemy-source-assets.md."
    )


def test_the_rock_is_attached_to_the_animated_mesh() -> None:
    """Not to the capsule, where it would hang in the air beside the creature."""
    text = source(BRUTE_CPP)

    assert re.search(
        r"CarriedRock->SetupAttachment\(\s*GetMesh\(\)\s*,\s*RockHoldBoneName\s*\)",
        text), (
        "CataclysmBruteCharacter.cpp does not attach the carried rock to the "
        "skeletal mesh at the prop bone. Attached to the actor root instead, it "
        "would sit beside the creature and never move with the hand."
    )


def test_the_carried_rock_and_the_thrown_rock_are_one_asset() -> None:
    """Two paths would let the held rock and the flown rock become different.

    The projectile's mesh and the carried one both come from `RockMesh`, which is
    resolved once in `ResolveBody`.
    """
    text = source(BRUTE_CPP)

    assert re.search(r"CarriedRock->SetStaticMesh\(\s*RockMesh\s*\)", text), (
        "the carried rock is not set from RockMesh, which is the same asset the "
        "throw flies. If it is loaded separately, the rock in the hand and the "
        "rock in the air can become two different meshes."
    )


def test_the_rock_cannot_hit_anything_while_it_is_held() -> None:
    """What is thrown is a projectile with its own sweep.

    A colliding mesh in the creature's hand would be a second, differently sized
    way to hit somebody, and one that follows the hand through the whole
    animation.
    """
    text = source(BRUTE_CPP)

    assert re.search(
        r"CarriedRock->SetCollisionEnabled\(\s*ECollisionEnabled::NoCollision\s*\)",
        text), (
        "the carried rock has collision. It is held, not thrown -- the thrown "
        "thing is an ACataclysmProjectile with its own sweep."
    )


def test_the_visibility_is_asked_of_the_brain_rather_than_remembered() -> None:
    """A wind-up ends three ways and a remembered flag has to handle each.

    The attack landing, a stun cancelling it, and the pawn being unpossessed all
    clear the controller's WindingUpAbility. Asking it covers all three without
    this class knowing what any of them are; remembering would need three
    separate hooks and would leave a rock in the hand the first time one was
    missed.
    """
    text = source(BRUTE_CPP)

    body = re.search(
        r"void ACataclysmBruteCharacter::UpdateCarriedRock\(\).*?\n\}",
        text, re.S)
    if body is None:
        pytest.fail(
            "CataclysmBruteCharacter.cpp has no UpdateCarriedRock. If it was "
            "renamed, rename it here.")

    assert "WindingUpAbility" in body.group(0), (
        "UpdateCarriedRock does not ask the controller what is being wound up, "
        "so the rock's visibility is remembered rather than derived. Every way a "
        "wind-up can end then needs its own hook."
    )

    assert re.search(r"UpdateCarriedRock\(\);", text.split("void ACataclysmBruteCharacter::UpdateCarriedRock")[0]), (
        "nothing calls UpdateCarriedRock, so the rock never appears or "
        "disappears."
    )


def test_the_measurement_stays_written_down() -> None:
    """Including that the mesh has no sockets, which is why a bone is used.

    Somebody reaching for a socket would find none and might conclude the asset
    is unusable, rather than that a bone is the right answer.
    """
    text = source(ASSET_REFERENCE)

    assert "no sockets at all" in text, (
        "game/docs/enemy-source-assets.md no longer records that the Rampage "
        "mesh has no sockets. That is why the rock hangs from a bone, and "
        "without it the choice looks arbitrary."
    )

    assert MEASURED_HAND_BONE in text, (
        f"game/docs/enemy-source-assets.md no longer mentions "
        f"{MEASURED_HAND_BONE}, so the bone the rock hangs from is recorded "
        f"only in the C++."
    )

    assert "Ability_RipNToss_Toss" in text and "1253" in text, (
        "the figures showing how far weapon_r is animated away from the "
        "creature are no longer in game/docs/enemy-source-assets.md. They are "
        "the evidence that the prop bone is the WRONG one, and without them the "
        "next person will reach for it again, as issue #470 records happening."
    )

    assert "190.8" in text, (
        "game/docs/enemy-source-assets.md no longer carries the 0.433 second "
        "sample. It is worth keeping precisely because it is the misleading "
        "one: it is what the prop bone was chosen on, and the table only "
        "teaches anything if the reader can see the early sample beside the "
        "later ones."
    )
