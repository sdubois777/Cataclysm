"""How fast each difficulty mode fills its empire upgrade tree.

WHY THIS EXISTS. Issue #289. The empire upgrade tree is partitioned by lethality
mode (issue #277), so a player's first Heretic character starts with an empty
Heretic tree however much Standard progress the account has. Two of Heretic's
rules push in opposite directions on how bad that cold start is:

    Surges spawn 25% more dungeons     -- more dungeons defeated, so the tree
                                          fills faster
    Cities have 2 upgrade slots not 3  -- several empire tree nodes are worth
                                          less

The issue asks whether the first over-compensates for the second, which would
make the hardest mode the fastest place to grow an empire tree.

WHAT THIS MEASURES, AND WHAT IT DOES NOT. It measures the first. It cannot
measure the second, because this simulation has no city upgrade system at all:
`world.City.upgrades` is a field that nothing sets and nothing reads. That is
issue #318.

SO THIS IS ONE-SIDED BY CONSTRUCTION, counting only the effect that helps
Heretic. It is still worth running, because it bounds the answer. If Heretic does
not fill faster even with only the favourable effect counted, the question is
settled without building the missing half.

Two other Heretic and Hardcore rules have no model here either, and both would
cost the harder modes rather than help them: equipment lost on death (player
power is one scalar with no per-item model) and the hidden heads-up display (the
policies see the true state). `LethalityRules` in `cataclysm_sim/config.py` says
so at the point where the numbers live.

WHAT IS HELD CONSTANT. Every mode runs the same empire tree -- none at all,
because the cold start is the situation the issue is about -- the same policy,
the same difficulty tier, and the same random seeds. The simulation accumulates
empire points and never spends them, so a faster fill cannot feed back into a
stronger campaign. That makes the comparison a clean rate measurement and not a
compounding one.
"""

from __future__ import annotations

import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import (
    LETHALITY_RULES, TREE_NONE, LethalityMode, TuningConfig,
)
from cataclysm_sim.engine import Simulation

#: Campaigns per row. Seeded from 0 upward, so this is reproducible rather than
#: noisy.
#:
#: 25 is a compromise: `sim/tests/test_analysis_scripts.py` runs this file and
#: the other four analysis scripts together take under a tenth of a second, so a
#: long run here would be the whole cost of that test file. THE CONCLUSION DOES
#: NOT DEPEND ON IT. Measured 2026-08-05 at five sample sizes:
#:
#:     trials    heretic    surges only
#:         15      0.98x          1.02x
#:         20      0.99x          1.04x
#:         25      1.01x          1.06x
#:         40      0.99x          1.09x
#:         80      1.00x          1.09x
#:
#: The surge multiplier is 1.25 and no row comes near it at any sample size.
TRIALS = 25

#: The difficulty tier every row runs at. Tier 1 because a first character in a
#: new lethality mode starts at tier 1 with an empty tree -- which is the exact
#: situation issue #289 is about. Issue #281 is why this is stated rather than
#: left to the default.
TIER = 1

MODES = (LethalityMode.STANDARD, LethalityMode.HARDCORE, LethalityMode.HERETIC)

HERETIC_RULES = LETHALITY_RULES[LethalityMode.HERETIC]
STANDARD_RULES = LETHALITY_RULES[LethalityMode.STANDARD]


def mean(xs) -> float:
    return statistics.fmean(xs) if xs else 0.0


def base_config() -> TuningConfig:
    return replace(TuningConfig(), tier=TIER).with_tree(TREE_NONE)


def run_config(cfg: TuningConfig) -> dict:
    """`TRIALS` campaigns under one config, summarised."""
    results = [Simulation(cfg, seed=i).run(policies.triage) for i in range(TRIALS)]
    return {
        "points": mean([r.empire_points for r in results]),
        "days": mean([r.survived_days for r in results]),
        # The headline. Points banked per day of campaign, which is the rate the
        # tree fills at. Computed per campaign and then averaged, NOT as mean
        # points over mean days -- a campaign that ended early would otherwise
        # be weighted by its length rather than counted once.
        "rate": mean([r.empire_points / max(1, r.survived_days) for r in results]),
        "win": 100.0 * mean([1.0 if r.won else 0.0 for r in results]),
        "lost": 100.0 * mean([1.0 if r.lost else 0.0 for r in results]),
        "cleared": mean([r.dungeons_cleared for r in results]),
        # Dungeons that reached the end of their timer undefeated. The mechanism
        # that decides whether more dungeons means more points or only more
        # damage, so it is printed beside the fill rate rather than inferred.
        "resolved": mean([r.dungeons_resolved for r in results]),
        # THE SHARE, WHICH IS THE QUANTITY THE PARAGRAPH BELOW ACTUALLY CLAIMS.
        # It used to print the two absolute counts instead, which supported the
        # claim only while campaigns in the two rows ran for similar lengths.
        # Once issue #1329 added Siege damage they do not: more dungeons means
        # more Sieges, the empire falls sooner, and a campaign that ends sooner
        # has fewer dungeons resolve in total while a LARGER share of them do.
        # Computed per campaign and then averaged, for the same reason `rate`
        # is.
        "share_undefeated": 100.0 * mean(
            [r.dungeons_resolved / max(1, r.dungeons_resolved
                                       + r.dungeons_cleared)
             for r in results]),
        "deaths": mean([r.deaths for r in results]),
    }


def run_mode(mode: LethalityMode) -> dict:
    """One of the three real modes."""
    return run_config(base_config().with_lethality(mode))


#: The two controls. Heretic changes two things this simulation models, so the
#: three-mode table alone cannot say which of them moved the fill rate. Each of
#: these applies exactly one of Heretic's rules on top of Standard.
CONTROLS = {
    "surges only": lambda: run_config(replace(
        base_config(),
        surge_dungeon_multiplier=HERETIC_RULES.surge_dungeon_multiplier)),
    "death cost only": lambda: run_config(replace(
        base_config(), death_day_cost=HERETIC_RULES.death_day_cost)),
}


#: Every row this report measured, filled in by `main`. Module level so that
#: `sim/tests/test_analysis_scripts.py` can check the printed conclusions against
#: the numbers they were computed from rather than re-implementing the script.
ROWS: dict[str, dict] = {}


def rule(title: str) -> None:
    print(f"\n{'=' * 78}\n{title}\n{'=' * 78}")


def main() -> None:
    rule("EMPIRE TREE FILL RATE BY LETHALITY MODE -- issue #289")
    print(f"  {TRIALS} campaigns per mode, difficulty tier {TIER}, no empire "
          f"tree, triage policy.")
    print("  Same seeds in every row. Empire points are accumulated and never "
          "spent, so")
    print("  a faster fill cannot make the campaign that produced it stronger.")
    print()
    print("  ONLY THE EFFECTS THIS SIMULATION MODELS. Heretic's 2 city upgrade "
          "slots")
    print("  instead of 3 is NOT here -- there is no city upgrade system to "
          "reduce, which")
    print("  is issue #318 -- and that is the effect that would cost Heretic. "
          "So this")
    print("  counts the rule that helps Heretic and not the one that hurts it.")
    print()

    header = (f"{'row':<26}{'death days':>12}{'surge x':>9}{'points':>9}"
              f"{'days':>8}{'pts/day':>10}{'cleared':>9}{'undefeated':>12}"
              f"{'deaths':>8}{'win%':>7}{'loss%':>7}")
    print(header)
    print("-" * len(header))

    def show(label, death_days, surge_x, s):
        print(f"{label:<26}{death_days:>12}{surge_x:>9.2f}{s['points']:>9.0f}"
              f"{s['days']:>8.0f}{s['rate']:>10.4f}{s['cleared']:>9.0f}"
              f"{s['resolved']:>12.0f}{s['deaths']:>8.1f}"
              f"{s['win']:>7.0f}{s['lost']:>7.0f}")

    for mode in MODES:
        rules = LETHALITY_RULES[mode]
        ROWS[mode.value] = run_mode(mode)
        show(mode.value, rules.death_day_cost, rules.surge_dungeon_multiplier,
             ROWS[mode.value])

    controls = {name: run() for name, run in CONTROLS.items()}
    ROWS.update(controls)
    print()
    show("standard + heretic surges", STANDARD_RULES.death_day_cost,
         HERETIC_RULES.surge_dungeon_multiplier, controls["surges only"])
    show("standard + heretic deaths", HERETIC_RULES.death_day_cost,
         STANDARD_RULES.surge_dungeon_multiplier, controls["death cost only"])

    standard = ROWS[LethalityMode.STANDARD.value]
    heretic = ROWS[LethalityMode.HERETIC.value]
    surges_only = controls["surges only"]
    ratio = heretic["rate"] / standard["rate"] if standard["rate"] else 0.0
    surge_ratio = (surges_only["rate"] / standard["rate"]
                   if standard["rate"] else 0.0)

    rule("WHAT IT SHOWS")
    print(f"  Heretic fills its empire tree at {ratio:.2f}x the Standard rate, "
          f"measured in")
    print(f"  empire points per day: {heretic['rate']:.4f} against "
          f"{standard['rate']:.4f}.")
    print()
    print(f"  The 25% extra dungeons ON THEIR OWN give {surge_ratio:.2f}x, from "
          f"the control row.")
    print("  That is the row that answers issue #289, because it is the one "
          "Heretic rule")
    print("  that could over-compensate, with nothing else changed.")
    print()
    print("  If more dungeons simply meant proportionally more points, that "
          "row would read")
    print(f"  {HERETIC_RULES.surge_dungeon_multiplier:.2f}x.")
    if surge_ratio < HERETIC_RULES.surge_dungeon_multiplier:
        print("  THE FILL RATE RISES BY LESS THAN THE DUNGEON COUNT DOES. More "
              "dungeons")
        print("  against an unchanged day budget means a larger share of them "
              "resolve")
        print(f"  undefeated -- {surges_only['share_undefeated']:.0f}% against "
              f"{standard['share_undefeated']:.0f}% -- and a dungeon nobody "
              f"cleared pays nothing.")
        print(f"  The two absolute counts, {surges_only['resolved']:.0f} "
              f"against {standard['resolved']:.0f}, do not settle it on their "
              "own:")
        print(f"  that row's campaigns run {surges_only['days']:.0f} days "
              f"against {standard['days']:.0f}, so there is less")
        print("  time for anything to happen in at all.")
    else:
        print("  THE FILL RATE RISES BY AT LEAST AS MUCH AS THE DUNGEON COUNT "
              "DOES, so the")
        print("  extra dungeons are being cleared rather than resolving "
              "undefeated.")
    print()
    print(f"  Heretic wins {heretic['win']:.0f}% of campaigns against Standard's "
          f"{standard['win']:.0f}%, and")
    print(f"  loses {heretic['lost']:.0f}% against {standard['lost']:.0f}%. The "
          f"extra dungeons are not free.")

    rule("WHAT THIS DOES NOT ANSWER")
    print("  Whether Heretic's tree is WORTH more per point, which is the other "
          "half of")
    print("  issue #289. Several empire tree nodes count cities by upgrade "
          "category and")
    print("  Heretic cities have one fewer slot to put an upgrade in. Nothing "
          "here")
    print("  measures that, because this simulation has no city upgrade system: "
          "the")
    print("  field world.City.upgrades is set by nothing and read by nothing. "
          "Issue #318.")
    print()
    print("  So the number above is an UPPER BOUND on how much Heretic is "
          "over-compensated,")
    print("  not an estimate of it. The unmodelled effects all cost the harder "
          "modes.")
    print()


# Called unconditionally, like the other analyse_*.py scripts, so that importing
# this file IS running the report. `sim/tests/test_analysis_scripts.py` executes
# it with `runpy.run_path`, which does not set `__name__` to `"__main__"`, so a
# guard here would make the whole report invisible to its own tests.
main()
