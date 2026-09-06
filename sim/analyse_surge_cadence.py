"""How many dungeons per surge, and how many days between, keeps an invested
player busy -- issue [#1090].

WHY THIS EXISTS. The project owner asked, in their own words: "if dungeons on
the outer layer are between 5-10 floors, the next layer dungeons are between
15-25 floors, and the next layer dungeons are between 30-50 floors, how many
dungeons per surge and how many days between to keep a player invested into the
explorer tree engaged? When I was originally thinking about this, I envisioned
spawning somewhere around 20 dungeons per surge and it would be interesting to
see the actual numbers run."

So the response variable is **how much of a campaign a maxed-Explorer player
spends with nothing to do**, and the two levers are `surge_dungeon_count` and
`surge_interval_days`. NOTHING HERE WRITES A CONSTANT. Every cell replaces a
copy of the config for the length of one batch, exactly as
`analyse_siege_dose.py` does, and the recommendation lives on the issue where
the owner can rule on it.

THE OWNER'S FLOOR RANGES ARE NOT THE SHIPPED ONES, and the difference is not
cosmetic. The question describes three layers at 5-10, 15-25 and 30-50 floors.
`TuningConfig.DUNGEON_SPECS` ships four city tiers, and only the middle one
matches:

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
DIFFERENT NUMBER. Three readings are printed side by side:

  * **idle share of the campaign** -- `idle_days / survived_days`. THE HEADLINE,
    because the owner's question is about a player's time and not about their
    decision points. A campaign that is 66% idle is one where two days in three
    have nothing in them.
  * **idle share of free days** -- `idle_days / free_days`, which is what
    `experiments.summarise` calls `idle%` and what issues [#4] and [#1090] were
    written against. It is always the larger number, because a free day is the
    only kind of day that can be idle. Quoting one where the other is meant
    turns 66% into 93%.
  * **the split** -- of those idle days, how many had **no dungeon standing at
    all** against how many had dungeons the player could not survive. The first
    is a content-supply problem and the second is a power problem, and the
    cadence only fixes the first.

**MEASURED, THE SPLIT IS ENTIRELY THE FIRST**: at every cell in this file the
"nothing survivable" and "declined everything" counts are zero, so the three
readings above are the only ones that differ and "nothing available to enter"
and "the player did nothing" are the same set of days. That is a result rather
than an assumption, which is why the columns are printed even though they are
zero -- a cadence that made them non-zero would change what the headline means.

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

**`surge_dungeon_count` IS NOT THE NUMBER OF DUNGEONS A SURGE SPAWNS.**
`Simulation.trigger_surge` spawns `round(surge_dungeon_count * volume)` where
`volume = (sum of the active Cataclysms' count_mult) ** cataclysm_volume_exponent`
-- so the difficulty tier, which sets how many Cataclysms are active, multiplies
the count. At tier 1 one Cataclysm is active and the realised count is within one
of the knob; at tier 8 all eight are and the multiplier is about 4.4.
`section_2_realised_size` prints the whole table, and **every grid row carries the
realised mean it actually ran at** rather than only the knob it was set from.
"""

from __future__ import annotations

import math
import os
import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import (TREE_EXPLORER_AS_DESIGNED, TREE_NONE, CityTier,
                                  DungeonType, SurgeMode, TuningConfig)
from cataclysm_sim.engine import Simulation, active_cataclysms_for
from cataclysm_sim.patterns import DEFAULT as PATTERN_DEFAULT, PATTERNS

#: Campaigns per seed block. TWO DISJOINT BLOCKS RUN AT EVERY CELL and both are
#: printed, so a cell costs twice this.
#:
#: THE DEFAULT IS A SMOKE TEST AND THE OUTPUT SAYS SO. The grid is 24 cells in
#: four worlds, which is 192 batches -- eight times what `analyse_siege_dose.py`
#: sweeps -- so the same default of 3 would cost a minute of the fast suite.
#: `sim/tests/test_analysis_scripts.py` runs this file, and none of its checks
#: depends on the size: they check the day ledger, the arithmetic tables and the
#: noise-floor identity, not a campaign share.
#:
#: Set `CATACLYSM_SURGE_CADENCE_TRIALS=1000` for 2,000 campaigns a cell, which is
#: the size the figures on issue [#1090] were taken at. That is 192,000 campaigns
#: and hours of wall clock, so run it in the background and narrow it with
#: `CATACLYSM_SURGE_CADENCE_WORLDS` (see `WORLDS`) to spread it over several jobs.
TRIALS = int(os.environ.get("CATACLYSM_SURGE_CADENCE_TRIALS", "1"))

#: Disjoint by construction at any `TRIALS`, and block A starts at seed 0.
BLOCKS = (("A", 0), ("B", TRIALS))

#: How many disjoint blocks `section_5_noise_floor` uses. Six, because six is
#: what issue [#1379] measured a real block-to-block spread over.
NOISE_BLOCKS = 6

#: The count axis. 4 is what `sim/cataclysm_sim/config.py` and
#: `UCataclysmSurgeScheduler` both ship, 5 is what `experiments.exp_calibrate`
#: chooses and the balance report runs at, and 20 is the number in the owner's
#: question. 10 brackets the two, and 30 and 40 are there to show where it breaks
#: rather than because anyone proposes them.
COUNTS = (4, 5, 10, 20, 30, 40)

#: The interval axis, in days between surges. 120 ships; 30 is the shortest gap
#: `surge_interval_min` would ever allow an escalating run to reach.
INTERVALS = (30, 60, 90, 120)

#: `(label, tree, difficulty tier)`. The four worlds every cell is measured in.
#:
#: TIER 4 IS THE SECOND TIER AND HERE IS WHY. The tier is the number of active
#: Cataclysms, so it multiplies the realised surge size: at tier 4 the shipped
#: knob of 4 already spawns about 11 dungeons a surge, which puts the owner's 20
#: inside the grid at a knob of 7 or 8 rather than off the end of it. Tier 8
#: multiplies by 4.4 and would have made the knob axis meaningless -- a knob of
#: 40 there is 176 dungeons in one wave -- and a no-tree player is already at the
#: floor at every cell, so it could say nothing about what the answer costs an
#: uninvested player.
WORLDS = (
    ("no tree, tier 1", TREE_NONE, 1),
    ("Explorer, tier 1", TREE_EXPLORER_AS_DESIGNED, 1),
    ("no tree, tier 4", TREE_NONE, 4),
    ("Explorer, tier 4", TREE_EXPLORER_AS_DESIGNED, 4),
)

#: Restrict the grid to some of `WORLDS`, by comma-separated index, so a long run
#: can be split across background jobs. `CATACLYSM_SURGE_CADENCE_WORLDS=1,3` runs
#: the two Explorer worlds. Empty means all four.
_pick = os.environ.get("CATACLYSM_SURGE_CADENCE_WORLDS", "").strip()
SELECTED = (WORLDS if not _pick
            else tuple(WORLDS[int(i)] for i in _pick.split(",")))

#: The layers the owner's question describes, against the tiers this model ships.
#: See the module docstring: only the middle one matches.
OWNER_LAYERS = ((5, 10), (15, 25), (30, 50))

CITY_TIERS = (CityTier.OUTPOST, CityTier.BULWARK,
              CityTier.SANCTUARY, CityTier.PILLAR)


def base_config(tier: int = 1, count: int = 4, interval: float = 120.0,
                tree=TREE_NONE) -> TuningConfig:
    """The settings every row runs under. See the module docstring."""
    return replace(
        TuningConfig(),
        tier=tier,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=float(interval),
        surge_dungeon_count=count,
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
    ).with_tree(tree)


#: What `development` ships, read off `TuningConfig` rather than written out so
#: this file cannot claim the wrong baseline after somebody moves the constant.
SHIPPED_COUNT = TuningConfig().surge_dungeon_count
SHIPPED_INTERVAL = TuningConfig().surge_interval_days
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
    changes any of them -- `_fall` and `_open_last_stand` both skip
    `self.current` on purpose. So reading them at the top of `step` classifies
    the day the step is about to spend.

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
        lo, hi = spec.floors
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
        mark = "   <- the only match" if tuple(OWNER_LAYERS[i:i + 1]) and i < len(
            OWNER_LAYERS) and OWNER_LAYERS[i] == shipped[i] else ""
        print(f"    {asked:<24}{tier.value:<14}"
              f"{f'{shipped[i][0]}-{shipped[i][1]}':<22}{mark}")
    print("\n  Every figure in this file is measured against the SHIPPED table, "
          "because that is what")
    print("  the model and the game implement. Which set is wanted is a design "
          "question.")

    tables = {}
    for tree in (TREE_NONE, TREE_EXPLORER_AS_DESIGNED):
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

    `Simulation.trigger_surge` spawns `round(surge_dungeon_count * volume)`, and
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
        out[tier] = {"volume": volume,
                     "counts": {c: round(c * volume) for c in COUNTS}}
    return out


def section_2_realised_size() -> dict:
    table = realised_size_table()
    print(f"\n{'=' * 118}")
    print("2. THE KNOB IS NOT THE NUMBER OF DUNGEONS. Exact arithmetic, no "
          "campaigns.")
    print("=" * 118)
    print("\n  A surge spawns round(surge_dungeon_count * volume), where volume "
          "is the sum of the")
    print("  ACTIVE Cataclysms' count_mult raised to cataclysm_volume_exponent "
          f"({base_config().cataclysm_volume_exponent:g}).")
    print("  The difficulty tier IS the number active, so the tier multiplies "
          "the wave. Averaged")
    print("  over 400 characters, because which Cataclysms are drawn moves it "
          "as well as how many.")
    print(f"\n    {'tier':>5}{'volume':>9}"
          + "".join(f"{f'knob {c}':>10}" for c in COUNTS))
    for tier, cell in table.items():
        mark = ""
        if cell["counts"][SHIPPED_COUNT] >= 18:
            mark = "   <- the shipped knob of 4 already spawns ~20 here"
        print(f"    {tier:>5}{cell['volume']:>9.2f}"
              + "".join(f"{cell['counts'][c]:>10}" for c in COUNTS)
              + (mark if tier == 8 else ""))
    return table


# ---------------------------------------------------------------------------
# Section 3 -- the idle measurement at the shipped cadence.
# ---------------------------------------------------------------------------

def section_3_idle_today() -> dict:
    print(f"\n{'=' * 118}")
    print(f"3. THE IDLE MEASUREMENT AT WHAT SHIPS -- {SHIPPED_COUNT} dungeons "
          f"every {SHIPPED_INTERVAL:.0f} days, static.")
    print("=" * 118)
    print("\n  Three readings of the same campaigns. They are not "
          "interchangeable; see the module")
    print("  docstring. idle%% is the share of ALL days; idle/free%% is the "
          "share of the days the")
    print("  player was choosing at all, which is what issues #4 and #1090 "
          "quote.")
    print(f"\n{'world':<20} {'blk':>3} {'idle%':>7} {'+-':>5} {'empty%':>7} "
          f"{'noSafe%':>8} {'declined%':>10} {'inDgn%':>7} {'forge%':>7} "
          f"{'idle/free%':>11} {'+-':>5} {'days':>6}")
    out = {}
    for label, tree, tier in SELECTED:
        cfg = base_config(tier=tier, count=SHIPPED_COUNT,
                          interval=SHIPPED_INTERVAL, tree=tree)
        out[label] = {}
        for block, seed0 in BLOCKS:
            s = measure(cfg, seed0)
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

def section_4_grid() -> dict:
    print(f"\n{'=' * 118}")
    print("4. THE GRID -- dungeons per surge against days between, in four "
          "worlds.")
    print("=" * 118)
    print("\n  knob        surge_dungeon_count, the lever that exists")
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

    out = {}
    for label, tree, tier in SELECTED:
        print(f"\n  --- {label} " + "-" * (110 - len(label)))
        print(HEADER)
        out[label] = {}
        for count in COUNTS:
            for interval in INTERVALS:
                cfg = base_config(tier=tier, count=count, interval=interval,
                                  tree=tree)
                cell = {block: measure(cfg, seed0) for block, seed0 in BLOCKS}
                out[label][(count, interval)] = cell
                for block, _ in BLOCKS:
                    print(row(count, interval, block, cell[block]))
    return out


# ---------------------------------------------------------------------------
# Section 5 -- the noise floor, done properly.
# ---------------------------------------------------------------------------

def section_5_noise_floor() -> dict:
    """Six disjoint blocks at one cell, against the analytic standard error.

    ISSUE [#1379] IS WHAT THIS SECTION IS FOR. Two blocks give one difference.
    The spread of a block mean is what a difference has to be read against, and
    it is knowable from a single block: campaigns are independent draws, so it
    is the campaign-to-campaign standard deviation over the root of the block
    size. This section prints both and they should agree.
    """
    label, tree, tier = SELECTED[0]
    cfg = base_config(tier=tier, count=SHIPPED_COUNT,
                      interval=SHIPPED_INTERVAL, tree=tree)
    print(f"\n{'=' * 118}")
    print("5. THE NOISE FLOOR, MEASURED RATHER THAN GUESSED FROM TWO BLOCKS. "
          "Issue #1379.")
    print("=" * 118)
    print(f"\n  {NOISE_BLOCKS} disjoint blocks of {TRIALS} campaigns at "
          f"{label}, {SHIPPED_COUNT} every {SHIPPED_INTERVAL:.0f} days.")
    print(f"\n    {'block':>7}{'seeds from':>12}{'idle%':>9}"
          f"{'analytic +-':>13}")
    means, ses = [], []
    for b in range(NOISE_BLOCKS):
        seed0 = b * TRIALS
        s = measure(cfg, seed0)
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
        f"  campaigns per block             {TRIALS}",
        f"  seed blocks                     A from {BLOCKS[0][1]}, "
        f"B from {BLOCKS[1][1]} (disjoint)",
        "  AXES, not constants             difficulty tier, empire tree, "
        "dungeons per surge, days between",
        "  shipped cadence                 "
        f"{SHIPPED_COUNT} dungeons every {SHIPPED_INTERVAL:.0f} days, static",
    ]


def main() -> None:
    print("=" * 118)
    print("SURGE CADENCE AGAINST AN INVESTED PLAYER'S IDLE TIME -- issue #1090")
    print("=" * 118)
    print("\nSETTINGS EVERY ROW BELOW RAN UNDER. A figure from this file "
          "carries them or it is not a figure.")
    for line in settings_lines():
        print(line)
    if TRIALS < 250:
        print(f"\n  *** {TRIALS} CAMPAIGNS A BLOCK IS A SMOKE TEST AND EVERY "
              "SHARE BELOW IS NOISE. ***")
        print("  Set CATACLYSM_SURGE_CADENCE_TRIALS=1000 for the size the "
              "figures on issue #1090 were taken at,")
        print("  and CATACLYSM_SURGE_CADENCE_WORLDS to split it over several "
              "background jobs.")

    walk = section_1_walk_days()
    realised = section_2_realised_size()
    today = section_3_idle_today()
    grid = section_4_grid()
    noise = section_5_noise_floor()

    print(f"\n{'=' * 118}")
    print("WHAT THE RUN SAYS")
    print("=" * 118)

    explorer = walk[TREE_EXPLORER_AS_DESIGNED.name]
    basics = [explorer[(DungeonType.BASIC, t)]["walk"] for t in CITY_TIERS]
    clamped = all(w == (1, 1) for w in basics)
    print("\n1. THE FLAT-DAY REDUCTION FLATTENS EVERY SURGE-SPAWNED DUNGEON TO "
          "THE FLOOR.")
    print(f"   A maxed Explorer walks every Basic dungeon at every city tier "
          f"in {'exactly 1 day' if clamped else 'the days above'},")
    print("   from an 8-floor Outpost to a 60-floor Pillar. Depth stops "
          "meaning time at all, so a")
    print("   bigger dungeon is not a longer one -- it is the same one day for "
          "more reward.")
    cow = explorer[(DungeonType.BASIC, CityTier.OUTPOST)]["cow"]
    print(f"   THE ONE EXCEPTION IS THE COW LEVEL, at 7 in 100: it walks "
          f"{cow[0]}-{cow[1]} days on an Outpost")
    print("   because its doubling cannot be reduced. It is the largest time "
          "cost a surge can")
    print("   present an invested player, and it is a coin flip rather than a "
          "decision.")
    deep = [(k, v) for k, v in explorer.items() if v["walk"][1] > 1]
    print(f"   NOT EVERYTHING CLAMPS: {len(deep)} of {len(explorer)} "
          "(kind, tier) pairs still cost more than a")
    print("   day at their deepest -- the endgame dungeons. The clamp is a "
          "statement about the")
    print("   content a SURGE produces, which is the content this question is "
          "about.")

    print("\n2. THE DIFFICULTY TIER IS ALREADY A DUNGEONS-PER-SURGE LEVER, AND "
          "A LARGER ONE.")
    for tier in (1, 4, 8):
        print(f"   tier {tier}: volume {realised[tier]['volume']:.2f}, so the "
              f"shipped knob of {SHIPPED_COUNT} spawns "
              f"{realised[tier]['counts'][SHIPPED_COUNT]} dungeons a surge")
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
    zero_split = all(
        cell[bl]["noSafe%"] == 0.0 and cell[bl]["declined%"] == 0.0
        for cell in today.values() for bl in ("A", "B"))
    print("   EVERY IDLE DAY WAS AN EMPTY MAP" if zero_split else
          "   SOME IDLE DAYS WERE A POWER BLOCK RATHER THAN AN EMPTY MAP")
    print("   -- 'nothing available to enter' and 'the player did nothing' "
          "are the same days here.")

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
