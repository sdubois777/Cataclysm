"""`TREE_EXPLORER_AS_DESIGNED` is what the Explorer branch gives, at every tier.

WHY THIS FILE EXISTS. Issue #1386. The preset said the branch removes 70 flat
days and adds no floors. Read node by node out of
`docs/Empire_Development_Tree_Final.json`, the branch removes 60 unconditionally
and adds 40 at one active Cataclysm type, and the 70 was reached by counting a
node with a condition on it (`Opportunist`) and a node in a different part of the
tree (`The Delver`).

**AND THEN ISSUE #1397 MADE IT PER-TIER, WHICH IS WHY THIS CHECKS EIGHT NUMBERS
AND NOT TWO.** Three nodes in the graph pay per ACTIVE CATACLYSM TYPE, and a
preset holding one float has to choose a tier to be right at. The project owner
ruled on 2026-09-06, verbatim, "Make the presets hold per-tier values". So
`EmpireTree` now carries a tier-independent part and a per-type part for each
affected effect, and this compares the pair against the graph at every active
count a campaign can face.

**`Sovereign's Haste` IS COUNTED NOW AND WAS NOT BEFORE.** That is the whole
substance of #1397: it removes a day per point per active type, which is as
unconditional as the four flat nodes once the tier is known. Counting it takes
the preset to 70 days at tier 1 -- **the same number it carried before #1386, by
a completely different and correct route.** The old 70 was `Opportunist` plus
`The Delver`; two wrong terms summed to the figure one missing right one would
have given. `test_seventy_at_tier_one_is_not_the_old_seventy` is what keeps that
from being read as a revert.

**THE NUMBERS HERE ARE COMPUTED FROM THE GRAPH, NOT TYPED.** A guard that
restated the same constants a second time would pass after the design document
changed, which is what issue #1288 found had happened to the Architect branch. So
this reads the maximum points off each node and multiplies by the per-point value
the node's own text states, and the per-point values are the only thing written
down here.

**AND IT CHECKS THE TEXT IT IS MULTIPLYING.** A point count read from the graph
and multiplied by a per-point value nobody checked is half a guard: the node
could be reworded from "-1 day per point" to "-1% per point" and every total here
would stay the same. Each node carries a phrase from its own description, and the
phrase is asserted before the arithmetic is believed.

`sim/tests/test_explorer_shape.py` checks the analysis script that found the
defect. This checks the constant that carried it. Neither can notice what the
other does.
"""

from __future__ import annotations

import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: Every number of active Cataclysm types a campaign can face.
#: `TuningConfig.active_cataclysm_count` clamps the difficulty tier to this
#: range, so these are all eight tiers.
ACTIVE_COUNTS = (1, 2, 3, 4, 5, 6, 7, 8)

#: The Explorer-branch nodes that take a flat number of days off EVERY dungeon
#: at EVERY tier alike. `phrase` is checked against the node's own text before
#: `per_point` is believed.
#:
#: `Fleet Footed` is a single point worth 5 days rather than 5 points worth 1,
#: which is why the value is per point and the points come from the graph.
FLAT_DAY_NODES = (
    ("Temporal Mastery", 1.0, "-1 Day from dungeon run time per point"),
    ("Overclock", 1.0, "-1 day from dungeon run time per point"),
    ("Pacing", 1.0, "-1 days from dungeon run time per point"),
    ("Fleet Footed", 5.0, "-5 days from dungeon run time"),
)

#: Days removed per point PER ACTIVE CATACLYSM TYPE, and the cap on the total.
#: The cap's own wording says "floors" and means days; that is the design
#: document's slip and it is quoted rather than corrected here.
PER_TYPE_DAY_NODES = (
    ("Sovereign's Haste", 1.0, 30.0, "for each active Cataclysm type"),
)

#: The Explorer-branch nodes that change how deep a dungeon is, at every tier
#: alike.
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


def check(nodes: list[dict], name: str, phrase: str) -> dict:
    """The node, with its branch and its wording confirmed."""
    node = find(nodes, name)
    assert branch_of(node) == "Explorer", (
        f"{name} is now in the {branch_of(node)} branch, not Explorer")
    text = node["data"].get("description") or ""
    assert phrase.lower() in text.lower(), (
        f"{name}'s description is now {text!r} and no longer contains "
        f"{phrase!r}. The totals below multiply this node's point count by a "
        "per-point value taken from that wording, so the wording changing "
        "means the value may have too. Check it by hand and follow it here.")
    return node


def days_removed(nodes: list[dict], active_types: int) -> float:
    total = sum(points(check(nodes, name, phrase)) * per_point
                for name, per_point, phrase in FLAT_DAY_NODES)
    for name, per_point, cap, phrase in PER_TYPE_DAY_NODES:
        node = check(nodes, name, phrase)
        total += min(cap, points(node) * per_point * active_types)
    return total


def floors_added(nodes: list[dict], active_types: int) -> float:
    total = sum(points(check(nodes, name, phrase)) * per_point
                for name, per_point, phrase in FLOOR_NODES)
    for name, per_point, phrase in PER_TYPE_FLOOR_NODES:
        total += points(check(nodes, name, phrase)) * per_point * active_types
    return total


# ---------------------------------------------------------------------------
# The two numbers, at every tier
# ---------------------------------------------------------------------------

class TestThePresetIsWhatTheGraphSays:
    @pytest.mark.parametrize("active", ACTIVE_COUNTS)
    def test_the_days_removed_match_at_every_active_count(
            self, nodes, preset, active):
        derived = days_removed(nodes, active)
        assert preset.days_removed(active) == derived, (
            f"at {active} active Cataclysm types TREE_EXPLORER_AS_DESIGNED "
            f"removes {preset.days_removed(active):g} flat days and the "
            f"Explorer branch's nodes remove {derived:g}. Read the preset "
            "through `days_removed` and not off `run_days_flat`, which is only "
            "the tier-independent half. Issues #1386 and #1397.")

    @pytest.mark.parametrize("active", ACTIVE_COUNTS)
    def test_the_floors_added_match_at_every_active_count(
            self, nodes, preset, active):
        derived = floors_added(nodes, active)
        assert preset.floors_added(active) == derived, (
            f"at {active} active Cataclysm types TREE_EXPLORER_AS_DESIGNED "
            f"adds {preset.floors_added(active):+g} floors and the Explorer "
            f"branch's depth nodes add {derived:+g}. Read the preset through "
            "`floors_added` and not off `floor_delta`.")

    def test_the_derived_totals_are_what_the_graph_gives(self, nodes):
        """The eight-tier answers, pinned as literals so that a change to them
        has to be deliberate. The typed comment is checked separately below."""
        assert [days_removed(nodes, n) for n in (1, 2, 3, 8)] == [70, 80, 90, 90]
        assert [floors_added(nodes, n) for n in (1, 8)] == [40, 180]

    def test_the_comment_above_the_constant_states_the_same_totals(self, nodes):
        """**THE TYPED COPY, WHICH IS NOT THE SAME THING AS THE COMPUTED ONE.**

        A reader checks the comment, not the arithmetic, and the comment is
        hand-typed while everything else here is derived from the graph. That
        asymmetry is exactly how four Siege figures sat stale under passing
        tests: the checks read what a script PRINTS, where the sentence is built
        from a function and cannot go stale, while the same worked example typed
        into a docstring went unread by anything.

        **AN EARLIER VERSION OF THIS TEST HAD THIS NAME AND DID NOT DO IT.** It
        asserted the derivation against literals and never opened `config.py`,
        while its own docstring said "the comment has to be the thing that is
        guarded". `CLAUDE.md` warns that a test whose name asserts something
        nobody verified is worse than a failure. That was one, and it was
        written in the same change that repaired two others of the same shape.

        THE COMMENT IS FLATTENED FIRST, because it is hard-wrapped: a phrase
        longer than one line is not present in the raw text, and a search for
        one would report a clean file that is not clean.
        """
        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "config.py").read_text(
            encoding="utf-8")
        start = source.index("TREE_EXPLORER_AS_DESIGNED = EmpireTree(")
        block = source[:start].rsplit("\n\n", 1)[-1].replace("#", " ")
        comment = re.sub(r"\s+", " ", block)

        # THE POSITIVE CONTROL. A phrase known to be in that block has to be
        # found, or a clean result below would mean the extraction is broken
        # rather than that the comment is right.
        assert "Sovereign's Haste" in comment, (
            "the comment block above TREE_EXPLORER_AS_DESIGNED was not found, "
            "so every check below would be searching an empty string")

        days = [days_removed(nodes, n) for n in (1, 2, 3)]
        floors = [floors_added(nodes, n) for n in (1, 8)]

        # **EVERY OCCURRENCE, NOT THE FIRST, AND THAT DISTINCTION IS NOT
        # THEORETICAL.** The comment states the floor totals in two separate
        # paragraphs. A first version of this check asked whether the right
        # sentence appeared somewhere, and its own guard proof caught it: a
        # break that changed one of the two copies to +160 left the other
        # intact and the test passed. So each claim is found by pattern and
        # every match has to agree.
        CLAIMS = (
            (r"REMOVES (\d+) DAYS AT DIFFICULTY TIER 1",
             (f"{days[0]:g}",),
             "how many days the preset removes at difficulty tier 1"),
            (r"(\d+) at tier 1, (\d+) at tier 2 and (\d+) from tier 3 upwards",
             (f"{days[0]:g}", f"{days[1]:g}", f"{days[2]:g}"),
             "the days removed at tiers 1, 2 and 3"),
            (r"\+(\d+) floors at tier 1 and \+(\d+) at tier 8",
             (f"{floors[0]:g}", f"{floors[1]:g}"),
             "the floors added at tiers 1 and 8"),
        )

        for pattern, expected, what in CLAIMS:
            found = re.findall(pattern, comment)
            found = [hit if isinstance(hit, tuple) else (hit,) for hit in found]

            assert found, (
                f"the comment above TREE_EXPLORER_AS_DESIGNED no longer states "
                f"{what}. It is the only copy of these numbers a reader sees, "
                "and it is hand-typed while everything else here is derived. If "
                "the sentence was reworded, follow it here rather than deleting "
                "the check. Issues #1386 and #1397.")

            wrong = [hit for hit in found if hit != expected]
            assert not wrong, (
                f"the comment states {what} as {wrong} in {len(wrong)} of "
                f"{len(found)} places, and the graph gives {expected}. A single "
                "stale copy is the whole failure this checks for.")

    def test_the_per_type_part_is_actually_per_type(self, nodes, preset):
        """**THE CONTROL FOR EVERYTHING ABOVE.**

        Eight equal numbers would satisfy every parametrised case while the
        preset was still a single figure standing for all eight tiers, which is
        the state issue #1397 exists to end. So both totals must actually move
        with the active count.
        """
        floors = {preset.floors_added(n) for n in ACTIVE_COUNTS}
        days = {preset.days_removed(n) for n in ACTIVE_COUNTS}

        assert len(floors) == len(ACTIVE_COUNTS), (
            f"the preset gives {sorted(floors)} floors across the eight active "
            "counts; it should give a different answer at each, because "
            "Infinite Depths pays per active Cataclysm type")
        assert len(days) > 1, (
            f"the preset removes {sorted(days)} days across the eight active "
            "counts; Sovereign's Haste should move it until its cap")

    def test_the_day_cap_is_reached_and_then_holds(self, nodes, preset):
        """`Sovereign's Haste` stops at -30, so the day total stops at 90.

        Asserted separately because the parametrised cases would pass on an
        implementation with no cap at all -- they compare against a derivation
        that has the same cap in it.
        """
        assert preset.days_removed(3) == preset.days_removed(8) == 90.0
        assert preset.days_removed(2) == 80.0

    def test_seventy_at_tier_one_is_not_the_old_seventy(self, preset):
        """**THE COINCIDENCE, HELD DOWN SO IT IS NOT READ AS A REVERT.**

        `run_days_flat` was 70 before issue #1386, reached by `Opportunist` --
        conditional on the board -- plus `The Delver`, a Tier 1 capstone option
        in no branch at all. It is 70 again at one active Cataclysm type, and
        this time it is the four unconditional nodes plus `Sovereign's Haste`.
        Two wrong terms summed to the figure one missing right one would have.

        The difference is visible in two places and this checks both: the old 70
        was flat at every tier, and it carried no floors.
        """
        assert preset.days_removed(1) == 70.0
        assert preset.days_removed(8) == 90.0, (
            "the old 70 was the same at every tier. If this is 70 at tier 8 "
            "too, the per-type part is not being applied and the preset has "
            "gone back to what issue #1386 repaired.")
        assert preset.floors_added(1) == 40.0, (
            "the old 70 came with no floors at all. Issue #1386 added them.")


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
        check(nodes, name, phrase)

    def test_sovereigns_haste_is_no_longer_excluded(self, nodes, preset):
        """It was on the list above until issue #1397 and is now counted.

        Kept as a test rather than deleted, because the reason it was excluded
        -- that it varies with the tier -- is now the reason `EmpireTree` has
        per-type fields at all.
        """
        assert preset.run_days_flat_per_type > 0, (
            "Sovereign's Haste is back out of the preset. The owner ruled on "
            "2026-09-06 that presets hold per-tier values rather than "
            "excluding the nodes that need them. Issue #1397.")
        assert "Sovereign's Haste" not in [name for name, _p, _w in EXCLUDED]

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


class TestTheModelAgreesWithTheTier:
    def test_the_active_count_is_the_difficulty_tier(self):
        """Where the number handed to the accessors comes from, checked rather
        than assumed."""
        from dataclasses import replace

        from cataclysm_sim.config import TuningConfig

        for tier in ACTIVE_COUNTS:
            assert replace(TuningConfig(), tier=tier
                           ).active_cataclysm_count() == tier

    def test_a_campaign_walks_a_dungeon_differently_at_a_different_tier(self):
        """**THE END-TO-END CHECK, and the one that would catch a call site the
        source guard misses.**

        Everything above tests the preset. This runs the engine's own two
        readers with the same tree at two tiers and requires each answer to
        differ, so a `run_days_for` or a `_make_dungeon` that read the raw
        fields would fail here even if every arithmetic test above passed.

        **THE DEPTH IS 400 FLOORS AND THAT IS NOT ARBITRARY.** The preset
        removes 70 days at tier 1 and 90 at tier 8, and `run_days_min` is 1, so
        anything shallower than about 90 floors clamps to one day at BOTH tiers
        and the comparison passes on a broken reader by measuring the clamp. A
        first draft of this test used 60 floors and did exactly that.
        """
        from dataclasses import replace

        from cataclysm_sim.config import (
            TREE_EXPLORER_AS_DESIGNED, CityTier, DungeonType, TuningConfig,
        )
        from cataclysm_sim.engine import Simulation

        def sim_at(tier: int) -> Simulation:
            cfg = replace(TuningConfig(), tier=tier).with_tree(
                TREE_EXPLORER_AS_DESIGNED)
            return Simulation(cfg, seed=0)

        shallow, deep = sim_at(1), sim_at(8)

        # THE CONTROL FOR THE DEPTH. Both walks must be off the one-day floor,
        # or the assertion below would be comparing two clamps.
        assert shallow.run_days_for(400) > shallow.cfg.run_days_min
        assert deep.run_days_for(400) > deep.cfg.run_days_min

        assert shallow.run_days_for(400) != deep.run_days_for(400), (
            "a 400-floor dungeon walks in the same number of days at tier 1 "
            "and tier 8 under the maxed Explorer preset. `run_days_for` is "
            "reading the tier-independent half of the tree; see "
            "`EmpireTree.days_removed`. Issue #1397.")

        # AND THE DEPTH THE DUNGEON IS BUILT WITH, which is the other reader.
        # `_make_dungeon` adds the tree's floors; `run_days_for` does not, so
        # neither check covers the other.
        def floors_of(sim: Simulation) -> int:
            city = next(c for c in sim.empire.cities.values()
                        if c.tier is CityTier.OUTPOST)
            return sim._make_dungeon(DungeonType.BASIC, city).floors

        assert floors_of(shallow) < floors_of(deep), (
            "a dungeon is built the same depth at tier 1 and tier 8 under the "
            "maxed Explorer preset. `_make_dungeon` is reading the "
            "tier-independent half; see `EmpireTree.floors_added`. The branch "
            "adds +40 floors at tier 1 and +180 at tier 8.")
