"""Exact port of DungeonSimulator/src/utils/calculateScores.tsx.

This is the authoritative power model. It replaces the placeholder curve the
empire rig was using, and it changes one thing fundamentally:

    Depth is LENGTH, not DIFFICULTY.

Because the baseline term is driven by `currentFloor / totalFloors`, a 150-floor
dungeon and a 20-floor dungeon at the same tier are nearly equally hard per
floor -- the deep one just takes 7x longer. At T1 the middle-floor Dungeon Score
runs 151 (20 floors) -> 159 (50) -> 171 (100) -> 184 (150): a 22% spread across
a 7.5x depth range.

The rig previously assumed deeper meant proportionally harder, which was wrong.
"""

from __future__ import annotations

import math

# Hard anchors: the maximum Power Score a player is expected to reach by the end
# of each difficulty tier.
PLAYER_MAX_SCORES: dict[int, float] = {
    0: 0, 1: 297, 2: 543, 3: 836, 4: 1177, 5: 2152, 6: 2766, 7: 3763, 8: 4584,
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

# Weights used to collapse a floor's rarity spread into one Dungeon Score.
DUNGEON_SCORE_MIX = (
    ("Common", 0.60), ("Elite", 0.20), ("Legendary", 0.15),
    ("Herald", 0.04), ("Boss", 0.01),
)


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

    baseline = p_min + width * 0.9 * floor_ratio
    type_bonus = width * TYPE_WEIGHTS.get(dungeon_type, 0.0)
    sub_bonus = width * SUBTYPE_WEIGHTS.get(subtype, 0.0)

    scaling_factor = FLOOR_SCALING_BASES[dungeon_type] / 20.0
    procedural = (scaling_factor * floor_ratio) + (current_floor * 0.5)
    depth_tension = (current_floor - middle_floor) * (tier * 1.2)

    out = {}
    for rarity, w in RARITY_WEIGHTS.items():
        score = (baseline + type_bonus + sub_bonus + width * w
                 + procedural + depth_tension + modifier_score)
        out[rarity] = round(score)
    return out


def dungeon_score(tier: int, dungeon_type: str, subtype: str,
                  total_floors: int, modifier_score: float = 0.0) -> int:
    """The dungeon's expected difficulty: its middle floor, rarity-weighted."""
    middle = math.ceil(total_floors / 2)
    s = enemy_scores(tier, dungeon_type, subtype, total_floors, middle, modifier_score)
    return round(sum(s[r] * w for r, w in DUNGEON_SCORE_MIX))


def final_boss_score(tier: int, dungeon_type: str, subtype: str,
                     total_floors: int, modifier_score: float = 0.0,
                     rarity: str = "Boss") -> int:
    """What waits on the last floor."""
    s = enemy_scores(tier, dungeon_type, subtype, total_floors, total_floors,
                     modifier_score)
    return s[rarity]


# ---------------------------------------------------------------------------

def _selftest() -> None:
    """Reproduce the reference screenshot: T1 / Basic / Timed / 50 floors / 25."""
    got = enemy_scores(1, "Basic", "Timed", 50, 25, 0)
    want = {"Common": 149, "Elite": 164, "Legendary": 178,
            "Herald": 193, "Boss": 238, "Cataclysm Boss": 297}
    for k, v in want.items():
        assert got[k] == v, f"{k}: got {got[k]}, want {v}"
    ds = dungeon_score(1, "Basic", "Timed", 50)
    assert ds == 159, f"dungeon score: got {ds}, want 159"
    print("scoring.py matches DungeonSimulator reference output.")


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
