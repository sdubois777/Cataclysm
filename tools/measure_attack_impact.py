"""Measure when each enemy's ordinary attack actually strikes, from its pose.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/measure_attack_impact.py

WHY IT MEASURES FROM THE POSE RATHER THAN FROM A NOTIFY. Issue #526 asks for the
authored notify where there is one. **There is not one anywhere.**
`tools/probe_attack_impact_sources.py` read all thirteen clips through
`unreal.AnimationLibrary.get_animation_notify_events` on 2026-08-21 and every one
came back empty, each with a single empty notify track named "1". The Paragon
packs were authored for a different game and carry no damage markers, so every
figure below comes from inspection.

    `clip.get_editor_property("notifies")` DOES NOT WORK AT ALL:
    `UAnimSequenceBase::Notifies` is protected and Python refuses it.
    `unreal.AnimationLibrary` is the route that works.

THREE RULES, NOT ONE, AND THEIR AGREEMENT IS THE EVIDENCE. Picking a single
definition of "the moment it strikes" and trusting it is how
`measure_animation_stride.py` came to report 0.0 cm/s for three walking clips --
it followed one bone that looked right and was never driven. Issue #778. So this
computes three independent answers for every clip:

    peak speed        the sample where the striking bone is moving fastest.
                      In a swing that is the contact point; in a throw it is
                      the release. This is what an animator means by the
                      contact frame.
    furthest reach    the sample where the bone is furthest from the pelvis.
                      Full extension of the blow.
    lowest point      the sample where the bone is nearest the ground. Only
                      meaningful for a downward blow, and reported for every
                      clip so it can be seen when it is not.

WHERE THE THREE AGREE the answer is solid. **WHERE THEY DISAGREE THIS SAYS SO
RATHER THAN CHOOSING**, and that clip needs an eye on it.

AND IT CARRIES A CONTROL. The striking bone is chosen as the candidate that
travels furthest, and a clip whose best candidate barely moves is refused rather
than measured, because a bone that is not driven cannot mark a strike.

WHAT IT CHANGES. Nothing. It only reads and reports.
"""

import unreal

#: Every clip a creature plays as its ORDINARY attack, and the creature's
#: designed attack interval in seconds.
#:
#: THE INTERVALS ARE COPIES OF `ARCHETYPES` in sim/cataclysm_sim/enemy_stats.py,
#: which is authoritative. They are repeated here because this script runs
#: inside the editor's interpreter and cannot import the simulation package.
#: `tools/tests/test_attack_impact_fits_the_telegraph.py` holds them to the
#: model, so a drift fails there rather than being reported wrongly here.
#:
#: THIRTEEN CLIPS FOR SEVEN CREATURES. The Imp draws from five, the Corrupted
#: Sentinel alternates two, and the Abyssal Warden swings left and right.
CLIPS = [
    # (creature, attack interval, clip path)
    ("Brute", 1.2,
     "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
     "Attack_Biped_Melee_A"),

    ("Imp", 0.9,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_A_SetA"),
    ("Imp", 0.9,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_B_SetA"),
    ("Imp", 0.9,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_C_SetA"),
    ("Imp", 0.9,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_D_SetA"),
    ("Imp", 0.9,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/"
     "Attack_E_SetA"),

    ("Hellhound", 1.1,
     "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations/"
     "Scorch_Primary_Fire_Med"),

    ("Abyssal Warden", 2.4,
     "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
     "PrimaryAttack_LA_Fast"),
    ("Abyssal Warden", 2.4,
     "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/"
     "PrimaryAttack_RA_Fast"),

    ("Corrupted Sentinel", 2.0,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege/"
     "Fire_Planted"),
    ("Corrupted Sentinel", 2.0,
     "/Game/ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege/"
     "Fire_Planted_B"),

    ("Succubus", 2.6,
     "/Game/ParagonCountess/Characters/Heroes/Countess/Animations/"
     "Primary_Attack_Normal"),

    ("Gatekeeper", 3.0,
     "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/"
     "Swing1_Medium"),
]

#: Bones that can be the striking part, matched as substrings of the lowercased
#: bone name. Which of these a rig drives is measured rather than assumed.
#:
#: INVERSE-KINEMATICS AND VIRTUAL BONES ARE EXCLUDED, AND ANYWHERE IN THE NAME
#: RATHER THAN ONLY AT THE FRONT. Such a bone follows a driven bone instead of
#: being driven, so at best it duplicates an answer already present and at worst
#: it is never driven at all -- which is issue #778, where `ik_foot_l` on the
#: Sevarog rig is present and never moves.
#:
#: **THE FIRST VERSION OF THIS LIST REFUSED ONLY A NAME STARTING WITH `ik_`, AND
#: THAT WAS NOT ENOUGH.** On the Hellhound it then chose `Goblin_ik_hand_gun`:
#: an inverse-kinematics bone belonging to the GOBLIN RIDER rather than to the
#: creature. The pack is one mesh for two creatures, which is issue #756.
CANDIDATE_PARTS = ("hand_", "weapon", "claw", "blade", "sword", "hammer",
                   "axe", "muzzle")

#: Refused wherever it appears in the bone name, not only at the start.
EXCLUDED_ANYWHERE = ("ik_", "vb ", "virtual")

#: Bones belonging to something that is not the creature. The Hellhound's pack
#: rigs the goblin rider into the same skeleton, so every `Goblin_` bone is the
#: rider's arm rather than the hound's jaw. Issue #756.
EXCLUDED_OWNERS = ("goblin_",)

#: Followed and reported on every clip whatever else is chosen. For a RANGED
#: attack the shot leaves the hand, and "the bone that travels furthest" is the
#: wrong selector for that: on the Succubus it chose a whipping tail bone over
#: the hand.
ALWAYS_REPORT = ("hand_l", "hand_r")

#: The bone every distance is measured from. Present on all five rigs; checked
#: rather than assumed, and the clip is refused if it is missing.
ROOT_BONE_CANDIDATES = ("pelvis", "root", "spine_01")

#: Samples across each clip. Paragon clips are authored at 30 frames a second,
#: so 120 is four times finer than the keys.
SAMPLES = 120

#: A bone that moves less than this across a whole clip is not being driven in
#: any way that could mark a strike.
MOVED_AT_ALL_CM = 5.0

#: How close two of the three rules have to land to count as agreeing, as a
#: fraction of the clip's own length. A twentieth of a clip is under two frames
#: at 30 frames a second.
AGREEMENT_FRACTION = 0.05


def say(line):
    unreal.log("IMPACT| " + str(line))


def positions(clip, bone, options):
    """(time, translation) for one bone across the whole clip."""
    length = clip.get_play_length()
    out = []
    for step in range(SAMPLES + 1):
        time = length * step / float(SAMPLES)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            clip, time, options)
        transform = unreal.AnimPoseExtensions.get_bone_pose(
            pose, bone, unreal.AnimPoseSpaces.WORLD)
        out.append((time, transform.translation))
    return out


def path_length(samples):
    total = 0.0
    for before, after in zip(samples, samples[1:], strict=False):
        total += (after[1] - before[1]).length()
    return total


def striking_bone(clip, names, options):
    """The candidate bone that travels furthest, and how far. None if none moves.

    THE CONTROL. A bone that does not move cannot mark a strike, and a rig where
    nothing moves is a rig this cannot read -- which is what happened to the
    stride measurement on Sevarog. Refusing is the right answer there; reporting
    a number is not.
    """
    best_name, best_samples, best_travel = None, None, 0.0
    for name in names:
        lowered = name.lower()
        if not any(part in lowered for part in CANDIDATE_PARTS):
            continue
        if any(part in lowered for part in EXCLUDED_ANYWHERE):
            continue
        if any(lowered.startswith(owner) for owner in EXCLUDED_OWNERS):
            continue
        samples = positions(clip, name, options)
        travel = path_length(samples)
        if travel > best_travel:
            best_name, best_samples, best_travel = name, samples, travel
    return best_name, best_samples, best_travel


def peak_speed_at(samples):
    """When the bone is moving fastest. The contact frame of a swing."""
    fastest_at, fastest = samples[0][0], 0.0
    for before, after in zip(samples, samples[1:], strict=False):
        gap = after[0] - before[0]
        if gap <= 0.0:
            continue
        speed = (after[1] - before[1]).length() / gap
        if speed > fastest:
            fastest, fastest_at = speed, (before[0] + after[0]) / 2.0
    return fastest_at, fastest


def furthest_reach_at(samples, root_samples):
    """When the bone is furthest from the creature's middle. Full extension."""
    furthest_at, furthest = samples[0][0], -1.0
    for (time, where), (_, middle) in zip(samples, root_samples, strict=False):
        away = (where - middle).length()
        if away > furthest:
            furthest, furthest_at = away, time
    return furthest_at, furthest


def lowest_point_at(samples):
    """When the bone is nearest the ground. Only meaningful for a downward blow."""
    lowest_at, lowest = samples[0][0], samples[0][1].z
    for time, where in samples:
        if where.z < lowest:
            lowest, lowest_at = where.z, time
    return lowest_at, lowest


def report(creature, interval, path, options):
    say("")
    say("=" * 70)
    name = path.rsplit("/", 1)[1]
    clip = unreal.load_asset("{0}.{1}".format(path, name))
    if clip is None:
        say("{0}: {1} NOT FOUND -- its Paragon pack is not installed. This "
            "clip was NOT measured.".format(creature, name))
        return

    length = clip.get_play_length()
    say("{0}: {1}".format(creature, name))
    say("  clip length {0:.4f} s, attack interval {1:.2f} s".format(
        length, interval))

    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, 0.0, options)
    names = [str(bone) for bone in
             unreal.AnimPoseExtensions.get_bone_names(pose)]

    middle = None
    for candidate in ROOT_BONE_CANDIDATES:
        if candidate in names:
            middle = candidate
            break
    if middle is None:
        say("  REFUSED: this rig has none of {0}, so there is nothing to "
            "measure reach from.".format(list(ROOT_BONE_CANDIDATES)))
        return

    bone, samples, travel = striking_bone(clip, names, options)
    if bone is None or travel < MOVED_AT_ALL_CM:
        say("  **REFUSED: no candidate bone travels more than {0:.0f} cm "
            "across this clip**, so nothing in it can mark a strike. That is "
            "the Sevarog failure in issue #778 and this clip needs an eye on "
            "it rather than a number.".format(MOVED_AT_ALL_CM))
        return

    root_samples = positions(clip, middle, options)

    fastest_at, fastest = peak_speed_at(samples)
    reach_at, reach = furthest_reach_at(samples, root_samples)
    lowest_at, lowest = lowest_point_at(samples)

    say("  striking bone {0}, travels {1:.0f} cm; reach measured from "
        "{2}".format(bone, travel, middle))

    # THE HANDS, WHATEVER ELSE WAS CHOSEN. A ranged attack leaves the hand, and
    # the furthest-travelling bone is not the hand on two of the rigs.
    for hand in ALWAYS_REPORT:
        if hand not in names:
            continue
        hand_samples = positions(clip, hand, options)
        hand_travel = path_length(hand_samples)
        if hand_travel < MOVED_AT_ALL_CM:
            say("  {0}: travels {1:.0f} cm, too little to mark "
                "anything".format(hand, hand_travel))
            continue
        hand_fast_at, hand_fast = peak_speed_at(hand_samples)
        hand_reach_at, _ = furthest_reach_at(hand_samples, root_samples)
        say("  {0}: travels {1:.0f} cm, peak speed at {2:.4f} s "
            "({3:.0f} cm/s), furthest reach at {4:.4f} s".format(
                hand, hand_travel, hand_fast_at, hand_fast, hand_reach_at))
    say("  peak speed     at {0:.4f} s  ({1:.0f} cm/s)".format(
        fastest_at, fastest))
    say("  furthest reach at {0:.4f} s  ({1:.0f} cm from {2})".format(
        reach_at, reach, middle))
    say("  lowest point   at {0:.4f} s  ({1:.0f} cm up)".format(
        lowest_at, lowest))

    # --- do the three rules agree? ---------------------------------------
    answers = [("peak speed", fastest_at), ("furthest reach", reach_at),
               ("lowest point", lowest_at)]
    window = length * AGREEMENT_FRACTION
    spread = max(time for _, time in answers) - min(time for _, time in answers)

    if spread <= window:
        say("  ALL THREE AGREE within {0:.4f} s. Strike at about "
            "{1:.4f} s.".format(window, sum(t for _, t in answers) / 3.0))
    else:
        pairs = []
        for left in range(len(answers)):
            for right in range(left + 1, len(answers)):
                if abs(answers[left][1] - answers[right][1]) <= window:
                    pairs.append((answers[left], answers[right]))
        if pairs:
            (first, second) = pairs[0]
            say("  TWO OF THREE AGREE: {0} and {1}, at about {2:.4f} s. The "
                "third is {3:.4f} s away.".format(
                    first[0], second[0], (first[1] + second[1]) / 2.0, spread))
        else:
            say("  **THE THREE RULES DISAGREE**, spread {0:.4f} s across a "
                "{1:.4f} s clip. This clip cannot be measured this way and "
                "needs judging by eye.".format(spread, length))


def main():
    say("==== when each ordinary attack strikes ====")
    say("Issue #526. NO CLIP CARRIES AN ANIMATION NOTIFY -- checked for all "
        "thirteen on 2026-08-21 -- so every figure here is from the pose.")

    options = unreal.AnimPoseEvaluationOptions()

    for creature, interval, path in CLIPS:
        try:
            report(creature, interval, path, options)
        except Exception as problem:        # noqa: BLE001 - reported, not raised
            say("{0}: {1} FAILED: {2}".format(creature, path, problem))

    say("")
    say("==== done ====")


main()
