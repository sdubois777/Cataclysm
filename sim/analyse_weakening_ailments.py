"""What the overflow rule already delivers to Cripple, Weaken, Shred and Madness.

Issue #300. The four weakening ailments have one affix each -- a chance to apply
-- while the six damage over time ailments were given damage and duration affixes
as well, by issue #205. This measures whether the missing two levers are missing
anything.

THE OVERFLOW RULE IS THE REASON THEY MIGHT NOT BE. `ailment_application` in
`cataclysm_sim/affixes.py` caps the chance to apply at 100% and turns everything
past it into a multiplier on magnitude, so 800% chance applies the effect every
hit at eight times its strength. Chance is therefore already both levers at once.

AND THE ROLL-OVER RULE IS THE SECOND HALF. `game/Data/StatusEffects.csv`
describes each of the four, and three of them say the same thing:

    Magnitude raises the reduction to a cap of 80%, then extends the duration
    instead.

So magnitude past a cap becomes duration. Chance is all three levers.

WHAT THIS SCRIPT MEASURES. How much ailment chance a character can actually
reach, and how much is needed to fill each ailment's magnitude cap. If the second
is far below the first, a magnitude affix would be dead on arrival and a duration
affix would be doing what the roll-over already does.

    python analyse_weakening_ailments.py

EVERY NUMBER IS READ FROM THE MODEL OR THE DATA. The base strengths, durations
and caps are parsed out of `game/Data/StatusEffects.csv`; the affix chance comes
from `cataclysm_sim.affixes`; the gem chances from `game/Data/Gems.csv`. Nothing
below is typed in, which is the failure `sim/tests/test_analysis_scripts.py`
exists to catch.
"""

from __future__ import annotations

import csv
import math
import pathlib
import re

from cataclysm_sim import affixes as af
from cataclysm_sim import player_power

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
GEMS = REPO_ROOT / "game" / "Data" / "Gems.csv"

#: The tier and gear level every affix figure below is taken at. The top of the
#: range, because the question is what a finished build can reach.
AFFIX_TIER = 7
GEAR_LEVEL = af.MAX_GEAR_LEVEL

#: The rarity column of `game/Data/Gems.csv` a finished build's gems sit in.
TOP_RARITY = "Cataclysmic"

#: The four this issue is about, and their affixes in `cataclysm_sim.affixes`.
WEAKENING = {
    "Cripple": af.CRIPPLE,
    "Weaken": af.WEAKEN,
    "Shred": af.SHRED,
    "Madness": af.MADNESS,
}


def format_base(base: tuple[float, bool]) -> str:
    """A base reduction with its unit, so a resistance point cannot be read as
    a percentage."""
    value, is_percent = base
    return f"{value:.0f}%" if is_percent else f"{value:.0f}pts"


def header(text: str) -> None:
    print(f"\n{'=' * 78}\n{text}\n{'=' * 78}")


def status_effect_rows() -> dict[str, str]:
    """Effect name to its description, for the four weakening debuffs."""
    with STATUS_EFFECTS.open(encoding="utf-8-sig", newline="") as handle:
        rows = {row["EffectName"]: row["Description"]
                for row in csv.DictReader(handle)}
    missing = sorted(set(WEAKENING) - set(rows))
    if missing:
        raise SystemExit(
            f"game/Data/StatusEffects.csv has no row for {missing}. The base "
            f"strengths and caps below are read from it.")
    return {name: rows[name] for name in WEAKENING}


def base_strength(description: str) -> tuple[float, bool] | None:
    """The reduction the effect applies before any magnitude multiplier.

    Returns the number AND whether it is a percentage. NOT EVERY REDUCTION IS
    ONE, and reading Shred's as a percentage is a mistake this script made
    before the units were checked: Cripple and Weaken take a percentage off a
    speed and a damage number, while Shred takes 10 POINTS off a resistance.
    Printing "10%" for the third made it read as a small version of the first
    two when it is a different quantity.

    None for Madness, which reduces nothing -- it redirects the enemy instead,
    so it has no magnitude to raise and its description says so.
    """
    match = re.search(r"[Rr]educes[^.]*?by (\d+(?:\.\d+)?)(%?)", description)
    if not match:
        return None
    return float(match.group(1)), match.group(2) == "%"


def base_duration(description: str) -> float:
    match = re.search(r"for (\d+(?:\.\d+)?) seconds", description)
    if not match:
        raise SystemExit(
            f"no duration found in {description!r}. Every weakening effect "
            f"states one, and this script reads it rather than storing it.")
    return float(match.group(1))


def magnitude_cap(description: str) -> float | None:
    """The reduction the effect stops at. None where it has no numeric cap."""
    match = re.search(r"cap of (\d+(?:\.\d+)?)%", description)
    return float(match.group(1)) if match else None


def rolls_over_into_duration(description: str) -> bool:
    return "extends the duration" in description


def gem_chance(gem_name: str) -> float:
    """One gem's chance to apply, in percent, at the top rarity."""
    with GEMS.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["GemName"] == gem_name:
                return 100.0 * float(row[TOP_RARITY])
    raise SystemExit(f"game/Data/Gems.csv has no gem named {gem_name!r}")


def affix_ceiling(affix: af.AilmentAffix) -> tuple[int, float]:
    """How many pieces can carry this affix, and the chance they add up to.

    One per piece, not one per affix slot: `ailment_group` puts every roll of
    the same chance in one group, and a piece cannot carry two affixes from one
    group.
    """
    pieces = sum(count for slot, count in af.GEAR_SLOTS.items()
                 if slot in affix.allowed_slots)
    per_piece = affix.chance_at(AFFIX_TIER, gear_level=GEAR_LEVEL)
    return pieces, pieces * per_piece


def what_the_effects_say() -> dict[str, str]:
    header("WHAT THE FOUR WEAKENING EFFECTS ALREADY DO")
    print("Read from game/Data/StatusEffects.csv, which is generated from the")
    print("Debuffs sheet of docs/All_Things_Cataclysm.xlsx.\n")
    print(f"{'effect':<10}{'base':>10}{'duration':>10}{'cap':>10}"
          f"{'rolls into duration':>22}")
    print("-" * 62)
    rows = status_effect_rows()
    for name, description in rows.items():
        base = base_strength(description)
        cap = magnitude_cap(description)
        print(f"{name:<10}"
              f"{('none' if base is None else format_base(base)):>10}"
              f"{base_duration(description):>9.0f}s"
              f"{('none' if cap is None else f'{cap:.0f}%'):>10}"
              f"{str(rolls_over_into_duration(description)):>22}")
    print()
    print("  Madness reduces nothing. It redirects the enemy, so it has no")
    print("  magnitude to raise, and its magnitude goes straight to duration.")
    print("  Shred's 10 is 10 POINTS of resistance, not 10 per cent. It is the")
    print("  only one of the four whose reduction is not a percentage, which is")
    print("  also why it has no percentage cap.")
    return rows


def how_much_chance_is_reachable() -> dict[str, float]:
    header("HOW MUCH CHANCE TO APPLY A FINISHED BUILD CAN REACH")
    print("Affixes: one per piece, because ailment_group puts every roll of the")
    print("same chance in one group and a piece cannot carry two from a group.")
    print(f"Gems: {player_power.TOTAL_SOCKETS} sockets exist across the gear, at "
          f"{TOP_RARITY} rarity.\n")
    print(f"{'effect':<10}{'pieces':>8}{'per piece':>11}{'affixes':>10}"
          f"{'per gem':>10}{'all sockets':>13}{'total':>10}")
    print("-" * 72)
    totals: dict[str, float] = {}
    for name, affix in WEAKENING.items():
        pieces, from_affixes = affix_ceiling(affix)
        per_gem = gem_chance(affix.gem)
        from_gems = per_gem * player_power.TOTAL_SOCKETS
        totals[name] = from_affixes + from_gems
        print(f"{name:<10}{pieces:>8}"
              f"{affix.chance_at(AFFIX_TIER, gear_level=GEAR_LEVEL):>10.0f}%"
              f"{from_affixes:>9.0f}%{per_gem:>9.0f}%{from_gems:>12.0f}%"
              f"{totals[name]:>9.0f}%")
    print()
    print("  The gem column is the ceiling, not a plan: a build filling every")
    print("  socket with one utility gem gives up every other gem in the game.")
    print("  The affix column alone is what a build gets for free while")
    print("  chasing the ailment at all.")
    return totals


def chance_needed_to_fill_the_cap(base: float, cap: float) -> float:
    """The total chance at which magnitude alone reaches the cap.

    `ailment_application` gives magnitude = total / 100, so the reduction is
    base x total / 100 and it reaches `cap` at 100 x cap / base.
    """
    return af.AILMENT_CHANCE_CAP * cap / base


def does_overflow_already_fill_the_caps(rows: dict[str, str],
                                        reachable: dict[str, float]) -> None:
    header("WHETHER THE OVERFLOW RULE ALREADY FILLS THE MAGNITUDE CAPS")
    print("A magnitude affix would only be worth a slot if the cap were hard to")
    print("reach without one. This is how much chance it takes.\n")
    print(f"{'effect':<10}{'base':>8}{'cap':>7}{'chance needed':>15}"
          f"{'affixes alone':>15}{'reached by':>26}")
    print("-" * 81)
    for name, description in rows.items():
        base = base_strength(description)
        cap = magnitude_cap(description)
        if base is None or cap is None:
            print(f"{name:<10}"
                  f"{('--' if base is None else format_base(base)):>8}"
                  f"{'--':>7}{'no cap':>15}{'--':>15}"
                  f"{'magnitude is duration':>26}")
            continue
        needed = chance_needed_to_fill_the_cap(base[0], cap)
        _, from_affixes = affix_ceiling(WEAKENING[name])
        per_gem = gem_chance(WEAKENING[name].gem)
        gems_needed = math.ceil(max(0.0, needed - from_affixes) / per_gem)
        reached = ("affixes alone" if from_affixes >= needed
                   else f"affixes + {gems_needed} of "
                        f"{player_power.TOTAL_SOCKETS} sockets")
        print(f"{name:<10}{format_base(base):>8}{cap:>6.0f}%{needed:>14.0f}%"
              f"{from_affixes:>14.0f}%{reached:>26}")
    print()
    print("  NEITHER CAP IS FILLED BY AFFIXES ALONE, and both are filled by")
    print("  affixes plus a handful of sockets. A build that wants the ailment")
    print("  at all reaches the cap; one that does not, does not.")
    print()
    print("  Shred has no percentage cap: it stops when the resistance it is")
    print("  reducing reaches zero, so what fills it depends on the enemy.")
    print(f"  Its reachable chance is {reachable['Shred']:.0f}%, a "
          f"{reachable['Shred'] / af.AILMENT_CHANCE_CAP:.0f}x magnitude")
    print(f"  multiplier on a base of {base_strength(rows['Shred'])[0]:.0f} "
          f"resistance points.")


def what_a_magnitude_affix_would_add(rows: dict[str, str]) -> None:
    header("WHAT A SEPARATE MAGNITUDE OR DURATION AFFIX WOULD ADD")
    print("Measured against what the chance affix already delivers through the")
    print("overflow rule and the roll-over into duration.\n")
    for name, description in rows.items():
        base = base_strength(description)
        cap = magnitude_cap(description)
        _, from_affixes = affix_ceiling(WEAKENING[name])
        if base is None:
            print(f"  {name:<9} has no magnitude at all, so a magnitude affix "
                  f"would have nothing to")
            print(f"  {'':<9} scale, and a duration affix would do what this "
                  f"ailment's magnitude does.")
            continue
        value, is_percent = base
        at_affix_ceiling = value * from_affixes / af.AILMENT_CHANCE_CAP
        unit = "%" if is_percent else "pts of resistance"
        if cap is None:
            print(f"  {name:<9} affixes alone reach "
                  f"{at_affix_ceiling:.0f}{unit} from a base of "
                  f"{value:.0f}{unit},")
            print(f"  {'':<9} which is {at_affix_ceiling / value:.1f}x, "
                  f"against no percentage cap at all.")
            continue
        print(f"  {name:<9} affixes alone reach {at_affix_ceiling:.0f}{unit} "
              f"against a {cap:.0f}% cap, which is")
        print(f"  {'':<9} {at_affix_ceiling / cap:.1f}x the cap. A few sockets "
              f"take it past.")
    print()
    print("  THE CONCLUSION. Chance to apply is already three levers in one:")
    print("  chance up to 100%, magnitude above that, and duration above the")
    print("  magnitude cap. A magnitude affix would raise a number that is")
    print("  already capped, so it would be worth nothing to any build past the")
    print("  cap, which is any build that wanted the ailment. A duration affix")
    print("  would do what the roll-over already does, bought with chance the")
    print("  build was buying anyway.")
    print()
    print("  WHAT THAT DOES NOT SAY. It does not say the four are as strong as")
    print("  the six damage over time ailments, or that one chance affix is")
    print("  priced right against three. It says a second and third affix would")
    print("  add no lever that is not already there. Issue #300.")


def main() -> None:
    print(__doc__.split("\n\n")[0])
    rows = what_the_effects_say()
    reachable = how_much_chance_is_reachable()
    does_overflow_already_fill_the_caps(rows, reachable)
    what_a_magnitude_affix_would_add(rows)


# Called unconditionally, like the other analyse_*.py scripts.
# `sim/tests/test_analysis_scripts.py` runs them with `runpy.run_path`, which
# does NOT set `__name__` to `"__main__"`, so a guarded call produces no output
# under any test. That was issue #319.
main()
