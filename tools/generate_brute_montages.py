"""Build the Brute's two ability montages out of the Rampage pack's clips.

Runs inside the Unreal editor's Python interpreter, not the system Python. Start
it with the runner, which checks the preconditions and checks afterwards that it
actually ran:

    python tools/run_editor_python.py tools/generate_brute_montages.py

RUN IT TWICE ON A FRESH ASSET. That is not a wart in this script, it is how the
engine works, and it is the thing that made issue #412 look impossible. See
"WHY TWICE" below.

WHY THIS EXISTS. Each of the Brute's two abilities is two clips: a wind-up that
ends with the creature poised, and a release where the attack happens. Until now
those two clips were sequenced in C++ by
game/Source/Cataclysm/Character/CataclysmBruteCharacter.cpp, out of separate
dynamic montages driven by timers, plus a third "hold" clip looped a fractional
number of times to cover the gap between them. Pull requests #407, #409, #410 and
#411 were four attempts to make that work and all four were the same fault: a
timer firing in the wrong order, a clip sized to the wrong window, a blend that
discarded the end of a clip, a hold that outlived its ability.

A montage holds the two clips on one continuous timeline, so there is no seam to
get wrong. Issue #412.

WHY GENERATED RATHER THAN CLICKED TOGETHER IN THE EDITOR. The same reason the
data tables, the gameplay tags and the input assets are: a .uasset is binary, so
an asset made by hand leaves no reviewable record of what it contains or why.
This script is that record. Re-running it overwrites the animation track of every
montage it owns, so an edit made by hand in the editor is lost on the next run.

WHY TWICE, AND WHY THE PREVIOUS ATTEMPT CONCLUDED THIS WAS IMPOSSIBLE. A montage
does not recompute its own length when its animation track is written. Nothing in
the editor's property system does it: UAnimMontage::PostEditChangeProperty
collects markers, updates the frame rate and propagates to children, and does not
touch the length. The only thing that recomputes it is UAnimMontage::PostLoad, at
Engine/Source/Runtime/Engine/Private/Animation/AnimMontage.cpp:464, which calls
FAnimTrack::ValidateSegmentTimes to lay the segments end to end and then
CalculateSequenceLength and SetCompositeLength to fix the montage's own length.
The engine logs "Please resave the asset" when it does so.

PostLoad only runs when the asset is read from disk. So in the process that
writes the segments the montage still reports a play length of 0.0000 seconds,
and a montage of zero length plays for zero seconds. Measuring at that moment and
stopping is what produced the conclusion, recorded in issue #412 on 2026-08-08,
that these assets could not be built from a script and had to be authored by
hand. They can. The sequence is:

    run 1   create the asset, write the animation track, save.
            The file on disk has the right segments and a length of zero.
    run 2   a new process loads it, PostLoad lays the segments end to end and
            fixes the length, this script sees the track is already correct and
            leaves it alone, and the save writes the corrected length out.
    run 3+  nothing changes.

THIS IS ALSO WHY StartPos IS NEVER WRITTEN HERE. FAnimSegment::StartPos is
declared UPROPERTY(BlueprintReadOnly, VisibleAnywhere), so the editor's property
system refuses to set it and Python reports it read-only. It does not need
setting: ValidateSegmentTimes derives it from the lengths of the segments before
it, which is exactly the "back to back with no gap" this wants.

THE SLOT IS DefaultSlot AND NOTHING HAD TO BE CHOSEN. The UAnimMontage
constructor calls AddSlot(FAnimSlotGroup::DefaultSlotName), so a newly created
montage already carries one slot track named DefaultSlot. That is the name of the
Slot node inside ABP_Brute and of ACataclysmBruteCharacter::AttackSlotName, so
the three already agree. The Paragon pack's own montages use a slot named
UpperBody, which is a different question about blending attacks over locomotion
from the waist up; it is not settled here.

WHAT THIS SCRIPT DOES NOT DO. It does not author montage sections. It does not
need to: the factory-created montage already has one section covering the whole
timeline, and these montages play straight through rather than jumping between
sections. FAnimSegment's owning array CompositeSections is not reachable from
Python at all -- reading it raises "Failed to find property 'composite_sections'"
-- so a design that needed named sections would need a different approach.
"""

import unreal

# --- where the source clips and the finished montages live -------------------

SKELETON = "/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage_Skeleton"
ANIMATIONS = "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations"
DESTINATION = "/Game/Enemies/Demonic/Brute"

# --- what to build -----------------------------------------------------------
#
# The wind-up clip first, then the release. The join between them is the moment
# the attack lands, which is what CataclysmBruteCharacter times the montage
# against. No hold clip appears here and none is wanted: Ability_GroundSmash_Loop
# and Ability_RipNToss_Idle exist to pad a wind-up out, and nothing needs padding
# once the two halves run back to back.

MONTAGES = [
    ("AM_Brute_Stomp", ["Ability_GroundSmash_Start", "Ability_GroundSmash_End"]),
    ("AM_Brute_RockThrow", ["Ability_RipNToss_Rip", "Ability_RipNToss_Toss"]),
]

# --- how each montage blends against the walking and standing animation ------
#
# THESE LIVE IN THE ASSET RATHER THAN IN THE C++, which is the change the project
# owner asked for on 2026-08-08: "why aren't we just plugging them into an
# animation blueprint and exposing the timing variables so it's easy to change on
# the fly?" A number here can be dragged in the montage editor against a live
# preview. The same number passed as an argument to
# PlaySlotAnimationAsDynamicMontage cannot be seen at all from outside the code.

#: Seconds an ability montage takes to blend in, and to blend back out to
#: locomotion. Roughly four frames at 30. The point is that it is not zero:
#: before ABP_Brute existed the mesh ran in single-node mode, which cannot blend,
#: and a wind-up cutting to a release in one frame is the fault that replaced.
BLEND_SECONDS = 0.15

#: When the blend back to locomotion starts, as seconds before the montage ends.
#:
#: ZERO MEANS PLAY THE WHOLE THING, THEN BLEND, and the engine's default of -1
#: does not mean what it looks like. AnimMontage.h says of BlendOutTriggerTime:
#: "<0 means using BlendOutTime, so BlendOut finishes as Montage ends." The blend
#: FINISHES as the montage ends, so it STARTS a blend-length before the end, and
#: the last 0.15 seconds of the release is a falling cross-fade against whatever
#: is underneath. That is what made the Brute's arms sag back toward standing
#: before its stomp landed, reported on 2026-08-08. At zero the trigger
#: comparison collapses to an epsilon, so nothing fires until the montage is
#: over.
BLEND_OUT_TRIGGER_TIME = 0.0


def log(message):
    """Print with a prefix the run log can be grepped for."""
    unreal.log("BRUTEMONTAGE: {}".format(message))


def load_clips(clip_names):
    """Load the source clips, or return None naming the first one missing.

    The Paragon packs are gitignored, so a fresh clone and every git worktree
    has none of them. That is an ordinary state rather than an error, and it is
    reported as such.
    """
    clips = []
    for clip_name in clip_names:
        path = "{}/{}".format(ANIMATIONS, clip_name)
        clip = unreal.EditorAssetLibrary.load_asset(path)
        if clip is None:
            log("source clip {} is absent; the Paragon pack is not "
                "installed".format(path))
            return None
        clips.append(clip)
    return clips


def wanted_segments(clips):
    """One segment per clip, each playing the whole clip once at authored speed.

    StartPos is deliberately absent. See the module docstring.
    """
    segments = []
    for clip in clips:
        segment = unreal.AnimSegment()
        segment.set_editor_property("anim_reference", clip)
        segment.set_editor_property("anim_start_time", 0.0)
        segment.set_editor_property("anim_end_time", clip.get_play_length())
        segment.set_editor_property("anim_play_rate", 1.0)
        segment.set_editor_property("looping_count", 1)
        segment.set_editor_property("cached_play_length", clip.get_play_length())
        segments.append(segment)
    return segments


def track_already_correct(montage, clips):
    """True when the montage's slot track already holds exactly these clips.

    Checked so that the second run leaves the track alone. Rewriting it would
    put StartPos back to zero on every segment, and only a further load would
    lay them end to end again -- so the asset would never settle.
    """
    tracks = montage.get_editor_property("slot_anim_tracks")
    if len(tracks) != 1:
        return False
    segments = tracks[0].get_editor_property("anim_track").get_editor_property(
        "anim_segments")
    if len(segments) != len(clips):
        return False
    for segment, clip in zip(segments, clips, strict=True):
        if segment.get_editor_property("anim_reference") != clip:
            return False
    return True


def build(name, clip_names):
    """Create or update one montage. Returns True when the asset is correct."""
    path = "{}/{}".format(DESTINATION, name)

    skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON)
    if skeleton is None:
        log("skeleton {} is absent; nothing to build".format(SKELETON))
        return False

    clips = load_clips(clip_names)
    if clips is None:
        return False

    expected_length = sum(clip.get_play_length() for clip in clips)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        montage = unreal.EditorAssetLibrary.load_asset(path)
        log("{} already existed".format(name))
    else:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("target_skeleton", skeleton)
        montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DESTINATION, unreal.AnimMontage, factory)
        log("{} created".format(name))

    if montage is None:
        log("{} could not be created or loaded".format(name))
        return False

    if track_already_correct(montage, clips):
        log("{} already holds the right clips; leaving the track alone".format(
            name))
    else:
        tracks = montage.get_editor_property("slot_anim_tracks")
        track = tracks[0]
        anim_track = track.get_editor_property("anim_track")
        anim_track.set_editor_property("anim_segments", wanted_segments(clips))
        track.set_editor_property("anim_track", anim_track)
        montage.set_editor_property("slot_anim_tracks", [track])
        log("{} animation track written with {} clip(s)".format(
            name, len(clips)))

    # Always written, whether or not the track needed rewriting, so that a
    # change to the constants above reaches an asset that already exists.
    blend_in = montage.get_editor_property("blend_in")
    blend_in.set_editor_property("blend_time", BLEND_SECONDS)
    montage.set_editor_property("blend_in", blend_in)

    blend_out = montage.get_editor_property("blend_out")
    blend_out.set_editor_property("blend_time", BLEND_SECONDS)
    montage.set_editor_property("blend_out", blend_out)

    montage.set_editor_property("blend_out_trigger_time", BLEND_OUT_TRIGGER_TIME)

    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)

    length = montage.get_play_length()
    slot = montage.get_editor_property("slot_anim_tracks")[0].get_editor_property(
        "slot_name")
    log("{} saved: slot={} play length={:.4f} (expected {:.4f}) "
        "blend in/out={:.2f}/{:.2f} blend out trigger={:.2f}".format(
            name, slot, length, expected_length,
            montage.get_editor_property("blend_in").get_editor_property(
                "blend_time"),
            montage.get_editor_property("blend_out").get_editor_property(
                "blend_time"),
            montage.get_editor_property("blend_out_trigger_time")))

    if abs(length - expected_length) > 0.001:
        log("{} still reports the wrong length. This is expected on the run "
            "that created it. Run this script again.".format(name))
        return False

    log("{} is correct".format(name))
    return True


def main():
    log("==== generate_brute_montages starting ====")
    correct = 0
    for name, clip_names in MONTAGES:
        if build(name, clip_names):
            correct += 1
    log("==== {} of {} montage(s) correct ====".format(correct, len(MONTAGES)))
    if correct != len(MONTAGES):
        log("Run this script again if a montage was just created, or install "
            "the Paragon Rampage pack if the source clips were absent.")


main()
