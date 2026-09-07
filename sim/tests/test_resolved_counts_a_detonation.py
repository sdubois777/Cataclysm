"""`RunResult.dungeons_resolved` counts the times a city actually paid.

Issue [#1373]. The tally used to be raised at the top of `Simulation._resolve`,
above the guard that decides whether anything is taken from the city, so two
kinds of timer running out for free were counted as detonations:

  * a **Fallen City** dungeon, whose `resolves` is false by design; and
  * a **Basic** dungeon whose host city has already fallen.

The field's own comment says "times a dungeon detonated undefeated", which is
what made this a defect rather than a naming preference. Measured over thirty
campaigns at `TuningConfig()` defaults on the `triage` policy: 10 of 3,665
counted resolves took nothing before the line moved, and 0 of 3,655 after.

`tools/tests/test_surge_port.py` asserts the line's POSITION, in this model and
in `UCataclysmEmpireRun::ResolveDungeon`, so that the two halves of the project
keep counting the same thing. This file asserts what the number actually comes
out as, which is the half that survives `_resolve` being rewritten around it.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import DungeonType, Simulation


def a_standing_city(sim: Simulation):
    """Any city that has not fallen."""
    for city in sim.empire.cities.values():
        if not city.fallen:
            return city
    raise AssertionError("the whole empire has already fallen")


def resolve_and_report(sim: Simulation, d) -> tuple[int, bool]:
    """Run one timer out. Returns how far the tally moved, and whether the
    host city's defence or population changed."""
    city = sim.empire.cities[d.city_id]
    before = (sim.resolved, city.defense, city.population)
    sim._resolve(d)
    return (sim.resolved - before[0],
            city.defense != before[1] or city.population != before[2])


class TestWhatOneTimerRunningOutCounts:
    """Built by hand, one dungeon at a time, so each case is unambiguous."""

    def test_an_ordinary_dungeon_on_a_standing_city_counts_and_bites(self):
        """**THE CONTROL ON EVERY TEST BELOW.** Without it a tally that never
        rose at all would satisfy the three negative cases and guard nothing.
        """
        sim = Simulation(TuningConfig(), seed=1)
        city = a_standing_city(sim)

        moved, bit = resolve_and_report(
            sim, sim._make_dungeon(DungeonType.BASIC, city))

        assert moved == 1, (
            "an ordinary dungeon detonating on a standing city did not raise "
            "Simulation.resolved, so nothing below this line is measuring "
            "anything")
        assert bit, (
            "an ordinary dungeon detonating on a standing city took nothing "
            "from it")

    def test_a_fallen_city_dungeon_is_not_counted(self):
        """Its `resolves` is false: the city it stands on has already paid."""
        sim = Simulation(TuningConfig(), seed=1)
        city = a_standing_city(sim)

        moved, bit = resolve_and_report(
            sim, sim._make_dungeon(DungeonType.FALLEN_CITY, city))

        assert not bit, (
            "a Fallen City dungeon took something from a city, which is not "
            "what Dungeon.resolves says it does")
        assert moved == 0, (
            "a Fallen City dungeon's timer running out was counted as a "
            "detonation. It takes nothing, and RunResult.dungeons_resolved "
            "documents itself as \"times a dungeon detonated undefeated\". "
            "Issue #1373")

    def test_an_ordinary_dungeon_on_a_fallen_city_is_not_counted(self):
        """There is nothing left to bite, so nothing was detonated."""
        sim = Simulation(TuningConfig(), seed=1)
        city = a_standing_city(sim)
        d = sim._make_dungeon(DungeonType.BASIC, city)

        # FALLEN BY HAND RATHER THAN THROUGH `_fall`, which absorbs every
        # dungeon standing on the city and would take this one off the board
        # before its timer could be run out.
        city.fallen = True
        sim.empire._invalidate()

        moved, bit = resolve_and_report(sim, d)

        assert not bit, (
            "a dungeon detonating on a city that has already fallen took "
            "something from it")
        assert moved == 0, (
            "a dungeon whose host city had already fallen was counted as a "
            "detonation, though the guard in Simulation._resolve returns "
            "before anything is taken. Issue #1373")

    def test_a_quest_dungeon_is_not_counted(self):
        """It relocates rather than detonating, and always did return above the
        tally. Kept so a rewrite of `_resolve` cannot lose that."""
        sim = Simulation(TuningConfig(), seed=1)
        city = a_standing_city(sim)

        moved, bit = resolve_and_report(
            sim, sim._make_dungeon(DungeonType.QUEST, city))

        assert not bit
        assert moved == 0, (
            "a Quest dungeon's timer running out was counted as a detonation. "
            "It picks up and moves; see the comment in Simulation._resolve")

    def test_the_dungeon_keeps_its_own_timer_count_either_way(self):
        """`Dungeon.times_resolved` counts timers and is a different quantity.

        It is per dungeon, it says how many times this dungeon's clock ran out,
        and for a Fallen City dungeon it repeatedly does. The two counters sat
        on adjacent lines, which is how the confusion in #1373 started; this
        says the fix separated them rather than moving both.
        """
        sim = Simulation(TuningConfig(), seed=1)
        city = a_standing_city(sim)
        d = sim._make_dungeon(DungeonType.FALLEN_CITY, city)

        sim._resolve(d)
        sim._resolve(d)

        assert d.times_resolved == 2, (
            "a Fallen City dungeon's own timer count no longer rises, so "
            "Dungeon.times_resolved has been moved below the guard as well. "
            "It counts timers on purpose; RunResult.dungeons_resolved is the "
            "one that counts damage")
        assert sim.resolved == 0


class _Counted(Simulation):
    """A campaign that also counts, for every timer that ran out, whether the
    host city's defence or population actually moved -- and what the tally
    would have been under the arithmetic #1373 replaced.

    DRAWS NOTHING. `_resolve` is the only thing overridden and the override
    reads state either side of it.
    """

    def __init__(self, cfg, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        #: Timers that ran out and cost the host city something.
        self.bites = 0
        #: What `resolved` counted before #1373: every non-Quest timer.
        self.old_tally = 0

    def _resolve(self, d) -> None:
        city = self.empire.cities[d.city_id]
        before = (self.resolved, city.defense, city.population)
        if d.dtype is not DungeonType.QUEST:
            self.old_tally += 1
        super()._resolve(d)
        if (self.resolved > before[0]
                and (city.defense != before[1]
                     or city.population != before[2])):
            self.bites += 1


class TestOverWholeCampaigns:
    """The same claim, measured rather than constructed."""

    @pytest.mark.parametrize("seed", range(4))
    def test_every_counted_resolve_took_something_from_a_city(self, seed):
        sim = _Counted(TuningConfig(), seed=seed)
        result = sim.run(policies.triage)

        assert result.dungeons_resolved == sim.bites, (
            f"campaign {seed} reported {result.dungeons_resolved} resolves "
            f"but only {sim.bites} of them changed a city's defence or "
            f"population. RunResult.dungeons_resolved says \"times a dungeon "
            f"detonated undefeated\"; issue #1373")

    def test_a_campaign_records_some_resolves_at_all(self):
        """The control on the class. `reported == bites` is satisfied by zero
        against zero, which is what a campaign where nothing ever detonated
        would report."""
        sim = _Counted(TuningConfig(), seed=0)
        result = sim.run(policies.triage)

        assert result.dungeons_resolved > 0, (
            "no dungeon detonated in a whole campaign at TuningConfig() "
            "defaults, so the equality above is comparing zero against zero")

    def test_the_old_arithmetic_really_did_count_more(self):
        """**THE CONTROL ON THE FIX ITSELF.** If the two tallies agreed there
        was nothing to fix and every test above would pass against the code as
        it stood. Measured across four campaigns rather than one, because a
        single campaign need not contain either free case.
        """
        gap = 0
        for seed in range(4):
            sim = _Counted(TuningConfig(), seed=seed)
            result = sim.run(policies.triage)
            assert sim.old_tally >= result.dungeons_resolved
            gap += sim.old_tally - result.dungeons_resolved

        assert gap > 0, (
            "counting every non-Quest timer gives the same total as counting "
            "the ones that cost a city something, so no campaign here ever "
            "reaches a Fallen City dungeon or a dungeon on a fallen city and "
            "this file's negative cases are unreachable in play")


class TestTheFieldStillMeansWhatItSays:
    def test_the_comment_on_the_field_is_the_one_the_arithmetic_earns(self):
        """The defect #1373 records was a comment that outlived its code, so
        the comment is worth a guard of its own."""
        import inspect

        from cataclysm_sim import engine

        lines = [ln for ln in inspect.getsource(engine.RunResult).splitlines()
                 if "dungeons_resolved" in ln]

        assert lines, "RunResult no longer has a dungeons_resolved field"
        assert "detonated undefeated" in lines[0], (
            "RunResult.dungeons_resolved no longer documents itself as \"times "
            "a dungeon detonated undefeated\". If the meaning changed, the "
            "tests in this file and the tally's position in "
            "Simulation._resolve have to change with it. Issue #1373")
