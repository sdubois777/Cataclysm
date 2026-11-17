"""`TREE_ARCHITECT_AS_DESIGNED`'s city damage falls with the difficulty tier.

WHY THIS FILE EXISTS, AND IT IS NOT A HYPOTHETICAL. Issue #1397 split
`Unyielding Defense` out of the Architect preset's damage multiplier so it could
be folded in at the tier a campaign is played at. The guard proof for that change
broke the split two different ways -- collapsing the per-type factor back into
one number, and making the accessor multiply once instead of once per active type
-- and **the whole fast suite passed both times.** Nothing anywhere checked it.

The Explorer preset has `test_the_explorer_preset_matches_the_tree.py`. This is
the Architect equivalent, for the one node issue #1397 moved.

**WHAT IT DELIBERATELY DOES NOT DO.** It does not check the other ten
damage-reduction nodes in the product, or the six city-health nodes beside it.
Those are the comment above the constant and issues #1288 and #1319; a guard over
all seventeen is worth having and is not this. This covers the node that varies
with the tier, because that is the one a single float could not express and the
one nothing was watching.
"""

from __future__ import annotations

import json
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: What `Unyielding Defense` pays per point, per active Cataclysm type, read off
#: its own text: "-0.5% damage taken by cities per active Cataclysm type per
#: point (multiplicative). Stacks once per active type."
PER_POINT = 0.995

#: The product the preset carried before issue #1397, with the node folded in at
#: one active type. The split has to reproduce it exactly at one, or every
#: figure ever measured at difficulty tier 1 moved for no stated reason.
SHIPPED_AT_ONE_ACTIVE_TYPE = 0.0766

ACTIVE_COUNTS = (1, 2, 3, 4, 5, 6, 7, 8)


@pytest.fixture(scope="module")
def node() -> dict:
    with open(TREE_JSON, encoding="utf-8") as handle:
        nodes = json.load(handle)["nodes"]

    for candidate in nodes:
        if candidate["data"].get("name") == "Unyielding Defense":
            return candidate

    pytest.fail(
        "'Unyielding Defense' is no longer a node in "
        f"{TREE_JSON.name}. `TREE_ARCHITECT_AS_DESIGNED.city_damage_mult_per_type` "
        "is that node and nothing else; if it went away the field should too.")


@pytest.fixture(scope="module")
def preset():
    from cataclysm_sim.config import TREE_ARCHITECT_AS_DESIGNED
    return TREE_ARCHITECT_AS_DESIGNED


def test_the_node_is_still_what_the_preset_says_it_is(node):
    """The wording and the branch, checked before the arithmetic is believed.

    The factor below is `0.995 ** points`, and both halves of that come from
    this node: the 0.995 from "-0.5% ... per point" and the points from
    `maxPoints`. A reword to "-1%" would leave every number here unchanged.
    """
    data = node["data"]
    text = data.get("description") or ""

    assert "-0.5%" in text, (
        f"Unyielding Defense now reads {text!r}. The preset folds it in as "
        f"{PER_POINT} per point; if the rate moved, follow it in "
        "`city_damage_mult_per_type` and here.")
    assert "per active Cataclysm type" in text, (
        f"Unyielding Defense no longer pays per active Cataclysm type: {text!r}. "
        "It is the only reason the Architect preset has a per-type field at "
        "all. Issue #1397.")
    assert data.get("maxPoints") == 5, (
        f"Unyielding Defense now has {data.get('maxPoints')} points, not 5. "
        "The per-type factor is 0.995 raised to that.")

    # The Architect branch is the north-east quadrant; `metadata.description`
    # in the graph says so.
    x, y = node["position"]["x"], node["position"]["y"]
    assert y < 0 and x > 0, (
        "Unyielding Defense has moved out of the Architect quadrant")


def test_the_per_type_factor_is_the_node(node, preset):
    expected = PER_POINT ** node["data"]["maxPoints"]
    assert preset.city_damage_mult_per_type == pytest.approx(expected), (
        f"the preset's per-type factor is {preset.city_damage_mult_per_type} "
        f"and the node gives {expected}. It is 0.995 per point across "
        f"{node['data']['maxPoints']} points.")


def test_it_reproduces_what_shipped_at_one_active_type(preset):
    """**THE CONTROL ON THE SPLIT.**

    Taking a factor out of a product and putting it back through a different
    route has to give the same answer where the two overlap, or difficulty tier
    1 figures measured before issue #1397 quietly stopped describing the model.
    """
    assert preset.damage_taken(1) == pytest.approx(SHIPPED_AT_ONE_ACTIVE_TYPE), (
        f"the Architect preset now takes {preset.damage_taken(1):.6f} of a "
        f"dungeon's damage at one active Cataclysm type, where it took "
        f"{SHIPPED_AT_ONE_ACTIVE_TYPE} before issue #1397 split "
        "`Unyielding Defense` out of the product. Every tier 1 figure ever "
        "measured against this preset assumed the old number.")


@pytest.mark.parametrize("active", ACTIVE_COUNTS)
def test_it_stacks_once_per_active_type(node, preset, active):
    """"Stacks once per active type", so eight types is the factor eight times
    over and not once."""
    expected = (SHIPPED_AT_ONE_ACTIVE_TYPE / PER_POINT ** 5
                * PER_POINT ** (5 * active))
    assert preset.damage_taken(active) == pytest.approx(expected), (
        f"at {active} active Cataclysm types the preset takes "
        f"{preset.damage_taken(active):.6f} and the node gives {expected:.6f}")


def test_a_city_takes_less_damage_at_every_higher_tier(preset):
    """**THE CONTROL THAT CATCHES A FACTOR APPLIED ONCE.**

    Eight equal numbers satisfy nothing above except the parametrised case,
    which compares against a derivation that could share the same mistake. This
    asserts the shape instead: strictly falling, and by a margin big enough that
    a rounding difference could not produce it.
    """
    taken = [preset.damage_taken(n) for n in ACTIVE_COUNTS]

    assert taken == sorted(taken, reverse=True), (
        f"the Architect preset's damage multiplier is {taken} across the eight "
        "active counts; Unyielding Defense reduces damage, so it must fall")
    assert len(set(taken)) == len(ACTIVE_COUNTS), (
        "the multiplier gives the same answer at two different active counts, "
        "so the per-type factor is not being applied per type. Issue #1397.")
    assert taken[-1] < taken[0] * 0.90, (
        f"the multiplier only falls from {taken[0]:.6f} to {taken[-1]:.6f} "
        "across eight active types. `0.995^5` per type should reach about 84% "
        "of the one-type figure; a smaller move means the factor is being "
        "applied once rather than once per type.")


def test_a_campaign_takes_less_damage_at_a_higher_tier(preset):
    """**END TO END THROUGH THE DAY LOOP**, so a call site that read the raw
    `city_damage_mult` would fail here even if every arithmetic test passed.

    One dungeon is resolved against one city at two tiers and the defence it
    takes off has to be smaller at the higher one. `_resolve` is the day loop's
    own damage path, not a second copy of the arithmetic.
    """
    from dataclasses import replace

    from cataclysm_sim.config import DungeonType, TuningConfig
    from cataclysm_sim.engine import Simulation

    def defence_lost(tier: int) -> float:
        cfg = replace(TuningConfig(), tier=tier).with_tree(preset)
        sim = Simulation(cfg, seed=0)
        city = next(iter(sim.empire.cities.values()))
        dungeon = sim._make_dungeon(DungeonType.BASIC, city)
        before = city.defense
        sim._resolve(dungeon)
        return before - city.defense

    # THE CONTROL. If the dungeon took nothing at either tier the comparison
    # below would be between two zeroes and would pass on anything.
    assert defence_lost(1) > 0.0

    assert defence_lost(1) > defence_lost(8), (
        "a dungeon takes the same or more defence off a city at tier 8 as at "
        "tier 1 under the maxed Architect preset. `_resolve` is reading the "
        "tier-independent half of the tree; see `EmpireTree.damage_taken`.")
