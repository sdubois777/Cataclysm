"""Read facts out of the Rampage pack that only the editor can answer.

    python tools/run_editor_python.py tools/probe_brute_animation.py

WHY THIS EXISTS. Three questions block the Brute's animation Blueprint (#387)
and none of them can be answered from outside the editor.

1. **Does `Sprint_Biped_Fwd` carry the same animation as `Jog_Biped_Fwd`?**
   Issue #386 says it does, which would mean a two-gait blend space blends
   between two identical poses. Nothing has compared the bone data.

2. **How long are the Rip and Toss clips?** Issue #382 lists figures and states
   in terms that they were not re-measured. `game/docs/enemy-source-assets.md`,
   the file that holds measured figures, has no Rip and Toss row at all.

3. **What can Unreal's Python actually build?** #387 claims an AnimBlueprint and
   a blend space can be made from Python but that wiring AnimGraph nodes cannot.
   That claim decides whether the graph has to be authored by hand, so it is
   worth testing rather than repeating.

WHAT IT CHANGES. Nothing. It only reads and reports.

A NOTE ON `AnimationBlueprintLibrary`. The obvious way to compare two clips is
`unreal.AnimationBlueprintLibrary.get_bone_pose_for_frame`. That class ships with
the **Animation Modifiers** plugin rather than with the engine's Python bindings,
so it is absent unless that plugin is enabled. The first run of this script found
it absent, which is why the comparison below reaches for the animation data model
instead and reports plainly when it cannot read one.
"""

import unreal

ANIMATIONS = "/Game/ParagonRampage/Characters/Heroes/Rampage/Animations"

#: The clips the Brute uses today, plus the ones the design wants to use.
INTERESTING = [
    "Idle_Biped",
    "Jog_Biped_Fwd",
    "Sprint_Biped_Fwd",
    "Jog_Quad_Fwd",
    "Sprint_Quad_Fwd",
    "Attack_Biped_Melee_A",
    "Ability_GroundSmash_Start",
    "Ability_GroundSmash_Loop",
    "Ability_GroundSmash_End",
    "Ability_RipNToss_Rip",
    "Ability_RipNToss_Idle",
    "Ability_RipNToss_Toss",
    "Ability_RipNToss_Cancel",
]

#: Pairs the design believes might be duplicates. Issue #386.
SUSPECTED_DUPLICATES = [
    ("Jog_Biped_Fwd", "Sprint_Biped_Fwd"),
    ("Jog_Quad_Fwd", "Sprint_Quad_Fwd"),
]


def say(line):
    unreal.log("PROBE| " + str(line))


def load(name):
    return unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, name))


def data_model(anim):
    """The clip's animation data model, or None with a reason logged.

    UE5 moved raw animation data behind IAnimationDataModel. The accessor has
    been spelled more than one way across versions, so each candidate is tried
    rather than assuming one.
    """
    for attempt in ("get_data_model", "get_animation_data_model"):
        if hasattr(anim, attempt):
            try:
                model = getattr(anim, attempt)()
                if model:
                    return model
            except Exception as error:                          # noqa: BLE001
                say("    {0}() raised {1}".format(attempt, error))
    for prop in ("data_model", "animation_data_model"):
        try:
            model = anim.get_editor_property(prop)
            if model:
                return model
        except Exception:                                       # noqa: BLE001
            pass
    return None


def bone_track_names(model):
    for attempt in ("get_bone_track_names", "get_bone_animation_track_names"):
        if hasattr(model, attempt):
            try:
                return list(getattr(model, attempt)())
            except Exception as error:                          # noqa: BLE001
                say("    {0}() raised {1}".format(attempt, error))
    return []


def track_keys(model, bone):
    """Position, rotation and scale keys for one bone, or None."""
    for attempt in ("get_bone_track_keys", "get_bone_animation_track_keys"):
        if hasattr(model, attempt):
            try:
                return getattr(model, attempt)(bone)
            except Exception as error:                          # noqa: BLE001
                say("    {0}({1}) raised {2}".format(attempt, bone, error))
    return None


def same_animation(first, second, label_a, label_b):
    """Compare two clips by their actual bone keys, not by their length.

    COMPARING LENGTHS PROVES NOTHING. Two different animations can run for the
    same time, and both suspected pairs here already do.
    """
    model_a, model_b = data_model(first), data_model(second)
    if model_a is None or model_b is None:
        say("  COULD NOT READ THE ANIMATION DATA MODEL for at least one of "
            "them, so no comparison was made.")
        return None

    tracks_a = bone_track_names(model_a)
    tracks_b = bone_track_names(model_b)
    say("  {0}: {1} animated bones".format(label_a, len(tracks_a)))
    say("  {0}: {1} animated bones".format(label_b, len(tracks_b)))

    if not tracks_a or not tracks_b:
        say("  NO BONE TRACKS READABLE, so nothing could be compared.")
        return None

    if sorted(tracks_a) != sorted(tracks_b):
        only_a = sorted(set(tracks_a) - set(tracks_b))[:5]
        only_b = sorted(set(tracks_b) - set(tracks_a))[:5]
        say("  DIFFERENT BONE SETS. Only in {0}: {1}. Only in {2}: {3}".format(
            label_a, only_a, label_b, only_b))
        return False

    worst = 0.0
    compared = 0
    for bone in tracks_a:
        keys_a = track_keys(model_a, bone)
        keys_b = track_keys(model_b, bone)
        if keys_a is None or keys_b is None:
            say("  COULD NOT READ KEYS for {0}, so nothing could be "
                "compared.".format(bone))
            return None
        try:
            pos_a, pos_b = list(keys_a[0]), list(keys_b[0])
            rot_a, rot_b = list(keys_a[1]), list(keys_b[1])
        except Exception as error:                              # noqa: BLE001
            say("  keys for {0} were not the expected shape: {1}".format(
                bone, error))
            return None

        if len(pos_a) != len(pos_b) or len(rot_a) != len(rot_b):
            say("  DIFFERENT KEY COUNTS on {0}: {1}/{2} against {3}/{4}".format(
                bone, len(pos_a), len(rot_a), len(pos_b), len(rot_b)))
            return False

        for one, two in zip(pos_a, pos_b, strict=True):
            worst = max(worst, float((one - two).length()))
            compared += 1
        for one, two in zip(rot_a, rot_b, strict=True):
            try:
                delta = abs(float(one.x - two.x)) + abs(float(one.y - two.y)) \
                    + abs(float(one.z - two.z)) + abs(float(one.w - two.w))
            except Exception:                                   # noqa: BLE001
                delta = 0.0
            worst = max(worst, delta)
            compared += 1

    say("  compared {0} keys across {1} bones; largest difference "
        "{2:.6f}".format(compared, len(tracks_a), worst))
    return worst < 1e-4


def probe_python_authoring():
    """What the editor's Python exposes for building an animation Blueprint."""
    say("")
    say("=== WHAT PYTHON CAN BUILD ===")

    for name in ("AnimBlueprintFactory", "BlendSpaceFactory1D",
                 "BlendSpaceFactoryNew"):
        say("  unreal.{0}: {1}".format(
            name, "present" if hasattr(unreal, name) else "ABSENT"))

    # THE QUESTION THAT DECIDES EVERYTHING: can two pins be joined? Node classes
    # being exposed is not enough -- an animation graph is the connections.
    say("")
    say("  Anything exposed that could connect two pins:")
    found = []
    for name in dir(unreal):
        low = name.lower()
        if any(word in low for word in ("connect", "makelink", "link_pin",
                                        "wire", "schema")):
            found.append(name)
    say("    {0}".format(found if found else "NOTHING"))

    if hasattr(unreal, "BlueprintEditorLibrary"):
        methods = [m for m in dir(unreal.BlueprintEditorLibrary)
                   if not m.startswith("_")]
        joiners = [m for m in methods
                   if any(w in m.lower() for w in ("connect", "link", "wire"))]
        say("    BlueprintEditorLibrary joining methods: {0}".format(
            joiners if joiners else "NONE"))
        makers = [m for m in methods
                  if any(w in m.lower() for w in ("add_node", "create_node",
                                                  "spawn"))]
        say("    BlueprintEditorLibrary node-making methods: {0}".format(
            makers if makers else "NONE"))

    say("")
    say("  Which plugins would add the missing animation library:")
    for name in ("AnimationBlueprintLibrary", "AnimationModifier",
                 "AnimationDataController"):
        say("    unreal.{0}: {1}".format(
            name, "present" if hasattr(unreal, name) else "ABSENT"))


def main():
    say("=== ASSETS ===")
    loaded = {}
    for name in INTERESTING:
        anim = load(name)
        loaded[name] = anim
        if anim is None:
            say("  {0}: NOT FOUND".format(name))
            continue
        say("  {0:32} {1:>8.4f} s".format(name, float(anim.get_play_length())))

    say("")
    say("=== ISSUE #386: ARE THE SPRINT CLIPS DUPLICATES? ===")
    for first_name, second_name in SUSPECTED_DUPLICATES:
        first, second = loaded.get(first_name), loaded.get(second_name)
        say("{0} against {1}:".format(first_name, second_name))
        if first is None or second is None:
            say("  one of them is missing, so nothing was compared")
            continue
        verdict = same_animation(first, second, first_name, second_name)
        if verdict is True:
            say("  VERDICT: IDENTICAL. A blend between these blends nothing.")
        elif verdict is False:
            say("  VERDICT: DIFFERENT. They are two real gaits.")
        else:
            say("  VERDICT: COULD NOT TELL. See the messages above.")

    probe_python_authoring()
    say("")
    say("=== PROBE FINISHED ===")


main()
