"""Measure the ground speed a locomotion animation was authored for.

Run inside the Unreal editor:

    python tools/run_editor_python.py <this file>

WHY. The Brute plays Jog_Biped_Fwd at a play rate derived from a constant that
was guessed, because the asset has no root motion to read a speed from. Guessing
produced visible foot sliding. This measures it instead.

HOW. In Epic's rig the ik_foot_l and ik_foot_r bones sit directly under root and
follow where the feet actually are. While a foot is planted it must travel
backwards relative to the character at exactly the speed the character moves
forwards, or the foot would slide. So: sample both feet every frame, take
whichever is lower (that is the planted one), and measure how fast it moves.

WHICH AXIS IS FORWARD IS NOT ASSUMED. The Rampage mesh needs a -90 degree yaw on
its component to face the way its actor faces, which means forward in the
animation's own space is not X. A first version of this script measured X, got a
median of -15 cm/s with symmetric extremes -- the signature of measuring a
side-to-side axis -- and reported a nonsense 38 cm/s jog. So every axis is
reported and the dominant one is chosen by size.

THE ESTIMATOR IS THE MEAN, AND THE SECOND VERSION OF THIS SCRIPT GOT THAT WRONG.
It averaged the top quartile of samples, reasoning that those were the frames
where a foot was genuinely planted. For Jog_Biped_Fwd that gave 373.7 cm/s. The
project owner then tuned the same animation by eye and found 225 cm/s correct --
the top quartile was 66% too high, because it measures the PEAK of the velocity
curve rather than a representative speed. The plain mean over the cycle gives
239.9 cm/s for the same animation, within 7% of the by-eye answer, so the mean is
what this uses now.

WHY THE MEAN IS RIGHT. Over one full gait cycle the tracked foot alternates
between stance, moving backwards at ground speed, and swing, moving forwards
quickly. Averaging the backwards speed of whichever foot is lower, across a whole
number of cycles, approximates the ground speed. Any estimator that favours the
extremes measures the swing instead.

WHAT THIS IS AND IS NOT. It is a starting estimate good to roughly ten percent,
not an exact answer. The IK foot bones do not touch the ground -- on Rampage they
stay 20 cm or more above it -- so "the lower foot" is an approximation of "the
planted foot". Judge the last of it by eye.
"""

import unreal

ANIMATIONS = [
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Biped_Fwd",
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Quad_Fwd",
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Run_Fwd",
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Idle_Biped",
    # The Abyssal Warden, added 2026-08-09 for issue #490. The project owner
    # reported the creature sliding rather than walking, and the play rate its
    # walk needs is this clip's authored speed divided by the designed 280 cm/s.
    # Idle is the control: standing still must read as zero and it is what shows
    # the method is measuring the right axis on this rig.
    "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Jog_Fwd",
    "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Run_Fwd",
    "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/TravelMode_Fwd",
    "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Idle",
    # The Hellhound, added 2026-08-20 for issue #39. It is played by
    # IggyScorch, which is a goblin riding a fire-breathing mount, so this is
    # the first rig measured here that is not one animal. Whether the IK foot
    # bones follow the mount's feet or the rider's is exactly what the idle
    # control below answers: if the method is measuring the wrong thing on
    # this rig, standing still will not read as zero.
    "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations/Jog_Fwd",
    "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations/Travelmode_Fwd",
    "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations/IggyScorch_Idle",
]

FOOT_BONES = ["ik_foot_l", "ik_foot_r"]
CHAIN = ["ik_foot_root", "root"]


def component_space(anim, bone, time):
    total = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, time, False)
    for parent in CHAIN:
        parent_pose = unreal.AnimationLibrary.get_bone_pose_for_time(
            anim, parent, time, False)
        total = total * parent_pose
    return total.translation


def cycle_mean(samples):
    """Mean backwards speed over the cycle. See the module docstring."""
    if not samples:
        return 0.0
    return sum(samples) / float(len(samples))


def measure(path):
    anim = unreal.load_asset(path)
    if anim is None:
        unreal.log_warning("could not load %s" % path)
        return

    length = unreal.AnimationLibrary.get_sequence_length(anim)
    frames = unreal.AnimationLibrary.get_num_frames(anim)
    if frames < 4:
        unreal.log_warning("%s has only %d frames" % (path, frames))
        return
    step = length / float(frames - 1)

    root_start = unreal.AnimationLibrary.get_bone_pose_for_time(
        anim, "root", 0.0, False).translation
    root_end = unreal.AnimationLibrary.get_bone_pose_for_time(
        anim, "root", length, False).translation
    root_travel = (root_end - root_start).length()

    # Signed speed along each axis, in both directions, so nothing is assumed.
    per_axis = {"+X": [], "-X": [], "+Y": [], "-Y": []}
    previous_planted = None

    for i in range(frames - 1):
        t0, t1 = i * step, (i + 1) * step
        now, nxt, height = {}, {}, {}
        for bone in FOOT_BONES:
            now[bone] = component_space(anim, bone, t0)
            nxt[bone] = component_space(anim, bone, t1)
            height[bone] = now[bone].z
        planted = min(FOOT_BONES, key=lambda b: height[b])

        # SKIP THE FRAME WHERE THE FEET SWAP. Right after the lower foot changes,
        # the pair of positions being differenced belongs to two different phases
        # and produces a spurious sample -- in Jog_Biped_Fwd, one of -53 cm/s
        # among values around +250. Only difference a foot against itself.
        if previous_planted is not None and previous_planted != planted:
            previous_planted = planted
            continue
        previous_planted = planted

        d = nxt[planted] - now[planted]
        per_axis["+X"].append(d.x / step)
        per_axis["-X"].append(-d.x / step)
        per_axis["+Y"].append(d.y / step)
        per_axis["-Y"].append(-d.y / step)

    results = dict((axis, cycle_mean(vals)) for axis, vals in per_axis.items())
    best = max(results, key=lambda a: results[a])

    unreal.log("")
    unreal.log("=== %s" % path.split("/")[-1])
    unreal.log("    length          %.3f s over %d frames" % (length, frames))
    unreal.log("    root travel     %.2f cm (0 means authored in place)" % root_travel)
    for axis in ("+X", "-X", "+Y", "-Y"):
        unreal.log("    planted along %s  %8.1f cm/s" % (axis, results[axis]))
    unreal.log("    DOMINANT AXIS   %s at %.1f cm/s  <-- authored ground speed"
               % (best, results[best]))


for asset in ANIMATIONS:
    try:
        measure(asset)
    except Exception as exc:  # noqa: BLE001 - report and continue to the next
        unreal.log_warning("%s failed: %s" % (asset, exc))

unreal.log("")
unreal.log("stride measurement finished")
