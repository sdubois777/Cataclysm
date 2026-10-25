"""What running a walk's days when the player leaves moves, measured on the same seeds.

THE OWNER'S DECISION OF 2026-10-24: nothing resolves while the player is inside
a dungeon; when the walk ends the clear is credited (or the death resolves) and
then the walk's days run in order. `TuningConfig.days_resolve_on_exit` carries
it. This script runs each cell twice on the same seeds, the switch off and on,
and prints the two side by side with the paired change in cities lost.

THE CELLS are the ones `sim/README.md` and `docs/DECISIONS.md` quote:

- the guard cell of the 2026-10-16 retune: Explorer as designed, `triage`,
  tier 1, 500 campaigns, the default day cap;
- the README's table of a player on `triage`: Explorer as designed and no
  tree, tiers 1 to 4, 300 campaigns, a day cap of 2,500;
- the README's undefended table: nobody enters any dungeon, tiers 1 to 4, 500
  campaigns, 2,000 days, "lost" the first day the Pillar is exposed. Nobody
  walks, so nothing is owed, and the two halves must be identical; that is the
  control on the script itself.

Every setting is raw `TuningConfig()` but for the tier, the tree, the day cap
and the switch. Stand-alone: `python measure_days_on_exit.py` from `sim/`.
`CATACLYSM_MEASURE_JOBS` sets the worker count (default: every core).
"""

from __future__ import annotations

import concurrent.futures
import math
import os
import statistics
import sys
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED, TREE_NONE, TuningConfig
from cataclysm_sim.engine import Simulation

GUARD_CAMPAIGNS = 500
TRIAGE_CAMPAIGNS = 300
TRIAGE_DAY_CAP = 2500
UNDEFENDED_CAMPAIGNS = 500
UNDEFENDED_DAYS = 2000

TREES = {"Explorer": TREE_EXPLORER_AS_DESIGNED, "none": TREE_NONE}


def config(tier: int, tree: str, on: bool, max_days: int | None = None) -> TuningConfig:
    cfg = replace(TuningConfig(), tier=tier, days_resolve_on_exit=on).with_tree(TREES[tree])
    return replace(cfg, max_days=max_days) if max_days is not None else cfg


def defended(args) -> dict:
    tier, tree, on, max_days, seed = args
    result = Simulation(config(tier, tree, on, max_days), seed=seed).run(policies.triage)
    return {
        "won": result.won, "lost": result.lost, "cities": result.cities_lost,
        "cleared": result.dungeons_cleared, "deaths": result.deaths,
        "surges": result.surges, "empty": result.surges_from_empty_board,
        "days": result.survived_days, "last_stand": result.last_stand,
    }


def undefended(args) -> dict:
    tier, on, seed = args
    sim = Simulation(config(tier, "none", on), seed=seed)
    lost_day = None
    while sim.day < UNDEFENDED_DAYS:
        sim.step(lambda _sim, _dungeons: None)
        if sim.empire.pillar_exposed():
            lost_day = sim.day
            break
    return {"lost": lost_day is not None, "day": lost_day if lost_day is not None else UNDEFENDED_DAYS,
            "cities": len(sim.empire.fallen_cities())}


def mean(values) -> float:
    values = list(values)
    return sum(values) / len(values)


def paired(off: list[dict], on: list[dict], key: str) -> str:
    diffs = [b[key] - a[key] for a, b in zip(off, on, strict=True)]
    sd = statistics.stdev(diffs) if len(diffs) > 1 else 0.0
    return f"{mean(diffs):+.2f} +/- {1.96 * sd / math.sqrt(len(diffs)):.2f}"


def wilson(hits: int, n: int) -> str:
    z = 1.96
    p = hits / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return f"{100 * p:.1f}% ({100 * (centre - half):.1f} to {100 * (centre + half):.1f})"


def run_cells(pool, cells, worker, seeds):
    jobs = {cell: [cell + (seed,) for seed in range(seeds)] for cell in cells}
    return {cell: list(pool.map(worker, jobs[cell], chunksize=25)) for cell in cells}


def main() -> int:
    workers = int(os.environ.get("CATACLYSM_MEASURE_JOBS", "0")) or None
    with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as pool:
        guard = run_cells(pool, [(1, "Explorer", on, None) for on in (False, True)], defended, GUARD_CAMPAIGNS)
        triage = run_cells(pool, [(tier, tree, on, TRIAGE_DAY_CAP)
                                  for tree in TREES for tier in (1, 2, 3, 4) for on in (False, True)],
                           defended, TRIAGE_CAMPAIGNS)
        alone = run_cells(pool, [(tier, on) for tier in (1, 2, 3, 4) for on in (False, True)],
                          undefended, UNDEFENDED_CAMPAIGNS)

    print("## The guard cell: Explorer, triage, tier 1, 500 campaigns, the default day cap\n")
    print("| switch | won (95% interval) | cities lost | change in cities lost |")
    print("|---|---|---:|---|")
    off, on = guard[(1, "Explorer", False, None)], guard[(1, "Explorer", True, None)]
    for name, rows in (("off", off), ("on", on)):
        change = paired(off, on, "cities") if rows is on else ""
        print(f"| {name} | {wilson(sum(r['won'] for r in rows), len(rows))} | {mean(r['cities'] for r in rows):.2f} "
              f"| {change} |")

    print("\n## A player on triage: 300 campaigns a cell, a day cap of 2,500\n")
    print("| tree | tier | switch | won (95% interval) | lost | cities lost | change in cities lost | cleared "
          "| deaths | surges | from an empty board | days | Last Stand reached |")
    print("|---|---:|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|")
    for tree in TREES:
        for tier in (1, 2, 3, 4):
            off, on = triage[(tier, tree, False, TRIAGE_DAY_CAP)], triage[(tier, tree, True, TRIAGE_DAY_CAP)]
            for name, rows in (("off", off), ("on", on)):
                n = len(rows)
                change = paired(off, on, "cities") if rows is on else ""
                print(f"| {tree} | {tier} | {name} | {wilson(sum(r['won'] for r in rows), n)} "
                      f"| {100 * sum(r['lost'] for r in rows) / n:.1f}% | {mean(r['cities'] for r in rows):.2f} "
                      f"| {change} | {mean(r['cleared'] for r in rows):.2f} | {mean(r['deaths'] for r in rows):.2f} "
                      f"| {mean(r['surges'] for r in rows):.2f} | {mean(r['empty'] for r in rows):.2f} "
                      f"| {statistics.median(r['days'] for r in rows):.0f} "
                      f"| {100 * sum(r['last_stand'] for r in rows) / n:.1f}% |")

    print("\n## Nobody enters any dungeon: 500 campaigns a cell, 2,000 days (the control)\n")
    print("| tier | switch | path to the Pillar lost | day lost: median | cities lost |")
    print("|---:|---|---|---:|---:|")
    identical = True
    for tier in (1, 2, 3, 4):
        off, on = alone[(tier, False)], alone[(tier, True)]
        identical = identical and off == on
        for name, rows in (("off", off), ("on", on)):
            lost = [r["day"] for r in rows if r["lost"]]
            median = f"{statistics.median(lost):.0f}" if lost else "-"
            print(f"| {tier} | {name} | {wilson(len(lost), len(rows))} | {median} "
                  f"| {mean(r['cities'] for r in rows):.2f} |")
    print(f"\nCONTROL: the undefended halves are {'identical' if identical else 'NOT IDENTICAL'}, campaign by "
          "campaign.")
    return 0 if identical else 1


if __name__ == "__main__":
    sys.exit(main())
