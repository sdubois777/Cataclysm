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

NOT EVERY RIG HAS THE BONES THIS READS, AND UNTIL 2026-08-20 THAT FAILED
SILENTLY. `get_bone_pose_for_time` for a bone the skeleton does not have returns
an identity transform rather than raising, so a rig with no inverse kinematics
chain measured as 0.0 cm/s on every axis -- which is exactly what a creature
standing still measures. The Imp found it: `Minion_Lane_Core_Skeleton` animates
69 bones and not one of them is an `ik_` bone, so both of its walks and its idle
all read zero and the walks looked like idles.

So the rig is now CHOSEN rather than assumed, from RIGS below, by asking the clip
which bones it actually drives; and a clip matching no rig is refused out loud
instead of being reported as zero. A wrong number that looks like a real number
is worse than no number.
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
    # The Imp, added 2026-08-20 for issue #39. It is played by the melee lane
    # minion, whose pack ships a combat walk and a non-combat walk where every
    # other creature measured here has one, so both are read before either is
    # chosen. NonCombat_Idle is the control: standing still must read as zero.
    #
    # THE SPEED THIS REPORTS IS FOR THE MESH AT ITS AUTHORED SIZE, and the Imp
    # wears it scaled down. A scaled mesh's foot travels proportionally less far
    # per stride, so the figure below has to be multiplied by that scale before
    # a play rate is derived from it. The creature's own header does that
    # arithmetic and says so; this file reports the asset as authored.
    ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
     "/Animations/Melee/Combat_JogFwd"),
    ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
     "/Animations/Melee/Combat_JogFwd_AggroMinion"),
    ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
     "/Animations/Melee/NonCombat_JogFwd"),
    ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
     "/Animations/Melee/NonCombat_JogFwd_A"),
    ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
     "/Animations/Melee/NonCombat_JogFwd_B"),
    ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
     "/Animations/Melee/NonCombat_Idle"),
    # The Succubus, added 2026-08-20 for issue #39. It is played by the Countess,
    # a hero rig rather than a minion one, so which of the two rigs below it
    # lands on is not known in advance. `Idle_Relaxed` is the control: standing
    # still must read as zero, and if it does not then the figures beside it are
    # measuring the wrong axis or the wrong bones and none of them may be used.
    #
    # BOTH WALKS ARE READ BEFORE EITHER IS CHOSEN. `Jog_Fwd` is what every other
    # creature here uses and `Jog_Fwd_Combat` is the pack's fighting stance
    # version of it, which for a caster that walks to eight metres and stops may
    # be the better read.
    "/Game/ParagonCountess/Characters/Heroes/Countess/Animations/Jog_Fwd",
    ("/Game/ParagonCountess/Characters/Heroes/Countess"
     "/Animations/Jog_Fwd_Combat"),
    "/Game/ParagonCountess/Characters/Heroes/Countess/Animations/Sprint_Fwd",
    ("/Game/ParagonCountess/Characters/Heroes/Countess"
     "/Animations/Idle_Relaxed"),
    # The Gatekeeper, added 2026-08-20 for issue #759. It is played by
    # Sevarog, the tallest creature in the project at 3.11 metres, and its
    # designed speed of 3.0 m/s is SLOW for a creature that size -- which is
    # why the walk is read as well as the jog. `Jog_Fwd` measures 9.0000
    # seconds against `Walk_Fwd` and `Run_Fwd` at 1.6000, so it is not an
    # ordinary gait cycle and the number below says what it really is.
    # `Idle` is the control: standing still must read as zero.
    "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Walk_Fwd",
    "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Jog_Fwd",
    "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Run_Fwd",
    "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/Idle",
]

#: The rigs this knows how to read, and which bones each keeps its feet in.
#:
#: EACH FOOT CARRIES ITS OWN PARENT CHAIN, because a leg's bones are sided. The
#: chain runs from the foot's immediate parent up to the root, and every bone in
#: it has to be listed: a missing parent that rotates during the stride corrupts
#: the foot's position without failing.
#:
#: THE FIRST RIG WHOSE BONES ARE ALL PRESENT WINS, so the more specific one has
#: to come first if two ever overlap. They do not today.
RIGS = [
    {
        "name": "Epic's standard rig, with inverse kinematics foot bones",
        "feet": {
            "ik_foot_l": ["ik_foot_root", "root"],
            "ik_foot_r": ["ik_foot_root", "root"],
        },
    },
    {
        # The Paragon lane minions. 69 animated bones, none of them an `ik_`
        # bone, so the foot is tracked through the leg it hangs off:
        # pelvis, thigh, calf, foot. `ball_l` and `ball_r` hang below the feet
        # and `kneecap_l` and `calf_twist_01_l` are siblings rather than parents.
        "name": "a rig with no inverse kinematics bones, tracked through the leg",
        "feet": {
            "foot_l": ["calf_l", "thigh_l", "pelvis", "root"],
            "foot_r": ["calf_r", "thigh_r", "pelvis", "root"],
        },
    },
]


def bones_driven_by(anim):
    """Every bone the clip animates, or None when that cannot be read."""
    reader = getattr(unreal.AnimationLibrary, "get_animation_track_names", None)
    if reader is None:
        return None
    try:
        return set(str(name) for name in reader(anim))
    except Exception:  # noqa: BLE001 -- the editor's own errors vary
        return None


def rig_for(anim):
    """Which rig this clip is on, or None when it is on none of them.

    RETURNING None IS THE POINT. Before this existed, a clip on an unknown rig
    was measured through bones it does not have and reported 0.0 cm/s, which is
    indistinguishable from a creature standing still.
    """
    driven = bones_driven_by(anim)
    if driven is None:
        # The engine would not say which bones are animated. Fall back to the
        # standard rig, which is what every clip measured before 2026-08-20 used,
        # rather than refusing everything.
        return RIGS[0]

    for rig in RIGS:
        wanted = set(rig["feet"])
        for chain in rig["feet"].values():
            wanted.update(chain)
        if wanted <= driven:
            return rig
    return None


def component_space(anim, bone, chain, time):
    total = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, time, False)
    for parent in chain:
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

    rig = rig_for(anim)
    if rig is None:
        unreal.log_warning(
            "%s is on a rig this does not know how to read, so NOTHING WAS "
            "MEASURED for it. Add its foot bones and their parent chains to "
            "RIGS. Reporting no figure rather than the 0.0 cm/s it would "
            "otherwise print, which reads as a creature standing still."
            % path.split("/")[-1])
        return
    foot_bones = sorted(rig["feet"])

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
        for bone in foot_bones:
            chain = rig["feet"][bone]
            now[bone] = component_space(anim, bone, chain, t0)
            nxt[bone] = component_space(anim, bone, chain, t1)
            height[bone] = now[bone].z
        planted = min(foot_bones, key=lambda b: height[b])

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
    unreal.log("    rig             %s" % rig["name"])
    unreal.log("    tracked feet    %s" % ", ".join(foot_bones))
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
