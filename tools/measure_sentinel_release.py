"""Find the moment the Corrupted Sentinel's shot leaves its weapon.

    python tools/run_editor_python.py tools/measure_sentinel_release.py

WHY THIS EXISTS. Issue #478. The creature's telegraph cannot be lined up with its
shot, because nothing has measured when the shot is released inside
`Fire_Planted`. The design's telegraph rule puts a marker on the ground for
`0.4 + Radius / 3.5` seconds and then the attack lands; if the animation releases
at some other moment, the creature either fires before its own marker has finished
warning, or holds a finished animation before firing.

The Brute has exactly this figure and it was measured rather than guessed:
`tools/measure_animation_impact.py` follows the throwing hand through
`Ability_RipNToss_Toss` and found the release at 0.539 s of an 0.87 s clip.

WHY THIS IS A SEPARATE SCRIPT AND NOT AN ADDITION TO THAT ONE. A throw and a shot
give different signals. A throw releases at the top of the hand's arc, which is a
height extremum and is what `measure_animation_impact.py` looks for. **A gun does
not arc.** The muzzle travels almost nowhere across a firing clip, so its height
says nothing at all; what says something is the RECOIL -- the moment the weapon is
driven backwards hardest.

**AS OF 2026-08-20 THIS SCRIPT ANSWERS "NO" AND THAT IS ITS USEFUL RESULT.** Two
methods were tried and both failed their control, so it reports no release moment
at all rather than a number nobody should use. What it has established is what
the clips do and do not drive, and which two approaches are already ruled out, so
the next attempt does not start from nothing.

WHAT IT MEASURES: **how far the gun's slide sits from the gun's body**, sampled
across the clip. A slide is racked back when the weapon fires and returns
afterwards, so the moment it is furthest back would be the moment of the shot. It
is measured relative to `weapon_r` rather than in world space, because the whole
weapon is carried around by the arm and an absolute position mostly measures the
arm.

THE CONTROL IS THE POINT OF THIS SCRIPT, AND BOTH METHODS FAILED IT.
`Idle_Planted` is a single pose 0.03 seconds long and `PlantedIntro` is the
creature rooting itself, so neither fires anything. Measured 2026-08-20:

  method                          Fire_Planted   PlantedIntro (fires nothing)
  largest muzzle acceleration     485 cm/s/s     502 cm/s/s
  slide furthest from the body    55.0 cm        102.8 cm

In both cases the clip with no shot in it reads at least as strongly as the clip
with one, so neither method is measuring a shot. The first is kept in this
comment rather than in code because it is the obvious method and somebody will
otherwise try it again.

WHY THEY PROBABLY FAILED, which is the lead for the next attempt. The offsets are
tens to hundreds of centimetres, and a real gun slide travels a few. Issue #478
already warns about this: "A Paragon rig's `weapon_r` bone is the PROP, and the
animator may drive it as the projectile rather than as the hand. On Rampage it
travels up to 1253 cm from the creature during a throw." These bones are very
likely props driven for effect rather than parts of a working weapon. **And three
of the four weapon bones are not animated by these clips at all**, which this
script reports.

SO THE RELEASE MOMENT MAY HAVE TO BE DECIDED RATHER THAN MEASURED. That is not a
disaster: every telegraphed ability in this project already resolves at the end
of its wind-up, and the creature can simply fire there. What is lost is the
guarantee that the visible barrel flash agrees with it, which is what the Brute's
`RockThrowStrikeIntoReleaseSeconds` buys for the thrown rock.

WHAT IT CHANGES. Nothing. It loads clips and prints what they say.
"""

import unreal

ANIMATIONS = ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
              "/Animations/Siege")

#: The two rooted firing clips the creature actually uses, its close-range
#: fallback, and two clips that fire nothing at all.
#:
#: THE LAST TWO ARE THE CONTROL. A method that finds a release in a single-frame
#: idle is finding noise. Measuring them costs one line each and is the only
#: thing that says the figures above them mean anything.
FIRING = ["Fire_Planted", "Fire_Planted_B", "MeleeAttack_A"]
NOT_FIRING = ["Idle_Planted", "PlantedIntro"]

#: Where the weapon might be. The melee lane minion out of the same pack carries
#: `weapon_gun_r`, `gun_slide`, `gun_foregrip` and `gun_stock_retractor`, so the
#: siege one probably carries something similar -- but it is a DIFFERENT skeleton,
#: `Minion_Lane_Siege_Skeleton`, so the names are read from the clip rather than
#: assumed. Anything in this list that the clip does not drive is reported as
#: absent instead of being measured as a bone that never moves.
CANDIDATE_BONES = [
    "weapon_gun_r", "gun_slide", "gun_stock_retractor", "gun_foregrip",
    "weapon_r", "weapon_l", "hand_r", "hand_l", "lowerarm_r", "upperarm_r",
]

#: The two the measurement actually rests on. The slide is the part that racks
#: when the weapon fires; the body is what it racks against, and subtracting one
#: from the other removes the arm carrying the whole weapon about.
#:
#: MEASURED ON 2026-08-20: of the four `gun_` and `weapon_gun_` bones the melee
#: lane minion carries, this skeleton's firing clips drive only `gun_slide`.
#: `weapon_gun_r`, `gun_foregrip` and `gun_stock_retractor` are not animated at
#: all, so `weapon_r` is what the slide is measured against.
SLIDE_BONE = "gun_slide"
BODY_BONE = "weapon_r"

#: How much louder a firing clip has to be than a clip that fires nothing before
#: its peak counts as a shot rather than as ordinary animation motion.
#:
#: TWICE, WHICH IS A JUDGEMENT AND A LENIENT ONE. Measured on 2026-08-20 the
#: firing clip is HALF as loud as the control, so this threshold is not what
#: decides the current answer -- any threshold at or above 1 gives the same "no".
#: It is here so that a future rig, or a future method, has to clear a bar rather
#: than merely produce a number.
CLEARLY_LOUDER_THAN_THE_CONTROL = 2.0

#: Samples across each clip. The clips are authored at 30 frames a second and
#: `Fire_Planted` is 2.40 seconds, so 240 samples is more than three times finer
#: than the keys.
SAMPLES = 240


def say(line):
    unreal.log("RELEASE| " + str(line))


def load(name):
    return unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, name))


def driven_bones(clip):
    """Every bone the clip animates, or None when that cannot be read."""
    reader = getattr(unreal.AnimationLibrary, "get_animation_track_names", None)
    if reader is None:
        return None
    try:
        return set(str(name) for name in reader(clip))
    except Exception:  # noqa: BLE001 -- the editor's own errors vary
        return None


def track(clip, bone, space, options):
    """(time, position) for one bone across the whole clip, in world space."""
    length = clip.get_play_length()
    out = []
    for step in range(SAMPLES + 1):
        time = length * step / float(SAMPLES)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            clip, time, options)
        transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, space)
        out.append((time, transform.translation))
    return out


def total_travel(samples):
    """How far the bone moves in all, summed frame to frame."""
    total = 0.0
    for index in range(1, len(samples)):
        total += float((samples[index][1] - samples[index - 1][1]).length())
    return total


def slide_against_the_body(clip, space, options):
    """(time, how far the slide sits from where it rests) across the clip.

    MEASURED FROM THE FIRST FRAME'S OFFSET rather than from an absolute
    position, because a slide at rest sits somewhere and what matters is how far
    it has moved from there.
    """
    slide = track(clip, SLIDE_BONE, space, options)
    body = track(clip, BODY_BONE, space, options)

    rest = slide[0][1] - body[0][1]
    out = []
    for index in range(len(slide)):
        offset = (slide[index][1] - body[index][1]) - rest
        out.append((slide[index][0], float(offset.length())))
    return out


def report(name, space, options):
    """Print what one clip does, and return its peak slide offset in cm.

    Returns None when the clip could not be read at all, which is different from
    a clip whose slide does not move.
    """
    clip = load(name)
    if clip is None:
        say("{0}: NOT FOUND -- the Paragon Minions pack is not installed"
            .format(name))
        return None

    length = clip.get_play_length()
    driven = driven_bones(clip)
    say("")
    say("{0}: length {1:.4f} s".format(name, length))

    if driven is not None:
        missing = [b for b in CANDIDATE_BONES if b not in driven]
        if missing:
            say("  not driven by this clip: {0}".format(", ".join(missing)))

        for needed in (SLIDE_BONE, BODY_BONE):
            if needed not in driven:
                say("  {0} is not driven by this clip, so the slide cannot be "
                    "read against the body here".format(needed))
                return None

    # HOW MUCH EACH CANDIDATE MOVES AT ALL, so a bone that never moves is ruled
    # out visibly rather than measured and quietly believed.
    for bone in CANDIDATE_BONES:
        if driven is not None and bone not in driven:
            continue
        travel = total_travel(track(clip, bone, space, options))
        say("  {0:22} travels {1:7.1f} cm in world space".format(bone, travel))

    offsets = slide_against_the_body(clip, space, options)
    furthest = max(offsets, key=lambda pair: pair[1])

    say("  --")
    say("  the slide sits furthest from the body at {0:.4f} s, by {1:.2f} cm, "
        "which is {2:.1f}% through".format(
            furthest[0], furthest[1],
            100.0 * furthest[0] / max(length, 1e-6)))

    # THE SHAPE, NOT ONLY THE PEAK. A single number cannot be told apart from a
    # slide that drifts steadily, and a reader looking at a rack-and-return can
    # see one at a glance.
    say("  how far it sits from rest, sampled across the clip:")
    line = []
    for step in range(0, len(offsets), max(1, len(offsets) // 16)):
        line.append("{0:.1f}".format(offsets[step][1]))
    say("    " + "  ".join(line))

    return furthest[1]


def main():
    say("==== when the Corrupted Sentinel's shot leaves its weapon ====")
    say("Issue #478. The design needs the release moment so the telegraph and")
    say("the shot can be lined up. Nothing here changes anything.")

    space = unreal.AnimPoseSpaces.WORLD
    options = unreal.AnimPoseEvaluationOptions()

    say("")
    say("==== THE CLIPS THAT FIRE ====")
    firing = {}
    for name in FIRING:
        firing[name] = report(name, space, options)

    say("")
    say("==== THE CONTROL: CLIPS THAT FIRE NOTHING ====")
    say("A confident release moment in these means the method is reading noise.")
    control = {}
    for name in NOT_FIRING:
        control[name] = report(name, space, options)

    # THE VERDICT IS COMPUTED RATHER THAN LEFT TO THE READER. Every number above
    # looks like an answer, and the only thing that decides whether any of them
    # is one is the comparison against a clip that fires nothing. Making the
    # script say so is what stops the peak being copied into a header.
    say("")
    say("==== IS THERE A RELEASE MOMENT HERE AT ALL? ====")

    loudest_control = max(
        [value for value in control.values() if value is not None] or [0.0])
    shot = firing.get("Fire_Planted")

    if shot is None:
        say("Fire_Planted could not be read, so nothing can be decided.")
    elif shot > loudest_control * CLEARLY_LOUDER_THAN_THE_CONTROL:
        say("YES. Fire_Planted's slide moves {0:.1f} cm against the loudest "
            "control's {1:.1f} cm, which is more than {2:.0f} times as far. The "
            "peak above is the release moment."
            .format(shot, loudest_control, CLEARLY_LOUDER_THAN_THE_CONTROL))
    else:
        say("NO, AND NO NUMBER FROM THIS RUN SHOULD BE USED. Fire_Planted's "
            "slide moves {0:.1f} cm and a clip that fires nothing moves {1:.1f} "
            "cm, so this is not measuring a shot. See the module comment for "
            "what has already been ruled out and why. Issue #478."
            .format(shot, loudest_control))

    say("")
    say("==== done ====")


main()
