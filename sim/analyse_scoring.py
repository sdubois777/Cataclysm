"""What the DungeonSimulator scoring model implies about player-vs-content.

Run: python analyse_scoring.py
"""

from cataclysm_sim import scoring as S


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

hdr("B. Where the Cataclysm Boss number comes from (T1, 125 floors)")
w = S.tier_width(1)
print(f"  tier width                     = {w:.0f}")
print(f"  baseline   0.90 x width        = {0.90 * w:>8.1f}")
print(f"  type bonus 0.20 x width        = {0.20 * w:>8.1f}   (Cataclysm)")
print(f"  rarity     0.50 x width        = {0.50 * w:>8.1f}   (Cataclysm Boss)")
print(f"  procedural 400/20 + 125*0.5    = {400 / 20 + 125 * 0.5:>8.1f}")
print(f"  depth      (125-63) * 1 * 1.2  = {(125 - 63) * 1.2:>8.1f}")
print(f"  {'-' * 40}")
print(f"  total                          = "
      f"{0.9 * w + 0.2 * w + 0.5 * w + 400 / 20 + 125 * 0.5 + (125 - 63) * 1.2:>8.1f}")
print("\n  The weighted terms alone sum to 1.60 x tier width, but a player can")
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
GDD = {1: (0, 385), 2: (386, 871), 3: (872, 1457), 4: (1458, 2144),
       5: (2145, 3251), 6: (3252, 4166), 7: (4167, 5209), 8: (5210, 6327)}
print(f"{'tier':>5}{'GDD range':>18}{'sim max':>10}{'sim in GDD range?':>20}")
print("-" * 88)
for t in range(1, 9):
    lo, hi = GDD[t]
    m = S.PLAYER_MAX_SCORES[t]
    ok = "yes" if lo <= m <= hi else "NO"
    print(f"{t:>5}{f'{lo}-{hi}':>18}{m:>10}{ok:>20}")

hdr("E. Depth is length, not difficulty")
print("  T1 Basic, Dungeon Score at the middle floor:\n")
for f in (8, 15, 25, 40, 60, 100, 150):
    print(f"    {f:>4} floors -> {S.dungeon_score(1, 'Basic', 'None', f):>4}"
          f"   ({f} days to run)")
print("\n  7.5x the depth buys 22% more difficulty. Floor count is a TIME and")
print("  LOOT lever, not a difficulty lever.")

hdr("F. Early floors score negative")
print("  T1 Basic, 50 floors:\n")
for f in (1, 3, 5, 10, 25):
    s = S.enemy_scores(1, "Basic", "None", 50, f)
    print(f"    floor {f:>3}: common={s['Common']:>6}  boss={s['Boss']:>6}")
print("\n  depthTension is (floor - middle) * tier * 1.2, which is large and")
print("  negative near the entrance. Floor 1 enemies have negative power.")
