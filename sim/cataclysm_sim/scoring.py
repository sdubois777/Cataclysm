"""Exact port of DungeonSimulator/src/utils/calculateScores.tsx.

This is the authoritative power model. It replaces the placeholder curve the
empire rig was using, and it changes one thing fundamentally:

    Depth is LENGTH, not DIFFICULTY.

Because the baseline term is driven by `currentFloor / totalFloors`, a 150-floor
dungeon and a 20-floor dungeon at the same tier are nearly equally hard per
floor -- the deep one just takes 7x longer. At T1 the middle-floor Dungeon Score
runs 194 (20 floors) -> 201 (50) -> 214 (100) -> 226 (150): a 16% spread across
a 7.5x depth range.

The rig previously assumed deeper meant proportionally harder, which was wrong.

Those four scores are measurements of the model below, so they go stale whenever
the anchors move. They did: they were 151/159/171/184 for the five months this
file carried the pre-#2 anchors. `sim/tests/test_analysis_scripts.py` now
recomputes them and fails if this paragraph disagrees. Issue #6.
"""

from __future__ import annotations

import math
import os
import pathlib
import re

# Hard anchors: the maximum Power Score a player is expected to reach by the end
# of each difficulty tier.
#
# These are a COPY of `playerMaxScores` in calculateScores.tsx. They went stale
# once already: DungeonSimulator commit f83b3b3 ("adjusted player score ranges",
# 2026-03-02) replaced every value here and this file was not updated, so the rig
# ran for five months on a power curve 21-44% below the real one. The self-test
# below now parses the reference file and fails on any drift, so a future edit
# there cannot pass unnoticed.
PLAYER_MAX_SCORES: dict[int, float] = {
    0: 0, 1: 385, 2: 871, 3: 1457, 4: 2144, 5: 3251, 6: 4166, 7: 5209, 8: 6327,
}

BASE_TYPE_SCORES = {"Basic": 30, "Quest": 60, "Fallen City": 90, "Cataclysm": 120}
FLOOR_SCALING_BASES = {"Basic": 100, "Quest": 200, "Fallen City": 300, "Cataclysm": 400}

# Fractions of the current tier's power gap (tierMax - tierMin).
TYPE_WEIGHTS = {"Basic": 0.0, "Quest": 0.05, "Fallen City": 0.1, "Cataclysm": 0.2}
SUBTYPE_WEIGHTS = {
    "None": 0, "Timed": 0, "Horde": 0.05, "Sacrificial": 0.2,
    "Cow Level": 0.1, "Elite": 0.15, "Siege": 0.05, "Volatile": 0.17,
}
RARITY_WEIGHTS = {
    "Common": 0, "Elite": 0.05, "Legendary": 0.1,
    "Herald": 0.15, "Boss": 0.3, "Cataclysm Boss": 0.5,
}

# The parts of the formula that are bare numbers in the reference rather than
# entries in one of its six tables. Named here only so a reader -- and the
# analysis scripts alongside this package -- can refer to them instead of
# retyping the formula in a second file, which is how issue #6 happened. The
# values are unchanged. `check_against_reference` cannot see them because they
# are not tables; `verify_scoring_port.py`, which executes the real TypeScript
# and compares outputs, is what proves they are still right.
BASELINE_WEIGHT = 0.9           # baseline = tierMin + width * this * floorRatio
PROCEDURAL_DIVISOR = 20.0       # scalingFactor = floorScalingBase / this
PROCEDURAL_PER_FLOOR = 0.5      # procedural += currentFloor * this
DEPTH_TENSION_PER_TIER = 1.2    # (currentFloor - middleFloor) * tier * this

# Weights used to collapse a floor's rarity spread into one Dungeon Score.
DUNGEON_SCORE_MIX = (
    ("Common", 0.60), ("Elite", 0.20), ("Legendary", 0.15),
    ("Herald", 0.04), ("Boss", 0.01),
)


def _js_round(x: float) -> int:
    """Round the way JavaScript's Math.round does: halves go up, toward +inf.

    Python's built-in round() is banker's rounding -- round(390.5) is 390, but
    Math.round(390.5) is 391. The reference model rounds every score it returns,
    and exact .5 values are common here because the formula is built from halves
    (`currentFloor * 0.5`) and fifths. Using round() made this port disagree with
    calculateScores.tsx on roughly 2% of inputs, always by exactly 1.
    """
    return math.floor(x + 0.5)


def tier_bounds(tier: int) -> tuple[float, float]:
    p_max = PLAYER_MAX_SCORES.get(tier, 4584)
    p_min = PLAYER_MAX_SCORES.get(tier - 1, 0)
    return p_min, p_max


def tier_width(tier: int) -> float:
    p_min, p_max = tier_bounds(tier)
    return p_max - p_min


def enemy_scores(tier: int, dungeon_type: str, subtype: str,
                 total_floors: int, current_floor: int,
                 modifier_score: float = 0.0) -> dict[str, int]:
    """Every rarity's score on one specific floor."""
    p_min, p_max = tier_bounds(tier)
    width = p_max - p_min
    middle_floor = math.ceil(total_floors / 2)
    floor_ratio = current_floor / total_floors

    baseline = p_min + width * BASELINE_WEIGHT * floor_ratio
    type_bonus = width * TYPE_WEIGHTS.get(dungeon_type, 0.0)
    sub_bonus = width * SUBTYPE_WEIGHTS.get(subtype, 0.0)

    scaling_factor = FLOOR_SCALING_BASES[dungeon_type] / PROCEDURAL_DIVISOR
    procedural = ((scaling_factor * floor_ratio)
                  + (current_floor * PROCEDURAL_PER_FLOOR))
    depth_tension = ((current_floor - middle_floor)
                     * (tier * DEPTH_TENSION_PER_TIER))

    out = {}
    for rarity, w in RARITY_WEIGHTS.items():
        score = (baseline + type_bonus + sub_bonus + width * w
                 + procedural + depth_tension + modifier_score)
        out[rarity] = _js_round(score)
    return out


def dungeon_score(tier: int, dungeon_type: str, subtype: str,
                  total_floors: int, modifier_score: float = 0.0) -> int:
    """The dungeon's expected difficulty: its middle floor, rarity-weighted."""
    middle = math.ceil(total_floors / 2)
    s = enemy_scores(tier, dungeon_type, subtype, total_floors, middle, modifier_score)
    return _js_round(sum(s[r] * w for r, w in DUNGEON_SCORE_MIX))


def final_boss_score(tier: int, dungeon_type: str, subtype: str,
                     total_floors: int, modifier_score: float = 0.0,
                     rarity: str = "Boss") -> int:
    """What waits on the last floor."""
    s = enemy_scores(tier, dungeon_type, subtype, total_floors, total_floors,
                     modifier_score)
    return s[rarity]


# ---------------------------------------------------------------------------

REFERENCE_ENV_VAR = "CATACLYSM_SCORING_REFERENCE"


def reference_path() -> pathlib.Path | None:
    """Locate DungeonSimulator's calculateScores.tsx, the authoritative model."""
    override = os.environ.get(REFERENCE_ENV_VAR)
    if override:
        p = pathlib.Path(override)
        return p if p.is_file() else None
    # sim/cataclysm_sim/scoring.py -> <workspace>/DungeonSimulator/src/utils/...
    guess = (pathlib.Path(__file__).resolve().parents[3]
             / "DungeonSimulator" / "src" / "utils" / "calculateScores.tsx")
    return guess if guess.is_file() else None


def _parse_ts_table(src: str, name: str) -> dict[str, float]:
    """Pull a `const <name> ... = { k: v, ... };` object literal out of the TSX."""
    m = re.search(r"const\s+" + re.escape(name) + r"\b[^=]*=\s*\{(.*?)\}\s*;",
                  src, re.DOTALL)
    if not m:
        raise AssertionError(f"could not find `{name}` in the reference file")
    out: dict[str, float] = {}
    # Keys are quoted ("Fallen City"), bare (Basic), or numeric (0) for the anchors.
    for quoted, bare, num in re.findall(
            r'(?:"([^"]+)"|([\w$]+))\s*:\s*(-?[\d.]+)', m.group(1)):
        out[quoted or bare] = float(num)
    if not out:
        raise AssertionError(f"parsed `{name}` from the reference but found no entries")
    return out


def check_against_reference(verbose: bool = True) -> bool:
    """Compare every constant here against calculateScores.tsx.

    Returns True if checked and matching, False if the reference was not found.
    Raises AssertionError on any drift.
    """
    ref = reference_path()
    if ref is None:
        if verbose:
            print("SKIPPED constant check: calculateScores.tsx not found.\n"
                  f"  Set {REFERENCE_ENV_VAR} to its path to enable drift detection.")
        return False

    src = ref.read_text(encoding="utf-8")
    ts_anchors = {int(k): v for k, v in
                  _parse_ts_table(src, "playerMaxScores").items()}

    for name, ours, theirs in (
        ("playerMaxScores", {int(k): float(v) for k, v in PLAYER_MAX_SCORES.items()},
         ts_anchors),
        ("baseTypeScores", {k: float(v) for k, v in BASE_TYPE_SCORES.items()},
         _parse_ts_table(src, "baseTypeScores")),
        ("floorScalingBases", {k: float(v) for k, v in FLOOR_SCALING_BASES.items()},
         _parse_ts_table(src, "floorScalingBases")),
        ("typeWeights", {k: float(v) for k, v in TYPE_WEIGHTS.items()},
         _parse_ts_table(src, "typeWeights")),
        ("subtypeWeights", {k: float(v) for k, v in SUBTYPE_WEIGHTS.items()},
         _parse_ts_table(src, "subtypeWeights")),
        ("rarityWeights", {k: float(v) for k, v in RARITY_WEIGHTS.items()},
         _parse_ts_table(src, "rarityWeights")),
    ):
        assert ours == theirs, (
            f"\n  {name} has DRIFTED from {ref}.\n"
            f"    scoring.py : {ours}\n"
            f"    reference  : {theirs}\n"
            "  Copy the reference values into scoring.py and re-run the sweeps -- "
            "every tuning number derived from the old curve is invalid.")

    if verbose:
        print(f"Constants match {ref.name} (all 6 tables).")
    return True


def _selftest() -> None:
    """Lock the formula, then verify the constants against the reference file.

    The numeric block below pins THIS file's arithmetic. It cannot detect a
    change to calculateScores.tsx on its own -- that is what
    check_against_reference() is for. Both are needed.
    """
    got = enemy_scores(1, "Basic", "Timed", 50, 25, 0)
    want = {"Common": 188, "Elite": 208, "Legendary": 227,
            "Herald": 246, "Boss": 304, "Cataclysm Boss": 381}
    for k, v in want.items():
        assert got[k] == v, f"{k}: got {got[k]}, want {v}"
    ds = dungeon_score(1, "Basic", "Timed", 50)
    assert ds == 201, f"dungeon score: got {ds}, want 201"
    print("Formula check passed (T1/Basic/Timed/50 floors/floor 25).")
    check_against_reference()


if __name__ == "__main__":
    _selftest()

    print("\nTier widths (the master scalar in every term):")
    prev = 0.0
    for t in range(1, 9):
        w = tier_width(t)
        delta = "" if t == 1 else f"  ({w / prev:+.2f}x vs T{t - 1})"
        print(f"  T{t}: max={PLAYER_MAX_SCORES[t]:>5}  width={w:>5.0f}{delta}")
        prev = w

    print("\nT1 Basic/Timed Dungeon Score vs depth (depth is length, not difficulty):")
    for f in (10, 20, 50, 100, 150):
        print(f"  {f:>4} floors -> {dungeon_score(1, 'Basic', 'Timed', f)}")

    print("\nT1 Basic/Timed, 50 floors, score by floor (note floor 1):")
    for f in (1, 5, 13, 25, 38, 50):
        print(f"  floor {f:>3}: common={enemy_scores(1,'Basic','Timed',50,f)['Common']:>5}"
              f"  boss={enemy_scores(1,'Basic','Timed',50,f)['Boss']:>5}")
