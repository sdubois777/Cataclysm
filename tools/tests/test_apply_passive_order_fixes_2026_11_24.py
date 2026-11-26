"""`tools/apply_passive_order_fixes_2026_11_24.py`, the passive order fixes of
2026-11-24, checked on small trees built here and on copies of the real ones.

WHY BOTH. The real trees say whether the script does the right thing to the files
it was written for, but only once: after the fixes land in `docs/`, a run on
those files changes nothing and a test that only read them would stop testing
the transformation at all. The small trees below are built in the UNFIXED
state, so the swap, the rewording, the new node and every refusal are exercised
whatever state `docs/` is in. The checks on the real files are written to hold
in both states: the result is fixed, a second run changes nothing, the graph is
untouched, and only the nodes a fix names have different content.
"""

from __future__ import annotations

import copy
import inspect
import json
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

import apply_passive_order_fixes_2026_11_24 as fixes  # noqa: E402
import test_class_passive_trees as tree_rules  # noqa: E402

DOCS = REPO_ROOT / "docs"

#: Every class tree in docs/, the six of today and the Death trees when they join.
REAL_TREES = sorted(DOCS.glob("*_Class_Tree_*.json"))


# --------------------------------------------------------------------------
# Building small trees
# --------------------------------------------------------------------------

def node(node_id: str, name: str, points: int, x: int, y: int, kind: str = "basic",
         colour: str = "#94a3b8", description: str | None = None) -> dict:
    return {
        "id": node_id,
        "position": {"x": x, "y": y},
        "type": kind,
        "data": {"kind": kind, "name": name,
                 "description": description if description is not None
                 else f"{name} does something per point.",
                 "currentPoints": 0, "color": colour, "maxPoints": points},
        "measured": {"width": 16, "height": 16},
        "selected": False,
        "dragging": False,
    }


def edge(source: str, target: str, required: int) -> dict:
    return {"id": f"e_{source}_{target}", "source": source, "target": target,
            "sourceHandle": "n", "targetHandle": "s", "type": "dependency",
            "data": {"requiredPoints": required}, "selected": False}


def tree(cls: str, nodes: list[dict], edges: list[dict]) -> dict:
    return {"version": "1.0",
            "metadata": {"name": f"{cls} Class Tree", "pointBudget": 230},
            "viewport": {"x": 0, "y": 0, "zoom": 1},
            "nodes": nodes, "uiElements": [], "edges": edges}


def masochist() -> dict:
    """The Masochist's path in miniature, unfixed: Blood Price before Deeper Cuts.

    The two are given different colours, as the real file does (the Blood Tithe
    limb is painted apart from the spine), so the test can see the colour stay
    with the position.
    """
    return tree("Masochist", [
        node("root", "Fervour", 1, 0, 0),
        node("spine_007", "Blood Price", 8, 0, -100, colour="#94a3b8"),
        node("spine_009", "Hardened Nerves", 6, 0, -200),
        node("bt_stem1", "Deeper Cuts", 10, 0, -300, colour="#cbd5e1"),
        node("bt_b2", "Grand Tithe", 6, 0, -400, colour="#cbd5e1",
             description=fixes.GRAND_TITHE.before),
        node("bt_kB", "Exsanguinate", 1, 0, -500, kind="keystone", colour="#cbd5e1"),
    ], [
        edge("root", "spine_007", 1),
        edge("spine_007", "spine_009", 4),
        edge("spine_009", "bt_stem1", 2),
        edge("bt_stem1", "bt_b2", 4),
        edge("bt_b2", "bt_kB", 6),
    ])


def necromancer() -> dict:
    """The Necromancer's Ossuary and Legion in miniature, unfixed.

    First Among the Dead opens at 30 points spent, as in the draft, so the new
    node it leads to opens at 40.
    """
    gathered = next(f for f in fixes.FIXES["Necromancer"]
                    if isinstance(f, fixes.Reword) and f.node == "Gathered Remains")
    return tree("Necromancer", [
        node("root", "Fervour", 1, 0, 0),
        node("os_stem0", "Bone Fuse", 6, 450, 516),
        node("os_stem1", "Mantle of Bone", 6, 450, 384),
        node("os_a0", "Gravefinder", 10, 300, 252),
        node("os_a1", "Gathered Remains", 8, 300, 120, description=gathered.before),
        node("lg_path", "Long Road", 40, -450, -12),
        node("lg_a1", "First Among the Dead", 10, -600, -408),
        node("lg_a2", "Master's Edge", 8, -600, -540),
    ], [
        edge("root", "os_stem0", 1),
        edge("os_stem0", "os_stem1", 2),
        edge("os_stem1", "os_a0", 2),
        edge("os_a0", "os_a1", 2),
        edge("root", "lg_path", 1),
        edge("lg_path", "lg_a1", 29),
        edge("lg_a1", "lg_a2", 2),
    ])


SABOTEUR_KEYSTONES = ("Overwhelming Presence", "Precision Placement", "Calculated Chaos",
                      "Mechanical Familiar")
SABOTEUR_OATHS = (("First Oath", 25), ("Second Oath", 50), ("Third Oath", 100),
                  ("Final Oath", 200))


def saboteur() -> dict:
    """The six Saboteur nodes that state a count cap, and its four oaths, unfixed.

    The oaths are as the real file has them: `options` empty and the tier in
    `pointThreshold` only, with no `isDecision`, `threshold` or `selectedOption`.
    """
    before = {f.node: f.before for f in fixes.SABOTEUR_CAPS}
    nodes = [
        node("root", "Fervour", 1, 0, 0),
        node("trunk_006", "Bag of Tricks", 3, 0, -100, description=before["Bag of Tricks"]),
        node("qty_001", "Minefield", 8, 0, -200, description=before["Minefield"]),
    ]
    edges = [edge("root", "trunk_006", 1), edge("trunk_006", "qty_001", 2)]
    for index, name in enumerate(SABOTEUR_KEYSTONES):
        nodes.append(node(f"k{index}", name, 1, 100 * index, -300, kind="keystone",
                          colour="#cbd5e1", description=before[name]))
        edges.append(edge("qty_001", f"k{index}", 8))
    for name, tier in SABOTEUR_OATHS:
        oath = node(f"capstone_{tier}", name, 1, 0, 400 + tier, kind="capstone",
                    colour="#cbd5e1",
                    description=f"Unlocks at {tier} points spent. Choose one of three oaths.")
        oath["data"]["options"] = []
        oath["data"]["pointThreshold"] = tier
        nodes.append(oath)
    return tree("Saboteur", nodes, edges)


def chain(cls: str, names: list[str], points: list[int]) -> dict:
    """A straight path through `names`, in that order, each edge asking for 2."""
    nodes = [node("root", "Fervour", 1, 0, 0)]
    edges = []
    previous = "root"
    for index, (name, held) in enumerate(zip(names, points, strict=True)):
        node_id = f"n{index}"
        nodes.append(node(node_id, name, held, 0, -100 * (index + 1)))
        edges.append(edge(previous, node_id, 1 if previous == "root" else 2))
        previous = node_id
    return tree(cls, nodes, edges)


def by_name(data: dict, name: str) -> dict:
    found = [n for n in data["nodes"] if n["data"]["name"] == name]
    assert len(found) == 1, f"{len(found)} nodes named {name!r}"
    return found[0]


def write(path: pathlib.Path, data: dict, newline: str = "\r\n") -> bytes:
    raw = (json.dumps(data, indent=2).replace("\n", newline) + newline).encode("utf-8")
    path.write_bytes(raw)
    return raw


# --------------------------------------------------------------------------
# A swap: content moves, the graph stays
# --------------------------------------------------------------------------

def test_the_masochist_swap_puts_the_health_cost_first_and_moves_only_content():
    data = masochist()
    before = copy.deepcopy(data)

    changes = fixes.apply_fixes(data, "Masochist")

    assert changes, "the unfixed miniature Masochist was left as it was"
    # THE ORDER THE OWNER APPROVED: the cost, then the node that pays for it.
    assert fixes.is_upstream(data, by_name(data, "Deeper Cuts")["id"],
                             by_name(data, "Blood Price")["id"])
    assert by_name(data, "Deeper Cuts")["id"] == "spine_007"
    assert by_name(data, "Blood Price")["id"] == "bt_stem1"

    # THE CONTENT MOVED WHOLE: name, sentence and size travel together.
    old_blood = by_name(before, "Blood Price")["data"]
    old_cuts = by_name(before, "Deeper Cuts")["data"]
    for field in ("description", "maxPoints", "kind", "currentPoints"):
        assert by_name(data, "Blood Price")["data"][field] == old_blood[field]
        assert by_name(data, "Deeper Cuts")["data"][field] == old_cuts[field]

    # AND THE GRAPH DID NOT: same identifiers, positions, kinds and edges.
    assert [(n["id"], n["position"], n["type"]) for n in data["nodes"]] == \
        [(n["id"], n["position"], n["type"]) for n in before["nodes"]]
    assert data["edges"] == before["edges"]


def test_a_swap_leaves_the_colour_with_the_position():
    """The judgement in the script's docstring: colour is how the authoring tool
    paints a limb, so it belongs to where the node is drawn."""
    data = masochist()
    fixes.apply_fixes(data, "Masochist")
    colours = {n["id"]: n["data"]["color"] for n in data["nodes"]}
    original = {n["id"]: n["data"]["color"] for n in masochist()["nodes"]}
    assert colours == original


def test_a_swap_keeps_the_order_of_the_fields_in_the_file():
    """So the file's diff is the values alone, not the same field moved."""
    data = masochist()
    fixes.apply_fixes(data, "Masochist")
    for after, before in zip(data["nodes"], masochist()["nodes"], strict=True):
        assert list(after["data"]) == list(before["data"])


def test_grand_tithe_is_lowered_to_a_figure_deeper_cuts_reaches():
    """Deeper Cuts tops out at 0.25% a point times 10 points: 2.5%. The fixed
    threshold has to be below that or the limb's own cost never satisfies it."""
    data = masochist()
    fixes.apply_fixes(data, "Masochist")
    sentence = by_name(data, "Grand Tithe")["data"]["description"]
    assert sentence == fixes.GRAND_TITHE.after
    threshold = float(sentence.split("above ")[1].split("%")[0])
    cuts = by_name(data, "Deeper Cuts")["data"]
    assert threshold < 0.25 * cuts["maxPoints"], (
        f"Grand Tithe asks for a cost above {threshold}% and Deeper Cuts reaches "
        f"{0.25 * cuts['maxPoints']}% at most")


@pytest.mark.parametrize("cls, first, then", [
    ("Masochist", "Deeper Cuts", "Blood Price"),
    ("Soul Collector", "Soul Fuel", "Spent Breath"),
    ("Shadow", "Envenom", "Death's Patience"),
])
def test_every_swap_puts_the_named_node_first(cls, first, then):
    assert fixes.Order(first, then) in fixes.FIXES[cls]
    data = chain(cls, [then, "Between", first], [6, 4, 12])
    if cls == "Masochist":
        # The Masochist's other fix needs its node to be there.
        data["nodes"].append(node("gt", "Grand Tithe", 6, 0, -900,
                                  description=fixes.GRAND_TITHE.before))
        data["edges"].append(edge("n2", "gt", 2))

    fixes.apply_fixes(data, cls)

    assert fixes.is_upstream(data, by_name(data, first)["id"], by_name(data, then)["id"])
    assert by_name(data, first)["data"]["maxPoints"] == 12
    assert by_name(data, then)["data"]["maxPoints"] == 6


# --------------------------------------------------------------------------
# The Necromancer
# --------------------------------------------------------------------------

def test_gathered_remains_keeps_only_its_remains_half():
    data = necromancer()
    fixes.apply_fixes(data, "Necromancer")
    sentence = by_name(data, "Gathered Remains")["data"]["description"]
    # The remains half as the 15-keystone merge wrote it (ruled 2026-11-24).
    assert sentence == ("Remains last 25% longer per point, so at 8 points they last "
                        "three times as long: 30 seconds rather than 10.")
    assert "Abomination" not in sentence
    assert by_name(data, "Gathered Remains")["data"]["maxPoints"] == 8


def test_the_abomination_half_is_a_node_of_its_own_that_opens_at_forty_points():
    data = necromancer()
    fixes.apply_fixes(data, "Necromancer")

    new = by_name(data, fixes.MANY_MADE_ONE.name)
    assert new["type"] == "basic"
    assert new["data"]["maxPoints"] == 8
    # THE SENTENCE THAT LEFT GATHERED REMAINS, word for word.
    assert new["data"]["description"] == (
        "Your Abomination rises sooner: each point takes 2 off the percentage of a "
        "floor's starting creatures you must kill, from 50% down to 34% at 8 points.")
    parent = by_name(data, "First Among the Dead")
    joining = [e for e in data["edges"] if e["target"] == new["id"]]
    assert [(e["source"], e["data"]["requiredPoints"]) for e in joining] == \
        [(parent["id"], parent["data"]["maxPoints"])]
    assert fixes.least_points_to_open(data)[new["id"]] >= 40
    # One column out and one row up from its parent, and in the parent's shape.
    assert new["position"] == {"x": -750, "y": -540}
    assert set(new) == set(parent)


def test_gravefinder_moves_to_directly_after_bone_fuse():
    data = necromancer()
    fixes.apply_fixes(data, "Necromancer")
    bone_fuse = by_name(data, "Bone Fuse")["id"]
    after = [e["target"] for e in data["edges"] if e["source"] == bone_fuse]
    assert after == [by_name(data, "Gravefinder")["id"]]
    assert fixes.is_upstream(data, by_name(data, "Gravefinder")["id"],
                             by_name(data, "Mantle of Bone")["id"])


def test_the_new_node_is_refused_where_its_place_is_taken():
    data = necromancer()
    data["nodes"].append(node("squatter", "In The Way", 4, -760, -530))
    data["edges"].append(edge("lg_a2", "squatter", 2))
    with pytest.raises(fixes.Refused, match="on top of"):
        fixes.apply_fixes(copy.deepcopy(data), "Necromancer")
    # AND A CHOSEN PLACE IS USED.
    fixes.apply_fixes(data, "Necromancer", new_node_position=(-900, -540))
    assert by_name(data, fixes.MANY_MADE_ONE.name)["position"] == {"x": -900, "y": -540}


def test_the_new_node_is_refused_if_it_would_open_before_forty_points():
    data = necromancer()
    next(e for e in data["edges"] if e["target"] == "lg_a1")["data"]["requiredPoints"] = 2
    with pytest.raises(fixes.Refused, match="at least 40"):
        fixes.apply_fixes(data, "Necromancer")


def test_gravefinder_is_refused_when_something_else_sits_after_bone_fuse():
    data = necromancer()
    by_name(data, "Mantle of Bone")["data"]["name"] = "Somebody Else"
    with pytest.raises(fixes.Refused, match="Mantle of Bone"):
        fixes.apply_fixes(data, "Necromancer")


# --------------------------------------------------------------------------
# The Saboteur: count caps reworded, and the oaths' decision fields
# --------------------------------------------------------------------------

def test_no_saboteur_node_states_a_count_cap_after_the_fix():
    """The owner's ruling of 2026-11-20: nothing that reserves Fervour has a count
    cap. Every one of the six said "maximum active ..." or "trap limit"."""
    data = saboteur()
    fixes.apply_fixes(data, "Saboteur")
    for fix in fixes.SABOTEUR_CAPS:
        sentence = by_name(data, fix.node)["data"]["description"]
        assert sentence == fix.after
        for cap in ("maximum active", "trap limit", "gadget limit", "+1 to maximum"):
            assert cap not in sentence, f"{fix.node} still says {cap!r}: {sentence}"


def test_a_sentence_a_later_ruling_wrote_is_left_alone_and_any_other_refused():
    """2026-11-25: the trap timing ruling reworded Minefield past this fix's
    sentence. A node reading a sentence in `later` is fixed already; a node
    reading anything else that is neither `before` nor `after` is still refused."""
    (minefield,) = [fix for fix in fixes.SABOTEUR_CAPS if fix.node == "Minefield"]
    assert minefield.later, "Minefield names the trap timing ruling's sentence"
    data = saboteur()
    by_name(data, "Minefield")["data"]["description"] = minefield.later[0]
    assert fixes.apply_reword(data, minefield, "Saboteur") == []
    assert by_name(data, "Minefield")["data"]["description"] == minefield.later[0]
    by_name(data, "Minefield")["data"]["description"] = "Somebody wrote this since."
    with pytest.raises(fixes.Refused, match="Minefield"):
        fixes.apply_reword(data, minefield, "Saboteur")


def test_precision_placement_keeps_its_one_trap_rule():
    """A conversion, not a cap: the coordinating session said to keep it."""
    data = saboteur()
    fixes.apply_fixes(data, "Saboteur")
    sentence = by_name(data, "Precision Placement")["data"]["description"]
    assert sentence.startswith("You may only have one active trap at a time.")


def test_a_raised_cap_becomes_a_reduced_reserve_sized_by_the_rulings_formula():
    """reduction = N / (C + N), C = 20 traps or about 6.7 gadgets on a pool of 100
    (docs/DECISIONS.md, 2026-11-20). Each written figure is within two points of
    what the formula gives for the count the node used to add."""
    def percent(sentence: str, kind: str) -> float:
        tail = sentence.lower().split(f"your {kind} reserve ")[1]
        return float(tail.split("%")[0])

    data = saboteur()
    fixes.apply_fixes(data, "Saboteur")
    bag = by_name(data, "Bag of Tricks")["data"]
    assert abs(percent(bag["description"], "traps") * bag["maxPoints"]
               - 100 * 3 / (20 + 3)) <= 2
    assert abs(percent(bag["description"], "gadgets") * bag["maxPoints"]
               - 100 * 3 / (100 / 15 + 3)) <= 2
    assert abs(percent(by_name(data, "Minefield")["data"]["description"], "traps")
               - 100 * 1 / (20 + 1)) <= 2
    assert abs(percent(by_name(data, "Mechanical Familiar")["data"]["description"], "gadgets")
               - 100 * 1 / (100 / 15 + 1)) <= 2


def test_the_saboteur_oaths_gain_the_decision_fields_and_keep_their_options():
    data = saboteur()
    fixes.apply_fixes(data, "Saboteur")
    for name, tier in SABOTEUR_OATHS:
        body = by_name(data, name)["data"]
        assert body["isDecision"] is True
        assert body["threshold"] == tier == body["pointThreshold"]
        assert body["selectedOption"] is None
        assert body["options"] == []
        # In the order the Berserker's capstones carry them.
        assert list(body)[-4:] == ["pointThreshold", "isDecision", "threshold",
                                   "selectedOption"]


def test_an_oath_whose_threshold_is_not_its_tier_is_refused():
    data = saboteur()
    by_name(data, "Third Oath")["data"]["pointThreshold"] = 75
    with pytest.raises(fixes.Refused, match="Third Oath"):
        fixes.apply_fixes(data, "Saboteur")


def test_an_oath_already_carrying_another_decision_value_is_refused():
    data = saboteur()
    by_name(data, "First Oath")["data"]["isDecision"] = False
    with pytest.raises(fixes.Refused, match="isDecision"):
        fixes.apply_fixes(data, "Saboteur")


def test_only_applies_the_named_trees_fixes(tmp_path):
    masochist_file = tmp_path / "Masochist_Class_Tree_Final.json"
    masochist_raw = write(masochist_file, masochist())
    saboteur_file = tmp_path / "Saboteur_Class_Tree_Final.json"
    write(saboteur_file, saboteur())

    assert fixes.main(["--only", "Saboteur", str(masochist_file), str(saboteur_file)]) == 0

    assert masochist_file.read_bytes() == masochist_raw
    fixed = json.loads(saboteur_file.read_text(encoding="utf-8"))
    assert fixes.apply_fixes(fixed, "Saboteur") == []


# --------------------------------------------------------------------------
# Idempotent, and refusing rather than guessing
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cls, build", [
    ("Masochist", masochist),
    ("Necromancer", necromancer),
    ("Saboteur", saboteur),
])
def test_a_second_run_changes_nothing(cls, build):
    data = build()
    assert fixes.apply_fixes(data, cls)
    once = copy.deepcopy(data)
    assert fixes.apply_fixes(data, cls) == []
    assert data == once


def test_a_missing_node_is_refused():
    data = masochist()
    data["nodes"] = [n for n in data["nodes"] if n["data"]["name"] != "Deeper Cuts"]
    data["edges"] = [e for e in data["edges"] if "bt_stem1" not in (e["source"], e["target"])]
    with pytest.raises(fixes.Refused, match="no node is named 'Deeper Cuts'"):
        fixes.apply_fixes(data, "Masochist")


def test_a_sentence_changed_since_the_approval_is_refused():
    data = masochist()
    by_name(data, "Grand Tithe")["data"]["description"] = "Something else entirely."
    with pytest.raises(fixes.Refused, match="neither the sentence"):
        fixes.apply_fixes(data, "Masochist")


def test_two_nodes_on_no_common_path_are_refused():
    data = masochist()
    # Deeper Cuts hangs off the root instead, beside Blood Price rather than below it.
    data["edges"] = [e for e in data["edges"] if e["target"] != "bt_stem1"]
    data["edges"].append(edge("root", "bt_stem1", 1))
    with pytest.raises(fixes.Refused, match="no common path"):
        fixes.apply_fixes(data, "Masochist")


def test_a_swap_that_resizes_a_keystones_parent_is_refused():
    """The structural rule a swap can break: a keystone asks for its parent in full,
    and a swap that changes the parent's size leaves the edge asking for the old
    size."""
    data = chain("Test", ["Small", "Middle", "Large"], [6, 4, 12])
    data["nodes"].append(node("k", "A Keystone", 1, 0, -900, kind="keystone"))
    data["edges"].append(edge("n0", "k", 6))
    with pytest.raises(fixes.Refused, match="not full investment"):
        fixes.apply_order(data, fixes.Order("Large", "Small"), "Test")
        fixes.check_structure(data, "Test")


def test_a_tree_whose_file_name_and_metadata_disagree_is_refused(tmp_path):
    path = tmp_path / "Bulwark_Class_Tree_Final.json"
    write(path, masochist())
    with pytest.raises(fixes.Refused, match="Masochist"):
        fixes.plan_file(path)


def test_a_sigil_layout_is_recognised_by_its_metadata(tmp_path):
    path = tmp_path / "Masochist_Class_Tree_Sigil.json"
    write(path, masochist(), newline="\n")
    assert fixes.plan_file(path).tree == "Masochist"


# --------------------------------------------------------------------------
# Files: all or nothing, format kept, --check
# --------------------------------------------------------------------------

def test_a_refusal_in_one_file_writes_none_of_them(tmp_path):
    good = tmp_path / "Masochist_Class_Tree_Final.json"
    good_raw = write(good, masochist())
    broken = masochist()
    broken["metadata"]["name"] = "Soul Collector Class Tree"
    bad = tmp_path / "Soul_Collector_Class_Tree_Final.json"
    bad_raw = write(bad, broken)

    assert fixes.main([str(good), str(bad)]) == 2
    assert good.read_bytes() == good_raw
    assert bad.read_bytes() == bad_raw


@pytest.mark.parametrize("newline", ["\r\n", "\n"])
def test_the_file_keeps_its_line_endings_and_changes_only_what_was_fixed(tmp_path,
                                                                          newline):
    path = tmp_path / "Masochist_Class_Tree_Final.json"
    raw = write(path, masochist(), newline=newline)

    assert fixes.main([str(path)]) == 0

    written = path.read_bytes()
    assert written != raw
    text = written.decode("utf-8")
    assert text.endswith(newline)
    if newline == "\r\n":
        assert "\n" not in text.replace("\r\n", ""), "a bare line feed was written"
    else:
        assert "\r" not in text, "a carriage return was written into an LF file"
    changed = [line for line, old in zip(text.splitlines(),
                                         raw.decode("utf-8").splitlines(), strict=True)
               if line != old]
    assert len(changed) == 7, changed  # two names, two sentences, two sizes, Grand Tithe


def test_check_writes_nothing_and_says_whether_anything_would_change(tmp_path):
    path = tmp_path / "Masochist_Class_Tree_Final.json"
    raw = write(path, masochist())
    assert fixes.main(["--check", str(path)]) == 1
    assert path.read_bytes() == raw
    assert fixes.main([str(path)]) == 0
    fixed = path.read_bytes()
    assert fixes.main(["--check", str(path)]) == 0
    assert fixes.main([str(path)]) == 0
    assert path.read_bytes() == fixed


def test_a_tree_with_no_fixes_is_not_rewritten(tmp_path):
    path = tmp_path / "Ravager_Class_Tree_Final.json"
    data = chain("Ravager", ["Blood Price", "Deeper Cuts"], [8, 10])
    raw = write(path, data)
    assert fixes.main([str(path)]) == 0
    assert path.read_bytes() == raw


def test_a_file_whose_format_cannot_be_reproduced_is_refused(tmp_path):
    path = tmp_path / "Masochist_Class_Tree_Final.json"
    path.write_text(json.dumps(masochist(), indent=4), encoding="utf-8")
    with pytest.raises(fixes.Refused, match="byte for byte"):
        fixes.plan_file(path)
    assert fixes.plan_file(path, allow_reformat=True).new_bytes is not None


# --------------------------------------------------------------------------
# The real trees
# --------------------------------------------------------------------------

def test_the_real_trees_are_present():
    """The parametrised test below would collect nothing if the glob broke."""
    names = {p.name for p in REAL_TREES}
    assert {"Bulwark_Class_Tree_Final.json", "Masochist_Class_Tree_Final.json"} <= names


@pytest.mark.parametrize("path", REAL_TREES, ids=lambda p: p.name)
def test_a_real_tree_ends_fixed_with_its_graph_and_its_rules_intact(tmp_path, path):
    target = tmp_path / path.name
    target.write_bytes(path.read_bytes())
    before = json.loads(path.read_text(encoding="utf-8"))

    assert fixes.main([str(target)]) == 0
    after = json.loads(target.read_text(encoding="utf-8"))
    cls = fixes.tree_name(after, target)

    # FIXED: every fix now finds nothing to do.
    assert fixes.apply_fixes(copy.deepcopy(after), cls) == []
    # AND A SECOND RUN DOES NOT TOUCH THE FILE.
    written = target.read_bytes()
    assert fixes.main([str(target)]) == 0
    assert target.read_bytes() == written

    # THE GRAPH IS KEPT: every node where it was, every edge as it was.
    where = {n["id"]: (n["position"], n["type"]) for n in after["nodes"]}
    for n in before["nodes"]:
        assert where[n["id"]] == (n["position"], n["type"])
    assert all(e in after["edges"] for e in before["edges"])

    # ONLY THE NODES A FIX NAMES HAVE DIFFERENT CONTENT.
    named = set()
    for fix in fixes.FIXES.get(cls, ()):
        named |= {getattr(fix, f) for f in ("node", "first", "then", "partner", "name")
                  if hasattr(fix, f)}
        named |= {name for name, _ in getattr(fix, "tiers", ())}
    content = {n["id"]: n["data"] for n in after["nodes"]}
    for n in before["nodes"]:
        if content[n["id"]] != n["data"]:
            assert {n["data"]["name"], content[n["id"]]["name"]} <= named, n["id"]

    # AND EVERY PER-TREE RULE THE TREES IN docs/ ARE HELD TO STILL HOLDS.
    for check in PER_TREE_RULES:
        check((cls, after))


#: The checks in test_class_passive_trees.py that take one tree, as its `tree`
#: fixture hands it: (name, data).
PER_TREE_RULES = [
    check for name, check in vars(tree_rules).items()
    if name.startswith("test_") and callable(check)
    and list(inspect.signature(check).parameters) == ["tree"]
]


def test_the_per_tree_rules_include_the_ones_a_swap_can_break():
    """A filter that matched nothing would make the loop above check nothing."""
    names = {check.__name__ for check in PER_TREE_RULES}
    assert {"test_a_keystone_requires_full_investment_in_its_parent",
            "test_no_edge_asks_for_more_points_than_its_source_can_hold",
            "test_the_spendable_tree_is_one_connected_web"} <= names, names
