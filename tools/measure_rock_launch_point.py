"""Measure where the throwing hand is when the rock leaves it.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/measure_rock_launch_point.py

WHY THIS EXISTS. Issue #465 left one thing unmeasured. The rock is fired from
`ACataclysmBruteCharacter::RockLaunchLocation()`, which is the `weapon_r` bone,
and `ACataclysmProjectile::Fire` then builds the whole flight from the vector
between that point and where the shot was aimed. Every argument about what a
short throw looks like therefore rests on where that bone is, and nobody has
measured its FORWARD offset -- only its height, and only in the rest pose.

It matters most at close range. The project owner reported on 2026-08-09 that a
point-blank throw "shoots way behind me and then redirects back at me". If the
hand at release reaches further forward than the target is standing, the vector
from hand to target points BACKWARD, and the throw is launched from beyond the
player travelling towards them. This says whether that can happen and from what
distance.

WHAT IT MEASURES. `weapon_r` and `hand_r` through both rip-and-toss clips, in all
three axes rather than height alone, and how far apart the two get.

FORWARD IS +Y IN THIS SPACE, NOT +X. The Rampage mesh takes a -90 degree yaw on
its component so that it faces the way its actor faces, which is set in
`ACataclysmBruteCharacter::ResolveBody`. `game/docs/enemy-source-assets.md`
records the same trap, and records
`tools/measure_animation_stride.py` falling into it: a first version of that
script measured X, got extremes symmetric about zero -- which is the signature of
a side-to-side axis -- and reported a nonsense jog speed. Height is Z above the
creature's feet, because the mesh is dropped by the capsule half height.

WHAT IT CHANGES. Nothing. It only reads and reports.
"""

import unreal

ANIMATIONS = "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations"

#: The clips the rock throw is built from, in montage order, plus the ground
#: smash for comparison.
CLIPS = [
    "Ability_RipNToss_Toss",
    "Ability_RipNToss_Rip",
]

#: The bone the rock hangs from and is thrown from,
#: `ACataclysmBruteCharacter::RockHoldBoneName`, and the actual hand beside it
#: for comparison. A weapon-attachment bone is not obliged to stay with the
#: hand, and whether it does is the whole question here.
BONES = ("weapon_r", "hand_r")

#: Samples across each clip. The clips are authored at 30 frames a second, so
#: 90 samples is three times finer than the keys and nothing is missed.
SAMPLES = 90


def say(line):
    unreal.log("LAUNCH| " + str(line))


def track(clip, bone, space, options):
    """(time, x, y, z) for the bone through the whole clip."""
    length = clip.get_play_length()
    out = []
    for step in range(SAMPLES + 1):
        time = length * step / float(SAMPLES)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            clip, time, options)
        transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, space)
        t = transform.translation
        out.append((time, t.x, t.y, t.z))
    return out


def report(clip_name, clip, space, options):
    say("")
    say("{0}: length {1:.4f} s".format(clip_name, clip.get_play_length()))

    tracks = {}
    for bone in BONES:
        tracks[bone] = track(clip, bone, space, options)

    for bone in BONES:
        samples = tracks[bone]
        if not samples:
            say("  {0}: no samples".format(bone))
            continue

        distances = [(x * x + y * y + z * z) ** 0.5
                     for _, x, y, z in samples]
        furthest = max(distances)
        furthest_at = samples[distances.index(furthest)][0]

        say("  {0}: starts x {1:7.1f} y {2:7.1f} z {3:7.1f}".format(
            bone, samples[0][1], samples[0][2], samples[0][3]))
        say("  {0}: furthest from the root {1:.1f} cm at {2:.3f} s".format(
            bone, furthest, furthest_at))

    # HOW FAR APART THE TWO BONES GET. A weapon bone that stays with the hand
    # differs from it by the length of the grip and no more. One that flies off
    # along the thrown object's path does not, and using it as a launch point
    # then puts the projectile wherever the animator sent the rock.
    say("  gap between weapon_r and hand_r, every third sample:")
    weapon = tracks.get("weapon_r") or []
    hand = tracks.get("hand_r") or []
    worst = 0.0
    worst_at = 0.0
    for index in range(min(len(weapon), len(hand))):
        time, wx, wy, wz = weapon[index]
        _, hx, hy, hz = hand[index]
        gap = ((wx - hx) ** 2 + (wy - hy) ** 2 + (wz - hz) ** 2) ** 0.5
        if gap > worst:
            worst = gap
            worst_at = time
        if index % 3 == 0:
            say("    {0:6.3f} s  weapon_r x {1:8.1f} y {2:8.1f} z {3:8.1f}"
                "   hand_r x {4:7.1f} y {5:7.1f} z {6:7.1f}   gap {7:8.1f}"
                .format(time, wx, wy, wz, hx, hy, hz, gap))
    say("  WORST GAP {0:.1f} cm at {1:.3f} s".format(worst, worst_at))


def main():
    say("==== rock launch point measurement ====")
    say("FORWARD IS +Y, not +X: the Rampage mesh is yawed -90 degrees so that "
        "it faces the way its actor faces. Z is height above the feet. The "
        "Brute's capsule is 48 cm in radius and 110 cm in half height.")

    space = unreal.AnimPoseSpaces.WORLD
    options = unreal.AnimPoseEvaluationOptions()

    for clip_name in CLIPS:
        clip = unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, clip_name))
        if clip is None:
            say("{0}: NOT FOUND -- the Paragon Rampage pack is not "
                "installed".format(clip_name))
            continue
        report(clip_name, clip, space, options)

    say("")
    say("==== done ====")


main()
