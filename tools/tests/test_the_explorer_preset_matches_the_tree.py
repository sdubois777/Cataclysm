"""`TREE_EXPLORER_AS_DESIGNED` is what the Explorer branch actually gives.

WHY THIS FILE EXISTS. Issue #1386. The preset said the branch removes 70 flat
days and adds no floors. Read node by node out of
`docs/Empire_Development_Tree_Final.json`, the branch removes 60 and adds 40, and
the 70 was reached by counting a node with a condition on it (`Opportunist`) and
a node in a different part of the tree (`The Delver`). Every campaign figure ever
quoted against the preset described a player who did not exist.

**THE NUMBERS HERE ARE COMPUTED FROM THE GRAPH, NOT TYPED.** That is the whole
point. A guard that restated the same two constants a second time would pass
after the design document changed, which is exactly what issue #1288 found had
happened to the Architect branch: one factor in the modelled multiplier matched
no node in the graph at all. So this reads the maximum points off each node and
multiplies by the per-point value the node's own text states, and the per-point
values are the only thing written down here.

**AND IT CHECKS THE TEXT IT IS MULTIPLYING.** A points count read from the graph
and multiplied by a per-point value nobody checked is half a guard: the node
could be reworded from "-1 day per point" to "-1% per point" and every total here
would stay the same. Each node therefore carries a phrase from its own
description, and the phrase is asserted before the arithmetic is believed.

WHAT THIS DOES NOT CHECK. That the preset is the right *idea* — whether a
per-active-Cataclysm node belongs in a tier-independent float at all is issue
#1397, and this file follows whatever `EXPECTED_ACTIVE_TYPES` says rather than
arguing it.

`sim/tests/test_explorer_shape.py` checks the analysis script that found the
defect. This checks the constant that carried it. Neither can notice what the
other does.
"""

from __future__ import annotations

import json
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: How many Cataclysm types the preset is written for. `Infinite Depths` pays
#: per active type, and `TuningConfig.active_cataclysm_count` ties that to the
#: difficulty tier, so a preset holding one float is a figure for one tier.
#: Difficulty tier 1 faces one. Issue #1397 is whether that is the right rule.
EXPECTED_ACTIVE_TYPES = 1

#: The Explorer-branch nodes that take a flat number of days off EVERY dungeon,
#: with no condition attached. `phrase` is checked against the node's own text
#: before `per_point` is believed.
#:
#: `Fleet Footed` is a single point worth 5 days rather than 5 points worth 1,
#: which is why the value is per point and the points come from the graph.
FLAT_DAY_NODES = (
    ("Temporal Mastery", 1.0, "-1 Day from dungeon run time per point"),
    ("Overclock", 1.0, "-1 day from dungeon run time per point"),
    ("Pacing", 1.0, "-1 days from dungeon run time per point"),
    ("Fleet Footed", 5.0, "-5 days from dungeon run time"),
)

#: The Explorer-branch nodes that change how deep a dungeon is. `per_point` is
#: floors per point at `EXPECTED_ACTIVE_TYPES`; `Infinite Depths` is the one
#: that depends on that number and it is multiplied in below rather than baked
#: in here.
FLOOR_NODES = (
    ("Architect of Greed", 1.0, "+1 floors to dungeons per point"),
    ("Deep Boring", 1.0, "+1 floors to dungeons per point"),
    ("Exclusionary Mapping", -1.0, "-1 floors to dungeons per point"),
)

#: Floors per point PER ACTIVE CATACLYSM TYPE.
PER_TYPE_FLOOR_NODES = (
    ("Infinite Depths", 2.0, "+2 floors per active Cataclysm type per point"),
)

#: Nodes the preset deliberately counts as ZERO, and the reason. Asserting the
#: reason is still in the node's text is what stops the exclusion outliving it.
EXCLUDED = (
    ("Opportunist", "no other active dungeons",
     "conditional on the board, not on the tree"),
    ("Architectural Insight", "points invested in the Architect branch",
     "zero for a build that spends nothing in Architect"),
    ("Rapid Descent", "reduces the remaining run time",
     "not a flat subtraction"),
    ("Tactical Entry", "are halved", "a multiplier, not a subtraction"),
    ("Sovereign's Haste", "for each active Cataclysm type",
     "per active type and left out; issue #1397"),
)


@pytest.fixture(scope="module")
def nodes() -> list[dict]:
    assert TREE_JSON.is_file(), f"{TREE_JSON} is missing"
    with open(TREE_JSON, encoding="utf-8") as handle:
        return json.load(handle)["nodes"]


@pytest.fixture(scope="module")
def preset():
    from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED
    return TREE_EXPLORER_AS_DESIGNED


def branch_of(node: dict) -> str:
    """Which branch a node sits in, from its position on the radial tree.

    `metadata.description` in the graph says "Branches: Architect (NE), Explorer
    (SE), Treasury (SW), Artisan (NW)". A capstone sits on the central axis and
    is in no branch, which is the distinction that threw `The Delver` out.
    """
    if node["data"].get("kind") == "capstone":
        return "CENTRAL"
    x, y = node["position"]["x"], node["position"]["y"]
    return {("S", "E"): "Explorer", ("N", "E"): "Architect",
            ("S", "W"): "Treasury", ("N", "W"): "Artisan"}[
                ("S" if y > 0 else "N", "E" if x > 0 else "W")]


def find(nodes: list[dict], name: str) -> dict:
    for node in nodes:
        if node["data"].get("name") == name:
            return node
    raise AssertionError(
        f"'{name}' is no longer a node in {TREE_JSON.name}. This guard reads "
        "the Explorer preset's totals off that file, so a renamed or deleted "
        "node has to break it rather than leave it quietly restating a total "
        "for something that no longer exists. Issues #1386 and #1288.")


def points(node: dict) -> int:
    value = node["data"].get("maxPoints")
    assert isinstance(value, int) and value > 0, (
        f"{node['data'].get('name')} has maxPoints {value!r}")
    return value


def check_text(node: dict, phrase: str) -> None:
    text = node["data"].get("description") or ""
    assert phrase.lower() in text.lower(), (
        f"{node['data'].get('name')}'s description is now {text!r} and no "
        f"longer contains {phrase!r}. The totals below multiply this node's "
        "point count by a per-point value taken from that wording, so the "
        "wording changing means the value may have too. Check it by hand and "
        "follow it here.")


def flat_days(nodes: list[dict]) -> float:
    total = 0.0
    for name, per_point, phrase in FLAT_DAY_NODES:
        node = find(nodes, name)
        assert branch_of(node) == "Explorer", (
            f"{name} is now in the {branch_of(node)} branch")
        check_text(node, phrase)
        total += points(node) * per_point
    return total


def floor_delta(nodes: list[dict], active_types: int) -> float:
    total = 0.0
    for name, per_point, phrase in FLOOR_NODES:
        node = find(nodes, name)
        assert branch_of(node) == "Explorer", (
            f"{name} is now in the {branch_of(node)} branch")
        check_text(node, phrase)
        total += points(node) * per_point
    for name, per_point, phrase in PER_TYPE_FLOOR_NODES:
        node = find(nodes, name)
        assert branch_of(node) == "Explorer", (
            f"{name} is now in the {branch_of(node)} branch")
        check_text(node, phrase)
        total += points(node) * per_point * active_types
    return total


# ---------------------------------------------------------------------------
# The two numbers
# ---------------------------------------------------------------------------

class TestThePresetIsWhatTheGraphSays:
    def test_the_flat_days_are_the_branchs_unconditional_nodes(
            self, nodes, preset):
        derived = flat_days(nodes)
        assert preset.run_days_flat == derived, (
            f"TREE_EXPLORER_AS_DESIGNED removes {preset.run_days_flat:g} flat "
            f"days and the Explorer branch's unconditional nodes remove "
            f"{derived:g}. It said 70 against 60 until issue #1386, because it "
            "counted Opportunist, which has a condition in its own text, and "
            "The Delver, which is a Tier 1 capstone option rather than an "
            "Explorer node. Do not change the constant to match a new total "
            "without reading why the total moved.")

    def test_the_floor_delta_is_the_branchs_depth_nodes(self, nodes, preset):
        derived = floor_delta(nodes, EXPECTED_ACTIVE_TYPES)
        assert preset.floor_delta == derived, (
            f"TREE_EXPLORER_AS_DESIGNED adds {preset.floor_delta:+g} floors "
            f"and the Explorer branch's depth nodes add {derived:+g} at "
            f"{EXPECTED_ACTIVE_TYPES} active Cataclysm type(s). It was 0 until "
            "issue #1386, which credited the branch with none of them.")

    def test_the_derived_totals_are_the_ones_the_comment_states(self, nodes):
        """The comment above the constant writes both totals out node by node.

        A reader checks the comment, not the arithmetic, so the comment has to
        be the thing that is guarded. Issue #1288's incident was a comment that
        went on stating a total after the node behind it stopped existing.
        """
        assert flat_days(nodes) == 60.0
        assert floor_delta(nodes, EXPECTED_ACTIVE_TYPES) == 40.0


class TestTheExclusionsStillHaveTheirReasons:
    """Each node the preset counts as zero, and the words that justify it.

    Without this the exclusions are a list somebody wrote once. `Opportunist` is
    excluded because its text carries a condition; if that condition were
    removed from the design the exclusion would be wrong and nothing would say
    so.
    """

    @pytest.mark.parametrize("name,phrase,why", EXCLUDED)
    def test_the_reason_is_still_in_the_nodes_own_text(
            self, nodes, name, phrase, why):
        node = find(nodes, name)
        check_text(node, phrase)

    def test_the_delver_is_not_an_explorer_node(self, nodes):
        """The other excluded term, and it is excluded for a different reason:
        not what it says but where it is."""
        holder = None
        for node in nodes:
            for option in node["data"].get("options") or []:
                if option["name"] == "The Delver":
                    holder = node
        assert holder is not None, (
            "'The Delver' is no longer a capstone option in "
            f"{TREE_JSON.name}. The preset excludes it because it is one of "
            "three exclusive Tier 1 capstone choices rather than an Explorer "
            "node, so if it became one the preset would need it.")
        assert branch_of(holder) == "CENTRAL"
        assert holder["data"].get("isDecision") is True
        assert len(holder["data"]["options"]) == 3


class TestTheTierAssumptionIsStated:
    def test_the_preset_understates_the_branch_above_tier_one(self, nodes):
        """**The limitation, asserted so it cannot be forgotten.**

        `Infinite Depths` pays per active Cataclysm type and a single float
        cannot follow that, so the preset is a tier 1 figure that
        `sim/experiments.py` nonetheless runs at every tier. This states the
        size of the gap rather than leaving it to a comment. Issue #1397.
        """
        at_one = floor_delta(nodes, 1)
        at_eight = floor_delta(nodes, 8)
        assert at_one == 40.0
        assert at_eight == 180.0
        assert at_eight > at_one, (
            "Infinite Depths no longer scales with the active Cataclysm count, "
            "so the preset may no longer need to be a per-tier figure at all. "
            "Issue #1397.")

    def test_the_active_type_count_is_the_difficulty_tier(self):
        """Where `EXPECTED_ACTIVE_TYPES` comes from, checked rather than
        assumed: the model derives the active count from the tier."""
        from dataclasses import replace

        from cataclysm_sim.config import TuningConfig

        assert replace(TuningConfig(), tier=EXPECTED_ACTIVE_TYPES
                       ).active_cataclysm_count() == EXPECTED_ACTIVE_TYPES
