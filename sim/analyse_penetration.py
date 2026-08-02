"""Underpowered penetration: what the proposed rule actually does.

Proposal: enemies above your Power Score gain resistance penetration,
2% per 50 points of difference.

Run: python analyse_penetration.py
"""

from cataclysm_sim import scoring as S

RESIST_CAP = 0.70          # GDD: resistances cap at 70%
FLAT_PER = 50.0            # proposed: 2% per 50 power difference
FLAT_RATE = 0.02


def dmg_multiplier(pen: float, base_resist: float = RESIST_CAP) -> float:
    """How much harder you get hit, vs sitting at the resist cap."""
    eff = max(0.0, base_resist - pen)
    return (1.0 - eff) / (1.0 - base_resist)


def hdr(t):
    print(f"\n{'=' * 94}\n{t}\n{'=' * 94}")


hdr("A. The worked example: 200 power vs a 600 enemy")
gap = 400
pen = (gap / FLAT_PER) * FLAT_RATE
print(f"  gap                 = {gap}")
print(f"  penetration         = {pen:.0%}")
print(f"  your resist         = 70% -> {70 - pen * 100:.0f}%")
print(f"  damage you take     = {1 - RESIST_CAP:.0%} -> {1 - (RESIST_CAP - pen):.0%}"
      f"   ({dmg_multiplier(pen):.2f}x harder)")
print("\n  A 3x power gap costs you 53% more incoming damage. That is a speed")
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
for t in range(1, 9):
    w = S.tier_width(t)
    pmax = S.PLAYER_MAX_SCORES[t]
    boss = S.final_boss_score(t, "Cataclysm", "None", 125, rarity="Cataclysm Boss")
    gap = boss - pmax
    pen = min(RESIST_CAP, (gap / FLAT_PER) * FLAT_RATE)
    print(f"{t:>5}{w:>8.0f}{pmax:>12}{boss:>12}{gap:>8.0f}"
          f"{pen:>10.0%}{dmg_multiplier(pen):>7.2f}x{gap / w:>10.2f}x")
print("\n  A maxed T1 player eats 13% pen at their final boss.")
print("  A maxed T8 player eats 47% -- three and a half times as much punishment")
print("  for the same 'I am at the cap and fighting the boss' situation.")
print("  The flat 50-point step is worth 17% of a T1 tier but only 6% of a T8 one.")

hdr("D. Fix: rate the gap against TIER WIDTH, not against a flat 50")
print("  Rule: 2% penetration per 10% of tier width you are under.\n")
print(f"{'tier':>5}{'gap/width':>11}{'relative pen':>14}{'dmg x':>8}"
      f"{'    (flat pen for comparison)':>32}")
print("-" * 94)
REL_RATE = 0.02 / 0.10      # 2% pen per 10% of width
for t in range(1, 9):
    w = S.tier_width(t)
    pmax = S.PLAYER_MAX_SCORES[t]
    boss = S.final_boss_score(t, "Cataclysm", "None", 125, rarity="Cataclysm Boss")
    gap = boss - pmax
    rel = min(RESIST_CAP, (gap / w) * REL_RATE)
    flat = min(RESIST_CAP, (gap / FLAT_PER) * FLAT_RATE)
    print(f"{t:>5}{gap / w:>10.2f}x{rel:>14.0%}{dmg_multiplier(rel):>7.2f}x"
          f"{flat:>26.0%}")
print("\n  Consistent 20-30% across every tier instead of 13% -> 47%.")
print("  Still uneven because the tier widths themselves are jagged -- this")
print("  mechanic inherits that problem rather than causing it.")

hdr("E. Ordinary content, not just the final boss (relative rule)")
print("  A player sitting at 70% of their tier, fighting a mid-floor Basic.\n")
print(f"{'tier':>5}{'player @70%':>13}{'Basic-50 mid':>14}{'gap':>8}"
      f"{'pen':>8}{'dmg x':>8}")
print("-" * 94)
for t in range(1, 9):
    lo, hi = S.tier_bounds(t)
    w = hi - lo
    p = lo + w * 0.70
    mid = S.dungeon_score(t, "Basic", "None", 50)
    gap = max(0.0, mid - p)
    pen = min(RESIST_CAP, (gap / w) * REL_RATE)
    print(f"{t:>5}{p:>13.0f}{mid:>14}{gap:>8.0f}{pen:>8.0%}"
          f"{dmg_multiplier(pen):>7.2f}x")
print("\n  Routine content barely triggers it, which is correct -- the mechanic")
print("  should only bite when you are genuinely punching above your weight.")
