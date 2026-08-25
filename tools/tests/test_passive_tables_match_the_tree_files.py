"""The generated passive tables must say what the tree files say.

WHY THIS EXISTS. Issue #50. The four class trees are authored in
`C:\\Projects\\PassiveTreeCreator` and exported into `docs/` as JSON, and the game
cannot read those: `docs/` is not packaged and is not a content directory. So
`tools/generate_datatables.py` turns them into `game/Data/PassiveNodes.csv` and
`game/Data/PassiveEdges.csv`, which become DataTable assets the game loads.

That is a copy, and this project's own history is what makes a copy worth a test:
`CLAUDE.md` records that `sim/cataclysm_sim/scoring.py` drifted silently from its
source twice before `sim/verify_scoring_port.py` was written.

WHAT WOULD GO WRONG WITHOUT IT, and none of it errors:

  a node dropped by the generator      the game holds a tree with a hole in it
  a threshold read from the wrong      a capstone opens at 0 points, so it is
    field                                available from the first point spent
  an edge's requirement lost           a node opens with nothing invested in it
  a capstone option not carried        a choice the design wrote is not offered

WHAT IS DELIBERATELY NOT CHECKED HERE. Whether the tree files themselves follow
the design's rules -- node counts, keystone parents, the connected web.
`test_class_passive_trees.py` does all of that against the source files. This
checks only that what those files say arrives intact.
"""

from __future__ import annotations

import csv
import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO_ROOT / "docs"
NODES_CSV = REPO_ROOT / "game" / "Data" / "PassiveNodes.csv"
EDGES_CSV = REPO_ROOT / "game" / "Data" / "PassiveEdges.csv"

#: The four class trees that exist. The other twenty are issue #24.
CLASS_TREES = ("Berserker", "Bulwark", "Saboteur", "Masochist")

#: The four capstone tiers, from the Passive Class Trees section of the design
#: document.
CAPSTONE_THRESHOLDS = {25, 50, 100, 200}


def load_tree(name: str) -> dict:
    path = DOCS / f"{name}_Class_Tree_Final.json"
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return json.loads(path.read_text(encoding="utf-8"))


def rows_of(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present. Run "
                    "python tools/generate_datatables.py")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def node_rows() -> dict[str, dict]:
    return {row["Name"]: row for row in rows_of(NODES_CSV)}


@pytest.fixture(scope="module")
def edge_rows() -> list[dict]:
    return rows_of(EDGES_CSV)


@pytest.fixture(scope="module")
def trees() -> dict[str, dict]:
    return {name: load_tree(name) for name in CLASS_TREES}


def threshold_in_the_file(node: dict) -> int:
    """What the source file says a capstone's threshold is.

    THREE PLACES TO LOOK AND ALL THREE ARE IN USE. The Masochist tree writes it
    as `threshold`, the Saboteur tree as `pointThreshold`, and the Berserker and
    Bulwark trees carry both. Every one of the four also states it in the
    description. Issue #935 asks for them to be made to agree.
    """
    data = node["data"]
    for field in ("threshold", "pointThreshold"):
        value = data.get(field)
        if isinstance(value, (int, float)):
            return int(value)

    matched = re.search(r"(\d+)\s+points", data.get("description", ""))
    assert matched, f"{data.get('name')!r} states no point threshold anywhere"
    return int(matched.group(1))


def test_every_node_of_every_tree_has_a_row(trees, node_rows):
    for name, data in trees.items():
        for node in data["nodes"]:
            key = f"{name}_{node['id']}"
            assert key in node_rows, (
                f"{name}: node {node['id']!r} has no row in {NODES_CSV.name}. "
                "Run python tools/generate_datatables.py"
            )

    expected = sum(len(data["nodes"]) for data in trees.values())
    assert len(node_rows) == expected, (
        f"{NODES_CSV.name} holds {len(node_rows)} rows and the four tree files "
        f"hold {expected} nodes between them."
    )


def test_a_row_name_is_the_tree_and_the_node_together(trees):
    """Node identifiers are unique only within a tree, so the key needs both.

    MEASURED RATHER THAN ASSUMED: fourteen identifiers are shared by more than
    one of the four trees, `capstone_25` among them. Keying on the identifier
    alone would silently merge them and the game would hold one tree's worth of
    capstones for four trees.
    """
    seen: dict[str, list[str]] = {}
    for name, data in trees.items():
        for node in data["nodes"]:
            seen.setdefault(node["id"], []).append(name)

    shared = {node: names for node, names in seen.items() if len(names) > 1}
    assert shared, (
        "No node identifier is shared between trees any more, which is the "
        "reason a row name carries the tree as well. If that is now genuinely "
        "true this test should be deleted rather than made to pass, and the "
        "row name can be simplified with it."
    )


def test_every_node_carries_its_words_and_its_cap(trees, node_rows):
    for name, data in trees.items():
        for node in data["nodes"]:
            row = node_rows[f"{name}_{node['id']}"]
            body = node["data"]

            assert row["Tree"] == name
            assert row["NodeId"] == node["id"]
            assert row["Kind"] == node["type"]
            assert row["NodeName"] == body["name"], (
                f"{name}/{node['id']}: the row is named {row['NodeName']!r} and "
                f"the tree file says {body['name']!r}"
            )
            assert int(row["MaxPoints"]) == int(body["maxPoints"]), (
                f"{name}/{node['id']}: the row holds {row['MaxPoints']} points "
                f"and the tree file says {body['maxPoints']}"
            )

            # THE DESCRIPTION IS WHITESPACE-COLLAPSED ON THE WAY THROUGH, so it
            # is compared the same way rather than exactly.
            collapsed = re.sub(r"\s+", " ", body["description"]).strip()
            assert row["Description"] == collapsed


def test_a_capstone_carries_its_threshold_and_a_basic_node_carries_none(
        trees, node_rows):
    for name, data in trees.items():
        for node in data["nodes"]:
            row = node_rows[f"{name}_{node['id']}"]
            threshold = int(row["Threshold"])

            if node["type"] != "capstone":
                assert threshold == 0, (
                    f"{name}/{node['id']} is a {node['type']} and carries a "
                    f"threshold of {threshold}. Only a capstone opens on points "
                    "spent; everything else opens on its edges."
                )
                continue

            assert threshold == threshold_in_the_file(node), (
                f"{name}/{node['id']}: the row says {threshold} and the tree "
                f"file says {threshold_in_the_file(node)}"
            )
            assert threshold in CAPSTONE_THRESHOLDS, (
                f"{name}/{node['id']}: {threshold} is not one of the four "
                f"tiers {sorted(CAPSTONE_THRESHOLDS)}"
            )


def test_capstone_options_are_carried_across(trees, node_rows):
    """Three per capstone, where the tree file has them.

    THE SABOTEUR'S FOUR HAVE NONE and its own descriptions say to choose one of
    three. That is issue #935 and it is a gap in the design file rather than in
    the generator, so this checks that whatever the file holds arrives -- none
    included -- rather than requiring three.
    """
    carried = 0
    for name, data in trees.items():
        for node in data["nodes"]:
            if node["type"] != "capstone":
                continue

            row = node_rows[f"{name}_{node['id']}"]
            options = node["data"].get("options", [])

            for index in range(3):
                option = options[index] if index < len(options) else {}
                expected = re.sub(r"\s+", " ", option.get("name", "")).strip()
                assert row[f"Option{index + 1}Name"] == expected, (
                    f"{name}/{node['id']} option {index + 1}: the row says "
                    f"{row[f'Option{index + 1}Name']!r} and the tree file says "
                    f"{expected!r}"
                )
                if expected:
                    carried += 1

    # NOT VACUOUS. Three trees carry three options on each of four capstones,
    # which is 36. Without this the test above would pass just as happily if the
    # generator wrote empty columns for every capstone in every tree.
    assert carried == 36, (
        f"{carried} capstone options were carried across, expected 36: three "
        "trees with three options on each of four capstones. The Saboteur's "
        "four have none, which is issue #935."
    )


def test_every_edge_is_carried_with_its_requirement(trees, edge_rows,
                                                    node_rows):
    by_name = {row["Name"]: row for row in edge_rows}

    expected = 0
    for name, data in trees.items():
        for edge in data["edges"]:
            expected += 1
            key = f"{name}_{edge['id']}"
            assert key in by_name, (
                f"{name}: edge {edge['id']!r} has no row in {EDGES_CSV.name}"
            )
            row = by_name[key]

            assert row["Source"] == f"{name}_{edge['source']}"
            assert row["Target"] == f"{name}_{edge['target']}"
            assert int(row["RequiredPoints"]) == int(
                edge.get("data", {}).get("requiredPoints", 0)), (
                f"{name}/{edge['id']}: the row asks for "
                f"{row['RequiredPoints']} points and the tree file asks for "
                f"{edge.get('data', {}).get('requiredPoints')}"
            )

            # AND BOTH ENDS MUST BE NODES THE GAME HOLDS. An edge naming a node
            # that is not in the node table is a requirement nothing can ever
            # satisfy, so the node behind it is unreachable and nothing says so.
            assert row["Source"] in node_rows
            assert row["Target"] in node_rows

    assert len(edge_rows) == expected


def test_no_capstone_is_wired_into_the_web(trees, edge_rows, node_rows):
    """A capstone tier is reached by total points spent, not along a path.

    `test_class_passive_trees.py` asserts this about the source files. It is
    asserted again here about what the game actually reads, because the two
    could differ only through a generator fault -- and a capstone with an edge
    would be a second, contradictory rule about when it opens.
    """
    capstones = {name for name, row in node_rows.items()
                 if row["Kind"] == "capstone"}

    for row in edge_rows:
        assert row["Source"] not in capstones, (
            f"{row['Name']} leads out of the capstone {row['Source']}"
        )
        assert row["Target"] not in capstones, (
            f"{row['Name']} leads into the capstone {row['Target']}"
        )


def test_each_tree_can_be_started_from_exactly_one_node(trees, edge_rows,
                                                        node_rows):
    """One node per tree has nothing leading to it, and it is where you begin.

    A TREE WITH NONE COULD NEVER BE STARTED and a tree with two would have two
    beginnings, which is not what any of the four is shaped like: the one root
    is the node that unlocks the class's resource.
    """
    targets = {row["Target"] for row in edge_rows}

    for name in trees:
        roots = [key for key, row in node_rows.items()
                 if row["Tree"] == name and row["Kind"] != "capstone"
                 and key not in targets]
        assert len(roots) == 1, (
            f"{name} has {len(roots)} nodes with nothing leading to them: "
            f"{sorted(roots)}"
        )
