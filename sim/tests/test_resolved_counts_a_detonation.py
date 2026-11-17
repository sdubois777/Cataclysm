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
        """It always did return above the tally. Kept so a rewrite of
        `_resolve` cannot lose that.

        SINCE 2026-10-13 ITS TIMER DOES HIT THE CITY, and this used to
        assert `not bit`. The hit is counted in `quest_hits` and not in
        `resolved`, so the tally this file is about still does not move.
        """
        sim = Simulation(TuningConfig(), seed=1)
        city = a_standing_city(sim)

        moved, bit = resolve_and_report(
            sim, sim._make_dungeon(DungeonType.QUEST, city))

        assert bit, (
            "a Quest dungeon's timer ran out on a standing city and took "
            "nothing. Since 2026-10-13 it takes an ordinary dungeon's hit; "
            "see Simulation._quest_hit")
        assert sim.quest_hits == 1
        assert moved == 0, (
            "a Quest dungeon's timer running out was counted in `resolved`. "
            "That tally is for ordinary dungeons; a Quest dungeon's hit is "
            "counted in `quest_hits`")

    def test_with_the_hit_switched_off_a_quest_dungeon_takes_nothing(self):
        """**THE CONTROL**, and the model as it was before 2026-10-13."""
        import dataclasses

        sim = Simulation(dataclasses.replace(
            TuningConfig(), quest_timer_hits_city=False), seed=1)
        city = a_standing_city(sim)

        moved, bit = resolve_and_report(
            sim, sim._make_dungeon(DungeonType.QUEST, city))

        assert not bit
        assert moved == 0 and sim.quest_hits == 0

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
        before = (self.resolved, city.defense, city.population, city.void_stacks)
        if d.dtype is not DungeonType.QUEST:
            self.old_tally += 1
        super()._resolve(d)
        # VOID STACKS COUNT AS TAKING SOMETHING, since 2026-10-18: an ordinary
        # Void dungeon's timer adds stacks instead of damage. Ruled by the
        # coordinating session that day.
        if (self.resolved > before[0]
                and (city.defense != before[1]
                     or city.population != before[2]
                     or city.void_stacks != before[3])):
            self.bites += 1


#: How many whole campaigns the tests below run. 80 since 2026-10-16, when it
#: was sized for a search this file no longer makes: the control on #1373's fix
#: looked through these campaigns for one that reached a Fallen City timer
#: running out or a dungeon on a fallen city. ISSUE #1341 MADE THAT RARER, and
#: the control now builds both states by hand; see
#: `test_the_old_arithmetic_really_did_count_more`. The two tests that still run
#: campaigns need only that some dungeon detonates, which every block does.
SEED_LIMIT = 80


@pytest.fixture(scope="module")
def campaigns() -> list[tuple[int, int, int, int]]:
    """Every campaign in `range(SEED_LIMIT)`, as
    `(seed, reported, bites, old_tally)`.

    RUN ONCE AND SHARED, because three tests want the same campaigns and each
    one costs about 30 ms.
    """
    out = []
    for seed in range(SEED_LIMIT):
        sim = _Counted(TuningConfig(), seed=seed)
        result = sim.run(policies.triage)
        out.append((seed, result.dungeons_resolved, sim.bites, sim.old_tally))
    return out


class TestOverWholeCampaigns:
    """The same claim, measured rather than constructed."""

    def test_every_counted_resolve_took_something_from_a_city(self,
                                                              campaigns):
        for seed, reported, bites, _ in campaigns:
            assert reported == bites, (
                f"campaign {seed} reported {reported} resolves but only "
                f"{bites} of them changed a city's defence or population. "
                f"RunResult.dungeons_resolved says \"times a dungeon detonated "
                f"undefeated\"; issue #1373")

    def test_a_campaign_records_some_resolves_at_all(self, campaigns):
        """The control on the class. `reported == bites` is satisfied by zero
        against zero, which is what a campaign where nothing ever detonated
        would report."""
        assert sum(reported for _, reported, _, _ in campaigns) > 0, (
            f"no dungeon detonated in {SEED_LIMIT} whole campaigns at "
            "TuningConfig() defaults, so the equality above is comparing zero "
            "against zero")

    def test_the_old_tally_never_counts_fewer(self, campaigns):
        """The reported count is a subset of what the old arithmetic counted,
        in every campaign: it only ever left timers out."""
        for seed, reported, _, old in campaigns:
            assert old >= reported, (
                f"campaign {seed}: counting every non-Quest timer gave {old}, "
                f"fewer than the {reported} that cost a city something. The "
                "second is a subset of the first and cannot exceed it")


class TestTheTwoArithmeticsDifferInTheseStates:
    """**THE CONTROL ON THE FIX ITSELF.** If the two tallies agreed there was
    nothing to fix, and every test in this file would pass against the code as
    it stood before #1373. This shows the two arithmetics differ in these
    states: a Fallen City whose timer runs out, and an ordinary dungeon whose
    city has already fallen. The old one counts each, and the reported one
    counts neither.

    BUILT BY HAND, SINCE ISSUE #1341. It used to search whole campaigns for one
    that reached either state, and #1341 made that rarer. Measured on
    2026-10-29 with `_Counted`, seeds 0 to 399 at `TuningConfig()` on `triage`:
    7 of 400 campaigns on `development` at `6d7bacf79`, and 2 of 400 once a
    Fallen City is as deep as what it absorbed. Of the 80 seeds the search
    looked through, one reached it before #1341 and none after. Shallower
    Fallen Cities, retaken sooner, are a plausible cause and were not traced.
    The states are still reachable in play; that is a measurement now rather
    than this test's job.
    """

    def test_the_old_arithmetic_really_did_count_more(self):
        sim = _Counted(TuningConfig(), seed=1)
        city = a_standing_city(sim)
        sim._resolve(sim._make_dungeon(DungeonType.FALLEN_CITY, city))
        assert (sim.old_tally, sim.resolved) == (1, 0), (
            f"a Fallen City's timer running out: the old arithmetic counted "
            f"{sim.old_tally} and the reported tally {sim.resolved}. The old "
            "one counted every non-Quest timer, so it should count this one "
            "and the reported one should not. Issue #1373")

        sim = _Counted(TuningConfig(), seed=1)
        city = a_standing_city(sim)
        d = sim._make_dungeon(DungeonType.BASIC, city)
        # FALLEN BY HAND, as in `test_an_ordinary_dungeon_on_a_fallen_city_is_not_counted`.
        city.fallen = True
        sim.empire._invalidate()
        sim._resolve(d)
        assert (sim.old_tally, sim.resolved) == (1, 0), (
            f"an ordinary dungeon's timer running out on a fallen city: the "
            f"old arithmetic counted {sim.old_tally} and the reported tally "
            f"{sim.resolved}. Issue #1373")

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
