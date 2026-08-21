"""Find out what each basic-attack clip carries before measuring anything with it.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/probe_attack_impact_sources.py

WHY THIS EXISTS RATHER THAN A MEASUREMENT. Issue #526 asks when inside each
enemy's basic-attack clip the damage lands, and says to read the animation
notifies where there are any and to fall back on inspecting the pose where there
are not. Which of those applies is different per pack and **has not been checked
for six of the seven creatures**.

IT ALSO CARRIES ITS OWN CONTROL, and that is the point. `measure_animation_stride.py`
followed `ik_foot_l` because that bone appeared in the animated track list, and
on the Sevarog rig it appears and is never driven -- so the tool reported 0.0
cm/s for three walking clips and 14.7 cm/s for the idle, which is the control and
must read zero. Issue #778. **Appearing in the track list and being driven are
different things**, so this reports how far every candidate bone actually travels
and refuses to nominate one that does not move.

WHAT IT CHANGES. Nothing. It only reads and reports.

WHY NOT A SKELETAL MESH COMPONENT. `USkeletalMeshComponent::SetPosition` only
sets the time on the single node instance, and `RefreshBoneTransforms`, which
would then evaluate the pose, is not exposed to Python.
`AnimPoseExtensions.get_anim_pose_at_time` evaluates without a component at all.
The same reasoning is in `tools/measure_animation_impact.py`.
"""

import unreal

#: Every clip a creature plays as its ORDINARY attack, with the constant in the
#: C++ that names it. Read out of the character classes rather than out of issue
#: #526, whose table is stale in two places: it lists the Abyssal Warden's clip
#: as `PrimaryAttack_LA` when the creature plays `PrimaryAttack_LA_Fast` and
#: `PrimaryAttack_RA_Fast`, and it lists one clip each for three creatures that
#: alternate between several.
#:
#: THIRTEEN CLIPS FOR SEVEN CREATURES. The Imp draws from five, the Corrupted
#: Sentinel alternates two, and the Abyssal Warden swings left and right. Each
#: has its own damage moment and none of them can be assumed to match another.
CLIPS = [
    # (creature, clip path, the C++ constant that names it)
    ("Brute",
     "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
     "Attack_Biped_Melee_A",
     "ACataclysmBruteCharacter::AttackAnimationPath"),

    ("Imp",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_A_SetA",
     "ACataclysmImpCharacter::AttackAnimationNames[0]"),
    ("Imp",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_B_SetA",
     "ACataclysmImpCharacter::AttackAnimationNames[1]"),
    ("Imp",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_C_SetA",
     "ACataclysmImpCharacter::AttackAnimationNames[2]"),
    ("Imp",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_D_SetA",
     "ACataclysmImpCharacter::AttackAnimationNames[3]"),
    ("Imp",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_E_SetA",
     "ACataclysmImpCharacter::AttackAnimationNames[4]"),

    ("Hellhound",
     "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations/"
     "Scorch_Primary_Fire_Med",
     "ACataclysmHellhoundCharacter::MaulAnimationPath"),

    ("Abyssal Warden",
     "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
     "PrimaryAttack_LA_Fast",
     "ACataclysmAbyssalWardenCharacter::LeftSwingAnimationPath"),
    ("Abyssal Warden",
     "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
     "PrimaryAttack_RA_Fast",
     "ACataclysmAbyssalWardenCharacter::RightSwingAnimationPath"),

    ("Corrupted Sentinel",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege/"
     "Fire_Planted",
     "ACataclysmCorruptedSentinelCharacter::FireAnimationNames[0]"),
    ("Corrupted Sentinel",
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege/"
     "Fire_Planted_B",
     "ACataclysmCorruptedSentinelCharacter::FireAnimationNames[1]"),

    ("Succubus",
     "/Game/ParagonCountess/Characters/Heroes/Countess/Animations/"
     "Primary_Attack_Normal",
     "ACataclysmSuccubusCharacter::AttackAnimationName"),

    ("Gatekeeper",
     "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/"
     "Swing1_Medium",
     "ACataclysmGatekeeperCharacter::CleaveAnimationName"),
]

#: Bone names worth following for a strike or a release, matched as substrings
#: of the lowercased bone name. Deliberately wide: which of these a rig actually
#: drives is what this probe is for.
CANDIDATE_PARTS = (
    "hand", "weapon", "wep", "claw", "blade", "sword", "hammer", "axe",
    "muzzle", "fire", "ball", "socket", "jaw", "head", "index_03",
)

#: Samples across each clip. Paragon clips are authored at 30 frames a second,
#: so 90 is three times finer than the keys and nothing between them is missed.
SAMPLES = 90

#: A bone that moves less than this across a whole clip is not being driven in
#: any way that could mark a strike. Two centimetres is well under the smallest
#: real swing and well over floating point noise.
MOVED_AT_ALL_CM = 2.0


def say(line):
    unreal.log("PROBE| " + str(line))


def bone_names_of(pose):
    try:
        return [str(name) for name in
                unreal.AnimPoseExtensions.get_bone_names(pose)]
    except Exception as problem:            # noqa: BLE001 - reported, not raised
        say("    could not list bone names: {0}".format(problem))
        return []


def notify_events(clip):
    """The clip's notify events, and how they were obtained.

    **`clip.get_editor_property("notifies")` DOES NOT WORK.** Measured on
    2026-08-21 across all thirteen clips: `UAnimSequenceBase::Notifies` is
    protected, and Python answers "Property 'Notifies' for attribute 'notifies'
    on 'AnimSequence' is protected and cannot be read" every time.
    `UAnimationBlueprintLibrary`, exposed as `unreal.AnimationLibrary`, is the
    supported route and is tried first. The protected property is still tried
    last, so that if a future engine version opens it this keeps working.
    """
    reader = getattr(unreal.AnimationLibrary, "get_animation_notify_events",
                     None)
    if reader is not None:
        try:
            return list(reader(clip)), "unreal.AnimationLibrary"
        except Exception as problem:        # noqa: BLE001
            say("    AnimationLibrary.get_animation_notify_events failed: "
                "{0}".format(problem))

    try:
        return list(clip.get_editor_property("notifies")), "the notifies property"
    except Exception as problem:            # noqa: BLE001
        return None, "no route worked: {0}".format(problem)


def notify_track_names(clip):
    """The names of the notify tracks, which exist even when empty."""
    reader = getattr(unreal.AnimationLibrary,
                     "get_animation_notify_track_names", None)
    if reader is None:
        return None
    try:
        return [str(name) for name in reader(clip)]
    except Exception:                       # noqa: BLE001
        return None


def notifies_of(clip):
    """Every animation notify on the clip, as (time, description).

    DEFENSIVE ABOUT THE PROPERTY NAMES. `FAnimNotifyEvent` exposes its trigger
    time under different names across engine versions, so each candidate is
    tried and whatever is found is reported rather than assumed.
    """
    events, how = notify_events(clip)
    if events is None:
        return [(-1.0, how)]

    say("    notifies read through {0}".format(how))
    tracks = notify_track_names(clip)
    if tracks is not None:
        say("    notify tracks on the clip: {0}".format(
            tracks if tracks else "none"))

    found = []
    for event in events:
        when = None
        for field in ("trigger_time_offset", "link_value", "display_time",
                      "time"):
            try:
                value = event.get_editor_property(field)
            except Exception:               # noqa: BLE001
                continue
            if isinstance(value, float):
                when = value
                break
        found.append((when if when is not None else -1.0, str(event)))
    return found


def travel_of(clip, bone, options):
    """Total path length and the extremes, for one bone through one clip."""
    length = clip.get_play_length()
    points = []
    for step in range(SAMPLES + 1):
        time = length * step / float(SAMPLES)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            clip, time, options)
        transform = unreal.AnimPoseExtensions.get_bone_pose(
            pose, bone, unreal.AnimPoseSpaces.WORLD)
        points.append((time, transform.translation))

    distance = 0.0
    for before, after in zip(points, points[1:], strict=False):
        distance += (after[1] - before[1]).length()

    def extreme(pick, axis):
        best = pick(points, key=lambda point: getattr(point[1], axis))
        return getattr(best[1], axis), best[0]

    return {
        "travel": distance,
        "max_x": extreme(max, "x"), "min_x": extreme(min, "x"),
        "max_y": extreme(max, "y"), "min_y": extreme(min, "y"),
        "max_z": extreme(max, "z"), "min_z": extreme(min, "z"),
    }


def report(creature, path, constant, options):
    say("")
    say("=" * 70)
    clip = unreal.load_asset("{0}.{1}".format(path, path.rsplit("/", 1)[1]))
    if clip is None:
        say("{0}: {1} NOT FOUND -- its Paragon pack is not installed".format(
            creature, path))
        return

    say("{0}: {1}".format(creature, path.rsplit("/", 1)[1]))
    say("  named by {0}".format(constant))
    say("  length {0:.4f} s".format(clip.get_play_length()))

    # --- what the pack authored ------------------------------------------
    events = notifies_of(clip)
    if not events:
        say("  NOTIFIES: none. The damage moment has to be inspected from the "
            "pose.")
    else:
        say("  NOTIFIES: {0}".format(len(events)))
        for when, description in events:
            say("    at {0:.4f} s: {1}".format(when, description))

    # --- which bones this rig actually drives ----------------------------
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, 0.0, options)
    names = bone_names_of(pose)
    say("  bones on the skeleton: {0}".format(len(names)))

    candidates = [name for name in names
                  if any(part in name.lower() for part in CANDIDATE_PARTS)]
    if not candidates:
        say("  NO CANDIDATE BONE NAMES AT ALL. The names on this rig are "
            "unlike the others; first twenty are {0}".format(names[:20]))
        return

    say("  candidate bones: {0}".format(len(candidates)))
    moved = []
    for bone in candidates:
        try:
            measured = travel_of(clip, bone, options)
        except Exception as problem:        # noqa: BLE001
            say("    {0}: could not sample: {1}".format(bone, problem))
            continue
        if measured["travel"] < MOVED_AT_ALL_CM:
            continue
        moved.append((measured["travel"], bone, measured))

    if not moved:
        say("  **NO CANDIDATE BONE MOVES MORE THAN {0:.0f} cm**, so none of "
            "them can mark a strike on this clip. That is the Sevarog failure "
            "in issue #778 happening again and the clip needs looking at by "
            "hand.".format(MOVED_AT_ALL_CM))
        return

    moved.sort(reverse=True)
    say("  bones that actually move, furthest first:")
    for distance, bone, measured in moved[:8]:
        say("    {0}: travels {1:.1f} cm".format(bone, distance))
        say("      X from {0:.1f} at {1:.3f}s to {2:.1f} at {3:.3f}s".format(
            measured["min_x"][0], measured["min_x"][1],
            measured["max_x"][0], measured["max_x"][1]))
        say("      Y from {0:.1f} at {1:.3f}s to {2:.1f} at {3:.3f}s".format(
            measured["min_y"][0], measured["min_y"][1],
            measured["max_y"][0], measured["max_y"][1]))
        say("      Z from {0:.1f} at {1:.3f}s to {2:.1f} at {3:.3f}s".format(
            measured["min_z"][0], measured["min_z"][1],
            measured["max_z"][0], measured["max_z"][1]))


def main():
    say("==== what each basic-attack clip carries ====")
    say("Issue #526. Reports notifies and which bones are really driven.")
    say("It measures nothing yet and decides nothing.")

    options = unreal.AnimPoseEvaluationOptions()

    for creature, path, constant in CLIPS:
        try:
            report(creature, path, constant, options)
        except Exception as problem:        # noqa: BLE001
            say("{0}: {1} FAILED: {2}".format(creature, path, problem))

    say("")
    say("==== done ====")


main()
