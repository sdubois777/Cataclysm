"""What a stated floor per affix costs, and what each affix is worth at its worst.

For issue #1230, which is the general one of four split out of a measurement
made with the project owner on 2026-09-03: every affix's worst roll was exactly
a tenth of its stated top, and on an affix whose top is itself small that made
the bottom of the ladder worthless.

WHY A SCRIPT RATHER THAN A TABLE IN THE ISSUE. The floors chosen there were
argued from numbers, and a table nobody can re-derive is a claim rather than a
measurement. Run this after changing any of the three ladder constants, any
stated top, or any floor.

    cd sim && python analyse_affix_floors.py

WHAT IT SHOWS, in four parts:

1.  Why the tier ladder could not simply be squeezed to raise the floor. That is
    the obvious reading of "run the ladder between the floor and the top" and it
    breaks the guard that a perfect roll two tiers down cannot beat the worst
    roll here.
2.  What the shape actually chosen costs instead, which is ladder steepness, and
    only on the affixes that state a floor.
3.  Every affix, its top, its floor and its worst roll.
4.  The three stats whose scale was the fault rather than their floor.

`sim/tests/test_analysis_scripts.py` runs this file and asserts the findings, so
a number that moves fails a test rather than going unnoticed.
"""

from __future__ import annotations

from cataclysm_sim import affixes as af
from cataclysm_sim import reference_build as rb

#: The bound `Cataclysm.Item.BandsOverlapByExactlyOneTier` enforces. A tier's
#: band reaches down to 0.75 of its own top, so a tier two below must be more
#: than 1/0.75 away or its perfect roll beats this tier's worst one.
TWO_TIER_BOUND = 1.0 / (1.0 - af.ROLL_BAND_FRACTION)

#: Pieces a modest commitment to one stat looks like.
#:
#: PIECES AND NOT AFFIX SLOTS. `slots_available` counts four slots per
#: eligible piece, and the affix group rule allows one roll of a given stat
#: and kind per piece, so the most a character can carry of one affix is one
#: per piece. Reading the slot count as the maximum overstates it fourfold.
MODEST_PIECES = 4


def pieces_available(affix) -> int:
    """How many worn pieces could each carry one roll of this affix."""
    return sum(count for slot, count in af.GEAR_SLOTS.items()
               if slot in affix.allowed_slots)


def squeezed_tier_ladder(share_of_top: float) -> float:
    """The tier ladder a floor of `share_of_top` would need, squeezing it.

    The three ladders multiply to `1 / WORST_MULTIPLIER`, so holding the roll
    band and the gear ladder still and demanding a worst roll of `share_of_top`
    leaves the tier ladder to take up the difference. A HIGHER floor needs a
    FLATTER ladder, so the share divides rather than multiplying.
    """
    return af.TIER_LADDER_SPAN * af.WORST_MULTIPLIER / share_of_top


def adjacent_ratio(span: float) -> float:
    """A geometric tier ladder's ratio between one tier and the next."""
    return span ** (1.0 / (len(af.AFFIX_TIERS) - 1))


def ladders_with_floor(top_value: float, floor: float) -> tuple[float, float]:
    """The tier and gear ladders an affix keeps once it states a floor.

    Returned as (tier 1 to tier 7 at +10, gear +0 to +10 at tier 7), both
    measured on a perfect roll.
    """
    def value(tier: int, gear: int) -> float:
        return af.affix_value(top_value, tier, 1.0, gear, floor)

    return (value(af.AFFIX_TIERS[-1], af.MAX_GEAR_LEVEL)
            / value(af.AFFIX_TIERS[0], af.MAX_GEAR_LEVEL),
            value(af.AFFIX_TIERS[-1], af.MAX_GEAR_LEVEL)
            / value(af.AFFIX_TIERS[-1], 0))


def guard_holds(top_value: float, floor: float) -> bool:
    """Both halves of `Cataclysm.Item.BandsOverlapByExactlyOneTier`."""
    def value(tier: int, roll: float) -> float:
        return af.affix_value(top_value, tier, roll, af.MAX_GEAR_LEVEL, floor)

    no_two_tier_overlap = all(
        value(tier - 2, 1.0) < value(tier, 0.0)
        for tier in af.AFFIX_TIERS[2:])
    one_tier_overlap_exists = any(
        value(tier - 1, 1.0) > value(tier, 0.0)
        for tier in af.AFFIX_TIERS[1:])
    return no_two_tier_overlap and one_tier_overlap_exists


def every_affix() -> list[tuple[str, float, float, int]]:
    """Name, top value, floor and how many affix slots it could occupy."""
    rows = [(a.name, a.top_value, a.floor, a.slots_available())
            for a in af.AFFIX_POOL]
    resistance_slots = af.slots_available_to(frozenset(af.GEAR_SLOTS))
    rows += [(f.name, f.top_value, f.floor, resistance_slots)
             for f in af.RESISTANCE_FAMILIES]
    return sorted(rows, key=lambda row: row[1])


def floored_affixes() -> list[tuple[str, float, float, int]]:
    return [row for row in every_affix() if row[2] > 0.0]


def report() -> None:
    print("WHY THE TIER LADDER COULD NOT SIMPLY BE SQUEEZED")
    print("=" * 74)
    print("  Holding the roll band and the gear ladder still, a higher floor")
    print("  leaves the tier ladder to take up the difference. Below about an")
    print("  eighth of the top that ladder is too flat for the guard.")
    print()
    print(f"  {'floor':<14}{'tier ladder':>13}{'tier to tier':>15}"
          f"{'two-tier guard':>17}")
    for divisor in (10, 8, 6, 5, 4, 3):
        span = squeezed_tier_ladder(1.0 / divisor)
        ratio = adjacent_ratio(span)
        holds = ratio ** 2 > TWO_TIER_BOUND
        print(f"  top/{divisor:<11}{span:>12.2f}x{ratio:>15.4f}"
              f"{'passes' if holds else 'FAILS':>17}")

    print()
    print("WHAT THE SHAPE CHOSEN COSTS INSTEAD")
    print("=" * 74)
    print("  The floor is added and the existing ladder runs across what is")
    print("  left. That map is monotone, so it preserves every comparison the")
    print("  guard makes. What it costs is steepness.")
    print()
    print(f"  {'floor':<14}{'T1 to T7':>11}{'+0 to +10':>12}{'the guard':>13}")
    for divisor in (10, 6, 5, 4, 3):
        top = 100.0
        tier_ladder, gear_ladder = ladders_with_floor(top, top / divisor)
        holds = guard_holds(top, top / divisor)
        print(f"  top/{divisor:<11}{tier_ladder:>10.2f}x{gear_ladder:>11.2f}x"
              f"{'passes' if holds else 'FAILS':>13}")

    print()
    print("EVERY AFFIX, ITS FLOOR AND ITS WORST ROLL")
    print("=" * 74)
    print(f"  {'affix':<38}{'top':>8}{'floor':>8}{'worst':>8}{'T1-T7':>9}")
    for name, top, floor, _slots in every_affix():
        worst = af.affix_value(top, af.AFFIX_TIERS[0], 0.0, 0, floor)
        tier_ladder, _ = ladders_with_floor(top, floor)
        stated = f"{floor:.2f}" if floor else "-"
        print(f"  {name:<38}{top:>8.2f}{stated:>8}{worst:>8.3f}"
              f"{tier_ladder:>8.2f}x")

    print()
    print("THE STATS WHOSE SCALE WAS THE FAULT, NOT THEIR FLOOR")
    print("=" * 74)
    hero = rb.character()
    pool = hero.stat("max_health")
    mana = hero.stat("max_mana")
    #: The class line at level 100 for the class built around each, which is
    #: what the affixes multiply and add to.
    class_line = {"health_regen": 37.65, "mana_regen": 26.75}
    for stat, size, what in (("health_regen", pool, "the health pool"),
                             ("mana_regen", mana, "the mana pool")):
        flat = next(a for a in af.AFFIX_POOL
                    if a.stat == stat and a.kind == "flat")
        increased = next(a for a in af.AFFIX_POOL
                         if a.stat == stat and a.kind == "increased")
        base = class_line[stat]
        print(f"  {flat.name}: top {flat.top_value} on "
              f"{pieces_available(flat)} pieces, and {increased.name} at "
              f"{increased.top_value} on {pieces_available(increased)}")
        for count, label in ((MODEST_PIECES, f"{MODEST_PIECES} pieces each way"),
                             (pieces_available(flat), "every piece each way")):
            total = ((base + count * flat.top_value)
                     * (1.0 + count * increased.top_value / 100.0))
            print(f"    {label}: {total:.1f} a second against {what} of "
                  f"{size:,.0f}, which is {total / size * 100:.2f}% a second")
    print()
    print("  Path of Exile calls 1 to 2% of maximum life per second a modest")
    print("  regeneration build and 5 to 8% a heavy one.")


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, matching
# `analyse_affix_spread.py` and the others. `sim/tests/test_analysis_scripts.py`
# runs these through `runpy.run_path`, which does NOT set `__name__` to
# `__main__`, so a guarded script runs and prints nothing and the test reports
# it as a script that produced no output.
report()
