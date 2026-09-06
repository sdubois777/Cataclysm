"""Which Cataclysms a campaign faces, and how many. Issue #1338.

WHY THIS FILE EXISTS. Two defects, one on top of the other, and the second was
invisible because of the first.

`TuningConfig.active_cataclysms` defaulted to 1 and nothing anywhere derived it
from the difficulty tier, while the comment directly above `CATACLYSM_ROSTER`
said "Moving up a tier adds a Cataclysm". So `sim/experiments.py`, which raises
the tier and leaves the count alone, produced every one of its figures -- at
every tier -- against a single Cataclysm.

`engine.Simulation` then took `CATACLYSM_ROSTER[:count]`, a fixed tuple whose
first entry is Demonic. So that single Cataclysm was always Demonic, which is
the only one of the eight that ignores the frontier: every lane-based policy
figure this model has produced was measured against the one Cataclysm that does
not respect lanes. Measured on the issue, at 250 campaigns per cell at tier 1:
which Cataclysm is active swings the win rate by 13.6 points against a 4.5 point
noise floor, and it flips the ordering between empire tree branches.

WHAT THE OWNER RULED, on 2026-09-06, in two parts:

- Tie the active count to the difficulty tier, and randomise which Cataclysms
  are drawn. A fixed order and leaving it alone were both offered and rejected.
- The draw is per character and it is held: "if they are on t3 with
  demonic/war/death they restart with those same cataclysms."

WHAT THAT MEANS HERE, and it is the decision this file is mostly guarding. A
campaign in this model is ONE RUN BY ONE CHARACTER -- `Simulation` plays a
single `cfg.tier`, ends the first time a Cataclysm dungeon is cleared or lost
in, and never loops. So the character is the seed: replaying a failed run is
re-running the seed, and the same seed must draw the same order. The tests below
are what make that a property of the code rather than of the docstring.

WHAT IS DELIBERATELY NOT ASSERTED. That the shuffle is uniform, beyond every
Cataclysm being able to come first. Whether every ordering is equally likely and
whether any pairing is constrained are both open design questions -- the owner's
ruling says the order is drawn per character and no more, `docs/DECISIONS.md`
lists what it leaves open, and issue #1338 carries it. A test asserting a
uniform distribution would be asserting a decision nobody has made.
"""

from __future__ import annotations

import random
from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import (Simulation, active_cataclysms_for,
                                  cataclysm_order_for)
from cataclysm_sim.modifiers import pool_for

ROSTER = TuningConfig().CATACLYSM_ROSTER
TIERS = range(1, len(ROSTER) + 1)


def at(tier: int, **overrides) -> TuningConfig:
    return replace(TuningConfig(), tier=tier, **overrides)


class TestTheCountComesFromTheDifficultyTier:
    """The half nothing implemented at all."""

    @pytest.mark.parametrize("tier", TIERS)
    def test_tier_n_faces_n_cataclysms(self, tier):
        assert at(tier).active_cataclysm_count() == tier, (
            f"difficulty tier {tier} does not face {tier} Cataclysms. The "
            f"design says a run starts against one and every boss defeated "
            f"adds another until all eight are active, so the tier is the "
            f"count. Issue #1338.")

    def test_the_default_config_is_not_pinned_to_one(self):
        """The exact defect: `active_cataclysms` was a flat 1, so raising the
        tier raised the power scale and nothing else."""
        assert at(1).active_cataclysm_count() != at(8).active_cataclysm_count(), (
            "the number of active Cataclysms is the same at tier 1 and tier 8. "
            "That was the state before issue #1338 and it is what made every "
            "figure the preset sweep produced a one-Cataclysm figure whatever "
            "its heading said.")

    def test_the_top_tier_faces_the_whole_roster(self):
        assert at(8).active_cataclysm_count() == len(ROSTER)

    @pytest.mark.parametrize("tier", [0, -3, 99])
    def test_a_tier_outside_the_table_still_gives_a_usable_count(self, tier):
        """`scoring.tier_bounds` falls back for a tier outside 1..8, so a config
        can carry one. The count must not go to zero -- a campaign with no
        active Cataclysm has an empty modifier pool and no attacker."""
        count = at(tier).active_cataclysm_count()
        assert 1 <= count <= len(ROSTER)

    def test_an_explicit_count_overrides_the_tier(self):
        """`experiments.exp_surge_modes` sweeps the count as its own axis and
        must keep being able to."""
        cfg = at(8, active_cataclysms=2)
        assert cfg.active_cataclysm_count() == 2
        assert len(Simulation(cfg, seed=0).active_types) == 2

    def test_the_engine_uses_the_derived_count(self):
        """The accessor being right proves nothing about the day loop reading
        it. Before #1338 the config comment and the engine disagreed and the
        comment was the thing people read."""
        for tier in TIERS:
            assert len(Simulation(at(tier), seed=4).active_types) == tier


class TestTheSeedIsTheCharacter:
    """The owner's replay rule, which is what fixes what a campaign means."""

    def test_the_same_seed_draws_the_same_cataclysms(self):
        """A failed run replays the same tier against the same Cataclysms. In
        this model replaying is re-running the seed, so this IS that rule."""
        for seed in range(20):
            first = Simulation(at(4), seed=seed).active_types
            replay = Simulation(at(4), seed=seed).active_types
            assert first == replay, (
                f"seed {seed} drew different Cataclysms on a replay. The owner "
                f"ruled on 2026-09-06 that a failed run restarts against the "
                f"same ones -- 'if they are on t3 with demonic/war/death they "
                f"restart with those same cataclysms'. Issue #1338.")

    def test_a_character_climbing_a_tier_keeps_what_it_already_faced(self):
        """Moving up a tier ADDS a Cataclysm. It does not re-roll the set, so
        the tier N set has to be a prefix of the tier N+1 set for one seed.

        This is why the order is drawn from the seed alone and not from the
        seed and the tier together: keyed on both, tier 3 and tier 4 would be
        two unrelated worlds rather than one character climbing."""
        for seed in range(10):
            for tier in range(1, len(ROSTER)):
                lower = Simulation(at(tier), seed=seed).active_types
                higher = Simulation(at(tier + 1), seed=seed).active_types
                assert higher[:tier] == lower, (
                    f"at seed {seed}, tier {tier + 1} does not face everything "
                    f"tier {tier} faced plus one. Moving up a tier adds a "
                    f"Cataclysm rather than drawing a new set. Issue #1338.")
                assert len(set(higher)) == tier + 1

    def test_the_draw_does_not_disturb_the_campaigns_own_generator(self):
        """Taking these draws from `Simulation.rng` would shift every later
        draw in the run, so changing the tier would silently re-roll the
        campaign and sweep cells sharing a seed block would stop being paired.
        Two configs that differ only in how many Cataclysms they activate must
        leave the main generator in the same place."""
        one = Simulation(at(1), seed=11)
        eight = Simulation(at(8), seed=11)
        assert one.rng.getstate() == eight.rng.getstate(), (
            "the Cataclysm draw is coming out of Simulation.rng, so the number "
            "of active Cataclysms shifts every other random draw in the "
            "campaign. Issue #1338.")

    def test_the_order_is_stable_across_processes(self):
        """`random.Random` seeds from a string through SHA-512, which is stable.
        `hash()` of a string is salted per process, so a switch to it would make
        every campaign irreproducible between runs while every test above still
        passed. Hence one hard-coded expected draw."""
        assert cataclysm_order_for(TuningConfig(), 0) == (
            "Chaos", "Famine", "War", "Void", "Death", "Demonic", "Celestial",
            "Pestilence")


class TestWhichCataclysmsAreDrawnActuallyVaries:
    """The half that was a fixed tuple beginning with Demonic."""

    def test_different_characters_meet_different_cataclysms(self):
        seen = {Simulation(at(1), seed=s).active_types for s in range(50)}
        assert len(seen) > 1, (
            "every campaign faces the same Cataclysm whatever its seed. That "
            "is the pre-#1338 behaviour: engine.py took a fixed prefix of a "
            "fixed roster.")

    def test_every_cataclysm_can_be_the_one_a_character_starts_against(self):
        """Not merely that the set varies -- that nothing is pinned. Demonic
        was first for every campaign this model ever ran, and Demonic is the
        only one of the eight that ignores the frontier."""
        firsts = {cataclysm_order_for(TuningConfig(), s)[0] for s in range(400)}
        assert firsts == set(ROSTER), (
            f"these Cataclysms never come first in 400 draws: "
            f"{sorted(set(ROSTER) - firsts)}. The owner ruled the order is "
            f"randomised per character with nothing always first and nothing "
            f"always last. Issue #1338.")

    def test_the_position_in_the_roster_tuple_carries_no_meaning(self):
        """`CATACLYSM_ROSTER` is a set. If a draw ever returns it unshuffled
        for most seeds, something has gone back to slicing it."""
        unshuffled = sum(1 for s in range(200)
                         if cataclysm_order_for(TuningConfig(), s) == ROSTER)
        assert unshuffled <= 2, (
            f"{unshuffled} of 200 seeds drew CATACLYSM_ROSTER in its declared "
            f"order. The tuple is the set to draw from and its order means "
            f"nothing. Issue #1338.")

    def test_a_draw_is_the_whole_roster_with_nothing_repeated(self):
        for seed in range(30):
            order = cataclysm_order_for(TuningConfig(), seed)
            assert sorted(order) == sorted(ROSTER)

    def test_active_is_the_front_of_the_order(self):
        cfg = at(3)
        assert (active_cataclysms_for(cfg, 9)
                == cataclysm_order_for(cfg, 9)[:3])


class TestTheModifierPoolGrowsWithTheTier:
    """What the config comment promised and nothing delivered: "Two active
    types means ~26 modifiers to roll from, which is what stops deep tiers
    running dry"."""

    def test_a_deeper_tier_draws_from_more_modifiers(self):
        sizes = [len(pool_for(Simulation(at(t), seed=2).active_types))
                 for t in TIERS]
        assert sizes == sorted(sizes)
        assert sizes[-1] > sizes[0], (
            "the modifier pool is the same size at tier 1 and tier 8. Active "
            "Cataclysms pool their modifiers, so it should grow with the "
            "count. Issue #1338.")

    def test_two_active_types_reach_the_size_the_comment_claims(self):
        pools = [len(pool_for(Simulation(at(2), seed=s).active_types))
                 for s in range(30)]
        assert min(pools) >= 20, (
            f"two active Cataclysms give as few as {min(pools)} modifiers to "
            f"roll from. config.py says two means about 26, which is what it "
            f"says stops deep tiers running dry.")


class TestCampaignsStillRun:
    """A guard that fails when the day loop is wrong is worth more than all of
    the above, which only check the draw."""

    @pytest.mark.parametrize("tier", TIERS)
    def test_a_campaign_completes_at_every_tier(self, tier):
        result = Simulation(at(tier), seed=6).run(policies.triage)
        assert result.survived_days > 0

    def test_every_cataclysm_can_be_played_as_the_only_active_one(self):
        """`patterns.py` gives four of the eight a rule of their own -- Demonic
        ignores the frontier, War hits the strongest city, Void erases cities,
        and Celestial sends few deep dungeons. Before #1338 only Demonic was
        ever reached by a campaign, so the other three rules ran in no
        measurement this project has on record."""
        played = set()
        for seed in range(60):
            sim = Simulation(at(1), seed=seed)
            played.add(sim.active_types[0])
            assert sim.run(policies.triage).survived_days > 0
        assert played == set(ROSTER), (
            f"never played as the only active Cataclysm: "
            f"{sorted(set(ROSTER) - played)}")


class TestTheReportSaysWhichCataclysmsItsFiguresAreAbout:
    """Issue #1338 is as much about the report not saying as about the code.

    The report already states its difficulty tier in the header, because issue
    #281 found that quoting a figure without it was quoting a figure about an
    unstated world. The active Cataclysms are the same kind of fact and were the
    same kind of silence -- worse, because the comment in `config.py` claimed
    the tier covered them.
    """

    def test_the_header_states_how_many_cataclysms_are_active(self):
        import experiments

        header = " ".join(" ".join(experiments.header_lines()).split())
        count = replace(TuningConfig(),
                        tier=experiments.SWEEP_TIER).active_cataclysm_count()
        assert f"ACTIVE CATACLYSMS {count} of 8" in header, (
            "the report header does not say how many Cataclysms its figures "
            "were measured against. It states the tier for exactly this reason "
            "-- issue #281 -- and the active set is the same kind of fact. "
            "Issue #1338.")

    def test_the_header_says_which_ones_vary(self):
        import experiments

        header = " ".join(" ".join(experiments.header_lines()).split())
        assert "WHICH ones is drawn per campaign" in header, (
            "the report header gives the active count without saying that "
            "which Cataclysms are drawn varies between campaigns. A reader "
            "otherwise takes a cell for one world measured precisely rather "
            "than an average over the worlds a player meets. Issue #1338.")

    def test_the_header_count_follows_the_sweep_tier(self, monkeypatch):
        """A hard-coded number in the header would pass both tests above and be
        wrong the moment `SWEEP_TIER` moved -- which is the defect issue #281
        found on the tier line itself."""
        import experiments

        monkeypatch.setattr(experiments, "SWEEP_TIER", 6)
        header = " ".join(" ".join(experiments.header_lines()).split())
        assert "ACTIVE CATACLYSMS 6 of 8" in header


def test_the_string_seeding_is_what_random_actually_does():
    """A control for `test_the_order_is_stable_across_processes`. If seeding a
    generator from a string ever stopped being reproducible, that test would
    fail and read as a bug in the draw."""
    assert (random.Random("cataclysm-order:0").random()
            == random.Random("cataclysm-order:0").random())
