"""How much smoothing the per-pair margin tolerance needs, measured.

    cd sim && python analyse_margin_tolerance.py

WHAT THIS IS FOR. Issue #328. Section 7 of `sim/experiments.py` groups empire
tree presets that a sample of campaigns cannot tell apart. Until issue #328 the
tolerance it used was `margin_noise`, a worst-case bound that assumes a cell
winning half its campaigns and losing the other half. Above difficulty tier 5
almost every campaign loses, the true spread is far smaller, and the bound was
about eight times wider than the closest measured pair needed.

The fix is to estimate each pair's tolerance from the rates those two cells
observed, which `margin_noise_between` does. The trap is that a cell observing
0 wins in 60 campaigns gets an estimated variance of exactly zero, so its
tolerance against another such cell is exactly zero and ANY non-zero gap gets
called significant. The rates have to be pulled off the boundary first.

WHAT THIS SCRIPT MEASURES: how much smoothing that takes.

THE TEST IT APPLIES. Take two cells with IDENTICAL true win and loss
probabilities. Any gap between the margins they report is sampling noise by
construction, so a tolerance that calls them different is wrong every time it
does so. A one-standard-error two-sided test allows that about 31.7% of the
time, which is P(|Z| > 1) for a normal. Measured against that target, a
tolerance that is too tight overshoots and one that is too loose undershoots.

HOW IT IS COMPUTED. Exactly, not by sampling. For n campaigns the joint
distribution of (wins, losses) is a trinomial with a few hundred outcomes worth
keeping, so the script enumerates every pair of outcomes and adds up the
probability that the pair is separated. There is no random seed and the answer
does not move between runs.

WHAT IT DOES NOT MEASURE. Whether the presets differ. It measures a statistical
procedure against a known truth, using no game model at all -- the only thing it
imports from the project is the tolerance formula itself.
"""

from __future__ import annotations

import math
from math import comb

from experiments import (
    MARGIN_SMOOTHING, margin_noise, margin_noise_between, margin_variance,
    smoothed_rates,
)

#: Campaigns per cell. The figure section 7 of `sim/experiments.py` is measured
#: at directly when the full sweep is too slow to run; the full sweep uses 150,
#: where every conclusion below holds with more margin rather than less.
TRIALS = 60

#: How often a one-standard-error two-sided test should separate two cells that
#: are in truth identical. P(|Z| > 1) for a standard normal.
TARGET_PERCENT = 31.73

#: Smoothing constants to compare, in pseudo-outcomes per category. 0.0 is no
#: smoothing, which is the version issue #328 says cannot be used. 0.0625 is
#: below the floor and is here to show where the floor is, rather than leaving
#: "0.5 is safe" as an assertion with nothing under it.
CANDIDATES = (0.0, 0.0625, 0.125, 0.25, 0.5, 1.0)

#: (win probability, loss probability) pairs to measure at. The first is the
#: worst case the bound is derived for and the rest walk towards the high-tier
#: regime, where almost every campaign is lost and the unsmoothed estimator
#: fails. (0.04, 0.95) is the point that catches 0.0625; without it the table
#: would show every candidate except 0.0 holding.
GRID = ((0.50, 0.50), (0.30, 0.60), (0.10, 0.80), (0.05, 0.90),
        (0.04, 0.95), (0.02, 0.95), (0.01, 0.98), (0.005, 0.99))

#: Outcomes rarer than this are dropped before the pairwise sum. Every table
#: below prints the probability actually kept so that a dropped tail cannot
#: hide inside a percentage.
NEGLIGIBLE = 1e-9

#: The closest pair of empire tree presets at difficulty tiers 6 and 7, as
#: (win%, loss%) each, measured on 2026-08-06 at 60 campaigns per cell. It is
#: the Explorer maxed preset against Explorer via floors (+30 floors). This is
#: the pair issue #328 was opened about, transcribed here so section D can show
#: what the change does and does not do to it.
TIER_67_PAIR = ((0.0, 96.7), (0.0, 100.0))

#: The gap between their margins, in percentage points. Both win nothing, so it
#: is the difference of the two loss rates above.
TIER_67_GAP = abs((TIER_67_PAIR[0][0] - TIER_67_PAIR[0][1])
                  - (TIER_67_PAIR[1][0] - TIER_67_PAIR[1][1]))


def outcomes(trials: int, win: float, loss: float,
             floor: float = NEGLIGIBLE) -> tuple[list, float]:
    """[(wins, losses, probability)] for one cell, and the mass kept.

    `win` and `loss` are probabilities, not percentages: this is the truth the
    cell is drawn from rather than something a cell reported.
    """
    none = 1.0 - win - loss
    kept = []
    for won in range(trials + 1):
        for lost in range(trials - won + 1):
            unresolved = trials - won - lost
            if (none == 0.0 and unresolved) or (win == 0.0 and won) \
                    or (loss == 0.0 and lost):
                continue
            probability = (comb(trials, won) * comb(trials - won, lost)
                           * win ** won * loss ** lost
                           * (none ** unresolved if unresolved else 1.0))
            if probability > floor:
                kept.append((won, lost, probability))
    mass = sum(probability for _, _, probability in kept)
    return [(won, lost, probability / mass) for won, lost, probability in kept], mass


def false_separation(trials: int, win: float, loss: float,
                     smoothing: float) -> tuple[float, float, int]:
    """(percent of pairs separated, mass kept, outcomes kept).

    Two cells drawn from the same (win, loss). Every separation is a false one.
    """
    cells, mass = outcomes(trials, win, loss)
    prepared = []
    for won, lost, probability in cells:
        margin = 100.0 * (won - lost) / trials
        rates = smoothed_rates(100.0 * won / trials, 100.0 * lost / trials,
                               trials, smoothing)
        prepared.append((margin, margin_variance(*rates), probability))
    separated = 0.0
    for margin_a, variance_a, probability_a in prepared:
        for margin_b, variance_b, probability_b in prepared:
            tolerance = 100.0 * math.sqrt((variance_a + variance_b) / trials)
            if abs(margin_a - margin_b) > tolerance:
                separated += probability_a * probability_b
    return 100.0 * separated, mass, len(cells)


def worst_over_the_grid(smoothing: float) -> tuple[float, tuple[float, float]]:
    """The highest false-separation rate any grid point reaches, and where."""
    worst, where = 0.0, GRID[0]
    for win, loss in GRID:
        rate, _, _ = false_separation(TRIALS, win, loss, smoothing)
        if rate > worst:
            worst, where = rate, (win, loss)
    return worst, where


def tolerance_at(win: float, loss: float, smoothing: float) -> float:
    """The tolerance two cells BOTH reporting these rates would be given.

    Rates in percentages here, because this is the shape `experiments.py`
    passes: what a cell reported, not what it was drawn from.
    """
    rates = smoothed_rates(win, loss, TRIALS, smoothing)
    return 100.0 * math.sqrt(2.0 * margin_variance(*rates) / TRIALS)


def main() -> None:
    print("=" * 78)
    print("HOW MUCH SMOOTHING THE PER-PAIR MARGIN TOLERANCE NEEDS")
    print(f"Issue #328. {TRIALS} campaigns per cell, exact enumeration.")
    print("=" * 78)

    print("\nA. HOW OFTEN TWO IDENTICAL CELLS ARE CALLED DIFFERENT")
    print("\n   Every separation below is a false one, because both cells are")
    print(f"   drawn from the same probabilities. The target is "
          f"{TARGET_PERCENT:.1f}%, which is what a")
    print("   one-standard-error two-sided test allows. Above it the tolerance "
          "is too")
    print("   tight; far below it the tolerance is wider than the sample "
          "needs.\n")
    header = (f"{'win':>7}{'loss':>7}{'cells':>7}{'kept':>9}"
              + "".join(f"{'s=' + format(s, 'g'):>10}" for s in CANDIDATES))
    print(header)
    print("-" * len(header))
    rates_by_point = {}
    for win, loss in GRID:
        row = []
        cells = mass = 0
        for smoothing in CANDIDATES:
            rate, mass, cells = false_separation(TRIALS, win, loss, smoothing)
            row.append(rate)
        rates_by_point[(win, loss)] = row
        print(f"{win:>7.3f}{loss:>7.2f}{cells:>7}{mass:>9.6f}"
              + "".join(f"{rate:>10.1f}" for rate in row))

    unsmoothed = rates_by_point[GRID[-1]][0]
    print(f"\n   WITHOUT SMOOTHING THE WORST POINT REACHES "
          f"{max(r[0] for r in rates_by_point.values()):.1f}%, against a "
          f"target of")
    print(f"   {TARGET_PERCENT:.1f}%. At a win probability of "
          f"{GRID[-1][0]:.3f} it is {unsmoothed:.1f}%: the tolerance is wrong "
          "more often")
    print("   than it is right, because most cells observe zero wins and are "
          "handed a")
    print("   variance of exactly zero.")

    print("\nB. WHERE THE FLOOR IS\n")
    print(f"{'smoothing':>11}{'worst %':>10}{'at win':>9}{'at loss':>9}"
          f"{'verdict':>12}")
    print("-" * 51)
    holding = []
    for smoothing in CANDIDATES:
        worst, (win, loss) = worst_over_the_grid(smoothing)
        holds = worst <= TARGET_PERCENT + 1.5
        if holds:
            holding.append(smoothing)
        print(f"{smoothing:>11g}{worst:>10.1f}{win:>9.3f}{loss:>9.2f}"
              f"{'holds' if holds else 'too tight':>12}")
    floor = min(holding)
    print(f"\n   THE SMALLEST CANDIDATE THAT HOLDS IS {floor:g}, and the one "
          f"below it does not.")
    print("   That is the whole reason for measuring: the floor is a real "
          "number rather")
    print("   than a matter of taste, and it is not zero.")

    print(f"\n   THE CHOSEN VALUE IS {MARGIN_SMOOTHING:g} PSEUDO-OUTCOMES PER "
          f"CATEGORY, which is {MARGIN_SMOOTHING / floor:.0f} steps above")
    print("   the floor. Two derivations arrive at it independently of this "
          "measurement:")
    print("     1. Agresti-Coull adds z^2/2 notional outcomes per category. "
          "The familiar")
    print("        \"add 2 successes and 2 failures\" is z = 1.96, a 95% "
          "interval. This")
    print("        tolerance is ONE standard error, so z = 1 and z^2/2 = 0.5.")
    print("     2. The Jeffreys prior for a multinomial is "
          "Dirichlet(1/2, ..., 1/2),")
    print("        which is 0.5 per category and mentions no confidence level "
          "at all.")
    print("\n   WHY NOT SIT ON THE FLOOR, which would be tighter and would "
          "separate more.")
    print("   Because the floor is measured over a finite grid at one sample "
          "size, and the")
    print("   candidate one step below it already fails. A constant chosen to "
          "sit at the")
    print("   edge of a measured region is one grid point away from the "
          "failure the")
    print("   smoothing exists to prevent, and what it buys is separations "
          "that were")
    print("   borderline anyway. Section D is the case where that costs "
          "something real.")

    print("\nC. WHAT IT COSTS AT THE CELLS THE SWEEP ACTUALLY LIVES IN\n")
    print("   The tolerance two cells BOTH reporting these rates get, in "
          "percentage points\n")
    header = (f"{'win%':>7}{'loss%':>7}"
              + "".join(f"{'s=' + format(s, 'g'):>10}" for s in CANDIDATES)
              + f"{'cap':>10}")
    print(header)
    print("-" * len(header))
    for win, loss in ((50.0, 50.0), (10.0, 80.0), (2.0, 95.0), (0.0, 97.0),
                      (0.0, 100.0)):
        print(f"{win:>7.1f}{loss:>7.1f}"
              + "".join(f"{tolerance_at(win, loss, s):>10.2f}"
                        for s in CANDIDATES)
              + f"{margin_noise(TRIALS):>10.2f}")

    zero = tolerance_at(0.0, 100.0, 0.0)
    chosen = tolerance_at(0.0, 100.0, MARGIN_SMOOTHING)
    print("\n   THE LAST ROW IS THE ONE THAT MATTERS. A cell that lost every "
          "campaign gets")
    print(f"   a tolerance of {zero:.2f} points without smoothing, so any gap "
          f"at all is called a")
    print(f"   difference. With smoothing it gets {chosen:.2f}, against a cap "
          f"of {margin_noise(TRIALS):.2f}.")

    print("\nD. THE PAIR THIS WAS OPENED FOR IS STILL REPORTED TIED")
    print("\n   Issue #328 quoted the closest pair at difficulty tiers 6 and 7 "
          "as 3.3")
    print("   percentage points apart and needing 2.3 to be called apart, "
          "against a cap of")
    print("   18.3 that called them tied. Measured again on 2026-08-06 at 60 "
          "campaigns per")
    print("   cell, that pair is the Explorer maxed preset against Explorer "
          "via floors")
    print(f"   (+30 floors): {TIER_67_PAIR[0][0]:.0f}% win with "
          f"{TIER_67_PAIR[0][1]:.1f}% loss against "
          f"{TIER_67_PAIR[1][0]:.0f}% win with {TIER_67_PAIR[1][1]:.0f}% "
          "loss.\n")
    unsmoothed_pair = 100.0 * math.sqrt(
        (margin_variance(*TIER_67_PAIR[0]) + margin_variance(*TIER_67_PAIR[1]))
        / TRIALS)
    smoothed_pair = margin_noise_between(*TIER_67_PAIR[0], *TIER_67_PAIR[1],
                                         TRIALS)
    cap = margin_noise(TRIALS)
    print(f"{'tolerance from':>22}{'points':>9}{'gap 3.3 is':>13}")
    print("-" * 44)
    for label, value in (("no smoothing", unsmoothed_pair),
                         (f"smoothing {MARGIN_SMOOTHING:g}", smoothed_pair),
                         ("the cap", cap)):
        print(f"{label:>22}{value:>9.2f}"
              f"{'APART' if TIER_67_GAP > value else 'tied':>13}")

    print("\n   THE 2.3 IN THE ISSUE WAS THE UNSMOOTHED FIGURE, which section "
          "A shows cannot")
    print("   be used: at these rates it separates two identical cells about "
          "half the time.")
    print(f"   Smoothed, the pair gets {smoothed_pair:.2f} and the 3.3 point "
          "gap does not clear it.")
    print("   That is the correct answer rather than a shortfall: the sample "
          "does not")
    print("   support calling those two presets different.")
    print(f"\n   WHAT THE CHANGE DOES BUY at those tiers is the ratio: "
          f"{cap / smoothed_pair:.1f} times")
    print("   tighter than the cap, so every pair separated by more than "
          f"{smoothed_pair:.1f} points is")
    print("   now reported as different where the cap called it a tie. "
          "Measured over all")
    print("   eight tiers on 2026-08-06, that moved the reported order at "
          "tiers 5, 6 and 7.")

    print("\nE. WHAT THIS DOES NOT SHOW")
    print("\n   It does not show the presets differ. It measures a statistical "
          "procedure")
    print("   against a known truth and imports no game model to do it.")
    print("\n   It does not show the per-pair tolerance is a refinement of the "
          "cap's")
    print("   grouping. It is not one. Splitting a group changes which preset "
          "opens the")
    print("   next, so a later pair can be compared that the cap never "
          "compared. See")
    print("   rank_by_score.")
    print("\n   It does not cover sample sizes other than "
          f"{TRIALS}. At 150 campaigns per cell,")
    print("   the size the full sweep uses, every rate above moves towards the "
          "target")
    print("   rather than away from it, because the estimate improves faster "
          "than the")
    print("   smoothing fades.")


main()
