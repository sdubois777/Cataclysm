"""The two descriptions of the empire passive tree, compared node by node.

WHY THIS EXISTS. Issue #25. The empire passive tree is described twice and the
two descriptions are different sizes:

    docs/Empire_Development_Tree_Final.json   159 nodes, 158 edges, the node
                                              graph the passive tree editor
                                              reads and writes
    docs/Empire_Skill_Tree_Keystones.md       105 bullets of prose covering the
                                              12 keystones, the 4 branch
                                              capstones and the 4 quadrants

Nothing said which one was the tree. That mattered because two open balance
issues, #4 and #5, are about branches of it, and `sim/cataclysm_sim/config.py`
models the whole tree as five scalars. You cannot argue about whether a branch is
worth its points while the list of what is in it is ambiguous.

WHAT THE COMPARISON FOUND. The graph is newer, by its own metadata: 2026-03-05
against the prose's 2026-02-10. It is also larger. The prose's bullets all name a
node in the graph; the graph has 68 names the prose never mentions. So the prose
is an earlier draft of the same tree rather than a rival description of it, and
the graph is authoritative. That is written into `docs/README.md` and into the
prose file's own header.

THREE PROSE BULLETS USED TO NAME NOTHING IN THE GRAPH, and until 2026-08-05 this
file exempted them by name. Issue #260 settled all three: the prose is
brainstorming written before the passive tree editor existed, so an idea in it
with no node was never built rather than lost. They were deleted from the prose,
which is why its bullet count went from 108 to 105, and recorded in full in
`docs/DECISIONS.md`.

THE QUADRANT IS CALLED TREASURY. The prose called it three things -- Treasurer in
its branch list, Tyrant in its capstone list, Treasury in its section heading. The
graph uses Treasury nine times, the other two never, and its on-canvas label reads
TREASURY. Note that the **Treasurer** city upgrade in `game/Data/CityUpgrades.csv`
is a different thing with a similar name and keeps it, so this is checked against
the empire tree files only.

WHAT IS ASSERTED HERE.

    the graph parses, and still has the node and edge counts it had
    every prose bullet names a node in the graph, with no exemptions left
    the three that were removed have not come back, in either file
    what they did is still recorded in docs/DECISIONS.md
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

#: Prose bullets with no node of that name in the graph. **Empty since
#: 2026-08-05, and it should stay empty.** Issue #260 asked whether the three
#: that used to be here were cut deliberately or lost in the rebuild. The
#: project owner answered that the prose is brainstorming written before the
#: passive tree editor existed, so an idea in the prose that is not in the graph
#: was never built rather than lost from it. All three were removed from the
#: prose and recorded in `docs/DECISIONS.md`.
#:
#: A new entry here is a claim that the prose describes a node the tree does not
#: have, which after #260 means the prose has been edited wrongly. Prefer fixing
#: the prose. If something genuinely belongs here, it needs an issue.
PROSE_ONLY: dict[str, str] = {}

#: The three that were removed on 2026-08-05, so a reader of this file can see
#: what the exemption list used to hold without opening the git history.
REMOVED_FROM_PROSE_ON_2026_08_05 = (
    "Bounties", "Weightless Spoils", "The 4 Decision Nodes",
)

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
        + f"\n\nThe graph is authoritative (see {DOCS_README}). Issue #260 "
          "settled what that means for a prose bullet with no node: the prose "
          "is brainstorming written before the passive tree editor existed, so "
          "the idea was never built rather than lost. Remove it from the prose "
          "and record it in docs/DECISIONS.md. Adding it to PROSE_ONLY in this "
          "file needs an issue saying why this one is different. Issues #25 "
          "and #260.")


def test_there_are_no_known_gaps_left(prose_names, graph_names):
    """WHAT THIS USED TO ASSERT. Until 2026-08-05 this was
    test_each_known_gap_is_still_a_gap, parametrized over PROSE_ONLY, and it
    checked that each of the three exemptions was STILL a gap so that resolving
    one forced the list to be updated. Issue #260 resolved all three at once, so
    the property to hold is that the list is empty.

    It is also asserted rather than left implicit because an empty parametrize
    list produces a SKIPPED test, and a skip reads as nothing being wrong."""
    assert PROSE_ONLY == {}, (
        "PROSE_ONLY is not empty. It records prose bullets naming nodes the "
        "tree does not have. Issue #260 settled that such a bullet describes "
        "something never built, so the fix is to remove it from the prose "
        f"rather than exempt it here. Currently exempted: {sorted(PROSE_ONLY)}")


@pytest.mark.parametrize("name", REMOVED_FROM_PROSE_ON_2026_08_05)
def test_a_node_removed_by_issue_260_has_not_come_back(name, prose_names,
                                                       graph_names):
    """The three the exemption list used to hold. Each was removed from the
    prose because the tree never had it, and each would have to come back
    through the passive tree editor at C:\\Projects\\PassiveTreeCreator rather
    than by being typed back into the prose.

    So there are two ways this fails and they need different fixes. The prose
    naming it again is a mistake. The GRAPH naming it is not — that is somebody
    building the idea, and then the prose may say so again and this entry should
    be dropped from the list above."""
    key = normalise(name)
    assert key not in prose_names, (
        f"{PROSE.name} names {name!r} again. Issue #260 removed it because the "
        f"tree never had it and the prose predates the passive tree editor. If "
        f"the node has since been built, check {TREE_JSON.name} first — the "
        f"prose may follow the graph, never lead it.")
    assert key not in graph_names, (
        f"{TREE_JSON.name} now has a node named {name!r}. That is a real "
        f"change rather than a mistake: somebody built an idea issue #260 "
        f"recorded as never built. Remove it from "
        f"REMOVED_FROM_PROSE_ON_2026_08_05 in this file, and if it is "
        f"Weightless Spoils then issue #308 about inventory slots is answered.")


def test_the_removed_nodes_are_recorded_in_the_decision_log(prose):
    """Deleting three ideas out of a design document loses them unless they are
    written down somewhere else. They went into docs/DECISIONS.md in full, so
    the decision is reversible by someone who never saw the prose file.

    This checks the log rather than the prose on purpose. The prose file is the
    thing they were deleted FROM, so it is the wrong place to prove they
    survived.

    IT ALSO CHECKS THE ENTRY, NOT THE WHOLE FILE, AND PROVING THE GUARD IS WHY.
    The first version searched all of docs/DECISIONS.md. Deleting the three
    names from the 2026-08-05 entry did not make it fail, because an entry from
    earlier that day already listed all three while describing them as an open
    question. Every name would have survived the record of what happened to them
    being deleted."""
    log = (REPO_ROOT / "docs" / "DECISIONS.md")
    assert log.is_file(), "docs/DECISIONS.md is missing"
    text = log.read_text(encoding="utf-8")
    heading = ("## 2026-08-05 — Three empire tree ideas in the prose were never "
               "built, not lost")
    assert heading in text, (
        "docs/DECISIONS.md has no entry for issue #260. Three bullets were "
        "deleted from the prose on 2026-08-05 and this entry is the only "
        "remaining copy of what they did.")
    entry = text[text.index(heading):]
    entry = entry[:entry.index("\n---", 1)] if "\n---" in entry[1:] else entry

    # The table ROW, not the name anywhere in the entry, and proving the guard
    # is why twice over. Deleting the Weightless Spoils row still left the name
    # in the entry's closing paragraph about issue #308, so a check for the name
    # passed while the record of what the node DID was gone. The row is the
    # record; the paragraph is a cross-reference.
    rows = {line.split("|")[1].strip().strip("*"): line
            for line in entry.splitlines()
            if line.startswith("|") and line.count("|") >= 4}
    for name in REMOVED_FROM_PROSE_ON_2026_08_05:
        assert name in rows, (
            f"the table in the docs/DECISIONS.md entry for issue #260 has no "
            f"row for {name!r}, which was deleted from {PROSE.name} on "
            f"2026-08-05. That table is the only remaining copy of what it did. "
            f"Rows found: {sorted(rows)}")
    assert "Adds 10 inventory slots" in rows["Weightless Spoils"], (
        "the Weightless Spoils row records the node but not its effect. That "
        "effect is the reason issue #308 exists, so the number matters more "
        "than the name.")


def test_the_prose_no_longer_names_a_tier_after_a_node_that_was_never_built(prose):
    """The Architect quadrant's tier 3 heading read "The Adaptive Bulwark
    (Decision Tier)". It was named after The 4 Decision Nodes, one of the three
    ideas issue #260 confirmed was never built, so the parenthetical described a
    tier that has no decision in it.

    The graph's decision nodes are the four tier capstones and Auto-Loot, none of
    which sits in the Architect quadrant's third tier. The tier keeps the name
    The Adaptive Bulwark; only the claim about what kind of tier it is is gone."""
    assert "Decision Tier" not in prose, (
        f"{PROSE.name} describes a tier as a Decision Tier again. The heading "
        "that said so was named after The 4 Decision Nodes, which issue #260 "
        "confirmed was never built. If a decision node has since been added to "
        "that tier with the passive tree editor, check "
        f"{TREE_JSON.name} first and then update this test.")


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
