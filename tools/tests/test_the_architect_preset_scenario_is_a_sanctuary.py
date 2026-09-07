"""Every field of the Architect preset is read for the same city: a Sanctuary.

WHY THIS FILE EXISTS. `TREE_ARCHITECT_AS_DESIGNED.resolve_bonus_days` is 13.0,
and 13 is 10 from `Strategic Reserve` -- which applies to every city -- plus 3
from `Emergency Shelters`, whose own text says "for Outposts". The preset's
scenario is a Sanctuary. Issues #1409 and #1413.

**THE VALUE IS NOT CORRECTED HERE AND THIS FILE PINS THE WRONG ONE ON PURPOSE.**
Two reasons, and the first is the deciding one:

  * **Correcting it was outside the scope this work was given.** The change that
    brought this file in was asked to establish coverage and add tests, not to
    move preset values, and `sim/cataclysm_sim/config.py` was explicitly out of
    bounds. The defect was found, written up in full with its evidence, and left
    for issue #1409 to carry. Finding something and deliberately not fixing it,
    said out loud, is a whole outcome.
  * **Changing it moves balance figures already on record.** A resolution timer
    three days longer than it should be gives the player three extra days on
    every dungeon in the Architect sweep, so section 7 of `sim/experiments.py`
    owes a re-run that this work did not do.

Pinning the current number and saying it is expected to change is this project's
own pattern for the situation. Issue #1319 did the same for the inert
city-health lever and gave the reason: **the change that fixes it cannot then be
mistaken for a change that did nothing.**

**NOTHING READ THE FIELD'S VALUE AT ALL.** `resolve_bonus_days` appears five
times in the repository -- its declaration, `EmpireTree.describe`, the two
presets that set it, and `sim/cataclysm_sim/engine.py` line 320 that reads it --
and in no test. Issue #1413 broke it and ran the whole fast suite to confirm
that, rather than concluding it from the search.

**THIS FILE ASSERTS THE RULE AND NOT THE NUMBER, WHICH IS THE POINT.** A test
pinning 10.0 would not stop a fourth instance of this mistake. The rule is:

    a node whose own text says "for Outposts" is excluded from every field of
    this preset, because the preset's scenario is a Sanctuary.

That one statement covers `Emergency Shelters` and `Border Patrol` in the
resolution timer and `Fortified Gates` in the city-health multiplier, and it
fails for a fourth such node added tomorrow.

**IT IS THE THIRD TIME IN THE SAME SHAPE.**

  * Issue #1288: a factor of 0.25 in `city_damage_mult` matching no node at all.
  * Issue #1319: 6.54x corrected to 5.90x, because the first count "matched node
    descriptions for a percentage without reading which tier each node applies
    to". `Fortified Gates` is the node it wrongly included.
  * Issue #1409: this one. `Emergency Shelters` is the node wrongly included.

Issue #1288 examined this very constant on 2026-09-05 and called it clean --
"By contrast `resolve_bonus_days=13.0` does trace cleanly". **The arithmetic in
that claim was right. What it skipped was reading which city tier each node
applies to.** Checking that the numbers add up is not checking that the things
being added apply to the same case.

THE NUMBERS ARE READ OFF THE GRAPH, NOT TYPED, for the reason
`tools/tests/test_the_explorer_preset_matches_the_tree.py` gives at length: a
guard that restates the same constant a second time goes on passing after the
design document changes, which is exactly what issue #1288 found had happened
here. The per-point values below are the only figures written down, and each is
checked against its own node's wording before it is believed.
"""

from __future__ import annotations

import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: The phrase a node uses to restrict itself to Outposts. Written once, because
#: every rule in this file turns on it.
OUTPOSTS_ONLY = "for Outposts"

#: Every Architect node that adds days to a resolution timer, with the phrase
#: from its own description that has to still be there for the number beside it
#: to mean what this file says it means.
#:
#: name -> (days per point, phrase that must appear, who it applies to)
TIMER_NODES = {
    "Strategic Reserve": (
        1.0, "+1 Day to all dungeon Resolution Timers per point", "every city"),
    "Emergency Shelters": (
        3.0, "+3 Days to Resolution Timers for Outposts", "Outposts only"),
    "Border Patrol": (
        1.0, "+1 day to Resolution Timer for Outposts per point", "Outposts only"),
}

#: Architect timer nodes left out for reasons that are NOT about the city tier,
#: named so a future reader does not "restore" one.
#:
#: name -> the phrase that earns the exclusion
EXCLUDED_FOR_OTHER_REASONS = {
    #: Pays per 20 points spent in the Explorer tree, so a pure Architect build
    #: gets nothing from it.
    "Bastion Spirit": "per 20 points spent in the Explorer tree",
    #: Conditional on how the run went rather than on the tree.
    "Martial Law": "At <10% City Health",
    #: An action a player takes, not a passive the tree grants.
    "Scorched Earth": "Destroy a city upgrade",
}

#: Every Architect node whose text says "for Outposts". EXACTLY THESE THREE, so
#: a fourth added to the design document fails
#: `test_exactly_these_nodes_are_outposts_only` and forces a decision rather
#: than being missed. `Fortified Gates` is here because it is the node issue
#: #1319 wrongly counted, and it belongs to a different field of the same
#: preset -- which is what makes this a rule and not a special case.
OUTPOSTS_ONLY_NODES = {"Emergency Shelters", "Border Patrol", "Fortified Gates"}

#: `Fortified Gates` at full investment: +8% Defense per point across 8 points.
#: The city-health multiplier is a sum of increases in this project, so counting
#: it would take 5.90 to 6.54 -- the figure issue #1319 corrected.
FORTIFIED_GATES_INCREASE = 0.64


def flat(text: str) -> str:
    """Whitespace collapsed to single spaces.

    Every file in this repository is hard-wrapped, and a phrase search that does
    not flatten first reports a clean tree that is not clean. The node
    descriptions are single JSON strings today; this costs nothing if they are
    ever re-wrapped.
    """
    return re.sub(r"\s+", " ", text).strip()


def is_architect(node: dict) -> bool:
    """The north-east quadrant, which the graph's own `metadata.description`
    calls the Architect branch."""
    return node["position"]["x"] > 0 and node["position"]["y"] < 0


@pytest.fixture(scope="module")
def nodes() -> dict:
    with open(TREE_JSON, encoding="utf-8") as handle:
        graph = json.load(handle)
    return {n["data"]["name"]: n
            for n in graph["nodes"] if n["data"].get("name")}


@pytest.fixture(scope="module")
def preset():
    from cataclysm_sim.config import TREE_ARCHITECT_AS_DESIGNED
    return TREE_ARCHITECT_AS_DESIGNED


def node(nodes: dict, name: str) -> dict:
    if name not in nodes:
        pytest.fail(
            f"{TREE_JSON.name} no longer has a node called {name!r}. "
            "`TREE_ARCHITECT_AS_DESIGNED` is built out of these nodes; if one "
            "went away the constants have to follow it. Issue #1409.")
    return nodes[name]


def text_of(nodes: dict, name: str) -> str:
    return flat(node(nodes, name)["data"].get("description") or "")


def worth(nodes: dict, name: str) -> float:
    """Days this timer node adds at full investment, from the graph's own point
    count times the per-point value its own text states."""
    per_point, _phrase, _applies = TIMER_NODES[name]
    return per_point * node(nodes, name)["data"]["maxPoints"]


# ---------------------------------------------------------------------------
# The nodes are what this file says they are, checked before any arithmetic
# ---------------------------------------------------------------------------

class TestTheNodesStillSayWhatIsClaimed:
    """A reword from "+1 Day" to "+1%" would leave every total below unchanged,
    so the wording is asserted first."""

    @pytest.mark.parametrize("name", sorted(TIMER_NODES))
    def test_the_wording_still_carries_the_number(self, nodes, name):
        per_point, phrase, _applies = TIMER_NODES[name]
        assert flat(phrase) in text_of(nodes, name), (
            f"{name} now reads {text_of(nodes, name)!r}. This file folds it in "
            f"at {per_point} days per point on the strength of {phrase!r}.")

    @pytest.mark.parametrize("name,points", [("Strategic Reserve", 10),
                                             ("Emergency Shelters", 1),
                                             ("Border Patrol", 5),
                                             ("Fortified Gates", 8)])
    def test_the_point_count_is_what_the_totals_multiply(
            self, nodes, name, points):
        got = node(nodes, name)["data"]["maxPoints"]
        assert got == points, (
            f"{name} now has {got} points, not {points}. Every total in this "
            "file is a per-point value times this.")

    @pytest.mark.parametrize(
        "name", sorted(set(TIMER_NODES) | OUTPOSTS_ONLY_NODES
                       | set(EXCLUDED_FOR_OTHER_REASONS)))
    def test_it_is_in_the_architect_quadrant(self, nodes, name):
        assert is_architect(node(nodes, name)), (
            f"{name} has moved out of the Architect quadrant, so it is no "
            "longer a node this preset can claim or exclude.")

    @pytest.mark.parametrize("name,phrase",
                             sorted(EXCLUDED_FOR_OTHER_REASONS.items()))
    def test_the_other_exclusions_still_earn_themselves(
            self, nodes, name, phrase):
        assert phrase in text_of(nodes, name), (
            f"{name} now reads {text_of(nodes, name)!r}. It is left out of "
            f"`resolve_bonus_days` because of {phrase!r}; if that stopped being "
            "true it has to be counted.")


# ---------------------------------------------------------------------------
# The rule: "for Outposts" means excluded, because the scenario is a Sanctuary
# ---------------------------------------------------------------------------

class TestTheOutpostRule:

    def test_exactly_these_nodes_are_outposts_only(self, nodes):
        """**THE COMPLETENESS CHECK, AND THE REASON THIS IS A RULE.**

        A fourth Architect node restricted to Outposts, added tomorrow, fails
        here rather than being quietly folded into a preset that models a
        Sanctuary. That is precisely how `Emergency Shelters` got into
        `resolve_bonus_days` and `Fortified Gates` into the first count of
        `city_health_mult`.
        """
        found = {name for name, n in nodes.items()
                 if is_architect(n) and OUTPOSTS_ONLY in text_of(nodes, name)}
        assert found == OUTPOSTS_ONLY_NODES, (
            f"Architect nodes saying {OUTPOSTS_ONLY!r}: {sorted(found)}; this "
            f"file knows about {sorted(OUTPOSTS_ONLY_NODES)}. Every one of them "
            "must be excluded from every field of TREE_ARCHITECT_AS_DESIGNED, "
            "because the preset's scenario is a Sanctuary. Add it here and "
            "confirm it is not in any constant. Issue #1409.")

    def test_the_search_finds_something(self, nodes):
        """**THE CONTROL.** A search that matched nothing would report every
        node as unrestricted, which is the failure this file is about. This
        project's rule is that a search finding nothing is not a finding until
        the same search has found something."""
        assert OUTPOSTS_ONLY in text_of(nodes, "Emergency Shelters")
        assert OUTPOSTS_ONLY not in text_of(nodes, "Strategic Reserve")


# ---------------------------------------------------------------------------
# What the preset holds, derived from the graph
# ---------------------------------------------------------------------------

class TestTheResolutionTimerIsStrategicReserveAlone:

    def test_only_one_timer_node_applies_to_every_city(self, nodes):
        """`Strategic Reserve` says "all dungeon Resolution Timers"; the other
        two say "for Outposts". That asymmetry is the whole finding."""
        unrestricted = {name for name in TIMER_NODES
                        if OUTPOSTS_ONLY not in text_of(nodes, name)}
        assert unrestricted == {"Strategic Reserve"}, (
            f"timer nodes that apply to a Sanctuary: {sorted(unrestricted)}")

    def test_what_the_rule_gives_is_strategic_reserve_alone(self, nodes):
        """What the preset SHOULD hold, derived from the graph. 10.0."""
        assert worth(nodes, "Strategic Reserve") == pytest.approx(10.0)

    def test_the_preset_does_not_follow_the_rule_yet(self, nodes, preset):
        """**PINNED ON PURPOSE. EXPECTED TO FAIL WHEN ISSUE #1409 LANDS.**

        The preset holds `Strategic Reserve` plus `Emergency Shelters`, and
        `Emergency Shelters` says "for Outposts" while the preset models a
        Sanctuary. That is the defect.

        When #1409 lands, change this to
        `== pytest.approx(worth(nodes, "Strategic Reserve"))` and turn
        `test_it_still_leaves_out_the_other_outposts_only_timer` into a check
        that neither Outposts-only node is counted. **Do not delete this test to
        make that change pass** -- it exists so the correction cannot be
        mistaken for a change that did nothing.
        """
        assert preset.resolve_bonus_days == pytest.approx(
            worth(nodes, "Strategic Reserve")
            + worth(nodes, "Emergency Shelters")), (
            f"the Architect preset adds {preset.resolve_bonus_days} days to "
            "every resolution timer, and the two nodes it was built from give "
            f"{worth(nodes, 'Strategic Reserve') + worth(nodes, 'Emergency Shelters')}. "
            "If this is now `Strategic Reserve` alone, issue #1409 has landed "
            "and this test needs updating with it.")

    def test_it_still_leaves_out_the_other_outposts_only_timer(
            self, nodes, preset):
        """`Border Patrol` is worth +5 and says "for Outposts", exactly as
        `Emergency Shelters` does. It is correctly left out. The defect is that
        the other one is in, not that this one is missing."""
        counted = (worth(nodes, "Strategic Reserve")
                   + worth(nodes, "Emergency Shelters"))
        assert preset.resolve_bonus_days != pytest.approx(
            counted + worth(nodes, "Border Patrol")), (
            "the Architect preset now counts `Border Patrol` as well, which "
            f"says {OUTPOSTS_ONLY!r}. The preset models a Sanctuary, so the "
            "answer is to remove `Emergency Shelters`, not to add this. "
            "Issue #1409.")


class TestTheSameRuleHoldsForCityHealth:
    """**THE SECOND FIELD, WHICH IS WHAT MAKES THIS A RULE.**

    `sim/tests/test_city_health_lever.py` checks that `city_health_mult` is
    5.90. This checks *why*: it is 5.90 rather than 6.54 because
    `Fortified Gates` says "for Outposts" and the scenario is a Sanctuary --
    the same sentence that decides the resolution timer above.
    """

    def test_fortified_gates_is_still_the_outposts_only_health_node(self, nodes):
        text = text_of(nodes, "Fortified Gates")
        assert OUTPOSTS_ONLY in text, (
            f"Fortified Gates now reads {text!r}. It is excluded from "
            "`city_health_mult` only because it is restricted to Outposts.")
        assert "+8% Defense" in text

    def test_the_multiplier_excludes_it(self, preset):
        """Counting it would add 0.64 and give 6.54, the figure issue #1319
        corrected."""
        assert preset.city_health_mult == pytest.approx(5.90)
        assert preset.city_health_mult != pytest.approx(
            5.90 + FORTIFIED_GATES_INCREASE), (
            "the Architect preset's city-health multiplier now counts "
            "`Fortified Gates`, which grants +8% Defense per point across 8 "
            "points but only for Outposts. The preset models a Sanctuary. "
            "Issue #1319.")


# ---------------------------------------------------------------------------
# It reaches the day loop
# ---------------------------------------------------------------------------

class TestTheTimerBonusReachesTheDayLoop:
    """A correct constant nothing reads is inert, which is what issue #1319
    shipped for `city_health_mult` and issue #1327 had to fix.
    `sim/cataclysm_sim/engine.py` line 320 is the only reader, and this drives
    it rather than repeating its arithmetic."""

    @staticmethod
    def timer(tree, dungeon_type=None) -> int:
        from dataclasses import replace

        from cataclysm_sim.config import CityTier, DungeonType, TuningConfig
        from cataclysm_sim.engine import Simulation

        cfg = replace(TuningConfig(), tier=1).with_tree(tree)
        sim = Simulation(cfg, seed=0)
        city = next(c for c in sim.empire.cities.values()
                    if c.tier is CityTier.SANCTUARY)
        return sim._make_dungeon(dungeon_type or DungeonType.BASIC,
                                 city).resolve_max

    def test_a_basic_dungeon_gets_the_extra_days(self, preset):
        """Same seed both sides, and the tree changes no random draw, so the
        whole difference is the field."""
        from cataclysm_sim.config import TREE_NONE

        without = self.timer(TREE_NONE)
        with_tree = self.timer(preset)

        assert without > 0, "the control measured no timer at all"
        assert with_tree > without, (
            f"a Basic dungeon's timer is {with_tree} days under the maxed "
            f"Architect preset and {without} with no tree. "
            "`resolve_bonus_days` is not reaching the day loop.")
        # One day of slack, because the timer is truncated to a whole number.
        assert with_tree - without == pytest.approx(
            preset.resolve_bonus_days, abs=1.0), (
            f"the timer moved by {with_tree - without} days and the preset "
            f"holds {preset.resolve_bonus_days}.")

    def test_no_other_dungeon_type_reads_it(self, preset):
        """Every other type takes its timer straight from the spec. Stated
        because the day loop's `else` branch is easy to lose.

        THE TYPES ARE READ OFF `DUNGEON_SPECS` AND NOT OFF `DungeonType`. A
        Cataclysm dungeon has no spec for a Sanctuary, so walking the enum
        raises a `KeyError` rather than testing anything -- which is what the
        first version of this test did.
        """
        from cataclysm_sim.config import (
            TREE_NONE, CityTier, DungeonType, TuningConfig)

        others = sorted(
            (d for (d, t) in TuningConfig().DUNGEON_SPECS
             if t is CityTier.SANCTUARY and d is not DungeonType.BASIC),
            key=lambda d: d.value)
        assert others, (
            "no dungeon type other than Basic has a Sanctuary spec, so this "
            "test compares nothing")

        for dtype in others:
            assert self.timer(preset, dtype) == self.timer(TREE_NONE, dtype), (
                f"a {dtype.value} dungeon's timer moved with the tree, and only "
                "Basic dungeons read `resolve_bonus_days`.")
