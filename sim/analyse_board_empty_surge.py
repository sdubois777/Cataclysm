"""What a surge fired by an empty board costs, and how short the minimum gap
between surges can safely be -- issue [#1406].

**THE RULE, RULED BY THE PROJECT OWNER ON 2026-09-07, VERBATIM:**

    "Anytime there are no longer dungeons on the board, a surge happens."

Any empty board fires, whatever emptied it. A dungeon the player cleared and a
dungeon that detonated undefeated both count. A narrower reading -- fire only on
a clear -- was proposed and the owner overruled it.
`TuningConfig.surge_on_empty_board` carries the rule and why the narrow version
was worse.

WHAT THIS FILE IS FOR. The rule itself is not in question; two things about it
are, and both are measurements rather than arguments.

  1. **Does keeping the 120-day clock as well actually matter?** It does if a
     board can stay non-empty forever, and section 1 shows that it can.
  2. **Is `surge_interval_min` of 25 days the right brake?** It is now the only
     one, so section 4 sweeps it. NOTHING HERE WRITES A CONSTANT. Every cell
     replaces a copy of the config for the length of one batch, exactly as
     `analyse_siege_dose.py` and `analyse_surge_cadence.py` do, and the
     recommendation lives on the issue where the owner can rule on it.

**THE WEAKEST PLAYER IS MEASURED FIRST AND IS WORLD 0.** A player with no empire
tree loses every losable city at difficulty tier 4 at the shipped cadence and
reaches the earned Cataclysm dungeon in almost no campaign -- issue [#1392].
Under this rule their dungeons detonating undefeated pull the next wave forward,
so that is the case most likely to be made too harsh, and the minimum gap is the
lever if it is. A stronger player emptying the board faster is the case the rule
was asked for and is not the one that can go wrong.

**THE SETTINGS MATCH `analyse_surge_cadence.base_config` EXACTLY**, so a figure
here can be read against the ones issue [#1090] recorded. They are copied rather
than imported because that file calls `main()` at module level and importing it
would run its whole 50-minute report.
`sim/tests/test_analysis_scripts.py::test_the_two_surge_scripts_measure_the_same_world`
asserts the two configurations are identical, so they cannot drift apart in
silence.

WHAT "IDLE" MEANS HERE IS THE SAME JUDGEMENT `analyse_surge_cadence.py` MAKES,
and it is copied for the same reason. An idle day is a free day on which the
policy chose nothing: nothing standing that the player would enter, and no craft
available either, because `triage` spends materials at the forge before it will
sit still. Of those idle days the run separates:

  * **empty board** -- nothing on the map at all. The share this rule exists to
    remove, and the only one a surge cadence can touch.
  * **nothing survivable** -- dungeons standing, none inside
    `death_risk_tolerance`. A POWER problem. No cadence fixes it and firing more
    surges at it makes it worse.
  * **declined** -- something standing, survivable, and the policy did nothing
    anyway. `triage` has no branch that does this; the column exists so that a
    policy which did would not be folded into the first.

**EVERY FIGURE NAMES ITS THREE CONDITIONS**, because on this project a campaign
figure without them is not a figure: the surge size, the number of active
Cataclysms, and the policy. All three default to something the balance report
does not use. `settings_lines` prints them and section 0 repeats them.

Cost: about 33 ms a campaign, so the default report of
`CATACLYSM_BOARD_EMPTY_TRIALS=1` is a smoke test that measures nothing and says
so. Set it to 1000 for 2,000 campaigns a cell -- the size the figures on #1090
were taken at -- and raise `CATACLYSM_BOARD_EMPTY_JOBS` with it.
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

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_ARCHITECT_AS_DESIGNED,
    TREE_EXPLORER_AS_DESIGNED,
    TREE_NONE,
    SurgeMode,
    TuningConfig,
)
from cataclysm_sim.engine import DungeonType, Simulation

# ---------------------------------------------------------------------------
# Axes and settings
# ---------------------------------------------------------------------------

#: Campaigns per seed block. TWO DISJOINT BLOCKS RUN AT EVERY CELL, so a cell
#: costs twice this. See the module docstring for what to set it to.
TRIALS = int(os.environ.get("CATACLYSM_BOARD_EMPTY_TRIALS", "1"))

#: Below this many campaigns a block, every share printed is noise, and the run
#: says so rather than letting a reader quote it.
SMOKE_BELOW = 250
SMOKE = TRIALS < SMOKE_BELOW

#: Disjoint by construction at any `TRIALS`, and block A starts at seed 0.
BLOCKS = (("A", 0), ("B", TRIALS))

#: How many disjoint blocks the noise floor uses. Six, because six is what issue
#: [#1379] measured a real block-to-block spread over. A smoke run takes three,
#: because at one campaign a block the spread is meaningless either way and the
#: only thing being exercised is the code that computes it.
NOISE_BLOCKS = 6 if not SMOKE else 3

#: How many worker processes the report fans out across. 1 runs it in this one.
JOBS = int(os.environ.get("CATACLYSM_BOARD_EMPTY_JOBS", "1"))

#: Set on a worker by `_run_cell_out_of_process`. A worker measures one cell,
#: prints it as JSON and exits without printing a report.
WORKER_CELL = os.environ.get("CATACLYSM_BOARD_EMPTY_CELL", "").strip()


def _env_floats(name: str, default: tuple[float, ...]) -> tuple[float, ...]:
    raw = os.environ.get(name, "").strip()
    return default if not raw else tuple(float(p) for p in raw.split(","))


#: What `development` ships, read off `TuningConfig` rather than written out, so
#: this file cannot claim the wrong baseline after somebody moves a constant.
SHIPPED_MIN_GAP = TuningConfig().surge_interval_min
SHIPPED_INTERVAL = TuningConfig().surge_interval_days
SHIPPED_COUNT = TuningConfig().surge_dungeon_count

#: The minimum-gap axis, in days. 25 is what `TuningConfig.surge_interval_min`
#: ships and is therefore the control; the rest bracket it in both directions so
#: the sweep can say whether the shipped value is a plateau or a slope. 120 is
#: the shipped surge interval, at which the brake is as strong as the clock and
#: the rule can add no surge at all -- the far end that says what the rule is
#: worth in total.
MIN_GAPS = _env_floats("CATACLYSM_BOARD_EMPTY_GAPS",
                       (10.0, 15.0, 25.0, 40.0, 60.0, 90.0, 120.0))

#: `(label, tree, difficulty tier)`. **World 0 is the weakest player and is
#: measured first**; see the module docstring.
WORLDS = (
    ("no tree, tier 4", TREE_NONE, 4),
    ("no tree, tier 1", TREE_NONE, 1),
    ("Explorer whole, t1", TREE_EXPLORER_AS_DESIGNED, 1),
    ("Explorer whole, t4", TREE_EXPLORER_AS_DESIGNED, 4),
    ("Architect def., t1", TREE_ARCHITECT_AS_DESIGNED, 1),
)

#: Restrict the report to some of `WORLDS`, by comma-separated index, so a long
#: run can be split across background jobs. Empty means all of them.
#:
#: **A SMOKE RUN TAKES TWO WORLDS AND NOT FIVE, AND THIS IS WHY.**
#: `sim/tests/test_analysis_scripts.py` executes this file, so its default cost
#: is paid by continuous integration on every pull request. At one campaign a
#: cell it measures nothing -- the run says so itself -- and a campaign here is
#: 800 to 1,800 days rather than the 300 a shorter setting gives, so the whole
#: report costs seconds of a suite that is meant to be short. The two kept are
#: the weakest player, whom every conclusion is read off, and the strongest,
#: which is the world with the most idle time to remove. Setting the axis
#: explicitly, or raising `CATACLYSM_BOARD_EMPTY_TRIALS` past the smoke
#: threshold, restores all five.
_pick = os.environ.get("CATACLYSM_BOARD_EMPTY_WORLDS", "").strip()
if _pick:
    SELECTED_INDICES = tuple(int(i) for i in _pick.split(","))
elif SMOKE:
    SELECTED_INDICES = (0, 2)
else:
    SELECTED_INDICES = tuple(range(len(WORLDS)))

#: Which world section 5 measures the noise floor in. **NOT WORLD 0.** The
#: weakest player wins 0.0% of campaigns and loses 24.04 cities of 25 whatever
#: happens, so a block-to-block spread taken there is 0.103 points on a win rate
#: and understates the noise everywhere a difference actually appears. This is
#: the first world in `SELECTED_INDICES` that is not world 0, which is the
#: tier-1 player with no empire tree -- the weakest player who still has
#: outcomes that move.
NOISE_WORLD = next((i for i in SELECTED_INDICES if i != 0),
                   SELECTED_INDICES[0])

#: A smoke run sweeps only the two ends. The shipped value is measured anyway,
#: because section 2 needs that cell and the two share it.
SWEPT_GAPS = MIN_GAPS if not SMOKE else (MIN_GAPS[0], SHIPPED_MIN_GAP,
                                         MIN_GAPS[-1])

#: Kinds `Simulation._resolve` never removes from the board. A Quest dungeon
#: relocates and a Fallen City dungeon refreshes, so neither can leave except by
#: being cleared or by its host city falling. Section 1 is about these.
NEVER_LEAVES_ON_ITS_OWN = (DungeonType.QUEST, DungeonType.FALLEN_CITY,
                           DungeonType.CATACLYSM)


def base_config(tier: int = 1, tree=TREE_NONE, min_gap: float | None = None,
                on: bool = True) -> TuningConfig:
    """The settings every row runs under.

    **IDENTICAL TO `analyse_surge_cadence.base_config` at its shipped count and
    interval**, so the two files' figures are comparable; see the module
    docstring for why it is copied rather than imported, and
    `sim/tests/test_analysis_scripts.py` for the guard that keeps them equal.

    @param min_gap  what to set `surge_interval_min` to. None leaves it where
                    `TuningConfig` ships it.
    @param on       whether the board-empty trigger fires at all. False is the
                    control: exactly what the model did before issue #1406.
    """
    cfg = replace(
        TuningConfig(),
        tier=tier,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=float(SHIPPED_INTERVAL),
        surge_dungeon_count=SHIPPED_COUNT,
        surge_count_max=max(SHIPPED_COUNT, TuningConfig().surge_count_max),
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
        surge_on_empty_board=on,
    ).with_tree(tree)
    if min_gap is not None:
        cfg = replace(cfg, surge_interval_min=float(min_gap))
    return cfg


# ---------------------------------------------------------------------------
# The instrumented campaign
# ---------------------------------------------------------------------------

class _Ledger(Simulation):
    """A campaign that also classifies every one of its days, and watches the
    board for stretches nothing can clear off it.

    DRAWS NOTHING, so an instrumented campaign is the campaign a bare
    `Simulation` would have run. `sim/tests/test_analysis_scripts.py` asserts
    that, and asserts the four day kinds sum to the days survived.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        self.walk_days = 0
        self.forge_days = 0
        self.dead_days = 0
        #: Free days on which the map held no dungeon at all.
        self.empty_board_days = 0
        #: Free days with dungeons standing, none inside
        #: `death_risk_tolerance`, and no craft available.
        self.no_safe_days = 0
        #: Free days where something was both standing and survivable and the
        #: policy still did nothing.
        self.declined_days = 0
        #: Dungeons a surge actually put on the map, and how many surges fired.
        self.spawned = 0
        self.surges_fired = 0
        #: Days whose whole board was made of kinds that never leave on their
        #: own, and the longest unbroken run of them. Section 1.
        self.stuck_days = 0
        self.longest_stuck = 0
        self._stuck_run = 0

    def step(self, policy) -> None:
        if self.dying:
            self.dead_days += 1
        elif self.crafting:
            self.forge_days += 1
        elif self.current is not None:
            self.walk_days += 1
        super().step(policy)

        kinds = {d.dtype for d in self.dungeons.values()}
        if kinds and kinds.issubset(NEVER_LEAVES_ON_ITS_OWN):
            self.stuck_days += 1
            self._stuck_run += 1
            self.longest_stuck = max(self.longest_stuck, self._stuck_run)
        else:
            self._stuck_run = 0

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
    """One cell: `trials` campaigns from `seed0` upward under one setting."""
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
    empty, empty_se = _mean_se(share(lambda s, r: s.empty_board_days))
    nosafe, _ = _mean_se(share(lambda s, r: s.no_safe_days))
    declined, _ = _mean_se(share(lambda s, r: s.declined_days))
    walk, _ = _mean_se(share(lambda s, r: s.walk_days))
    forge, _ = _mean_se(share(lambda s, r: s.forge_days))
    cities, cities_se = _mean_se([float(r.cities_lost) for r in results])
    earned, earned_se = _mean_se(
        [100.0 * (r.cataclysm_floors > 0) for r in results])
    won, won_se = _mean_se([100.0 * r.won for r in results])
    surges, surges_se = _mean_se([float(r.surges) for r in results])
    early, _ = _mean_se([float(r.surges_from_empty_board) for r in results])
    return {
        "n": trials,
        "idle%": idle, "idle%se": idle_se,
        "empty%": empty, "empty%se": empty_se,
        "noSafe%": nosafe, "declined%": declined,
        "walk%": walk, "forge%": forge,
        # SURGES PER CAMPAIGN STOPS BEING A FIXED BUDGET UNDER THIS RULE, which
        # is why both halves are reported. `early` is how many of `surges` the
        # empty board pulled forward.
        "surges": surges, "surges_se": surges_se, "early": early,
        # Days between surges, as the campaign actually experienced it. Read
        # against `surge_interval_min` -- this is the number the brake bounds.
        "gap": statistics.fmean(
            [r.survived_days / max(1, r.surges) for r in results]),
        "cities": cities, "cities_se": cities_se,
        "earned%": earned, "earned%se": earned_se,
        "won%": won, "won%se": won_se,
        "cleared": statistics.fmean(
            [float(r.dungeons_cleared) for r in results]),
        "resolved": statistics.fmean(
            [float(r.dungeons_resolved) for r in results]),
        "days": statistics.fmean([float(r.survived_days) for r in results]),
        # Section 1: boards nothing can clear off on its own.
        "stuck%": statistics.fmean(
            [100.0 * s.stuck_days / max(1, r.survived_days)
             for s, r in zip(sims, results, strict=True)]),
        "stuckCampaigns%": 100.0 * statistics.fmean(
            [1.0 if s.longest_stuck else 0.0 for s in sims]),
        "longestStuck": max((s.longest_stuck for s in sims), default=0),
    }


HEADER = (f"{'rule':>5} {'gap':>6} {'idle%':>7} {'+-':>5} {'empty%':>7} "
          f"{'noSafe%':>8} {'inDgn%':>7} {'surges':>7} {'early':>7} "
          f"{'days/sg':>8} {'cities':>7} {'+-':>5} {'earned%':>8} "
          f"{'won%':>6} {'+-':>5} {'days':>6}")


def row(label: str, gap: str, s: dict) -> str:
    return (f"{label:>5} {gap:>6} {s['idle%']:>7.1f} {s['idle%se']:>5.1f} "
            f"{s['empty%']:>7.1f} {s['noSafe%']:>8.1f} {s['walk%']:>7.1f} "
            f"{s['surges']:>7.1f} {s['early']:>7.1f} {s['gap']:>8.1f} "
            f"{s['cities']:>7.2f} {s['cities_se']:>5.2f} "
            f"{s['earned%']:>8.1f} {s['won%']:>6.1f} "
            f"{s['won%se']:>5.2f} {s['days']:>6.0f}")


# ---------------------------------------------------------------------------
# Cells and the fan-out
# ---------------------------------------------------------------------------

def _cell_key(world_index: int, min_gap: float, on: bool, seed0: int) -> str:
    """One batch of campaigns, named by everything that decides what it is."""
    return f"{world_index},{min_gap:g},{1 if on else 0},{seed0}"


def _measure_named_cell(key: str) -> dict:
    """Measure the one cell `key` names. Both sides of the fan-out use this, so
    a worker cannot drift from the in-process path."""
    world_index, min_gap, on, seed0 = key.split(",")
    _, tree, tier = WORLDS[int(world_index)]
    cfg = base_config(tier=tier, tree=tree, min_gap=float(min_gap),
                      on=on == "1")
    return measure(cfg, int(seed0))


def _run_cell_out_of_process(key: str) -> tuple[str, dict]:
    """Re-invoke this file as a worker for one cell and read back its JSON.

    SUBPROCESS RATHER THAN A PROCESS POOL, for the reason
    `analyse_surge_cadence.py` gives: this file is executed by
    `runpy.run_path` in the fast suite, so it has no import name a pool could
    pickle against, and it calls `main()` at module level, so a spawning pool
    would re-run the whole report in every child.
    """
    env = dict(os.environ)
    env["CATACLYSM_BOARD_EMPTY_CELL"] = key
    env["CATACLYSM_BOARD_EMPTY_TRIALS"] = str(TRIALS)
    env["CATACLYSM_BOARD_EMPTY_JOBS"] = "1"
    env["PYTHONPATH"] = os.pathsep.join(
        [os.path.dirname(os.path.abspath(__file__)),
         env.get("PYTHONPATH", "")]).rstrip(os.pathsep)
    done = subprocess.run([sys.executable, os.path.abspath(__file__)],
                          env=env, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"cell {key} failed ({done.returncode}):\n"
                           f"{done.stdout}\n{done.stderr}")
    return key, json.loads(done.stdout.strip().splitlines()[-1])


def control_cells() -> list[str]:
    """Sections 1 to 3: the rule off and on at the shipped gap, every world."""
    return [_cell_key(i, SHIPPED_MIN_GAP, on, seed0)
            for i in SELECTED_INDICES
            for on in (False, True)
            for _, seed0 in BLOCKS]


def gap_cells() -> list[str]:
    """Section 4: the minimum-gap sweep, rule on, every world."""
    return [_cell_key(i, gap, True, seed0)
            for i in SELECTED_INDICES
            for gap in SWEPT_GAPS
            for _, seed0 in BLOCKS]


def noise_cells() -> list[str]:
    """Section 5: `NOISE_BLOCKS` disjoint blocks in one world.

    **NOT WORLD 0, AND THAT IS THE WHOLE POINT.** The weakest player loses every
    losable city and wins nothing in nearly every campaign, so a win rate
    measured there barely varies between blocks and a spread taken from it
    understates the noise in every world where anything actually moves. This
    file did measure it there at first, and got a block-to-block standard
    deviation of 0.103 points on a win rate of 0.0% -- a floor low enough to
    call almost any difference real. `NOISE_WORLD` names a world that varies.
    """
    return [_cell_key(NOISE_WORLD, SHIPPED_MIN_GAP, True, b * TRIALS)
            for b in range(NOISE_BLOCKS)]


def all_cells() -> list[str]:
    """Every batch the report needs, deduplicated, in a stable order.

    ONE PASS FOR THE WHOLE REPORT. Where two sections name the same batch it is
    measured once.
    """
    seen, out = set(), []
    for key in control_cells() + gap_cells() + noise_cells():
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


def pooled(measured: dict[str, dict], world_index: int, min_gap: float,
           on: bool) -> dict:
    """The two blocks of one setting, averaged.

    BLOCK MEANS AND NOT A POOLED SAMPLE. The two blocks are equal in size, so
    the mean of the means is the mean of the whole, and keeping them apart is
    what lets section 5 say what a block-to-block difference looks like.
    """
    cells = [measured[_cell_key(world_index, min_gap, on, seed0)]
             for _, seed0 in BLOCKS]
    keys = [k for k, v in cells[0].items() if isinstance(v, (int, float))]
    out = {k: statistics.fmean([c[k] for c in cells]) for k in keys}

    # A LONGEST UNBROKEN STRETCH IS A MAXIMUM AND NOT A MEAN. Averaging
    # two blocks' worst cases gives a number that is neither block's
    # answer and understates the real worst case, which is the figure
    # section 6 reads to say the clock has to stay.
    out["longestStuck"] = max(c["longestStuck"] for c in cells)
    return out


# ---------------------------------------------------------------------------
# Sections
# ---------------------------------------------------------------------------

def settings_lines() -> list[str]:
    """The three conditions every figure here needs beside it, plus the rest."""
    cfg = base_config()
    return [
        "  policy                        triage",
        f"  surge size                    {cfg.surge_dungeon_count} dungeons, "
        f"cap {cfg.surge_count_max}, mode {cfg.surge_mode.value}",
        f"  active Cataclysms             1 at tier 1, "
        f"{base_config(tier=4).active_cataclysm_count()} at tier 4",
        f"  surge interval                {cfg.surge_interval_days:g} days",
        f"  minimum gap, as shipped       {SHIPPED_MIN_GAP:g} days",
        f"  resolve floor ratio           {cfg.resolve_floor_ratio:g}",
        f"  power escalation              "
        f"{cfg.dungeon_power_escalation_per_100_days:g} per 100 days",
        f"  craft                         {cfg.craft_days} days for "
        f"+{100 * cfg.craft_power_gain_frac:g}% of a tier width",
        f"  campaigns per cell            {2 * TRIALS} "
        f"({TRIALS} in each of 2 disjoint blocks)",
    ]


def section_0_settings() -> None:
    print("=" * 118)
    print("0. THE CONDITIONS. A campaign figure without these is not a "
          "figure.")
    print("=" * 118)
    for line in settings_lines():
        print(line)
    if SMOKE:
        print()
        print(f"  ** SMOKE RUN: {TRIALS} campaign(s) a block. Every share "
              f"below is noise. Set")
        print("     CATACLYSM_BOARD_EMPTY_TRIALS=1000 for the real "
              "measurement. **")


def section_1_what_never_leaves(measured: dict[str, dict]) -> dict:
    """Why the 120-day clock has to stay.

    A pure board-empty trigger that REPLACED the clock would give fewer surges
    than today to any player whose board never empties, which makes the game
    easier for the weakest player -- the opposite of the intent. Whether such a
    board exists is a fact about `Simulation._resolve` and is measured here
    rather than argued.
    """
    print(f"\n{'=' * 118}")
    print("1. BOARDS THAT NOTHING TAKES OFF THE MAP -- why the clock stays as "
          "well as the trigger.")
    print("=" * 118)
    print("   A Quest dungeon relocates instead of detonating and a Fallen "
          "City dungeon refreshes, so")
    print("   neither ever leaves `Simulation.dungeons` on its own. A player "
          "who ignores one keeps the")
    print("   board permanently non-empty. Measured with the RULE OFF, which "
          "is the world the clock")
    print("   has to cover.")
    print()
    print(f"   {'world':>20} {'stuck%':>8} {'campaigns%':>12} "
          f"{'longest':>9}")
    out = {}
    for i in SELECTED_INDICES:
        label = WORLDS[i][0]
        s = pooled(measured, i, SHIPPED_MIN_GAP, on=False)
        out[label] = s
        print(f"   {label:>20} {s['stuck%']:>8.1f} "
              f"{s['stuckCampaigns%']:>12.1f} {s['longestStuck']:>9.0f}")
    print()
    print("   stuck%      -- share of the campaign whose WHOLE board was made "
          "of such dungeons")
    print("   campaigns%  -- share of campaigns holding at least one such day")
    print("   longest     -- longest unbroken run of them, in days, over "
          "every campaign measured")
    return out


def section_2_the_rule(measured: dict[str, dict]) -> dict:
    """The rule off against on, at the shipped minimum gap, in every world."""
    print(f"\n{'=' * 118}")
    print(f"2. THE RULE OFF AGAINST ON, at the shipped minimum gap of "
          f"{SHIPPED_MIN_GAP:g} days.")
    print("=" * 118)
    out = {}
    for i in SELECTED_INDICES:
        label = WORLDS[i][0]
        print(f"\n   {label}")
        print("  " + HEADER)
        off = pooled(measured, i, SHIPPED_MIN_GAP, on=False)
        on = pooled(measured, i, SHIPPED_MIN_GAP, on=True)
        out[label] = {"off": off, "on": on}
        print("  " + row("off", "--", off))
        print("  " + row("on", f"{SHIPPED_MIN_GAP:g}", on))
    print()
    print("   early    -- of `surges`, how many the empty board pulled "
          "forward. SURGES PER CAMPAIGN IS")
    print("               NOT A FIXED BUDGET under this rule, so any figure "
          "on record stated per")
    print("               campaign changes MEANING rather than value.")
    print("   days/sg  -- days survived over surges fired, which is the gap "
          "the campaign experienced.")
    return out


def section_3_where_the_idle_time_went(rule: dict) -> dict:
    """Of the idle time the rule removes, how much was an empty board."""
    print(f"\n{'=' * 118}")
    print("3. WHAT THE RULE CAN AND CANNOT REACH.")
    print("=" * 118)
    print("   Only the empty-board share is a content-supply problem. The "
          "'nothing survivable' share is")
    print("   a POWER problem and a surge cadence cannot fix it -- firing "
          "more waves at it makes it worse.")
    print()
    print(f"   {'world':>20} {'idle% off':>10} {'idle% on':>9} "
          f"{'removed':>8} {'empty% off':>11} {'noSafe% off':>12} "
          f"{'noSafe% on':>11}")
    out = {}
    for label, pair in rule.items():
        off, on = pair["off"], pair["on"]
        removed = off["idle%"] - on["idle%"]
        out[label] = removed
        print(f"   {label:>20} {off['idle%']:>10.1f} {on['idle%']:>9.1f} "
              f"{removed:>8.1f} {off['empty%']:>11.1f} "
              f"{off['noSafe%']:>12.1f} {on['noSafe%']:>11.1f}")
    return out


def section_4_the_minimum_gap(measured: dict[str, dict]) -> dict:
    """The sweep the rule makes necessary.

    `surge_interval_min` used to be consulted only by the escalating surge
    modes, where it floors a decaying gap. It is now the only brake on how fast
    the board-empty trigger can fire, so what it should be is a measurement.
    """
    print(f"\n{'=' * 118}")
    print("4. THE MINIMUM GAP SWEEP -- the only brake left, so its value "
          "matters more than it did.")
    print("=" * 118)
    print(f"   {SHIPPED_MIN_GAP:g} days is what ships and is the control row. "
          f"{MIN_GAPS[-1]:g} is the surge interval itself,")
    print("   at which the brake is as strong as the clock and the rule can "
          "add nothing.")
    out: dict[str, dict[float, dict]] = {}
    for i in SELECTED_INDICES:
        label = WORLDS[i][0]
        print(f"\n   {label}")
        print("  " + HEADER)
        out[label] = {}
        for gap in SWEPT_GAPS:
            s = pooled(measured, i, gap, on=True)
            out[label][gap] = s
            mark = "on*" if gap == SHIPPED_MIN_GAP else "on"
            print("  " + row(mark, f"{gap:g}", s))
    print("\n   * the shipped value.")
    return out


def section_5_noise_floor(measured: dict[str, dict]) -> dict:
    """What a difference has to beat before it is a difference.

    SIX DISJOINT BLOCKS OF THE SAME CELL. Every difference in this report is
    read against this and never against a two-block gap, which is one draw of a
    spread and not the spread. Issue [#1379].

    MEASURED IN `NOISE_WORLD` AND NOT IN WORLD 0. See `noise_cells` for why:
    the weakest player's outcomes barely vary at all, so a floor taken there
    would call almost any difference real.
    """
    label = WORLDS[NOISE_WORLD][0]
    cells = [measured[_cell_key(NOISE_WORLD, SHIPPED_MIN_GAP, True,
                                b * TRIALS)]
             for b in range(NOISE_BLOCKS)]
    wins = [c["won%"] for c in cells]
    cities = [c["cities"] for c in cells]
    earned = [c["earned%"] for c in cells]
    empirical = statistics.stdev(wins) if len(wins) > 1 else float("nan")
    analytic = statistics.fmean([c["won%se"] for c in cells])
    city_sd = (statistics.stdev(cities) if len(cities) > 1 else float("nan"))
    earned_sd = (statistics.stdev(earned) if len(earned) > 1 else float("nan"))
    print(f"\n{'=' * 118}")
    print(f"5. THE NOISE FLOOR, {NOISE_BLOCKS} disjoint blocks of "
          f"{TRIALS} campaigns in '{label}'.")
    print("=" * 118)
    print("   NOT world 0. The weakest player's outcomes barely vary, so a "
          "floor taken there would call")
    print("   almost any difference real. See `noise_cells`.")
    print()
    print(f"   win rate, block to block         sd {empirical:.3f} points, "
          f"against an analytic {analytic:.3f}")
    print(f"   cities lost, block to block      sd {city_sd:.3f} of 25")
    print(f"   earned dungeon, block to block   sd {earned_sd:.3f} points")
    print()
    print("   A DIFFERENCE BETWEEN TWO CELLS HAS TO BEAT ABOUT 2.8 OF THESE, "
          "not one: each side carries")
    print("   its own spread, so the threshold is 2 x sqrt(2) x sd. On the win "
          "rate that is roughly")
    print(f"   {2 * math.sqrt(2) * empirical:.2f} points and on cities lost "
          f"{2 * math.sqrt(2) * city_sd:.2f} of 25.")
    print("   Read every difference above against these. A gap smaller than "
          "about two of them is not")
    print("   a difference; it is the sample size.")
    return {"empirical": empirical, "analytic": analytic}


def main() -> None:
    if WORKER_CELL:
        print(json.dumps(_measure_named_cell(WORKER_CELL)))
        return

    print()
    print("WHAT A SURGE FIRED BY AN EMPTY BOARD COSTS -- issue #1406")
    print()
    print('   The owner ruled on 2026-09-07, verbatim: "Anytime there are no '
          'longer dungeons on')
    print('   the board, a surge happens."')
    print()

    section_0_settings()
    measured = measure_cells(all_cells(), jobs=JOBS)
    stuck = section_1_what_never_leaves(measured)
    rule = section_2_the_rule(measured)
    removed = section_3_where_the_idle_time_went(rule)
    gaps = section_4_the_minimum_gap(measured)
    noise = section_5_noise_floor(measured)

    print(f"\n{'=' * 118}")
    print("6. WHAT THE RUN SAYS.")
    print("=" * 118)

    worst = max(s["longestStuck"] for s in stuck.values())
    print("\n   1. THE CLOCK HAS TO STAY. The longest stretch in which "
          "nothing on the board could leave")
    print(f"      on its own was {worst:.0f} days, against a surge interval of "
          f"{SHIPPED_INTERVAL:g}. A trigger that")
    print("      REPLACED the clock would have given that campaign fewer "
          "surges than it gets today.")

    most = max(removed, key=removed.get) if removed else None
    if most is not None:
        print(f"\n   2. THE IDLE TIME REMOVED runs from "
              f"{min(removed.values()):.1f} to {max(removed.values()):.1f} "
              f"points of a campaign across")
        print(f"      the {len(removed)} worlds measured, most in "
              f"'{most}'.")

    weakest = WORLDS[SELECTED_INDICES[0]][0]
    pair = rule[weakest]
    print(f"\n   3. THE WEAKEST PLAYER, '{weakest}': cities lost "
          f"{pair['off']['cities']:.2f} -> {pair['on']['cities']:.2f} of 25, "
          f"earned")
    print(f"      Cataclysm dungeon {pair['off']['earned%']:.1f}% -> "
          f"{pair['on']['earned%']:.1f}%, surges "
          f"{pair['off']['surges']:.1f} -> {pair['on']['surges']:.1f} of "
          f"which {pair['on']['early']:.1f} pulled forward.")
    print(f"      Read every one of those against the noise floor of "
          f"{noise['empirical']:.2f} points in section 5.")

    print("\n   4. THE RECOMMENDATION IS NOT IN THIS FILE, and no constant "
          "moves on the strength of a")
    print("      sweep alone on this project. Section 4 prints the minimum-gap "
          "sweep and issue #1406")
    print("      carries the single recommendation for the owner to rule on.")
    _ = gaps
    print()


# Called unconditionally, like every other analyse_*.py here, because
# `sim/tests/test_analysis_scripts.py` executes this file with `runpy.run_path`
# and that does not set `__name__` to `"__main__"`.
main()
