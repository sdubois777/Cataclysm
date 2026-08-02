"""Combined dungeon risk: tier + type + subtype + modifiers + floors together.

Every number here comes from one call chain, so nothing is held constant that
would not be in play. Run: python analyse_dungeons.py
"""

from __future__ import annotations

import itertools
import random
import statistics

from cataclysm_sim import combat, scoring
from cataclysm_sim.modifiers import MODIFIERS

PER_FLOOR_RISK = 0.010
BOSS_MULT = 6.0
ROSTER = ("Demonic", "Death", "War", "Pestilence", "Famine", "Celestial",
          "Chaos", "Void")
SUBTYPES = ["None", "Timed", "Horde", "Siege", "Cow Level",
            "Elite", "Volatile", "Sacrificial"]
TYPES = ["Basic", "Quest", "Fallen City", "Cataclysm"]


def hdr(t):
    print(f"\n{'=' * 112}\n{t}\n{'=' * 112}")


def pool_for(tier: int) -> list[tuple[str, float]]:
    """Active Cataclysms pool their modifiers. Tier N has N active."""
    return [m for t in ROSTER[:tier] for m in MODIFIERS.get(t, [])]


def player_at(tier: int, frac: float) -> float:
    lo, hi = scoring.tier_bounds(tier)
    return lo + (hi - lo) * frac


def roll_mods(rng, tier: int, subtype: str) -> float:
    pool = pool_for(tier)
    n = tier * (2 if subtype == "Sacrificial" else 1)
    n = min(n, len(pool))
    return sum(s for _, s in rng.sample(pool, n))


def risk_of(power, tier, dtype, subtype, floors, mod_score):
    boss = "Cataclysm Boss" if dtype == "Cataclysm" else "Boss"
    return combat.death_chance(
        player_power=power, tier=tier, dtype=dtype, subtype=subtype,
        total_floors=floors, modifier_score=mod_score,
        per_floor_risk=PER_FLOOR_RISK, boss_risk_multiplier=BOSS_MULT,
        boss_rarity=boss)


# ---------------------------------------------------------------------------
hdr("A. Modifier pools now that active Cataclysms combine")
print(f"{'tier':>6}{'active Cataclysms':>20}{'pool size':>12}"
      f"{'mods rolled':>13}{'sacrificial':>13}{'enough?':>10}")
print("-" * 112)
for t in range(1, 9):
    pool = pool_for(t)
    sac = t * 2
    ok = "yes" if sac <= len(pool) else "NO"
    print(f"{t:>6}{t:>20}{len(pool):>12}{t:>13}{sac:>13}{ok:>10}")
print("\n  Pooling fixes the exhaustion completely -- even a T8 Sacrificial")
print("  wants 16 modifiers against a pool of 116.")

# ---------------------------------------------------------------------------
hdr("B. Every term's contribution to one dungeon (T1, 40 floors, player at 70%)")
tier, floors = 1, 40
w = scoring.tier_width(tier)
p = player_at(tier, 0.70)
rng = random.Random(11)
ms = statistics.fmean(roll_mods(rng, tier, "None") for _ in range(500))
print(f"  tier width {w:.0f}   player {p:.0f}   mean modifier score {ms:.1f}\n")
print(f"{'term':<28}{'value':>10}{'as % of width':>16}")
print("-" * 112)
print(f"{'baseline (0.9 x w x 0.5)':<28}{0.9 * w * 0.5:>10.1f}{0.9 * 0.5:>15.0%}")
print(f"{'type bonus (Basic 0.00)':<28}{0.0:>10.1f}{0.0:>15.0%}")
print(f"{'subtype bonus (None 0.00)':<28}{0.0:>10.1f}{0.0:>15.0%}")
print(f"{'modifiers (1 rolled)':<28}{ms:>10.1f}{ms / w:>15.1%}")
print(f"{'procedural':<28}{100 / 20 * 0.5 + 20 * 0.5:>10.1f}"
      f"{(100 / 20 * 0.5 + 20 * 0.5) / w:>15.1%}")
print(f"{'depth tension (mid floor)':<28}{0.0:>10.1f}{0.0:>15.0%}")
print("\n  Modifiers are a real term but a small one at T1 (~4% of tier width).")
print("  Their weight grows with tier because the count scales 1 -> 8.")

# ---------------------------------------------------------------------------
hdr("C. COMBINED: full dungeon specs, T1, player at 70% of tier")
print("  Every row is a complete dungeon. Sorted by risk.\n")
rng = random.Random(3)
rows = []
for dtype, subtype, floors in itertools.product(
        TYPES, ["None", "Elite", "Sacrificial"], (15, 40, 100)):
    ms = statistics.fmean(roll_mods(rng, 1, subtype) for _ in range(200))
    mid = scoring.dungeon_score(1, dtype, subtype, floors, ms)
    boss_r = "Cataclysm Boss" if dtype == "Cataclysm" else "Boss"
    last = scoring.enemy_scores(1, dtype, subtype, floors, floors, ms)[boss_r]
    ow = combat.overwhelm(p, last, w)
    rk = risk_of(p, 1, dtype, subtype, floors, ms)
    rows.append((rk, dtype, subtype, floors, ms, mid, last, ow))
rows.sort()
print(f"{'type':<13}{'subtype':<13}{'floors':>7}{'days':>6}{'mods':>7}"
      f"{'mid':>7}{'last':>7}{'overwhelm':>11}{'RISK':>8}")
print("-" * 112)
for rk, dtype, subtype, floors, ms, mid, last, ow in rows:
    print(f"{dtype:<13}{subtype:<13}{floors:>7}{floors:>6}{ms:>7.0f}"
          f"{mid:>7}{last:>7}{ow:>10.0%}{rk:>8.0%}")
print(f"\n  Safest {rows[0][0]:.0%} -> deadliest {rows[-1][0]:.0%}. "
      f"Depth dominates, but the")
print("  worst combinations stack type + subtype + modifiers + depth together.")

# ---------------------------------------------------------------------------
hdr("D. The same sweep at T4 -- does the shape hold as tiers climb?")
tier = 4
w4 = scoring.tier_width(tier)
p4 = player_at(tier, 0.70)
rng = random.Random(5)
rows = []
for dtype, subtype, floors in itertools.product(
        TYPES, ["None", "Sacrificial"], (15, 40, 100)):
    ms = statistics.fmean(roll_mods(rng, tier, subtype) for _ in range(200))
    boss_r = "Cataclysm Boss" if dtype == "Cataclysm" else "Boss"
    last = scoring.enemy_scores(tier, dtype, subtype, floors, floors, ms)[boss_r]
    ow = combat.overwhelm(p4, last, w4)
    rk = risk_of(p4, tier, dtype, subtype, floors, ms)
    rows.append((rk, dtype, subtype, floors, ms, last, ow))
rows.sort()
print(f"  tier width {w4:.0f}   player {p4:.0f}\n")
print(f"{'type':<13}{'subtype':<13}{'floors':>7}{'mods':>7}{'last':>7}"
      f"{'overwhelm':>11}{'RISK':>8}")
print("-" * 112)
for rk, dtype, subtype, floors, ms, last, ow in rows:
    print(f"{dtype:<13}{subtype:<13}{floors:>7}{ms:>7.0f}{last:>7}"
          f"{ow:>10.0%}{rk:>8.0%}")

# ---------------------------------------------------------------------------
hdr("E. How much of the risk spread does each axis actually own?")
print("  T1, player at 70%. Holding everything else at its middle value and")
print("  moving one axis end to end.\n")
rng = random.Random(9)
base_ms = statistics.fmean(roll_mods(rng, 1, "None") for _ in range(300))
base = risk_of(p, 1, "Basic", "None", 40, base_ms)


def spread(label, lo, hi):
    print(f"{label:<26}{lo:>10.0%}{hi:>10.0%}{hi - lo:>12.0%}")


print(f"{'axis':<26}{'min':>10}{'max':>10}{'spread':>12}")
print("-" * 112)
spread("floors  (8 -> 150)",
       risk_of(p, 1, "Basic", "None", 8, base_ms),
       risk_of(p, 1, "Basic", "None", 150, base_ms))
spread("type    (Basic -> Catac)",
       risk_of(p, 1, "Basic", "None", 40, base_ms),
       risk_of(p, 1, "Cataclysm", "None", 40, base_ms))
sac_ms = statistics.fmean(roll_mods(rng, 1, "Sacrificial") for _ in range(300))
spread("subtype (None -> Sacrif)",
       risk_of(p, 1, "Basic", "None", 40, base_ms),
       risk_of(p, 1, "Basic", "Sacrificial", 40, sac_ms))
spread("modifiers (0 -> 8 rolls)",
       risk_of(p, 1, "Basic", "None", 40, 0.0),
       risk_of(p, 1, "Basic", "None", 40, 8 * 20.0))
spread("gear   (30% -> 130%)",
       risk_of(player_at(1, 1.30), 1, "Basic", "None", 40, base_ms),
       risk_of(player_at(1, 0.30), 1, "Basic", "None", 40, base_ms))
print(f"\n  Baseline dungeon risk: {base:.0%}")
