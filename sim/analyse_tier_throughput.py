"""Why a player with no empire tree loses everything at difficulty tier 4.

Issue [#1392] recorded that a player who has bought nothing from the empire tree
loses every city they can lose at difficulty tier 4 and reaches the earned
Cataclysm dungeon in 2% of campaigns. Re-measured on `998d758` it is **0.0%**.
Nothing had investigated why. This file is that investigation, kept so the
answer can be re-measured rather than re-derived.

**IT MEASURES AND PROPOSES NOTHING.** Every counterfactual in section 4 changes
one setting to say whether that step is load-bearing. None of them is a
recommendation, and issue [#1349] sets the standing rule that a constant does not
move on the strength of a curve alone.

## What it found

**The player is not too weak. They are outpaced.** At difficulty tier 4 with no
empire tree they are idle 0.0% of the campaign and walking a dungeon 94.1% of it,
and the least dangerous dungeon standing sits at a death chance of 0.118 against
a `death_risk_tolerance` of 0.35. Setting that tolerance to 1.0, so the player
refuses nothing, moves no column at all.

**Section 1 is the whole answer and it needs no campaigns.** The difficulty tier
is the number of active Cataclysms, and `Simulation.trigger_surge` raises the
wave size by `sum(count_mult) ** cataclysm_volume_exponent` over the active
patterns. At tier 4 that is 2.694, so a surge lands 11 dungeons where tier 1
lands 4. A dungeon with no tree walks in 21.3 days. **235 walk-days arrive every
120 days, and a player has one day per day.** Break-even is between tier 1
(0.71 arriving per day) and tier 2 (1.24). Above it the board can only grow.

The four steps after that all follow from the first, and each is checked here:

1. more walking arrives per day than a day contains -- section 1, arithmetic;
2. nothing removes a dungeon but a clear, because
   `dungeon_persists_after_resolve` is True -- section 3;
3. a city absorbs a fixed 10 to 12.5 detonations and **the difficulty tier does
   not change that** -- section 2, arithmetic;
4. every fall fires another surge, which is 89% of all surges at tier 4 and the
   only trigger with no spacing floor -- sections 3 and 4, and issue [#1432];
5. the run ends when the player is committed to the Last Stand at a death chance
   of 0.999, because `triage`'s endgame branch ignores `death_risk_tolerance`
   -- section 3.

**The 0% is not a truncated run.** Section 5 gives the player a policy identical
to `triage` except that it never enters the Last Stand. The campaign runs the
full 2,500 days instead of 779 and the earned Cataclysm still opens in 0.0% of
them, because tier 4 needs two Cataclysms finished rather than one and the player
completes 3.31 quest objectives against a requirement of at least ten.

## Conditions

Every campaign figure runs under `triage`, STATIC surges at the shipped
`surge_dungeon_count` and `surge_interval_days`, resolve floor ratio 2.0, dungeon
power escalation 0.10 per 100 days, and craft 12 days for +4% of a tier width --
identical to `analyse_surge_cadence.base_config` at its shipped count and
interval, so the two files' figures are comparable.

## Running it

`TRIALS` defaults to 1 campaign a cell so the fast test suite can run the whole
file. **One campaign a cell is a smoke run and its campaign figures mean
nothing**; sections 1 and 2 are exact arithmetic and are right at any setting.

```
CATACLYSM_TIER_THROUGHPUT_TRIALS=400 python -u sim/analyse_tier_throughput.py
```

400 campaigns a cell is about four minutes and is what the figures quoted in this
docstring and on [#1392] were measured at.

[#1349]: https://github.com/sdubois777/Cataclysm/issues/1349
[#1392]: https://github.com/sdubois777/Cataclysm/issues/1392
[#1432]: https://github.com/sdubois777/Cataclysm/issues/1432
"""
from __future__ import annotations

import math
import os
import statistics
from dataclasses import replace
from types import SimpleNamespace

from cataclysm_sim import policies
from cataclysm_sim.config import (SurgeMode, TREE_NONE, TuningConfig, CityTier,
                                  DungeonType)
from cataclysm_sim.engine import Simulation, active_cataclysms_for
from cataclysm_sim.patterns import DEFAULT as PATTERN_DEFAULT, PATTERNS

#: Campaigns per cell. One is a smoke run; see the module docstring.
TRIALS = int(os.environ.get("CATACLYSM_TIER_THROUGHPUT_TRIALS", "1"))

#: How many seeds section 1 averages the wave multiplier over. Which Cataclysm
#: types are active is drawn from the seed, and they have different
#: `count_mult`, so a single seed would report one roster rather than the mean.
#: Exact arithmetic given the roster, so this is cheap and independent of TRIALS.
ROSTER_SEEDS = 200

#: The three city tiers a surge can actually target. The Pillar is excluded by
#: `SURGE_TARGET_WEIGHT[PILLAR] = 0.0` and by `trigger_surge`'s own filter, so
#: including it would average in a dungeon that is never spawned by a wave.
LOSABLE = (CityTier.OUTPOST, CityTier.BULWARK, CityTier.SANCTUARY)


def base_config(tier: int = 1, tree=TREE_NONE, **over) -> TuningConfig:
    """The settings every campaign here runs under.

    **IDENTICAL TO `analyse_surge_cadence.base_config` at its shipped count and
    interval**, so the two files' figures are comparable.
    `sim/tests/test_analysis_scripts.py` holds the guard that keeps them equal.

    @param over  one setting to change, for section 4. Applied last, so it wins.
    """
    shipped = TuningConfig()
    cfg = replace(
        shipped,
        tier=tier,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=float(shipped.surge_interval_days),
        surge_dungeon_count=shipped.surge_dungeon_count,
        surge_count_max=max(shipped.surge_dungeon_count, shipped.surge_count_max),
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
    ).with_tree(tree)
    return replace(cfg, **over) if over else cfg


# ---------------------------------------------------------------------------
# Section 1 -- the arrival rate. Exact arithmetic, no campaigns.
# ---------------------------------------------------------------------------

def wave_multiplier(cfg: TuningConfig) -> float:
    """What `trigger_surge` multiplies the wave size by, at this tier.

    ASKED OF THE SAME PATTERN TABLE THE ENGINE READS rather than written out, so
    a change to a `count_mult` still moves this number. `trigger_surge` computes
    `sum(cmults) ** cataclysm_volume_exponent` over the active types
    (`engine.py:561-564`); which types are active depends on the seed, so this
    averages over `ROSTER_SEEDS` of them.
    """
    vols = []
    for seed in range(ROSTER_SEEDS):
        cmults = [PATTERNS.get(t, PATTERN_DEFAULT).count_mult
                  for t in active_cataclysms_for(cfg, seed)]
        vols.append(sum(cmults) ** cfg.cataclysm_volume_exponent)
    return statistics.fmean(vols)


def dungeons_per_surge(cfg: TuningConfig) -> int:
    """How many dungeons one wave lands, the way `trigger_surge` counts them.

    CALLED UNBOUND ON A STAND-IN, the way `analyse_surge_cadence.surge_count_for`
    does and for the same reason: `Simulation.surge_count` reads `self.cfg` and
    `self.surge_index` and nothing else, and building a real `Simulation` builds
    a whole empire graph for one integer.
    """
    base = Simulation.surge_count(SimpleNamespace(cfg=cfg, surge_index=0))
    return max(1, round(base * wave_multiplier(cfg)))


def walk_days_per_dungeon(cfg: TuningConfig) -> float:
    """Days to walk a mid-depth Basic dungeon, meaned over the losable tiers.

    THROUGH `run_days_for` AND `floors_added` rather than off the raw fields.
    Issue #1397 made the tree's floor bonus a per-active-Cataclysm-type figure,
    so reading `floor_delta` gives the tier-independent half and no warning;
    `tools/tests/test_the_tree_is_read_per_tier.py` enforces going through the
    method. `run_days_for` is asked rather than reimplemented for the same
    reason -- it is where the tree's reduction and the floor and cap are applied.
    """
    sim = Simulation(cfg, seed=0)
    added = cfg.tree.floors_added(cfg.active_cataclysm_count())
    days = []
    for ctier in LOSABLE:
        lo, hi = cfg.spec(DungeonType.BASIC, ctier).floors
        days.append(sim.run_days_for(int(round((lo + hi) / 2.0 + added))))
    return statistics.fmean(days)


def arrival_rate(cfg: TuningConfig) -> float:
    """Walk-days of dungeon arriving per calendar day, from scheduled waves.

    **A LOWER BOUND AND NOT AN ESTIMATE.** It counts the scheduled wave only.
    Surges fired by a city falling and by the board emptying land on top of it,
    and at tier 4 the first of those is 89% of every surge that fires.

    Above 1.0 the player cannot keep the board clear however they play, because
    they walk at most one day per day.
    """
    return (dungeons_per_surge(cfg) * walk_days_per_dungeon(cfg)
            / cfg.surge_interval_days)


def section_arrival_rate() -> dict:
    print("=" * 78)
    print("1. WALK-DAYS ARRIVING PER CALENDAR DAY, with no empire tree")
    print("=" * 78)
    print("   Exact arithmetic; no campaigns are run. A player walks at most one")
    print("   day per day, so above 1.00 the board can only grow. Scheduled")
    print("   waves only -- a surge fired by a city falling lands on top.")
    print()
    print(f"   {'tier':>5} {'active':>7} {'dungeons/surge':>15} "
          f"{'walk days each':>15} {'gap':>5} {'arriving per day':>17}")
    out = {}
    for tier in (1, 2, 3, 4, 5, 6, 8):
        cfg = base_config(tier=tier)
        per, walk = dungeons_per_surge(cfg), walk_days_per_dungeon(cfg)
        rate = arrival_rate(cfg)
        out[tier] = rate
        print(f"   {tier:>5} {cfg.active_cataclysm_count():>7} {per:>15} "
              f"{walk:>15.1f} {cfg.surge_interval_days:>5.0f} {rate:>17.2f}")
    print()
    print(f"   THE BOARD CANNOT BE KEPT CLEAR FROM TIER "
          f"{min(t for t, r in out.items() if r > 1.0)} UPWARD.")
    print(f"   Tier 1 is {out[1]:.2f} and tier 4 is {out[4]:.2f}, which is "
          f"{out[4] / out[1]:.2f} times as much work per day.")
    return out


# ---------------------------------------------------------------------------
# Section 2 -- what a city can absorb. Exact arithmetic, no campaigns.
# ---------------------------------------------------------------------------

def detonations_survived(cfg: TuningConfig, ctier: CityTier) -> float:
    """How many mid-depth detonations this city tier absorbs before it falls.

    `_resolve` subtracts `defense_damage * scale * taken`, where `scale` is the
    dungeon's depth over the typical depth for its type and tier, so a mid-depth
    dungeon has scale 1.0 (`engine.py:790-808`).
    """
    taken = cfg.tree.damage_taken(cfg.active_cataclysm_count())
    bite = cfg.spec(DungeonType.BASIC, ctier).defense_damage * taken
    pool = cfg.TIER_STATS[ctier].max_defense * cfg.tree.city_health_mult
    return pool / bite if bite > 0 else float("inf")


def section_city_absorption() -> dict:
    print()
    print("=" * 78)
    print("2. HOW MANY DETONATIONS A CITY ABSORBS, with no empire tree")
    print("=" * 78)
    print("   Exact arithmetic; no campaigns are run. There is no regeneration,")
    print("   repair or garrison in the model: `city.defense` only ever rises in")
    print("   `_retake`, which needs the player to clear the Fallen City.")
    print()
    print(f"   {'city':<12}" + "".join(f"{'tier ' + str(t):>10}"
                                       for t in (1, 2, 4, 8)))
    out = {}
    for ctier in LOSABLE:
        row = [detonations_survived(base_config(tier=t), ctier)
               for t in (1, 2, 4, 8)]
        out[ctier] = row
        print(f"   {ctier.value:<12}" + "".join(f"{v:>10.1f}" for v in row))
    same = all(len(set(round(v, 6) for v in row)) == 1 for row in out.values())
    print()
    print("   THE DIFFICULTY TIER DOES NOT CHANGE A SINGLE ONE OF THESE."
          if same else
          "   The difficulty tier changes these, which it did not use to.")
    print("   It is what arrives at the city that the tier multiplies, not what")
    print("   the city can take. See section 1.")
    return out


# ---------------------------------------------------------------------------
# The instrumented campaign
# ---------------------------------------------------------------------------

class _Ledger(Simulation):
    """A campaign that records why each day passed, and how the run ended.

    DRAWS NOTHING, so an instrumented campaign is the campaign a bare
    `Simulation` would have run: every hook below either increments a counter or
    calls `death_chance`, which is pure. `sim/tests/test_analysis_scripts.py`
    asserts that against an uninstrumented run.

    WHY IT COUNTS SURGES BY CAUSE. The three call sites into `trigger_surge` are
    the scheduled one, `_fall`, and `_maybe_surge_on_empty_board`, and only the
    second passes `from_city_fall`. The third is told apart by watching
    `surges_from_empty_board`, which the engine maintains itself.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        self.walk_days = self.forge_days = self.dead_days = 0
        self.empty_board_days = self.no_safe_days = self.declined_days = 0
        self.spawned = 0
        self.surges_scheduled = self.surges_from_fall = self.surges_empty = 0
        self.last_stand_day: int | None = None
        self.last_stand_risk: float | None = None
        self.entered_last_stand = False
        self.ended_in: str | None = None

    def step(self, policy) -> None:
        if self.dying:
            self.dead_days += 1
        elif self.crafting:
            self.forge_days += 1
        elif self.current is not None:
            self.walk_days += 1
        before = self.current
        super().step(policy)
        if (before is None and self.current is not None
                and self.last_stand is not None
                and self.current.did == self.last_stand.did):
            self.entered_last_stand = True

    def trigger_surge(self, from_city_fall: bool = False) -> None:
        before, empty_before = self._next_did, self.surges_from_empty_board
        super().trigger_surge(from_city_fall=from_city_fall)
        if self._next_did <= before:
            return
        self.spawned += self._next_did - before
        if from_city_fall:
            self.surges_from_fall += 1
        elif self.surges_from_empty_board > empty_before:
            self.surges_empty += 1
        else:
            self.surges_scheduled += 1

    def _open_last_stand(self) -> None:
        had = self.last_stand is not None
        super()._open_last_stand()
        if not had and self.last_stand is not None:
            self.last_stand_day = self.day
            self.last_stand_risk = self.death_chance(self.last_stand)

    def _finish_current(self) -> None:
        d, was_lost = self.current, self.lost
        super()._finish_current()
        if self.lost and not was_lost and d is not None:
            self.ended_in = ("died in the Last Stand"
                             if (self.last_stand is not None
                                 and d.did == self.last_stand.did)
                             else "died in the earned Cataclysm")


def ledger_policy(policy, refuse_last_stand: bool = False):
    """Wrap a policy so every free day is classified before it answers.

    `Simulation.death_chance` is pure, so this draws nothing.

    @param refuse_last_stand  hide the Last Stand from the policy. Section 5
                              needs it: `triage`'s endgame branch takes any
                              Cataclysm dungeon and ignores
                              `death_risk_tolerance`, so a player who would
                              rather not die at 0.999 has no other way to say so.
    """
    def counted(sim, dungeons):
        if refuse_last_stand and sim.last_stand is not None:
            dungeons = [d for d in dungeons if d.did != sim.last_stand.did]
        risks = [sim.death_chance(d) for d in dungeons]
        choice = policy(sim, dungeons)
        if choice is None:
            if not dungeons:
                sim.empty_board_days += 1
            elif not any(r <= sim.cfg.death_risk_tolerance for r in risks):
                sim.no_safe_days += 1
            else:
                sim.declined_days += 1
        return choice
    return counted


def _mean_se(values: list[float]) -> tuple[float, float]:
    """Mean, and the standard error of that mean.

    THE SECOND NUMBER IS THE POINT: a difference smaller than a couple of these
    is not a difference. Issue #1379.
    """
    if not values:
        return 0.0, 0.0
    mean = statistics.fmean(values)
    if len(values) < 2:
        return mean, float("nan")
    return mean, statistics.stdev(values) / math.sqrt(len(values))


def measure(cfg: TuningConfig, seed0: int = 0, trials: int = 0,
            refuse_last_stand: bool = False) -> dict:
    """One cell: `trials` campaigns from `seed0` upward."""
    trials = trials or TRIALS
    pol = ledger_policy(policies.ALL["triage"], refuse_last_stand)
    sims, res = [], []
    for i in range(trials):
        s = _Ledger(cfg, seed=seed0 + i)
        res.append(s.run(pol))
        sims.append(s)

    def share(fn) -> list[float]:
        return [100.0 * fn(s, r) / max(1, r.survived_days)
                for s, r in zip(sims, res, strict=True)]

    cities, cities_se = _mean_se([float(r.cities_lost) for r in res])
    earned, earned_se = _mean_se([100.0 * (r.cataclysm_floors > 0) for r in res])
    ends: dict[str, int] = {}
    for s, r in zip(sims, res, strict=True):
        key = ("won" if r.won else s.ended_in if r.lost else "ran out of days")
        ends[key] = ends.get(key, 0) + 1
    ls = [s for s in sims if s.last_stand_day is not None]
    return {
        "n": trials,
        "days": statistics.fmean([r.survived_days for r in res]),
        "cities": cities, "cities_se": cities_se,
        "earned%": earned, "earned%se": earned_se,
        "won%": 100.0 * sum(1 for r in res if r.won) / trials,
        "cleared": statistics.fmean([float(r.dungeons_cleared) for r in res]),
        "resolved": statistics.fmean([float(r.dungeons_resolved) for r in res]),
        "spawned": statistics.fmean([float(s.spawned) for s in sims]),
        "objectives": statistics.fmean([float(r.objectives) for r in res]),
        "idle%": _mean_se(share(lambda s, r: r.idle_days))[0],
        "noSafe%": _mean_se(share(lambda s, r: s.no_safe_days))[0],
        "empty%": _mean_se(share(lambda s, r: s.empty_board_days))[0],
        "walk%": _mean_se(share(lambda s, r: s.walk_days))[0],
        "sgSched": statistics.fmean([float(s.surges_scheduled) for s in sims]),
        "sgFall": statistics.fmean([float(s.surges_from_fall) for s in sims]),
        "sgEmpty": statistics.fmean([float(s.surges_empty) for s in sims]),
        "lastStand%": 100.0 * len(ls) / trials,
        "lastStandDay": statistics.fmean([s.last_stand_day for s in ls]) if ls else 0.0,
        "lastStandRisk": statistics.fmean([s.last_stand_risk for s in ls]) if ls else 0.0,
        "ends": ends,
    }


# ---------------------------------------------------------------------------
# Section 3 -- what the campaigns do
# ---------------------------------------------------------------------------

_HDR = (f"   {'world':<30} {'days':>6} {'cities':>7} {'+-':>5} {'earned%':>8} "
        f"{'won%':>6} {'cleared':>8} {'spawned':>8} {'idle%':>6} "
        f"{'noSafe%':>8} {'walk%':>6}")


def _row(label: str, s: dict) -> str:
    return (f"   {label:<30} {s['days']:>6.0f} {s['cities']:>7.2f} "
            f"{s['cities_se']:>5.2f} {s['earned%']:>8.1f} {s['won%']:>6.1f} "
            f"{s['cleared']:>8.1f} {s['spawned']:>8.1f} {s['idle%']:>6.1f} "
            f"{s['noSafe%']:>8.1f} {s['walk%']:>6.1f}")


def section_campaigns() -> dict:
    print()
    print("=" * 78)
    print(f"3. WHAT THE CAMPAIGNS DO, no empire tree, {TRIALS} per cell")
    print("=" * 78)
    cells = {}
    print(_HDR)
    for tier in (1, 4):
        cells[tier] = measure(base_config(tier=tier))
        print(_row(f"no tree, tier {tier}", cells[tier]))

    print()
    print(f"   {'world':<20} {'scheduled':>10} {'from a fall':>12} "
          f"{'board empty':>12} {'Last Stand opens':>17} {'on day':>7} "
          f"{'death chance':>13}")
    for tier, s in cells.items():
        print(f"   {'no tree, tier ' + str(tier):<20} {s['sgSched']:>10.2f} "
              f"{s['sgFall']:>12.2f} {s['sgEmpty']:>12.3f} "
              f"{s['lastStand%']:>16.1f}% {s['lastStandDay']:>7.0f} "
              f"{s['lastStandRisk']:>13.3f}")

    print()
    for tier, s in cells.items():
        total = max(1.0, s["sgSched"] + s["sgFall"] + s["sgEmpty"])
        print(f"   tier {tier}: {100.0 * s['sgFall'] / total:.0f}% of surges "
              f"come from a city falling. How each campaign ended: "
              + ", ".join(f"{k} {100.0 * v / s['n']:.1f}%"
                          for k, v in sorted(s["ends"].items())))
    return cells


# ---------------------------------------------------------------------------
# Section 4 -- which single step is load-bearing
# ---------------------------------------------------------------------------

#: `(label, one setting to change)`. **NONE OF THESE IS A PROPOSAL.** Each says
#: what that one step is worth, holding every other setting where it ships.
LEVERS = (
    ("shipped, the control", {}),
    ("no surge when a city falls", dict(surge_on_city_fall=False)),
    ("no surge when board empties", dict(surge_on_empty_board=False)),
    ("detonated dungeon leaves", dict(dungeon_persists_after_resolve=False)),
    ("a surge lands 2, not 4", dict(surge_dungeon_count=2)),
    ("no tier wave multiplier", dict(cataclysm_volume_exponent=0.0)),
    ("the player refuses nothing", dict(death_risk_tolerance=1.0)),
    ("surge gap 240, not 120", dict(surge_interval_days=240.0)),
)


def section_levers() -> dict:
    print()
    print("=" * 78)
    print(f"4. WHICH SINGLE STEP IS LOAD-BEARING, tier 4, no tree, "
          f"{TRIALS} per cell")
    print("=" * 78)
    print("   Each row changes ONE setting and holds the rest. NONE IS A")
    print("   PROPOSAL -- see the module docstring and issue #1349.")
    print()
    print(_HDR)
    out = {}
    for label, over in LEVERS:
        out[label] = measure(base_config(tier=4, **over))
        print(_row(label, out[label]))
    control, empty = out["shipped, the control"], out["no surge when board empties"]
    identical = all(
        control[k] == empty[k]
        for k in ("days", "cities", "earned%", "spawned", "sgSched", "sgFall"))
    print()
    print(f"   THE BOARD-EMPTY SURGE FIRES {control['sgEmpty']:.3f} TIMES A "
          f"CAMPAIGN HERE. Turning it off")
    print("   reproduces the control in every column, digit for digit."
          if identical else
          "   does NOT reproduce the control, so it now moves this world.")
    return out


# ---------------------------------------------------------------------------
# Section 5 -- is the 0% a truncated run?
# ---------------------------------------------------------------------------

def section_truncation() -> dict:
    print()
    print("=" * 78)
    print(f"5. IS THE 0% A TRUNCATED RUN? tier 4, no tree, {TRIALS} per cell")
    print("=" * 78)
    print("   `triage`'s endgame branch takes any Cataclysm dungeon and ignores")
    print("   `death_risk_tolerance`, so the player is committed to the Last")
    print("   Stand. The second row hides it from them instead.")
    print()
    cfg = base_config(tier=4)
    out = {"committed": measure(cfg),
           "refusing": measure(cfg, refuse_last_stand=True)}
    print(f"   {'policy':<34} {'days':>6} {'cities':>7} {'earned%':>8} "
          f"{'won%':>6} {'objectives':>11} {'cleared':>8}")
    for label, s in (("triage as it ships", out["committed"]),
                     ("triage, refusing the Last Stand", out["refusing"])):
        print(f"   {label:<34} {s['days']:>6.0f} {s['cities']:>7.2f} "
              f"{s['earned%']:>8.1f} {s['won%']:>6.1f} "
              f"{s['objectives']:>11.2f} {s['cleared']:>8.1f}")
    need = cfg.cataclysms_required()
    cheapest = sorted(cfg.quest_objectives_for(t)
                      for t in cfg.CATACLYSM_ROSTER)[:need]
    print()
    print(f"   THE WIN CONDITION SCALES AGAINST THEM TOO. Tier 4 needs {need} "
          f"of {cfg.active_cataclysm_count()} Cataclysms")
    print(f"   finished where tier 1 needs "
          f"{base_config(tier=1).cataclysms_required()} of 1, and the "
          f"{need} cheapest cost {sum(cheapest)} quest dungeons between them.")
    print(f"   The player completes {out['refusing']['objectives']:.2f} "
          f"objectives given the full {cfg.max_days} days.")
    return out


def main() -> None:
    print(f"conditions: triage policy, STATIC surges, "
          f"surge_dungeon_count={TuningConfig().surge_dungeon_count}, "
          f"surge_interval_days={TuningConfig().surge_interval_days:g},")
    print("            resolve floor ratio 2.0, escalation 0.10 per 100 days, "
          "craft 12 days for +4%, empire tree TREE_NONE,")
    print(f"            {TRIALS} campaigns per cell"
          + ("  -- A SMOKE RUN. Campaign figures mean nothing at one campaign;"
             " see the module docstring." if TRIALS < 100 else ""))
    section_arrival_rate()
    section_city_absorption()
    section_campaigns()
    section_levers()
    section_truncation()


# Called unconditionally, like every other analyse_*.py here, because
# `sim/tests/test_analysis_scripts.py` executes this file with `runpy.run_path`
# and that does not set `__name__` to `"__main__"`.
main()
