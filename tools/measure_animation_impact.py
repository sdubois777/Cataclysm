"""Measure when an attack animation actually strikes, by evaluating its pose.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/measure_animation_impact.py

WHY THIS EXISTS. An attack animation has to line up with the moment the game
deals its damage, and nothing in the project knew when either Paragon clip
actually strikes. The Brute's ground smash montage was built assuming the
impact was the join between its two clips. It is not: the second clip begins
with the creature's fists still overhead, and they take about a fifth of a
second to reach the ground. Assuming otherwise put the damage a fifth of a
second away from the blow that was supposed to cause it, and forced a visible
freeze into the wind-up to cover the difference.

HOW IT MEASURES. The clips carry no animation notifies -- measured on
2026-08-08, all five of the Brute's ability clips have none -- so there is no
authored marker to read. Instead each clip is evaluated with
unreal.AnimPoseExtensions.get_anim_pose_at_time and the hands are followed
through it. For a downward blow the strike is the moment the hands stop
descending. For a throw the release is the moment the throwing hand reaches
the top of its arc.

WHAT IT CHANGES. Nothing. It only reads and reports.

A NOTE ON WHY NOT A SKELETAL MESH COMPONENT. The obvious approach is to spawn a
mesh, set the animation, and step it with SetPosition. That does not work:
USkeletalMeshComponent::SetPosition only sets the time on the single node
instance, and the function that would then evaluate the pose,
RefreshBoneTransforms, is not exposed to Python. AnimPoseExtensions evaluates
without a component at all.
"""

import unreal

ANIMATIONS = "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations"

#: The clips the Brute's two abilities are built from, in montage order.
CLIPS = [
    "Ability_GroundSmash_Start",
    "Ability_GroundSmash_End",
    "Ability_RipNToss_Rip",
    "Ability_RipNToss_Toss",
    # NEVER MEASURED UNTIL ISSUE #416. The pack ships a second throw clip, and
    # that issue lists using it as one of four options on the grounds that it
    # might release the rock sooner -- which would reduce how far the montage
    # has to be compressed to fit inside the telegraph. Measured now so the
    # option is answered with a number rather than left open.
    "Ability_RipNToss_Toss_Enraged",
]

#: Samples across each clip. The clips are authored at 30 frames a second, so
#: 90 samples is three times finer than the keys and nothing is missed.
SAMPLES = 90

#: Centimetres. A hand within this much of its lowest point in the clip counts
#: as having arrived, so the strike is the FIRST arrival rather than the very
#: lowest sample, which can come later while the pose settles.
ARRIVED_WITHIN_CM = 5.0


def say(line):
    unreal.log("IMPACT| " + str(line))


def sample(clip, bone, space, options):
    """(time, height) for the bone through the whole clip."""
    length = clip.get_play_length()
    out = []
    for step in range(SAMPLES + 1):
        time = length * step / float(SAMPLES)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            clip, time, options)
        transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, space)
        out.append((time, transform.translation.z))
    return out


def report(clip_name, clip, space, options):
    say("")
    say("{0}: length {1:.4f} s".format(clip_name, clip.get_play_length()))

    for bone in ("hand_l", "hand_r"):
        samples = sample(clip, bone, space, options)
        heights = [height for _, height in samples]
        lowest = min(heights)
        highest = max(heights)
        lowest_at = samples[heights.index(lowest)][0]
        highest_at = samples[heights.index(highest)][0]

        # The strike: the first sample that has arrived at the bottom.
        struck_at = None
        for time, height in samples:
            if height <= lowest + ARRIVED_WITHIN_CM:
                struck_at = time
                break

        say("  {0}: starts {1:.0f} cm, ends {2:.0f} cm".format(
            bone, heights[0], heights[-1]))
        say("  {0}: lowest {1:.0f} cm at {2:.3f} s, "
            "first within {3:.0f} cm of it at {4:.3f} s".format(
                bone, lowest, lowest_at, ARRIVED_WITHIN_CM, struck_at))
        say("  {0}: highest {1:.0f} cm at {2:.3f} s".format(
            bone, highest, highest_at))


def main():
    say("==== animation impact measurement ====")

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
