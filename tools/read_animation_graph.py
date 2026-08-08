"""Read what is wired to what inside an animation Blueprint, and write it down.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/read_animation_graph.py

WHY THIS EXISTS. Issue #408. `ABP_Brute` shipped with both of its "Blend Poses by
bool" nodes wired the wrong way round, so the Brute played its four-legged chase
animation standing still and its standing animation while moving. Nothing caught
it, and nothing could: the two automation tests that cover this asset check that
it is attached to the character, not what is inside it.

WHY THE MISTAKE IS EASY. The engine puts the TRUE pose on pin index 0, which is
the opposite of how the node reads top to bottom. It says so itself, in
Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_BlendListByBool.cpp:

    // Note: Intentionally flipped boolean sense
    // (the true input is #0, and the false input is #1)
    return GetActiveValue() ? 0 : 1;

Each of the seven vertical slice enemies will need an animation Blueprint, and
that flipped sense is waiting in every one of them.

WHAT WAS NOT KNOWN UNTIL NOW. Issue #408 says whether the editor's Python can
traverse graph pin connections "is not known and needs one experiment to settle".
It can. `unreal.EdGraphPin` is not a bound type, but every graph node exposes
`list_input_pins()`, and each pin exposes `get_pin_name()`, `get_owning_node()`
and `list_connected_pins()`. Following a pose pin to the node on the other end is
three calls.

WHAT IT WRITES. `game/Data/animation_graph_readings.json`, holding what was read
and the content hash of the asset it was read from.
`tools/tests/test_animation_graph_reading_is_current.py` fails when the asset has
changed since, which is what stops the record quietly describing an older graph.
That is the same arrangement `game/Data/datatable_asset_sources.json` already uses
for the generated DataTables.

WHAT IT CANNOT SEE. The animation graph only. The blueprint's own method for
enumerating nodes is `AnimationBlueprintLibrary.GetNodesOfClass` and it
refuses anything that is not an `AnimGraphNode`, so the event graph -- where
`bChasing` and `bMoving` are computed from the pawn's ground speed -- is
invisible to it. The conditions below are therefore recorded BY NAME and how
each is computed is unchecked. The 375 cm/s gait threshold that
`tools/tests/test_brute_matches_the_model.py` records has still never been
read out of the asset. Issue #430.

WHAT IT CHANGES. Nothing in the asset. It only reads, and writes the record.
"""

import hashlib
import json
import os

import unreal

#: The animation Blueprints to read, by object path.
GRAPHS = [
    "/Game/Enemies/Demonic/Brute/ABP_Brute",
]

#: Where the record goes, relative to the project's Content directory's parent.
RECORD = "Data/animation_graph_readings.json"

#: What the node's own pins are called. ASKED OF THE EDITOR RATHER THAN
#: GUESSED: the pins are DISPLAYED as "Blend Pose 0" and are NAMED
#: "BlendPose_0". Matching the displayed spelling finds nothing and reports
#: an empty graph, which is the same as reporting a correct one.
#:
#: The number after the underscore is the pin INDEX, not the reading order.
POSE_PIN_PREFIX = "BlendPose_"
TIME_PIN_PREFIX = "BlendTime_"

#: The pin carrying the condition the node switches on.
CONDITION_PIN = "bActiveValue"


def say(line):
    unreal.log("READGRAPH| " + str(line))


def project_dir():
    """The game/ directory, from the Content directory the editor reports."""
    content = unreal.Paths.project_content_dir()
    return os.path.abspath(os.path.join(content, os.pardir))


def content_hash(object_path):
    """SHA-256 of the .uasset behind an object path, or None.

    THE SAME NUMBER GIT LFS RECORDS. `.uasset` is LFS-tracked in this
    repository, so a checkout that has not fetched the content holds a pointer
    file naming this hash. The test that reads this record can therefore compare
    like with like whether or not the real bytes are present.
    """
    package = object_path.split(".")[0]
    if not package.startswith("/Game/"):
        return None

    relative = package[len("/Game/"):] + ".uasset"
    full = os.path.join(unreal.Paths.project_content_dir(), relative)
    if not os.path.isfile(full):
        return None

    digest = hashlib.sha256()
    with open(full, "rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def clip_behind(pin):
    """The animation a pose pin leads to, as (clip name, play rate), or None.

    Follows the one connection on the pin to whatever node is on the other end.
    A pin with nothing on it, or with something that is not a sequence player,
    answers None -- both are legitimate graphs and neither is this reader's
    business to judge.
    """
    connected = pin.list_connected_pins()
    if not connected:
        return None

    node = connected[0].get_owning_node()
    if not isinstance(node, unreal.AnimGraphNode_SequencePlayer):
        return {"node": type(node).__name__, "name": node.get_name()}

    inner = node.get_editor_property("node")
    sequence = inner.get_editor_property("sequence")
    return {
        "clip": sequence.get_name() if sequence else None,
        "play_rate": round(float(inner.get_editor_property("play_rate")), 6),
        "node": node.get_name(),
    }


def driver_of(pin):
    """What is plugged into a pin, described in words.

    The node title is what the graph shows a person, so it is what a person
    checking this record against the editor will be looking at.
    """
    connected = pin.list_connected_pins()
    if not connected:
        return None

    node = connected[0].get_owning_node()

    # THE VARIABLE'S OWN NAME FIRST. A node title reads back as the node
    # class for a variable getter, which says nothing about which
    # question the blend is asking.
    for attempt in ("variable_reference",):
        try:
            reference = node.get_editor_property(attempt)
            name = str(reference.get_editor_property("member_name"))
            if name:
                return {"variable": name, "node": node.get_name()}
        except Exception:  # noqa: BLE001 - a title is tried next
            pass

    # NO ARGUMENT. unreal.NodeTitleType is not a bound type in this build,
    # and naming it raises AttributeError inside the try above -- which
    # silently fell through to the class name and made every condition
    # read as 'K2Node_VariableGet'.
    try:
        title = str(node.get_node_title()).strip()
        if title and title != type(node).__name__:
            return {"title": title, "node": node.get_name()}
    except Exception:  # noqa: BLE001 - the class name is the floor
        pass

    return {"title": type(node).__name__, "node": node.get_name()}


def read_blend_node(node):
    """One "Blend Poses by bool" node, as what is on each of its pose pins."""
    poses = {}
    times = {}
    condition = None

    for pin in node.list_input_pins():
        # str() BECAUSE get_pin_name RETURNS AN unreal.Name, not a Python
        # string, and unreal.Name has no startswith.
        name = str(pin.get_pin_name())
        if name.startswith(POSE_PIN_PREFIX):
            index = name[len(POSE_PIN_PREFIX):].strip()
            poses[index] = clip_behind(pin)
        elif name.startswith(TIME_PIN_PREFIX):
            index = name[len(TIME_PIN_PREFIX):].strip()
            times[index] = pin.get_pin_value()
        elif name == CONDITION_PIN:
            condition = driver_of(pin)

    return {
        "node": node.get_name(),
        # SPELLED OUT RATHER THAN LEFT AS 0 AND 1. Which index means which
        # branch is the whole of what went wrong, so the record says it in
        # words and a reader never has to remember the engine's flipped sense.
        "true_branch": poses.get("0"),
        "false_branch": poses.get("1"),
        "blend_time_true": times.get("0"),
        "blend_time_false": times.get("1"),
        # WITHOUT THIS THE RECORD SAYS "TRUE" AND NOT TRUE OF WHAT. A
        # reader checking the wiring has to know which question the node
        # is asking before the two branches mean anything.
        "condition": condition,
    }


def read_graph(object_path):
    asset = unreal.load_asset(object_path)
    if asset is None:
        say("{0}: NOT FOUND".format(object_path))
        return None

    blends = asset.get_nodes_of_class(unreal.AnimGraphNode_BlendListByBool)
    players = asset.get_nodes_of_class(unreal.AnimGraphNode_SequencePlayer)

    reading = {
        "asset": object_path,
        "sha256": content_hash(object_path),
        "blend_by_bool_nodes": [read_blend_node(node) for node in blends],
        "sequence_players": [],
    }

    for node in players:
        inner = node.get_editor_property("node")
        sequence = inner.get_editor_property("sequence")
        reading["sequence_players"].append({
            "node": node.get_name(),
            "clip": sequence.get_name() if sequence else None,
            "play_rate": round(float(inner.get_editor_property("play_rate")), 6),
        })

    return reading


def report(reading):
    say("")
    say("{0}".format(reading["asset"]))
    say("  sha256: {0}".format(reading["sha256"]))

    say("  blend by bool nodes: {0}".format(len(reading["blend_by_bool_nodes"])))
    for blend in reading["blend_by_bool_nodes"]:
        say("    {0}".format(blend["node"]))
        say("      TRUE  branch (pin 0): {0}".format(blend["true_branch"]))
        say("      FALSE branch (pin 1): {0}".format(blend["false_branch"]))
        say("      switches on: {0}".format(blend["condition"]))
        say("      blend times: true {0}, false {1}".format(
            blend["blend_time_true"], blend["blend_time_false"]))

    say("  sequence players: {0}".format(len(reading["sequence_players"])))
    for player in reading["sequence_players"]:
        say("    {0}: {1} at {2}".format(
            player["node"], player["clip"], player["play_rate"]))


def main():
    say("==== reading animation graphs ====")

    readings = []
    for object_path in GRAPHS:
        reading = read_graph(object_path)
        if reading is not None:
            readings.append(reading)
            report(reading)

    if not readings:
        say("")
        say("Nothing was read. The Paragon packs and this project's own animation "
            "Blueprints are gitignored or absent, so there is nothing to record.")
        say("==== done ====")
        return

    record = {
        "what_this_is":
            "Written by tools/read_animation_graph.py. What is wired to what "
            "inside each animation Blueprint, and the SHA-256 of the asset it "
            "was read from. tools/tests/test_animation_graph_reading_is_current.py "
            "fails when an asset has changed since. Regenerate with "
            "python tools/run_editor_python.py tools/read_animation_graph.py",
        "what_is_not_covered":
            "The animation graph only. The event graph, where the "
            "condition variables below are computed from the pawn's "
            "ground speed, cannot be enumerated from Python -- "
            "AnimationBlueprintLibrary.GetNodesOfClass refuses anything "
            "that is not an AnimGraphNode. So each condition is recorded "
            "by NAME and how it is computed is unchecked, including the "
            "375 cm/s gait threshold. See issue #430.",
        "the_flipped_boolean_sense":
            "A Blend Poses by bool node puts the TRUE pose on pin index 0 and "
            "the FALSE pose on pin 1, which is the opposite of how the node "
            "reads top to bottom. Both of ABP_Brute's were wired backwards on "
            "the strength of that. See issue #408.",
        "graphs": readings,
    }

    path = os.path.join(project_dir(), RECORD)
    with open(path, "w") as handle:
        json.dump(record, handle, indent=2, sort_keys=True)
        handle.write("\n")

    say("")
    say("wrote {0}".format(path))
    say("==== done ====")


main()
