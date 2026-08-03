"""What a two-handed weapon's affixes must be worth to balance dual wielding.

Issue #117. The project owner settled that dual wielding gives a second weapon
piece, and that the balance comes from a two-handed weapon's affixes being worth
more per affix rather than from equalising the slot count:

    yes dual wielding is a thing. This is compensated for by 2h affixes having
    more value than 1h affixes

This measures candidate values for that multiplier. Run:

    python analyse_two_handed_multiplier.py

WHAT DIFFERS BETWEEN THE TWO LOADOUTS, and nothing else does. Seventeen
non-weapon pieces carry 68 affix slots either way and are held identical here,
so every number below is driven by the weapon alone.

    two-handed    1 piece,  4 affix slots, each worth M times a one-handed one
    dual wield    2 pieces, 8 affix slots, each worth one

THE BASE DAMAGE OF TWO WEAPONS IS SUMMED. Decided by the project owner
2026-08-03, answering a question `docs/Cataclysm_GDD_v2.md` does not address:
it covers dual wielding only in terms of damage types and never says what
happens to the damage number. Both readings are still measured below, because
the averaged one is what the current weapon base damages were set against and
the comparison is what shows they no longer fit.

WHAT SUMMING CHANGES, and it is not small. Two one-handed bases sum to more than
any two-handed base: an Axe and a Sword give 86 against a Greatsword's 78. The
five two-handed bases average 1.03 times two one-handed ones, which is parity,
so a two-handed weapon has no base damage advantage at all. Section VI states
that the two-hander stays ahead on raw damage, and under summing it is not.

Raising the affix multiplier alone cannot fix that without breaking a different
rule. The answer came from research rather than from measurement: Last Epoch
applies its two-handed bonus to implicits as well as affixes, and a weapon's
base damage is an implicit here. See `why_the_multiplier_applies_to_implicits_too`
at the end.

A SECOND RULE ALREADY IN THE DESIGN constrains this. Section VII states:

    Two one-handed weapons count as one equipped piece, the same way they give
    the same six sockets a two-handed weapon gives. Dual wielding must not be
    worth free Power Score.

So the Power Score model deliberately rates the two loadouts the same. If the
affix budgets are not also equal, a dual wielder carries power their rating does
not count, which is the same problem that rule exists to prevent, expressed in
affixes instead of in the score.
"""

from __future__ import annotations

from cataclysm_sim import affixes as af

TIER = 8
AFFIX_TIER = 7
GEAR_LEVEL = af.MAX_GEAR_LEVEL

#: The two highest-damage one-handed bases, against the highest-damage
#: two-handed one. Picking the best of each is what makes the comparison a
#: comparison rather than an artefact of which bases were chosen.
ONE_HANDED = ("Axe", "Sword")
TWO_HANDED = "Greatsword"

#: A weapon holds two prefixes, and the weapon prefix pool has exactly three
#: entries: flat damage, increased damage and increased spell damage. An attack
#: build takes the first two, which is the only sensible pair for a weapon that
#: is not a caster's.
PREFIXES_PER_WEAPON = af.PREFIXES_PER_PIECE
SUFFIXES_PER_WEAPON = af.SUFFIXES_PER_PIECE

#: What the 68 non-weapon slots contribute to damage, in the reference build's
#: proportions. Held identical between the two loadouts.
NON_WEAPON_FLAT_DAMAGE_AFFIXES = 5
NON_WEAPON_INCREASED_DAMAGE_AFFIXES = 5

CANDIDATES = (1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5)


def header(text: str) -> None:
    print(f"\n{'=' * 78}\n{text}\n{'=' * 78}")


def flat_damage_value(multiplier: float = 1.0) -> float:
    return af.FLAT_DAMAGE.value_at(AFFIX_TIER, gear_level=GEAR_LEVEL) * multiplier


def increased_damage_value(multiplier: float = 1.0) -> float:
    return af.INCREASED_DAMAGE.value_at(AFFIX_TIER, gear_level=GEAR_LEVEL) * multiplier


def base_damage(name: str) -> float:
    """The flat attack damage a weapon base supplies, at full upgrade level."""
    base = af.base_named(name)
    return sum(i.value_at(GEAR_LEVEL) for i in base.implicits
               if i.stat == "attack_damage" and i.kind == "flat")


def damage_per_hit(weapon_base: float,
                   weapon_flat_affixes: int,
                   weapon_increased_affixes: int,
                   weapon_multiplier: float) -> float:
    """One basic attack, which is 100% of weapon damage by definition.

        (base + flat) x (1 + increases)

    The base bracket is the weapon plus every flat damage affix, which is what a
    player reads "weapon damage" to mean and why flat added damage is worth
    taking at all.
    """
    flat = (flat_damage_value() * NON_WEAPON_FLAT_DAMAGE_AFFIXES
            + flat_damage_value(weapon_multiplier) * weapon_flat_affixes)
    increased = (increased_damage_value() * NON_WEAPON_INCREASED_DAMAGE_AFFIXES
                 + increased_damage_value(weapon_multiplier) * weapon_increased_affixes)
    return (weapon_base + flat) * (1.0 + increased / 100.0)


def two_handed_damage(multiplier: float) -> float:
    # Two prefixes on one weapon: one flat damage, one increased damage.
    return damage_per_hit(base_damage(TWO_HANDED), 1, 1, multiplier)


def dual_wield_damage(sum_bases: bool) -> float:
    bases = [base_damage(name) for name in ONE_HANDED]
    weapon_base = sum(bases) if sum_bases else sum(bases) / len(bases)
    # Four prefixes across two weapons: two flat damage, two increased damage.
    return damage_per_hit(weapon_base, 2, 2, 1.0)


def solve_multiplier(sum_bases: bool) -> float | None:
    """The multiplier at which the two loadouts deal the same damage per hit.

    Found by bisection rather than algebra: the multiplier enters both brackets
    of the pipeline, so damage is quadratic in it, and reading the crossing off
    the same function the table prints means the two cannot disagree.
    """
    target = dual_wield_damage(sum_bases)
    low, high = 0.0, 10.0
    if two_handed_damage(high) < target:
        return None
    for _ in range(200):
        mid = (low + high) / 2.0
        if two_handed_damage(mid) < target:
            low = mid
        else:
            high = mid
    return (low + high) / 2.0


def affix_budget_table() -> None:
    header("AFFIX BUDGET: what each loadout is worth in one-handed affixes")
    print("Seventeen non-weapon pieces give 68 slots either way. Only the weapon")
    print("slots differ, so only they are counted here.\n")
    print(f"{'multiplier':>10}  {'2H weapon slots':>16}  {'dual wield':>11}  "
          f"{'total 2H':>9}  {'total DW':>9}  {'verdict':<28}")
    print("-" * 96)
    for multiplier in CANDIDATES:
        two_handed_slots = af.AFFIX_SLOTS_PER_PIECE * multiplier
        dual_slots = af.AFFIX_SLOTS_PER_PIECE * 2
        total_two = 68 + two_handed_slots
        total_dual = 68 + dual_slots
        if abs(total_two - total_dual) < 0.01:
            verdict = "equal"
        elif total_two < total_dual:
            verdict = f"dual wield ahead by {total_dual - total_two:.0f}"
        else:
            verdict = f"two-hander ahead by {total_two - total_dual:.0f}"
        print(f"{multiplier:>10.2f}  {two_handed_slots:>16.1f}  {dual_slots:>11.0f}  "
              f"{total_two:>9.1f}  {total_dual:>9.1f}  {verdict:<28}")


def base_damage_table() -> None:
    header("WEAPON BASE DAMAGE, before any affix")
    for name in ONE_HANDED:
        print(f"  {name:<14} one-handed   {base_damage(name):>7.1f}")
    both = sum(base_damage(n) for n in ONE_HANDED)
    print(f"  {'both summed':<14}              {both:>7.1f}")
    print(f"  {'both averaged':<14}              {both / 2:>7.1f}")
    print(f"  {TWO_HANDED:<14} two-handed   {base_damage(TWO_HANDED):>7.1f}")
    print()
    print(f"  A two-hander is {base_damage(TWO_HANDED) / (both / 2):.2f}x the average "
          f"one-hander and {base_damage(TWO_HANDED) / both:.2f}x the two together.")


def damage_table(sum_bases: bool) -> None:
    reading = "SUMMED" if sum_bases else "AVERAGED"
    header(f"DAMAGE PER BASIC ATTACK, with dual-wield base damage {reading}")

    dual = dual_wield_damage(sum_bases)
    print(f"  dual wield, 8 weapon affix slots at normal value : {dual:>10,.0f}")
    print(f"  the target an ordinary hit must reach at tier 8  : "
          f"{af.damage_target(TIER):>10,.0f}\n")
    print(f"{'multiplier':>10}  {'two-handed damage':>18}  {'vs dual wield':>14}  "
          f"{'verdict':<24}")
    print("-" * 74)
    for multiplier in CANDIDATES:
        value = two_handed_damage(multiplier)
        ratio = value / dual
        if abs(ratio - 1.0) < 0.005:
            verdict = "equal"
        elif ratio < 1.0:
            verdict = f"dual wield ahead {1 / ratio:.2f}x"
        else:
            verdict = f"two-hander ahead {ratio:.2f}x"
        print(f"{multiplier:>10.2f}  {value:>18,.0f}  {ratio:>13.3f}x  {verdict:<24}")

    crossing = solve_multiplier(sum_bases)
    if crossing is None:
        print("\n  The two never cross inside a sensible range.")
    else:
        print(f"\n  EQUAL DAMAGE AT A MULTIPLIER OF {crossing:.3f}")


#: What the shared 68 non-weapon slots put into critical strikes. Stated rather
#: than derived, because the comparison only needs it held equal, but the
#: absolute crit rate does move the ratio so it is not hidden.
SHARED_FLAT_CRIT_CHANCE_AFFIXES = 3
SHARED_FLAT_CRIT_MULTIPLIER_AFFIXES = 3

#: The class base, from sim/cataclysm_sim/classes.py. 150 means a critical
#: strike deals 1.5 times a normal hit.
BASE_CRIT_MULTIPLIER = 150.0


def crit_factor(weapon_suffix_slots: int, weapon_multiplier: float) -> float:
    """Average damage relative to a non-critical hit.

    ONLY FLAT CRITICAL STRIKE AFFIXES ARE USED HERE, and that is forced rather
    than chosen. Increased critical strike chance multiplies a base that comes
    from the skill, and no skill in the project supplies one, so it is worth
    nothing at present. See issue #120. Flat critical strike chance enters the
    base bracket, so it works, which makes it the only measurable route.

    A weapon holds two suffixes. A critical strike build takes one flat critical
    strike chance and one flat critical strike multiplier per weapon.
    """
    per_weapon_pairs = weapon_suffix_slots // 2

    chance = (af.FLAT_CRIT_CHANCE.value_at(AFFIX_TIER, gear_level=GEAR_LEVEL)
              * SHARED_FLAT_CRIT_CHANCE_AFFIXES
              + af.FLAT_CRIT_CHANCE.value_at(AFFIX_TIER, gear_level=GEAR_LEVEL)
              * per_weapon_pairs * weapon_multiplier)

    multiplier = (BASE_CRIT_MULTIPLIER
                  + af.FLAT_CRIT_MULTIPLIER.value_at(AFFIX_TIER, gear_level=GEAR_LEVEL)
                  * SHARED_FLAT_CRIT_MULTIPLIER_AFFIXES
                  + af.FLAT_CRIT_MULTIPLIER.value_at(AFFIX_TIER, gear_level=GEAR_LEVEL)
                  * per_weapon_pairs * weapon_multiplier)

    chance = min(chance, 100.0)  # Hard cap: above 100% a crit chance means nothing.
    return 1.0 + (chance / 100.0) * (multiplier / 100.0 - 1.0)


def average_damage(sum_bases: bool, multiplier: float, two_handed: bool) -> float:
    """Damage per hit including critical strikes, which is what the four extra
    suffix slots a dual wielder holds actually buy."""
    if two_handed:
        return two_handed_damage(multiplier) * crit_factor(SUFFIXES_PER_WEAPON * 2,
                                                           multiplier)
    return dual_wield_damage(sum_bases) * crit_factor(SUFFIXES_PER_WEAPON * 4, 1.0)


def solve_with_crit(sum_bases: bool) -> float | None:
    target = average_damage(sum_bases, 1.0, two_handed=False)
    low, high = 0.0, 10.0
    if average_damage(sum_bases, high, two_handed=True) < target:
        return None
    for _ in range(200):
        mid = (low + high) / 2.0
        if average_damage(sum_bases, mid, two_handed=True) < target:
            low = mid
        else:
            high = mid
    return (low + high) / 2.0


def crit_table() -> None:
    header("WITH THE SUFFIX SLOTS COUNTED: average damage including criticals")
    print("The dual wielder holds four more suffix slots as well as four more")
    print("prefixes. Those go to critical strikes, so leaving them out overstates")
    print("the two-hander. Attack speed cannot be included at all: its base comes")
    print("from the weapon and no weapon supplies one, so every increase to it is")
    print("worth nothing. That is issue #120, and it is why this stops at criticals.\n")

    for sum_bases in (False, True):
        reading = "AVERAGED" if not sum_bases else "SUMMED"
        dual = average_damage(sum_bases, 1.0, two_handed=False)
        crossing = solve_with_crit(sum_bases)
        print(f"  dual-wield base damage {reading}")
        print(f"    dual wield average hit          : {dual:>10,.0f}")
        for candidate in (1.5, 2.0, 2.5):
            value = average_damage(sum_bases, candidate, two_handed=True)
            print(f"    two-hander at {candidate:.2f}              : "
                  f"{value:>10,.0f}   {value / dual:.3f}x")
        if crossing is None:
            print("    they never cross inside a sensible range")
        else:
            print(f"    EQUAL AT A MULTIPLIER OF {crossing:.3f}")
        print()


def why_it_is_not_two() -> None:
    header("WHY THE ANSWER IS NOT SIMPLY 2.0")
    print("Eight one-handed affix slots against four two-handed ones is a ratio")
    print("of 2.0, so 2.0 equalises the AFFIX BUDGET exactly. It does not")
    print("equalise DAMAGE, because the two-hander also brings more base damage")
    print("and the multiplier enters both brackets of the pipeline:\n")
    print("    (base + flat) x (1 + increases)\n")
    print("The multiplier raises the flat affixes inside the first bracket and")
    print("the increased affixes inside the second, so its effect on damage is")
    print("roughly squared while its effect on the affix budget is linear. That")
    print("is the same shape already recorded for gear upgrade level in")
    print("docs/DECISIONS.md, where +10 gear grows damage 9.58x while growing")
    print("Power Score 1.56x.\n")
    for sum_bases in (True, False):
        reading = "summed" if sum_bases else "averaged"
        at_two = two_handed_damage(2.0) / dual_wield_damage(sum_bases)
        print(f"  at 2.0 with bases {reading:<9}: the two-hander deals "
              f"{at_two:.2f}x the dual wielder")


#: Attacks per second, read off Path of Exile's base weapon table rather than
#: derived. One-handed weapons run 1.15 to 1.55 and two-handed 1.15 to 1.45, so
#: the two classes OVERLAP and a two-hander is only slightly slower.
#:
#: An earlier version of this file derived rates instead, on the assumption that
#: base damage per second should be even within a hand class. That produced 1.25
#: against 0.85, a 32% gap, which is nothing like what a shipped game uses. A
#: two-hander earns its advantage through much larger base damage, not through
#: swinging much more slowly.
ONE_HANDED_RATE = 1.35
TWO_HANDED_RATE = 1.28


def why_the_multiplier_applies_to_implicits_too() -> None:
    header("THE ANSWER: THE MULTIPLIER APPLIES TO IMPLICITS AS WELL AS AFFIXES")
    print("Last Epoch balances two-handed weapons by giving them an inherent")
    print("bonus to their affixes AND their implicit stats. In this project a")
    print("weapon's base damage IS an implicit, so one multiplier covers both.")
    print()
    print("That removes a choice this file previously presented as open. Raising")
    print("the affix multiplier alone to reach a damage edge needs about 2.75,")
    print("which hands the two-hander three affix slots the dual wielder does")
    print("not have -- the free power section VII forbids, pointed the other")
    print("way. Raising the five two-handed base damages by hand reaches the")
    print("same place but changes numbers that did not need changing. Applying")
    print("the multiplier already chosen to both does it with neither.\n")

    two_bracket = (base_damage(TWO_HANDED) * 2.0
                   + flat_damage_value() * NON_WEAPON_FLAT_DAMAGE_AFFIXES
                   + flat_damage_value(2.0))
    dual_base = sum(base_damage(n) for n in ONE_HANDED)
    dual_bracket = (dual_base
                    + flat_damage_value() * NON_WEAPON_FLAT_DAMAGE_AFFIXES
                    + flat_damage_value() * 2)

    print(f"  two-handed {TWO_HANDED}: base {base_damage(TWO_HANDED):.0f} "
          f"doubled to {base_damage(TWO_HANDED) * 2:.0f}, "
          f"full bracket {two_bracket:.0f}")
    print(f"  dual wield {' and '.join(ONE_HANDED)}: base {dual_base:.0f} summed, "
          f"full bracket {dual_bracket:.0f}")
    print()
    print("  The increased bucket is identical both ways at a multiplier of 2.0,")
    print("  so it cancels and the per-hit ratio is the brackets alone:")
    print(f"    damage per hit          {two_bracket / dual_bracket:.3f}x "
          f"to the two-hander")

    # Dual wielding averages the two weapons' rates. Both Path of Exile and Last
    # Epoch do this: Path of Exile alternates hands, which produces the average,
    # and Last Epoch states it as the arithmetic mean of the two implicits.
    per_second = ((two_bracket * TWO_HANDED_RATE)
                  / (dual_bracket * ONE_HANDED_RATE))
    print(f"    damage per second       {per_second:.3f}x  "
          f"(two-hander {TWO_HANDED_RATE}/s against a dual-wield average "
          f"of {ONE_HANDED_RATE}/s)")
    print()
    print("  So the two-hander hits considerably harder per swing and is modestly")
    print("  ahead per second, and the dual wielder holds a fourth damage type")
    print("  and a wider spread of affixes. No weapon base damage changes, and")
    print("  the affix budgets stay exactly equal at 76 slots-worth each.")
    print()
    print("  NO DEFENSIVE PENALTY ON DUAL WIELDING. Last Epoch charges 8% more")
    print("  damage taken, reduced from 9%. Rejected by the project owner")
    print("  2026-08-03, and its own forums carry threads asking for it to be")
    print("  removed, so the reception is evidence rather than only taste.")


def what_two_point_zero_actually_does() -> None:
    header("WHAT A MULTIPLIER OF 2.0 DOES, EXACTLY")
    print("At 2.0 the affix contributions are not merely close, they are")
    print("identical, and that is arithmetic rather than luck. Two weapon suffix")
    print("slots at double value is the same number as four at single value, and")
    print("the same holds for the prefixes. So at 2.0 the whole of the")
    print("two-hander's advantage is its base damage and nothing else.\n")

    two_crit = crit_factor(SUFFIXES_PER_WEAPON * 2, 2.0)
    dual_crit = crit_factor(SUFFIXES_PER_WEAPON * 4, 1.0)
    print(f"  critical strike factor, two-hander at 2.0 : {two_crit:.6f}")
    print(f"  critical strike factor, dual wield        : {dual_crit:.6f}")
    print(f"  identical                                 : "
          f"{abs(two_crit - dual_crit) < 1e-9}\n")

    averaged = two_handed_damage(2.0) / dual_wield_damage(sum_bases=False)
    print(f"  With base damage averaged, the two-hander then deals {averaged:.3f}x")
    print(f"  the dual wielder, purely from {base_damage(TWO_HANDED):.0f} base against "
          f"{sum(base_damage(n) for n in ONE_HANDED) / 2:.0f}.\n")

    print("  Three statements the design already makes, and how 2.0 lands")
    print("  against each, with base damage averaged:\n")
    print("    Section VII: two one-handers count as one piece for Power Score,")
    print("    so dual wielding is not free power        -> affix budget equal, met")
    print("    Section VI: the two-hander stays ahead on")
    print("    raw damage                                -> ahead 1.21x, met")
    print("    Section V: dual wielding is the primary")
    print("    route to multiclassing                    -> 4 damage types vs 3, met")
    print()
    print("  With base damage SUMMED, the second of those fails at 2.0: the")
    print("  two-hander falls to 0.96x and dual wielding wins both axes.")


def main() -> None:
    print(__doc__.split("\n\n")[0])
    base_damage_table()
    affix_budget_table()
    damage_table(sum_bases=False)
    damage_table(sum_bases=True)
    crit_table()
    why_it_is_not_two()
    what_two_point_zero_actually_does()
    why_the_multiplier_applies_to_implicits_too()


if __name__ == "__main__":
    main()
