"""The earned Cataclysm boss dungeon stands on no city, never resolves, and grows.

Issue #1324 slice 6b. THE OWNER, 2026-11-02, verbatim: "On no city
(Recommended)". Once a run has earned the boss -- the quest objectives of half
its Cataclysms met, rounded up -- it sits on no city: it never bites one, no
timer runs on it, and a Last Stand later moves it to the Pillar (issue #43's).
Until then `Simulation._maybe_open_cataclysm` put it on the Pillar.

THE COORDINATING SESSION'S RULINGS FOR THE SLICE, under the owner's delegation:

  * Its depth is a roll from the Cataclysm row (100 to 150) plus one floor for
    every ORDINARY dungeon defeated, those before it opened AND those after --
    the design's Dungeon Basics bullet and its Dungeon Types row. Until then
    the model added the count once, at opening.
  * No Siege on it, barred as the Cow Level is
    (`test_cataclysm_cannot_be_a_cow_level.py` holds the bar itself).
  * Its Cataclysm is the newest active one, the last of `active_types`.

THE GAME HOLDS THE SAME RULES in `UCataclysmEmpireRun::OpenCataclysmDungeonIfEarned`
and `DeepenCataclysmDungeon`; `tools/tests/test_surge_port.py` compares the two
sides' source, and the Unreal groups `Cataclysm.EarnedBoss.` and
`Cataclysm.EarnedBossSave.` run the game's. (This said `Cataclysm.EmpireRun.`
until the second phase of the slice; no test of the boss is in that group.)
"""

from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation


def campaign(active: int = 1, seed: int = 0, **overrides) -> Simulation:
    """A run facing `active` Cataclysms where clearing a dungeon cannot kill.

    THE DEATH ROLL IS OFF for the reason `test_engine.py` gives: `_finish_current`
    rolls before it counts anything, so a test counting clears would otherwise
    be choosing seeds that happen to survive.
    """
    cfg = replace(TuningConfig(), active_cataclysms=active, per_floor_risk=0.0,
                  boss_risk_multiplier=0.0, **overrides).with_tree(TREE_NONE)
    return Simulation(cfg, seed=seed)


def outpost(sim: Simulation):
    return next(c for c in sim.empire.cities.values()
                if c.tier is CityTier.OUTPOST)


def clear(sim: Simulation, dtype: DungeonType, sender: str = ""):
    """Clear one dungeon of this kind through `_finish_current`, the path a
    campaign takes, on a standing Outpost."""
    d = sim._make_dungeon(dtype, outpost(sim))
    d.source = sender
    sim.current = d
    sim._finish_current()
    return d


def earn_the_boss(sim: Simulation) -> None:
    """Clear quest dungeons of the run's first Cataclysms until the boss opens,
    one at a time through the ordinary path, so the opening is the clear that
    met the requirement and not a call made by hand."""
    cfg = sim.cfg
    for name in sim.active_types[:cfg.cataclysms_required()]:
        for _ in range(cfg.quest_objectives_for(name)):
            clear(sim, DungeonType.QUEST, name)


class TestWhereItStands:
    def test_it_opens_on_the_quest_clear_that_meets_the_requirement(self):
        """THE WORKED EXAMPLE: three Cataclysms, so two must be finished.
        Finishing the first opens nothing; the clear that finishes the second
        opens it, and not one clear before."""
        sim = campaign(active=3, seed=4)
        cfg = sim.cfg
        assert cfg.cataclysms_required() == 2

        first, second = sim.active_types[0], sim.active_types[1]
        for _ in range(cfg.quest_objectives_for(first)):
            clear(sim, DungeonType.QUEST, first)

        assert sim.cataclysm is None, "one Cataclysm of the two opened it"

        for _ in range(cfg.quest_objectives_for(second) - 1):
            clear(sim, DungeonType.QUEST, second)

        assert sim.cataclysm is None, "it opened one quest clear early"

        clear(sim, DungeonType.QUEST, second)

        assert sim.cataclysm is not None, (
            "the clear that finished the second Cataclysm did not open it")
        assert sim.cataclysm.did in sim.dungeons

    def test_it_stands_on_no_city(self):
        sim = campaign(seed=1)
        earn_the_boss(sim)
        boss = sim.cataclysm

        assert boss.city_id is None, (
            f"the earned boss stands on city {boss.city_id}; the owner put it "
            "on no city on 2026-11-02")
        assert boss.dtype is DungeonType.CATACLYSM

        # CONTROL: every other dungeon the run made stands on a city.
        others = [d for d in sim.dungeons.values() if d is not boss]
        assert all(d.city_id is not None for d in others)

    def test_it_is_sent_by_the_newest_active_cataclysm(self):
        sim = campaign(active=4, seed=6)
        earn_the_boss(sim)

        assert sim.cataclysm.source == sim.active_types[-1]

    def test_it_opens_once(self):
        """A second Cataclysm finished after it opened opens no second boss."""
        sim = campaign(active=3, seed=2)
        earn_the_boss(sim)
        first = sim.cataclysm

        third = sim.active_types[2]
        for _ in range(sim.cfg.quest_objectives_for(third)):
            clear(sim, DungeonType.QUEST, third)

        bosses = [d for d in sim.dungeons.values()
                  if d.dtype is DungeonType.CATACLYSM]
        assert bosses == [first]

    def test_it_never_resolves_and_bites_nothing_over_two_thousand_days(self):
        """No timer runs on it, so it cannot resolve and no city pays for it.

        THE EMPIRE'S DAYS ALONE, `_empire_day`, so nothing the player does can
        take it off the board; what is measured is what the board does to it.
        """
        sim = campaign(seed=3, max_days=5000)
        earn_the_boss(sim)
        boss = sim.cataclysm
        timer = boss.resolve_in
        resolved_before = sim.resolved

        for _ in range(2000):
            sim._empire_day()

        assert boss.did in sim.dungeons, "something took the boss off the board"
        assert boss.city_id is None
        assert boss.resolve_in == timer, (
            f"its timer moved from {timer} to {boss.resolve_in}; none runs on "
            "a dungeon that stands on no city")
        assert boss.times_resolved == 0
        # CONTROL: the board was live -- ordinary dungeons did resolve.
        assert sim.resolved > resolved_before


class TestHowDeepItIs:
    def test_it_opens_as_deep_as_its_roll_plus_every_ordinary_clear(self):
        sim = campaign(seed=5)
        for _ in range(7):
            clear(sim, DungeonType.BASIC)
        earn_the_boss(sim)

        boss = sim.cataclysm
        lo, hi = sim.cfg.spec(DungeonType.CATACLYSM, CityTier.PILLAR).floors
        assert lo <= sim.cataclysm_base_floors <= hi
        assert boss.floors == sim.cataclysm_base_floors + 7
        assert sim.cataclysm_floors_earned == 7

    def test_an_ordinary_clear_after_it_opens_deepens_it(self):
        """The half that changed on 2026-11-02. Until then the model said a
        later clear did not deepen a dungeon that already existed."""
        sim = campaign(seed=5)
        earn_the_boss(sim)
        boss = sim.cataclysm
        before_floors, before_walk = boss.floors, boss.run_days

        clear(sim, DungeonType.BASIC)
        clear(sim, DungeonType.BASIC)

        assert boss.floors == before_floors + 2
        assert boss.run_days == sim._walk_days(boss.floors, boss.subtype)
        assert boss.run_days > before_walk
        assert sim.cataclysm_floors_earned == 2

    def test_a_quest_or_fallen_city_clear_after_it_opens_does_not(self):
        sim = campaign(active=3, seed=8)
        earn_the_boss(sim)
        boss = sim.cataclysm
        before = boss.floors

        clear(sim, DungeonType.QUEST, sim.active_types[2])
        clear(sim, DungeonType.FALLEN_CITY)

        assert boss.floors == before, (
            "a Quest or Fallen City clear deepened the boss; only ordinary "
            "dungeons count")

        # CONTROL: an ordinary clear on the same run does.
        clear(sim, DungeonType.BASIC)
        assert boss.floors == before + 1

    def test_the_growth_has_no_cap(self):
        sim = campaign(seed=9)
        earn_the_boss(sim)
        for _ in range(400):
            clear(sim, DungeonType.BASIC)

        assert sim.cataclysm.floors == sim.cataclysm_base_floors + 400

    def test_the_result_reports_the_grown_depth(self):
        """`RunResult.cataclysm_floors` is the boss's depth at the end, which now
        includes the clears after it opened."""
        sim = campaign(seed=10)
        earn_the_boss(sim)
        for _ in range(3):
            clear(sim, DungeonType.BASIC)

        result = sim.run(policies.never_craft)

        assert result.cataclysm_floors >= sim.cataclysm_base_floors + 3
        assert (result.cataclysm_floors
                == sim.cataclysm_base_floors + result.cataclysm_floors_earned)


class TestACampaignThatEarnsIt:
    def test_every_policy_plays_a_run_holding_a_boss_on_no_city(self):
        """Every one of the nine policies is handed a board holding a dungeon on
        no city, and none of them may look it up as though it stood on one."""
        for name, policy in policies.ALL.items():
            sim = campaign(seed=11)
            earn_the_boss(sim)
            result = sim.run(policy)
            assert result.cataclysm_floors > 0, name
