"""`tools/apply_demonic_tree_rework_2026_11_24.py`, the Demonic tree rework of
2026-11-24, checked on copies of the real trees in both directions.

WHY BOTH DIRECTIONS. Once the rework is in `docs/`, a run on those files changes
nothing, and a test that only ran the script on them would stop testing the
transformation. So each real tree is first put BACK to the sentences the rework
was approved against (`unrework` below, the script's own data read the other
way), and the script must turn that back into the file in `docs/` byte for byte.
That holds whatever state `docs/` is in: before the rework the "unreworked"
copy is the file itself.
"""

from __future__ import annotations

import copy
import json
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_demonic_tree_rework_2026_11_24 as rework  # noqa: E402
import apply_passive_order_fixes_2026_11_24 as order_fixes  # noqa: E402

DOCS = REPO_ROOT / "docs"
TREES = ("Masochist", "Ravager", "Ritualist")


def path_of(tree: str) -> pathlib.Path:
    return DOCS / f"{tree}_Class_Tree_Final.json"


def load(tree: str) -> dict:
    return json.loads(path_of(tree).read_text(encoding="utf-8"))


def by_name(data: dict, name: str) -> dict:
    found = [n for n in data["nodes"] if n["data"]["name"] == name]
    assert len(found) == 1, f"{len(found)} nodes named {name!r}"
    return found[0]


def unrework(data: dict, tree: str) -> dict:
    """The tree as it was before the rework: every change read backwards."""
    data = copy.deepcopy(data)
    for fix in reversed(rework.REWORKS[tree]):
        if isinstance(fix, rework.Rework):
            found = [n for n in data["nodes"] if n["data"]["name"] == fix.new_name]
            if found and found[0]["data"]["description"] == fix.after:
                found[0]["data"]["name"] = fix.node
                found[0]["data"]["description"] = fix.before
        elif isinstance(fix, rework.OptionRework):
            capstone = by_name(data, fix.capstone)
            option = capstone["data"]["options"][fix.option - 1]
            if option["name"] == fix.new_name and option["description"] == fix.after:
                option["name"] = fix.before_name
                option["description"] = fix.before
        elif isinstance(fix, rework.SwapToParentOf):
            child = by_name(data, fix.child)
            parents = {e["source"] for e in data["edges"] if e["target"] == child["id"]}
            node = by_name(data, fix.node)
            if node["id"] in parents:
                order_fixes.swap_content(node, by_name(data, fix.partner), tree)
    return data


def write_like(path: pathlib.Path, data: dict, like: pathlib.Path) -> None:
    raw = like.read_bytes()
    form = order_fixes.format_of(raw, json.loads(raw.decode("utf-8")))
    assert form is not None, f"{like.name} cannot be reproduced byte for byte"
    path.write_bytes(form.write(data))


# --------------------------------------------------------------------------
# The real trees
# --------------------------------------------------------------------------

@pytest.mark.parametrize("tree", TREES)
def test_the_unreworked_tree_is_turned_into_the_file_in_docs(tmp_path, tree):
    """The script, run on the approved-against state, writes exactly docs/."""
    target = tmp_path / path_of(tree).name
    write_like(target, unrework(load(tree), tree), path_of(tree))

    assert rework.main([str(target)]) == 0
    assert target.read_bytes() == path_of(tree).read_bytes(), (
        f"the rework of the {tree} tree does not reproduce docs/{path_of(tree).name}; "
        "either a sentence there was edited by hand or the script changed")


@pytest.mark.parametrize("tree", TREES)
def test_the_tree_in_docs_is_already_reworked_and_a_second_run_writes_nothing(tmp_path, tree):
    target = tmp_path / path_of(tree).name
    target.write_bytes(path_of(tree).read_bytes())
    assert rework.apply_rework_to_tree(load(tree), tree) == []
    assert rework.main([str(target)]) == 0
    assert target.read_bytes() == path_of(tree).read_bytes()
    assert rework.main(["--check", str(target)]) == 0


@pytest.mark.parametrize("tree", TREES)
def test_the_rework_changes_no_identifier_kind_position_maximum_or_edge(tree):
    before = unrework(load(tree), tree)
    after = copy.deepcopy(before)
    assert rework.apply_rework_to_tree(after, tree), f"nothing to do on the unreworked {tree}"
    assert rework.graph_of(after) == rework.graph_of(before)


def test_every_change_is_one_the_audits_name_and_none_is_listed_twice():
    for tree, fixes in rework.REWORKS.items():
        names = [getattr(f, "node", None) or getattr(f, "capstone", None) for f in fixes]
        keys = [(n, getattr(f, "option", 0)) for n, f in zip(names, fixes, strict=True)]
        assert len(keys) == len(set(keys)), f"{tree}: a node is changed twice: {keys}"


def test_no_reworked_masochist_sentence_states_a_fraction():
    """The owner's answer to question 4: values below 1 become whole-number steps.
    Before the rework five Masochist nodes stated one (0.5, 0.4, 0.25)."""
    fractional = re.compile(r"\d+\.\d+")
    for fix in rework.MASOCHIST:
        assert not fractional.search(fix.after), (fix, fractional.search(fix.after))


def test_volatile_leads_to_every_one_bursts_and_legion_to_the_swarm():
    """P-B1 and P-B2 of the Ritualist audit: the explosion node now sits before
    the keystone that makes explosions, and the node Volatile left is Legion."""
    data = load("Ritualist")
    parent = {e["target"]: e["source"] for e in data["edges"]}
    ids = {n["id"]: n["data"]["name"] for n in data["nodes"]}
    assert ids[parent[by_name(data, "Every One Bursts")["id"]]] == "Volatile"
    assert ids[parent[by_name(data, "The Swarm")["id"]]] == "Legion"
    assert not [n for n in data["nodes"] if n["data"]["name"] == "Spiteful Ash"]


def test_the_swap_leaves_the_colour_with_the_position():
    before = unrework(load("Ritualist"), "Ritualist")
    colours = {n["id"]: n["data"].get("color") for n in before["nodes"]}
    after = copy.deepcopy(before)
    rework.apply_rework_to_tree(after, "Ritualist")
    assert {n["id"]: n["data"].get("color") for n in after["nodes"]} == colours


def test_the_masochist_tree_is_reworked_after_the_order_fix():
    """The audit was written against the fixed order: Deeper Cuts first."""
    data = load("Masochist")
    assert order_fixes.is_upstream(data, by_name(data, "Deeper Cuts")["id"],
                                   by_name(data, "Blood Price")["id"])
    assert by_name(data, "Deeper Cuts")["id"] == "basic_spine_007"
    assert by_name(data, "Grand Tithe")["data"]["description"] == order_fixes.GRAND_TITHE.after


def test_a_sigil_layout_is_reworked_by_its_metadata(tmp_path):
    """A copy under another file name, with every node moved, is still found."""
    data = unrework(load("Ravager"), "Ravager")
    for node in data["nodes"]:
        node["position"] = {"x": node["position"]["x"] + 7, "y": -node["position"]["y"]}
    target = tmp_path / "Ravager_Class_Tree_Sigil.json"
    target.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    assert rework.main([str(target)]) == 0
    reworked = json.loads(target.read_text(encoding="utf-8"))
    assert by_name(reworked, "Thick Hide")["data"]["description"] == \
        "+1% increased Armor and +1% increased Maximum Health per point."


# --------------------------------------------------------------------------
# Refusals
# --------------------------------------------------------------------------

def test_a_masochist_tree_the_order_fixes_have_not_reached_is_refused(tmp_path):
    data = unrework(load("Masochist"), "Masochist")
    grand = by_name(data, "Grand Tithe")
    grand["data"]["description"] = order_fixes.GRAND_TITHE.before
    with pytest.raises(rework.Refused, match="order fixes"):
        rework.apply_rework_to_tree(data, "Masochist")


def test_a_sentence_changed_since_the_approval_is_refused_and_nothing_is_written(tmp_path):
    data = unrework(load("Ravager"), "Ravager")
    by_name(data, "Braced")["data"]["description"] = "+9% increased Damage Reduction per point."
    target = tmp_path / "Ravager_Class_Tree_Final.json"
    write_like(target, data, path_of("Ravager"))
    written = target.read_bytes()
    assert rework.main([str(target)]) == 2
    assert target.read_bytes() == written


def test_a_missing_node_is_refused():
    data = unrework(load("Ritualist"), "Ritualist")
    by_name(data, "Distance")["data"]["name"] = "Far Away"
    with pytest.raises(rework.Refused, match="Distance"):
        rework.apply_rework_to_tree(data, "Ritualist")


def test_a_new_name_already_taken_is_refused():
    data = unrework(load("Masochist"), "Masochist")
    by_name(data, "Bulk")["data"]["name"] = "Provoked"
    with pytest.raises(rework.Refused, match="Provoked"):
        rework.apply_rework_to_tree(data, "Masochist")


def test_a_capstone_option_moved_out_of_place_is_refused():
    data = unrework(load("Ravager"), "Ravager")
    options = by_name(data, "The First Onslaught")["data"]["options"]
    options[0], options[1] = options[1], options[0]
    with pytest.raises(rework.Refused, match="option"):
        rework.apply_rework_to_tree(data, "Ravager")


def test_a_refusal_in_one_file_writes_none_of_them(tmp_path):
    good = tmp_path / "Ravager_Class_Tree_Final.json"
    write_like(good, unrework(load("Ravager"), "Ravager"), path_of("Ravager"))
    bad_data = unrework(load("Ritualist"), "Ritualist")
    by_name(bad_data, "Unhurried")["data"]["description"] = "something else"
    bad = tmp_path / "Ritualist_Class_Tree_Final.json"
    write_like(bad, bad_data, path_of("Ritualist"))
    before = good.read_bytes()
    assert rework.main([str(good), str(bad)]) == 2
    assert good.read_bytes() == before
