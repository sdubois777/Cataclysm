"""Put the "swing connects" marker on the player's attack clips.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/place_swing_markers.py

WHY THIS EXISTS. Issue #1133. Damage, the swing arc and every other effect used
to fire in the frame a skill activated, while the attack animation played for
one to nearly two seconds beside it, so an enemy was hurt while the weapon was
still going backwards. `UCataclysmSwingTiming` reads a marker off the clip and
schedules the blow for where it sits. This script is what puts the marker there.

WHY A SCRIPT AND NOT CLICKING. Placing a notify by hand in the editor is a
change to a binary asset that nobody can review. This file states the four
figures in one place, in text, next to where they came from, and can be re-run
after a clip is replaced. The .uasset files it writes are committed, and they
are the ones the game reads.

WHERE THE FIGURES COME FROM. `tools/measure_attack_impact.py` measured them from
the pose on 2026-09-01 and `game/docs/player-source-assets.md` records them with
the caveats. **READ THOSE CAVEATS.** The measuring script refused two of the four
clips on its own three-rule agreement test, because two of its three rules were
written for weapon swings and downward blows and these are unarmed clips. Peak
speed is the rule that applies and it has separate corroboration, but nobody has
watched these clips yet. Every figure below is a starting point to be adjusted by
playing, not a settled answer.

IT IS SAFE TO RUN TWICE. Everything on the marker track is removed before
anything is added, so a second run replaces rather than stacks. A clip that
accumulated two markers would take the earlier one and the mistake would be
invisible.

WHAT IT CHANGES. Four .uasset files under
game/Content/Characters/Mannequins/Anims/Unarmed/Attack/, which are tracked in
git. It saves them; `git status` afterwards is the evidence it did anything.
"""

import os

import unreal

#: The one notify track this project owns on a clip. Everything on it is removed
#: and rewritten by this script, so nothing else may be put here by hand.
MARKER_TRACK = "Cataclysm"

#: Clip, and how many seconds into it the blow lands.
#:
#: THESE ARE THE PEAK SPEED FIGURES, which is the sample where the striking bone
#: moves fastest. That is what an animator means by the contact frame, and on
#: MM_Attack_01 two separate bones -- hand_r and weapon_r -- peak at the same
#: sample, which is corroboration the three-rule test could not give.
#:
#: MM_ChargedAttack IS MARKED THOUGH NOTHING PLAYS IT. It is deliberately not in
#: the cycle -- at 1.8333 seconds it is nearly three times a fast weapon's swing
#: interval -- but it is kept for a skill that deserves a heavier swing, and
#: marking it now costs nothing and means the clip is ready.
CLIPS = [
    ("MM_Attack_01", 0.3708),
    ("MM_Attack_02", 0.3458),
    ("MM_Attack_03", 0.7847),
    ("MM_ChargedAttack", 1.0771),
]

FOLDER = "/Game/Characters/Mannequins/Anims/Unarmed/Attack"


#: Where the .uasset files really live, so a save can be checked by looking at
#: the file rather than by believing the editor.
DISK_FOLDER = ("Content/Characters/Mannequins/Anims/Unarmed/Attack")


def say(line):
    unreal.log("MARKER| " + str(line))


def disk_stamp(name):
    """(size, modification time) of one clip's file, or None if it is absent.

    THE POINT OF THIS IS TO CATCH A SAVE THAT DID NOT HAPPEN. Both halves are
    taken because either alone can miss: a rewrite that changes no bytes keeps
    the size, and two writes inside the filesystem's timestamp granularity keep
    the time. Adding a notify changes the size, so in practice the size is what
    answers here.
    """
    path = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
        DISK_FOLDER, name + ".uasset")
    try:
        info = os.stat(path)
    except OSError:
        return None
    return (info.st_size, info.st_mtime_ns)


def place(name, seconds):
    """Put one marker on one clip, and say what happened."""
    path = "{0}/{1}".format(FOLDER, name)
    clip = unreal.load_asset("{0}.{1}".format(path, name))
    if clip is None:
        say("{0}: NOT FOUND. This checkout has no Mannequin assets, so the "
            "marker was NOT placed.".format(name))
        return False

    length = clip.get_play_length()
    if seconds > length:
        say("{0}: REFUSED. The marker at {1:.4f} s is past the clip's own "
            "{2:.4f} s. Nothing was changed.".format(name, seconds, length))
        return False

    # CLEARED FIRST SO A SECOND RUN REPLACES RATHER THAN STACKS. The track is
    # created if it is not there; removing from a track that does not exist is
    # harmless.
    unreal.AnimationLibrary.add_animation_notify_track(
        clip, MARKER_TRACK, unreal.LinearColor(1.0, 0.4, 0.1, 1.0))
    removed = unreal.AnimationLibrary.remove_animation_notify_events_by_track(
        clip, MARKER_TRACK)

    unreal.AnimationLibrary.add_animation_notify_event(
        clip, MARKER_TRACK, seconds,
        unreal.CataclysmSwingConnectsNotify)

    # READ IT BACK RATHER THAN TRUSTING THE CALL. A notify that failed to attach
    # leaves the asset looking fine and the game timing every blow off the
    # fallback instead, which is a quiet wrong answer rather than a loud one.
    events = unreal.AnimationLibrary.get_animation_notify_events(clip)
    placed = [e for e in events
              if isinstance(e.notify, unreal.CataclysmSwingConnectsNotify)]
    if len(placed) != 1:
        say("{0}: FAILED. Expected exactly one marker after writing, found "
            "{1}.".format(name, len(placed)))
        return False

    # FORCED, BECAUSE ADDING A NOTIFY DOES NOT MARK THE PACKAGE DIRTY. This
    # cost a run on 2026-09-01 and it failed in the worst possible way: the
    # script reported all four clips "saved", the editor logged
    #
    #     LogFileHelpers: All files are already saved.
    #
    # and not one byte reached disk. `save_asset` defaults to
    # only_if_is_dirty=True, and `AnimationLibrary.add_animation_notify_event`
    # changes the object without flagging its package, so there was nothing for
    # the save to do. Everything downstream looked fine: the read-back below
    # found the notify, because the notify was really there -- in memory.
    # `only_if_is_dirty=False` IS THE WHOLE FIX AND THERE IS NO SECOND HALF.
    # `clip.mark_package_dirty()` was tried first and does not exist: the method
    # is not exposed to Python, and the attempt raised
    # "'AnimSequence' object has no attribute 'mark_package_dirty'" on all four
    # clips. Forcing the save is enough on its own.
    before = disk_stamp(name)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(
        clip, only_if_is_dirty=False)
    after = disk_stamp(name)

    # AND THE FILE ON DISK IS CHECKED, NOT THE RETURN VALUE. The whole reason
    # this run failed silently the first time is that every in-process signal
    # said success. The only thing that could have caught it was looking at the
    # file, so that is what is looked at.
    if not saved or after == before:
        say("{0}: **NOT WRITTEN.** The marker was added in memory and the file "
            "on disk did not change (save returned {1}, stamp {2} -> {3}). "
            "Nothing was committed for this clip.".format(
                name, saved, before, after))
        return False

    say("{0}: marker at {1:.4f} s of {2:.4f} s ({3:.1f}% in), {4} old marker(s) "
        "removed, written to disk.".format(
            name, seconds, length, seconds / length * 100.0, removed))
    return True


def main():
    say("==== placing the swing-connects marker ====")
    say("Issue #1133. Figures measured by tools/measure_attack_impact.py and "
        "recorded in game/docs/player-source-assets.md, which states which of "
        "them the measurement refused.")

    done = 0
    for name, seconds in CLIPS:
        try:
            if place(name, seconds):
                done += 1
        except Exception as problem:        # noqa: BLE001 - reported, not raised
            say("{0}: FAILED: {1}".format(name, problem))

    say("")
    say("==== {0} of {1} clips marked ====".format(done, len(CLIPS)))
    if done != len(CLIPS):
        say("**NOT EVERY CLIP WAS MARKED.** An unmarked clip still works: "
            "UCataclysmSwingTiming falls back to the middle of it. But the "
            "figure will be wrong, so read the lines above.")


main()
