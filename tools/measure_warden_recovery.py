"""How far each of the Abyssal Warden's attack clips ends from its idle pose.

    python tools/run_editor_python.py tools/measure_warden_recovery.py

WHY THIS EXISTS. The project owner played the creature on 2026-08-09 and said
"he just kinda teleports back to the neutral position. I'm sure the animation
includes the back end of the swing that brings him back to center naturally
right?"

That is a claim about the art and it can be measured rather than argued. The
Grux pack ships `PrimaryAttack_LA` and `PrimaryAttack_LA_Recovery` as separate
clips, so the swing may well end mid-follow-through with the recovery being the
part that returns the creature to a neutral stance. If so, cutting from the end
of the swing straight to the idle is a jump, and playing the recovery in between
removes most of it.

WHAT IT MEASURES. The distance between a clip's LAST pose and the idle's FIRST
pose, bone by bone, in the mesh's own space. That distance is exactly the size
of the jump the player sees when the animation is switched.

    swing alone      -> the jump today
    recovery alone   -> the jump if the recovery is played after the swing

If the second is much smaller than the first, the owner is right and the fix is
to play the recovery. If they are similar, the jump is coming from somewhere
else and playing the recovery would not help.

WHAT IT CHANGES. Nothing. It evaluates poses and prints numbers.

WHY `AnimPoseExtensions` AND NOT `AnimationBlueprintLibrary`. The latter ships
with the Animation Modifiers plugin rather than with the engine's Python
bindings and is absent in this build; `tools/probe_brute_animation.py` found
that and `tools/compare_animation_clips.py` records it.
"""

import unreal

ANIMATIONS = "/Game/ParagonGrux/Characters/Heroes/Grux/Animations"

#: The clip a resting creature loops. Every distance below is measured to its
#: first pose, because that is the pose the switch jumps to.
IDLE = "Idle"

#: Each attack clip, and the recovery the pack ships beside it. The question is
#: whether the recovery ends closer to the idle than the swing does.
PAIRS = [
    ("PrimaryAttack_LA", "PrimaryAttack_LA_Recovery"),
    ("PrimaryAttack_RA", "PrimaryAttack_RA_Recovery"),
]

#: The roar has no recovery clip in the pack, so it is measured on its own to
#: say whether it needs one.
ALONE = ["Ultimate_Roar"]

#: Bones spread across the body rather than one. A follow-through is mostly in
#: the arms and the spine, and a test that watched only the pelvis would report
#: a swing as already neutral.
BONES = ["root", "pelvis", "spine_03", "hand_l", "hand_r", "foot_l", "foot_r",
         "head"]


def say(line):
    unreal.log("PROBE| " + str(line))


def load(name):
    return unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, name))


def pose_at(clip, time, options, space):
    """Every listed bone's position at one moment, as a dictionary."""
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, time, options)
    found = {}
    for bone in BONES:
        try:
            transform = unreal.AnimPoseExtensions.get_bone_pose(
                pose, bone, space)
            found[bone] = transform.translation
        except Exception:                                        # noqa: BLE001
            continue
    return found


def distance_to_idle(clip, idle_pose, options, space):
    """How far the clip's LAST pose is from the idle's first, worst bone."""
    # THE LAST POSE, NOT THE ONE AFTER IT. Asking for the play length exactly
    # can evaluate past the final key on some clips, so a hair inside it is
    # asked for instead.
    length = float(clip.get_play_length())
    ending = pose_at(clip, max(0.0, length - 0.001), options, space)

    worst = 0.0
    where = "no bone compared"
    for bone, position in ending.items():
        if bone not in idle_pose:
            continue
        gap = float((position - idle_pose[bone]).length())
        if gap > worst:
            worst = gap
            where = bone
    return worst, where


def main():
    options = unreal.AnimPoseEvaluationOptions()
    space = unreal.AnimPoseSpaces.WORLD

    idle = load(IDLE)
    if idle is None:
        say("Idle NOT FOUND, so there is nothing to measure against.")
        return

    idle_pose = pose_at(idle, 0.0, options, space)
    say("Idle's first pose read for {0} bones: {1}".format(
        len(idle_pose), sorted(idle_pose)))
    say("")

    say("=== HOW BIG IS THE JUMP TO THE IDLE, PER CLIP ===")
    say("  Smaller is a smaller jump for the player to see.")
    say("")

    for swing_name, recovery_name in PAIRS:
        for name in (swing_name, recovery_name):
            clip = load(name)
            if clip is None:
                say("  {0:32} NOT FOUND".format(name))
                continue
            gap, bone = distance_to_idle(clip, idle_pose, options, space)
            say("  {0:32} {1:>8.2f} cm   worst bone: {2}".format(
                name, gap, bone))
        say("")

    for name in ALONE:
        clip = load(name)
        if clip is None:
            say("  {0:32} NOT FOUND".format(name))
            continue
        gap, bone = distance_to_idle(clip, idle_pose, options, space)
        say("  {0:32} {1:>8.2f} cm   worst bone: {2}".format(name, gap, bone))

    say("")
    say("=== AND HOW WELL DOES A RECOVERY FOLLOW ITS SWING? ===")
    say("  A recovery that does not START where its swing ENDS would swap one "
        "jump for another.")
    say("")

    for swing_name, recovery_name in PAIRS:
        swing = load(swing_name)
        recovery = load(recovery_name)
        if swing is None or recovery is None:
            say("  {0} / {1}: one of them is missing".format(
                swing_name, recovery_name))
            continue

        length = float(swing.get_play_length())
        swing_end = pose_at(swing, max(0.0, length - 0.001), options, space)
        recovery_start = pose_at(recovery, 0.0, options, space)

        worst = 0.0
        where = "no bone compared"
        for bone, position in swing_end.items():
            if bone not in recovery_start:
                continue
            gap = float((position - recovery_start[bone]).length())
            if gap > worst:
                worst = gap
                where = bone

        say("  {0} -> {1}: {2:.2f} cm   worst bone: {3}".format(
            swing_name, recovery_name, worst, where))

    say("")
    say("=== A LEFT-RIGHT DOUBLE SWING: DOES EACH JOIN HOLD? ===")
    say("  Asked for by the project owner on 2026-08-09: 'if he has an attack "
        "for both arms include them both so he gives a good left right'.")
    say("  A join over about 5 cm is a visible jump in the middle of the "
        "combo, which is the thing being fixed rather than moved.")
    say("")

    JOINS = [
        ("PrimaryAttack_LA", "PrimaryAttack_RA"),
        ("PrimaryAttack_RA", "PrimaryAttack_LA"),
        ("PrimaryAttack_LA_Fast", "PrimaryAttack_RA_Fast"),
        ("PrimaryAttack_RA_Fast", "PrimaryAttack_RA_Recovery"),
        ("PrimaryAttack_LA_Fast", "PrimaryAttack_LA_Recovery"),
        ("PrimaryAttack_Start", "PrimaryAttack_LA"),
    ]

    for first_name, second_name in JOINS:
        first = load(first_name)
        second = load(second_name)
        if first is None or second is None:
            say("  {0:28} -> {1:28} one of them is missing".format(
                first_name, second_name))
            continue

        length = float(first.get_play_length())
        ending = pose_at(first, max(0.0, length - 0.001), options, space)
        starting = pose_at(second, 0.0, options, space)

        worst = 0.0
        where = "no bone compared"
        for bone, position in ending.items():
            if bone not in starting:
                continue
            gap = float((position - starting[bone]).length())
            if gap > worst:
                worst = gap
                where = bone

        say("  {0:28} -> {1:28} {2:>7.2f} cm   worst: {3}".format(
            first_name, second_name, worst, where))

    say("")
    say("=== THE TIMING BUDGET AGAINST A 2.4 SECOND ATTACK INTERVAL ===")
    say("")

    COMBOS = [
        ("one swing then its recovery",
         ["PrimaryAttack_LA", "PrimaryAttack_LA_Recovery"]),
        ("left, right, then a recovery",
         ["PrimaryAttack_LA", "PrimaryAttack_RA", "PrimaryAttack_RA_Recovery"]),
        ("left, right, no recovery",
         ["PrimaryAttack_LA", "PrimaryAttack_RA"]),
        ("the fast pair then a recovery",
         ["PrimaryAttack_LA_Fast", "PrimaryAttack_RA_Fast",
          "PrimaryAttack_RA_Recovery"]),
    ]

    interval = 2.4
    for label, names in COMBOS:
        total = 0.0
        missing = False
        for name in names:
            clip = load(name)
            if clip is None:
                missing = True
                break
            total += float(clip.get_play_length())
        if missing:
            say("  {0:34} a clip is missing".format(label))
            continue
        rate = max(1.0, total / interval)
        verdict = ("fits as authored, {0:.3f} s to spare".format(
            interval - total) if total <= interval
            else "needs play rate {0:.2f}".format(rate))
        say("  {0:34} {1:>6.3f} s   {2}".format(label, total, verdict))

    say("")
    say("=== PROBE FINISHED ===")


main()
