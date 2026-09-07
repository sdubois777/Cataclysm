"""Every field an empire tree preset sets changes something a campaign can see.

WHY THIS FILE EXISTS. Issue #1413 broke each of the fifteen non-default field
values across the six presets in `sim/cataclysm_sim/config.py`, one at a time,
and ran the whole fast suite after each. Several broke nothing at all. The two
that issue #1397 found the same way -- by chance, while proving an unrelated
guard -- are the reason that sweep happened.

**THIS IS THE WIRING HALF AND NOT THE VALUE HALF.** It does not say 0.85 is the
right multiplier; it says that whatever `run_days_mult` holds, the day loop reads
it, so a call site that stops reading a field fails here. The value half is
`tools/tests/test_the_explorer_preset_matches_the_tree.py`,
`test_the_architect_preset_scales_with_the_tier.py`,
`test_the_architect_preset_scenario_is_a_sanctuary.py` and
`test_a_preset_name_describes_its_values.py`. Neither half can notice what the
other does:

  * a field with a correct value that nothing reads is inert, which is what
    issue #1319 found for `city_health_mult` and issue #1327 had to fix;
  * a field that is read with a wrong value is issue #1288, #1319 and #1409.

**THE PROBE TABLE IS WRITTEN OUT AND NOT DERIVED.** Every field of `EmpireTree`
is named here with the engine quantity it must move. A thirteenth field added
without a probe fails `test_every_field_has_a_probe`, so leaving one uncovered is
a decision somebody makes rather than an omission -- the same reasoning
`tools/tests/test_the_tree_is_read_per_tier.py` gives for its own list.
"""

from __future__ import annotations

import dataclasses
from dataclasses import replace

import pytest

from cataclysm_sim.config import (
    TREE_NONE, TREE_PRESETS, TREE_PROPOSED_FIX, CityTier, DungeonType,
    EmpireTree, TuningConfig,
)
from cataclysm_sim.engine import Simulation
from cataclysm_sim.world import build_empire

#: Enough floors that `run_days_for` is not sitting on `run_days_min` or
#: `run_days_max`. The Explorer preset removes 90 days from a walk at difficulty
#: tier 3 upwards, so a small dungeon clamps to 1 day with or without the tree
#: and a probe built on one would compare two clamps and pass on anything.
DEEP_ENOUGH = 200

#: A difficulty tier with more than one active Cataclysm type, so a per-type
#: field is multiplied or added more than once. At tier 1 a per-type field and a
#: flat one of the same size are indistinguishable, which is the confusion issue
#: #1397 exists to remove.
HIGH_TIER = 8


def sim_at(tree: EmpireTree, tier: int = 1) -> Simulation:
    """A simulation with this tree, at this difficulty tier, on a fixed seed.

    The seed is fixed and the tree does not change how many numbers are drawn
    from it, so two simulations built this way with different trees make
    identical draws and any difference between them is the tree.
    """
    return Simulation(replace(TuningConfig(), tier=tier).with_tree(tree), seed=0)


def a_city(sim: Simulation, tier: CityTier = CityTier.SANCTUARY):
    return next(c for c in sim.empire.cities.values() if c.tier is tier)


# ---------------------------------------------------------------------------
# The probes. Each takes a tree and returns one number the day loop computes.
# ---------------------------------------------------------------------------

def walk_days(tree: EmpireTree, tier: int = 1) -> float:
    """How many days a deep dungeon takes to walk."""
    return sim_at(tree, tier).run_days_for(DEEP_ENOUGH)


def dungeon_floors(tree: EmpireTree, tier: int = 1) -> float:
    sim = sim_at(tree, tier)
    return sim._make_dungeon(DungeonType.BASIC, a_city(sim)).floors


def resolve_timer(tree: EmpireTree, tier: int = 1) -> float:
    """A Basic dungeon's resolution timer. Only Basic dungeons read the tree;
    every other type takes its timer straight from the spec."""
    sim = sim_at(tree, tier)
    return sim._make_dungeon(DungeonType.BASIC, a_city(sim)).resolve_max


def defence_removed(tree: EmpireTree, tier: int = 1) -> float:
    """Defence one resolve takes off a city -- the day loop's own damage path,
    not a second copy of the arithmetic."""
    sim = sim_at(tree, tier)
    city = a_city(sim)
    dungeon = sim._make_dungeon(DungeonType.BASIC, city)
    before = city.defense
    sim._resolve(dungeon)
    return before - city.defense


def city_defence_pool(tree: EmpireTree, tier: int = 1) -> float:
    cfg = replace(TuningConfig(), tier=tier).with_tree(tree)
    empire = build_empire(cfg)
    return next(c for c in empire.cities.values()
                if c.tier is CityTier.SANCTUARY).max_defense


def days_between_surges(tree: EmpireTree, tier: int = 1) -> float:
    return sim_at(tree, tier).surge_gap()


# ---------------------------------------------------------------------------
# field -> (probe, tier to probe at, tree A kwargs, tree B kwargs, why)
# ---------------------------------------------------------------------------
#
# A and B differ in ONE field, except where a field only means anything
# alongside another -- `run_days_flat_per_type_cap` caps a per-type rate and
# says nothing without one, so both trees carry the rate and differ in the cap.
PROBES: dict[str, tuple] = {
    "run_days_flat": (
        walk_days, 1, {}, {"run_days_flat": 60.0},
        "flat days come off every dungeon's walk"),
    "run_days_mult": (
        walk_days, 1, {}, {"run_days_mult": 0.85},
        "the scalar multiplies the walk after the flat days come off"),
    "run_days_flat_per_type": (
        walk_days, HIGH_TIER, {}, {"run_days_flat_per_type": 10.0},
        "Sovereign's Haste removes a day per point per active Cataclysm type"),
    "run_days_flat_per_type_cap": (
        walk_days, HIGH_TIER,
        {"run_days_flat_per_type": 10.0, "run_days_flat_per_type_cap": 30.0},
        {"run_days_flat_per_type": 10.0, "run_days_flat_per_type_cap": 50.0},
        "the cap holds the per-type part down at a high tier. BOTH SIDES CARRY "
        "A CAP: a cap of 0 means NO cap, so comparing 30 against 0 would "
        "measure the special case rather than the cap"),
    "floor_delta": (
        dungeon_floors, 1, {}, {"floor_delta": 20.0},
        "floors added or removed change how deep a dungeon is"),
    "floor_delta_per_type": (
        dungeon_floors, HIGH_TIER, {}, {"floor_delta_per_type": 20.0},
        "Infinite Depths adds floors per active Cataclysm type"),
    "city_damage_mult": (
        defence_removed, 1, {}, {"city_damage_mult": 0.55},
        "the share of a resolve's damage a city actually takes"),
    "city_damage_mult_per_type": (
        defence_removed, HIGH_TIER, {}, {"city_damage_mult_per_type": 0.9752},
        "Unyielding Defense, multiplied in once per active type"),
    "city_health_mult": (
        city_defence_pool, 1, {}, {"city_health_mult": 5.90},
        "how much damage a city can absorb before it falls"),
    "resolve_bonus_days": (
        resolve_timer, 1, {}, {"resolve_bonus_days": 10.0},
        "days added to every Basic dungeon's resolution timer"),
    "surge_bonus_days": (
        days_between_surges, 1, {}, {"surge_bonus_days": 10.0},
        "days added to the gap between surges"),
}

#: `name` is a label and moves nothing, so it has no probe and says so here
#: rather than being missing from the table.
NOT_A_LEVER = {"name"}


def tree(**kwargs) -> EmpireTree:
    return EmpireTree(name="probe", **kwargs)


class TestTheProbeTableCoversTheClass:

    def test_every_field_has_a_probe(self):
        """A field added to `EmpireTree` without a probe is caught here.

        THE POINT OF THE WHOLE FILE. `city_health_mult` was added by issue
        #1319, read nothing, and shipped inert; the campaign test written to
        catch that passed by chance on floating-point jitter. A field with no
        probe is a field in that position.
        """
        fields = {f.name for f in dataclasses.fields(EmpireTree)}
        covered = set(PROBES) | NOT_A_LEVER
        assert fields == covered, (
            f"`EmpireTree` fields with no probe: {sorted(fields - covered)}; "
            f"probes for fields that no longer exist: {sorted(covered - fields)}. "
            "Add a probe naming the engine quantity the field must move, or put "
            "it in NOT_A_LEVER with a reason.")

    def test_nothing_is_in_both_lists(self):
        assert not (set(PROBES) & NOT_A_LEVER)


class TestEachFieldReachesTheDayLoop:

    @pytest.mark.parametrize("field", sorted(PROBES))
    def test_changing_it_changes_what_a_campaign_sees(self, field):
        probe, tier, a_kwargs, b_kwargs, why = PROBES[field]
        a, b = probe(tree(**a_kwargs), tier), probe(tree(**b_kwargs), tier)
        assert a != b, (
            f"`EmpireTree.{field}` changed from {a_kwargs.get(field, 'its default')} "
            f"to {b_kwargs[field]} and {probe.__name__} stayed at {a} at "
            f"difficulty tier {tier}. Nothing in the day loop is reading it. "
            f"It is meant to be the lever that {why}.")

    @pytest.mark.parametrize("field", sorted(PROBES))
    def test_the_probe_measured_something(self, field):
        """**THE FIRST CONTROL.** A probe returning zero on both sides would
        make the test above compare two zeroes, which passes on nothing."""
        probe, tier, a_kwargs, _b_kwargs, _why = PROBES[field]
        assert probe(tree(**a_kwargs), tier) > 0.0

    @pytest.mark.parametrize(
        "field", sorted(f for f in PROBES if PROBES[f][0] is walk_days))
    def test_the_walk_probes_are_not_sitting_on_a_clamp(self, field):
        """**THE SECOND CONTROL, AND ONLY FOR THE WALK.** Two numbers that
        differ prove the field is read. They do not prove the probe is measuring
        the field rather than a limit it happens to sit on, and `run_days_for`
        is the one probe with limits: it clamps to `run_days_min` and
        `run_days_max`.

        Restricted to the walk probes on purpose. Comparing a floor count or a
        defence pool against a day limit would be comparing two unrelated
        quantities and would pass or fail by coincidence.
        """
        probe, tier, a_kwargs, b_kwargs, _why = PROBES[field]
        cfg = TuningConfig()
        for kwargs in (a_kwargs, b_kwargs):
            days = probe(tree(**kwargs), tier)
            assert days not in (cfg.run_days_min, cfg.run_days_max), (
                f"a walk of {DEEP_ENOUGH} floors under `{field}={kwargs}` takes "
                f"{days} days, which is a clamp boundary. A probe pinned to a "
                "clamp compares two clamps.")


class TestTheTwoTimerFieldsNothingElseGuards:
    """`TREE_PROPOSED_FIX`'s two timer values, pinned through the day loop.

    THESE TWO WERE GUARDED BY NOTHING AT ALL. Issue #1413 changed
    `resolve_bonus_days` from 5.0 to 2.0 and `surge_bonus_days` from 10.0 to 3.0
    and ran the whole fast suite; neither moved a test. They are also the two
    fields the preset's own name does not mention -- it is called
    "Proposed budget (x0.85 time, x0.55 dmg)", which states its other two.

    THE CLASS ABOVE CANNOT COVER THEM AND THAT IS BY DESIGN. It builds its own
    trees to prove the day loop reads each field, so it goes on passing whatever
    a preset holds. This asserts the preset's OWN numbers, and it does it by
    measuring the day loop rather than reading the constant back, so it fails
    both when the value changes and when the call site stops reading it.

    THE VALUES ARE A DESIGN BUDGET AND NOT DERIVED FROM THE NODE GRAPH, which is
    why they are written out here rather than computed. `TREE_PROPOSED_FIX` is a
    proposal -- "a conservative whole-tree budget: modest multiplicative speed,
    clamped city damage reduction, small timer padding" -- not a reading of
    `docs/Empire_Development_Tree_Final.json`. A change to either number is a
    change of proposal, and it should have to say so here.
    """

    #: The Proposed budget preset's two timer fields, and what each is worth.
    RESOLVE_DAYS = 5.0
    SURGE_DAYS = 10.0

    def test_it_adds_its_stated_days_to_a_dungeons_timer(self):
        """Measured against no tree on the same seed. The tree changes no random
        draw, so the whole difference is the field."""
        preset = TREE_PROPOSED_FIX
        without = resolve_timer(TREE_NONE)
        with_tree = resolve_timer(preset)

        assert without > 0, "the control measured no timer at all"
        # A day, because the timer is truncated to a whole number.
        assert with_tree - without == pytest.approx(self.RESOLVE_DAYS, abs=1.0), (
            f"the Proposed budget preset adds {with_tree - without} days to a "
            f"Basic dungeon's resolution timer and holds "
            f"`resolve_bonus_days={preset.resolve_bonus_days}`. It is meant to "
            f"add {self.RESOLVE_DAYS}.")

    def test_it_adds_its_stated_days_between_surges(self):
        """No truncation here, so this is exact."""
        preset = TREE_PROPOSED_FIX
        without = days_between_surges(TREE_NONE)
        with_tree = days_between_surges(preset)

        assert without > 0, "the control measured no gap at all"
        assert with_tree - without == pytest.approx(self.SURGE_DAYS), (
            f"the Proposed budget preset adds {with_tree - without} days "
            f"between surges and holds `surge_bonus_days={preset.surge_bonus_days}`. "
            f"It is meant to add {self.SURGE_DAYS}.")

    def test_the_constants_here_are_the_ones_the_preset_holds(self):
        """Says plainly which two numbers the two tests above are pinning, so a
        deliberate change to the proposal has one obvious place to land."""
        assert TREE_PROPOSED_FIX.resolve_bonus_days == pytest.approx(
            self.RESOLVE_DAYS)
        assert TREE_PROPOSED_FIX.surge_bonus_days == pytest.approx(self.SURGE_DAYS)

    def test_no_other_preset_pads_the_surge_gap(self):
        """`surge_bonus_days` is set by this preset alone. A second preset
        gaining it would change how often every campaign is attacked, and the
        two tests above would not notice."""
        for tree in TREE_PRESETS:
            if tree is TREE_PROPOSED_FIX:
                continue
            assert tree.surge_bonus_days == 0.0, (
                f"{tree.name} now pads the gap between surges by "
                f"{tree.surge_bonus_days} days.")


class TestEveryPresetFieldIsProbed:
    """A preset that sets a field no probe covers would slip through the class
    above, because that one builds its own trees."""

    @pytest.mark.parametrize(
        "preset", TREE_PRESETS, ids=lambda t: t.name)
    def test_every_non_default_field_it_sets_has_a_probe(self, preset):
        defaults = EmpireTree(name=preset.name)
        set_fields = {
            f.name for f in dataclasses.fields(EmpireTree)
            if f.name not in NOT_A_LEVER
            and getattr(preset, f.name) != getattr(defaults, f.name)}
        assert set_fields <= set(PROBES), (
            f"{preset.name} sets {sorted(set_fields - set(PROBES))}, which no "
            "probe covers, so nothing here would notice if the day loop stopped "
            "reading them.")
