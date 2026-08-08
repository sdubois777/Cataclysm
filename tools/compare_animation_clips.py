"""Say whether two animation clips hold the same bone animation.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/compare_animation_clips.py

WHY THIS EXISTS. Issue #386. The Paragon Rampage pack ships `Sprint_Biped_Fwd`
beside `Jog_Biped_Fwd`, and the plan for making the Brute run while chasing was to
swap between them. Measured on 2026-08-07, the two return identical bone poses at
every time sampled, so swapping would change nothing on screen. The gait question
was answered another way -- the Brute drops onto all fours, landed in #390 -- but
the finding itself was never confirmed, and it is the reason nobody should reach
for a `Sprint_*` clip in this pack again.

WHY IT COULD NOT BE CONFIRMED UNTIL NOW. The original measurement used
`unreal.AnimationBlueprintLibrary.get_bone_pose_for_frame`. **That class is not in
this engine build**: it ships with the Animation Modifiers plugin rather than with
the engine's Python bindings, and `tools/probe_brute_animation.py` reports it
absent. So the finding stood on file sizes -- 29 bytes of difference in 176
kilobytes -- which is evidence and not proof, and this repository's own history
includes two silent drifts of copied numbers.

`unreal.AnimPoseExtensions` IS available and evaluates a clip without needing a
skeletal mesh component, which is what `tools/measure_animation_impact.py` already
uses to find when an attack strikes. This does the same thing for a different
question.

WHAT IT MEASURES. Every bone named below, at evenly spaced
times through both clips. Two clips of different lengths are sampled at the same
FRACTIONS of their own length rather than at the same absolute times, so a clip
that is the same movement played slower still compares equal -- which is the
interesting case, because a pack that realises a sprint by playing the jog faster
would produce exactly that.

WHAT IT CHANGES. Nothing. It only reads and reports.
"""

import unreal

ANIMATIONS = "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations"

#: The pairs to compare, as (name, name). Both names are looked up under
#: ANIMATIONS. A pair whose clips are missing is reported and skipped, because
#: the Paragon packs are gitignored and absent on a fresh clone.
PAIRS = [
    ("Jog_Biped_Fwd", "Sprint_Biped_Fwd"),
    ("Jog_Biped_Bwd", "Sprint_Biped_Bwd"),
    ("Jog_Biped_Lft", "Sprint_Biped_Lft"),
    ("Jog_Biped_Rt", "Sprint_Biped_Rt"),
    ("Jog_Quad_Fwd", "Sprint_Quad_Fwd"),
    ("Jog_Quad_Bwd", "Sprint_Quad_Bwd"),
    ("Jog_Quad_Lft", "Sprint_Quad_Lft"),
    ("Jog_Quad_Rt", "Sprint_Quad_Rt"),
    # THE CONTROL. These two are known to be different clips and must be
    # reported as different, or a run in which everything compares equal proves
    # nothing about the pairs above.
    ("Jog_Biped_Fwd", "Jog_Quad_Fwd"),
]

#: Bones sampled in each pose. The pelvis carries the body's travel and bounce;
#: the feet carry the gait itself. A sprint that differed from a jog would differ
#: in at least one of them.
BONES = ("pelvis", "foot_l", "foot_r")

#: Samples across each clip, at the same fractions of each one's length.
SAMPLES = 24

#: Centimetres. Two poses further apart than this on any axis are different.
#: Well under a footfall and well over any floating point difference.
SAME_WITHIN_CM = 0.5


def say(line):
    unreal.log("COMPARE| " + str(line))


def load(name):
    return unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, name))


def frame_count(clip):
    """How many frames a clip has, or "unknown".

    Read defensively: an editor property this build does not have raises,
    and losing the whole comparison over a line of extra detail would be a
    poor trade.
    """
    for name in ("number_of_sampled_frames", "number_of_frames"):
        try:
            return clip.get_editor_property(name)
        except Exception:  # noqa: BLE001 - the next name is tried
            continue
    return "unknown"


def pose_at(clip, fraction, bone, space, options):
    """One bone's position at a fraction of the way through a clip."""
    time = clip.get_play_length() * fraction
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, time, options)
    transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, space)
    return transform.translation


def largest_difference(first, second, space, options):
    """The biggest gap between the two clips on any bone at any sample.

    Returns (centimetres, description) so a difference can be reported with the
    bone and time it was found at rather than as a bare number.
    """
    worst = 0.0
    where = "no bone differed"
    sampled = 0

    for bone in BONES:
        for step in range(SAMPLES + 1):
            fraction = step / float(SAMPLES)
            try:
                a = pose_at(first, fraction, bone, space, options)
                b = pose_at(second, fraction, bone, space, options)
            except Exception as missing:  # noqa: BLE001 - reported, not raised
                say("    {0}: could not be sampled ({1})".format(bone, missing))
                break

            sampled += 1

            gap = max(abs(a.x - b.x), abs(a.y - b.y), abs(a.z - b.z))
            if gap > worst:
                worst = gap
                where = "{0} at {1:.0f}% of the clip".format(
                    bone, fraction * 100.0)

    return worst, where, sampled


def compare(first_name, second_name, space, options):
    first = load(first_name)
    second = load(second_name)

    say("")
    say("{0}  vs  {1}".format(first_name, second_name))

    if first is None or second is None:
        say("    NOT FOUND -- the Paragon Rampage pack is not installed")
        return

    say("    lengths: {0:.4f} s and {1:.4f} s".format(
        first.get_play_length(), second.get_play_length()))
    say("    frames:  {0} and {1}".format(
        frame_count(first), frame_count(second)))

    worst, where, sampled = largest_difference(first, second, space, options)

    # NO SAMPLES IS NOT AGREEMENT. A pair whose bones could not be read at
    # all would otherwise report a largest difference of zero and be called
    # the same animation, which is the failure this whole file exists to
    # avoid making twice.
    if sampled == 0:
        say("    VERDICT: CANNOT TELL -- no bone could be sampled")
        return

    say("    largest difference on any bone at any sample: "
        "{0:.4f} cm ({1}), from {2} samples".format(worst, where, sampled))
    say("    VERDICT: {0}".format(
        "THE SAME ANIMATION" if worst <= SAME_WITHIN_CM
        else "different animations"))


def main():
    say("==== comparing animation clips ====")
    say("Sampling {0} bones at {1} points through each clip.".format(
        len(BONES), SAMPLES + 1))
    say("Two clips count as the same when no bone differs by more than "
        "{0} cm.".format(SAME_WITHIN_CM))

    # LOCAL AND WORLD ARE THE ONLY TWO. Asked of the editor rather than
    # assumed: unreal.AnimPoseSpaces has no COMPONENT, and reaching for one
    # raises AttributeError and takes the whole run with it. WORLD is what
    # tools/measure_animation_impact.py already uses, and an evaluated pose
    # has no actor placed in a level, so it is the component space.
    space = unreal.AnimPoseSpaces.WORLD
    options = unreal.AnimPoseEvaluationOptions()

    for first_name, second_name in PAIRS:
        compare(first_name, second_name, space, options)

    say("")
    say("==== done ====")


main()
