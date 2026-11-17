"""What the DungeonSimulator scoring model implies about player-vs-content.

Run: python analyse_scoring.py

Every figure in the prose under a table is computed from the model, not typed.
Issue #6: this file spent five months printing conclusions worked out against
the player power anchors issue #2 replaced, and nothing flagged it because a
sentence in a print statement cannot go out of date noisily. Keep it that way --
if you want to state a number here, derive it.
"""

import math

from cataclysm_sim import scoring as S

#: The example this file walks through in detail. A Cataclysm dungeon at the
#: shallowest tier, at the depth `experiments.py` runs one at.
EXAMPLE_TIER = 1
EXAMPLE_FLOORS = 125

#: Depths sampled to show that floor count is a time lever, not a difficulty one.
DEPTHS = (8, 15, 25, 40, 60, 100, 150)


def hdr(t):
    print(f"\n{'=' * 88}\n{t}\n{'=' * 88}")


hdr("A. Can a MAXED player of tier N beat tier N's content?")
print("  'player max' is the anchor from playerMaxScores.")
print("  Every other column is what actually stands on the last floor.\n")
print(f"{'tier':>5}{'player max':>12}{'Basic-50 boss':>15}{'Basic-50 mid':>14}"
      f"{'Catac-125 boss':>16}{'boss/player':>13}")
print("-" * 88)
for t in range(1, 9):
    pmax = S.PLAYER_MAX_SCORES[t]
    basic_boss = S.final_boss_score(t, "Basic", "None", 50, rarity="Boss")
    basic_mid = S.dungeon_score(t, "Basic", "None", 50)
    cat_boss = S.final_boss_score(t, "Cataclysm", "None", 125, rarity="Cataclysm Boss")
    print(f"{t:>5}{pmax:>12}{basic_boss:>15}{basic_mid:>14}{cat_boss:>16}"
          f"{cat_boss / pmax:>12.2f}x")

hdr(f"B. Where the Cataclysm Boss number comes from "
    f"(T{EXAMPLE_TIER}, {EXAMPLE_FLOORS} floors)")
w = S.tier_width(EXAMPLE_TIER)
middle = math.ceil(EXAMPLE_FLOORS / 2)
base_w = S.BASELINE_WEIGHT
type_w = S.TYPE_WEIGHTS["Cataclysm"]
rarity_w = S.RARITY_WEIGHTS["Cataclysm Boss"]
scaling = S.FLOOR_SCALING_BASES["Cataclysm"] / S.PROCEDURAL_DIVISOR
procedural = scaling + EXAMPLE_FLOORS * S.PROCEDURAL_PER_FLOOR
tension = (EXAMPLE_FLOORS - middle) * EXAMPLE_TIER * S.DEPTH_TENSION_PER_TIER
weighted = base_w + type_w + rarity_w


def term(label: str, value: float, note: str = "") -> None:
    print(f"  {label:<30} = {value:>8.1f}{note}")


print(f"  {'tier width':<30} = {w:>8.0f}")
term(f"baseline   {base_w:.2f} x width", base_w * w)
term(f"type bonus {type_w:.2f} x width", type_w * w, "   (Cataclysm)")
term(f"rarity     {rarity_w:.2f} x width", rarity_w * w, "   (Cataclysm Boss)")
term(f"procedural {S.FLOOR_SCALING_BASES['Cataclysm']:.0f}"
     f"/{S.PROCEDURAL_DIVISOR:.0f} + {EXAMPLE_FLOORS}"
     f"*{S.PROCEDURAL_PER_FLOOR}", procedural)
term(f"depth      ({EXAMPLE_FLOORS}-{middle}) * {EXAMPLE_TIER}"
     f" * {S.DEPTH_TENSION_PER_TIER}", tension)
print(f"  {'-' * 40}")
term("total", weighted * w + procedural + tension)
print(f"\n  The weighted terms alone sum to {weighted:.2f} x tier width, but a "
      f"player can")
print("  only gain 1.00 x tier width across the entire tier. The gap is")
print("  structural, not a tuning accident.")

hdr("C. Tier width -- the master scalar in every single term")
prev = None
for t in range(1, 9):
    wd = S.tier_width(t)
    d = "" if prev is None else f"{wd / prev:+.2f}x"
    flag = ""
    if prev and (wd / prev < 0.9 or wd / prev > 1.5):
        flag = "   <-- discontinuity"
    print(f"  T{t}: max={S.PLAYER_MAX_SCORES[t]:>5}  width={wd:>5.0f}  {d:>7}{flag}")
    prev = wd

hdr("D. GDD 'Power Score Ranges by Tier' vs DungeonSimulator anchors")
#: Transcribed by hand from the "Power Score Ranges by Tier" table in
#: docs/Cataclysm_GDD_v2.md, read 2026-08-05. That table is a second copy of the
#: anchors inside the same document and nothing tests it, so this transcription
#: is checked against the model by sim/tests/test_analysis_scripts.py and the
#: document itself is issue #253.
GDD = {1: (0, 385), 2: (386, 883), 3: (884, 1508), 4: (1509, 2225),
       5: (2226, 3078), 6: (3079, 4057), 7: (4058, 5120), 8: (5121, 6327)}
print(f"{'tier':>5}{'GDD range':>18}{'sim max':>10}{'sim in GDD range?':>20}")
print("-" * 88)
for t in range(1, 9):
    lo, hi = GDD[t]
    m = S.PLAYER_MAX_SCORES[t]
    ok = "yes" if lo <= m <= hi else "NO"
    print(f"{t:>5}{f'{lo}-{hi}':>18}{m:>10}{ok:>20}")

hdr("E. Depth is length, not difficulty")
print(f"  T{EXAMPLE_TIER} Basic, Dungeon Score at the middle floor:\n")
by_depth = {f: S.dungeon_score(EXAMPLE_TIER, "Basic", "None", f)
            for f in DEPTHS}
for f, score in by_depth.items():
    print(f"    {f:>4} floors -> {score:>4}   ({f} days to run)")
shallow, deep = DEPTHS[0], DEPTHS[-1]
print(f"\n  {deep / shallow:.1f}x the depth buys "
      f"{by_depth[deep] / by_depth[shallow] - 1:.0%} more difficulty. Floor "
      f"count is a TIME and")
print("  LOOT lever, not a difficulty lever.")

hdr("F. Early floors score negative")
SHALLOW_FLOORS = 50
print(f"  T{EXAMPLE_TIER} Basic, {SHALLOW_FLOORS} floors:\n")
for f in (1, 3, 5, 10, 25):
    s = S.enemy_scores(EXAMPLE_TIER, "Basic", "None", SHALLOW_FLOORS, f)
    print(f"    floor {f:>3}: common={s['Common']:>6}  boss={s['Boss']:>6}")
first = S.enemy_scores(EXAMPLE_TIER, "Basic", "None", SHALLOW_FLOORS,
                       1)["Common"]
print(f"\n  depthTension is (floor - middle) * tier * "
      f"{S.DEPTH_TENSION_PER_TIER}, which is large and")
print(f"  negative near the entrance. A floor 1 Common enemy scores {first}, "
      f"which is")
print(f"  {'below' if first < 0 else 'above'} zero.")
