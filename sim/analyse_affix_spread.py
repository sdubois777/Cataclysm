"""How many different numbers an affix can actually SHOW on an early drop.

Issue #1179, from a play test on 2026-09-02: affixes "all spawn with the exact
same numbers instead of randomly within a range", and several are too small to
be worth a slot.

**The rolls are genuinely random.** `UCataclysmDropRoll::RollItem` draws a fresh
roll position per affix from a stream seeded per creature and per moment of
death. What this script measures is that at the start of the game the random
part is squeezed until it is invisible on the tool tip, by three multipliers
that were each designed separately:

1. **The roll band is a quarter wide, by design.** A roll lands between 75 and
   100 per cent of its tier's top. `docs/Cataclysm_GDD_v2.md` states this
   deliberately. Not the fault.

2. **At difficulty tier 1 an affix can only be tier 1 or tier 2.**
   `UCataclysmDropRoll::MaxAffixTierOnADrop` returns the difficulty tier plus
   one. Tier is the big variance axis and at the start of the game it barely
   moves. Note this makes the reachable values TWO SEPARATE BANDS rather than
   one continuous range, which is why some displayed numbers are skipped
   entirely.

3. **Almost every drop is gear level 0**, which divides everything by about
   3.96. 99.3 per cent of drops by weight gate at gear level 0.

Their product is what a player sees. This script measures it rather than
arguing it, so the figures can be rechecked after any change to the roll band,
the tier gate or the gear level curve.

WHY IT MODELS THE TOOL TIP AND NOT ONLY THE VALUE. A value of 0.031 and a value
of 0.041 are different numbers and identical on screen; the complaint was about
what is on screen. `UCataclysmItemTooltip::NumberInWords` is reproduced below
and is the only part of this script that is a copy of engine code rather than a
call into the model.

Run from `sim/`:

    python analyse_affix_spread.py
"""

from __future__ import annotations

from cataclysm_sim import affixes as af

#: Roll positions sampled inside each tier's band.
#:
#: DENSE ON PURPOSE. The question is which distinct strings are REACHABLE, so
#: sampling too coarsely would under-report the spread and make the finding look
#: worse than it is. 401 positions puts samples about a quarter of a per cent of
#: the band apart, far finer than any displayed step.
ROLL_SAMPLES = 401

#: The difficulty tier a new character plays at.
STARTING_DIFFICULTY_TIER = 1

#: The gear upgrade level almost every drop arrives at. See point 3 above.
UNUPGRADED = 0

#: An affix showing this many distinct numbers or fewer has effectively no
#: visible roll. Three is the threshold the issue used.
FEW = 3


def number_in_words(value: float) -> str:
    """`UCataclysmItemTooltip::NumberInWords`, reproduced.

    A COPY, AND THE ONLY ONE IN THIS FILE. The engine prints a whole number
    without a decimal point and everything else to one decimal place:

        if (FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value), 0.005f))
            return FString::FromInt(FMath::RoundToInt(Value));
        return FString::Printf(TEXT("%.1f"), Value);

    `tools/tests/` is where a check that this copy still matches the engine
    belongs; see the test beside this script.
    """
    if abs(value - round(value)) < 0.005:
        return str(int(round(value)))
    return f"{value:.1f}"


def reachable_tiers(difficulty_tier: int = STARTING_DIFFICULTY_TIER) -> list[int]:
    """The affix tiers a drop can roll at this difficulty tier.

    `UCataclysmDropRoll::MaxAffixTierOnADrop` is the difficulty tier plus one,
    capped at the top tier, and a drop may roll anything at or below it.
    """
    return list(range(1, af.max_affix_tier_on_a_drop(difficulty_tier) + 1))


def displayed_values(affix,
                     difficulty_tier: int = STARTING_DIFFICULTY_TIER,
                     gear_level: int = UNUPGRADED) -> tuple[list[str], float, float]:
    """Every distinct string this affix can show, and its true value range.

    Returned together because the interesting comparison is between the two: a
    wide true range that collapses onto one string is the finding.
    """
    seen: dict[str, None] = {}
    lowest = float("inf")
    highest = 0.0

    for tier in reachable_tiers(difficulty_tier):
        for step in range(ROLL_SAMPLES):
            roll = step / (ROLL_SAMPLES - 1)
            value = affix.value_at(tier, roll=roll, gear_level=gear_level)
            seen[number_in_words(value)] = None
            lowest = min(lowest, value)
            highest = max(highest, value)

    return list(seen), lowest, highest


def every_measurable_affix() -> list[af.StatAffix | af.AffixFamily]:
    """Every affix the model carries whose value can be measured, by name.

    THE MODEL RATHER THAN `game/Data/Affixes.csv`, so this script needs nothing
    outside `sim/` and stays runnable on its own. The model holds 50 of the 72
    non-hybrid rows in that file, plus the three resistance families, and that
    includes every affix issue #1179 names.

    THE RESISTANCE FAMILIES ARE INCLUDED AND ARE NOT StatAffix. Resistance has a
    breadth axis -- one type, two, or all eight -- so it is modelled as its own
    class. Leaving them out lost "All resistances", which is one of the twelve
    the issue lists and the only one of them that skips a displayed number.
    """
    found: dict[str, af.StatAffix | af.AffixFamily] = {}
    for name in dir(af):
        value = getattr(af, name)
        if isinstance(value, (af.StatAffix, af.AffixFamily)):
            found[value.name] = value
    return [found[name] for name in sorted(found)]


def main() -> None:
    tiers = reachable_tiers()
    print("HOW MANY DIFFERENT NUMBERS AN AFFIX CAN SHOW ON AN EARLY DROP")
    print("=" * 74)
    print()
    print(f"  difficulty tier      {STARTING_DIFFICULTY_TIER}")
    print(f"  affix tiers reachable {tiers}")
    print(f"  gear level           {UNUPGRADED}")
    print(f"  roll positions tried {ROLL_SAMPLES} inside each tier's band")
    print()

    affixes = every_measurable_affix()
    measured = [(a, *displayed_values(a)) for a in affixes]

    squeezed = [row for row in measured if len(row[1]) <= FEW]
    squeezed.sort(key=lambda row: (len(row[1]), row[0].name))

    print(f"AFFIXES THAT CAN SHOW {FEW} NUMBERS OR FEWER: "
          f"{len(squeezed)} of {len(affixes)}")
    print("-" * 74)
    print(f"  {'affix':<38}{'true range':>18}   every number it can show")
    for affix, shown, low, high in squeezed:
        print(f"  {affix.name:<38}{low:>8.3f} to {high:<7.3f}   "
              f"{', '.join(sorted(shown))}")
    print()

    stuck = [row for row in squeezed if len(row[1]) == 1]
    if stuck:
        print(f"  OF THOSE, {len(stuck)} CAN SHOW ONLY ONE NUMBER, so two drops of "
              f"the item")
        print("  can never differ in it at all:")
        for affix, shown, low, high in stuck:
            print(f"    {affix.name:<36}always {shown[0]}  "
                  f"(truly {low:.3f} to {high:.3f})")
        print()

    # BOTH SPELLINGS OF ZERO. A value of exactly 0 prints as "0" and a small
    # non-zero one prints as "0.0"; a player reads either as nothing. Checking
    # only for "0" meant this branch never fired, because every affix here is
    # small-but-non-zero and prints the second spelling.
    worthless = [row for row in stuck if row[1][0] in ("0", "0.0")]
    if worthless:
        print(f"  AND {len(worthless)} OF THOSE ALWAYS SHOW ZERO, which is a slot "
              f"granting nothing")
        print("  a player can read. That half is issue #858.")
        print()

    print("THE HEADLINE AFFIXES, WHICH ARE NOT IN THE LIST ABOVE")
    print("-" * 74)
    print("  They do show a spread, so the play report is not literally true of")
    print("  every affix. The spread is still narrow enough that two drops very")
    print("  often read the same.")
    print()
    print(f"  {'affix':<38}{'true range':>18}{'numbers':>10}")
    headline = ("Flat damage", "Increased damage", "Flat maximum health",
                "Increased maximum health")
    by_name = {a.name: (a, s, lo, hi) for a, s, lo, hi in measured}
    for name in headline:
        if name not in by_name:
            continue
        affix, shown, low, high = by_name[name]
        print(f"  {affix.name:<38}{low:>8.3f} to {high:<7.3f}{len(shown):>10}")
    print()

    print("WHAT SQUEEZES THEM, MEASURED SEPARATELY")
    print("-" * 74)
    sample = by_name.get("Flat maximum health")
    if sample:
        affix = sample[0]
        stated = affix.top_value
        at_top = affix.value_at(af.AFFIX_TIERS[-1], roll=1.0,
                                gear_level=af.MAX_GEAR_LEVEL)
        at_start = affix.value_at(1, roll=1.0, gear_level=UNUPGRADED)
        print(f"  Taking {affix.name} as the example, stated top value {stated:g}:")
        print(f"    best roll, top tier, fully upgraded   {at_top:>8.3f}")
        print(f"    best roll, tier 1, gear level 0       {at_start:>8.3f}")
        print(f"    so an early drop is worth 1 part in "
              f"{at_top / at_start:.1f} of a finished one")
    print()
    print("  The three multipliers are each intended on their own. This script")
    print("  measures their product, which nobody chose directly.")


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, matching
# `analyse_per_tier_rarity.py` and the others. `sim/tests/test_analysis_scripts.py`
# runs these through `runpy.run_path`, which does NOT set `__name__` to
# `__main__`, so a guarded script runs and prints nothing and the test reports
# it as a script that produced no output.
main()
