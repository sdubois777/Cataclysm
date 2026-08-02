"""Run the tuning experiments and print a report.

    python experiments.py

Ground rules now fixed by design rather than swept:
  * One dungeon floor costs exactly one day.
  * The dungeon you are inside has its timer PAUSED. Everything else ticks.
  * A city falling triggers a surge.
  * Days are also spent at the forge, which defends nothing.
  * Surges escalate; the open question is HOW (see experiment 2).
"""

from __future__ import annotations

import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_ARCHITECT_AS_DESIGNED, TREE_EXPLORER_AS_DESIGNED, TREE_EXPLORER_DEEP,
    TREE_EXPLORER_VIA_FLOORS, TREE_NONE, TREE_PROPOSED_FIX,
    EmpireTree, SurgeMode, TuningConfig,
)
from cataclysm_sim.engine import Simulation

TRIALS = 250


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
    for esc in (0.00, 0.10, 0.22, 0.35, 0.50):
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


def exp_presets(base: TuningConfig):
    rule("7. EMPIRE TREE PRESETS -- head to head")
    print(f"{'preset':<42}{'win%':>7}{'loss%':>7}{'stale%':>8}{'cities':>8}"
          f"{'floors':>9}{'crafts':>8}{'triage%':>9}")
    print("-" * 100)
    for tree in (TREE_NONE, TREE_EXPLORER_AS_DESIGNED, TREE_EXPLORER_VIA_FLOORS,
                 TREE_EXPLORER_DEEP, TREE_ARCHITECT_AS_DESIGNED, TREE_PROPOSED_FIX):
        cfg = base.with_tree(tree)
        s = summarise(batch(cfg, policies.triage, trials=150))
        print(f"{tree.name:<42}{s['win']:>7.0f}{s['lost']:>7.0f}{s['stale']:>8.0f}"
              f"{s['cities']:>8.1f}{s['floors']:>9.0f}{s['crafts']:>8.1f}"
              f"{s['triage']:>9.1f}")


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
    print("CATACLYSM -- empire layer tuning rig (rev 3: paused timers + the forge)")
    print(f"{TRIALS} campaigns per cell, deterministic seeds\n")
    print("Column key:")
    print("  win%       cleared 8 objectives, then the Cataclysm dungeon")
    print("  stale%     neither won nor lost -- hit the day cap")
    print("  floors     total floors cleared -- the loot proxy")
    print("  power      final player Power Score (Cataclysm dungeon needs ~320-420)")
    print("  forge%     share of the run spent at the forge, defending nothing")
    print("  triage%    free days facing 2+ dungeons about to detonate")

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
