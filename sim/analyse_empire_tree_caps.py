"""What the sizes of Bastion Spirit and Golden Age do to a campaign -- the evidence for their caps (#2741 slice 3a).

    CATACLYSM_TREE_CAP_TRIALS=200 CATACLYSM_TREE_CAP_JOBS=12 python -u sim/analyse_empire_tree_caps.py

Two nodes of the empire upgrade tree grow with points spent in ANOTHER branch, and the owner ruled on 2026-11-18,
"Cap each one": "Keep the wording and add a maximum per node, chosen by the building session from the simulation and
recorded as a judgement for you to review." This measures what each node's size does, so the caps are chosen against
something. The coordinating session agreed both caps on 2026-11-18 (S3, S4); `docs/DECISIONS.md` records them.

BASTION SPIRIT: "+1 Day to all dungeon Resolution Timers per 20 points spent in the Explorer tree", 15 points. Read per
point with the Explorer branch full (317 points, 15 whole steps) it is +225 days; read once per step, +15. A player
holding it holds both branches, so the tree here is both as designed: `TREE_ARCHITECT_AS_DESIGNED`'s city fields and
its 10 days, and `TREE_EXPLORER_AS_DESIGNED`'s run and floor fields, with Bastion Spirit's days added to
`resolve_bonus_days`.

GOLDEN AGE: "+1% Total Empire Population per point, for every 20 points invested in the Architect branch", 15 points.
Read per point with the Architect branch full (326 points, 16 steps) it is +240%, x3.4. THE MODEL HAS NO POPULATION
MULTIPLIER, so this scales every city tier's `max_population` through a replaced `TIER_STATS`, under
`TREE_ARCHITECT_AS_DESIGNED`. Population is read in the model only as the living share, which scales a defeat's
empire points; so the most this can show is points per run.

Policy `triage`, the default `TuningConfig` at each tier, and the same seeds 0..TRIALS-1 for every value, so the cells
of a tier differ only by the value.

MEASURED 2026-11-19 AT 200 CAMPAIGNS A CELL, 7,200 campaigns, at development `58b5ddb51`, where an Outpost's ordinary
dungeon is 5 to 10 floors (issue #2749, B9). At 200 campaigns one standard error of a win rate is about 3 points.

    Bastion Spirit days   tier 1 win%      tier 4 win% / cities lost   tier 8 win%
      0                   62.0             25.0 / 6.76                  2.0
      5                   63.5             17.5 / 6.87                  2.0
     10                   59.5             21.0 / 6.82                  3.5
     15                   57.0             20.0 / 7.09                  1.5
     30                   59.0             16.0 / 7.46                  3.0
     60                   53.5             18.5 / 8.62                  2.0
    225                   59.5             21.5 / 9.46                  1.5

  No size wins more campaigns than none; the win rates move by about the noise of 200 campaigns with no trend in the
  size. MORE cities are lost at tier 4 as the days grow: a Siege bites its city every day it stands until its timer
  fires, so a longer timer is more bites. That finding holds for every timer node, Strategic Reserve included. Cap
  chosen: +15 days in total (S3).

    Golden Age factor     win% at tiers 1 / 4 / 8     tier 8 points a run
      x1.0                36.0 / 20.5 / 2.0           171.8
      x1.2                36.0 / 20.5 / 2.5           171.7
      x1.4                36.0 / 21.0 / 3.5           172.9
      x2.0                36.0 / 21.0 / 3.5           172.7
      x3.4                36.0 / 20.0 / 4.5           174.3

  No change in wins at tiers 1 and 4 or in cities lost at any size; at tier 8 the wins are 4 to 9 campaigns of 200;
  points per run up 1.5% at x3.4. The model cannot measure what population is worth, so the cap is a judgement: +40% in
  total (S4).

THE CAPS WERE CHOSEN ON 2026-11-18 from this measurement taken before Outpost dungeons became 5 to 10 floors. Tier 4
cities lost then were 6.92, 7.35, 7.92, 8.54 and 9.36 at 0, 15, 30, 60 and 225 days, and Golden Age's tier 8 points ran
from 164.3 to 169.8, up 3%. The same conclusions held then: no size won more campaigns, and cities lost rose with the
days.

RUN WITH NO SETTINGS IT PLAYS ONE CAMPAIGN A CELL, 36 in all, which `sim/tests/test_analysis_scripts.py` runs to keep
the script working; the figures above need the 200.
"""

from __future__ import annotations

import os
import statistics
import time
from dataclasses import fields, replace

from cataclysm_sim import policies
from cataclysm_sim.config import (TREE_ARCHITECT_AS_DESIGNED, TREE_EXPLORER_AS_DESIGNED, EmpireTree,
                                  TuningConfig)
from cataclysm_sim.engine import Simulation

#: Campaigns a cell. One by default, which is a smoke test and not a measurement.
TRIALS = int(os.environ.get("CATACLYSM_TREE_CAP_TRIALS", "1"))

#: Worker processes. One by default, which runs in this process and is what a test can run.
JOBS = int(os.environ.get("CATACLYSM_TREE_CAP_JOBS", "1"))

TIERS = (1, 4, 8)

#: Bastion Spirit's days: none, steps up to the cap and past it, and the per-point reading at a full Explorer branch.
BASTION_DAYS = (0, 5, 10, 15, 30, 60, 225)

#: Golden Age's factor on population: none, steps up to the cap and past it, and the per-point reading, x3.4.
GOLDEN_FACTORS = (1.0, 1.2, 1.4, 2.0, 3.4)

def both_branches() -> EmpireTree:
    """Both branches as designed: every field either preset sets, taken from the preset that sets it.

    BUILT HERE, AND NOT A MODULE-LEVEL COPY OF ONE PRESET, so a field added to either preset later is carried rather
    than silently left out -- the reason `tools/tests/test_a_copied_preset_still_matches_its_comment.py` exists. A
    field both presets set would need a ruling on which wins, so it stops the script instead.
    """
    blank = EmpireTree()
    merged = {}
    for each in fields(EmpireTree):
        if each.name == "name":
            continue
        architect = getattr(TREE_ARCHITECT_AS_DESIGNED, each.name)
        explorer = getattr(TREE_EXPLORER_AS_DESIGNED, each.name)
        default = getattr(blank, each.name)
        if architect != default and explorer != default:
            raise SystemExit(f"both presets set {each.name}; which one this measurement takes needs a ruling")
        merged[each.name] = architect if architect != default else explorer
    return EmpireTree(name="Architect and Explorer as designed", **merged)

#: Every cell's figures, by (node, value, tier): what a test reads after the script has run.
ROWS: dict[tuple[str, float, int], dict[str, float]] = {}


def config_for(node: str, value: float, tier: int) -> TuningConfig:
    """The campaign settings for one cell."""
    base = replace(TuningConfig(), tier=tier)
    if node == "Bastion Spirit":
        both = both_branches()
        return base.with_tree(replace(both, resolve_bonus_days=both.resolve_bonus_days + value))
    stats = {kind: replace(each, max_population=each.max_population * value)
             for kind, each in base.TIER_STATS.items()}
    return replace(base, TIER_STATS=stats).with_tree(TREE_ARCHITECT_AS_DESIGNED)


def one(job: tuple[str, float, int, int]) -> tuple:
    """One campaign: the cell it belongs to and what it came to."""
    node, value, tier, seed = job
    result = Simulation(config_for(node, value, tier), seed=seed).run(policies.triage)
    return (node, value, tier, result.won, result.lost, result.cities_lost, result.empire_points,
            result.final_population_frac)


def main() -> None:
    jobs = [("Bastion Spirit", float(v), t, s) for v in BASTION_DAYS for t in TIERS for s in range(TRIALS)]
    jobs += [("Golden Age", float(v), t, s) for v in GOLDEN_FACTORS for t in TIERS for s in range(TRIALS)]
    began = time.time()
    if JOBS > 1:
        from multiprocessing import Pool
        with Pool(JOBS) as pool:
            results = pool.map(one, jobs, chunksize=8)
    else:
        results = [one(job) for job in jobs]

    cells: dict[tuple[str, float, int], list[tuple]] = {}
    for result in results:
        cells.setdefault(result[:3], []).append(result[3:])
    for key, got in cells.items():
        ROWS[key] = {
            "win": 100.0 * sum(g[0] for g in got) / len(got),
            "lost": 100.0 * sum(g[1] for g in got) / len(got),
            "cities": statistics.mean(g[2] for g in got),
            "points": statistics.mean(g[3] for g in got),
            "living": statistics.mean(g[4] for g in got),
        }

    print(f"{len(results)} campaigns, {TRIALS} a cell, policy triage, {JOBS} worker(s), "
          f"{time.time() - began:.0f} s")
    for node, values, unit in (("Bastion Spirit", BASTION_DAYS, "days"), ("Golden Age", GOLDEN_FACTORS, "x")):
        print(f"\n{node} ({unit})")
        print("tier  value   win%  lost%  cities  points  living")
        for tier in TIERS:
            for value in values:
                row = ROWS[(node, float(value), tier)]
                print(f"{tier:>4}  {value:>5}  {row['win']:5.1f}  {row['lost']:5.1f}  {row['cities']:6.2f}  "
                      f"{row['points']:6.1f}  {row['living']:6.3f}")


# NOT IN A WORKER. A worker process re-imports this file under the name `__mp_main__`, and running `main` there would
# start the whole measurement again inside every worker. `runpy.run_path`, which the tests use, names it `<run_path>`.
if __name__ != "__mp_main__":
    main()
