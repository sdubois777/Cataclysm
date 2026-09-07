"""How many dungeons per surge, and how many days between, keeps an invested
player busy -- issue [#1090].

WHY THIS EXISTS. The project owner asked, in their own words: "how do we keep a
player who is fully invested into the explorer tree engaged? ... how many
dungeons per surge and how many days between to keep a player invested into the
explorer tree engaged? When I was originally thinking about this, I envisioned
spawning somewhere around 20 dungeons per surge and it would be interesting to
see the actual numbers run." They then said of what ships: "4 dungeons every 120
days is incredibly low, and boring either way. So we need to up those numbers."

So the response variable is **how much of a campaign a maxed-Explorer player
spends with nothing to do**, and the two levers are `surge_dungeon_count` and
`surge_interval_days`. NOTHING HERE WRITES A CONSTANT. Every cell replaces a
copy of the config for the length of one batch, exactly as
`analyse_siege_dose.py` does, and the recommendation lives on the issue where
the owner can rule on it.

**`surge_count_max` SILENTLY CAPS THE COUNT AXIS AT 14, AND THIS FILE LIFTS IT.**
`Simulation.surge_count` applies `min(n, surge_count_max)` to the base count and
not only to the escalation growth it reads as, and `surge_count_max` is 14
(`MostDungeonsPerSurge` on the game side). So a knob of 20, 30 or 40 spawns
exactly what a knob of 14 spawns:

    knob    4    5   10   14   20   30   40
    fires   4    5   10   14   14   14   14

Measured by calling `surge_count()` directly; `test_analysis_scripts.py` holds
that table. A sweep that did not lift the cap would print three identical rows
and would never once measure the number in the owner's question.
`base_config` therefore raises `surge_count_max` to the knob whenever the knob is
higher, which is the only way the axis means what its label says.

**THE OWNER RULED ON 2026-09-06: "Leave it, document it".** The cap stays at 14.
So this is the documentation, and it is written here because this is the file a
future sweeper of the surge count will open:

    ASKING FOR MORE THAN 14 DUNGEONS A SURGE SILENTLY GIVES 14.
    It does not warn, error, or print anything different. A sweep whose count
    axis runs past 14 measures the same cell repeatedly and reports it as a
    trend. This file lifts the cap so its own axis means something; NOTHING
    ELSE DOES, and nothing that ships may exceed 14 without moving
    `surge_count_max` here and `MostDungeonsPerSurge` in `CataclysmSurge.h`
    together.

The axis keeps one value above the cap -- 20, the number in the owner's question
-- so the grid can show what the ruling costs rather than merely asserting it.

THE OWNER'S FLOOR RANGES ARE NOT THE SHIPPED ONES, and the difference is not
cosmetic. An earlier form of the question described three layers at 5-10, 15-25
and 30-50 floors. `TuningConfig.DUNGEON_SPECS` ships four city tiers, and only
the middle one matches:

    layer in the question   shipped tier   shipped Basic floors
    5-10                    Outpost        8-15
    15-25                   Bulwark        15-25   <- the only match
    30-50                   Sanctuary      25-40
    (none)                  Pillar         40-60

Whether the question proposes those ranges, misremembers the table, or counts
three of the four tiers is a design question and not one this file can settle.
**Every figure below is measured against the SHIPPED table**, because that is
what the model and the game both implement. `section_1_walk_days` prints both.

WHAT "IDLE" MEANS HERE, BECAUSE IT IS A JUDGEMENT AND A DIFFERENT ONE GIVES A
DIFFERENT NUMBER. An idle day is a free day on which the policy chose nothing:
nothing standing that the player would enter, **and no craft available either**,
because `triage` spends materials at the forge before it will sit still. Four
readings are printed side by side:

  * **idle share of the campaign** -- `idle_days / survived_days`. THE HEADLINE,
    because the owner's question is about a player's time and not about their
    decision points. A campaign that is 66% idle is one where two days in three
    have nothing in them.
  * **idle share of free days** -- `idle_days / free_days`, which is what
    `experiments.summarise` calls `idle%` and what issues [#4] and [#1090] were
    written against. It is always the larger number, because a free day is the
    only kind of day that can be idle. **Quoting one where the other is meant
    changes the figure by tens of points**, so every number here names its
    denominator.
  * **the share of the campaign spent inside a dungeon** -- the positive reading
    of the same question, and the one that says how much of a campaign is the
    game the player came for.
  * **the split** -- of those idle days, how many had **no dungeon standing at
    all** against how many had dungeons the player could not survive, against
    how many the policy simply declined. The first is a content-supply problem
    and the second is a power problem, and the cadence only fixes the first.
    Which it is, is measured rather than assumed; the run prints the three
    columns and `main` says which way they came out.

THE DAY LEDGER CLOSES, AND A TEST CHECKS IT. `_Ledger` classifies every day of
every campaign as walking, at the forge, dead, or free, and
`sim/tests/test_analysis_scripts.py` asserts the four sum to `survived_days` and
that an instrumented campaign is identical to a plain one. Neither the subclass
nor the policy wrapper draws a random number, so they cannot move a campaign.

A NOISE FLOOR THAT IS A REAL ONE. Issue [#1379]: a difference between two seed
blocks is one difference and not a spread, and this project has quoted it as a
noise floor before. Every cell here carries the **standard error of its own
mean**, computed from the campaign-to-campaign spread inside the cell, so a
difference can be read against something. `section_5_noise_floor` checks that
analytic figure against the empirical spread of six disjoint blocks at one cell,
which is the comparison #1379 asks for.

WHAT IS HELD CONSTANT, AND IT IS PRINTED IN THE OUTPUT TOO. The `triage` policy,
static surges, resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12
days for +4% of tier width -- the settings `analyse_siege_dose.py` runs under and
the balance report uses. THE DIFFICULTY TIER AND THE EMPIRE TREE ARE AXES here
rather than constants, because the whole question is about an invested player and
half of it is what the answer costs an uninvested one.

**"AN INVESTED PLAYER" IS TWO DIFFERENT PLAYERS AND BOTH ARE MEASURED.** Issue
[#1386] found that `TREE_EXPLORER_AS_DESIGNED` -- the preset every campaign
figure this project quoted for "Explorer maxed" was measured against -- was not
the Explorer branch at full investment. It modelled one 56-point sub-build of
it: the four unconditional day-removal nodes, their total overstated at 70 days
where the branch's own is 60, and **none of the branch's five depth nodes**.
[#1399] repaired the preset, which now carries 60 flat days and +40 floors and
really is the whole branch. So the two worlds here are:

  * **Explorer whole** -- `TREE_EXPLORER_AS_DESIGNED` as it now ships. Its
    dungeons are deep enough that sixty days no longer collapses them, so depth
    still costs time.
  * **Explorer speed** -- `TREE_EXPLORER_DAY_NODES_ONLY`, defined in this file:
    the same day removal with the depth nodes left unbought. A real and cheap
    build, whose dungeons all collapse to the one-day floor. **Every "Explorer
    maxed" figure this project published before #1399 describes this one**, so
    it is kept in the grid to make those figures comparable.

**THE FLOOR BONUS IS +40 AND NOT +50.** #1386's node table sums to +40 --
20 + 10 + 20 + 0 - 10 -- while its prose said +50, and
`sim/analyse_explorer_shape.py` measured #1383 at +50. That was raised on #1386
and #1399 settled on +40, which is what the preset now carries and what this
file measures. **A figure here and a figure on #1383 therefore differ by ten
floors of investment** and are not interchangeable.

**Neither world is the shape the branch is about to have.** [#1383] measured
what shape the walk-time reduction should be, and the owner has ruled that the
four unconditional nodes become a percentage of run time rather than a fixed
subtraction. **The per-point numbers are not settled and nothing is
implemented**, so nothing here measures it, and a cadence chosen on this grid
will be read against a tree that is changing. When that shape lands, this grid
has to be re-run.

**`surge_dungeon_count` IS STILL NOT THE NUMBER OF DUNGEONS A SURGE SPAWNS**,
even with the cap lifted. `Simulation.trigger_surge` spawns
`round(surge_count() * volume)` where
`volume = (sum of the active Cataclysms' count_mult) ** cataclysm_volume_exponent`
-- so the difficulty tier, which sets how many Cataclysms are active, multiplies
the count. At tier 1 one Cataclysm is active and the realised count is within one
of the knob; at tier 8 all eight are and the multiplier is about 4.4.
`section_2_realised_size` prints the whole table, and **every grid row carries the
realised mean it actually ran at** rather than only the knob it was set from.

RUNNING IT. The default is a smoke test that proves the code path and nothing
else; every share it prints is one campaign. The real grid is 5 core-hours, so
it fans out across processes:

    CATACLYSM_SURGE_CADENCE_TRIALS=1000 CATACLYSM_SURGE_CADENCE_JOBS=12 \
        python -u sim/analyse_surge_cadence.py

`sim/tests/test_analysis_scripts.py` asserts the fanned-out grid is identical to
the single-process one, because a measurement that changes when it is split is
not a measurement.
"""

from __future__ import annotations

import concurrent.futures
import json
import math
import os
import statistics
import subprocess
import sys
from dataclasses import replace
from types import SimpleNamespace

from cataclysm_sim import policies
from cataclysm_sim.config import (TREE_ARCHITECT_AS_DESIGNED,
                                  TREE_EXPLORER_AS_DESIGNED, TREE_NONE, CityTier,
                                  DungeonType, SurgeMode, TuningConfig)
from cataclysm_sim.engine import Simulation, active_cataclysms_for
from cataclysm_sim.patterns import DEFAULT as PATTERN_DEFAULT, PATTERNS


def _env_ints(name: str, default: tuple[int, ...]) -> tuple[int, ...]:
    """A comma-separated integer axis from the environment, or the default."""
    raw = os.environ.get(name, "").strip()
    return default if not raw else tuple(int(part) for part in raw.split(","))


def _axis(name: str, full: tuple[int, ...], smoke: bool) -> tuple[int, ...]:
    """One sweep axis: whatever the environment says, else the full axis, else
    -- for a smoke run nobody asked to widen -- only its two ends.

    WHY THE SMOKE RUN IS NARROWED AT ALL. `sim/tests/test_analysis_scripts.py`
    executes this file, so its default cost is paid by continuous integration on
    every pull request. The full 24-cell grid is 192 campaigns and thirteen
    seconds of a suite that runs in about three minutes, and at one campaign a
    cell it measures nothing -- the run says so itself. The two ends are kept
    rather than the middle because they are the extremes the code has to
    survive: the smallest wave at the longest gap, and the largest wave at the
    shortest, which is also the cell where `surge_count_max` had to be lifted.
    Setting the axis explicitly, or raising `CATACLYSM_SURGE_CADENCE_TRIALS`
    past the smoke threshold, restores the whole thing.
    """
    if os.environ.get(name, "").strip() or not smoke:
        return _env_ints(name, full)
    return (full[0], full[-1]) if len(full) > 1 else full


#: Campaigns per seed block. TWO DISJOINT BLOCKS RUN AT EVERY CELL and both are
#: printed, so a cell costs twice this.
#:
#: THE DEFAULT IS A SMOKE TEST AND THE OUTPUT SAYS SO. The grid is 24 cells in
#: four worlds, which is 192 batches -- eight times what `analyse_siege_dose.py`
#: sweeps -- so the same default of 3 would cost a minute of the fast suite.
#: `sim/tests/test_analysis_scripts.py` runs this file, and none of its checks
#: depends on the size: they check the day ledger, the arithmetic tables, the
#: noise-floor identity and the fan-out, not a campaign share.
#:
#: Set `CATACLYSM_SURGE_CADENCE_TRIALS=1000` for 2,000 campaigns a cell, which is
#: the size the figures on issue [#1090] were taken at. That is 192,000 campaigns
#: and about five core-hours, so raise `CATACLYSM_SURGE_CADENCE_JOBS` with it.
TRIALS = int(os.environ.get("CATACLYSM_SURGE_CADENCE_TRIALS", "1"))

#: Below this many campaigns a block, every share printed is noise and the run
#: says so. It is also what narrows the default sweep axes; see `_axis`.
SMOKE_BELOW = 250
SMOKE = TRIALS < SMOKE_BELOW

#: Disjoint by construction at any `TRIALS`, and block A starts at seed 0.
BLOCKS = (("A", 0), ("B", TRIALS))

#: How many disjoint blocks `section_5_noise_floor` uses. Six, because six is
#: what issue [#1379] measured a real block-to-block spread over.
NOISE_BLOCKS = 6

#: The count axis. 4 is what `sim/cataclysm_sim/config.py` and
#: `UCataclysmSurgeScheduler` both ship, 5 is what `experiments.exp_calibrate`
#: chooses and the balance report runs at, 10 brackets the two, and 14 is
#: `surge_count_max` -- the most the shipped game will ever spawn, which the
#: owner ruled on 2026-09-06 stays where it is. 20 is the number in the owner's
#: question and is the one value here that CANNOT SHIP without moving that cap;
#: it is measured so the grid shows what the ruling costs. See the module
#: docstring.
COUNTS = _axis("CATACLYSM_SURGE_CADENCE_COUNTS", (4, 5, 10, 14, 20), SMOKE)

#: The interval axis, in days between surges. 120 ships; 30 is close to the
#: shortest gap `surge_interval_min` (25) would ever allow an escalating run to
#: reach, so it is the floor of what the game's own arithmetic considers sane.
INTERVALS = _axis("CATACLYSM_SURGE_CADENCE_INTERVALS", (30, 60, 90, 120), SMOKE)

#: `(label, tree, difficulty tier)`. The four worlds every cell is measured in.
#:
#: TIER 4 IS THE SECOND TIER AND HERE IS WHY. The tier is the number of active
#: Cataclysms, so it multiplies the realised surge size: at tier 4 the shipped
#: knob of 4 already spawns about 11 dungeons a surge, which puts the owner's 20
#: inside the grid at a knob of 7 or 8 rather than off the end of it. It is also
#: the midpoint of the eight-tier ladder, so it says whether a cadence chosen at
#: tier 1 survives being multiplied. Tier 8 multiplies by 4.4 and would have made
#: the knob axis meaningless -- a knob of 40 there is 176 dungeons in one wave --
#: and a no-tree player is already at the floor at every cell there, so it could
#: say nothing about what the answer costs an uninvested player.
#: The cheap half of the Explorer branch: its four unconditional day-removal
#: nodes and none of its five depth nodes. 56 of the branch's 316 points.
#:
#: THIS IS A REAL PLAYER AND THE ONE THE COMPLAINT BELONGS TO. Issue [#1386]
#: found that `TREE_EXPLORER_AS_DESIGNED` used to model exactly this sub-build
#: while being named for the whole branch, and [#1399] repaired it -- the preset
#: now carries the branch's depth nodes as well, so it is the WHOLE branch and
#: this file no longer has to invent one. What the repair leaves without a name
#: is the sub-build itself, which is what this preset is for: a player who buys
#: the cheap speed nodes and stops. Its dungeons collapse to the one-day floor,
#: and every "Explorer maxed" figure this project quoted before #1399 describes
#: it rather than a fully invested player.
#:
#: DEFINED HERE RATHER THAN IN `config.py` because it is a question this file
#: asks, not a preset the model needs, and this file changes no constant.
TREE_EXPLORER_DAY_NODES_ONLY = replace(
    TREE_EXPLORER_AS_DESIGNED,
    name="Explorer day nodes only (#1386)",
    floor_delta=0.0,
)

WORLDS = (
    ("no tree, tier 1", TREE_NONE, 1),
    ("Explorer speed, t1", TREE_EXPLORER_DAY_NODES_ONLY, 1),
    ("no tree, tier 4", TREE_NONE, 4),
    ("Explorer whole, t4", TREE_EXPLORER_AS_DESIGNED, 4),
    ("Explorer whole, t1", TREE_EXPLORER_AS_DESIGNED, 1),
    ("Architect def., t1", TREE_ARCHITECT_AS_DESIGNED, 1),
)

#: Restrict the grid to some of `WORLDS`, by comma-separated index, so a long run
#: can be split across background jobs. `CATACLYSM_SURGE_CADENCE_WORLDS=1,3` runs
#: the two Explorer worlds. Empty means all four.
_pick = os.environ.get("CATACLYSM_SURGE_CADENCE_WORLDS", "").strip()
SELECTED_INDICES = (tuple(range(len(WORLDS))) if not _pick
                    else tuple(int(i) for i in _pick.split(",")))
SELECTED = tuple(WORLDS[i] for i in SELECTED_INDICES)

#: How many worker processes the grid fans out across. 1 runs it in this process.
#: The grid is about five core-hours at `TRIALS=1000`, so this is what makes it
#: re-runnable rather than merely runnable.
JOBS = int(os.environ.get("CATACLYSM_SURGE_CADENCE_JOBS", "1"))

#: Set on a worker process by `_run_cell_out_of_process`. A worker measures one
#: cell, prints it as JSON and exits without printing a report.
WORKER_CELL = os.environ.get("CATACLYSM_SURGE_CADENCE_CELL", "").strip()

#: The layers an earlier form of the owner's question described, against the
#: tiers this model ships. See the module docstring: only the middle one matches.
OWNER_LAYERS = ((5, 10), (15, 25), (30, 50))

CITY_TIERS = (CityTier.OUTPOST, CityTier.BULWARK,
              CityTier.SANCTUARY, CityTier.PILLAR)


def base_config(tier: int = 1, count: int = 4, interval: float = 120.0,
                tree=TREE_NONE) -> TuningConfig:
    """The settings every row runs under. See the module docstring.

    THE `surge_count_max` LINE IS NOT HOUSEKEEPING. `Simulation.surge_count`
    caps the base count at it, so without this line every knob above 14 is a
    knob of 14 and the top half of the count axis measures nothing. It is raised
    only when the knob asks for more, so the shipped cap still applies at every
    cell that does not exceed it.
    """
    return replace(
        TuningConfig(),
        tier=tier,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=float(interval),
        surge_dungeon_count=count,
        surge_count_max=max(count, TuningConfig().surge_count_max),
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
    ).with_tree(tree)


#: What `development` ships, read off `TuningConfig` rather than written out so
#: this file cannot claim the wrong baseline after somebody moves the constant.
SHIPPED_COUNT = TuningConfig().surge_dungeon_count
SHIPPED_INTERVAL = TuningConfig().surge_interval_days
SHIPPED_COUNT_CAP = TuningConfig().surge_count_max
def surge_count_for(tier: int, count: int, cap: int | None = None) -> int:
    """What `Simulation.surge_count` returns for this knob, at this cap.

    THE WHOLE POINT OF THIS FILE'S CAP FIX IS IN ONE LINE OF THAT METHOD, so it
    is asked rather than reimplemented: `min(n, surge_count_max)` is applied to
    the base count and not only to the escalation growth. `cap` of None means
    the cap `base_config` uses, which is the knob itself; pass
    `SHIPPED_COUNT_CAP` to ask what the shipped game would do instead.

    CALLED UNBOUND, ON A STAND-IN CARRYING THE TWO ATTRIBUTES IT READS.
    `surge_count` reads `self.cfg` and `self.surge_index` and nothing else, and
    building a real `Simulation` builds a whole empire graph -- 96 of them, for
    a table of 96 integers, cost four seconds of the fast test suite. This keeps
    the answer coming from the engine's own method rather than a copy of it,
    which is the property that matters: a change to that line still moves this
    table. `sim/tests/test_analysis_scripts.py` checks this against a real
    `Simulation` at every knob, so the stand-in cannot quietly stop matching.
    """
    cfg = base_config(tier=tier, count=count)
    if cap is not None:
        cfg = replace(cfg, surge_count_max=cap)
    return Simulation.surge_count(SimpleNamespace(cfg=cfg, surge_index=0))


#: Cities in the empire, and how many of them can actually fall. The Pillar's
#: fall is the end of the run rather than a lost city, so "cities lost" is out of
#: the second number.
TOTAL_CITIES = sum(t.count for t in TuningConfig().TIER_STATS.values())
LOSABLE_CITIES = TOTAL_CITIES - TuningConfig().TIER_STATS[CityTier.PILLAR].count


class _Ledger(Simulation):
    """A campaign that also classifies every one of its days.

    FOUR KINDS, AND THEY ARE EXHAUSTIVE: dead, at the forge, walking a dungeon,
    or free to choose. `Simulation.step` branches on exactly these flags in
    exactly this order, and nothing between the top of `step` and that branch
    changes any of them: every write to `self.current`, `self.dying` and
    `self.crafting` in `engine.py` is either in `__init__`, in `_finish_craft`,
    in `_finish_current`, or in the player-action section of `step` itself --
    all of which are downstream of the read below. `_resolve`, `_fall`,
    `_open_last_stand`, `_apply_siege_damage` and `trigger_surge` write none of
    the three.

    `sim/tests/test_analysis_scripts.py` asserts the four sum to
    `survived_days`, which is the check that would catch a fifth branch being
    added to the day loop and this file quietly under-counting it.

    DRAWS NOTHING, so an instrumented campaign is the campaign a bare
    `Simulation` would have run. The same test asserts that too.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        self.walk_days = 0
        self.forge_days = 0
        self.dead_days = 0
        #: Free days on which the map held no dungeon at all.
        self.empty_board_days = 0
        #: Free days with dungeons standing, none inside `death_risk_tolerance`,
        #: and no craft available -- the player is blocked on POWER, not on
        #: content, and no surge cadence fixes that.
        self.no_safe_days = 0
        #: Free days where something was both standing and survivable and the
        #: policy still did nothing. `triage` has no branch that does this; the
        #: counter exists so that a policy which did would not be silently
        #: folded into `empty_board_days`.
        self.declined_days = 0
        #: Dungeons a surge actually put on the map, and how many surges fired.
        self.spawned = 0
        self.surges_fired = 0

    def step(self, policy) -> None:
        if self.dying:
            self.dead_days += 1
        elif self.crafting:
            self.forge_days += 1
        elif self.current is not None:
            self.walk_days += 1
        super().step(policy)

    def trigger_surge(self, from_city_fall: bool = False) -> None:
        before = self._next_did
        super().trigger_surge(from_city_fall=from_city_fall)
        if self._next_did > before:
            self.spawned += self._next_did - before
            self.surges_fired += 1


def ledger_policy(policy):
    """Wrap a policy so every free day is classified before it answers.

    `Simulation.death_chance` is pure, so this draws nothing.
    """
    def counted(sim, dungeons):
        standing = len(dungeons)
        tolerance = sim.cfg.death_risk_tolerance
        any_safe = any(sim.death_chance(d) <= tolerance for d in dungeons)
        choice = policy(sim, dungeons)
        if choice is None:
            if standing == 0:
                sim.empty_board_days += 1
            elif not any_safe:
                sim.no_safe_days += 1
            else:
                sim.declined_days += 1
        return choice
    return counted


def _mean_se(values: list[float]) -> tuple[float, float]:
    """Mean, and the standard error of that mean.

    THE SECOND NUMBER IS THE POINT. Campaigns inside a cell are independent
    draws, so the spread of the cell mean is the campaign-to-campaign standard
    deviation over the root of the count. A difference smaller than a couple of
    these is not a difference. Issue [#1379].
    """
    n = len(values)
    if n == 0:
        return 0.0, 0.0
    mean = statistics.fmean(values)
    if n < 2:
        return mean, float("nan")
    return mean, statistics.stdev(values) / math.sqrt(n)


def measure(cfg: TuningConfig, seed0: int, trials: int = TRIALS) -> dict:
    """One cell: `trials` campaigns from `seed0` upward at one cadence."""
    policy = ledger_policy(policies.ALL["triage"])
    sims, results = [], []
    for i in range(trials):
        sim = _Ledger(cfg, seed=seed0 + i)
        results.append(sim.run(policy))
        sims.append(sim)

    def share(fn) -> list[float]:
        return [100.0 * fn(s, r) / max(1, r.survived_days)
                for s, r in zip(sims, results, strict=True)]

    idle, idle_se = _mean_se(share(lambda s, r: r.idle_days))
    walk, _ = _mean_se(share(lambda s, r: s.walk_days))
    empty, _ = _mean_se(share(lambda s, r: s.empty_board_days))
    nosafe, _ = _mean_se(share(lambda s, r: s.no_safe_days))
    declined, _ = _mean_se(share(lambda s, r: s.declined_days))
    forge, _ = _mean_se(share(lambda s, r: s.forge_days))
    of_free, of_free_se = _mean_se(
        [100.0 * r.idle_days / max(1, r.free_days) for r in results])
    cleared, cleared_se = _mean_se([float(r.dungeons_cleared) for r in results])
    earned, earned_se = _mean_se(
        [100.0 * (r.cataclysm_floors > 0) for r in results])
    cities, cities_se = _mean_se([float(r.cities_lost) for r in results])
    return {
        "n": trials,
        # THE HEADLINE. Days with nothing to do, as a share of the campaign.
        "idle%": idle, "idle%se": idle_se,
        "empty%": empty, "noSafe%": nosafe, "declined%": declined,
        "walk%": walk, "forge%": forge,
        # The reading issues #4 and #1090 used. Always the larger of the two.
        "idleFree%": of_free, "idleFree%se": of_free_se,
        "cleared": cleared, "cleared_se": cleared_se,
        "earned%": earned, "earned%se": earned_se,
        "cities": cities, "cities_se": cities_se,
        "days": statistics.fmean([r.survived_days for r in results]),
        "won%": 100.0 * sum(1 for r in results if r.won) / trials,
        "triage%": 100.0 * statistics.fmean(
            [r.triage_pressure for r in results]),
        # WHAT THE CELL ACTUALLY SPAWNED, not the knob it was set from.
        "perSurge": statistics.fmean(
            [s.spawned / max(1, s.surges_fired) for s in sims]),
        "surges": statistics.fmean([float(s.surges_fired) for s in sims]),
    }


HEADER = (f"{'knob':>5} {'gap':>4} {'blk':>3} {'/surge':>7} {'idle%':>7} "
          f"{'+-':>5} {'empty%':>7} {'noSafe%':>8} {'inDgn%':>7} {'forge%':>7} "
          f"{'idle/free%':>11} {'cleared':>8} {'earned%':>8} {'cities':>7} "
          f"{'triage%':>8} {'days':>6}")


def row(count: int, interval: int, block: str, s: dict) -> str:
    return (f"{count:>5} {interval:>4} {block:>3} {s['perSurge']:>7.1f} "
            f"{s['idle%']:>7.1f} {s['idle%se']:>5.1f} {s['empty%']:>7.1f} "
            f"{s['noSafe%']:>8.1f} {s['walk%']:>7.1f} {s['forge%']:>7.1f} "
            f"{s['idleFree%']:>11.1f} {s['cleared']:>8.1f} "
            f"{s['earned%']:>8.1f} {s['cities']:>7.2f} {s['triage%']:>8.1f} "
            f"{s['days']:>6.0f}")


# ---------------------------------------------------------------------------
# Section 1 -- the walk-day table. Exact arithmetic, no campaigns.
# ---------------------------------------------------------------------------

def walk_day_table(tree) -> dict:
    """Walk days for the floor range of every (kind, tier), with and without the
    Cow Level doubling.

    THE COW LEVEL COLUMN IS NOT DECORATION. `Simulation._walk_days` routes that
    sub-type around `run_days_for` entirely -- "time to complete is doubled and
    cannot be reduced" -- so it is the one kind of dungeon an Explorer's 70 flat
    days do not touch, at a spawn weight of 7 in 100.
    """
    cfg = base_config(tree=tree)
    sim = Simulation(cfg, seed=0)
    out = {}
    for (dtype, tier), spec in cfg.DUNGEON_SPECS.items():
        # THE TREE'S FLOOR DELTA IS PART OF THE DEPTH, and `_make_dungeon`
        # applies it to the spec range before anything asks how long the walk
        # is. A table that read the bare spec would understate the Explorer
        # branch's real dungeons by its fifty floors and would make the walk
        # look shorter than the campaigns below actually ran. Issue [#1386].
        lo, hi = (max(1, int(round(f + tree.floor_delta))) for f in spec.floors)
        out[(dtype, tier)] = {
            "floors": (lo, hi),
            "walk": (sim.run_days_for(lo), sim.run_days_for(hi)),
            "cow": (sim._walk_days(lo, "Cow Level"),
                    sim._walk_days(hi, "Cow Level")),
        }
    return out


def section_1_walk_days() -> dict:
    print(f"\n{'=' * 118}")
    print("1. WHAT A DUNGEON COSTS AN INVESTED PLAYER. Exact arithmetic, no "
          "campaigns.")
    print("=" * 118)
    print("\n  THE OWNER'S FLOOR RANGES AGAINST THE SHIPPED ONES. Only the "
          "middle layer matches.")
    print(f"    {'layer in the question':<24}{'shipped tier':<14}"
          f"{'shipped Basic floors':<22}")
    shipped = [base_config().spec(DungeonType.BASIC, t).floors
               for t in CITY_TIERS]
    for i, tier in enumerate(CITY_TIERS):
        asked = (f"{OWNER_LAYERS[i][0]}-{OWNER_LAYERS[i][1]}"
                 if i < len(OWNER_LAYERS) else "(none)")
        mark = ("   <- the only match"
                if i < len(OWNER_LAYERS) and OWNER_LAYERS[i] == shipped[i]
                else "")
        print(f"    {asked:<24}{tier.value:<14}"
              f"{f'{shipped[i][0]}-{shipped[i][1]}':<22}{mark}")
    print("\n  Every figure in this file is measured against the SHIPPED table, "
          "because that is what")
    print("  the model and the game implement. Which set is wanted is a design "
          "question.")

    tables = {}
    for tree in (TREE_NONE, TREE_EXPLORER_AS_DESIGNED,
                 TREE_EXPLORER_DAY_NODES_ONLY):
        tables[tree.name] = walk_day_table(tree)
        print(f"\n  {tree.name}  (flat days removed: {tree.run_days_flat:g})")
        print(f"    {'kind':<12}{'tier':<11}{'floors':>10}{'walk days':>12}"
              f"{'as a Cow Level':>17}")
        for (dtype, tier), cell in sorted(
                tables[tree.name].items(),
                key=lambda kv: (kv[0][0].value, list(CITY_TIERS).index(kv[0][1]))):
            lo, hi = cell["floors"]
            wl, wh = cell["walk"]
            cl, ch = cell["cow"]
            print(f"    {dtype.value:<12}{tier.value:<11}{f'{lo}-{hi}':>10}"
                  f"{f'{wl}-{wh}':>12}{f'{cl}-{ch}':>17}")
    return tables


# ---------------------------------------------------------------------------
# Section 2 -- what the knob actually spawns. Exact arithmetic, no campaigns.
# ---------------------------------------------------------------------------

def realised_size_table(seeds: int = 400) -> dict:
    """Mean realised dungeons per surge, by difficulty tier and knob setting.

    TWO NUMBERS PER CELL AND THE DIFFERENCE BETWEEN THEM IS THE POINT.
    `uncapped` is what the knob would spawn if `surge_count_max` did not exist;
    `shipped_cap` is what it spawns under the cap this repository ships. They
    part company above a knob of 14, and a sweep that only printed the first
    would be describing a game nobody can play. `lifted` is what this file
    actually measures, which is the first, because `base_config` raises the cap.

    `Simulation.trigger_surge` spawns `round(surge_count() * volume)`, and
    `volume` depends on WHICH Cataclysms the character drew as well as how many,
    so this averages the volume over `seeds` characters rather than quoting one.
    """
    out = {}
    for tier in range(1, len(TuningConfig().CATACLYSM_ROSTER) + 1):
        cfg = base_config(tier=tier)
        volumes = []
        for seed in range(seeds):
            mults = [PATTERNS.get(t, PATTERN_DEFAULT).count_mult
                     for t in active_cataclysms_for(cfg, seed)]
            volumes.append(sum(mults) ** cfg.cataclysm_volume_exponent)
        volume = statistics.fmean(volumes)
        capped, lifted = {}, {}
        for c in COUNTS:
            capped[c] = round(surge_count_for(tier, c, SHIPPED_COUNT_CAP)
                              * volume)
            lifted[c] = round(surge_count_for(tier, c) * volume)
        out[tier] = {"volume": volume, "shipped_cap": capped, "lifted": lifted}
    return out


def section_2_realised_size() -> dict:
    table = realised_size_table()
    print(f"\n{'=' * 118}")
    print("2. THE KNOB IS NOT THE NUMBER OF DUNGEONS. Exact arithmetic, no "
          "campaigns.")
    print("=" * 118)
    print("\n  A surge spawns round(surge_count() * volume), where volume is "
          "the sum of the")
    print("  ACTIVE Cataclysms' count_mult raised to cataclysm_volume_exponent "
          f"({base_config().cataclysm_volume_exponent:g}).")
    print("  The difficulty tier IS the number active, so the tier multiplies "
          "the wave. Averaged")
    print("  over 400 characters, because which Cataclysms are drawn moves it "
          "as well as how many.")
    print("\n  FIRST, THE CAP. surge_count() applies min(n, surge_count_max) "
          "to the BASE count, and")
    print(f"  surge_count_max ships at {SHIPPED_COUNT_CAP} "
          f"(MostDungeonsPerSurge in CataclysmSurge.h). So under what ships:")
    probe = {c: surge_count_for(1, c, SHIPPED_COUNT_CAP) for c in COUNTS}
    print(f"\n    {'knob':>8}" + "".join(f"{c:>7}" for c in COUNTS))
    print(f"    {'fires':>8}" + "".join(f"{probe[c]:>7}" for c in COUNTS))
    print("\n  THIS FILE LIFTS THE CAP to the knob, so the axis means what its "
          "label says. Shipping")
    print("  any count above that therefore costs TWO constants, in the "
          "simulation and in C++.")

    print("\n  Realised dungeons a surge spawns, WITH THE CAP LIFTED (what "
          "every cell below ran at):")
    print(f"\n    {'tier':>5}{'volume':>9}"
          + "".join(f"{f'knob {c}':>10}" for c in COUNTS))
    for tier, cell in table.items():
        print(f"    {tier:>5}{cell['volume']:>9.2f}"
              + "".join(f"{cell['lifted'][c]:>10}" for c in COUNTS))
    print(f"\n  The same table UNDER THE SHIPPED CAP OF {SHIPPED_COUNT_CAP}, "
          "which is what the game does today:")
    print(f"\n    {'tier':>5}{'volume':>9}"
          + "".join(f"{f'knob {c}':>10}" for c in COUNTS))
    for tier, cell in table.items():
        mark = ("   <- the shipped knob of 4 already spawns ~20 here"
                if tier == max(table) else "")
        print(f"    {tier:>5}{cell['volume']:>9.2f}"
              + "".join(f"{cell['shipped_cap'][c]:>10}" for c in COUNTS)
              + mark)
    return table


# ---------------------------------------------------------------------------
# Section 3 -- the idle measurement at the shipped cadence.
# ---------------------------------------------------------------------------

def section_3_idle_today(measured: dict[str, dict]) -> dict:
    print(f"\n{'=' * 118}")
    print(f"3. THE IDLE MEASUREMENT AT WHAT SHIPS -- {SHIPPED_COUNT} dungeons "
          f"every {SHIPPED_INTERVAL:.0f} days, static.")
    print("=" * 118)
    print("\n  Three readings of the same campaigns. They are not "
          "interchangeable; see the module")
    print("  docstring. idle% is the share of ALL days; idle/free% is the "
          "share of the days the")
    print("  player was choosing at all, which is what issues #4 and #1090 "
          "quote.")
    print(f"\n{'world':<20} {'blk':>3} {'idle%':>7} {'+-':>5} {'empty%':>7} "
          f"{'noSafe%':>8} {'declined%':>10} {'inDgn%':>7} {'forge%':>7} "
          f"{'idle/free%':>11} {'+-':>5} {'days':>6}")
    out = {}
    for world_index in SELECTED_INDICES:
        label = WORLDS[world_index][0]
        out[label] = {}
        for block, seed0 in BLOCKS:
            s = measured[_cell_key(world_index, SHIPPED_COUNT,
                                   int(SHIPPED_INTERVAL), seed0)]
            out[label][block] = s
            print(f"{label:<20} {block:>3} {s['idle%']:>7.1f} "
                  f"{s['idle%se']:>5.1f} {s['empty%']:>7.1f} "
                  f"{s['noSafe%']:>8.1f} {s['declined%']:>10.1f} "
                  f"{s['walk%']:>7.1f} {s['forge%']:>7.1f} "
                  f"{s['idleFree%']:>11.1f} {s['idleFree%se']:>5.1f} "
                  f"{s['days']:>6.0f}")
    return out


# ---------------------------------------------------------------------------
# Section 4 -- the grid.
# ---------------------------------------------------------------------------

def _cell_key(world_index: int, count: int, interval: int, seed0: int) -> str:
    """One batch of campaigns, named by everything that decides what it is.

    THE SEED GOES IN THE NAME RATHER THAN A BLOCK LETTER so that sections 3, 4
    and 5 all name their work the same way and can be measured in one pass. It
    also makes the duplicates collapse: section 3's cell IS the grid's
    (4, 120) cell in the same world, and naming them identically means it is
    measured once rather than twice.
    """
    return f"{world_index},{count},{interval},{seed0}"


def _measure_named_cell(key: str) -> dict:
    """Measure the one cell `key` names. Both sides of the fan-out use this, so
    a worker cannot drift from the in-process path."""
    world_index, count, interval, seed0 = (int(part) for part in key.split(","))
    _, tree, tier = WORLDS[world_index]
    cfg = base_config(tier=tier, count=count, interval=interval, tree=tree)
    return measure(cfg, seed0)


def _run_cell_out_of_process(key: str) -> tuple[str, dict]:
    """Re-invoke this file as a worker for one cell and read back its JSON.

    SUBPROCESS RATHER THAN A PROCESS POOL, because this file is executed by
    `runpy.run_path` in the fast suite and has no import name a pool could
    pickle a function against, and because it calls `main()` at module level --
    a spawning pool would re-run the whole report in every child.
    `CATACLYSM_SURGE_CADENCE_CELL` is what stops that: a worker measures its
    cell, prints one JSON line and returns.
    """
    env = dict(os.environ)
    env["CATACLYSM_SURGE_CADENCE_CELL"] = key
    env["CATACLYSM_SURGE_CADENCE_TRIALS"] = str(TRIALS)
    env["CATACLYSM_SURGE_CADENCE_JOBS"] = "1"
    env["PYTHONPATH"] = os.pathsep.join(
        [os.path.dirname(os.path.abspath(__file__)),
         env.get("PYTHONPATH", "")]).rstrip(os.pathsep)
    done = subprocess.run([sys.executable, os.path.abspath(__file__)],
                          env=env, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"cell {key} failed ({done.returncode}):\n"
                           f"{done.stdout}\n{done.stderr}")
    return key, json.loads(done.stdout.strip().splitlines()[-1])


def grid_cells() -> list[str]:
    """Every cell the grid asks for, as keys."""
    return [_cell_key(i, count, interval, seed0)
            for i in SELECTED_INDICES
            for count in COUNTS
            for interval in INTERVALS
            for _, seed0 in BLOCKS]


def shipped_cadence_cells() -> list[str]:
    """Section 3's cells: what ships, in every selected world, both blocks."""
    return [_cell_key(i, SHIPPED_COUNT, int(SHIPPED_INTERVAL), seed0)
            for i in SELECTED_INDICES
            for _, seed0 in BLOCKS]


def noise_floor_cells() -> list[str]:
    """Section 5's cells: `NOISE_BLOCKS` disjoint blocks at one world."""
    return [_cell_key(SELECTED_INDICES[0], SHIPPED_COUNT,
                      int(SHIPPED_INTERVAL), b * TRIALS)
            for b in range(NOISE_BLOCKS)]


def all_cells() -> list[str]:
    """Every batch the whole report needs, deduplicated, in a stable order.

    ONE PASS FOR THE WHOLE REPORT. Sections 3 and 5 used to be measured in this
    process while only the grid fanned out, which left about a fifth of the run
    serial and made the fan-out much less useful than it looks. They name their
    work the same way the grid does, so they go through the same pass -- and
    where they name the same batch, it is measured once.
    """
    seen, out = set(), []
    for key in grid_cells() + shipped_cadence_cells() + noise_floor_cells():
        if key not in seen:
            seen.add(key)
            out.append(key)
    return out


def measure_cells(keys: list[str], jobs: int = 1) -> dict[str, dict]:
    """Measure every named cell, in this process or across `jobs` of them.

    THE TWO PATHS MUST AGREE, and `sim/tests/test_analysis_scripts.py` asserts
    they do. Every campaign is seeded from its own cell, so nothing here depends
    on the order the cells were measured in.
    """
    if jobs <= 1:
        return {key: _measure_named_cell(key) for key in keys}
    out: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for key, cell in pool.map(_run_cell_out_of_process, keys):
            out[key] = cell
    return {key: out[key] for key in keys}


def section_4_grid(measured: dict[str, dict]) -> dict:
    print(f"\n{'=' * 118}")
    print("4. THE GRID -- dungeons per surge against days between, in "
          f"{len(SELECTED)} worlds.")
    print("=" * 118)
    print("\n  knob        surge_dungeon_count, the lever that exists (with "
          "surge_count_max raised to match)")
    print("  gap         surge_interval_days")
    print("  /surge      dungeons a surge ACTUALLY spawned, mean over the "
          "campaigns in the cell")
    print("  idle%       days with nothing to do, share of the whole campaign "
          "-- THE HEADLINE")
    print("  +-          standard error of that mean, from the "
          "campaign-to-campaign spread")
    print("  empty%      of the campaign, days idle with NO dungeon standing "
          "at all")
    print("  noSafe%     of the campaign, days idle with dungeons standing "
          "that were unsurvivable")
    print("  inDgn%      share of the campaign spent inside a dungeon")
    print("  forge%      share of the campaign spent at the forge")
    print("  idle/free%  the same idle days over FREE days -- the reading "
          "issues #4 and #1090 use")
    print("  cleared     dungeons cleared per campaign")
    print("  earned%     share of campaigns that opened the EARNED Cataclysm "
          "dungeon")
    print(f"  cities      cities lost, mean per campaign, of {LOSABLE_CITIES} "
          f"that can fall ({TOTAL_CITIES} in the empire)")
    print("  triage%     free days facing 2+ dungeons about to detonate -- the "
          "empire layer's own health metric")
    if JOBS > 1:
        print(f"\n  Fanned out across {JOBS} worker processes. Every campaign "
              "is seeded from its own")
        print("  cell and block, so the grid does not depend on the order the "
              "cells were measured in.")

    out = {}
    for world_index in SELECTED_INDICES:
        label = WORLDS[world_index][0]
        print(f"\n  --- {label} " + "-" * (110 - len(label)))
        print(HEADER)
        out[label] = {}
        for count in COUNTS:
            for interval in INTERVALS:
                cell = {block: measured[_cell_key(world_index, count,
                                                  interval, seed0)]
                        for block, seed0 in BLOCKS}
                out[label][(count, interval)] = cell
                for block, _ in BLOCKS:
                    print(row(count, interval, block, cell[block]))
    return out


# ---------------------------------------------------------------------------
# Section 5 -- the noise floor, done properly.
# ---------------------------------------------------------------------------

def section_5_noise_floor(measured: dict[str, dict]) -> dict:
    """Six disjoint blocks at one cell, against the analytic standard error.

    ISSUE [#1379] IS WHAT THIS SECTION IS FOR. Two blocks give one difference.
    The spread of a block mean is what a difference has to be read against, and
    it is knowable from a single block: campaigns are independent draws, so it
    is the campaign-to-campaign standard deviation over the root of the block
    size. This section prints both and they should agree.
    """
    label = WORLDS[SELECTED_INDICES[0]][0]
    print(f"\n{'=' * 118}")
    print("5. THE NOISE FLOOR, MEASURED RATHER THAN GUESSED FROM TWO BLOCKS. "
          "Issue #1379.")
    print("=" * 118)
    print(f"\n  {NOISE_BLOCKS} disjoint blocks of {TRIALS} campaigns at "
          f"{label}, {SHIPPED_COUNT} every {SHIPPED_INTERVAL:.0f} days.")
    print(f"\n    {'block':>7}{'seeds from':>12}{'idle%':>9}"
          f"{'analytic +-':>13}")
    means, ses = [], []
    for b, key in enumerate(noise_floor_cells()):
        seed0 = b * TRIALS
        s = measured[key]
        means.append(s["idle%"])
        ses.append(s["idle%se"])
        print(f"    {chr(ord('A') + b):>7}{seed0:>12}{s['idle%']:>9.2f}"
              f"{s['idle%se']:>13.2f}")
    empirical = statistics.stdev(means) if len(means) > 1 else float("nan")
    analytic = statistics.fmean(ses)
    first_two = abs(means[0] - means[1])
    print(f"\n    block-to-block standard deviation over {NOISE_BLOCKS} "
          f"blocks   {empirical:>8.3f}")
    print(f"    mean analytic standard error of one block            "
          f"{analytic:>8.3f}")
    print(f"    the A-to-B gap alone, which is NOT a spread           "
          f"{first_two:>8.3f}")
    print("\n  The first two agree because both estimate the same quantity. "
          "The third is one")
    print("  draw from a distribution whose width is the first, and using it "
          "as a noise floor")
    print("  is what issue #1379 records going wrong.")
    return {"means": means, "empirical": empirical, "analytic": analytic,
            "a_to_b": first_two}


# ---------------------------------------------------------------------------

def settings_lines() -> list[str]:
    cfg = base_config()
    return [
        "  policy                          triage",
        f"  surge mode                      {cfg.surge_mode.value}",
        f"  resolve timer days per floor    {cfg.resolve_floor_ratio:.1f}",
        "  dungeon power per 100 days      "
        f"{cfg.dungeon_power_escalation_per_100_days:.2f}",
        f"  days per craft                  {cfg.craft_days}",
        f"  tier width gained per craft     {cfg.craft_power_gain_frac:.2f}",
        f"  lethality mode                  {cfg.lethality_mode.value}",
        f"  Explorer whole branch           run_days_flat="
        f"{TREE_EXPLORER_AS_DESIGNED.run_days_flat:g}, floors "
        f"{TREE_EXPLORER_AS_DESIGNED.floor_delta:+g} -- what config.py ships "
        "since #1399",
        f"  Explorer day nodes only         run_days_flat="
        f"{TREE_EXPLORER_DAY_NODES_ONLY.run_days_flat:g}, floors "
        f"{TREE_EXPLORER_DAY_NODES_ONLY.floor_delta:+g} -- the 56-point "
        "sub-build, issue #1386",
        f"  surge_count_max                 raised to the knob (ships at "
        f"{SHIPPED_COUNT_CAP})",
        f"  campaigns per block             {TRIALS}",
        f"  seed blocks                     A from {BLOCKS[0][1]}, "
        f"B from {BLOCKS[1][1]} (disjoint)",
        "  AXES, not constants             difficulty tier, empire tree, "
        "dungeons per surge, days between",
        "  shipped cadence                 "
        f"{SHIPPED_COUNT} dungeons every {SHIPPED_INTERVAL:.0f} days, static",
    ]


def main() -> None:
    # A worker process. One cell, one JSON line, no report. See
    # `_run_cell_out_of_process`.
    if WORKER_CELL:
        print(json.dumps(_measure_named_cell(WORKER_CELL)))
        return

    print("=" * 118)
    print("SURGE CADENCE AGAINST AN INVESTED PLAYER'S IDLE TIME -- issue #1090")
    print("=" * 118)
    print("\nSETTINGS EVERY ROW BELOW RAN UNDER. A figure from this file "
          "carries them or it is not a figure.")
    for line in settings_lines():
        print(line)
    if SMOKE:
        print(f"\n  *** {TRIALS} CAMPAIGNS A BLOCK IS A SMOKE TEST AND EVERY "
              "SHARE BELOW IS NOISE. ***")
        print("  Set CATACLYSM_SURGE_CADENCE_TRIALS=1000 for the size the "
              "figures on issue #1090 were taken at,")
        print("  and CATACLYSM_SURGE_CADENCE_JOBS to fan the grid out across "
              "processes.")

    walk = section_1_walk_days()
    realised = section_2_realised_size()

    # ONE MEASUREMENT PASS FOR SECTIONS 3, 4 AND 5. See `all_cells`: they all
    # name their batches the same way, so a batch two of them want is run once,
    # and the whole report -- not only the grid -- gets the fan-out.
    keys = all_cells()
    print(f"\nMeasuring {len(keys)} batches of {TRIALS} campaigns"
          + (f" across {JOBS} worker processes." if JOBS > 1
             else " in this process.")
          + f" {len(keys) * TRIALS} campaigns in all.")
    measured = measure_cells(keys, JOBS)

    today = section_3_idle_today(measured)
    grid = section_4_grid(measured)
    noise = section_5_noise_floor(measured)

    print(f"\n{'=' * 118}")
    print("WHAT THE RUN SAYS")
    print("=" * 118)

    speed = walk[TREE_EXPLORER_DAY_NODES_ONLY.name]
    whole = walk[TREE_EXPLORER_AS_DESIGNED.name]
    speed_basics = [speed[(DungeonType.BASIC, t)]["walk"] for t in CITY_TIERS]
    whole_basics = [whole[(DungeonType.BASIC, t)]["walk"] for t in CITY_TIERS]
    clamped = all(w == (1, 1) for w in speed_basics)
    print("\n1. THE TWO INVESTED PLAYERS WALK A SURGE AT COMPLETELY DIFFERENT "
          "SPEEDS.")
    print("   THE SPEED SUB-BUILD, which config.py ships as "
          "'Explorer maxed', walks every Basic")
    print(f"   dungeon at every city tier in "
          f"{'exactly 1 day' if clamped else 'the days above'}, from an "
          "8-floor Outpost to a 60-floor Pillar.")
    print("   Depth stops meaning time at all, so a bigger dungeon is not a "
          "longer one -- it is")
    print("   the same one day for more reward.")
    print(f"   THE WHOLE BRANCH does not clamp: its Basic dungeons walk "
          f"{whole_basics[0][0]}-{whole_basics[0][1]} days on an")
    print(f"   Outpost and {whole_basics[2][0]}-{whole_basics[2][1]} on a "
          "Sanctuary, because its fifty extra floors outrun the")
    print("   sixty days it removes. Issue #1386: these are different players "
          "and every figure")
    print("   below says which one it describes.")
    cow = speed[(DungeonType.BASIC, CityTier.OUTPOST)]["cow"]
    print(f"   THE COW LEVEL IS THE EXCEPTION FOR BOTH, at 7 in 100: it walks "
          f"{cow[0]}-{cow[1]} days on an Outpost")
    print("   for the speed sub-build because its doubling cannot be reduced.")
    deep = [(k, v) for k, v in speed.items() if v["walk"][1] > 1]
    print(f"   NOT EVERYTHING CLAMPS EVEN FOR THE SUB-BUILD: {len(deep)} of "
          f"{len(speed)} (kind, tier) pairs still")
    print("   cost more than a day at their deepest -- the endgame dungeons.")

    print("\n2. THE DIFFICULTY TIER IS ALREADY A DUNGEONS-PER-SURGE LEVER, AND "
          "A LARGER ONE.")
    for tier in sorted({1, 4, max(realised)}):
        print(f"   tier {tier}: volume {realised[tier]['volume']:.2f}, so the "
              f"shipped knob of {SHIPPED_COUNT} spawns "
              f"{realised[tier]['shipped_cap'][SHIPPED_COUNT]} dungeons a surge")
    print("   The number in the question -- about 20 a surge -- is what the "
          "SHIPPED knob already")
    print("   produces in the back half of the tier ladder. Raising the knob "
          "raises it there too.")

    print("\n3. IDLE TIME AT THE SHIPPED CADENCE, both blocks, "
          f"{TRIALS} campaigns each:")
    for label in today:
        a, b = today[label]["A"], today[label]["B"]
        print(f"   {label:<20} idle {a['idle%']:>5.1f}% / {b['idle%']:>5.1f}% "
              f"of the campaign   ({a['idleFree%']:>5.1f}% / "
              f"{b['idleFree%']:>5.1f}% of free days)   "
              f"in a dungeon {a['walk%']:>5.1f}% / {b['walk%']:>5.1f}%")
    # HOW MUCH OF THE IDLE TIME IS A POWER PROBLEM RATHER THAN A SUPPLY ONE.
    # Reported as the largest share seen anywhere rather than as "all zero":
    # these are means over a thousand campaigns, so a single campaign spending
    # one day blocked on power makes an exact test for zero fail while the
    # figure itself rounds to 0.0 in every printed column. Saying "some idle
    # days were a power block" off the back of that would be true and
    # worthless. The number is what says whether it matters.
    def worst(cells, field):
        return max(c[bl][field] for c in cells for bl in ("A", "B"))

    everywhere = list(today.values()) + [c for w in grid.values()
                                         for c in w.values()]
    worst_no_safe = worst(everywhere, "noSafe%")
    worst_declined = worst(everywhere, "declined%")
    print(f"   THE LARGEST POWER BLOCK ANYWHERE IN THIS RUN IS "
          f"{worst_no_safe:.3f}% of a campaign, and the largest")
    print(f"   share the policy simply declined is {worst_declined:.3f}%, "
          f"across {len(everywhere)} cells.")
    if max(worst_no_safe, worst_declined) < 0.05:
        print("   Both round to zero in every column printed above, so "
              "'nothing available to enter'")
        print("   and 'the player did nothing' are the same days here, and "
              "the cadence is the whole")
        print("   of what makes an idle day.")
    else:
        print("   That is large enough to read in the columns above, so part "
              "of the idle time is a")
        print("   power problem that no surge cadence fixes.")

    print("\n4. THE GRID'S HEADLINE COLUMN, idle share of the campaign, worse "
          "block of the two:")
    for label in grid:
        print(f"\n   {label}")
        print(f"     {'knob':>6}" + "".join(f"{f'{i}d':>9}" for i in INTERVALS))
        for count in COUNTS:
            cells = [max(grid[label][(count, i)]["A"]["idle%"],
                         grid[label][(count, i)]["B"]["idle%"])
                     for i in INTERVALS]
            print(f"     {count:>6}" + "".join(f"{c:>9.1f}" for c in cells))

    print(f"\n5. THE NOISE FLOOR IS {noise['empirical']:.3f} points over "
          f"{NOISE_BLOCKS} blocks, against an analytic")
    print(f"   {noise['analytic']:.3f} and an A-to-B gap of "
          f"{noise['a_to_b']:.3f}. Read every difference above against the")
    print("   first two and never against the third. Issue #1379.")

    print("\n6. THE RECOMMENDATION IS NOT IN THIS FILE. No constant changes on "
          "the strength of a")
    print("   sweep alone on this project; this prints the grid and issue "
          "#1090 carries the single")
    print("   recommendation for the owner to rule on.")
    print()


# Called unconditionally, like every other analyse_*.py here, because
# `sim/tests/test_analysis_scripts.py` executes this file with `runpy.run_path`
# and that does not set `__name__` to `"__main__"`.
main()
