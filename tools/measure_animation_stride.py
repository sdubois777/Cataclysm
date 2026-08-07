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
"""

import unreal

ANIMATIONS = [
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Biped_Fwd",
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Jog_Quad_Fwd",
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Run_Fwd",
    "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/Idle_Biped",
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


def robust_speed(samples):
    """Mean of the top quartile: the part of the cycle where a foot is planted."""
    if not samples:
        return 0.0
    ordered = sorted(samples)
    upper = ordered[int(len(ordered) * 0.75):]
    return sum(upper) / float(len(upper)) if upper else 0.0


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

    for i in range(frames - 1):
        t0, t1 = i * step, (i + 1) * step
        now, nxt, height = {}, {}, {}
        for bone in FOOT_BONES:
            now[bone] = component_space(anim, bone, t0)
            nxt[bone] = component_space(anim, bone, t1)
            height[bone] = now[bone].z
        planted = min(FOOT_BONES, key=lambda b: height[b])
        d = nxt[planted] - now[planted]
        per_axis["+X"].append(d.x / step)
        per_axis["-X"].append(-d.x / step)
        per_axis["+Y"].append(d.y / step)
        per_axis["-Y"].append(-d.y / step)

    results = dict((axis, robust_speed(vals)) for axis, vals in per_axis.items())
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
