"""A Cataclysm dungeon may not roll the Cow Level sub-type.

**THE RULING.** The project owner, 2026-09-06, verbatim: *"Last stand is a
cataclysm dungeon and should not be allowed to roll as a cow level sub type."*
Asked how far to take it -- one pair, the full legality matrix, or settled
together with the Fallen City question -- they answered *"Only the one you
ruled"*. Issue
[#1333](https://github.com/sdubois777/Cataclysm/issues/1333).

**WHY IT WAS RULED RATHER THAN REPAIRED.** A Cow Level's time "is doubled and
cannot be reduced". `Simulation._open_last_stand` adds the Last Stand's floor
bonuses after the dungeon is built and worked the walk out again with a bare
`run_days_for`, which knows nothing about sub-types, so a Cow Level Last Stand
walked in 145 days where 290 was correct. Offered the repair, the owner removed
the situation instead. About 7 in 100 Last Stands were affected.

**WHAT THIS FILE DOES NOT SAY.** Anything about the other 27 dungeon-type and
sub-type pairs. All of them stay legal, by the owner's explicit scope, and
`TestTheOtherPairsStayLegal` is here to keep that a decision rather than a thing
that quietly drifted -- a later session adding a second exclusion should have to
break a test that says the owner ruled one.

THE OTHER HALF OF THE PORT is `tools/tests/test_dungeon_subtype_port.py`, which
compares this rule against `UCataclysmSurgeScheduler::CataclysmForbiddenSubType`
in the game, and `Cataclysm.Surge.ACataclysmDungeonNeverRollsACowLevel`, which is
the same rule checked against the C++ implementation's behaviour rather than its
constants.
"""

from __future__ import annotations

import collections
import dataclasses

import pytest

from cataclysm_sim.config import CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation

#: Enough rolls that a 7-in-100 outcome absent from all of them is a rule and
#: not luck: the chance of missing it by accident is 0.93 ** 20000.
ROLLS = 20_000


def safe(**overrides) -> TuningConfig:
    """A config where clearing a dungeon cannot kill the player.

    The campaign-level tests below need runs to reach the end rather than to
    die on the way, and this is how `test_engine.py` does it.
    """
    return dataclasses.replace(TuningConfig(), per_floor_risk=0.0,
                               boss_risk_multiplier=0.0, **overrides)


def pillar(sim: Simulation):
    return sim.empire.cities[sim.empire.pillar_id]


def spread(sim: Simulation, dtype: DungeonType, city,
           rolls: int = ROLLS) -> collections.Counter:
    """How often each sub-type comes up for that kind of dungeon."""
    return collections.Counter(
        sim._roll_subtype(dtype, city) for _ in range(rolls))


class TestTheRule:
    def test_a_cataclysm_dungeon_never_rolls_a_cow_level(self):
        sim = Simulation(TuningConfig(), seed=0)
        seen = spread(sim, DungeonType.CATACLYSM, pillar(sim))

        assert seen["Cow Level"] == 0, (
            f"a Cataclysm dungeon rolled Cow Level {seen['Cow Level']} times "
            f"in {ROLLS}; the owner ruled on 2026-09-06 that it cannot")

    def test_and_an_ordinary_dungeon_still_does(self):
        """**THE CONTROL, and without it the test above proves nothing.** A
        `_roll_subtype` that returned "Timed" unconditionally, or a weight table
        that had lost its Cow Level row, would satisfy the rule and break the
        game. This is the same draw over the same seeds asked for a Basic
        dungeon."""
        sim = Simulation(TuningConfig(), seed=0)
        seen = spread(sim, DungeonType.BASIC, pillar(sim))

        assert seen["Cow Level"] > 0, (
            "no Basic dungeon rolled a Cow Level either, so the Cataclysm "
            "check above says nothing about the dungeon type")

    def test_the_dungeon_still_gets_a_sub_type(self):
        """A refused sub-type is redistributed, never dropped. Every dungeon has
        one since issue #1293, so a Cataclysm coming out plain would quietly
        reintroduce the outcome that issue removed."""
        sim = Simulation(TuningConfig(), seed=1)
        seen = spread(sim, DungeonType.CATACLYSM, pillar(sim))

        assert sum(seen.values()) == ROLLS
        assert all(name in sim.cfg.SUBTYPE_SPAWN_WEIGHTS for name in seen), (
            f"a Cataclysm rolled something that is not a sub-type: "
            f"{sorted(set(seen) - set(sim.cfg.SUBTYPE_SPAWN_WEIGHTS))}")

    def test_the_barred_weight_is_spread_over_the_rest_in_proportion(self):
        """Cow Level's 7 points go to the other six in proportion to theirs,
        which is what the Siege refusal already does and what the owner asked
        for here. Dropping the 7 instead would leave the six at their old
        shares of 100 and 7 in 100 rolls with nothing to return."""
        sim = Simulation(TuningConfig(), seed=2)
        w = dict(sim.cfg.SUBTYPE_SPAWN_WEIGHTS)
        seen = spread(sim, DungeonType.CATACLYSM, pillar(sim))

        total = sum(v for n, v in w.items() if n != "Cow Level")
        assert total == pytest.approx(93.0)

        for name, weight in w.items():
            if name == "Cow Level":
                continue
            wanted = 100.0 * weight / total
            got = 100.0 * seen[name] / ROLLS
            assert got == pytest.approx(wanted, abs=1.5), (
                f"{name} came up {got:.2f}% of the time where the "
                f"redistributed weights want {wanted:.2f}%")

        # AND EVERY SHARE IS ABOVE WHAT IT WAS ON THE FULL LINE, which is the
        # difference between spreading the 7 and dropping it.
        for name, weight in w.items():
            if name == "Cow Level":
                continue
            assert 100.0 * seen[name] / ROLLS > 100.0 * weight / 100.0

    def test_it_costs_exactly_one_draw(self):
        """The same promise `Cataclysm.Surge.RollingASubTypeCostsExactlyOneDraw`
        makes of the game. A wave rolls every dungeon from one stream, so a roll
        whose cost depended on the dungeon's kind would make each dungeon's
        depth depend on what the one before it drew -- and the two sides would
        stop being comparable at all.

        THE BAR IS APPLIED BY SHORTENING THE LIST rather than by refusing after
        the fact, which is what makes this true: `random.choices` takes one
        `random()` whatever the list length.
        """
        for seed in (0, 1, 7, 99, 20260906):
            rolled = Simulation(TuningConfig(), seed=seed)
            drawn = Simulation(TuningConfig(), seed=seed)

            assert rolled.rng.getstate() == drawn.rng.getstate()

            rolled._roll_subtype(DungeonType.CATACLYSM, pillar(rolled))
            drawn.rng.random()

            # THE SIEGE REFUSAL IS NOT WHAT IS BEING MEASURED and cannot fire
            # here: it takes a second draw in this model and always has, which
            # predates this rule and is issue #1329's territory. Nothing stands
            # on the Pillar in a sim that has not run, so no roll is refused.
            assert rolled.sieges_on(pillar(rolled).cid) == 0

            assert rolled.rng.getstate() == drawn.rng.getstate(), (
                f"at seed {seed} rolling a Cataclysm's sub-type left the "
                "stream somewhere a single draw does not")


class TestTheOtherPairsStayLegal:
    """The owner: "Only the one you ruled". Twenty-seven pairs are untouched."""

    @pytest.mark.parametrize("dtype", [DungeonType.BASIC, DungeonType.QUEST,
                                       DungeonType.FALLEN_CITY])
    def test_every_other_kind_of_dungeon_can_still_be_anything(self, dtype):
        sim = Simulation(TuningConfig(), seed=3)
        seen = spread(sim, dtype, pillar(sim))

        missing = set(sim.cfg.SUBTYPE_SPAWN_WEIGHTS) - set(seen)
        assert not missing, (
            f"a {dtype.value} dungeon can no longer roll {sorted(missing)}. "
            "The ruling of 2026-09-06 barred exactly one pair -- Cataclysm "
            "and Cow Level -- and said nothing about any other")

    def test_a_cataclysm_can_still_be_any_of_the_other_six(self):
        sim = Simulation(TuningConfig(), seed=4)
        seen = spread(sim, DungeonType.CATACLYSM, pillar(sim))

        wanted = set(sim.cfg.SUBTYPE_SPAWN_WEIGHTS) - {"Cow Level"}
        assert set(seen) == wanted, (
            f"a Cataclysm dungeon rolled {sorted(seen)} where the ruling "
            f"leaves it {sorted(wanted)}")

    def test_the_model_records_exactly_one_forbidden_pair(self):
        """The rule is data, so this is where "only the one you ruled" is held.

        A session that adds a second row here is making a design decision the
        owner has not made. `docs/DECISIONS.md` carries the ruling and the
        scope; issue #1342 is the nearest open question.
        """
        cfg = TuningConfig()
        assert cfg.SUBTYPES_FORBIDDEN_ON == {
            DungeonType.CATACLYSM: ("Cow Level",)}


class TestTheFightsThatActuallyGetBuilt:
    """The two Cataclysm dungeons a campaign can produce, through the real
    construction path rather than by calling the roll directly."""

    def test_no_last_stand_is_ever_a_cow_level(self):
        cfg = safe()
        built = 0

        for seed in range(300):
            sim = Simulation(cfg, seed=seed)
            sim._open_last_stand()
            built += 1
            assert sim.last_stand.subtype != "Cow Level", (
                f"the Last Stand at seed {seed} came out a Cow Level")

        assert built == 300

    def test_no_earned_cataclysm_dungeon_is_ever_a_cow_level(self):
        cfg = safe()

        for seed in range(300):
            sim = Simulation(cfg, seed=seed)
            sim.objectives = cfg.quest_objectives_required
            sim._maybe_open_cataclysm()
            assert sim.cataclysm is not None
            assert sim.cataclysm.subtype != "Cow Level", (
                f"the earned boss at seed {seed} came out a Cow Level")

    def test_the_defect_this_closes_can_no_longer_happen(self):
        """**THE ORIGINAL BUG, checked at the place it happened.** Issue #1333:
        `_open_last_stand` recomputed the walk after adding floor bonuses, and a
        Cow Level Last Stand lost its doubling -- 145 days for 145 floors where
        290 was correct.

        The walk is now taken from `_walk_days`, which knows the sub-type, so
        the two would agree even if a Cataclysm could be a Cow Level. It cannot,
        so this asserts both: the days match the helper, and the sub-type that
        made them disagree is unreachable.
        """
        cfg = safe()

        for seed in range(300):
            sim = Simulation(cfg, seed=seed)
            sim._open_last_stand()
            fight = sim.last_stand

            assert fight.run_days == sim._walk_days(fight.floors,
                                                    fight.subtype)
            assert fight.subtype != "Cow Level"

    def test_a_cow_level_walk_is_still_doubled_where_one_can_occur(self):
        """THE CONTROL FOR THE TEST ABOVE. `_walk_days` agreeing with
        `run_days_for` on every Last Stand is only evidence because the two
        genuinely differ for a Cow Level. An ordinary dungeon is where that
        still happens."""
        sim = Simulation(safe(), seed=0)

        assert sim._walk_days(145, "Cow Level") == 290
        assert sim._walk_days(145, "Timed") == 145

        for seed in range(400):
            trial = Simulation(safe(), seed=seed)
            city = next(c for c in trial.empire.cities.values()
                        if c.tier is CityTier.OUTPOST)
            d = trial._make_dungeon(DungeonType.BASIC, city)
            if d.subtype != "Cow Level":
                continue
            assert d.run_days == 2 * d.floors
            return

        pytest.fail("no seed in 400 built an ordinary Cow Level, so this "
                    "control checked nothing")
