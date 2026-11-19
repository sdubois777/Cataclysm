"""What the size of Architectural Insight does to a campaign -- the evidence for its cap (#2741 slice 4).

    CATACLYSM_INSIGHT_CAP_TRIALS=200 CATACLYSM_INSIGHT_CAP_JOBS=12 python -u sim/analyse_architectural_insight_cap.py

Architectural Insight is "+1 floor for every 10 points invested in the Architect branch per point", 10 points. Read per
point and per whole step with the Architect branch full (326 points, 32 steps) it is +320 floors on every dungeon, so
the owner's ruling 2 of 2026-11-18, "Cap each one", reaches it as it reached Golden Age and Bastion Spirit. The
coordinating session's W4 of 2026-11-19: measure +0, +10, +20, +40, +80 and +320, and cap it at +10 floors in total
unless the measurement shows a reason not to.

A PLAYER HOLDING IT HOLDS BOTH BRANCHES -- it is an Explorer node that counts Architect points -- so the tree here is
both as designed, as `analyse_empire_tree_caps.py` builds it: `TREE_ARCHITECT_AS_DESIGNED`'s city fields and
`TREE_EXPLORER_AS_DESIGNED`'s run and floor fields, with the node's floors added to `floor_delta`, which the
simulation adds to every dungeon it makes, the boss's included (`Simulation._make_dungeon`).

Policy `triage`, the default `TuningConfig` at each tier, and the same seeds 0..TRIALS-1 for every value, so the cells
of a tier differ only by the value.

RUN WITH NO SETTINGS IT PLAYS ONE CAMPAIGN A CELL, 18 in all, which `sim/tests/test_analysis_scripts.py` runs to keep
the script working; a measurement needs the 200.
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
TRIALS = int(os.environ.get("CATACLYSM_INSIGHT_CAP_TRIALS", "1"))

#: Worker processes. One by default, which runs in this process and is what a test can run.
JOBS = int(os.environ.get("CATACLYSM_INSIGHT_CAP_JOBS", "1"))

TIERS = (1, 4, 8)

#: The node's floors on every dungeon: none, the recommended cap, sizes past it, and the uncapped reading.
INSIGHT_FLOORS = (0, 10, 20, 40, 80, 320)


def both_branches() -> EmpireTree:
    """Both branches as designed: every field either preset sets, taken from the preset that sets it.

    BUILT HERE, AND NOT A MODULE-LEVEL COPY OF ONE PRESET, so a field added to either preset later is carried rather
    than silently left out. A field both presets set would need a ruling on which wins, so it stops the script instead.
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


#: Every cell's figures, by (floors, tier): what a test reads after the script has run.
ROWS: dict[tuple[float, int], dict[str, float]] = {}


def config_for(floors: float, tier: int) -> TuningConfig:
    """The campaign settings for one cell."""
    both = both_branches()
    # THE TIER-INDEPENDENT FLOORS, read through `floors_added` with no active type, as
    # `tools/tests/test_the_tree_is_read_per_tier.py` asks: the node adds to that half, and the per-type half
    # (Infinite Depths) stays where the preset put it.
    return replace(TuningConfig(), tier=tier).with_tree(replace(both, floor_delta=both.floors_added(0) + floors))


def one(job: tuple[float, int, int]) -> tuple:
    """One campaign: the cell it belongs to and what it came to."""
    floors, tier, seed = job
    result = Simulation(config_for(floors, tier), seed=seed).run(policies.triage)
    return (floors, tier, result.won, result.lost, result.cities_lost, result.empire_points,
            result.final_population_frac)


def main() -> None:
    jobs = [(float(v), t, s) for v in INSIGHT_FLOORS for t in TIERS for s in range(TRIALS)]
    began = time.time()
    if JOBS > 1:
        from multiprocessing import Pool
        with Pool(JOBS) as pool:
            results = pool.map(one, jobs, chunksize=8)
    else:
        results = [one(job) for job in jobs]

    cells: dict[tuple[float, int], list[tuple]] = {}
    for result in results:
        cells.setdefault(result[:2], []).append(result[2:])
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
    print("\nArchitectural Insight (floors on every dungeon, both branches as designed)")
    print("tier  floors   win%  lost%  cities  points  living")
    for tier in TIERS:
        for floors in INSIGHT_FLOORS:
            row = ROWS[(float(floors), tier)]
            print(f"{tier:>4}  {floors:>6}  {row['win']:5.1f}  {row['lost']:5.1f}  {row['cities']:6.2f}  "
                  f"{row['points']:6.1f}  {row['living']:6.3f}")
    print("\nThis measures and proposes nothing; issue #2741 slice 4 records what the cap was chosen from.")


# NOT IN A WORKER. A worker process re-imports this file under the name `__mp_main__`, and running `main` there would
# start the whole measurement again inside every worker. `runpy.run_path`, which the tests use, names it `<run_path>`.
if __name__ != "__mp_main__":
    main()
