"""The rock must leave a bone that stays with the creature.

WHY THIS EXISTS. Issue #470. The Brute's thrown rock is fired from whatever bone
`ACataclysmBruteCharacter::RockHoldBoneName` names, and that was `weapon_r` from
#460 until #470. `weapon_r` is the Rampage rig's PROP bone, and through the throw
clips the animator drives it as the ROCK rather than as the hand: it is flung up
to 1253 cm away from the creature. The rock was therefore spawned nearly seven
metres in front of the Brute and flew BACKWARDS to reach a target standing closer
than that. The project owner reported it from play as the rock shooting way
behind them and then redirecting back.

WHAT THE OLD TESTS CHECKED, AND WHY NONE OF THEM COULD CATCH IT.

    Cataclysm.Brute.TheCarriedRockHangsFromTheHandBone
        compared the bone name against a written copy of itself. It says the
        bone was chosen deliberately. It says nothing about where the bone goes.

    Cataclysm.Brute.ItLobsTheRockFromItsHandRatherThanItsWaist
        compared the launch point against the socket location of the same bone,
        which is the definition of that launch point and so cannot disagree. It
        also takes its no-art path on every machine, which is issue #466.

THIS ONE CHECKS THE MEASUREMENT. A launch point is only a launch point if it is
somewhere on the creature. The figures below were measured out of the animation
assets and are the thing that decides it, so they live here as data rather than
being re-derived by whoever next reads the code.

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
ASSET_REFERENCE = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

#: How far each candidate bone gets from the creature's own root, in
#: centimetres, at any point of `Ability_RipNToss_Toss`.
#:
#: MEASURED, NOT ASSUMED. `tools/measure_rock_launch_point.py` evaluates the
#: animation pose with `unreal.AnimPoseExtensions.get_anim_pose_at_time` and
#: follows both bones through the clip. Run 2026-08-09 against the Paragon
#: Rampage pack; the output is in `game/Saved/Logs/run_editor_python.log` and the
#: table is reproduced in `game/docs/enemy-source-assets.md`.
FURTHEST_FROM_THE_CREATURE_CM = {
    "hand_r": 255.2,
    "weapon_r": 1253.4,
}

#: The furthest from its own root a bone may be and still be somewhere a rock
#: could leave the creature's hand from.
#:
#: DERIVED FROM THE CREATURE, NOT PICKED. The Brute's capsule is 48 cm in radius
#: and 110 cm in half height, so it stands 220 cm tall and 96 cm across --
#: `BruteCapsuleRadius` and `BruteCapsuleHalfHeight` in
#: `game/Source/Cataclysm/Character/CataclysmBruteCharacter.h`. A bone further
#: from the root than the creature is tall is not on the creature, whatever it is
#: called. 300 cm allows a fully extended arm above the head on a creature of
#: that size and still refuses `weapon_r` by a factor of four.
FURTHEST_A_LAUNCH_POINT_MAY_BE_CM = 300.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def the_bone_the_rock_leaves_from() -> str:
    """What `ACataclysmBruteCharacter::RockHoldBoneName` is set to."""
    text = source(BRUTE_CPP)
    match = re.search(
        r"ACataclysmBruteCharacter::RockHoldBoneName\s*=\s*TEXT\(\"([^\"]*)\"\)",
        text)
    if match is None:
        pytest.fail(
            "CataclysmBruteCharacter.cpp has no "
            "'RockHoldBoneName = TEXT(\"...\")' line. If the constant was "
            "renamed, rename it here too. If it was deleted, nothing records "
            "where the rock leaves the creature from, and issue #470 is the "
            "account of what that costs."
        )
    return match.group(1)


def test_the_rock_leaves_a_bone_that_stays_with_the_creature() -> None:
    """The whole of issue #470 in one assertion.

    A bone that the animation throws across the arena is not a launch point. The
    rock spawns wherever it is at the moment of release, so the flight starts
    from there and everything downstream -- the direction, the ground speed, the
    range that is left to fly -- is measured from a place the creature is not.
    """
    bone = the_bone_the_rock_leaves_from()

    if bone not in FURTHEST_FROM_THE_CREATURE_CM:
        pytest.fail(
            f"the rock now leaves the bone {bone!r}, which has never been "
            f"measured. Run 'python tools/run_editor_python.py "
            f"tools/measure_rock_launch_point.py' from the repository root, add "
            f"the bone and its furthest distance from the root to "
            f"FURTHEST_FROM_THE_CREATURE_CM in this file, and then this test "
            f"can say whether it is a legal place to throw from. Measured bones "
            f"so far: {sorted(FURTHEST_FROM_THE_CREATURE_CM)}."
        )

    furthest = FURTHEST_FROM_THE_CREATURE_CM[bone]
    assert furthest <= FURTHEST_A_LAUNCH_POINT_MAY_BE_CM, (
        f"the rock leaves {bone!r}, which the throw animation takes "
        f"{furthest:.0f} cm from the creature's root -- past the "
        f"{FURTHEST_A_LAUNCH_POINT_MAY_BE_CM:.0f} cm a launch point may be, and "
        f"further than the creature is tall. The rock will be spawned that far "
        f"away and fly back towards its target. That is issue #470, and it is "
        f"what happens when a rig's prop bone is used as a hand: the animator is "
        f"moving the ROCK with it, not the hand."
    )


def test_the_measurement_is_recorded_where_somebody_would_look() -> None:
    """The figures above are only useful if the next person can find them.

    `game/docs/enemy-source-assets.md` is the file that records what was read out
    of the art packs. It carried the earlier, misleading measurement of these two
    bones -- a single sample at 0.433 seconds, before they had finished
    separating -- and that is what the wrong bone was chosen on.
    """
    text = source(ASSET_REFERENCE)

    for bone in FURTHEST_FROM_THE_CREATURE_CM:
        assert bone in text, (
            f"game/docs/enemy-source-assets.md does not mention {bone!r}, so "
            f"the measurement this test relies on is recorded nowhere a person "
            f"would look for it."
        )

    assert "measure_rock_launch_point.py" in text, (
        "game/docs/enemy-source-assets.md does not name "
        "tools/measure_rock_launch_point.py, so there is no way to find out how "
        "the bone distances in this file were arrived at or to re-run it."
    )


def test_the_probe_that_produced_the_measurement_still_exists() -> None:
    """A measurement nobody can repeat is an assertion.

    The numbers in this file came out of an editor run. If the script that
    produced them is deleted, they become folklore.
    """
    probe = REPO_ROOT / "tools" / "measure_rock_launch_point.py"
    assert probe.is_file(), (
        "tools/measure_rock_launch_point.py is gone. It is what measured the "
        "bone distances this file asserts against, and without it they cannot "
        "be checked or extended to another creature."
    )

    text = source(probe)
    assert "hand_r" in text and "weapon_r" in text, (
        "tools/measure_rock_launch_point.py no longer tracks both bones, so it "
        "can no longer produce the comparison this file rests on."
    )
