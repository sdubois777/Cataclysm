"""What shape the Explorer branch's walk-time reduction should have. Issue #1383.

WHY THIS EXISTS. The project owner set a balance target on 2026-09-06, verbatim:
**"how do we keep a player who is fully invested into the explorer tree engaged?
That is what the game should be mostly balanced around, or at least to some
degree."** and directed, verbatim: **"adjust the explorer tree so it doesn't just
completely null the threat of the cataclysm"**.

Today the branch removes a flat 70 days from a dungeon's walk and the walk is
clamped at `run_days_min`, which is 1. Every ordinary dungeon in the game is
shallower than 71 floors, so the subtraction goes negative and lands on the
clamp. **Nothing here changes a constant.** Every shape is applied to a subclass
of `Simulation` for the length of one batch; `cataclysm_sim/config.py` and
`cataclysm_sim/engine.py` are not written to.

WHAT THE BRANCH ACTUALLY GIVES, node by node, read off
`docs/Empire_Development_Tree_Final.json` rather than off the modelled 70. The
node names are here for the reason issue #1288 gives: the Architect branch's
multiplier was a bare number and one of its factors turned out to match no node.

    UNCONDITIONAL, EVERY DUNGEON
      Temporal Mastery      -25   -1 day per point x 25            Explorer
      Overclock             -20   -1 day per point x 20            Explorer
      Pacing                -10   -1 day per point x 10            Explorer
      Fleet Footed           -5   keystone, flat                   Explorer
                          -----
                            -60

    CONDITIONAL, AND THE MODELLED 70 COUNTS BOTH AS IF THEY WERE NOT
      Opportunist            -5   ONLY in a city with no other active dungeon
      The Delver             -5   ONLY if chosen over The Aegis of Hope and
                                  The Hoarder at the Tier 1 milestone, and it
                                  is a CENTRAL capstone, not an Explorer node

    EXCLUDED FROM THE 70 AND FROM THIS FILE, ALL OF WHICH MAKE IT WORSE
      Rapid Descent         10 pts  -0.1 days per floor cleared per point, so
                                    -1.0 day per floor at full investment
      Sovereign's Haste     10 pts  -1 day per point per active Cataclysm,
                                    capped at -30
      Tactical Entry        keystone  run time HALVED above 50 floors -- the
                                    one multiplicative walk-time node the tree
                                    already has, and the config comment's
                                    exclusion list does not mention it
      Imperial Roads        10 pts  -1 day per point, Fallen City only, and it
                                    is an ARCHITECT node
      The Last Stand        capstone  walk reduced to one day outright, in a
                                    city within 7 days of falling

So `TREE_EXPLORER_AS_DESIGNED`'s 70 is 60 unconditional plus two conditional
fives, and the true full-investment figure is larger than 70 rather than equal to
it. **The shapes below are compared against 70 anyway**, because 70 is what
`development` models and what the owner's directive was given against, and
because every candidate shape has to beat the shipped one on the shipped number
before the extra nodes are worth arguing about.

TWO CLAIMS IN ISSUE #1383 THAT THIS FILE CORRECTS.

**"Every dungeon costs one day" is 91.7% of dungeons, not all of them.** Measured
over 200 campaigns at the settings below, 14,011 of 15,273 dungeons created walk
in exactly one day under the shipped branch. Every single one of the other 1,262
is either a **Cow Level**, whose time "is doubled and cannot be reduced" and so
never goes through `run_days_for` at all, or a dungeon **on the Pillar**, which
is the only place floor counts exceed 71. `one_day_share` recomputes it.

**A gradient does survive above 71 floors**, on the Cataclysm dungeon (100-150
floors) and the Pillar's Fallen City (80-120). It is the only gradient left, it
exists on dungeons a surge can never target -- `SURGE_TARGET_WEIGHT` gives the
Pillar 0.0 -- and it does not rescue the ordinary game.

WHAT IS HELD FIXED, AND THE CADENCE IS THE HALF THIS FILE DOES NOT MOVE.
Difficulty tier 1, the `triage` policy, **static surges every 120 days for 5
dungeons**, resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12 days
for +4% of tier width. Those are `experiments.exp_calibrate`'s choices and what
the balance report runs at; the raw `TuningConfig` default of 4 dungeons is a
value calibration rejects, so a figure here and a figure at 4 are not comparable.
**The owner's other directive was to raise the cadence** and that is being
measured separately. Every number in this file therefore describes a player on
today's cadence, which is the cadence the owner called "incredibly low". A shape
chosen here must be re-measured once the cadence moves.

HOW UNCERTAINTY IS REPORTED, AND WHY NOT AS A GAP BETWEEN TWO BLOCKS. Issue #1379
records the mistake: several measurements on this project have treated the
difference between two seed blocks as a noise floor. **Two blocks give one
difference, not a spread.** Every share here carries a binomial standard error
computed from the cell's own rate and size, every mean carries the standard error
of the campaign-level sample, and `BLOCKS` runs enough disjoint blocks that the
spread across them can be read directly. The two 2,000-campaign halves issue
#1383 asks for are printed as `A` and `B`; the per-block table underneath them is
what the tolerance is actually taken from.
"""

from __future__ import annotations

import math
import os
import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_EXPLORER_AS_DESIGNED, TREE_NONE, SurgeMode, TuningConfig,
)
from cataclysm_sim.engine import Simulation

#: Campaigns per block. `sim/tests/test_analysis_scripts.py` executes this file,
#: so the default has to be small; the figures put to the owner on issue #1383
#: were taken with `CATACLYSM_EXPLORER_TRIALS=500`, which is 4,000 campaigns a
#: shape and 48,000 in all. That is around half an hour. Run it in the
#: background, as `CLAUDE.md` says of `experiments.py` for the same reason.
#:
#: An environment variable rather than a command line flag because the test
#: executes this file with `runpy.run_path`, which leaves `sys.argv` pointing at
#: pytest's own arguments. `analyse_siege_dose.py` documents the same choice.
TRIALS = int(os.environ.get("CATACLYSM_EXPLORER_TRIALS", "1"))

#: Eight disjoint blocks. Disjoint by construction at any `TRIALS`. Eight rather
#: than two so the printed spread is a spread: issue #1379.
BLOCK_COUNT = 8
BLOCKS = tuple((chr(ord("a") + i), i * TRIALS) for i in range(BLOCK_COUNT))

#: The two halves issue #1383 asks for, as block groups rather than as separate
#: runs, so the halves and the spread come out of the same campaigns.
HALVES = (("A", BLOCKS[:4]), ("B", BLOCKS[4:]))

#: The depths every shape is quoted at. 11 / 20 / 32 are the midpoints of the
#: Basic dungeon at the three city sizes a surge can target; 50 is the depth the
#: project owner used in the sentence this whole question comes from, verbatim
#: "you could have a 50 floor dungeon that only takes you a couple days in world
#: time to beat"; 125 is the midpoint of the earned Cataclysm dungeon.
QUOTED_DEPTHS = (11, 20, 32, 50, 125)


def base_config() -> TuningConfig:
    """The settings every row runs under. See the module docstring."""
    return replace(
        TuningConfig(),
        tier=1,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=120.0,
        surge_dungeon_count=5,
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
    ).with_tree(TREE_NONE)


BASE = base_config()

#: THE SHIPPED FLAT, read off the config rather than written out, so a change to
#: `TREE_EXPLORER_AS_DESIGNED` cannot leave this file claiming the wrong
#: baseline. `analyse_siege_dose.py` does the same for the Siege spawn weight.
FLAT_TODAY = TREE_EXPLORER_AS_DESIGNED.run_days_flat


def flat(amount: float, minimum: int = 1):
    """`max(minimum, base - amount)` -- the shape `development` ships."""
    def shape(base_days: float) -> float:
        return max(float(minimum), base_days - amount)
    return shape


def mult(scalar: float, minimum: int = 1):
    """`max(minimum, base x scalar)` -- a rate change rather than a subtraction.

    THIS IS THE ONLY SHAPE THAT PRESERVES THE GRADIENT EXACTLY, because it is a
    change to the rate itself. `FCataclysmDungeon::WalkDaysPerFloor` is already
    that rate in the game, so this shape is a scalar on a quantity the engine
    computes rather than a new concept.
    """
    def shape(base_days: float) -> float:
        return max(float(minimum), base_days * scalar)
    return shape


def capped(scalar: float, amount: float, minimum: int = 1):
    """`max(minimum, base x scalar, base - amount)`.

    A CAP ON THE TOTAL REDUCTION AND A PROPORTIONAL FLOOR ARE THE SAME SHAPE,
    which is worth saying because issue #1383 lists them as two candidates.
    "Reduce by at most `1 - scalar` of the base" and "never go below `scalar`
    times the base" are the same inequality. Below the crossover -- `amount /
    (1 - scalar)` floors -- the proportional term binds and this behaves as
    `mult`; above it the flat term binds, so the deepest dungeons keep more of
    their cost than a pure multiplier would leave them.
    """
    def shape(base_days: float) -> float:
        return max(float(minimum), base_days * scalar, base_days - amount)
    return shape


#: The shapes measured, in the order they are printed. `none` is the uninvested
#: player and is the control issue #1383 asks for: a branch tuned so the invested
#: player is threatened must not make an untreed run impossible.
SHAPES = (
    ("none", None),
    (f"flat {FLAT_TODAY:g}", flat(FLAT_TODAY)),
    ("flat 45", flat(45.0)),
    ("flat 30", flat(30.0)),
    (f"flat {FLAT_TODAY:g} min5", flat(FLAT_TODAY, minimum=5)),
    (f"flat {FLAT_TODAY:g} min10", flat(FLAT_TODAY, minimum=10)),
    ("x0.30", mult(0.30)),
    ("x0.20", mult(0.20)),
    ("x0.15", mult(0.15)),
    ("x0.10", mult(0.10)),
    ("x0.05", mult(0.05)),
    (f"x0.15 cap{FLAT_TODAY:g}", capped(0.15, FLAT_TODAY)),
)


class _ShapedSimulation(Simulation):
    """A campaign whose walk time follows `shape` instead of the tree's.

    WHY THE OVERRIDE IS ON `run_days_for` AND NOT ON `_walk_days`. A Cow Level's
    time "is doubled and cannot be reduced", which `Simulation._walk_days`
    implements by not calling `run_days_for` at all. Overriding the inner method
    therefore inherits that exemption for free; overriding the outer one would
    silently apply every candidate shape to the one sub-type the design says is
    exempt. `cow_levels` below counts them so the exemption is visible in the
    output rather than assumed.

    It records every dungeon it creates, because the collapse is a fact about
    the dungeons a campaign contains and not about how the campaign ended.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0, shape=None) -> None:
        super().__init__(cfg, seed=seed)
        self._shape = shape
        self.made: list[tuple[int, int, str]] = []

    def run_days_for(self, floors: int) -> int:
        if self._shape is None:
            return super().run_days_for(floors)
        cfg = self.cfg
        days = self._shape(floors * cfg.days_per_floor)
        days = max(cfg.run_days_min, min(cfg.run_days_max, days))
        return int(math.ceil(days))

    def _make_dungeon(self, *args, **kwargs):
        d = super()._make_dungeon(*args, **kwargs)
        self.made.append((d.floors, d.run_days, d.subtype))
        return d


def empty_day_counter(policy):
    """Wrap a policy so free days facing an empty board are counted.

    **THIS IS THE OWNER'S COMPLAINT, AND `RunResult.idle_days` IS NOT IT.**
    `idle_days` counts every free day the policy declined, which includes days
    with dungeons standing that the player judged not worth entering. A day with
    nothing on the board at all is a different thing: there is no decision to
    make and no risk being taken, and it is what "4 dungeons every 120 days is
    incredibly low, and boring either way" describes. Both are reported.

    Draws no random numbers, so an instrumented batch is the same campaigns a
    bare one would run.
    """
    def counted(sim, dungeons):
        if not dungeons:
            sim.empty_days = getattr(sim, "empty_days", 0) + 1
        return policy(sim, dungeons)
    return counted


def one_day_share(shape, depths=None) -> float:
    """Share of the Basic dungeon depth range a surge can target that this
    shape puts on the one-day clamp.

    ANALYTIC AND NOT SAMPLED, over the union of the three ranges
    `TuningConfig.DUNGEON_SPECS` gives Basic dungeons at Outpost, Bulwark and
    Sanctuary -- 8 to 40 floors. A shape that clamps the whole range has no
    gradient left in ordinary play whatever a campaign happens to roll.
    """
    depths = depths or range(8, 41)
    days = [walk_days(shape, f) for f in depths]
    return 100.0 * sum(1 for d in days if d == 1) / len(days)


def walk_days(shape, floors: int, cfg: TuningConfig | None = None) -> int:
    """What `_ShapedSimulation.run_days_for` returns for a dungeon this deep."""
    cfg = cfg or BASE
    base_days = floors * cfg.days_per_floor
    days = base_days if shape is None else shape(base_days)
    days = max(cfg.run_days_min, min(cfg.run_days_max, days))
    return int(math.ceil(days))


def spread(shape) -> float:
    """The gradient, as the ratio of a median Sanctuary walk to a median Outpost
    walk: 32 floors against 11.

    **THIS IS THE FIGURE THE WHOLE ISSUE TURNS ON.** With no tree it is 32/11 =
    2.9; a shape that returns 1.0 has flattened the difference between a small
    dungeon and a large one, which is what issue #1383 says a flat subtraction
    cannot avoid once it exceeds the largest ordinary depth.
    """
    return walk_days(shape, 32) / walk_days(shape, 11)


def measure(shape, seed0: int, trials: int = TRIALS) -> dict:
    """One cell: `trials` campaigns from `seed0` upward under one shape."""
    cfg = BASE
    policy = empty_day_counter(policies.ALL["triage"])
    results, empties, made, cows = [], [], [], 0
    for i in range(trials):
        sim = _ShapedSimulation(cfg, seed=seed0 + i, shape=shape)
        results.append(sim.run(policy))
        empties.append(getattr(sim, "empty_days", 0))
        made.extend(sim.made)
        cows += sum(1 for m in sim.made if m[2] == "Cow Level")

    days = [max(1, r.survived_days) for r in results]
    one_day = sum(1 for _f, d, _s in made if d == 1)
    return {
        "n": trials,
        "earned%": 100.0 * sum(1 for r in results
                               if r.cataclysm_floors > 0) / trials,
        "won%": 100.0 * sum(1 for r in results if r.won) / trials,
        "lastStand%": 100.0 * sum(1 for r in results if r.last_stand) / trials,
        "cities": statistics.fmean([r.cities_lost for r in results]),
        "days": statistics.fmean(days),
        # The owner's complaint, twice: days with an empty board, and days the
        # player declined everything on it.
        "empty%": 100.0 * statistics.fmean(
            [e / d for e, d in zip(empties, days, strict=True)]),
        "idle%": 100.0 * statistics.fmean(
            [r.idle_days / d for r, d in zip(results, days, strict=True)]),
        "cleared": statistics.fmean([r.dungeons_cleared for r in results]),
        # The collapse itself, measured on the dungeons the campaigns contained
        # rather than on the depth range.
        "1day%": 100.0 * one_day / max(1, len(made)),
        "cows%": 100.0 * cows / max(1, len(made)),
        # Campaign-level samples, kept so the caller can pool blocks and take a
        # standard error over the campaigns rather than over the block means.
        "_cities": [r.cities_lost for r in results],
        "_earned": [1.0 if r.cataclysm_floors > 0 else 0.0 for r in results],
        "_empty": [e / d for e, d in zip(empties, days, strict=True)],
    }


def pool(cells: list[dict]) -> dict:
    """Several blocks read as one sample, with standard errors.

    THE ERRORS COME FROM THE CAMPAIGNS AND NOT FROM THE BLOCK MEANS. Both are
    legitimate; the campaign-level one is the tighter of the two and does not
    need enough blocks to estimate a variance, which is what issue #1379 found
    a two-block gap could not do.
    """
    n = sum(c["n"] for c in cells)
    out = {"n": n}
    for key in ("earned%", "won%", "lastStand%", "cities", "days", "empty%",
                "idle%", "cleared", "1day%", "cows%"):
        out[key] = sum(c[key] * c["n"] for c in cells) / n
    for key, sample in (("earned%", "_earned"), ("cities", "_cities"),
                        ("empty%", "_empty")):
        values = [v for c in cells for v in c[sample]]
        scale = 100.0 if key != "cities" else 1.0
        out[key + "_se"] = (scale * statistics.stdev(values) / math.sqrt(n)
                            if n > 1 else 0.0)
    return out


HEADER = (f"{'shape':>14} {'blk':>4} {'earned%':>8} {'won%':>6} {'LS%':>6} "
          f"{'cities':>7} {'days':>6} {'empty%':>7} {'idle%':>6} "
          f"{'cleared':>8} {'1day%':>7}")


def row(label: str, block: str, s: dict) -> str:
    return (f"{label:>14} {block:>4} {s['earned%']:>8.1f} {s['won%']:>6.1f} "
            f"{s['lastStand%']:>6.1f} {s['cities']:>7.2f} {s['days']:>6.0f} "
            f"{s['empty%']:>7.1f} {s['idle%']:>6.1f} {s['cleared']:>8.1f} "
            f"{s['1day%']:>7.1f}")


def main() -> dict:
    print("=" * 108)
    print("THE EXPLORER BRANCH'S WALK-TIME SHAPE -- issue #1383")
    print("=" * 108)
    print(f"tier {BASE.tier}, policy triage, {BASE.surge_mode.name.lower()} "
          f"surges every {BASE.surge_interval_days:g} days for "
          f"{BASE.surge_dungeon_count} dungeons, "
          f"resolve floor ratio {BASE.resolve_floor_ratio:g}, "
          f"{TRIALS} campaigns x {BLOCK_COUNT} blocks = "
          f"{TRIALS * BLOCK_COUNT} per shape.")
    print("THE SURGE CADENCE IS HELD AT TODAY'S VALUE and the owner has "
          "directed that it rise. Every figure below")
    print("describes a player on the cadence they called 'incredibly low'. "
          "Re-measure when it moves.")
    print()

    print("-" * 108)
    print("1. THE SHAPE ITSELF, before any campaign is run. Walk days at each "
          "depth, and the gradient.")
    print("-" * 108)
    head = "  ".join(f"{f:>4}f" for f in QUOTED_DEPTHS)
    print(f"{'shape':>14}  {head}  {'32f/11f':>8}  {'8-40f on the clamp':>19}")
    shape_table = {}
    for label, shape in SHAPES:
        cells = "  ".join(f"{walk_days(shape, f):>5}" for f in QUOTED_DEPTHS)
        shape_table[label] = {
            "days": {f: walk_days(shape, f) for f in QUOTED_DEPTHS},
            "spread": spread(shape),
            "clamped%": one_day_share(shape),
        }
        print(f"{label:>14}  {cells}  {spread(shape):>8.2f}  "
              f"{one_day_share(shape):>18.0f}%")
    print()
    print("A SPREAD OF 1.00 IS THE COLLAPSE issue #1383 reports: a 32 floor "
          "dungeon and an 11 floor one cost the")
    print("same day, so floor count has stopped being a reason to choose one "
          "over the other. Only a multiplier")
    print("holds the untreed 2.91 exactly; every subtraction loses some of it "
          "and a large enough one loses all.")
    print()

    print("-" * 108)
    print("2. WHAT EACH SHAPE DOES TO A CAMPAIGN. Both 2,000-campaign halves "
          "issue #1383 asks for, then the")
    print("   eight blocks they are made of, so the spread is a spread and "
          "not one difference.")
    print("-" * 108)
    print(HEADER)
    measured = {}
    for label, shape in SHAPES:
        cells = {name: measure(shape, seed0) for name, seed0 in BLOCKS}
        measured[label] = cells
        for half, group in HALVES:
            print(row(label, half, pool([cells[name] for name, _ in group])))
        for name, _ in BLOCKS:
            print(row("", name, cells[name]))
    print()

    print("-" * 108)
    print("3. POOLED, WITH STANDARD ERRORS. A gap smaller than the two errors "
          "added is not a measurement.")
    print("-" * 108)
    print(f"{'shape':>14} {'earned%':>16} {'cities lost/25':>18} "
          f"{'empty days%':>16} {'spread':>8}")
    pooled = {}
    for label, _shape in SHAPES:
        p = pool([measured[label][name] for name, _ in BLOCKS])
        pooled[label] = p
        print(f"{label:>14} {p['earned%']:>10.1f} +-{p['earned%_se']:<4.1f} "
              f"{p['cities']:>12.2f} +-{p['cities_se']:<4.2f} "
              f"{p['empty%']:>10.1f} +-{p['empty%_se']:<4.1f} "
              f"{shape_table[label]['spread']:>8.2f}")
    print()

    control = pooled["none"]
    today = pooled[f"flat {FLAT_TODAY:g}"]
    print(f"THE UNINVESTED PLAYER reaches the earned Cataclysm dungeon in "
          f"{control['earned%']:.1f}% of campaigns and loses "
          f"{control['cities']:.2f} of 25 cities.")
    print(f"THE SHIPPED BRANCH takes that to {today['earned%']:.1f}% and "
          f"{today['cities']:.2f}, with {today['1day%']:.0f}% of every dungeon "
          f"it meets walked in a day.")
    print("A shape is a candidate when it leaves the invested player ahead of "
          "the uninvested one -- investment")
    print("must still be worth something -- while keeping a gradient the "
          "untreed player would recognise.")
    return {"shape_table": shape_table, "pooled": pooled,
            "measured": measured}


RESULT = main()
