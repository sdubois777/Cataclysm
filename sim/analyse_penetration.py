"""Underpowered penetration: what the proposed rule would have done.

THIS ANALYSES A REJECTED PROPOSAL, and the rejection went the way this file
recommended. Read section D first.

The proposal was: enemies above your Power Score gain resistance penetration,
2% per 50 points of difference. Section C measures what a flat point step does
across the eight tiers and section D proposes rating the gap against tier width
instead. That second shape is what the project built, as Overwhelm in
`cataclysm_sim/combat.py`, and on 2026-08-03 `docs/DECISIONS.md` recorded that
enemies carry no Penetration stat at all because Overwhelm already does the job
and does it better -- it shrinks as the player out-powers the content, and it
strips armour, block and evasion rather than resistance alone.

So nothing here describes a live mechanic. It is kept because it is the
measurement the decision was made on, and section F now runs the same comparison
against the rule that shipped.

Run: python analyse_penetration.py

Every figure in the prose is computed. Issue #6: the four numbers under section C
were worked out against the player power anchors issue #2 replaced, and they were
copied from here into the module docstring of `cataclysm_sim/combat.py`, where
they justified a live design decision while being wrong.
"""

from cataclysm_sim import combat, scoring as S

#: The mitigation a geared player is assumed to be sitting on. Read from the
#: model rather than typed, because the design document's resistance cap is one
#: number in one place.
RESIST_CAP = combat.BASE_MITIGATION

#: The rejected proposal's own constants. Nothing else in the project reads
#: these; they exist so section C can measure what the rule would have done.
FLAT_PER = 50.0            # proposed: 2% per 50 power difference
FLAT_RATE = 0.02

#: The dungeon every tier comparison below is made against.
DTYPE, SUBTYPE, BOSS = "Cataclysm", "None", "Cataclysm Boss"
FLOORS = 125

#: How much harder you get hit, versus sitting at the mitigation cap. The same
#: function the shipped mechanic uses, so the two rules are compared on one
#: scale rather than on two copies of one formula.
dmg_multiplier = combat.damage_multiplier


def catac_boss(tier: int) -> int:
    return S.final_boss_score(tier, DTYPE, SUBTYPE, FLOORS, rarity=BOSS)


def hdr(t):
    print(f"\n{'=' * 94}\n{t}\n{'=' * 94}")


EXAMPLE_PLAYER, EXAMPLE_ENEMY = 200, 600
hdr(f"A. The worked example: {EXAMPLE_PLAYER} power vs a {EXAMPLE_ENEMY} enemy")
gap = EXAMPLE_ENEMY - EXAMPLE_PLAYER
pen = (gap / FLAT_PER) * FLAT_RATE
print(f"  gap                 = {gap}")
print(f"  penetration         = {pen:.0%}")
print(f"  your resist         = {RESIST_CAP:.0%} -> {RESIST_CAP - pen:.0%}")
print(f"  damage you take     = {1 - RESIST_CAP:.0%} -> {1 - (RESIST_CAP - pen):.0%}"
      f"   ({dmg_multiplier(pen):.2f}x harder)")
print(f"\n  A {EXAMPLE_ENEMY / EXAMPLE_PLAYER:.0f}x power gap costs you "
      f"{dmg_multiplier(pen) - 1:.0%} more incoming damage. That is a speed")
print("  bump, not the 'scary' the rule is reaching for.")

hdr("B. How much penetration would actually feel dangerous?")
print(f"{'pen':>8}{'your resist':>14}{'dmg taken':>12}{'vs capped':>12}")
print("-" * 94)
for pen in (0.0, 0.10, 0.16, 0.25, 0.35, 0.50, 0.70):
    print(f"{pen:>8.0%}{RESIST_CAP - pen:>14.0%}{1 - (RESIST_CAP - pen):>12.0%}"
          f"{dmg_multiplier(pen):>11.2f}x")
print("\n  'Scary but possible' is around 2.0-2.7x, i.e. 30-50% penetration.")

hdr("C. The problem: a flat rate does not scale with tier")
print("  Every tier's final boss, fought by a MAXED player of that tier.\n")
print(f"{'tier':>5}{'width':>8}{'player max':>12}{'Catac boss':>12}{'gap':>8}"
      f"{'flat pen':>10}{'dmg x':>8}{'gap/width':>11}")
print("-" * 94)
flat_pen = {}
for t in range(1, 9):
    w = S.tier_width(t)
    pmax = S.PLAYER_MAX_SCORES[t]
    boss = catac_boss(t)
    gap = boss - pmax
    pen = min(RESIST_CAP, (gap / FLAT_PER) * FLAT_RATE)
    flat_pen[t] = pen
    print(f"{t:>5}{w:>8.0f}{pmax:>12}{boss:>12}{gap:>8.0f}"
          f"{pen:>10.0%}{dmg_multiplier(pen):>7.2f}x{gap / w:>10.2f}x")
low, high = flat_pen[1], flat_pen[8]
print(f"\n  A maxed T1 player eats {low:.0%} pen at their final boss.")
print(f"  A maxed T8 player eats {high:.0%} -- {high / low:.1f} times as much "
      f"punishment")
print("  for the same 'I am at the cap and fighting the boss' situation.")
print(f"  The flat {FLAT_PER:.0f}-point step is worth "
      f"{FLAT_PER / S.tier_width(1):.0%} of a T1 tier but only "
      f"{FLAT_PER / S.tier_width(8):.0%} of a T8 one.")

REL_RATE = 0.02 / 0.10      # 2% pen per 10% of width
hdr(f"D. Fix: rate the gap against TIER WIDTH, not against a flat {FLAT_PER:.0f}")
print(f"  Rule: {FLAT_RATE:.0%} penetration per "
      f"{FLAT_RATE / REL_RATE:.0%} of tier width you are under.\n")
print(f"{'tier':>5}{'gap/width':>11}{'relative pen':>14}{'dmg x':>8}"
      f"{'    (flat pen for comparison)':>32}")
print("-" * 94)
rel_pen = {}
for t in range(1, 9):
    w = S.tier_width(t)
    gap = catac_boss(t) - S.PLAYER_MAX_SCORES[t]
    rel = min(RESIST_CAP, (gap / w) * REL_RATE)
    rel_pen[t] = rel
    print(f"{t:>5}{gap / w:>10.2f}x{rel:>14.0%}{dmg_multiplier(rel):>7.2f}x"
          f"{flat_pen[t]:>26.0%}")
print(f"\n  Between {min(rel_pen.values()):.0%} and {max(rel_pen.values()):.0%} "
      f"across every tier instead of {low:.0%} -> {high:.0%}.")
print("  Still uneven because the tier widths themselves are jagged -- this")
print("  mechanic inherits that problem rather than causing it.")

GEARED_FRACTION = 0.70
hdr("E. Ordinary content, not just the final boss (relative rule)")
print(f"  A player sitting at {GEARED_FRACTION:.0%} of their tier, fighting a "
      f"mid-floor Basic.\n")
print(f"{'tier':>5}{f'player @{GEARED_FRACTION:.0%}':>13}"
      f"{'Basic-50 mid':>14}{'gap':>8}{'pen':>8}{'dmg x':>8}")
print("-" * 94)
ordinary = {}
for t in range(1, 9):
    lo, hi = S.tier_bounds(t)
    w = hi - lo
    p = lo + w * GEARED_FRACTION
    mid = S.dungeon_score(t, "Basic", "None", 50)
    gap = max(0.0, mid - p)
    pen = min(RESIST_CAP, (gap / w) * REL_RATE)
    ordinary[t] = pen
    print(f"{t:>5}{p:>13.0f}{mid:>14}{gap:>8.0f}{pen:>8.0%}"
          f"{dmg_multiplier(pen):>7.2f}x")
worst = max(ordinary.values())
print(f"\n  Routine content reaches {worst:.0%} penetration at worst, which is "
      f"correct -- the")
print("  mechanic should only bite when you are genuinely punching above your")
print("  weight.")

hdr("F. What was actually adopted: Overwhelm, in cataclysm_sim/combat.py")
print("  Section D's shape won. The rule that shipped rates the gap against")
print(f"  tier width at {combat.OVERWHELM_RATE:.0%} per 1.0x of shortfall, "
      f"capped at {combat.OVERWHELM_CAP:.0%},")
print("  and strips every kind of mitigation rather than resistance alone.")
print("  Same maxed-player-at-their-own-final-boss situation as section C.\n")
print(f"{'tier':>5}{'gap/width':>11}{'overwhelm':>11}{'dmg x':>8}"
      f"{'  section D':>12}{'  section C (flat)':>19}")
print("-" * 94)
shipped = {}
for t in range(1, 9):
    w = S.tier_width(t)
    pmax = S.PLAYER_MAX_SCORES[t]
    ow = combat.overwhelm(pmax, catac_boss(t), w)
    shipped[t] = ow
    print(f"{t:>5}{(catac_boss(t) - pmax) / w:>10.2f}x{ow:>11.0%}"
          f"{dmg_multiplier(ow):>7.2f}x{rel_pen[t]:>12.0%}{flat_pen[t]:>19.0%}")
print(f"\n  Overwhelm runs {min(shipped.values()):.0%} to "
      f"{max(shipped.values()):.0%} across the eight tiers, against "
      f"{min(rel_pen.values()):.0%} to {max(rel_pen.values()):.0%}")
print("  for section D's proposal and "
      f"{low:.0%} to {high:.0%} for the flat rule. Both tier-width")
print("  rules behave the same way at every tier; the flat one does not.")
