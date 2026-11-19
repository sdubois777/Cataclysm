"""What the Outpost ring's ordinary dungeon floor range does to a campaign -- issue
#2749, B9.

WHY THIS EXISTS. The project owner, playing the first surge on 2026-11-18, found
first-wave dungeons of 14 and 15 floors on the Outposts "a bit excessive for the
initial wave of dungeons out on the weakest cities", and answered, asked what
should change: "Just lower the non quest dungeons." The coordinating session read
that as lowering the Outpost ring's ordinary (Basic) range, then 8 to 15, with
Quest dungeons and the inner rings unchanged, and asked for the range to be
proposed from the simulation, measuring cities lost, days and win rate before
choosing. This file is that measurement. It writes no constant.

THE METHOD. For each candidate range, the shipped `TuningConfig()` with ONLY the
`(BASIC, OUTPOST)` row's floors replaced -- its damage, every other row and every
other setting as shipped -- run through every policy in `policies.ALL` for
`TRIALS` campaigns from seed 0 upward, the same seeds in every cell so the
comparison is paired, and summarised by `experiments.summarise`. The figures
printed are the means of the nine policies' summaries.

WHAT IT MEASURED, 2026-11-19, at 400 campaigns per policy per range (18,000
campaigns, 530 seconds), on development 2e0d6f8fc, before the row moved:

    range    win%   lost%  cities  days  triage%  idle%  floors
    8-15     35.3   40.9    9.4    1231   31.4    17.3    767
    6-12     36.9   40.3    9.1    1204   28.4    18.5    750
    5-10     37.0   41.1    8.4    1178   25.1    20.2    729
    4-8      37.3   41.9    7.8    1165   21.7    22.9    707
    3-6      38.7   41.2    7.1    1151   17.1    26.5    684

Win and loss barely move: the steps in win rate are inside the noise, about 2.5
points for one policy at 400 campaigns. What moves steadily with depth is the
strategy layer: cities lost and triage pressure fall, and the idle share of free
days rises. `experiments.health` aims at triage near 45% and idle at or below 20%.

THE RULING, by the coordinating session on 2026-11-19: 5 TO 10. It removes every
first-wave depth above 10, cuts the typical depth from 11.5 to 7.5, costs six
points of triage and keeps idle at its 20% line, where 4 to 8 crosses it for no
measured gain in wins. The game's `UCataclysmSurgeScheduler::SpecFor` and
`config.DUNGEON_SPECS` carry it; `docs/DECISIONS.md` records it.

SINCE THE ROW MOVED, "shipped" in this file's output is 5-10, and the 8-15 row is
the range before the ruling. The table above is a record and is not re-checked
by the fast suite: 18,000 campaigns is several minutes. `sim/tests/
test_analysis_scripts.py` runs the smoke default and checks what each cell
changes and which ranges are swept.

RUNNING IT. The default is a smoke test: one campaign a policy a range, which
proves the code path and nothing else, and the output says so. For the
measurement above:

    CATACLYSM_OUTPOST_DEPTH_TRIALS=400 python -u sim/analyse_outpost_depth.py
"""

from __future__ import annotations

import os
import time
from dataclasses import replace

import experiments
from cataclysm_sim import policies
from cataclysm_sim.config import CityTier, DungeonType, TuningConfig

#: Campaigns a policy a range. One by default: see "RUNNING IT" above.
TRIALS = int(os.environ.get("CATACLYSM_OUTPOST_DEPTH_TRIALS", "1"))

#: Below this many campaigns a cell, every figure printed is noise.
MEASURED_TRIALS = 100

#: The candidate ranges: the row before the ruling first, the suggested start of
#: the issue (4-8), the ruled range (5-10), one on each side of those, and 3-6.
RANGES: tuple[tuple[int, int], ...] = ((8, 15), (6, 12), (5, 10), (4, 8), (3, 6))

#: The row before issue #2749 moved it, and the row the ruling chose.
BEFORE = (8, 15)
RULED = (5, 10)

KEY = (DungeonType.BASIC, CityTier.OUTPOST)

#: The fields of `experiments.summarise` printed, in order.
FIELDS = ("win", "lost", "stale", "cities", "days", "death_day", "floors",
          "points", "triage", "idle", "deaths")


def config_for(least: int, most: int) -> TuningConfig:
    """The shipped config with only the Outpost ordinary row's floors replaced."""
    base = TuningConfig()
    specs = dict(base.DUNGEON_SPECS)
    specs[KEY] = replace(specs[KEY], floors=(least, most))
    return replace(base, DUNGEON_SPECS=specs)


def measure(trials: int = TRIALS) -> dict[tuple[int, int], dict[str, float]]:
    """The mean of the nine policies' summaries for each range, printing each
    policy's row as it goes."""
    overall: dict[tuple[int, int], dict[str, float]] = {}
    for least, most in RANGES:
        cfg = config_for(least, most)
        rows = []
        for name, policy in policies.ALL.items():
            summary = experiments.summarise(experiments.batch(cfg, policy, trials))
            rows.append(summary)
            print(f"{least:>2}-{most:<2} {name:<20} "
                  + " ".join(f"{f}={summary[f]:.1f}" for f in FIELDS))
        overall[(least, most)] = {f: sum(r[f] for r in rows) / len(rows)
                                  for f in FIELDS}
        print(f"{least:>2}-{most:<2} {'MEAN OF THE POLICIES':<20} "
              + " ".join(f"{f}={overall[(least, most)][f]:.1f}" for f in FIELDS))
    return overall


def main() -> dict[tuple[int, int], dict[str, float]]:
    shipped = TuningConfig().DUNGEON_SPECS[KEY].floors
    print(f"OUTPOST ORDINARY DUNGEON DEPTH, issue #2749 B9. {TRIALS} campaign(s) a "
          f"policy a range, {len(policies.ALL)} policies, seeds from 0.")
    print(f"The shipped Outpost ordinary row is {shipped[0]}-{shipped[1]}; "
          f"before the ruling it was {BEFORE[0]}-{BEFORE[1]}.")
    if TRIALS < MEASURED_TRIALS:
        print(f"SMOKE RUN: fewer than {MEASURED_TRIALS} campaigns a cell, so every "
              f"figure below is noise. Set CATACLYSM_OUTPOST_DEPTH_TRIALS=400 for "
              f"the measurement in this file's docstring.")
    began = time.time()
    overall = measure()
    print(f"\nTHE MEANS OF THE POLICIES ({TRIALS} campaign(s) a policy a range; "
          f"{time.time() - began:.0f} s)")
    print(f"{'range':<7}" + "".join(f"{f:>10}" for f in FIELDS))
    for (least, most), means in overall.items():
        mark = "  <- shipped" if (least, most) == shipped else ""
        print(f"{least}-{most:<5}" + "".join(f"{means[f]:>10.1f}" for f in FIELDS)
              + mark)
    return overall


RESULTS = main()
