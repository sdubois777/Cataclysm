"""A play policy can see that a Siege's damage is daily.

WHY THIS FILE EXISTS. Issue #1340. `sim/cataclysm_sim/policies.py` scored every
dungeon by what ONE RESOLVE costs its city -- `d.defense_damage`. A Siege's
damage does not happen at resolve time; it happens every day the Siege stands
and grows every day it has stood. So the single most dangerous dungeon in the model
was scored identically to an ordinary one on the same city, and no policy could
choose it *because* it was a Siege. The project owner ruled on 2026-09-06,
verbatim: "Fix the policies soon, before more figures accumulate (Recommended)".
A separate turtling policy was offered in the same decision and NOT taken, which
is why none is added here.

READ THIS BEFORE TRUSTING THE ISSUE'S ACCEPTANCE CRITERION. The criterion set
with that ruling was that the cities-lost ratio between `triage` and
`lane_aware` would move off 1.0 once the policies could see a Siege. **It does
not, and the fix in this file is not what is holding it at 1.0.** Measured at
600 campaigns a cell, difficulty tier 1, no empire tree, cities lost out of 25:

    surge 4, Siege modelled   before 1.01   after 0.98
    surge 5, Siege modelled   before 1.02   after 1.01

WHAT DOES MOVE, at 120 campaigns, tier 1, no tree, surge 4, triage: the policies
clear **279** Sieges with this change against **176** blind. The blindness was
real and it is repaired. It was simply not the thing flattening the ratio.

WHY NO POLICY COULD MEET THAT CRITERION, measured on 2026-09-06 at tier 1, no
tree, surge 4:

  * The player is free to choose on **8.0%** of days -- a dungeon walk is 12 to
    49 days and a fresh Siege empties its city in 14 to 47. **Those two day
    counts are the ones that held when this was measured**; the owner cut the
    growth on issue #1349 later the same day and a fresh Siege now empties its
    city in 25 to 70, which is the change the figures below argued for.
  * So **72.7%** of Sieges standing on a living city give the player **zero**
    moments at which they are both free and could still finish the walk in
    time. The other 27.3% give exactly one. None gave two.
  * Sieges deal **64.7%** of every killing blow landed on a city.
  * An oracle policy that ALWAYS takes a reachable Siege, ignoring everything
    else, leaves the ratio at **1.01** over 200 campaigns.

The last of those is the decisive one: the ceiling on what any targeting change
can achieve here is nothing. The ratio is flat because the player's action
cadence is slower than a Siege's lifetime, which is a time-budget problem and
not a targeting one. **Issue #1351 carries the full working and is the open
question**; do not read a flat ratio here as this fix having regressed.

SO WHAT THESE TESTS GUARD is that the policies see a Siege, act on one while it
can still be answered, and leave every other dungeon's score alone -- not that
the ratio recovered. A test asserting the ratio would be asserting something
this change cannot deliver and nothing else in the model currently can either.
"""

from __future__ import annotations

import inspect
from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_NONE, CityTier, DungeonType, EmpireTree, TuningConfig,
)
from cataclysm_sim.engine import Simulation

#: The two policies that weigh damage prevented, and so the two that were fixed.
SCORING_POLICIES = ("triage", "lane_aware")

#: Every sub-type that is not the one this file is about.
OTHER_SUBTYPES = ["Timed", "Horde", "Elite", "Volatile", "Sacrificial",
                  "Cow Level"]


def sim_for(tree: EmpireTree = TREE_NONE, tier: int = 1,
            **overrides) -> Simulation:
    cfg = replace(TuningConfig(), tier=tier, **overrides).with_tree(tree)
    return Simulation(cfg, seed=0)


def city_of(sim: Simulation, tier: CityTier):
    return next(c for c in sim.empire.cities.values() if c.tier is tier)


def put(sim: Simulation, city, subtype: str = "Siege", stood_for: int = 0,
        dtype: DungeonType = DungeonType.BASIC, run_days: int | None = None):
    """One dungeon of that sub-type on that city, standing for that long."""
    d = sim._make_dungeon(dtype, city)
    d.subtype = subtype
    d.spawned_day = sim.day - stood_for
    if run_days is not None:
        d.run_days = run_days
    return d


def a_reachable_siege(sim, city, **kw):
    """A Siege the player could still finish walking before the city dies.

    THE WALK LENGTH IS FORCED RATHER THAN ROLLED, and that is the point. A
    dungeon's depth is random, so a rolled dungeon lands on either side of the
    line depending on the seed and a test that let it roll would quietly stop
    covering the reachable case the first time the roll changed.

    THAT WAS ACUTE WHEN THE MARGIN WAS ONE DAY AND IT IS STILL TRUE NOW THAT IT
    IS NOT. At tier 1 the median walk is 13 days to an Outpost and 35 to a
    Sanctuary, against the 25 and 55 a fresh Siege has left since the owner cut
    the growth on issue #1349 on 2026-09-06; it left 14 and 34 before that, and
    the median player arrived as the city fell. The margin is wide now, but the
    roll still crosses it for a deep enough dungeon or a Siege that has already
    stood a while, which is exactly the case this covers.

    THE 12 AND 33 THIS PARAGRAPH USED TO GIVE WERE NEVER REPRODUCIBLE, which is
    what issue #1364 turned out to be about: the walk lengths are nearly uniform
    where the median sits, so it is a coin flip between two adjacent days.
    `test_the_siege_prose_in_policies_is_true.py` re-measures the four and holds
    `policies.py` to them; nothing here depends on the exact day.
    """
    d = put(sim, city, run_days=2, **kw)
    assert policies.siege_urgency(sim, d, city, 5.0) > 1.0, (
        "this fixture is meant to build a Siege that can still be answered "
        "and did not, so the test below is not covering what it names")
    return d


class TestItMirrorsTheEngineRatherThanRestatingIt:
    """`siege_daily_damage` exists so a policy can ask what the day loop is
    about to do to a city. If the two ever disagree the policy is reasoning
    about a game that is not being played, so these drive the engine and
    compare, rather than re-deriving the formula and comparing two copies of
    the same arithmetic -- which would pass while both were wrong.
    """

    @pytest.mark.parametrize("tier", list(CityTier))
    @pytest.mark.parametrize("stood_for", [0, 1, 5, 20])
    def test_it_equals_what_the_day_loop_actually_takes(self, tier, stood_for):
        sim = sim_for()
        city = city_of(sim, tier)
        d = put(sim, city, stood_for=stood_for)

        predicted = policies.siege_daily_damage(sim, d, city)
        before = city.defense
        sim._apply_siege_damage()
        assert predicted == pytest.approx(before - city.defense)

    @pytest.mark.parametrize("mult", [0.25, 0.5, 1.0])
    def test_it_tracks_the_trees_damage_reduction(self, mult):
        """The owner ruled on 2026-09-06 that damage reduction still applies to
        a Siege. A policy that ignored it would over-value clearing one in
        exactly the empire that has already paid to make it survivable."""
        sim = sim_for(EmpireTree(name="d", city_damage_mult=mult))
        city = city_of(sim, CityTier.BULWARK)
        d = put(sim, city, stood_for=6)

        predicted = policies.siege_daily_damage(sim, d, city)
        before = city.defense
        sim._apply_siege_damage()
        assert predicted == pytest.approx(before - city.defense)

    def test_a_spawn_day_after_the_clock_does_not_read_as_a_repair(self):
        """`_apply_siege_damage` clamps the age at zero for this reason and the
        mirror has to clamp it in the same place."""
        sim = sim_for()
        city = city_of(sim, CityTier.OUTPOST)
        d = put(sim, city)
        d.spawned_day = sim.day + 50
        assert policies.siege_daily_damage(sim, d, city) > 0.0

    @pytest.mark.parametrize("run_days", [1, 3, 11])
    @pytest.mark.parametrize("stood_for", [0, 4])
    def test_the_walk_total_equals_that_many_days_of_the_day_loop(
            self, run_days, stood_for):
        """THE CLOSED FORM IS THE EASY THING TO GET WRONG -- it has to carry the
        growth accruing during the walk, not just repeat today's bite. Driven
        against the engine one day at a time rather than checked against the
        formula it came from."""
        sim = sim_for()
        city = city_of(sim, CityTier.SANCTUARY)
        d = put(sim, city, stood_for=stood_for, run_days=run_days)

        predicted = policies.siege_damage_during_the_walk(sim, d, city)
        before = city.defense
        for _ in range(run_days):
            sim._apply_siege_damage()
            sim.day += 1
        assert predicted == pytest.approx(before - city.defense)


class TestItIsSilentAboutEverythingElse:
    """The fix must not move any other dungeon's score. If it does, every
    figure that moves after this lands is unattributable."""

    @pytest.mark.parametrize("subtype", OTHER_SUBTYPES)
    def test_no_other_subtype_does_daily_damage(self, subtype):
        sim = sim_for()
        city = city_of(sim, CityTier.SANCTUARY)
        d = put(sim, city, subtype=subtype, stood_for=30)
        assert policies.siege_daily_damage(sim, d, city) == 0.0
        assert policies.siege_damage_during_the_walk(sim, d, city) == 0.0

    @pytest.mark.parametrize("subtype", OTHER_SUBTYPES)
    @pytest.mark.parametrize("fatal_mult", [4.0, 5.0])
    def test_no_other_subtype_moves_its_urgency_off_one(self, subtype,
                                                        fatal_mult):
        sim = sim_for()
        city = city_of(sim, CityTier.SANCTUARY)
        d = put(sim, city, subtype=subtype, stood_for=30)
        assert policies.siege_urgency(sim, d, city, fatal_mult) == 1.0

    def test_a_siege_on_a_fallen_city_is_worth_nothing_extra(self):
        """`_apply_siege_damage` skips a fallen city, so there is no damage to
        prevent. Without this the policy would chase a Siege that is doing
        nothing at all -- and a Fallen City dungeon can carry the sub-type."""
        sim = sim_for()
        city = city_of(sim, CityTier.OUTPOST)
        d = put(sim, city, stood_for=10)
        city.fallen = True
        assert policies.siege_daily_damage(sim, d, city) == 0.0
        assert policies.siege_urgency(sim, d, city, 5.0) == 1.0

    def test_the_whole_change_is_inert_when_sieges_are(self):
        """THE CONTROL THAT MAKES EVERY LATER FIGURE ATTRIBUTABLE. With the
        three Siege constants zeroed, a campaign must come out exactly as it
        did before this change -- so anything that moves in a Siege world moved
        because of the Siege and not because the scoring was disturbed."""
        inert = replace(TuningConfig(), tier=1,
                        siege_defence_bite_per_day=0.0,
                        siege_population_bite_per_day=0.0,
                        siege_damage_growth_per_day=0.0).with_tree(TREE_NONE)
        sim = Simulation(inert, seed=0)
        city = city_of(sim, CityTier.OUTPOST)
        d = put(sim, city, stood_for=40)
        assert policies.siege_urgency(sim, d, city, 5.0) == 1.0


class TestTheWindowToAnswerOneOpensAndCloses:
    """AN UNATTENDED SIEGE ALWAYS KILLS ITS CITY -- 25 days for an Outpost, 70
    for the Pillar since the owner cut the growth on issue #1349 -- so "will
    this city die?" is settled before a policy is asked. The only live question
    is whether the player can still get there, and that is what the urgency
    answers.
    """

    def test_a_siege_the_player_can_still_reach_is_worth_the_fatal_weight(self):
        sim = sim_for()
        city = city_of(sim, CityTier.SANCTUARY)
        d = a_reachable_siege(sim, city)
        assert policies.siege_urgency(sim, d, city, 5.0) == pytest.approx(5.0)
        assert policies.siege_urgency(sim, d, city, 4.0) == pytest.approx(4.0)

    def test_a_siege_the_player_cannot_reach_in_time_is_worth_nothing_extra(
            self):
        """THE MISTAKE THE FIRST VERSION OF THIS FIX MADE, kept here as a test
        because it looked entirely reasonable. Scoring by the share of the city
        a Siege would eat during the walk gives FULL weight to the Sieges that
        are already lost -- they eat all of it -- and the LEAST weight to one
        that has just arrived, which is the only moment it can be answered. It
        moved the acceptance ratio from 1.01 to 1.03 over 600 campaigns, which
        is to say not at all.

        Chasing a Siege that cannot be reached spends the whole walk losing the
        city it went to defend AND whatever it walked past.
        """
        sim = sim_for()
        city = city_of(sim, CityTier.OUTPOST)
        hopeless = put(sim, city, stood_for=0, run_days=400)
        assert policies.siege_urgency(sim, hopeless, city, 5.0) == 1.0

    def test_the_window_closes_as_the_siege_is_ignored(self):
        """A Siege answerable today is not answerable indefinitely: the daily
        bite grows while the defence it is measured against falls, so the same
        walk stops fitting. This is the compounding the issue is about,
        expressed as the thing a player loses -- the chance to act."""
        sim = sim_for()
        city = city_of(sim, CityTier.SANCTUARY)
        d = a_reachable_siege(sim, city)

        seen = []
        for _ in range(60):
            seen.append(policies.siege_urgency(sim, d, city, 5.0))
            sim._apply_siege_damage()
            sim.day += 1

        assert seen[0] == 5.0, "the window was shut before it opened"
        assert seen[-1] == 1.0, f"the window never closed: {seen[-5:]}"
        # It closes once and stays closed rather than flickering.
        assert seen == sorted(seen, reverse=True), f"not monotone: {seen}"

    def test_the_boundary_is_exactly_whether_the_walk_fits(self):
        sim = sim_for()
        city = city_of(sim, CityTier.BULWARK)
        d = put(sim, city, stood_for=3, run_days=1)
        during = policies.siege_damage_during_the_walk(sim, d, city)

        city.defense = during + 1.0
        assert policies.siege_urgency(sim, d, city, 5.0) == 5.0
        city.defense = during
        assert policies.siege_urgency(sim, d, city, 5.0) == 1.0


class TestThePolicyActuallyPicksIt:
    """The arithmetic above would all pass with the helpers wired to nothing."""

    FIELDS = ("floors", "run_days", "resolve_max", "resolve_in",
              "defense_damage", "population_damage", "modifier_score",
              "power_scale")

    def pair(self, sim, city, dtype=DungeonType.BASIC):
        """A reachable Siege and an ordinary dungeon, identical otherwise.

        Everything a policy scores except the sub-type is copied across, so a
        policy that cannot see the sub-type has no way to prefer either and the
        choice is decided by the fix or not at all.
        """
        siege = a_reachable_siege(sim, city, dtype=dtype)
        plain = put(sim, city, subtype="Timed", dtype=dtype)
        for f in self.FIELDS:
            setattr(plain, f, getattr(siege, f))
        return siege, plain

    @pytest.mark.parametrize("name", SCORING_POLICIES)
    @pytest.mark.parametrize("tier", list(CityTier))
    def test_it_prefers_the_siege_to_an_identical_ordinary_dungeon(
            self, name, tier):
        sim = sim_for()
        city = city_of(sim, tier)
        siege, plain = self.pair(sim, city)
        assert policies.ALL[name](sim, [plain, siege]) is siege, (
            f"{name} chose the ordinary dungeon over a Siege on the same city, "
            "with every other field it scores set equal")

    @pytest.mark.parametrize("name", SCORING_POLICIES)
    @pytest.mark.parametrize("dtype", [DungeonType.QUEST, DungeonType.BASIC])
    def test_the_subtype_is_seen_on_every_kind_of_dungeon(self, name, dtype):
        """A SIEGE IS NOT ONLY AN ORDINARY DUNGEON. `_roll_subtype` runs for
        every `_make_dungeon`, so a Quest dungeon can carry one and it bites the
        same way. Over twelve campaigns measured on 2026-09-06 the Sieges
        reaching the map were 114 Basic, 28 Fallen City, 11 Quest and 3
        Cataclysm.

        A fix applied only to the ordinary-dungeon branch would pass every other
        test in this file, which is why this one names the type.
        """
        sim = sim_for()
        city = city_of(sim, CityTier.SANCTUARY)
        siege, plain = self.pair(sim, city, dtype=dtype)
        assert policies.ALL[name](sim, [plain, siege]) is siege, (
            f"{name} cannot see a Siege carried by a {dtype.name} dungeon")

    @pytest.mark.parametrize("name", SCORING_POLICIES)
    def test_a_fired_timer_does_not_discount_a_siege(self, name):
        """Both policies cut a dungeon's value to a fifth once its timer has
        fired, because it has already spent its damage. A Siege has spent none
        of it -- it is spending it now -- so the discount would rank it lowest
        exactly while it is doing the most harm.
        """
        sim = sim_for()
        city = city_of(sim, CityTier.BULWARK)
        siege, plain = self.pair(sim, city)
        siege.resolve_in = -1.0
        plain.resolve_in = -1.0
        assert policies.ALL[name](sim, [plain, siege]) is siege, (
            f"{name} discounted a Siege whose timer had fired")

    @pytest.mark.parametrize("name", SCORING_POLICIES)
    def test_it_does_not_chase_a_siege_it_cannot_reach(self, name):
        """The other half of the ruling, and the half that costs cities if it
        is wrong. Given a hopeless Siege and an ordinary dungeon it could
        actually defend, the policy must take the ordinary one."""
        sim = sim_for()
        city = city_of(sim, CityTier.OUTPOST)
        hopeless = put(sim, city, stood_for=0, run_days=400)
        assert policies.siege_urgency(sim, hopeless, city, 5.0) == 1.0
        plain = put(sim, city, subtype="Timed", run_days=2)
        assert policies.ALL[name](sim, [hopeless, plain]) is plain, (
            f"{name} walked 400 days to a Siege that empties the city in 25")


class TestTheControlsStayBlind:
    """THE SCOPE OF THE OWNER'S RULING, HELD IN PLACE RATHER THAN ONLY WRITTEN
    DOWN.

    `triage` and `lane_aware` are the two policies that weigh damage prevented,
    and they are the two that were fixed. The rest are controls: `random_pick`
    has to stay random, `greedy_loot` has to go on ignoring the empire,
    `always_craft` has to go on letting cities burn, and `never_craft` and
    `nearest_deadline` sort by a timer and compute no damage at all, so there
    is no calculation in either to repair.

    THE GAP BETWEEN BEST AND WORST IS THE SIGNAL THIS RIG PRODUCES. Teaching a
    baseline to defend itself closes that gap by raising the floor, which looks
    like the same number moving and means the opposite thing.
    """

    @pytest.mark.parametrize("name", [n for n in policies.ALL
                                      if n not in SCORING_POLICIES])
    def test_a_control_policy_does_not_consult_the_siege_helpers(self, name):
        source = inspect.getsource(policies.ALL[name])
        assert "siege" not in source.lower(), (
            f"{name} is a control and now reads the Siege helpers. If that is "
            "deliberate, the gap between the best and worst policy no longer "
            "measures what it did before. Issue #1340.")

    @pytest.mark.parametrize("name", SCORING_POLICIES)
    def test_the_two_that_were_fixed_do_consult_them(self, name):
        """The negative above is worthless without this: if the helpers were
        renamed and nothing called them, every control would pass and the pair
        of tests would agree that the scope was respected while the fix had
        silently left the file."""
        source = inspect.getsource(policies.ALL[name])
        assert "siege_urgency" in source


class TestItReachesRealCampaigns:
    """EVERY TEST ABOVE WOULD PASS WITH THE HELPERS WIRED INTO A DEAD BRANCH.

    This one plays campaigns and counts. It is deliberately NOT the ratio the
    issue proposed as its acceptance criterion -- see this module's docstring
    for why that criterion cannot be met by any targeting change, and for the
    oracle measurement that establishes the ceiling.
    """

    def sieges_cleared(self, policy, n):
        cfg = replace(TuningConfig(), tier=1).with_tree(TREE_NONE)
        cleared = 0
        for seed in range(n):
            sim = Simulation(cfg, seed=seed)
            original = sim._finish_current

            def finish(_s=sim, _o=original):
                nonlocal cleared
                d = _s.current
                _o()
                if (d is not None and d.subtype == "Siege"
                        and d.did not in _s.dungeons):
                    cleared += 1

            sim._finish_current = finish
            sim.run(policy)
        return cleared

    @pytest.mark.parametrize("name", SCORING_POLICIES)
    def test_a_seeing_policy_clears_more_sieges_than_a_blind_one(
            self, name, monkeypatch):
        """The blind figure is produced by forcing the urgency back to 1.0,
        which reproduces the previous scoring exactly: the multiplier vanishes
        and the fired-timer discount applies again, which is what the code did
        before issue #1340."""
        n = 40
        policy = policies.ALL[name]
        seeing = self.sieges_cleared(policy, n)

        monkeypatch.setattr(policies, "siege_urgency",
                            lambda *a, **k: 1.0)
        blind = self.sieges_cleared(policy, n)

        assert seeing > blind * 1.2, (
            f"{name} cleared {seeing} Sieges over {n} campaigns at difficulty "
            f"tier 1, no empire tree, surge size 4, against {blind} with the "
            "urgency forced back to 1.0. The policy is no longer acting on a "
            "Siege differently from any other dungeon. Issue #1340.")
