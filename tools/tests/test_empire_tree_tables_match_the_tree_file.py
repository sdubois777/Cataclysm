"""The generated empire tree tables must say what the tree file says, and say
which nodes can be bought.

WHY THIS EXISTS. Issue #2741, slice 1a. `docs/Empire_Development_Tree_Final.json`
is the empire upgrade tree (the owner, 2026-08-05, #260). The game cannot read
`docs/`, so `tools/generate_datatables.py` turns it into
`game/Data/EmpireTreeNodes.csv` and `game/Data/EmpireTreeEdges.csv`, which become
DataTable assets. That is a copy, and `CLAUDE.md` records a copy in this project
drifting silently twice.

WHAT THE TABLES ADD THAT THE FILE DOES NOT SAY, and what each check here holds:

  a branch for every node        read from where the node sits; held equal to the
                                 file's own metadata, its node identifiers and the
                                 survey's point totals
  a threshold for the four       200 points in the branch (the coordinating
  branch capstones               session, Q2, 2026-11-18)
  which nodes can be bought      the owner's scope ruling of 2026-11-18; every
                                 node and option classified, the totals pinned

WHAT WOULD GO WRONG WITHOUT IT, and none of it errors in the game:

  a node dropped                 the tree the game draws has a hole in it
  an edge's requirement lost     a node opens with nothing invested in it
  a node put in the wrong        a cross-branch node such as Architectural
    branch                         Insight counts the wrong points
  a refused node left buyable    a player spends a point on a node that does
                                   nothing and never will until its system exists
"""

from __future__ import annotations

import collections
import csv
import json
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_FILE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
EDGES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeEdges.csv"

# THE CLASSIFICATION IS THE GENERATOR'S, imported rather than written out a
# second time here, so the checks below are about the generator's lists.
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: The four tier-milestone capstones' thresholds, from the file and the design
#: document's Empire-Wide Upgrades section.
CENTRE_THRESHOLDS = {"Foundations of the Empire": 25, "Edicts of Power": 50,
                     "The Imperial Vanguard": 100, "The Imperial Zenith": 200}

#: Each branch's points, the branch capstone's 1 included. The survey of
#: 2026-11-18 (#2741, first comment) gives 325, 316, 312 and 287 without it.
BRANCH_POINTS = {"Architect": 326, "Explorer": 317, "Treasury": 313,
                 "Artisan": 288, "Centre": 4}


@pytest.fixture(scope="module")
def tree() -> dict:
    return json.loads(TREE_FILE.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def node_rows() -> dict[str, dict]:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


@pytest.fixture(scope="module")
def edge_rows() -> dict[str, dict]:
    with EDGES_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def _centre(node: dict) -> bool:
    return node["data"]["kind"] == "capstone" and bool(node["data"].get("options"))


# ---------------------------------------------------------------------------
# What the file says arrives intact
# ---------------------------------------------------------------------------

def test_the_tables_are_the_generators_output() -> None:
    """The CSV files on disk are what the generator writes now. Without this every
    check below could be passing against a file nobody regenerated."""
    # READ WITH LINE ENDINGS TRANSLATED. A Windows checkout holds the file with
    # CRLF and git stores it with LF; the generator renders LF.
    assert NODES_CSV.read_text(encoding="utf-8") == gen.render_csv(
        gen.empire_tree_nodes()), (
        "game/Data/EmpireTreeNodes.csv is not what the generator writes. Run "
        "python tools/generate_datatables.py")
    assert EDGES_CSV.read_text(encoding="utf-8") == gen.render_csv(
        gen.empire_tree_edges()), (
        "game/Data/EmpireTreeEdges.csv is not what the generator writes. Run "
        "python tools/generate_datatables.py")


def test_every_node_has_a_row_named_for_its_identifier(tree, node_rows) -> None:
    ids = {node["id"] for node in tree["nodes"]}
    assert len(ids) == 159
    assert set(node_rows) == ids


def test_every_node_carries_its_words_its_kind_its_cap_and_its_place(
        tree, node_rows) -> None:
    for node in tree["nodes"]:
        row = node_rows[node["id"]]
        body = node["data"]
        assert row["NodeName"] == body["name"].strip()
        assert row["Description"] == " ".join(body["description"].split())
        assert row["Kind"] == node["type"]
        assert int(row["MaxPoints"]) == body["maxPoints"]
        assert float(row["PositionX"]) == pytest.approx(node["position"]["x"])
        assert float(row["PositionY"]) == pytest.approx(node["position"]["y"])


def test_a_centre_capstone_carries_its_three_options(tree, node_rows) -> None:
    centre = [node for node in tree["nodes"] if _centre(node)]
    assert len(centre) == 4
    for node in centre:
        row = node_rows[node["id"]]
        options = node["data"]["options"]
        assert len(options) == 3
        for index, option in enumerate(options, start=1):
            assert row[f"Option{index}Name"] == option["name"]
            assert row[f"Option{index}Description"] == option["description"]


def test_no_other_node_carries_an_option(tree, node_rows) -> None:
    """Auto-Loot, a keystone, has two options in the file; they are not carried,
    as a class tree keystone's would not be, and it is refused as a whole."""
    for node in tree["nodes"]:
        if _centre(node):
            continue
        row = node_rows[node["id"]]
        assert all(not row[f"Option{index}{part}"]
                   for index in (1, 2, 3)
                   for part in ("Name", "Description", "WaitsOn")), row["NodeName"]


def test_the_thresholds_are_the_files_and_the_rulings(tree, node_rows) -> None:
    for node in tree["nodes"]:
        row = node_rows[node["id"]]
        name = row["NodeName"]
        if _centre(node):
            assert int(row["Threshold"]) == CENTRE_THRESHOLDS[name]
            assert int(row["Threshold"]) == node["data"]["threshold"]
        elif row["Kind"] == "capstone":
            assert int(row["Threshold"]) == 200, name
        else:
            assert int(row["Threshold"]) == 0, name


def test_every_edge_is_carried_with_its_requirement(tree, edge_rows) -> None:
    assert len(tree["edges"]) == 158
    assert set(edge_rows) == {edge["id"] for edge in tree["edges"]}
    for edge in tree["edges"]:
        row = edge_rows[edge["id"]]
        assert row["Source"] == edge["source"]
        assert row["Target"] == edge["target"]
        assert int(row["RequiredPoints"]) == edge["data"]["requiredPoints"]


def test_a_keystones_edge_asks_for_its_parent_in_full(tree, node_rows,
                                                      edge_rows) -> None:
    """The rule the buying code relies on rather than restating: a keystone needs
    its parent full, and the file already writes that into the edge."""
    for edge in edge_rows.values():
        target = node_rows[edge["Target"]]
        if target["Kind"] == "keystone":
            source = node_rows[edge["Source"]]
            assert int(edge["RequiredPoints"]) == int(source["MaxPoints"]), (
                target["NodeName"])


# ---------------------------------------------------------------------------
# The branch, which the file does not state
# ---------------------------------------------------------------------------

def test_the_file_names_the_quadrants_the_generator_reads(tree) -> None:
    assert ("Branches: Architect (NE), Explorer (SE), Treasury (SW), Artisan (NW)"
            in tree["metadata"]["description"])


def test_every_identifier_that_names_a_branch_agrees(node_rows) -> None:
    prefixes = {"_arch_": "Architect", "_expl_": "Explorer",
                "_tres_": "Treasury", "_arts_": "Artisan"}
    checked = 0
    for name, row in node_rows.items():
        for prefix, branch in prefixes.items():
            if prefix in name:
                assert row["Branch"] == branch, row["NodeName"]
                checked += 1
    # A check that read no row would pass on any branch at all. 64 of the 159
    # identifiers name a branch, counted on 2026-11-18.
    assert checked == 64


def test_no_edge_crosses_from_one_branch_to_another(node_rows, edge_rows) -> None:
    for edge in edge_rows.values():
        assert (node_rows[edge["Source"]]["Branch"]
                == node_rows[edge["Target"]]["Branch"]), edge


def test_each_branch_holds_the_points_the_survey_counted(node_rows) -> None:
    points = collections.Counter()
    for row in node_rows.values():
        points[row["Branch"]] += int(row["MaxPoints"])
    assert dict(points) == BRANCH_POINTS
    assert sum(points.values()) == 1248


def test_each_branch_has_one_capstone_at_its_end(node_rows) -> None:
    capstones = collections.Counter(row["Branch"] for row in node_rows.values()
                                    if row["Kind"] == "capstone")
    assert capstones == {"Centre": 4, "Architect": 1, "Explorer": 1,
                         "Treasury": 1, "Artisan": 1}


def test_a_node_on_an_axis_is_refused_rather_than_guessed() -> None:
    node = {"position": {"x": 0.0, "y": -50.0},
            "data": {"kind": "basic", "name": "On the line"}}
    with pytest.raises(gen.DataError, match="sits on an axis"):
        gen._empire_branch(node)


# ---------------------------------------------------------------------------
# Which nodes can be bought: the owner's scope ruling of 2026-11-18
# ---------------------------------------------------------------------------

def _classified(node_rows) -> dict[str, tuple[str, str]]:
    """Every node and centre option by name: (its branch, what it waits on)."""
    out = {}
    for row in node_rows.values():
        if row["Branch"] == gen.EMPIRE_CENTRE:
            for index in (1, 2, 3):
                out[row[f"Option{index}Name"]] = (row["Branch"],
                                                  row[f"Option{index}WaitsOn"])
        else:
            out[row["NodeName"]] = (row["Branch"], row["WaitsOn"])
    return out


def test_every_node_and_option_is_classified_once(node_rows) -> None:
    names = set(_classified(node_rows))
    assert len(names) == 155 + 12
    built = set(gen.EMPIRE_TREE_BUILT)
    refused = set(gen.EMPIRE_TREE_REFUSED)
    assert not built & refused
    assert built | refused == names


def test_a_centre_capstone_itself_waits_on_nothing(node_rows) -> None:
    """It is bought by taking an option, and the options carry the refusal."""
    for row in node_rows.values():
        if row["Branch"] == gen.EMPIRE_CENTRE:
            assert row["WaitsOn"] == ""


def test_a_refused_node_says_what_it_waits_on_and_a_built_one_says_nothing(
        node_rows) -> None:
    for name, (_, waits_on) in _classified(node_rows).items():
        if name in gen.EMPIRE_TREE_BUILT:
            assert waits_on == "", name
        else:
            system = gen.EMPIRE_TREE_REFUSED[name]
            assert waits_on == gen.EMPIRE_TREE_SYSTEMS[system], name


def test_every_system_named_is_one_some_node_waits_on() -> None:
    assert set(gen.EMPIRE_TREE_REFUSED.values()) == set(gen.EMPIRE_TREE_SYSTEMS)


def test_a_built_node_names_one_of_the_five_slices_that_make_it_act() -> None:
    assert set(gen.EMPIRE_TREE_BUILT.values()) == {3, 4, 5, 6, 7}


def test_the_points_that_can_be_bought_and_the_points_that_wait(node_rows) -> None:
    """Pinned so that moving a node between the lists is a decision a reviewer
    sees. A node that gains its system moves out of the refused list and these
    figures move with it. Gold gained its system in #2742: slice 6 of #2741
    (2026-11-20) split the 303 points "gold" and "upgrade cost" held into the
    reason each node really waits on: 189 of them the gold rules slice 6 builds,
    92 of those built in slice 6a, 42 in 6b and 47 in 6c. The owner's rulings
    of 2026-11-20 built Treasury Guard's 10 and Tax Collection's 8 in 6d, and
    Global Credit's and Sovereign Loans' 10 with the debt in 6e. Slice 7
    (2026-11-21) built the 17 that waited on a Treasurer city upgrade: Economic
    Zones' 10, Trade Routes' 5, Commercial Hubs' 1 and The Golden Touch's 1."""
    points = collections.Counter()
    options = collections.Counter()
    for row in node_rows.values():
        if row["Branch"] == gen.EMPIRE_CENTRE:
            for index in (1, 2, 3):
                name = row[f"Option{index}Name"]
                options[gen.EMPIRE_TREE_REFUSED.get(name)
                        or f"slice {gen.EMPIRE_TREE_BUILT[name]}"] += 1
            continue
        name = row["NodeName"]
        points[gen.EMPIRE_TREE_REFUSED.get(name)
               or f"slice {gen.EMPIRE_TREE_BUILT[name]}"] += int(row["MaxPoints"])

    assert dict(points) == {
        "slice 3": 315, "slice 4": 162, "slice 5": 152, "slice 6": 209,
        "slice 7": 17,
        "market": 44, "gold cap": 5, "gold cap and death loss": 20,
        "build time": 5, "fee figure": 2, "cap figure": 1, "crafting": 278,
        "population growth": 10, "loot chest": 8, "bonus dungeon": 8,
        "stash": 3, "modifier reroll": 3, "loot filter": 1,
        "destroy upgrade": 1,
    }
    assert sum(points[f"slice {n}"] for n in (3, 4, 5, 6, 7)) == 855
    assert dict(options) == {"slice 3": 3, "slice 4": 4, "slice 5": 1,
                             "crafting": 2, "slice 6": 1, "prowess ruling": 1}


@pytest.mark.parametrize("name, slice_", [
    # The coordinating session's rulings of 2026-11-18.
    ("Veteran's Edge", 4),      # C4: "Empire XP" is empire upgrade points
    ("Urban Fortification", 3),  # C4: "surge damage" is a resolving dungeon's
    ("Civil Defense", 3),
    ("Imperial Command", 3),
    ("Dungeon Mastery", 5),     # Q7: "dungeon type" is the design's four
    ("Bastion Spirit", 3),      # Q1: built, and capped in slice 3
    ("Infinite Depths", 4),     # the owner: built as designed
    ("Architectural Insight", 4),  # the owner's ruling 2: capped in slice 4
    ("Temporal Efficiency", 5),
    ("Golden Age", 3),
    # The owner's rulings of 2026-11-20, relayed by the coordinating session.
    ("Treasury Guard", 6),      # "1. to your cities" (D9)
    ("Tax Collection", 6),      # "we can adjust the gold prices ... later" (D8)
    # "no gold debt unless there's an option for it in the empire tree": these two.
    ("Global Credit", 6),       # D2, D3
    ("Sovereign Loans", 6),     # D2, D4
    # Slice 7: R1 to R4, a Treasurer city upgrade, which #2781 made buyable.
    ("Commercial Hubs", 7),     # R1
    ("Economic Zones", 7),      # R2
    ("Trade Routes", 7),        # R3
    ("The Golden Touch", 7),    # R4
])
def test_a_ruled_node_is_built_in_its_slice(name, slice_) -> None:
    assert gen.EMPIRE_TREE_BUILT.get(name) == slice_


@pytest.mark.parametrize("name, system", [
    ("Imperial Prowess", "prowess ruling"),   # Q5
    ("Scorched Earth", "destroy upgrade"),    # Q6
    ("Explorer Synergy", "crafting"),         # Q8: its cap waits on crafting
    ("Architectural Synergy", "crafting"),
    ("Field Depot I", "stash"),               # Q4
    ("Field Depot II", "stash"),
    ("Field Depot III", "stash"),
])
def test_a_ruled_node_is_refused_for_its_reason(name, system) -> None:
    assert gen.EMPIRE_TREE_REFUSED.get(name) == system


def test_a_name_in_neither_list_stops_generation(monkeypatch) -> None:
    monkeypatch.delitem(gen.EMPIRE_TREE_BUILT, "Foundation")
    with pytest.raises(gen.DataError, match="'Foundation' is in neither"):
        gen.empire_tree_nodes()


def test_a_name_in_both_lists_stops_generation(monkeypatch) -> None:
    monkeypatch.setitem(gen.EMPIRE_TREE_REFUSED, "Foundation", "market")
    with pytest.raises(gen.DataError, match="'Foundation' is in both"):
        gen.empire_tree_nodes()


def test_a_name_no_node_is_called_stops_generation(monkeypatch) -> None:
    monkeypatch.setitem(gen.EMPIRE_TREE_BUILT, "A Node Nobody Drew", 3)
    with pytest.raises(gen.DataError, match="A Node Nobody Drew"):
        gen.empire_tree_nodes()
