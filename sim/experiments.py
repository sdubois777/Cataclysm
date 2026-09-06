"""Run the tuning experiments and print a report.

    python experiments.py

Ground rules now fixed rather than swept:
  * One dungeon floor costs one day. Fixed by this model rather than by the
    design: `days_per_floor` is 1.0 and no sweep moves it. In the game that is
    a starting rate, and city upgrades and the empire tree lower it while the
    floor count stays where it is. `sim/README.md` has the long version.
  * The dungeon you are inside has its timer PAUSED. Everything else ticks.
  * A city falling triggers a surge.
  * Days are also spent at the forge, which defends nothing.
  * Surges escalate; the open question is HOW (see experiment 2).
"""

from __future__ import annotations

import math
import statistics
from dataclasses import replace

from cataclysm_sim import combat, policies, scoring
from cataclysm_sim.config import (
    TREE_ARCHITECT_AS_DESIGNED, TREE_EXPLORER_AS_DESIGNED, TREE_EXPLORER_DEEP,
    TREE_EXPLORER_VIA_FLOORS, TREE_NONE, TREE_PROPOSED_FIX,
    CityTier, DungeonType, EmpireTree, SurgeMode, TuningConfig,
)
from cataclysm_sim.engine import Simulation

TRIALS = 250

#: The difficulty tier every section runs at, except the preset comparison.
#:
#: THE GAME HAS EIGHT TIERS AND THIS REPORT MEASURES ONE. It always did: the
#: default in `cataclysm_sim/config.py` is `tier: int = 1` and no section used to
#: override it, so every tuning conclusion this project holds is a tier 1 result
#: and nothing said so. Issue #281. Named here so that it is a decision rather
#: than an accident, and printed in the header so a reader of the output cannot
#: miss it.
#:
#: Tier matters because tier width -- the gap to the tier below -- multiplies
#: every weighted term of the Enemy Score formula. It runs 385, 498, 625, 717,
#: 853, 979, 1063, 1207, so the relation between player power and enemy power is
#: not the same at both ends.
SWEEP_TIER = 1

#: Which tiers the empire tree preset comparison runs at, section 7.
#:
#: Both ends of the curve, because that is where a scaling problem shows and
#: sweeping all eight would cost about two and a half hours. Section 7 is the one
#: worth paying for: issues #4 and #5, the two open tuning findings, both turn on
#: the preset ordering it prints. If the ordering is the same at tier 1 and tier
#: 8 the tier 1 conclusions probably generalise; if it differs, that is a finding
#: on its own. Issue #281.
#:
#: THE OTHER SETTINGS STAY AS CALIBRATED AT TIER 1. Sections 0 and 2 pick the
#: surge and forge numbers, and they are not re-derived per tier. Only the tier
#: changes between the two tables, which is what makes them comparable.
PRESET_TIERS = (1, 8)


#: The second surge size the preset comparison runs at, beside the one sections 0
#: and 2 calibrated. Issue #1297; the project owner chose to add one on
#: 2026-09-05 rather than to leave the section on one surge size or to give up a
#: difficulty tier for it.
#:
#: WHY THE ORDERING NEEDS A SECOND SURGE SIZE AT ALL. It moves more with this
#: number than with the tier. Measured 2026-09-05 at 150 campaigns per cell with
#: everything else held at what this section receives, the ordering differs from
#: the calibrated 5 at EVERY other value tried, at both tiers:
#:
#:      dungeons per surge          4     5     6     7
#:      tier 1, no tree win%       44    52    15    11
#:      tier 1, presets beating it  0     1     4     4
#:      tier 8, presets beating it  1     1     2     2
#:
#: WHY 7 AND NOT 6 OR 4. Two reasons decide it and a third does not.
#:
#:   1. It is the far end of the range `exp_calibrate` sweeps, which is 5, 6, 7.
#:      That mirrors PRESET_TIERS taking both ends of the tier range for the same
#:      reason: a scaling problem shows at the ends.
#:   2. It is the CHEAPEST of the three, 102 seconds against 106 for 6 and 136
#:      for 4. A tier 8 block gets FASTER as the surge grows, because campaigns
#:      end sooner: 108 seconds at 4 against 72 at 7.
#:   3. NOT "it separates the presets most", which decided this once and does
#:      not now. On the pool this section ran at when issue #1297 chose 7, that
#:      value beat 6 at tier 8, three presets against two. Issue #1303 took the
#:      Corrupted Stalker back out of the modifier pool, which re-rolls every
#:      campaign, and 6 and 7 now TIE at both tiers -- four presets beat the
#:      no-tree row at tier 1 and two at tier 8, for both. The value did not
#:      change because the two reasons above were always pool-independent, but
#:      the reasoning is written as it now stands rather than as it was.
#:
#: 4 IS THE `TuningConfig` DEFAULT AND WAS REJECTED. `exp_calibrate` never tries
#: it, so it describes a world this report would not choose; it is also the
#: slowest of the three and leaves two presets unrankable at tier 8 rather than
#: one, which narrows the ordering it is supposed to widen.
PRESET_SECOND_SURGE_SIZE = 7


def cataclysm_power_key(cfg: TuningConfig) -> list[str]:
    """The lines describing what the power column should be compared against.

    Issue #8. This used to be a hard-coded range, worked out against the player
    power anchors that issue #2 replaced and never re-derived. It is computed
    here instead, for the tier the report is actually running at, so it cannot
    go stale again. The retired range is not written out anywhere in this file,
    because `sim/tests/test_power_threshold.py` refuses it by text.

    IT IS NOT ONE NUMBER. Death chance falls smoothly with power and never
    reaches zero, because the thing on the last floor outscores the maximum
    player power of its own tier at every tier.
    """
    spec = cfg.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)]
    floors = round(sum(spec.floors) / 2)
    modifier_score = 0.0
    ceiling = scoring.tier_bounds(cfg.tier)[1]

    def risk(power: float) -> float:
        return combat.death_chance(
            power, cfg.tier, "Cataclysm", "None", floors, modifier_score,
            cfg.per_floor_risk, cfg.boss_risk_multiplier, "Cataclysm Boss",
            cfg.overwhelm_rate, cfg.overwhelm_cap)

    needed = combat.power_for_death_chance(
        combat.REPORTED_DEATH_CHANCE, cfg.tier, "Cataclysm", "None", floors,
        modifier_score, cfg.per_floor_risk, cfg.boss_risk_multiplier,
        "Cataclysm Boss", cfg.overwhelm_rate, cfg.overwhelm_cap)

    # Said rather than assumed: whether the safer figure is reachable follows
    # from the two numbers, so this sentence cannot become false while the
    # numbers move.
    reach = ("and so is NOT reachable at this tier"
             if needed > ceiling else "and is reachable within this tier")

    return [
        "  power      final player Power Score. The Cataclysm dungeon at tier "
        f"{cfg.tier} is {floors} floors and",
        f"             kills a player at the tier ceiling of {ceiling:,.0f} "
        f"{risk(ceiling):.0%} of the time. Dying there ends",
        f"             the run. A {combat.REPORTED_DEATH_CHANCE:.0%} death "
        f"chance needs {needed:,.0f}, which is "
        f"{needed / ceiling:.2f}x that ceiling,",
        f"             {reach}.",
    ]


def header_lines() -> list[str]:
    """Everything printed before the first section, including which tier this is.

    A FUNCTION SO IT CAN BE TESTED. `main()` runs about 25,000 campaigns and
    takes roughly eighteen minutes, so a test that called it to read the header
    would never be run. `sim/tests/test_sweep_tier.py` calls this instead.
    """
    ceiling = scoring.tier_bounds(SWEEP_TIER)[1]
    return [
        "CATACLYSM -- empire layer tuning rig (rev 3: paused timers + the forge)",
        f"{TRIALS} campaigns per cell, deterministic seeds",
        "",
        f"DIFFICULTY TIER {SWEEP_TIER} of 8, player power ceiling "
        f"{ceiling:,.0f}. Every section below runs at",
        "that tier and no other, except section 7, which also runs at tier "
        f"{PRESET_TIERS[-1]}.",
        f"A result from this report is a tier {SWEEP_TIER} result unless its own "
        "heading says so. Issue #281.",
        "",
        f"ACTIVE CATACLYSMS {replace(TuningConfig(), tier=SWEEP_TIER).active_cataclysm_count()} "
        f"of 8, which is the tier. WHICH ones is drawn per campaign, from the",
        "seed, so a cell averages over the characters a player would meet rather "
        "than describing",
        "one fixed world. Until issue #1338 the count was 1 at every tier and "
        "the set was always",
        "Demonic, so every figure this report produced before then was a "
        "one-Cataclysm figure",
        "whatever its heading said.",
        "",
        "Column key:",
        "  win%       cleared 8 objectives, then the Cataclysm dungeon",
        "  stale%     neither won nor lost -- hit the day cap",
        "  floors     total floors cleared -- the loot proxy",
        *cataclysm_power_key(replace(TuningConfig(), tier=SWEEP_TIER)),
        "  forge%     share of the run spent at the forge, defending nothing",
        "  triage%    free days facing 2+ dungeons about to detonate",
    ]


def batch(cfg: TuningConfig, policy, trials: int = TRIALS, seed0: int = 0):
    return [Simulation(cfg, seed=seed0 + i).run(policy) for i in range(trials)]


def mean(xs):
    return statistics.fmean(xs) if xs else 0.0


def summarise(results):
    lost = [r for r in results if r.lost]
    return {
        "days": mean([r.survived_days for r in results]),
        "death_day": mean([r.survived_days for r in lost]),
        "win": 100.0 * mean([1.0 if r.won else 0.0 for r in results]),
        "lost": 100.0 * mean([1.0 if r.lost else 0.0 for r in results]),
        "stale": 100.0 * mean([0.0 if (r.won or r.lost) else 1.0 for r in results]),
        "cities": mean([r.cities_lost for r in results]),
        "obj": mean([r.objectives for r in results]),
        "floors": mean([r.floors_cleared for r in results]),
        "power": mean([r.power for r in results]),
        "crafts": mean([r.crafts for r in results]),
        "forge%": 100.0 * mean([r.craft_days / max(1, r.survived_days) for r in results]),
        "deaths": mean([r.deaths for r in results]),
        "surges": mean([r.surges for r in results]),
        "gap": mean([r.final_surge_gap for r in results]),
        "count": mean([r.final_surge_count for r in results]),
        "triage": 100.0 * mean([r.triage_pressure for r in results]),
        "idle": 100.0 * mean([r.idle_days / max(1, r.free_days) for r in results]),
        "points": mean([r.empire_points for r in results]),
    }


def rule(title):
    print(f"\n{'=' * 100}\n{title}\n{'=' * 100}")


def policy_spread(cfg: TuningConfig, trials: int = TRIALS):
    rows = [(n, summarise(batch(cfg, f, trials))) for n, f in policies.ALL.items()]
    score = [s["win"] - s["lost"] for _, s in rows]
    return rows, max(score) - min(score)


def health(s) -> float:
    pen = 0.0
    pen += abs(s["win"] - 55.0) * 1.0
    pen += max(0.0, s["stale"] - 10.0) * 2.0
    pen += abs(s["triage"] - 45.0) * 1.2
    pen += max(0.0, s["idle"] - 20.0) * 1.5
    return -pen


# ---------------------------------------------------------------------------

def exp_calibrate() -> TuningConfig:
    rule("0. CALIBRATION -- timers pause inside dungeons, forge competes for days")
    print(f"{'ratio':>7}{'interval':>10}{'count':>7}{'days':>7}{'win%':>7}"
          f"{'loss%':>7}{'stale%':>8}{'obj':>6}{'power':>7}{'triage%':>9}"
          f"{'idle%':>7}{'health':>9}")
    print("-" * 100)

    best = None
    for ratio in (1.2, 1.6, 2.0):
        for interval in (75, 90, 120):
            for count in (5, 6, 7):
                cfg = replace(TuningConfig(),
                              tier=SWEEP_TIER,
                              surge_mode=SurgeMode.STATIC,
                              resolve_floor_ratio=ratio,
                              surge_interval_days=float(interval),
                              surge_dungeon_count=count)
                s = summarise(batch(cfg, policies.triage, trials=80))
                h = health(s)
                print(f"{ratio:>7.1f}{interval:>10}{count:>7}{s['days']:>7.0f}"
                      f"{s['win']:>7.0f}{s['lost']:>7.0f}{s['stale']:>8.0f}"
                      f"{s['obj']:>6.1f}{s['power']:>7.0f}{s['triage']:>9.1f}"
                      f"{s['idle']:>7.1f}{h:>9.0f}")
                if best is None or h > best[0]:
                    best = (h, cfg, s, ratio, interval, count)

    _, cfg, s, ratio, interval, count = best
    print(f"\n  BEST STATIC BASELINE: ratio {ratio}, surge every {interval}d x{count}")
    print(f"    -> {s['win']:.0f}% win, {s['lost']:.0f}% loss, "
          f"{s['stale']:.0f}% stalemate, {s['triage']:.0f}% triage, "
          f"{s['idle']:.0f}% idle, final power {s['power']:.0f}")
    return cfg


def exp_policies(base: TuningConfig):
    rule("1. POLICY COMPARISON -- dungeon vs forge vs nothing")
    print(f"{'policy':<18}{'days':>7}{'win%':>7}{'loss%':>7}{'stale%':>8}"
          f"{'cities':>8}{'obj':>6}{'floors':>8}{'power':>7}{'crafts':>8}"
          f"{'forge%':>8}{'triage%':>9}")
    print("-" * 100)
    rows, spread = policy_spread(base)
    for name, s in rows:
        print(f"{name:<18}{s['days']:>7.0f}{s['win']:>7.0f}{s['lost']:>7.0f}"
              f"{s['stale']:>8.0f}{s['cities']:>8.1f}{s['obj']:>6.1f}"
              f"{s['floors']:>8.0f}{s['power']:>7.0f}{s['crafts']:>8.1f}"
              f"{s['forge%']:>8.1f}{s['triage']:>9.1f}")
    print(f"\n  POLICY SPREAD: {spread:.0f} points of win rate.")
    print("  Compare never_craft against always_craft against triage: if all")
    print("  three land in the same place, the forge is not a real decision.")
    return spread


#: Difficulty treadmill rates section 2 sweeps. It hands section 7 whichever cell
#: wins, so THIS is where section 7's escalation rate comes from -- not from
#: `TuningConfig.dungeon_power_escalation_per_100_days`, whose default of 0.22 is
#: only the starting point. Measured 2026-09-05, the sweep chose 0.10.
#:
#: Named rather than written inline because the day-cap warning used to quote the
#: default as though the table had run at it, printing 22% and a 6.5x power
#: multiplier beside a table produced at 10% and 3.5x. Issue #1290.
FORGE_ESCALATION_RATES = (0.00, 0.10, 0.22, 0.35, 0.50)


def exp_forge(base: TuningConfig):
    rule("2. THE FORGE -- is 'build the gear and let a city burn' a real choice?")
    print("  The forge only matters if power is scarce, and power is only scarce")
    print("  if the Cataclysm outruns passive loot. Sweeping that treadmill rate")
    print("  against what a craft is worth.\n")
    print("  A HEALTHY cell is one where 'judicious' beats BOTH extremes: never")
    print("  crafting starves you of power, always crafting costs you the empire.\n")
    print(f"{'esc/100d':>12}{'craft':>13}{'never%':>9}{'always%':>9}"
          f"{'judicious%':>12}{'edge':>7}{'crafts':>8}{'forge%':>8}"
          f"{'power':>8}{'cities':>8}")
    print("-" * 100)
    best = None
    for esc in FORGE_ESCALATION_RATES:
        for days, frac in ((12, 0.04), (12, 0.08)):
            cfg = replace(base,
                          dungeon_power_escalation_per_100_days=esc,
                          craft_days=days, craft_power_gain_frac=frac)
            n = summarise(batch(cfg, policies.never_craft, trials=120))
            a = summarise(batch(cfg, policies.always_craft, trials=120))
            t = summarise(batch(cfg, policies.triage, trials=120))
            # How much judgement beats the better of the two dumb extremes.
            edge = t["win"] - max(n["win"], a["win"])
            print(f"{esc:>12.2f}{f'{days}d +{frac:.0%}w':>13}"
                  f"{n['win']:>9.0f}{a['win']:>9.0f}{t['win']:>12.0f}"
                  f"{edge:>7.0f}{t['crafts']:>8.1f}{t['forge%']:>8.1f}"
                  f"{t['power']:>8.0f}{t['cities']:>8.1f}")
            if best is None or edge > best[0]:
                best = (edge, esc, days, frac)
    edge, esc, days, frac = best
    print(f"\n  BEST: escalation {esc:.2f} per 100 days, craft {days}d +{frac:.0%} tier width")
    print(f"    judgement beats the better extreme by {edge:.0f} points of win rate.")
    if edge <= 0:
        print("    ...which is <= 0, so the forge is still not a real decision.")
    return replace(base, dungeon_power_escalation_per_100_days=esc,
                   craft_days=days, craft_power_gain_frac=frac)


def exp_surge_modes(base: TuningConfig):
    rule("3. HOW SHOULD SURGES ESCALATE? -- accelerate, swell, or both")
    print(f"{'mode':<26}{'win%':>7}{'loss%':>7}{'stale%':>8}{'days':>7}"
          f"{'death day':>11}{'surges':>8}{'end gap':>9}{'end N':>7}"
          f"{'triage%':>9}{'spread':>8}")
    print("-" * 100)
    configs = {
        "STATIC (no escalation)": replace(base, surge_mode=SurgeMode.STATIC),
        "ACCELERATING x0.88": replace(base, surge_mode=SurgeMode.ACCELERATING,
                                      surge_interval_decay=0.88),
        "ACCELERATING x0.93": replace(base, surge_mode=SurgeMode.ACCELERATING,
                                      surge_interval_decay=0.93,
                                      surge_interval_min=40.0),
        "SWELLING +0.5/surge": replace(base, surge_mode=SurgeMode.SWELLING,
                                       surge_count_growth=0.5),
        "SWELLING +1.0/surge": replace(base, surge_mode=SurgeMode.SWELLING,
                                       surge_count_growth=1.0),
        "BOTH (x0.92, +0.34)": replace(base, surge_mode=SurgeMode.BOTH,
                                       surge_interval_decay=0.92,
                                       surge_count_growth=0.34),
    }
    for name, cfg in configs.items():
        s = summarise(batch(cfg, policies.triage))
        _, spread = policy_spread(cfg, trials=120)
        print(f"{name:<26}{s['win']:>7.0f}{s['lost']:>7.0f}{s['stale']:>8.0f}"
              f"{s['days']:>7.0f}{s['death_day']:>11.0f}{s['surges']:>8.1f}"
              f"{s['gap']:>9.0f}{s['count']:>7.1f}{s['triage']:>9.1f}{spread:>8.0f}")


def exp_death_spiral(base: TuningConfig):
    rule("4. THE DEATH SPIRAL -- should a fallen city speed the clock up?")
    print(f"{'mode':<26}{'fall advances':>15}{'win%':>7}{'loss%':>7}"
          f"{'death day':>11}{'cities':>8}{'triage%':>9}")
    print("-" * 100)
    for mode in (SurgeMode.ACCELERATING, SurgeMode.SWELLING):
        for advances in (False, True):
            cfg = replace(base, surge_mode=mode, city_fall_advances_escalation=advances)
            s = summarise(batch(cfg, policies.triage, trials=150))
            print(f"{mode.value:<26}{str(advances):>15}{s['win']:>7.0f}"
                  f"{s['lost']:>7.0f}{s['death_day']:>11.0f}"
                  f"{s['cities']:>8.1f}{s['triage']:>9.1f}")


def exp_explorer(base: TuningConfig):
    rule("5. EXPLORER BRANCH -- flat day reduction")
    print(f"{'flat days':<12}{'15fl':>7}{'30fl':>7}{'60fl':>7}{'win%':>7}"
          f"{'loss%':>7}{'floors':>8}{'crafts':>8}{'triage%':>9}{'idle%':>8}"
          f"{'spread':>8}")
    print("-" * 100)
    for flat in (0, 3, 5, 10, 20, 70):
        cfg = base.with_tree(EmpireTree(name=f"-{flat}d", run_days_flat=flat))
        sim = Simulation(cfg, seed=0)
        cols = [sim.run_days_for(f) for f in (15, 30, 60)]
        s = summarise(batch(cfg, policies.triage, trials=150))
        _, spread = policy_spread(cfg, trials=120)
        print(f"{flat:<12}" + "".join(f"{c:>7}" for c in cols) +
              f"{s['win']:>7.0f}{s['lost']:>7.0f}{s['floors']:>8.0f}"
              f"{s['crafts']:>8.1f}{s['triage']:>9.1f}{s['idle']:>8.1f}"
              f"{spread:>8.0f}")
    print("\n  The tree as written grants 70 flat days.")


def exp_days_vs_floors(base: TuningConfig):
    rule("6. DAYS VS FLOORS -- the same speedup, bought two different ways")
    print(f"{'tree':<32}{'30fl run':>10}{'win%':>7}{'loss%':>7}{'floors':>9}"
          f"{'power':>8}{'crafts':>8}{'triage%':>9}{'spread':>8}")
    print("-" * 100)
    for label, tree in [
        ("baseline", EmpireTree(name="b")),
        ("-10 days flat", EmpireTree(name="d10", run_days_flat=10)),
        ("-10 floors", EmpireTree(name="f10", floor_delta=-10)),
        ("-25 days flat", EmpireTree(name="d25", run_days_flat=25)),
        ("-25 floors", EmpireTree(name="f25", floor_delta=-25)),
        ("+30 floors (deep)", EmpireTree(name="f+30", floor_delta=+30)),
    ]:
        cfg = base.with_tree(tree)
        eff = max(1, int(round(30 + tree.floor_delta)))
        rd = Simulation(cfg, seed=0).run_days_for(eff)
        s = summarise(batch(cfg, policies.triage, trials=150))
        _, spread = policy_spread(cfg, trials=120)
        print(f"{label:<32}{rd:>10}{s['win']:>7.0f}{s['lost']:>7.0f}"
              f"{s['floors']:>9.0f}{s['power']:>8.0f}{s['crafts']:>8.1f}"
              f"{s['triage']:>9.1f}{spread:>8.0f}")
    print("\n  'floors' is the loot proxy. Flat day reduction keeps the reward")
    print("  and deletes the cost. Floor reduction pays for itself.")


PRESETS = (TREE_NONE, TREE_EXPLORER_AS_DESIGNED, TREE_EXPLORER_VIA_FLOORS,
           TREE_EXPLORER_DEEP, TREE_ARCHITECT_AS_DESIGNED, TREE_PROPOSED_FIX)


def win_rate_noise(trials: int) -> float:
    """How far apart two win rates must be before the gap means anything.

    A cell is `trials` independent campaigns, so its win rate is a binomial
    proportion whose standard error is at most sqrt(0.25 / trials), largest at
    50%. The difference of two independent rates has sqrt(2) times that. At 150
    campaigns per cell it comes to 5.8 percentage points.

    WITHOUT THIS THE ORDERING IS NOISE. Six presets sorted by win rate always
    produce an ordering, whether or not the gaps mean anything, and at tier 8 the
    win rate collapses towards zero and every preset ties. Reporting that as
    "the ordering differs between tiers" would be reporting the sort's tie-break
    as a finding.
    """
    return 100.0 * math.sqrt(2.0) * math.sqrt(0.25 / max(1, trials))


def margin_variance(win: float, loss: float) -> float:
    """The variance of one campaign's outcome, scored +1 win, -1 loss, 0 none.

    Both rates are percentages, as `summarise` reports them. The result is in
    units of the score itself, so 1.0 is the largest it can be.
    """
    p, q = win / 100.0, loss / 100.0
    return max(0.0, (p + q) - (p - q) ** 2)


def margin_noise(trials: int) -> float:
    """How far apart two win-minus-loss margins must be before the gap means
    anything.

    WHY IT IS NOT `win_rate_noise`. Issue #294. Win and loss are not two
    independent rates. They come from the same campaign, which scores

        +1  won        -1  lost        0  no result

    so the margin a cell reports is the sample mean of that one score, and its
    spread is the variance of a single random variable rather than the
    difference of two independent binomial proportions. Using `win_rate_noise`
    on a margin reports a tolerance half the size of the right one.

    THE DERIVATION. Write p for the win probability and q for the loss
    probability, with p + q <= 1. For the score S above,

        E[S]   = p - q
        E[S^2] = p + q          because S^2 is 1 for a win or a loss and 0
                                otherwise
        Var(S) = (p + q) - (p - q)^2

    A cell is `trials` independent campaigns, so the standard error of its
    margin is sqrt(Var(S) / trials), and comparing two independent cells needs
    sqrt(Var_a / trials + Var_b / trials).

    THE WORST CASE IS Var(S) = 1. The first term is largest when p + q = 1, no
    campaign going unresolved, and the second is smallest when p = q. Both hold
    at p = q = 0.5. So the bound is

        100 * sqrt(2) * sqrt(1 / trials)

    which is exactly twice `win_rate_noise(trials)`, because the win rate's own
    worst case is sqrt(0.25) and this one is sqrt(1). At 150 campaigns per cell
    it is 11.5 percentage points; at 60 it is 18.3.

    IT IS A BOUND, NOT AN ESTIMATE, and for these tables a conservative one. A
    cell where almost every campaign loses has p near 0 and q near 1, giving
    Var(S) near zero and a true tolerance far below the bound. Measured at 60
    campaigns per cell on 2026-08-05, the closest pair at tiers 6 and 7 needed
    2.3 points and this bound gave them 18.3.

    THE GROUPING NO LONGER USES THE BOUND. Issue #328. It uses
    `margin_noise_between`, which estimates the variance from the rates each
    pair of cells actually observed, after `smoothed_rates` has pulled those
    rates off the boundary. This bound stays as the CAP that `rank_by_score`
    applies to any per-pair tolerance, and as the comparison the report prints
    per tier so the difference between the two is visible.

    WHAT THIS PARAGRAPH USED TO SAY. Until issue #328 it read "THE GROUPING
    USES THE BOUND ANYWAY, and that is deliberate", because estimating the
    variance from observed rates sends it to exactly zero for a cell that won
    nothing, which would call any non-zero gap significant. That is still true
    of an unsmoothed estimate; `smoothed_rates` is what fixed it, and
    `sim/analyse_margin_tolerance.py` is the measurement that chose how much
    smoothing.

    THE BOUND ERRS SAFELY. It can report a real difference as a tie; it cannot
    report noise as a difference.
    """
    return 2.0 * win_rate_noise(trials)


#: Pseudo-outcomes added to each of a campaign's three possible results -- won,
#: lost, no result -- before a cell's variance is estimated from the rates it
#: observed. Issue #328.
#:
#: WHY ANY SMOOTHING. A cell that observed 0 wins in 60 campaigns gets an
#: estimated win probability of exactly 0, and `margin_variance` then returns
#: exactly 0, so `margin_noise_between` gives that pair a tolerance of exactly
#: 0 and calls any non-zero gap significant. The cell's true win rate is not
#: zero; the sample has simply not seen the tail. Substituting the observed rate
#: straight in asserts a certainty the sample does not have.
#:
#: WHY 0.5 AND NOT SOMETHING ELSE. Two derivations agree on it, and the
#: measurement in `sim/analyse_margin_tolerance.py` accepts it.
#:
#:   1. Agresti-Coull adds z^2/2 notional outcomes to each category, where z is
#:      the number of standard errors the interval covers. The familiar "add 2
#:      successes and 2 failures" is z = 1.96, a 95% interval. The tolerance
#:      here is ONE standard error, so z = 1 and z^2/2 = 0.5.
#:   2. The Jeffreys prior for a multinomial is Dirichlet(1/2, ..., 1/2), which
#:      is 0.5 per category with no reference to a confidence level at all.
#:
#: WHAT THE MEASUREMENT SAYS, exactly, at 60 campaigns per cell over 113 (p, q)
#: points. Two cells with IDENTICAL true rates should be called different about
#: 31.7% of the time by a one-standard-error two-sided test. Unsmoothed, the
#: worst point over that grid reaches 51.0% and 39 of the 113 points exceed
#: 33%. At 0.125 and above nothing exceeds 33% anywhere. 0.5 sits inside the
#: safe region rather than on its edge, and what it costs is confined to cells
#: whose true win rate is under about 2%.
MARGIN_SMOOTHING = 0.5


def smoothed_rates(win: float, loss: float, trials: int,
                   smoothing: float = MARGIN_SMOOTHING) -> tuple[float, float]:
    """Win and loss rates pulled off the 0% and 100% boundaries. Issue #328.

    Both rates in and out are percentages, as `summarise` reports them. The
    third outcome -- a campaign that ran out of days, which is neither a win nor
    a loss -- is smoothed too, which is why the denominator gains three
    pseudo-outcomes rather than two. See `MARGIN_SMOOTHING`.

    This CANNOT push a rate past a boundary it was inside: the result is a
    weighted average of the observed rate and 1/3, so it always moves toward
    the middle and never past it.
    """
    denominator = trials + 3.0 * smoothing
    won = win / 100.0 * trials + smoothing
    lost = loss / 100.0 * trials + smoothing
    return 100.0 * won / denominator, 100.0 * lost / denominator


def margin_noise_between(win_a: float, loss_a: float,
                         win_b: float, loss_b: float,
                         trials: int) -> float:
    """The tolerance for one pair of cells, from the rates they actually got.

    The same derivation as `margin_noise` without the worst-case substitution,
    so this is what separates that pair rather than what would separate the
    hardest possible pair. THIS IS WHAT THE GROUPING USES since issue #328.

    The rates are smoothed first -- see `MARGIN_SMOOTHING` -- because a cell
    that won nothing would otherwise be handed a variance of exactly zero.
    """
    win_a, loss_a = smoothed_rates(win_a, loss_a, trials)
    win_b, loss_b = smoothed_rates(win_b, loss_b, trials)
    variance = margin_variance(win_a, loss_a) + margin_variance(win_b, loss_b)
    return 100.0 * math.sqrt(variance / max(1, trials))


def win_rate_noise_between(win_a: float, loss_a: float,
                           win_b: float, loss_b: float,
                           trials: int) -> float:
    """The WIN RATE tolerance for one pair of cells, from the rates they got.

    Stands to `win_rate_noise` exactly as `margin_noise_between` stands to
    `margin_noise`: the same derivation without the worst-case substitution, so
    it separates that pair rather than the hardest possible pair.

    Issue #5. Section 7 prints a win rate for every preset at every tier, and
    until now attached a tolerance to none of them individually -- only to the
    orderings, which leave presets out. Two rates printed side by side read as a
    comparison whether or not one is offered, and a reader who makes that
    comparison has no way to know what the sample can resolve.

    A cell's win rate is a binomial proportion, so its variance is p(1 - p) and
    the difference of two independent cells needs the sum. The loss rate is
    taken only so `smoothed_rates` can pull p off the 0% and 100% boundaries
    over the same three outcomes the margin path uses; it does not otherwise
    enter the result.

    IT IS BOUNDED BY `win_rate_noise`. Both p(1 - p) terms are largest at
    p = 0.5, where the sum is 0.5 and this returns 100 * sqrt(0.5 / trials),
    which is exactly `win_rate_noise(trials)`. So it can only ever be tighter
    than the bound, never looser, and `sim/tests/test_preset_vs_no_tree.py`
    checks that over a grid of rates.
    """
    win_a, _ = smoothed_rates(win_a, loss_a, trials)
    win_b, _ = smoothed_rates(win_b, loss_b, trials)
    p_a, p_b = win_a / 100.0, win_b / 100.0
    variance = p_a * (1.0 - p_a) + p_b * (1.0 - p_b)
    return 100.0 * math.sqrt(variance / max(1, trials))


def pair_tolerance_from(win: dict[str, float], loss: dict[str, float],
                        trials: int):
    """A `rank_by_score` pair tolerance built from one tier's measured rates.

    Issue #328. Returns a function of two preset names, which is the shape
    `rank_by_score` wants. Written as a factory rather than a closure inside
    the tier loop so that it can be tested on its own.
    """
    def tolerance(name_a: str, name_b: str) -> float:
        return margin_noise_between(win[name_a], loss[name_a],
                                    win[name_b], loss[name_b], trials)
    return tolerance


def rank_by_score(scores: dict[str, float], tolerance: float,
                  exclude=(), pair_tolerance=None) -> list[tuple[str, ...]]:
    """Preset names grouped by score, best group first.

    RENAMED FROM `rank_by_win`, issue #294. The grouping was never specific to
    a win rate -- it takes any dict of name to number -- and section 7 now ranks
    on win minus loss, so a name saying "win" would have been wrong.

    Two presets within `tolerance` of each other go in the same group, so the
    result says only what the sample size can support.

    `pair_tolerance` is optional and takes two names, returning the tolerance
    for that pair. Issue #328. `tolerance` is then a CAP rather than the figure
    used: the pair figure is used where it is smaller, which it always is for
    `margin_noise_between` against `margin_noise`, and the cap stops a future
    caller passing something looser than the worst case can justify.

    `exclude` names presets to leave out of the ranking entirely. Issue #294.
    A preset whose campaigns ran out of days has no win rate to rank: 0% win
    and 0% loss is the absence of a result, not a poor one. Ranked anyway, the
    Architect preset at tier 8 comes out FIRST under win minus loss, ahead of
    the only preset that wins anything. Leaving it out is the fix; putting it
    anywhere in the order is the bug. `warn_about_unresolved_campaigns` names
    the cells and is what the caller passes in here.

    The names are dropped BEFORE grouping, not after. Which presets are present
    decides what the rest are measured against. With a tolerance of 5,
    {a: 10, b: 6, c: 2} is (a, b) then (c), and the same three without a is one
    group (b, c). Filtering the finished groups would give the grouping of a
    set that was never ranked.

    A PRESET JOINS A GROUP ONLY IF IT IS WITHIN TOLERANCE OF EVERY MEMBER, not
    only of the one that opened the group. Issue #328. With a single `tolerance`
    the two rules are the SAME rule: the list is sorted by score, so the opener
    is the furthest member and clearing it clears the rest. They come apart once
    the tolerance depends on the pair, because the widest gap and the widest
    tolerance need not belong to the same pair, and a group whose members are
    separable is a false claim of a tie.

    THE RESULT IS NOT GUARANTEED TO BE A REFINEMENT of what the same scores give
    under the cap alone. A tighter tolerance can split a group, and splitting it
    changes which preset opens the next one, so a later pair can be compared
    that was never compared before. {a: 10, b: 6, c: 2} with a cap of 5 gives
    (a, b) then (c); if the pair (a, b) tolerates only 3 and the pair (b, c)
    tolerates 5, it gives (a) then (b, c). Neither grouping contains the other.
    """
    ranked = {name: value for name, value in scores.items()
              if name not in exclude}

    def between(name_a: str, name_b: str) -> float:
        if pair_tolerance is None:
            return tolerance
        return min(tolerance, pair_tolerance(name_a, name_b))

    groups: list[list[str]] = []
    for name in sorted(ranked, key=lambda n: (-ranked[n], n)):
        if groups and all(abs(ranked[member] - ranked[name])
                          <= between(member, name) for member in groups[-1]):
            groups[-1].append(name)
        else:
            groups.append([name])
    return [tuple(names) for names in groups]


def closest_ranked_pair(scores: dict[str, float],
                        exclude=()) -> tuple[str, str] | None:
    """The two names with the smallest gap between their scores.

    Which pair decides whether the tolerance mattered: every wider gap survives
    a tolerance that this one survives. Returns None when fewer than two names
    are left after `exclude`.
    """
    ranked = sorted((value, name) for name, value in scores.items()
                    if name not in exclude)
    if len(ranked) < 2:
        return None
    best = min(zip(ranked, ranked[1:], strict=False),
               key=lambda pair: pair[1][0] - pair[0][0])
    return best[0][1], best[1][1]


#: Above this share of campaigns ending with no result, a cell's win and loss
#: rates describe the day cap more than they describe the preset, so the sweep
#: says so rather than letting them be read as outcomes. Issue #293.
UNRESOLVED_WARNING_PERCENT = 50.0


def warn_about_unresolved_campaigns(names, stale: dict[str, float],
                                    max_days: int,
                                    escalation_per_100_days: float
                                    ) -> list[str]:
    """Name any preset whose campaigns mostly ended with no result, and return
    those names.

    WHY THIS EXISTS. Issue #293. `engine.Simulation.run` is

        while self.day < self.cfg.max_days and not self.lost and not self.won:

    with no other way out, so a campaign that is neither won nor lost ended
    because it ran out of days. It has NO result. The `stale%` column reads like
    a third outcome beside winning and losing, and it is not one.

    That matters because the table is a ranking. At tier 8 the Architect preset
    scores 0% win, 0% loss and 100% no result, and every ranking metric proposed
    on issue #294 reads that row as a good score: win minus loss puts it FIRST,
    ahead of the only preset that wins anything.

    WHAT WAS MEASURED, 2026-08-05, 100 campaigns per row, Architect at tier 8:

        max_days   win%  loss%  stale%  obj/8    days
            2500      0      0     100    1.4    2500
            5000      0      0     100    2.0    5000
           10000      0     20      80    3.2    9860
           20000      0    100       0    3.5   11375

    Raising the cap resolves them, and it resolves them as losses. The win rate
    never leaves zero.

    BUT THAT EXPERIMENT DOES NOT MEAN WHAT IT LOOKS LIKE, and the reason is why
    this warning exists rather than a larger default day cap. Dungeon power is
    keyed to elapsed days, at `dungeon_power_escalation_per_100_days` = 0.22 in
    `config.py`:

        power_scale = 1.0 + 0.22 * (day / 100)

    So the day cap is not an independent variable. At 2500 days dungeons reach
    6.5 times base power; at the 11,375 mean days those runs reached, 26 times.
    Giving a campaign more days also gives it a harder game, so the measurement
    cannot separate "this preset is too weak to win" from "the difficulty
    treadmill outruns any preset given long enough". Both are consistent with
    every number above.

    What is established is narrower and is what this function reports: a cell
    with most campaigns unresolved has no comparable win or loss rate, and the
    reader has to be told which cells those are.
    """
    unresolved = [name for name in names
                  if stale.get(name, 0.0) >= UNRESOLVED_WARNING_PERCENT]
    if not unresolved:
        return []
    print(f"\n  NO RESULT for most campaigns of "
          f"{len(unresolved)} preset(s), at the {max_days:,} day cap:")
    for name in unresolved:
        print(f"    {name}: {stale[name]:.0f}% of campaigns ran out of days "
              "without winning or losing")
    print("  Those campaigns have no outcome, so this preset's win and loss "
          "rates are not")
    print("  comparable with the others in this table. Raising the cap "
          "resolves them as")
    print("  losses, but it also raises dungeon power, which is keyed to "
          "elapsed days at")
    print(f"  {escalation_per_100_days:.0%} per 100 days, which is "
          f"{1.0 + escalation_per_100_days * max_days / 100.0:.1f}x base power "
          "by the cap, so a longer run is")
    print("  a harder run and the cap is not an independent variable. "
          "Issue #293.")
    return unresolved


#: The preset every other preset is measured against. A preset earns the points
#: it costs only if it beats allocating none, so this row is the baseline for
#: the whole table. Taken from the preset object rather than written out, so
#: renaming the preset cannot leave this pointing at a name that is gone.
BASELINE_PRESET = TREE_NONE.name



#: NOT `surge_dungeon_count`. Issue #1297 made it the section's second axis
#: beside the difficulty tier, so the section chooses it rather than inheriting
#: it, and each block names the size it ran at in its own heading. Listing it
#: here would say it was the same in every table below, which it is not.
#:
#: The fields section 7 does not choose, in the order they are printed. Each is
#: (attribute on TuningConfig, label, how to format it). Read off the config the
#: section was handed rather than written out, so the report cannot claim a
#: setting it did not run under -- which is the defect issue #1290 records in the
#: day-cap warning a few lines below this.
INHERITED_SETTINGS = (
    ("surge_mode", "how surges escalate", "{}"),
    ("resolve_floor_ratio", "resolve timer days per floor", "{:.1f}"),
    ("surge_interval_days", "days between surges", "{:.0f}"),
    ("dungeon_power_escalation_per_100_days",
     "dungeon power added per 100 days", "{:.2f}"),
    ("craft_days", "days per craft", "{:.0f}"),
    ("craft_power_gain_frac", "tier width gained per craft", "{:.2f}"),
)


def inherited_settings(cfg: TuningConfig) -> list[tuple[str, str]]:
    """(label, value) for every setting section 7 takes from another section.

    Issue #1287. Sections 0 and 2 choose these by maximising `health`, which
    scores a NO-TREE player against a 55% win rate and 45% triage target. Nothing
    in that choice is aimed at the empire tree, and section 7 does not re-derive
    them per tier or per preset -- so the preset ordering it prints is a
    statement about one world, and the report never said which.

    THE ORDERING IS NOT STABLE ACROSS THEM. Measured 2026-09-05 at tier 1, the
    triage policy, 150 campaigns per cell, everything held at the config this
    section receives except the surge size:

        dungeons per surge   4     5     6     7
        No tree win%        44    52    15    11
        Architect win%      45    52    53    48
        verdict vs no tree  tied  tied  BETTER  BETTER

    Four surge sizes gave FOUR DIFFERENT ORDERINGS. At 4 no preset beat no tree;
    at 5 one did; at 6 and 7 four of five did and the no-tree row fell to second
    from last. `exp_calibrate` tries 5, 6 and 7 and picks 5, robustly -- its top
    three cells all choose 5 and the best cell choosing 6 is 34 health points
    behind -- so this is not a coin flip that a rerun would land differently.
    It is a real dependency on a number chosen for an unrelated reason.

    `TuningConfig.surge_dungeon_count` defaults to 4, which calibration never
    tries. Anyone calling `exp_presets` with a raw `TuningConfig` -- which the
    reproduction snippet on issue #293 does -- measures the count 4 column and
    gets a different answer from the report's.

    NO CONSTANT WAS CHANGED TO MAKE THIS STABLE, and none should be. The
    dependency is a property of the model, not a defect in it: a defensive branch
    is worth little when surges are small enough that an undefended empire
    survives anyway, and worth a great deal when they are not. What was wrong is
    that the report asserted a preset ordering without saying which world it held
    in. Issue #5 is the cost of that.
    """
    out = []
    for field, label, form in INHERITED_SETTINGS:
        value = getattr(cfg, field)
        # An enum formats as "SurgeMode.STATIC" otherwise, which names the
        # Python type rather than the setting.
        out.append((label, form.format(getattr(value, "value", value))))
    return out


def print_inherited_settings(cfg: TuningConfig) -> None:
    """Print what section 7 inherited, and that its conclusions are conditional
    on it. See `inherited_settings`."""
    print("\n  SETTINGS THIS SECTION DID NOT CHOOSE, taken from sections 0 "
          "and 2 and held")
    print("  the same in every table below. Issue #1287.")
    for label, value in inherited_settings(cfg):
        print(f"    {label:<38}{value:>8}")
    print("\n  THE PRESET ORDERING IS CONDITIONAL ON THESE. Sections 0 and 2 "
          "pick them by")
    print("  scoring a NO-TREE player against a 55% win rate and 45% triage "
          "target, which is")
    print("  not a question about the empire tree, and they are not re-derived "
          "per tier or")
    print("  per preset. See inherited_settings.")
    print("\n  THE SURGE SIZE USED TO BE ON THAT LIST AND IS NOW SWEPT, "
          "because the ordering")
    print("  moves more with it than with the tier. Measured 2026-09-05 at 150 "
          "campaigns per")
    print("  cell, moving only that number:")
    print("    dungeons per surge            4     5     6     7")
    print("    tier 1, no tree win%         44    52    15    11")
    print("    tier 1, presets beating it    0     1     4     4")
    print("    tier 8, presets beating it    1     1     2     2")
    print("  The ordering differed from the calibrated 5 at every other value, "
          "at both tiers,")
    print("  so the section runs a second size rather than warning about it. "
          "Issue #1297.")

def compare_against_no_tree(wins: dict[str, float], losses: dict[str, float],
                            stale: dict[str, float], trials: int,
                            baseline: str = BASELINE_PRESET):
    """Every preset's win rate against the no-tree row, with a pair tolerance.

    Returns {preset name: (gap in points, tolerance in points, verdict)}, where
    verdict is "BETTER", "WORSE", "cannot be told apart", or "no result" for a
    preset whose campaigns mostly ran out of days at this tier and so has no win
    rate worth comparing. The baseline itself is not in the result.

    WHY THIS EXISTS. Issue #5, which reported that maxing the Architect quadrant
    "produces the same win rate as allocating no passive points at all", quoting
    52% against 52% from this section at 150 campaigns per cell.

    BOTH FIGURES WERE RIGHT AND THE CONCLUSION WAS NOT. `win_rate_noise(150)` is
    5.77 points, so a nil gap at that sample size is not a measurement of
    equality -- it is the sample failing to resolve whatever the gap is. Re-run
    at 1,000 campaigns per cell over two disjoint blocks of seeds, tier 1,
    triage policy, the same calibrated config this section receives:

        preset                        seeds 0-999   seeds 1000-1999   both
        No tree                              45.8              47.5   46.6
        Architect maxed (as designed)        55.0              57.5   56.2

    The Architect preset beats no tree by 9.6 points, replicated. At 150
    campaigns that was invisible, and the no-tree cell happened to land 5.4
    points above its own long-run value, which closed the gap to nothing.

    THESE FIGURES MOVED AND CAME BACK, and the round trip is worth more than the
    number. Issue #1282 added the Corrupted Stalker to `modifiers.py` AND to the
    pool the draw reads, taking the pool a single Cataclysm draws from to 15 and
    the expected modifier score of a draw from 10.71 to 11.33. Every campaign
    was re-rolled and these cells read 46.0 and 53.1, a gap of 7.1. The project
    owner then ruled the Corrupted Stalker is granted separately rather than
    drawn, issue #1303 took it back out of the pool, and the cells returned to
    46.6 and 57.0 -- EXACTLY, block by block, the same seeds giving the same
    campaigns. The row is still in the table; it is no longer drawn.

    That is the evidence that `modifiers.pool_for` is the only route by which a
    modifier reaches a dungeon. A figure that moves and comes back tells you
    where the control is; a figure that only moves does not.

    THOSE THREE FIGURES ARE HISTORY AND THE TABLE ABOVE IS NOT. They were all
    measured at `city_damage_mult` = 0.023, which issue #1288 then corrected to
    0.0766 because the old value carried a factor matching no node in the tree.
    That is why the Architect cell reads 56.2 above and 57.0 in the paragraph --
    a different change, to a different constant, not a contradiction.

    WHY THE ORDERINGS DID NOT CATCH IT. They carry a tolerance and they group
    ties honestly, but `rank_by_score` is given an `exclude` set holding every
    preset that failed to resolve at ANY tier, so that the orderings compare the
    same presets at each one -- issue #294. The Architect preset has no result in
    94% of its tier 8 campaigns, so it is excluded at tier 8 AND at tier 1, where
    it resolved in 91% of them. It therefore appears in no ordering at any tier,
    while its win rate is still printed in the table above, next to the no-tree
    row, where a reader will compare the two. This function is that comparison,
    made for every preset the table prints rather than only the ranked ones.
    """
    out: dict[str, tuple[float, float, str]] = {}
    for name in wins:
        if name == baseline:
            continue
        if stale.get(name, 0.0) >= UNRESOLVED_WARNING_PERCENT:
            out[name] = (0.0, 0.0, "no result")
            continue
        gap = wins[name] - wins[baseline]
        tolerance = win_rate_noise_between(
            wins[name], losses[name],
            wins[baseline], losses[baseline], trials)
        if abs(gap) <= tolerance:
            verdict = "cannot be told apart"
        else:
            verdict = "BETTER" if gap > 0 else "WORSE"
        out[name] = (gap, tolerance, verdict)
    return out


def print_comparison_against_no_tree(comparison, trials: int,
                                     stale: dict[str, float],
                                     baseline: str = BASELINE_PRESET) -> None:
    """Print what `compare_against_no_tree` measured, worst gap last."""
    print(f"\n  AGAINST THE {baseline!r} ROW, on win rate, at this tier. "
          "Issue #5.")
    print("  A preset is worth its points only if it beats allocating none, so "
          "this is the")
    print("  comparison issues #4 and #5 are both about. The report used to "
          "print these two")
    print("  win rates side by side in the table above and attach a tolerance "
          "to neither.")
    order = sorted(comparison.items(), key=lambda kv: -kv[1][0])
    for name, (gap, tolerance, verdict) in order:
        if verdict == "no result":
            print(f"    {name:<42}no result in "
                  f"{stale.get(name, 0.0):.0f}% of campaigns at this tier, "
                  "so no win rate to compare")
            continue
        print(f"    {name:<42}{gap:>+7.1f} points, needs {tolerance:>4.1f} "
              f" {verdict}")
    print("\n  'CANNOT BE TOLD APART' IS NOT 'NO DIFFERENCE'. It means this "
          "sample cannot")
    print("  resolve the gap. The smallest gap this section can "
          "resolve at all is")
    print(f"  {win_rate_noise(trials):.1f} points, at {trials} campaigns per cell, and a per-pair "
          "tolerance is")
    print("  tighter than that only where a cell's rate sits away from "
          "50%. Reading a nil")
    print("  gap here as evidence that a branch does nothing is what "
          "produced issue #5, and")
    print("  measured at 1,000 campaigns the gap it called zero is 9.6 "
          "points. See")
    print("  compare_against_no_tree.")


def preset_tables(base: TuningConfig, tiers, trials: int):
    """Every table and ordering for ONE surge size. Issue #1297.

    Split out of `exp_presets` so the section can run the whole block once
    per surge size the way it already runs it once per difficulty tier.
    Prints as it goes and returns what it measured, so the caller can
    compare one surge size's orderings against another's without a person
    reading two tables side by side.
    """
    wins: dict[int, dict[str, float]] = {}
    #: Win rate minus loss rate, in percentage points. THE METRIC THE ORDERING
    #: USES since issue #294, because win rate collapses towards zero above tier
    #: 3 and stops telling the presets apart. A campaign scores +1 won, -1 lost
    #: and 0 for no result, so this is the mean of that score, and `margin_noise`
    #: derives its tolerance -- which is not the win rate's.
    margins: dict[int, dict[str, float]] = {tier: {} for tier in tiers}
    #: Loss rates, kept so the tolerance for a pair can be computed from the
    #: rates those two cells actually got rather than from the worst case.
    losses: dict[int, dict[str, float]] = {tier: {} for tier in tiers}
    #: Mean quest objectives cleared, out of `quest_objectives_required`. Kept
    #: alongside the win rate because a win requires all of them, so this is the
    #: same axis measured before it saturates. Issue #294.
    objectives: dict[int, dict[str, float]] = {tier: {} for tier in tiers}
    #: Percentage of campaigns that ended with no result. Issue #293.
    stale: dict[int, dict[str, float]] = {tier: {} for tier in tiers}
    #: Presets whose campaigns mostly ran out of days, per tier. These are kept
    #: out of the ranking below rather than placed in it. Issue #294.
    unresolved: dict[int, list[str]] = {}
    #: Each preset's win rate against the no-tree row, per tier, with the
    #: tolerance for that pair. Issue #5. Returned so a caller can check the
    #: comparison rather than read it out of the printed report.
    comparisons: dict[int, dict[str, tuple[float, float, str]]] = {}
    for tier in tiers:
        ceiling = scoring.tier_bounds(tier)[1]
        print(f"\n  TIER {tier} -- player power ceiling {ceiling:,.0f}, "
              f"day cap {base.max_days:,}")
        header = (f"{'preset':<42}{'win%':>7}{'loss%':>7}{'w-l':>7}"
                  f"{'stale%':>8}"
                  f"{'obj/' + str(base.quest_objectives_required):>7}"
                  f"{'cities':>8}{'floors':>9}{'crafts':>8}{'triage%':>9}")
        print(header)
        print("-" * len(header))
        wins[tier] = {}
        for tree in PRESETS:
            cfg = replace(base, tier=tier).with_tree(tree)
            s = summarise(batch(cfg, policies.triage, trials=trials))
            wins[tier][tree.name] = s["win"]
            losses[tier][tree.name] = s["lost"]
            margins[tier][tree.name] = s["win"] - s["lost"]
            objectives[tier][tree.name] = s["obj"]
            stale[tier][tree.name] = s["stale"]
            print(f"{tree.name:<42}{s['win']:>7.0f}{s['lost']:>7.0f}"
                  f"{s['win'] - s['lost']:>7.0f}"
                  f"{s['stale']:>8.0f}{s['obj']:>7.1f}{s['cities']:>8.1f}"
                  f"{s['floors']:>9.0f}{s['crafts']:>8.1f}{s['triage']:>9.1f}")
        unresolved[tier] = warn_about_unresolved_campaigns(
            wins[tier].keys(), stale[tier], base.max_days,
            base.dungeon_power_escalation_per_100_days)
        comparisons[tier] = compare_against_no_tree(
            wins[tier], losses[tier], stale[tier], trials)
        print_comparison_against_no_tree(
            comparisons[tier], trials, stale[tier])

    if len(tiers) > 1:
        tolerance = margin_noise(trials)
        win_tolerance = win_rate_noise(trials)
        #: {preset name: the tiers where its campaigns mostly had no result}.
        #: A preset is kept out of EVERY ordering if it failed to resolve at ANY
        #: tier, so the orderings printed below cover one set of presets and can
        #: be read against each other. Ranking each tier over whatever resolved
        #: there would compare a five-preset order against a six-preset one, and
        #: the grouping is not a projection -- removing the preset that opened a
        #: tie group changes what the rest are compared against -- so the two
        #: would not be the same measurement.
        no_result_at: dict[str, list[int]] = {}
        for tier in tiers:
            for name in unresolved[tier]:
                no_result_at.setdefault(name, []).append(tier)

        print("\n  PRESET ORDER BY WIN RATE MINUS LOSS RATE, BEST FIRST. "
              "Presets the sample")
        print(f"  cannot separate are shown tied, because {trials} campaigns "
              "per cell is what")
        print("  there is.")
        print("\n  THE TOLERANCE IS COMPUTED PER PAIR, from the rates those "
              "two cells actually")
        print("  got, rather than from the worst case any cell could have. "
              "Issue #328. A cell")
        print("  that loses nearly every campaign varies far less than one "
              "that wins half, so")
        print("  one figure for the whole table gave the high tiers about "
              "eight times the")
        print(f"  tolerance they needed. The worst case is still the cap, at "
              f"{tolerance:.1f} points.")
        print("  Rates are smoothed by "
              f"{MARGIN_SMOOTHING} of a campaign per outcome first, or a cell "
              "that won")
        print("  nothing would be handed a tolerance of exactly zero. See "
              "MARGIN_SMOOTHING and")
        print("  sim/analyse_margin_tolerance.py.")
        print("\n  WHY THIS METRIC AND NOT WIN RATE. Issue #294. Win rate "
              "collapses towards zero")
        print("  above tier 3: at tier 8 five of six presets sit between 0 and "
              "2, which is a tie")
        print("  at the floor rather than a tie in the middle, and it orders "
              "nothing. Win minus")
        print("  loss keeps varying there because the loss rate does.")
        print(f"\n  ITS TOLERANCE IS NOT THE WIN RATE'S {win_tolerance:.1f} "
              "POINTS. Win and loss come from")
        print("  the same campaign, which scores +1 won, -1 lost, 0 no result, "
              "so the margin is")
        print("  the mean of one score rather than the difference of two "
              "independent rates. Its")
        print(f"  worst-case spread is exactly twice the win rate's, which is "
              f"the {tolerance:.1f} points")
        print("  this table caps at. See margin_noise.")
        if no_result_at:
            print(f"\n  LEFT OUT OF THE ORDER, no result at the "
                  f"{base.max_days:,} day cap:")
            for name, bad_tiers in no_result_at.items():
                print(f"    {name}, at tier "
                      + ", ".join(str(t) for t in bad_tiers))
            print("  A preset whose campaigns ran out of days has no win rate "
                  "to rank -- 0% win and")
            print("  0% loss is the absence of a result, not a poor one, and "
                  "win minus loss would")
            print("  rank it FIRST. It is left out at every tier, not only "
                  "where it failed to")
            print("  resolve, so the orderings below cover the same presets. "
                  "Issue #294.")
        orders = {}
        bound_orders = {}
        for tier in tiers:
            order = rank_by_score(margins[tier], tolerance,
                                  exclude=no_result_at,
                                  pair_tolerance=pair_tolerance_from(
                                      wins[tier], losses[tier], trials))
            orders[tier] = order
            bound_orders[tier] = rank_by_score(margins[tier], tolerance,
                                               exclude=no_result_at)
            print(f"    tier {tier}: "
                  + (" > ".join(" = ".join(group) for group in order)
                     if order else "NO PRESET RANKED"))

        print("\n  HOW MUCH THE PER-PAIR TOLERANCE BUYS, for the closest pair "
              "actually measured")
        print("  at each tier. The cap assumes a cell that wins half its "
              "campaigns and loses")
        print("  the other half; a cell that loses nearly all of them varies "
              "far less.")
        print("\n  THIS IS THE HARDEST PAIR AT EACH TIER, so read it as a "
              "lower bound on what")
        print("  changed rather than as the change. Every wider gap in the "
              "table is helped")
        print("  more. The verdicts are for that pair TAKEN ON ITS OWN: a "
              "preset joins a")
        print("  group only if it is within tolerance of every member, so a "
              "preset can sit")
        print("  outside a group while still being within tolerance of one "
              "preset inside it.")
        for tier in tiers:
            pair = closest_ranked_pair(margins[tier], exclude=no_result_at)
            if pair is None:
                print(f"    tier {tier}: fewer than two presets ranked")
                continue
            first, second = pair
            observed = margin_noise_between(
                wins[tier][first], losses[tier][first],
                wins[tier][second], losses[tier][second], trials)
            gap = abs(margins[tier][first] - margins[tier][second])
            used = min(tolerance, observed)
            verdict = "APART" if gap > used else "tied"
            was = "APART" if gap > tolerance else "tied"
            print(f"    tier {tier}: closest gap {gap:>5.1f} points, "
                  f"tolerance for that pair {observed:>5.1f} -> {verdict:<5} "
                  f"cap {tolerance:>5.1f} -> {was}")
        print("\n  THE PAIR ISSUE #328 WAS OPENED ABOUT IS STILL REPORTED "
              "TIED. It quoted 2.3")
        print("  points for the closest pair at tiers 6 and 7 against a gap "
              "of 3.3. That 2.3")
        print("  was the UNSMOOTHED estimate, which is the one that gives a "
              "cell winning")
        print("  nothing a tolerance of exactly zero and cannot be used. "
              "Smoothed, that pair")
        print("  gets about 4.3, and 3.3 does not clear it. What the change "
              "buys is the")
        print("  wider pairs, which the block below shows.")

        print("\n  THE SAME ORDER UNDER THE CAP ALONE, which is what this "
              "report printed before")
        print("  issue #328. Kept so the change is visible here rather than "
              "only in the issue.")
        for tier in tiers:
            order = bound_orders[tier]
            print(f"    tier {tier}: "
                  + (" > ".join(" = ".join(group) for group in order)
                     if order else "NO PRESET RANKED"))
        moved = [t for t in tiers if orders[t] != bound_orders[t]]
        if moved:
            print("  The per-pair tolerance CHANGES the reported order at tier "
                  + ", ".join(str(t) for t in moved) + ".")
        else:
            print("  The per-pair tolerance changes no reported order at any "
                  "tier measured, so")
            print("  nothing on issues #4 and #5 turns on it at this sample "
                  "size.")

        print(f"\n  PRESET ORDER BY WIN RATE ALONE, as a second opinion, "
              f"tolerance {win_tolerance:.1f} points.")
        print("  This is the metric issue #294 was opened about. Kept so the "
              "collapse is")
        print("  visible in the report rather than only in the issue.")
        for tier in tiers:
            order = rank_by_score(wins[tier], win_tolerance,
                                  exclude=no_result_at)
            print(f"    tier {tier}: "
                  + (" > ".join(" = ".join(group) for group in order)
                     if order else "NO PRESET RANKED"))

        print(f"\n  QUEST OBJECTIVES CLEARED, out of "
              f"{base.quest_objectives_required}, as a second opinion. Issue "
              "#294 asked whether")
        print("  this separates the presets where win rate does not, because a "
              "win requires")
        print("  all of them and this is the same axis measured before it "
              "saturates.")
        for tier in tiers:
            values = sorted(objectives[tier].values())
            print(f"    tier {tier}: {values[-1] - values[0]:>4.1f} spread over "
                  + ", ".join(f"{v:.1f}" for v in values))
        print("\n  MEASURED 2026-08-05: IT DOES NOT. It saturates at the low "
              "tiers, where five")
        print("  of six presets sit within 0.1 of each other, and at the high "
              "tiers it")
        print("  collapses the same way win rate does. The one preset it "
              "separates is the")
        print("  one win rate already separates. Win minus loss is the metric "
              "issue #294")
        print("  settled on instead; this column is kept because it explains "
              "the collapse.")

        flat = [t for t, order in orders.items() if len(order) == 1]
        if not any(orders.values()):
            # Every preset was left out. Each order is [], and [] == [] would
            # otherwise print "the ordering is THE SAME at every tier" -- an
            # agreement between two rankings of nothing. Issue #294.
            print("\n  NO RANKING AT ALL. Every preset ran out of days at one "
                  "or more of the tiers")
            print("  compared, so no preset has an outcome that can be "
                  "ordered. Issue #294.")
        elif flat:
            print("\n  NO CONCLUSION. Every preset ties at tier "
                  + ", ".join(str(t) for t in flat) + ", so there is no ordering")
            print("  to compare. Win minus loss is the metric issue #294 chose "
                  "because it still")
            print("  varies where win rate does not; a tie under it as well "
                  "means the presets")
            print("  really are within the sample's reach of each other at "
                  "that tier.")
        elif len(set(tuple(o) for o in orders.values())) == 1:
            print("\n  The ordering is THE SAME at every tier measured, so the")
            print("  tier 1 conclusions on issues #4 and #5 probably generalise.")
        else:
            print("\n  The ordering DIFFERS between tiers, so the tier 1")
            print("  conclusions on issues #4 and #5 do not generalise. That is a")
            print("  finding in its own right. Issue #281.")
    return {
        "wins": wins,
        "losses": losses,
        "margins": margins,
        "stale": stale,
        "unresolved": unresolved,
        "comparisons": comparisons,
    }


def compare_surge_sizes(measured, tiers, trials: int) -> None:
    """Whether the preset ordering survives a change of surge size.

    Issue #1297. The section runs the whole tier block once per surge size; this
    says whether the two agree, which is the question that made a second size
    worth its runtime. It is the surge-size twin of the "ordering is THE SAME at
    every tier" line `preset_tables` prints.

    A preset left out of an ordering at one size is left out at both, for the
    reason issue #294 gives about tiers: two orderings over different sets of
    presets are not the same measurement and cannot be compared.
    """
    sizes = list(measured)
    if len(sizes) < 2:
        return

    print(f"\n{'=' * 100}")
    print("  DOES THE ORDERING SURVIVE A CHANGE OF SURGE SIZE? Issue #1297.")
    print(f"{'=' * 100}")
    print("  This is the question the second block above is paid for. The "
          "surge size is chosen")
    print("  by section 0, scoring a NO-TREE player against a win rate and "
          "triage target, which")
    print("  is not a question about the empire tree -- so an ordering that "
          "only holds at the")
    print("  size section 0 happened to pick is not a finding about the tree. "
          "Issue #1287.")

    #: Left out everywhere if it failed to resolve anywhere, at any size or
    #: tier. Issue #294's rule, applied across both axes.
    excluded: dict[str, list[int]] = {}
    for size in sizes:
        for tier in tiers:
            for name in measured[size]["unresolved"].get(tier, []):
                excluded.setdefault(name, []).append(tier)
    if excluded:
        print("\n  LEFT OUT OF EVERY ORDERING BELOW, no result at some size "
              "and tier:")
        for name in sorted(excluded):
            print(f"    {name}")

    tolerance = margin_noise(trials)
    orders: dict[tuple[int, int], tuple] = {}
    for size in sizes:
        for tier in tiers:
            data = measured[size]
            order = rank_by_score(
                data["margins"][tier], tolerance, exclude=excluded,
                pair_tolerance=pair_tolerance_from(
                    data["wins"][tier], data["losses"][tier], trials))
            orders[(size, tier)] = tuple(tuple(g) for g in order)

    for tier in tiers:
        print(f"\n  TIER {tier}")
        for size in sizes:
            order = orders[(size, tier)]
            print(f"    {size} per surge: "
                  + (" > ".join(" = ".join(group) for group in order)
                     if order else "NO PRESET RANKED"))
        distinct = {orders[(size, tier)] for size in sizes}
        if len(distinct) == 1:
            print("    SAME at both surge sizes.")
        else:
            print("    DIFFERS between surge sizes, so an ordering read off "
                  "one of them is a")
            print("    statement about that surge size and not about the "
                  "empire tree.")

    agree = [tier for tier in tiers
             if len({orders[(size, tier)] for size in sizes}) == 1]
    print()
    if len(agree) == len(tiers):
        print("  The ordering holds at every surge size measured, at every "
              "tier, so it is a")
        print("  statement about the empire tree rather than about the surge "
              "settings.")
    elif not agree:
        print("  The ordering holds at NO tier once the surge size moves. "
              "Nothing in the tables")
        print("  above orders the presets independently of a number section 0 "
              "chose for an")
        print("  unrelated reason. Issues #1287 and #1297.")
    else:
        print("  The ordering holds at tier "
              + ", ".join(str(t) for t in agree)
              + " and not at the others, so it generalises at some")
        print("  tiers and not at all of them.")


def exp_presets(base: TuningConfig, tiers=PRESET_TIERS, trials: int = 150,
                surge_sizes=None):
    """The preset comparison, run once per surge size and once per tier.

    Returns {tier: {preset name: win rate}} FOR THE CALIBRATED SURGE SIZE,
    which is what it returned before issue #1297 added the second one. The
    other block is printed and compared but not returned; a caller wanting
    it should call `preset_tables` directly.
    """
    sizes = tuple(surge_sizes) if surge_sizes is not None else (
        base.surge_dungeon_count, PRESET_SECOND_SURGE_SIZE)
    #: Duplicates would print the same block twice and compare it with
    #: itself, which reads as agreement. Order is kept: the calibrated size
    #: is first and is the one returned.
    sizes = tuple(dict.fromkeys(sizes))

    rule(f"7. EMPIRE TREE PRESETS -- head to head, at tier "
         f"{' and tier '.join(str(t) for t in tiers)}"
         f" and at {' and '.join(str(s) for s in sizes)} dungeons per surge")
    print("  Everything else is held at the values sections 0 and 2 "
          "calibrated at tier "
          f"{SWEEP_TIER},")
    print("  which are named below. The tier and the surge size are the "
          "two axes this section")
    print("  sweeps; neither is inherited. Issues #281 and #1297.")
    print_inherited_settings(base)

    measured = {}
    for size in sizes:
        print(f"\n{'=' * 100}")
        print(f"  {size} DUNGEONS PER SURGE"
              + ("   <- the value sections 0 and 2 calibrated"
                 if size == base.surge_dungeon_count else ""))
        print(f"{'=' * 100}")
        measured[size] = preset_tables(
            replace(base, surge_dungeon_count=size), tiers, trials)

    compare_surge_sizes(measured, tiers, trials)
    return measured[sizes[0]]["wins"]




def exp_escalation(base: TuningConfig, mode: SurgeMode):
    rule(f"8. CATACLYSM STACKING -- under {mode.value} surges")
    print(f"{'cataclysms':<12}{'no tree win%':>14}{'fixed tree win%':>17}"
          f"{'days':>8}{'power':>8}{'triage%':>10}{'cities':>8}")
    print("-" * 100)
    for n in (1, 2, 3, 4, 6, 8):
        c0 = replace(base, active_cataclysms=n, surge_mode=mode).with_tree(TREE_NONE)
        c1 = replace(base, active_cataclysms=n, surge_mode=mode).with_tree(TREE_PROPOSED_FIX)
        a = summarise(batch(c0, policies.triage, trials=120))
        b = summarise(batch(c1, policies.triage, trials=120))
        print(f"{n:<12}{a['win']:>14.0f}{b['win']:>17.0f}{b['days']:>8.0f}"
              f"{b['power']:>8.0f}{b['triage']:>10.1f}{b['cities']:>8.1f}")


def main():
    for line in header_lines():
        print(line)

    base = exp_calibrate()
    exp_policies(base)
    base = exp_forge(base)
    print("\n  (all experiments below use the forge/treadmill settings above)")
    exp_policies(base)
    exp_surge_modes(base)
    exp_death_spiral(base)
    exp_explorer(base)
    exp_days_vs_floors(base)
    exp_presets(base)
    exp_escalation(base, SurgeMode.ACCELERATING)

    print(f"\n{'=' * 100}\nDone.\n{'=' * 100}")


if __name__ == "__main__":
    main()
