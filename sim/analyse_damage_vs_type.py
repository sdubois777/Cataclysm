"""What specialising damage against one enemy type is actually worth.

WHY THIS EXISTS. Issue #234. The eight "increased damage against X enemies"
affixes give 400% at affix tier T7, against the generic "Increased damage"
affix's 125%. That ratio of 3.2 is derived from what this project already pays
for narrowing a modifier from all eight damage types to one, and is not in
question. What is in question is the MAGNITUDE, because a resistance caps at 70%
and increased damage caps at nothing.

The issue named three things worth measuring and could not measure them. This
file measures all three, and every figure it prints is computed from
`cataclysm_sim/affixes.py` and `cataclysm_sim/enemy_stats.py` rather than typed.

WHAT IT FOUND, in one line each. The full working is in the output.

    The 2.83x figure the issue quotes is the bare case, and no real build is
    bare. With the reference build's other six increased damage affixes present,
    the same four slots swing 1.81x, because increases are additive and a larger
    shared bracket dilutes any addition to it.

    The crossover is NOT at 3.2 active Cataclysms, and it is LOWER, which means
    the specialist stops being worth taking sooner than the arithmetic
    suggested. 3.2 is where the two builds deal equal AVERAGE DAMAGE. Time to
    kill is proportional to one over damage, and averaging a reciprocal is not
    the reciprocal of the average, so the crossover on hits to kill is 1.76 at
    the reference build.

    The one-Cataclysm case is where the swing is largest, and one Cataclysm is
    where every run starts.

WHAT THIS DOES NOT DO. It does not say whether 400% is right. That is a question
about how the game feels and it needs play, which is why issue #234 exists and
why this file reports rather than recommends.

Run: python analyse_damage_vs_type.py
"""

from cataclysm_sim import affixes as A
from cataclysm_sim import enemy_stats

#: The comparison issue #234 sets up: four prefix slots, spent one way or the
#: other. Both affixes occupy the same slots and the same position, so they
#: compete for one prefix on one piece and a player cannot have both.
SLOTS = 4

#: The difficulty tier every figure below is measured at. Tier 8 is where the
#: design document fixes its damage target, so it is the tier with a stated
#: right answer to compare against.
TIER = 8

#: How many other increased damage affixes the build already carries. The bare
#: case is what issue #234 quotes; the reference case is the build every other
#: damage number in `affixes.py` is fitted against.
BARE = 0
REFERENCE = A.REFERENCE_INCREASED_DAMAGE_AFFIXES


def generic_per_slot() -> float:
    """Increase from one generic damage affix, as a fraction."""
    return A.INCREASED_DAMAGE.value_at(7) / 100.0


def specific_per_slot() -> float:
    """Increase from one type-specific damage affix, as a fraction."""
    return A.DAMAGE_VS_TOP_VALUE / 100.0


def bracket(other_slots: int, matching: bool, specialised: bool) -> float:
    """The `(1 + increases)` multiplier for one build against one enemy.

    @param other_slots  generic increased damage affixes the build already has
    @param matching     whether the enemy is the type the build specialised in
    @param specialised  whether the four slots went to type-specific affixes
    """
    increases = other_slots * generic_per_slot()
    if specialised:
        if matching:
            increases += SLOTS * specific_per_slot()
    else:
        increases += SLOTS * generic_per_slot()
    return 1.0 + increases


def average_hits(other_slots: int, specialised: bool, cataclysms: int,
                 base: float, health: float) -> float:
    """Hits to kill an average enemy, averaged over the active Cataclysms.

    HITS, NOT DAMAGE, and the difference is not cosmetic. Hits to kill is
    proportional to one over damage, and the average of a reciprocal is not the
    reciprocal of the average. Averaging damage lets a huge number against one
    enemy type carry the average; averaging hits does not, because no amount of
    extra damage takes the time below zero while a small number against seven
    other types takes it up without limit. Clearing a floor is a sum of times,
    so time is the quantity to average. Section C prints both and they disagree.

    THIS DOES NOT FLOOR AT ONE HIT. Below one hit a kill is still one swing, so
    where the table prints less than 1.00 the real figure is 1.00 and the model
    flatters the build. That happens only at the reference build with one active
    Cataclysm, and it flatters the specialist, so every conclusion drawn here
    about the specialist being too strong is conservative.
    """
    total = 0.0
    for i in range(cataclysms):
        m = bracket(other_slots, matching=(i == 0), specialised=specialised)
        total += health / (base * m)
    return total / cataclysms


def average_damage(other_slots: int, specialised: bool,
                   cataclysms: int, base: float) -> float:
    """Damage per hit, averaged over the active Cataclysms."""
    total = 0.0
    for i in range(cataclysms):
        total += base * bracket(other_slots,
                                matching=(i == 0), specialised=specialised)
    return total / cataclysms


def crossover_cataclysms(other_slots: int, base: float, health: float) -> float:
    """Where the two builds are equal, in active Cataclysms, on hits to kill.

    Solved rather than searched, so the answer is exact rather than the first
    integer that happens to flip. With G the generalist's multiplier, M the
    specialist's against its own type and S against everything else:

        1/G = (1/N)(1/M) + ((N-1)/N)(1/S)

    which rearranges to N = (1/M - 1/S) / (1/G - 1/S). Health and the base
    bracket cancel, so the crossover does not depend on either.
    """
    g = bracket(other_slots, matching=True, specialised=False)
    m = bracket(other_slots, matching=True, specialised=True)
    s = bracket(other_slots, matching=False, specialised=True)
    denominator = 1.0 / g - 1.0 / s
    if denominator == 0:
        return float("inf")
    return (1.0 / m - 1.0 / s) / denominator


def crossover_on_damage(other_slots: int) -> float:
    """The same crossover measured on average damage instead of average hits.

    This is the arithmetic the issue's 3.2 comes from: the specialist's whole
    bonus divided by the generalist's, since averaging damage makes the
    specialist's contribution linear in 1/N.
    """
    return specific_per_slot() / generic_per_slot()


def main() -> None:
    common = enemy_stats.stats_on_floor("Common", TIER, "Cataclysm")
    health = common.effective_health
    base = A.reference_weapon_base(TIER) + (
        A.FLAT_DAMAGE.value_at(7) * A.REFERENCE_FLAT_DAMAGE_AFFIXES)
    target = A.damage_target(TIER)

    print("=" * 78)
    print("WHAT SPECIALISING DAMAGE AGAINST ONE ENEMY TYPE IS WORTH")
    print("=" * 78)
    print()
    print(f"  Issue #234. Measured at difficulty tier {TIER}, at affix tier T7,")
    print(f"  on {SLOTS} prefix slots, against an average Common enemy.")
    print()
    print(f"    generic 'Increased damage'            "
          f"{generic_per_slot() * 100:>7.0f}% per slot")
    print(f"    'Increased damage against X enemies'  "
          f"{specific_per_slot() * 100:>7.0f}% per slot")
    print(f"    ratio                                 "
          f"{specific_per_slot() / generic_per_slot():>8.2f}")
    print()
    print(f"    average Common enemy health           {health:>8,.0f}")
    print(f"    damage one hit has to do              {target:>8,.0f}")
    print(f"    base bracket before increases         {base:>8,.0f}")
    print()

    print("=" * 78)
    print("A. THE SWING SHRINKS AS THE BUILD FILLS UP")
    print("=" * 78)
    print()
    print("  Increases are additive, so the four slots in question land in the")
    print("  same bracket as every other increase the build carries. The larger")
    print("  that bracket already is, the less any addition to it changes.")
    print()
    print(f"  {'other increased affixes':<26}{'generalist':>12}"
          f"{'specialist':>12}{'ratio':>9}")
    print("  " + "-" * 59)
    for other in range(0, 9):
        g = bracket(other, matching=True, specialised=False)
        m = bracket(other, matching=True, specialised=True)
        mark = ""
        if other == BARE:
            mark = "   <- the case issue #234 quotes"
        elif other == REFERENCE:
            mark = "   <- the reference build"
        print(f"  {other:<26}{'x' + format(g, '.2f'):>12}"
              f"{'x' + format(m, '.2f'):>12}{m / g:>9.2f}{mark}")
    print()
    bare_ratio = (bracket(BARE, True, True) / bracket(BARE, True, False))
    ref_ratio = (bracket(REFERENCE, True, True) / bracket(REFERENCE, True, False))
    print(f"  The bare case swings {bare_ratio:.2f}x. The reference build, which is the")
    print("  build every other damage number in affixes.py is fitted against,")
    print(f"  swings {ref_ratio:.2f}x from the same four slots.")
    print()
    print("  So the headline figure overstates what a geared character sees, and")
    print("  understates what a character sees early, when the bracket is empty.")
    print()

    print("=" * 78)
    print("B. HITS TO KILL, AT ONE ACTIVE CATACLYSM AND AT EIGHT")
    print("=" * 78)
    print()
    print("  The first thing issue #234 asked for. Averaged over the active")
    print("  Cataclysms, because a run faces all of them and the player does not")
    print("  choose which enemy walks into the room.")
    print()
    for other, label in ((BARE, "bare"), (REFERENCE, "reference build")):
        print(f"  With {other} other increased damage affixes ({label}):")
        print(f"    {'active Cataclysms':<20}{'generalist':>12}"
              f"{'specialist':>12}{'specialist is':>15}")
        print("    " + "-" * 57)
        for n in (1, 2, 3, 4, 8):
            gh = average_hits(other, False, n, base, health)
            sh = average_hits(other, True, n, base, health)
            verdict = "ahead" if sh < gh else "behind"
            print(f"    {n:<20}{gh:>12.2f}{sh:>12.2f}"
                  f"{verdict + ' ' + format(gh / sh, '.2f') + 'x':>15}")
        print()

    print("=" * 78)
    print("C. WHERE THE CROSSOVER ACTUALLY LANDS")
    print("=" * 78)
    print()
    print("  The second thing issue #234 asked for. It expected 3.2, which is")
    print("  the ratio of the two affix values. That is the answer only if the")
    print("  comparison is made on average DAMAGE.")
    print()
    print(f"  {'other increased affixes':<26}{'on hits':>12}{'on damage':>12}")
    print("  " + "-" * 50)
    for other in (BARE, REFERENCE, 8):
        print(f"  {other:<26}{crossover_cataclysms(other, base, health):>12.2f}"
              f"{crossover_on_damage(other):>12.2f}")
    print()
    print("  Time to kill is one over damage, and the average of a reciprocal")
    print("  is not the reciprocal of the average. Averaging damage lets a huge")
    print("  number against one type carry the average. Averaging time does not,")
    print("  because extra damage cannot take a kill below zero seconds while")
    print("  weak damage against seven other types raises it without limit.")
    print("  Clearing a floor is a sum of times, so time is what to average.")
    print()
    hits_cross = crossover_cataclysms(REFERENCE, base, health)
    damage_cross = crossover_on_damage(REFERENCE)
    print(f"  On time, at the reference build, the crossover is "
          f"{hits_cross:.2f} active")
    print(f"  Cataclysms, not {damage_cross:.2f}. It is LOWER, so the specialist stops")
    print("  being worth taking SOONER than the affix ratio suggested, not later.")
    print(f"  Past {hits_cross:.2f} Cataclysms the four generic slots clear faster.")
    print()
    print("  That cuts against the worry issue #234 was filed with. The affix is")
    print("  strong where one Cataclysm is active and stops paying at two, and a")
    print("  run adds a Cataclysm every time one is defeated.")
    print()
    print("  Two things push the crossover back up and neither is modelled here:")
    print("    Enemies may not be drawn evenly across the active Cataclysms.")
    print("    The player chooses which dungeon to enter.")
    print("  Both favour the specialist, so the figures above are a lower bound")
    print("  on how long specialising keeps paying.")
    print()

    print("=" * 78)
    print("D. THE ONE-CATACLYSM CASE, WHICH IS WHERE EVERY RUN STARTS")
    print("=" * 78)
    print()
    print("  The third thing issue #234 asked for. At one active Cataclysm every")
    print("  enemy matches, so the specialist pays no penalty at all and the")
    print("  swing is at its largest.")
    print()
    for other, label in ((BARE, "bare"), (REFERENCE, "reference build")):
        gh = average_hits(other, False, 1, base, health)
        sh = average_hits(other, True, 1, base, health)
        print(f"    {label:<18} generalist {gh:.2f} hits, "
              f"specialist {sh:.2f} hits, {gh / sh:.2f}x")
    print()
    intended = A.HITS_TO_KILL_A_COMMON_ENEMY
    print(f"  The design document fixes {intended:.0f} non-critical hits as what an")
    print("  average Common enemy at this tier should take. Against that:")
    print()
    for other, label in ((BARE, "bare"), (REFERENCE, "reference build")):
        for specialised, who in ((False, "generalist"), (True, "specialist")):
            h = average_hits(other, specialised, 1, base, health)
            print(f"    {label:<18}{who:<12}{h:>6.2f} hits"
                  f"   {intended / h:>5.2f}x faster than intended")
    print()
    print("  Faster than intended is not the same as wrong: the intended figure")
    print("  is for a character with no damage prefixes at all beyond the")
    print("  reference set, and spending four more slots on damage is supposed")
    print("  to pay. What the numbers show is the SIZE of the payment, which is")
    print("  the thing issue #234 says has to be judged against play.")
    print()

    print("=" * 78)
    print("WHAT THIS DOES NOT SETTLE")
    print("=" * 78)
    print()
    print("  Nothing here says 400% is too high or too low. Issue #234 exists")
    print("  because that is a feel question, and this file is the measurement")
    print("  it asked for, not an answer to it.")
    print()
    print("  If the value moves, DAMAGE_VS_TOP_VALUE in cataclysm_sim/affixes.py")
    print("  is the single definition, and the eight rows on the Affixes sheet of")
    print("  docs/All_Things_Cataclysm.xlsx, game/Data/Affixes.csv,")
    print("  game/Content/Data/DT_Affixes.uasset and the 'Damage Against a")
    print("  Target's Type' section of docs/Cataclysm_GDD_v2.md all move with it.")
    print("  tools/tests/test_damage_against_a_target_type.py fails on a partial")
    print("  change rather than letting the copies drift.")
    print()


# Called unconditionally, like the other analyse_*.py scripts, so that importing
# this file IS running the report. `sim/tests/test_analysis_scripts.py` executes
# it with `runpy.run_path`, which does not set `__name__` to `"__main__"`, so a
# guard here would make the whole report invisible to its own tests.
main()
