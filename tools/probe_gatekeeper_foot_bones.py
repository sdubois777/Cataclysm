"""Find out why the stride measurement reads nothing on the Sevarog rig.

    python tools/run_editor_python.py tools/probe_gatekeeper_foot_bones.py

WHAT WENT WRONG. `tools/measure_animation_stride.py` reported 0.0 cm/s for
`Walk_Fwd`, `Jog_Fwd` and `Run_Fwd`, and **14.7 cm/s for `Idle`**, which is the
control and must read zero. So the numbers are wrong in both directions and none
of them may be used.

It said the clips are on "Epic's standard rig, with inverse kinematics foot
bones", which means `ik_foot_l` and `ik_foot_r` appear in the animated tracks.
Appearing in the tracks and being DRIVEN are different things: a track that holds
one constant value all the way through is still a track.

WHAT THIS PRINTS. For each clip, which of the candidate foot bones it drives, and
how far each of those bones actually moves across the clip. A bone whose travel
is near zero is a bone the clip does not really animate, whatever the track list
says.

IT CHANGES NOTHING.
"""

import unreal

ANIMATIONS = "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations"

CLIPS = ["Walk_Fwd", "Jog_Fwd", "Run_Fwd", "Idle"]

#: Both rigs `measure_animation_stride.py` knows about, plus the plain root.
CANDIDATES = [
    "ik_foot_l", "ik_foot_r", "ik_foot_root",
    "foot_l", "foot_r",
    "calf_l", "calf_r", "thigh_l", "thigh_r",
    "pelvis", "root",
]

#: How many samples to take across a clip when measuring how far a bone moves.
SAMPLES = 60


def say(line):
    unreal.log("PROBE| " + str(line))


def driven_by(anim):
    reader = getattr(unreal.AnimationLibrary, "get_animation_track_names", None)
    if reader is None:
        return None
    try:
        return set(str(name) for name in reader(anim))
    except Exception:  # noqa: BLE001 -- the editor's own errors vary
        return None


def travel_of(anim, bone, length):
    """How far this bone moves across the clip, in centimetres.

    THE SPREAD RATHER THAN THE SUM, because a foot returns to where it started
    every stride: summing the per-frame steps of a cycle gives roughly zero and
    says nothing. The distance between the furthest-apart samples says whether
    the bone moves at all.
    """
    points = []
    for index in range(SAMPLES):
        time = length * index / float(SAMPLES - 1)
        try:
            transform = unreal.AnimationLibrary.get_bone_pose_for_time(
                anim, bone, time, True)
        except Exception:  # noqa: BLE001
            return None
        points.append(transform.translation)

    widest = 0.0
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            widest = max(widest, float((points[i] - points[j]).length()))
    return widest


def main():
    say("=== WHICH BONES EACH SEVAROG CLIP REALLY DRIVES ===")
    say("")
    say("A bone can be in the track list and hold one value all the way "
        "through.")
    say("`travel` is how far apart the two furthest samples of that bone are, "
        "in")
    say("centimetres. Near zero means the clip does not animate it.")

    for name in CLIPS:
        anim = unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, name))
        if anim is None:
            say("")
            say("{0}: NOT FOUND".format(name))
            continue

        length = float(anim.get_play_length())
        tracks = driven_by(anim)

        say("")
        say("{0}  ({1:.4f} s, {2} animated tracks)".format(
            name, length, len(tracks) if tracks is not None else "unknown"))

        for bone in CANDIDATES:
            in_tracks = tracks is not None and bone in tracks
            travel = travel_of(anim, bone, length)
            if travel is None:
                say("    {0:16} could not be read".format(bone))
                continue
            say("    {0:16} in tracks: {1:5}   travel {2:8.2f} cm".format(
                bone, "yes" if in_tracks else "no", travel))

    say("")
    say("=== PROBE FINISHED ===")


main()
