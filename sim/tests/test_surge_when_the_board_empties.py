"""A surge fires whenever the board holds no dungeons, however it emptied.

Issue [#1406]. **The rule, ruled by the project owner on 2026-09-07, verbatim:**

    "Anytime there are no longer dungeons on the board, a surge happens."

Any empty board fires, whatever emptied it. A dungeon the player cleared and a
dungeon that detonated undefeated both count. A narrower reading -- fire only on
a clear -- was proposed and the owner overruled it, so the tests below assert
the broad rule and one of them asserts the narrow one is NOT what was built.

Three things about it are worth guarding and each has a class here:

  * the trigger fires on an empty board and only on an empty board;
  * `surge_interval_min` brakes it, and a braked trigger is delayed rather than
    lost;
  * the 120-day clock is still there. That is not decoration: a Quest dungeon
    and a Fallen City dungeon never leave the board on their own, so a trigger
    that REPLACED the clock would give a player who ignores one FEWER surges
    than today.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import DungeonType, Simulation

#: Kinds `Simulation._resolve` never takes off the board. A Quest dungeon
#: relocates and a Fallen City dungeon refreshes.
NEVER_LEAVES_ON_ITS_OWN = (DungeonType.QUEST, DungeonType.FALLEN_CITY,
                           DungeonType.CATACLYSM)


def do_nothing(sim, dungeons):
    """A policy that never enters anything, so the board is only changed by the
    day loop. `triage` would clear dungeons and confuse what is being tested."""
    return None


def quiet(**kwargs) -> TuningConfig:
    """Defaults with the clock pushed out of the way, so only the board-empty
    trigger can fire. `max_days` is left alone."""
    return dataclasses.replace(TuningConfig(), **kwargs)


def a_standing_city(sim: Simulation):
    for city in sim.empire.cities.values():
        if not city.fallen:
            return city
    raise AssertionError("the whole empire has already fallen")


def parked(cfg: TuningConfig, seed: int = 1, since: float = 999.0
           ) -> Simulation:
    """A simulation with an empty board, no surge due on the clock, and a last
    surge that **the first `step` after this will see** as `since` days ago.

    COUNTED FROM THE DAY THE STEP LANDS ON AND NOT FROM TODAY, because
    `Simulation.step` advances the day before anything else happens. Writing
    the log entry against the current day instead makes every gap here read one
    short, which is a whole day of the brake in a test measuring the brake.

    THE LOG ENTRY IS WHAT `days_since_last_surge` READS, and it is written by
    hand rather than by running a campaign so that a test can say exactly how
    long the gap is.
    """
    sim = Simulation(cfg, seed=seed)
    sim.dungeons.clear()
    sim.day = 1000
    sim.next_surge_day = 1e9              # the clock will not fire
    sim.surge_log = [(int(sim.day + 1 - since), cfg.surge_interval_days, 4)]
    return sim


class TestTheTriggerFires:
    def test_an_empty_board_fires_a_surge(self):
        sim = parked(quiet())
        assert not sim.dungeons

        sim.step(do_nothing)

        assert sim.dungeons, (
            "the board was empty, the minimum gap had long passed and the "
            "clock was not due, and no surge fired. The owner ruled on "
            "2026-09-07, verbatim \"Anytime there are no longer dungeons on "
            "the board, a surge happens\"; issue #1406")
        assert sim.surges_from_empty_board == 1

    def test_a_board_with_one_dungeon_on_it_fires_nothing(self):
        """**THE CONTROL ON THE TEST ABOVE.** Without it a trigger that fired
        every single day would pass, and so would one wired to nothing at all
        while some other path spawned the dungeons."""
        sim = parked(quiet())
        sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))
        before = len(sim.dungeons)

        sim.step(do_nothing)

        assert len(sim.dungeons) == before, (
            "a surge fired while a dungeon was still standing")
        assert sim.surges_from_empty_board == 0

    @pytest.mark.parametrize("dtype", list(DungeonType))
    def test_every_kind_of_dungeon_counts_as_something_on_the_board(self,
                                                                    dtype):
        """A Fallen City dungeon and a Quest dungeon are things to do, so
        neither leaves the board empty. This is what makes the rule readable
        from inside the game: there is always something on the board.

        THE CATACLYSM DUNGEON IS BUILT ON THE PILLAR because
        `TuningConfig.DUNGEON_SPECS` holds a row for that kind at the Pillar
        tier only, and `_make_dungeon` raises a `KeyError` anywhere else.
        """
        sim = parked(quiet())
        host = (sim.empire.cities[sim.empire.pillar_id]
                if dtype is DungeonType.CATACLYSM else a_standing_city(sim))
        sim._make_dungeon(dtype, host)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0, (
            f"a {dtype.name} dungeon standing on the map did not stop the "
            "board reading as empty")

    def test_the_dungeon_the_player_is_walking_counts_as_standing(self):
        """A player inside the last dungeon has not emptied the board, so being
        slow inside it cannot pull the next wave forward."""
        sim = parked(quiet())
        d = sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))
        sim.current = d
        sim.days_remaining = 50

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0, (
            "the board read as empty while the player was inside a dungeon")

    def test_clearing_the_last_dungeon_fires_it(self):
        """One of the two ways a board empties."""
        cfg = quiet()
        sim = parked(cfg)
        d = sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))
        sim.current = d
        sim.days_remaining = 1

        sim.step(do_nothing)          # the walk finishes; the board empties
        assert not any(x.did == d.did for x in sim.dungeons.values())
        assert sim.surges_from_empty_board == 0, (
            "the clear is only seen on the next day, by design; see the "
            "comment in Simulation.step")

        sim.step(do_nothing)
        assert sim.surges_from_empty_board == 1, (
            "clearing the last dungeon did not fire a surge")

    def test_a_dungeon_detonating_undefeated_fires_it_too(self):
        """The other way, and **the half the owner overruled a narrower rule to
        keep.** A clear-only rule would let a player buy a quiet stretch by
        letting the last dungeon detonate instead of clearing it. Under this
        rule that trade does not exist.
        """
        cfg = quiet(dungeon_persists_after_resolve=False)
        sim = parked(cfg)
        d = sim._make_dungeon(DungeonType.BASIC, a_standing_city(sim))
        d.resolve_in = 1.0

        sim.step(do_nothing)          # the timer runs out; the board empties

        assert not any(x.did == d.did for x in sim.dungeons.values()), (
            "the dungeon did not leave the board when its timer ran out")
        assert sim.surges_from_empty_board == 1, (
            "a dungeon detonating undefeated emptied the board and no surge "
            "followed. The owner overruled the narrower \"only a clear fires\" "
            "rule on 2026-09-07; issue #1406")


class TestTheMinimumGapBrakes:
    def test_it_does_not_fire_before_the_minimum_gap_has_passed(self):
        cfg = quiet(surge_interval_min=25.0)
        sim = parked(cfg, since=24.0)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 0, (
            "a surge fired 24 days after the last one, inside the 25-day "
            "surge_interval_min. That constant is now the ONLY brake on how "
            "fast waves can come; issue #1406")

    def test_it_fires_exactly_at_the_minimum_gap(self):
        """**THE CONTROL ON THE TEST ABOVE**, which a trigger that never fired
        would otherwise pass. The two differ by one day and nothing else."""
        cfg = quiet(surge_interval_min=25.0)
        sim = parked(cfg, since=25.0)

        sim.step(do_nothing)

        assert sim.surges_from_empty_board == 1, (
            "a surge did not fire at exactly surge_interval_min days")

    def test_a_braked_trigger_is_delayed_rather_than_lost(self):
        """The board is still empty tomorrow, so the check is re-asked every
        day and fires on the first day the gap allows. If it were asked only at
        the moment the board emptied, a player who emptied it early would get
        no wave at all."""
        cfg = quiet(surge_interval_min=25.0)
        sim = parked(cfg, since=20.0)

        for day in range(5):          # 20, 21, 22, 23, 24 days since
            sim.step(do_nothing)
            assert sim.surges_from_empty_board == 0, (
                f"a surge fired {20 + day} days after the last one, inside the "
                "25-day minimum gap")

        sim.step(do_nothing)          # 25 days since the last surge
        assert sim.surges_from_empty_board == 1, (
            "the trigger was blocked by the minimum gap and never re-asked")

    def test_the_shipped_minimum_gap_is_the_one_the_sweep_measured(self):
        """The value is a measurement and not a guess; issue #1406 carries it.
        This holds the number the sweep in `sim/analyse_board_empty_surge.py`
        calls its control row."""
        assert TuningConfig().surge_interval_min == 25.0, (
            "surge_interval_min has moved. It is now the only brake on the "
            "board-empty trigger as well as the floor under the escalating "
            "surge modes, so moving it needs the sweep in "
            "sim/analyse_board_empty_surge.py re-run and issue #1406 updated.")


class TestTheClockIsStillThere:
    """A trigger that REPLACED the clock would make the game easier for the
    weakest player, which is the opposite of what the rule is for."""

    def test_a_board_that_never_empties_still_gets_its_scheduled_surges(self):
        cfg = quiet()
        sim = Simulation(cfg, seed=1)
        sim.dungeons.clear()
        # A Fallen City dungeon never leaves the board on its own and nothing
        # here clears it, so the board can never empty.
        sim._make_dungeon(DungeonType.FALLEN_CITY, a_standing_city(sim))
        sim.next_surge_day = 1.0

        for _ in range(int(cfg.surge_interval_days) + 2):
            sim.step(do_nothing)

        assert len(sim.surge_log) >= 2, (
            "the 120-day clock stopped delivering surges to a board that "
            "never empties. Issue #1406 keeps the clock AND adds the trigger, "
            "firing on whichever comes first")
        assert sim.surges_from_empty_board == 0

    def test_a_board_that_nothing_can_clear_really_does_occur(self):
        """**THE CONTROL ON THE WHOLE CLASS.** If every board emptied by itself
        eventually, keeping the clock would be arguing about a state that
        cannot happen.

        Measured over whole campaigns with the rule off, which is the world the
        clock has to cover.
        """
        cfg = quiet(surge_on_empty_board=False)
        longest = 0
        campaigns_with_one = 0

        for seed in range(8):
            sim = Simulation(cfg, seed=seed)
            run = 0
            best = 0
            policy = policies.triage
            while (sim.day < cfg.max_days and not sim.lost and not sim.won):
                sim.step(policy)
                kinds = {d.dtype for d in sim.dungeons.values()}
                if kinds and kinds.issubset(NEVER_LEAVES_ON_ITS_OWN):
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
            longest = max(longest, best)
            campaigns_with_one += 1 if best else 0

        assert campaigns_with_one > 0, (
            "no campaign in eight ever held a board made only of dungeons "
            "that cannot leave on their own, so the argument for keeping the "
            "clock rests on a state that does not occur")
        assert longest > 0


class TestOverWholeCampaigns:
    """The claims that only whole campaigns can make."""

    @staticmethod
    def _run(on: bool, seed: int):
        cfg = dataclasses.replace(TuningConfig(), surge_on_empty_board=on)
        return Simulation(cfg, seed=seed).run(policies.triage)

    @pytest.mark.parametrize("seed", range(4))
    def test_the_rule_never_gives_a_campaign_fewer_surges(self, seed):
        """It fires the scheduled wave EARLY and never cancels one, so a
        campaign cannot end up with fewer waves per day than it had.

        COMPARED AS A RATE AND NOT AS A TOTAL. Campaigns are not the same
        length either side -- more waves means the empire falls sooner -- so a
        raw count of surges can go down while the cadence went up, and would
        make this assertion fail for a reason that is not a defect.
        """
        off, on = self._run(False, seed), self._run(True, seed)
        rate_off = off.surges / max(1, off.survived_days)
        rate_on = on.surges / max(1, on.survived_days)

        assert rate_on >= rate_off, (
            f"campaign {seed}: {rate_on * 1000:.2f} surges per 1,000 days with "
            f"the rule on against {rate_off * 1000:.2f} with it off. The rule "
            "is supposed to pull waves forward, never remove one")

    @pytest.mark.parametrize("seed", range(4))
    def test_with_the_rule_off_nothing_is_pulled_forward(self, seed):
        off = self._run(False, seed)
        assert off.surges_from_empty_board == 0, (
            "surge_on_empty_board is False and the trigger fired anyway")

    def test_the_rule_actually_fires_in_a_real_campaign(self):
        """**THE CONTROL ON THE CLASS.** Every assertion above is satisfied by
        a rule that never fires at all."""
        fired = sum(self._run(True, seed).surges_from_empty_board
                    for seed in range(4))
        assert fired > 0, (
            "no board emptied in four whole campaigns at TuningConfig() "
            "defaults on the triage policy, so nothing above is measuring the "
            "rule")

    @pytest.mark.parametrize("seed", range(4))
    def test_the_flag_gates_the_whole_trigger(self, seed):
        """With the flag off the campaign is exactly the campaign a model
        WITHOUT the trigger runs.

        Compared against a subclass whose `_maybe_surge_on_empty_board` does
        nothing at all, which is the model as it stood before issue #1406. A
        flag that was read in the wrong place -- or not read -- would show up
        here as a different campaign rather than as a different counter.
        """
        class NoTrigger(Simulation):
            def _maybe_surge_on_empty_board(self) -> None:
                return

        cfg = dataclasses.replace(TuningConfig(), surge_on_empty_board=False)
        assert NoTrigger(cfg, seed=seed).run(policies.triage) == \
            Simulation(cfg, seed=seed).run(policies.triage), (
            "with surge_on_empty_board off, the campaign still differs from "
            "one that never asks the question")
