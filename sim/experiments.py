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


def rank_by_win(wins: dict[str, float], tolerance: float,
                exclude=()) -> list[tuple[str, ...]]:
    """Preset names grouped by win rate, best group first.

    Two presets within `tolerance` of each other go in the same group, so the
    result says only what the sample size can support.

    `exclude` names presets to leave out of the ranking entirely. Issue #294.
    A preset whose campaigns ran out of days has no win rate to rank: 0% win
    and 0% loss is the absence of a result, not a poor one. Ranked anyway, the
    Architect preset at tier 8 comes out FIRST under win minus loss, ahead of
    the only preset that wins anything. Leaving it out is the fix; putting it
    anywhere in the order is the bug. `warn_about_unresolved_campaigns` names
    the cells and is what the caller passes in here.

    The names are dropped BEFORE grouping, not after. Each preset is compared
    with the FIRST member of the group being built, so which presets are
    present decides what the rest are measured against. With a tolerance of 5,
    {a: 10, b: 6, c: 2} is (a, b) then (c), and the same three without a is one
    group (b, c). Filtering the finished groups would give the grouping of a
    set that was never ranked.
    """
    ranked = {name: rate for name, rate in wins.items() if name not in exclude}
    groups: list[list] = []
    for name in sorted(ranked, key=lambda n: (-ranked[n], n)):
        if groups and abs(groups[-1][0] - ranked[name]) <= tolerance:
            groups[-1][1].append(name)
        else:
            groups.append([ranked[name], [name]])
    return [tuple(names) for _, names in groups]


#: Above this share of campaigns ending with no result, a cell's win and loss
#: rates describe the day cap more than they describe the preset, so the sweep
#: says so rather than letting them be read as outcomes. Issue #293.
UNRESOLVED_WARNING_PERCENT = 50.0


def warn_about_unresolved_campaigns(names, stale: dict[str, float],
                                    max_days: int) -> list[str]:
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
    print("  22% per 100 days, so a longer run is a harder run and the cap is "
          "not an")
    print("  independent variable. Issue #293.")
    return unresolved


def exp_presets(base: TuningConfig, tiers=PRESET_TIERS, trials: int = 150):
    """The preset comparison, run once per tier. Returns the win rates it printed.

    Returned as {tier: {preset name: win rate}} so a caller can compare the
    orderings rather than a person having to read two tables side by side.
    """
    rule(f"7. EMPIRE TREE PRESETS -- head to head, at tier "
         f"{' and tier '.join(str(t) for t in tiers)}")
    print("  Only the tier changes between these tables. The surge and forge")
    print("  settings are the ones calibrated at tier "
          f"{SWEEP_TIER} in sections 0 and 2.")

    wins: dict[int, dict[str, float]] = {}
    #: Mean quest objectives cleared, out of `quest_objectives_required`. Kept
    #: alongside the win rate because a win requires all of them, so this is the
    #: same axis measured before it saturates. Issue #294.
    objectives: dict[int, dict[str, float]] = {tier: {} for tier in tiers}
    #: Percentage of campaigns that ended with no result. Issue #293.
    stale: dict[int, dict[str, float]] = {tier: {} for tier in tiers}
    #: Presets whose campaigns mostly ran out of days, per tier. These are kept
    #: out of the ranking below rather than placed in it. Issue #294.
    unresolved: dict[int, list[str]] = {}
    for tier in tiers:
        ceiling = scoring.tier_bounds(tier)[1]
        print(f"\n  TIER {tier} -- player power ceiling {ceiling:,.0f}, "
              f"day cap {base.max_days:,}")
        header = (f"{'preset':<42}{'win%':>7}{'loss%':>7}{'stale%':>8}"
                  f"{'obj/' + str(base.quest_objectives_required):>7}"
                  f"{'cities':>8}{'floors':>9}{'crafts':>8}{'triage%':>9}")
        print(header)
        print("-" * len(header))
        wins[tier] = {}
        for tree in PRESETS:
            cfg = replace(base, tier=tier).with_tree(tree)
            s = summarise(batch(cfg, policies.triage, trials=trials))
            wins[tier][tree.name] = s["win"]
            objectives[tier][tree.name] = s["obj"]
            stale[tier][tree.name] = s["stale"]
            print(f"{tree.name:<42}{s['win']:>7.0f}{s['lost']:>7.0f}"
                  f"{s['stale']:>8.0f}{s['obj']:>7.1f}{s['cities']:>8.1f}"
                  f"{s['floors']:>9.0f}{s['crafts']:>8.1f}{s['triage']:>9.1f}")
        unresolved[tier] = warn_about_unresolved_campaigns(
            wins[tier].keys(), stale[tier], base.max_days)

    if len(tiers) > 1:
        tolerance = win_rate_noise(trials)
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

        print(f"\n  PRESET ORDER BY WIN RATE, BEST FIRST. Presets within "
              f"{tolerance:.1f} points of")
        print(f"  each other are shown tied, because {trials} campaigns per cell "
              "cannot separate them.")
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
        for tier in tiers:
            order = rank_by_win(wins[tier], tolerance, exclude=no_result_at)
            orders[tier] = order
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
        print("  one win rate already separates. Issue #294 stays open.")

        flat = [t for t, order in orders.items() if len(order) == 1]
        if not any(orders.values()):
            # Every preset was left out. Each order is [], and [] == [] would
            # otherwise print "the ordering is THE SAME at every tier" -- an
            # agreement between two rankings of nothing. Issue #294.
            print("\n  NO RANKING AT ALL. Every preset ran out of days at one "
                  "or more of the tiers")
            print("  compared, so no preset has a win rate that can be "
                  "ordered. Issue #294.")
        elif flat:
            print("\n  NO CONCLUSION. Every preset ties at tier "
                  + ", ".join(str(t) for t in flat) + ", so there is no ordering")
            print("  to compare. Win rate stops separating the presets once it "
                  "collapses towards")
            print("  zero. Measuring these tiers needs a metric that still "
                  "varies there.")
        elif len(set(tuple(o) for o in orders.values())) == 1:
            print("\n  The ordering is THE SAME at every tier measured, so the")
            print("  tier 1 conclusions on issues #4 and #5 probably generalise.")
        else:
            print("\n  The ordering DIFFERS between tiers, so the tier 1")
            print("  conclusions on issues #4 and #5 do not generalise. That is a")
            print("  finding in its own right. Issue #281.")
    return wins


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
