"""The two descriptions of the empire passive tree, compared node by node.

WHY THIS EXISTS. Issue #25. The empire passive tree is described twice and the
two descriptions are different sizes:

    docs/Empire_Development_Tree_Final.json   159 nodes, 158 edges, the node
                                              graph the passive tree editor
                                              reads and writes
    docs/Empire_Skill_Tree_Keystones.md       108 bullets of prose covering the
                                              12 keystones, the 4 branch
                                              capstones and the 4 quadrants

Nothing said which one was the tree. That mattered because two open balance
issues, #4 and #5, are about branches of it, and `sim/cataclysm_sim/config.py`
models the whole tree as five scalars. You cannot argue about whether a branch is
worth its points while the list of what is in it is ambiguous.

WHAT THE COMPARISON FOUND. The graph is newer, by its own metadata: 2026-03-05
against the prose's 2026-02-10. It is also larger. 105 of the prose's 108 bullets
name a node in the graph; the graph has 68 names the prose never mentions. So the
prose is an earlier draft of the same tree rather than a rival description of it,
and the graph is authoritative. That is written into `docs/README.md` and into the
prose file's own header.

THE QUADRANT IS CALLED TREASURY. The prose called it three things -- Treasurer in
its branch list, Tyrant in its capstone list, Treasury in its section heading. The
graph uses Treasury nine times, the other two never, and its on-canvas label reads
TREASURY. Note that the **Treasurer** city upgrade in `game/Data/CityUpgrades.csv`
is a different thing with a similar name and keeps it, so this is checked against
the empire tree files only.

WHAT IS ASSERTED HERE.

    the graph parses, and still has the node and edge counts it had
    every prose bullet names a node in the graph, except three known ones
    those three are named individually, so resolving one has to update this file
    both files use one name for the Treasury quadrant, and it is Treasury
    docs/README.md still says which file is authoritative
    the prose file's own header still says it is the commentary

WHAT IS NOT ASSERTED. That the two agree on what a node *does*. They frequently
word the same effect differently, and reconciling the wording is not what issue
#25 asked for. Only the names are compared.
"""

from __future__ import annotations

import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO_ROOT / "docs"
TREE_JSON = DOCS / "Empire_Development_Tree_Final.json"
PROSE = DOCS / "Empire_Skill_Tree_Keystones.md"
DOCS_README = DOCS / "README.md"

#: The counts as issue #25 found them. Pinned rather than derived, because the
#: point of pinning is that a node appearing or disappearing is noticed by a
#: person. Both files change by hand or through the passive tree editor.
NODE_COUNT = 159
EDGE_COUNT = 158

#: Prose bullets with no node of that name in the graph. Each is here by name
#: rather than as a count, so resolving one means editing this list and saying
#: which way it went. Issue #260.
PROSE_ONLY = {
    "bounties":
        "the graph has a node named Bounty granting +5% loot quantity per "
        "point, which is a different effect",
    "weightless spoils":
        "nothing in the graph mentions inventory at all",
    "the 4 decision nodes":
        "the graph has five decision nodes and none of them lets a player pick "
        "a Cataclysm type for resistance",
}

#: What the quadrant is called, and the two names the prose used for it that the
#: graph never has.
QUADRANT = "Treasury"
RETIRED_QUADRANT_NAMES = ("Tyrant", "Treasurer")

#: Roman numerals the graph uses where the prose writes "(Rank N)".
ROMAN = {"I": "1", "II": "2", "III": "3"}


def normalise(name: str) -> str:
    """One spelling for a node named slightly differently in the two files.

    The prose writes point limits and ranks into the name -- "Foundation (Max 10
    pts)", "Field Depot (Rank 2)" -- and prefixes a capstone with its branch.
    The graph writes "Foundation", "Field Depot II" and no prefix. None of that
    is a disagreement about which node it is.
    """
    text = name.strip()
    text = re.sub(r"^(?:DECISION\s+)?NOTABLE:\s*", "", text)
    text = re.sub(r"^(Architect|Explorer|Treasurer|Treasury|Tyrant|Artisan):\s*",
                  "", text)
    text = re.sub(r"\s*\(Max\s*\d+\s*pts?\)", "", text)
    text = re.sub(r"\s*\(Rank\s*(\d+)\)", r" \1", text)
    text = re.sub(r"\s*\(([^)]*?),\s*Max\s*\d+\s*pts?\)", r" (\1)", text)
    text = re.sub(r"\b(I{1,3})\b$", lambda m: ROMAN[m.group(1)], text)
    return re.sub(r"\s+", " ", text).strip().lower()


@pytest.fixture(scope="module")
def graph() -> dict:
    if not TREE_JSON.is_file():
        pytest.skip(f"{TREE_JSON.name} is not present")
    return json.loads(TREE_JSON.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def graph_names(graph: dict) -> dict[str, str]:
    """Every node name and every capstone option name, normalised to original."""
    out: dict[str, str] = {}
    for node in graph["nodes"]:
        data = node["data"]
        out.setdefault(normalise(data["name"]), data["name"])
        for option in data.get("options", []):
            out.setdefault(normalise(option["name"]), option["name"])
    return out


@pytest.fixture(scope="module")
def prose() -> str:
    if not PROSE.is_file():
        pytest.skip(f"{PROSE.name} is not present")
    return PROSE.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def prose_names(prose: str) -> dict[str, str]:
    """Every bolded lead-in of a bullet, which is how the prose names a node."""
    out: dict[str, str] = {}
    for line in prose.splitlines():
        found = re.match(r"\s*-\s+\*\*(.+?):?\*\*", line)
        if found:
            out.setdefault(normalise(found.group(1)), found.group(1).strip())
    return out


# --------------------------------------------------------------------------
# The graph itself
# --------------------------------------------------------------------------

def test_the_node_graph_still_has_the_size_it_was_reconciled_at(graph):
    assert len(graph["nodes"]) == NODE_COUNT, (
        f"{TREE_JSON.name} has {len(graph['nodes'])} nodes, not {NODE_COUNT}. "
        "That is fine if a node was added or removed on purpose -- update the "
        "count here and check the prose and docs/README.md still describe the "
        "tree. Issue #25.")
    assert len(graph["edges"]) == EDGE_COUNT, (
        f"{TREE_JSON.name} has {len(graph['edges'])} edges, not {EDGE_COUNT}.")


def test_every_node_has_a_name_and_a_kind(graph):
    """The comparison below is worthless if a node can be nameless."""
    for node in graph["nodes"]:
        data = node["data"]
        assert data.get("name", "").strip(), f"a node has no name: {node['id']}"
        assert data.get("kind") in {"capstone", "keystone", "basic"}, (
            f"{data['name']} has kind {data.get('kind')!r}")


# --------------------------------------------------------------------------
# The two files against each other
# --------------------------------------------------------------------------

def test_every_prose_bullet_names_a_node_in_the_graph(prose_names, graph_names):
    """The direction that matters. The graph having more is expected -- it is
    newer. The prose describing something the tree does not have is a defect,
    and the three that do are listed above by name."""
    missing = {key: original for key, original in prose_names.items()
               if key not in graph_names and key not in PROSE_ONLY}
    assert not missing, (
        f"{PROSE.name} describes nodes that are not in {TREE_JSON.name}:\n  "
        + "\n  ".join(sorted(missing.values()))
        + f"\n\nThe graph is authoritative (see {DOCS_README}). Either the "
          "prose is describing something that was cut, in which case remove it "
          "there, or a node was lost, in which case add it back with the "
          "passive tree editor. If it is a known gap, add it to PROSE_ONLY in "
          "this file with the reason. Issue #25.")


@pytest.mark.parametrize("key", sorted(PROSE_ONLY), ids=lambda k: k)
def test_each_known_gap_is_still_a_gap(key, prose_names, graph_names):
    """If one is resolved, this fails and forces the list to be updated rather
    than leaving a stale exemption behind."""
    assert key in prose_names, (
        f"{PROSE.name} no longer describes {key!r}. If it was removed on "
        f"purpose, delete it from PROSE_ONLY in this file. Issue #260.")
    assert key not in graph_names, (
        f"{TREE_JSON.name} now has a node named {key!r}, so it is no longer a "
        f"gap. Delete it from PROSE_ONLY in this file. Issue #260.")


def test_the_graph_has_more_names_than_the_prose(prose_names, graph_names):
    """The reason the graph is authoritative. If the prose ever overtakes it,
    the direction of authority recorded in docs/README.md needs re-reading."""
    assert len(graph_names) > len(prose_names), (
        f"{PROSE.name} now names {len(prose_names)} things and "
        f"{TREE_JSON.name} only {len(graph_names)}. The graph was chosen as "
        "authoritative because it was the larger and newer of the two.")


# --------------------------------------------------------------------------
# One name for the quadrant
# --------------------------------------------------------------------------

def test_the_graph_calls_the_quadrant_treasury_and_nothing_else(graph):
    text = json.dumps(graph)
    assert QUADRANT in text
    for retired in RETIRED_QUADRANT_NAMES:
        assert retired not in text, (
            f"{TREE_JSON.name} calls the {QUADRANT} quadrant {retired!r}. The "
            f"graph is what settled this name; if it has changed, change "
            f"{PROSE.name} and {DOCS_README.name} with it. Issue #25.")


def test_the_prose_calls_the_quadrant_treasury_and_nothing_else(prose):
    """It used all three of Treasurer, Tyrant and Treasury, in that order,
    within forty lines of each other."""
    assert f"## {QUADRANT} Quadrant" in prose
    for retired in RETIRED_QUADRANT_NAMES:
        assert retired not in prose, (
            f"{PROSE.name} calls the {QUADRANT} quadrant {retired!r} again. "
            f"{TREE_JSON.name} has never called it that. Issue #25.")


def test_the_city_upgrade_called_treasurer_is_untouched():
    """The refusal above is about the empire tree quadrant. A city upgrade with
    a similar name is a different thing and keeps it, so this confirms the check
    has not become a ban on the word."""
    upgrades = REPO_ROOT / "game" / "Data" / "CityUpgrades.csv"
    if not upgrades.is_file():
        pytest.skip("CityUpgrades.csv is not present")
    assert "Treasurer" in upgrades.read_text(encoding="utf-8-sig")


# --------------------------------------------------------------------------
# The two places that say which file is the tree
# --------------------------------------------------------------------------

def test_the_docs_readme_says_which_file_is_the_tree():
    if not DOCS_README.is_file():
        pytest.skip("docs/README.md is not present")
    # One long line, so the sentence survives being re-wrapped.
    body = " ".join(DOCS_README.read_text(encoding="utf-8").split())
    assert (f"**`{TREE_JSON.name}` is the tree. `{PROSE.name}` "
            f"is commentary on it.**") in body, (
        f"docs/README.md no longer records that {TREE_JSON.name} is "
        f"authoritative for the empire tree. That was the thing issue #25 "
        f"asked for.")


def test_the_prose_file_says_it_is_the_commentary(prose):
    """A reader opening the prose directly must be told, not only a reader who
    opens docs/README.md first."""
    header = prose[:prose.index("### Tier 1")]
    assert "This file is commentary" in header
    assert TREE_JSON.name in header


def test_the_prose_no_longer_claims_google_drive_is_authoritative(prose):
    """Its header said "Source of truth is the Drive doc", which contradicted
    both docs/README.md and CLAUDE.md. Those have said since 2026-08-02 that the
    repository copies are the source of truth and are not synced back."""
    assert "Source of truth is the Drive doc" not in prose, (
        f"{PROSE.name} claims the Google Drive copy is authoritative again. "
        "The repository copies have been the source of truth since 2026-08-02; "
        "see docs/README.md and CLAUDE.md.")
