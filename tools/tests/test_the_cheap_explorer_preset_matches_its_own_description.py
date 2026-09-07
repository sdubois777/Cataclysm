"""`TREE_EXPLORER_DAY_NODES_ONLY` is the build its own comment describes.

Issue #1420. The preset is the cheap Explorer sub-build: the branch's four
unconditional speed nodes and none of its five depth nodes, 56 of the branch's
316 points. It is the build every "Explorer maxed" figure this project quoted
before issue #1399 actually describes, and the one the project owner's original
complaint about idle time belongs to.

WHY THIS FILE EXISTS, AND IT IS NOT THE OBVIOUS REASON. The preset had no guard
of its own until now, and it spent 2026-09-07 being something other than what it
said. It was `replace(TREE_EXPLORER_AS_DESIGNED, floor_delta=0.0)` in
`sim/analyse_surge_cadence.py`; `dataclasses.replace` carries across every field
it is not told to change, so when issue #1397 gave the preset it copied a
per-active-Cataclysm-type half for its days and its floors, this one silently
gained both -- a fifth day-removal node, so no longer 56 points, and +20 floors
at difficulty tier 1 rising to +160 at tier 8, when having no depth nodes at all
is its entire reason for existing.

**NOTHING FAILED.** It kept running and kept producing campaign figures under a
comment that had become false, and a person reading the diff found it. Issue
#1416 repaired the copy. Issue #1420 removed the copy: the preset is now written
out field by field in `sim/cataclysm_sim/config.py`, and this file is what checks
it against the design document.

WHAT IS CHECKED, AND WHY EACH PART IS NEEDED:

  1. **The point totals come out of the graph**, not out of a second copy of the
     same numbers. `docs/Empire_Development_Tree_Final.json` gives both 56 and
     316, and the comment that states them is read and compared.
  2. **The walk multiplier is derived from the four nodes' own descriptions.** A
     guard that restated `0.975 ** 55 * 0.88` would pass after the design
     document changed, which is what issue #1288 found had happened to the
     Architect branch.
  3. **The depth nodes are counted in the graph**, so "none of its five" fails if
     the branch ever grows a sixth rather than quietly describing five of six.
  4. **Every field of `EmpireTree` is named in the definition.** That is what
     stops the 2026-09-07 defect coming back in a new shape: a field this build
     does not buy has to be written as not bought, so a field added to the
     dataclass is a decision somebody makes here rather than a default nobody
     saw. `tools/tests/test_a_copied_preset_still_matches_its_comment.py` holds
     the same reasoning for a preset built by copying, and records why an
     inherited-field set is the interesting thing to watch.

`tools/tests/test_the_explorer_preset_matches_the_tree.py` does the equivalent
job for `TREE_EXPLORER_AS_DESIGNED`, the whole branch. Neither can notice what
the other does: the two presets share a walk multiplier and differ in everything
else.
"""

from __future__ import annotations

import ast
import json
import pathlib
import re
from dataclasses import MISSING, fields

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIG = REPO_ROOT / "sim" / "cataclysm_sim" / "config.py"
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: Every number of active Cataclysm types a campaign can face, which is what
#: `TuningConfig.active_cataclysm_count` clamps the difficulty tier to.
ACTIVE_COUNTS = (1, 2, 3, 4, 5, 6, 7, 8)

#: The four nodes the build IS, and a phrase from each one's own description.
#: The phrase is asserted before the per-point value is believed, because a
#: point count multiplied by a value nobody checked is half a guard: the node
#: could be reworded from "-2.5% of run time" to "-2.5% of loot" and every
#: total here would stay the same.
#:
#: `Fleet Footed` is one point worth -12% where the other three are worth -2.5%
#: a point, which is why the value is per point and the points come from the
#: graph.
SPEED_NODES = (
    ("Temporal Mastery", 0.025, "-2.5% of dungeon run time per point"),
    ("Overclock", 0.025, "-2.5% of dungeon run time per point"),
    ("Pacing", 0.025, "-2.5% of dungeon run time per point"),
    ("Fleet Footed", 0.12, "-12% of dungeon run time"),
)

#: The five nodes the build is NOT. Named so that "none of its five depth nodes"
#: is checkable rather than assertable, and counted in the graph below so that a
#: sixth appearing is a failure rather than a quietly wrong five.
DEPTH_NODES = (
    "Architect of Greed",
    "Deep Boring",
    "Infinite Depths",
    "Architectural Insight",
    "Exclusionary Mapping",
)

#: What a depth node's description looks like. **NOT SIMPLY THE WORD "floor"**:
#: nine other Explorer nodes mention floors while changing loot, stash placement
#: or magic find rather than depth -- `Supply Caches`, `Manifest Wealth`, the
#: three `Field Depot` nodes, `Quality over Quantity`, `The High Roller`,
#: `Tactical Entry` and `Rapid Descent`. A looser pattern would report a dozen
#: depth nodes and the count check below would be noise.
DEPTH_PHRASES = re.compile(
    r"floors? to dungeons|floors? per active Cataclysm type"
    r"|Dungeons gain \+\d+ floor", re.IGNORECASE)

#: The one flat-day node left in the branch, which this build does not buy.
#: Issue #1383 made the other four a percentage.
FLAT_DAY_NODE = "Sovereign's Haste"

#: Sentences the comment above the preset must still contain, because the
#: comment is the only description a reader gets and it is hand-typed.
PHRASES = (
    "none of its five depth nodes",
    "IT REMOVES NO FLAT DAYS AT ALL AND ITS SPEED IS ENTIRELY",
    "the sub-build is the same 56 points it always was",
)


# ---------------------------------------------------------------------------
# Reading the design document
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def nodes() -> list[dict]:
    assert TREE_JSON.is_file(), f"{TREE_JSON} is missing"
    with open(TREE_JSON, encoding="utf-8") as handle:
        return json.load(handle)["nodes"]


@pytest.fixture(scope="module")
def preset():
    from cataclysm_sim.config import TREE_EXPLORER_DAY_NODES_ONLY
    return TREE_EXPLORER_DAY_NODES_ONLY


def branch_of(node: dict) -> str:
    """Which branch a node sits in, from its position on the radial tree.

    `metadata.description` in the graph says "Branches: Architect (NE), Explorer
    (SE), Treasury (SW), Artisan (NW)". A capstone sits on the central axis and
    is in no branch at all.
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
        "the cheap Explorer preset's totals off that file, so a renamed or "
        "deleted node has to break it rather than leave it restating a total "
        "for something that no longer exists.")


def points(node: dict) -> int:
    value = node["data"].get("maxPoints")
    assert isinstance(value, int) and value > 0, (
        f"{node['data'].get('name')} has maxPoints {value!r}")
    return value


def check(nodes: list[dict], name: str, phrase: str) -> dict:
    """The node, with its branch and its wording confirmed."""
    node = find(nodes, name)
    assert branch_of(node) == "Explorer", (
        f"{name} is now in the {branch_of(node)} branch, not Explorer")
    text = node["data"].get("description") or ""
    assert phrase.lower() in text.lower(), (
        f"{name}'s description is now {text!r} and no longer contains "
        f"{phrase!r}. The multiplier below multiplies this node's point count "
        "by a per-point value taken from that wording, so the wording changing "
        "means the value may have too. Check it by hand and follow it here.")
    return node


def explorer_branch_points(nodes: list[dict]) -> int:
    """Every point the Explorer branch holds. Capstones are excluded because
    they sit on the central axis and belong to no branch."""
    return sum(node["data"].get("maxPoints") or 0
               for node in nodes if branch_of(node) == "Explorer")


def speed_multiplier(nodes: list[dict]) -> float:
    """What the four speed nodes multiply a walk by, at full investment.

    COMBINED MULTIPLICATIVELY, which is what the project owner ruled on
    2026-09-06: 55 points at -2.5% is `0.975 ** 55` and not `1 - 55 * 0.025`,
    and the second is negative.
    """
    product = 1.0
    for name, per_point, phrase in SPEED_NODES:
        product *= (1.0 - per_point) ** points(check(nodes, name, phrase))
    return product


# ---------------------------------------------------------------------------
# Reading the definition, as source rather than as an object
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def definition() -> ast.Call:
    """The `EmpireTree(...)` call the preset is assigned from.

    READ AS SOURCE AND NOT AS AN OBJECT, because the property issue #1420 asks
    for is about how it is WRITTEN. A preset built by copying another one and a
    preset written out field by field can hold identical values, and only one of
    them can silently change when a neighbour does.
    """
    tree = ast.parse(CONFIG.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        target = node.targets[0]
        if (isinstance(target, ast.Name)
                and target.id == "TREE_EXPLORER_DAY_NODES_ONLY"):
            return node.value
    raise AssertionError(
        "TREE_EXPLORER_DAY_NODES_ONLY is not assigned at module level in "
        f"{CONFIG.relative_to(REPO_ROOT).as_posix()}. Issue #1420 moved it "
        "there out of sim/analyse_surge_cadence.py; if it moved again, follow "
        "it here rather than deleting this file.")


@pytest.fixture(scope="module")
def comment() -> str:
    """The comment block above the preset, flattened.

    FLATTENED BECAUSE THE COMMENTS ARE HARD-WRAPPED. A phrase longer than one
    line is not present in the raw text, so a search for one would report a
    clean file that is not clean, which has cost this project a wrong answer
    more than once.

    Only the leading `#` of each line is stripped, so a `#1420` inside the prose
    survives as written.
    """
    source = CONFIG.read_text(encoding="utf-8")
    anchor = "TREE_EXPLORER_DAY_NODES_ONLY = EmpireTree("
    assert source.count(anchor) == 1, (
        f"{anchor!r} appears {source.count(anchor)} times in config.py")
    block = source[:source.index(anchor)].rsplit("\n\n", 1)[-1]
    lines = [line.lstrip().lstrip("#").lstrip(":").strip()
             for line in block.splitlines()]
    return re.sub(r"\s+", " ", " ".join(lines))


# ---------------------------------------------------------------------------
# 56 of 316
# ---------------------------------------------------------------------------

class TestThePointTotalsAreTheGraphs:
    """**THE TYPED COPY, CHECKED AGAINST THE DESIGN DOCUMENT.** "56 of the
    branch's 316 points" is hand-typed prose, and prose is what goes stale while
    code keeps working."""

    def test_the_four_speed_nodes_cost_fifty_six_points(self, nodes):
        total = sum(points(check(nodes, name, phrase))
                    for name, _per_point, phrase in SPEED_NODES)
        assert total == 56, (
            f"the four unconditional speed nodes cost {total} points in "
            f"{TREE_JSON.name} and the preset's comment says 56.")

    def test_the_explorer_branch_holds_three_hundred_and_sixteen(self, nodes):
        total = explorer_branch_points(nodes)
        assert total == 316, (
            f"the Explorer branch holds {total} points in {TREE_JSON.name} and "
            "the preset's comment says 316.")

    def test_the_comment_states_both_numbers(self, nodes, comment):
        cheap = sum(points(check(nodes, name, phrase))
                    for name, _per_point, phrase in SPEED_NODES)
        branch = explorer_branch_points(nodes)

        # THE POSITIVE CONTROL. An empty comment would satisfy nothing here and
        # would mean the extraction is broken rather than the comment wrong.
        assert len(comment) > 100, (
            "no comment block was found above the preset in config.py; the "
            "extraction is reading the wrong lines")

        assert f"{cheap} of the branch's {branch} points" in comment, (
            f"the comment above TREE_EXPLORER_DAY_NODES_ONLY no longer states "
            f"'{cheap} of the branch's {branch} points'. The graph gives those "
            "two numbers; follow them in the comment rather than deleting this "
            "check.")

    @pytest.mark.parametrize("phrase", PHRASES)
    def test_the_comment_still_says_what_this_file_rests_on(
            self, comment, phrase):
        """Without this, the comment could be rewritten to promise something
        else while every check here went on testing the old promise."""
        assert phrase in comment, (
            f"the comment above TREE_EXPLORER_DAY_NODES_ONLY no longer says "
            f"{phrase!r}, and the checks in this file rest on it. If the preset "
            "changed, change the checks; if the wording changed, follow it "
            "here.")


# ---------------------------------------------------------------------------
# The four nodes it has
# ---------------------------------------------------------------------------

class TestItBuysTheFourSpeedNodes:
    def test_the_walk_multiplier_is_the_four_nodes_at_full_investment(
            self, nodes, preset):
        """**THE FOUR NODES MOVED FIELD ONCE, LEGITIMATELY, ON 2026-09-07**, and
        that is the standard for judging a preset that changes shape.

        They were four fixed-day nodes removing 25, 20, 10 and 5 days. The
        project owner ruled them a percentage of run time on 2026-09-06,
        verbatim "Change to a percentage", and issue #1383 chose -2.5% a point
        and -12% for the keystone. So this build's speed left `run_days_flat`
        and arrived in `run_days_mult` **because the field carrying those four
        nodes moved, not because the build gained or lost any of them**. It is
        the same 56 points either side of that date, which is why the name was
        kept.

        The contrast with the defect of the following day is the whole point of
        keeping both stories: there the build gained nodes it is documented as
        not having, in silence.
        """
        derived = speed_multiplier(nodes)
        assert abs(preset.run_days_mult - derived) < 1e-12, (
            f"TREE_EXPLORER_DAY_NODES_ONLY multiplies a walk by "
            f"{preset.run_days_mult:.6f} and the branch's four speed nodes "
            f"multiply it by {derived:.6f}.")
        assert 0.0 < derived < 1.0

    def test_it_is_the_same_speed_as_the_whole_branch(self, preset):
        """**THE CONTROL ON THE TEST ABOVE, AND THE POINT OF THE BUILD.** All
        four speed nodes are in the whole-branch preset too, so the two must
        agree on the multiplier exactly. What separates the two builds is depth,
        not speed, and a difference here would mean one of them has stopped
        being a subset of the other."""
        from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED
        assert preset.run_days_mult == TREE_EXPLORER_AS_DESIGNED.run_days_mult

    def test_it_removes_no_flat_days_at_any_tier(self, nodes, preset):
        """`Sovereign's Haste` is the branch's only flat-day node since issue
        #1383, it is worth ten points, and buying it would make this build 66
        points rather than 56. That is the exact shape of the 2026-09-07 defect,
        so it is asserted at every tier and not only at tier 1."""
        check(nodes, FLAT_DAY_NODE, "for each active Cataclysm type")

        removed = {preset.days_removed(n) for n in ACTIVE_COUNTS}
        assert removed == {0.0}, (
            f"the preset removes {sorted(removed)} flat days across the eight "
            f"active Cataclysm counts. It buys none of {FLAT_DAY_NODE}, which "
            "is the only flat-day node the branch has left, so every one of "
            "those eight answers should be zero.")
        assert preset.run_days_flat == 0.0
        assert preset.run_days_flat_per_type == 0.0


# ---------------------------------------------------------------------------
# The five nodes it does not have
# ---------------------------------------------------------------------------

class TestItBuysNoneOfTheFiveDepthNodes:
    def test_it_adds_no_floors_at_any_tier(self, preset):
        """**THE ASSERTION THE 2026-09-07 DEFECT WOULD HAVE FAILED.** It had +20
        floors at difficulty tier 1 and +160 at tier 8, from `Infinite Depths`,
        inherited without being asked for."""
        added = {preset.floors_added(n) for n in ACTIVE_COUNTS}
        assert added == {0.0}, (
            f"the preset adds {sorted(added)} floors across the eight active "
            "Cataclysm counts and is defined as buying none of the branch's "
            "five depth nodes.")
        assert preset.floor_delta == 0.0
        assert preset.floor_delta_per_type == 0.0

    @pytest.mark.parametrize("name", DEPTH_NODES)
    def test_the_node_it_excludes_is_still_in_the_branch(self, nodes, name):
        """An exclusion outlives the thing it excludes unless somebody checks.
        A deleted or renamed depth node makes "none of its five" a claim about
        four."""
        assert branch_of(find(nodes, name)) == "Explorer"

    def test_the_branch_has_exactly_those_five_and_no_sixth(self, nodes):
        """**THE HALF THAT NAMING FIVE NODES CANNOT COVER.** Asserting each of
        five exists says nothing about a sixth arriving. If the design gains one
        the comment's "five" is wrong and this build would have to say what it
        does about it, so the graph is counted rather than trusted."""
        found = sorted(
            node["data"]["name"] for node in nodes
            if branch_of(node) == "Explorer"
            and DEPTH_PHRASES.search(node["data"].get("description") or ""))
        assert found == sorted(DEPTH_NODES), (
            f"the Explorer branch's depth nodes are now {found} and this file "
            f"lists {sorted(DEPTH_NODES)}. The preset's comment says it buys "
            "none of five; if there is a sixth, decide what this build does "
            "about it and say so in that comment.")

    def test_the_search_for_depth_nodes_rejects_a_node_that_only_mentions_floors(
            self):
        """**THE CONTROL ON THE PATTERN ABOVE.** Nine Explorer nodes mention
        floors while changing loot or stash placement rather than depth. A
        pattern that matched the word alone would find a dozen "depth nodes",
        the count check would fail for the wrong reason, and the reflex fix
        would be to widen the list rather than to look."""
        assert DEPTH_PHRASES.search("+1 floors to dungeons per point.")
        assert DEPTH_PHRASES.search("+2 floors per active Cataclysm type per point.")
        assert not DEPTH_PHRASES.search(
            "+5% chance per point for a floor to contain a loot chest.")
        assert not DEPTH_PHRASES.search("A Stash appears every 30 floors in dungeons.")
        assert not DEPTH_PHRASES.search(
            "+2% Magic Find for every Floor removed from default dungeon depth.")


# ---------------------------------------------------------------------------
# The same player at every tier
# ---------------------------------------------------------------------------

def test_it_is_the_same_player_at_every_tier(preset):
    """What makes the tier 1 and tier 4 worlds in
    `sim/analyse_surge_cadence.py` comparable at all.

    **EVERY EFFECT, NOT ONLY THE TWO THAT WENT WRONG.** A future
    per-active-Cataclysm-type field on any of the three accessors would break
    this without being named anywhere.
    """
    for effect, call in (("days removed", preset.days_removed),
                         ("floors added", preset.floors_added),
                         ("damage taken", preset.damage_taken)):
        answers = {call(n) for n in ACTIVE_COUNTS}
        assert len(answers) == 1, (
            f"TREE_EXPLORER_DAY_NODES_ONLY gives {sorted(answers)} for "
            f"{effect} across the eight active Cataclysm counts. It is the four "
            "unconditional speed nodes, which do not vary with the tier, so a "
            "second answer means it has taken on a per-active-type node.")


def test_it_is_a_different_build_from_the_whole_branch(preset):
    """**THE CONTROL FOR THE WHOLE FILE.** Every assertion above would also be
    satisfied by the two Explorer presets having become the same object, or by
    this one having quietly become the tree that does nothing at all."""
    from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED, TREE_NONE

    assert preset is not TREE_EXPLORER_AS_DESIGNED
    assert preset.floors_added(1) != TREE_EXPLORER_AS_DESIGNED.floors_added(1)
    assert preset.days_removed(1) != TREE_EXPLORER_AS_DESIGNED.days_removed(1)
    assert preset.run_days_mult != TREE_NONE.run_days_mult, (
        "the preset no longer speeds a walk up at all, which would make it "
        "TREE_NONE under another name")


def test_it_is_one_of_the_shipped_presets(preset):
    """It is in `TREE_PRESETS` since issue #1420, which is what a preset being
    shipped means here and is how it picks up the roster-wide guards -- among
    them `sim/tests/test_city_health_lever.py`, which fails if a preset with no
    Architect node starts raising city health."""
    from cataclysm_sim.config import TREE_PRESETS
    assert preset in TREE_PRESETS


# ---------------------------------------------------------------------------
# Written out, not copied
# ---------------------------------------------------------------------------

class TestItIsWrittenOutFieldByField:
    """**THE PROPERTY ISSUE #1420 ACTUALLY ASKS FOR**, and the only one here
    that a preset holding correct values can still fail."""

    def test_it_is_built_from_empire_tree_and_not_from_another_preset(
            self, definition):
        assert isinstance(definition, ast.Call), (
            "TREE_EXPLORER_DAY_NODES_ONLY is no longer assigned from a call")
        assert isinstance(definition.func, ast.Name), (
            f"it is built by {ast.dump(definition.func)}")
        assert definition.func.id == "EmpireTree", (
            f"TREE_EXPLORER_DAY_NODES_ONLY is built by "
            f"{definition.func.id}(...) and not by EmpireTree(...). If that is "
            "`replace`, it is a copy of another preset again: "
            "`dataclasses.replace` carries across every field it is not told "
            "to change, which is how this preset gained a fifth day node and "
            "+20 floors in silence on 2026-09-07. Issues #1416 and #1420.")
        assert not definition.args, (
            "EmpireTree is being given a positional argument, so this preset is "
            "being built from something rather than from its own fields")

    def test_every_field_of_the_dataclass_is_named(self, definition):
        """**A FIELD LEFT TO ITS DEFAULT IS A FIELD NOBODY DECIDED ABOUT.**
        Naming all twelve is what makes a thirteenth, added to `EmpireTree`
        later, a failure here rather than a silent default -- which is the
        general rule issue #1420 was a specific case of.
        """
        from cataclysm_sim.config import EmpireTree

        named = [keyword.arg for keyword in definition.keywords]
        assert None not in named, (
            "the definition uses `**` unpacking, so what it sets cannot be read "
            "from the source")
        assert len(named) == len(set(named)), f"a field is named twice: {named}"

        expected = [field.name for field in fields(EmpireTree)]
        missing = [name for name in expected if name not in named]
        assert not missing, (
            f"TREE_EXPLORER_DAY_NODES_ONLY does not say what it does about "
            f"{missing}. Issue #1420 defines this preset with every field of "
            "EmpireTree written out, so that a field added to the dataclass is "
            "a decision made here rather than a default nobody saw. Write the "
            "value it should hold and say why in the comment above it.")
        unknown = [name for name in named if name not in expected]
        assert not unknown, f"it sets {unknown}, which EmpireTree has no field for"

    def test_the_fields_it_writes_out_are_mostly_the_defaults(self, preset):
        """**AND THAT IS THE POINT, NOT AN ARGUMENT FOR DELETING THEM.**

        Ten of the twelve are at the dataclass default, so the definition could
        be shortened to two lines and would behave identically today. It is
        written long on purpose: the ten say out loud that this build buys no
        depth, no flat days, no city nodes and no timer nodes, which is what
        distinguishes it from the branch it is a subset of.

        This asserts the shape rather than the values -- exactly two fields
        carry the build -- so a preset that quietly started setting a third
        fails here even if every other check in this file still passes.
        """
        from cataclysm_sim.config import EmpireTree

        away = {field.name for field in fields(EmpireTree)
                if field.default is not MISSING
                and getattr(preset, field.name) != field.default}
        assert away == {"name", "run_days_mult"}, (
            f"the preset sets {sorted(away)} away from the EmpireTree defaults. "
            "It is the four speed nodes and nothing else, so its name and its "
            "walk multiplier are the only two fields that should carry a value "
            "of their own.")
