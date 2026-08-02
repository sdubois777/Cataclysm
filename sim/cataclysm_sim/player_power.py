"""The player's Power Score.

`scoring.py` gives the score of every enemy and dungeon. This module gives the
score of the player, on the same scale, so the two can be compared. That
comparison is what the whole difficulty system runs on: a dungeon near the
player's score gives average rewards, below it gives less, above it gives more.

WHAT THIS IS. `docs/Cataclysm_GDD_v2.md` names the inputs to Power Score --
level, gear level, gem quality, socket count, gear quality and resistances --
but never gives a formula. This module proposes one. Unlike `scoring.py`, which
is a port of an authoritative file in another repository and must never be
hand-edited, this file is an original and the numbers in it are open to change.

HOW IT IS CALIBRATED. `scoring.PLAYER_MAX_SCORES` fixes the score a player is
expected to have reached at the end of each of the eight difficulty tiers, from
385 at tier 1 to 6327 at tier 8. Those anchors are authoritative. The weights
below are derived from them rather than chosen: given the reference character
defined in `reference_character()`, the formula lands on 6327 exactly at tier 8
and on 384 against an anchor of 385 at tier 1, and within 5.3% of the six in
between. The two end tiers are pinned in the underlying arithmetic; tier 1 comes
out one point low only because the reference character's level and filled socket
count are whole numbers, so tier 1 rounds to level 12 and 6 gems rather than the
12.5 and 5.625 the continuous curve asks for.

WHY IT IS NOT EXACT IN THE MIDDLE. The anchors are not smooth. Tier 5 is 1107
points wide where the surrounding trend is about 790, and tier 6 is narrower
than tier 5. Any character whose gear and level advance smoothly with the
difficulty tier produces a smooth curve, so it cannot pass through a kink. The
entire residual sits at the tier 4 / tier 5 boundary. See issue #7, which
records the same anomaly from the enemy side.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from . import scoring

# --------------------------------------------------------------------------
# The shape of a character
# --------------------------------------------------------------------------

MAX_LEVEL = 100

# The eight item and gem rarities, in order. Index into this list + 1 is the
# rarity number used by the formula, so Everyday is 1 and Cataclysmic is 8.
RARITIES = ("Everyday", "Quality", "Superb", "Masterful",
            "Legendary", "Mythical", "Ascendant", "Cataclysmic")
MAX_RARITY = len(RARITIES)

# Gear upgrade level, "+1" through "+10" (Gear Leveling in the design document).
MAX_UPGRADE = 10

# Gear pieces that carry a rarity and an upgrade level: 7 armour (head, chest,
# shoulders, gloves, pants, boots, belt), 8 rings, a necklace, a relic, and the
# weapon. Two one-handed weapons count as one weapon for scoring, the same way
# they give the same 6 sockets a two-handed weapon gives, so that dual wielding
# is not worth more Power Score than using a single weapon.
GEAR_PIECES = 18

# Total sockets across all equipment, fixed at 45 by the design document. The
# four potion slots hold gems but are consumables, not gear, so they contribute
# through their sockets only.
TOTAL_SOCKETS = 45

# One resistance per damage type.
RESISTANCE_COUNT = 8

# Resistance counted by Power Score stops at the design's 70% cap. Affixes may
# push actual resistance above it -- that headroom is real, because enemy
# penetration eats into it -- but it is headroom against penetration rather
# than power, so it does not raise the score.
RESISTANCE_CAP = 70.0

# --------------------------------------------------------------------------
# The design decision: what share of Power Score each bucket supplies
# --------------------------------------------------------------------------

# These four shares are the only free choice in this module. They say what a
# fully finished character -- level 100, eighteen Cataclysmic pieces at +10,
# forty-five Cataclysmic gems, all eight resistances capped -- draws from each
# source.
#
# They are a design decision, not a fit. Worst-case anchor error stays between
# 5.29% and 5.35% across allocations as different as 8/60/24/8 and 18/50/22/10,
# because the residual is the anchor kink described in the module docstring and
# no reallocation touches it.
#
# What the shares do change is what the player chases, and what one gear upgrade
# is worth. That second effect is the real constraint. Raising the level share
# to 18% forces a +10 piece to be worth 11.9x the same piece at +0, because gear
# then has to supply the same curvature from a smaller share. The values below
# put a +10 piece at 3.5x, which is the reason they were chosen over the
# alternatives rather than any improvement in anchor fit.
SHARE_LEVEL = 0.10
SHARE_GEAR = 0.50
SHARE_GEMS = 0.30
SHARE_RESISTANCES = 0.10

assert abs(SHARE_LEVEL + SHARE_GEAR + SHARE_GEMS + SHARE_RESISTANCES - 1.0) < 1e-9


# --------------------------------------------------------------------------
# Deriving the weights from the anchors
# --------------------------------------------------------------------------

def _curve_coefficients() -> tuple[float, float]:
    """The quadratic through the tier 1 and tier 8 anchors, with no constant.

    The reference character's score works out to `a * tier^2 + b * tier`. It is
    quadratic for one structural reason: gear rarity and gear upgrade level both
    rise with the tier and multiply each other, and gem rarity rises with the
    tier while the number of gems the player has filled rises with it too.
    Everything else -- level, resistances -- is linear.

    Pinning both ends rather than least-squares fitting all eight is deliberate.
    Tier 1 and tier 8 are the two anchors that have to be exact: tier 1 is where
    a new character is measured and tier 8 is the end of the game.
    """
    t1 = scoring.PLAYER_MAX_SCORES[1]
    t8 = scoring.PLAYER_MAX_SCORES[8]
    # a + b = t1  and  64a + 8b = t8
    a = (t8 - 8 * t1) / 56.0
    b = t1 - a
    return a, b


def _derive_weights() -> dict[str, float]:
    """Turn the four shares and the two anchors into the five weights."""
    a, _ = _curve_coefficients()
    t8 = scoring.PLAYER_MAX_SCORES[8]

    # Level, gems and resistances are pinned by their tier 8 totals alone.
    w_level = SHARE_LEVEL * t8 / MAX_LEVEL
    w_gem = SHARE_GEMS * t8 / (TOTAL_SOCKETS * MAX_RARITY)
    w_resist = SHARE_RESISTANCES * t8 / (RESISTANCE_COUNT * RESISTANCE_CAP)

    # Curvature comes from two places: gems, where rarity and the number of
    # filled sockets both climb, and gear, where rarity and upgrade level both
    # climb. Gems take whatever their share implies; gear supplies the rest.
    a_from_gems = (TOTAL_SOCKETS / MAX_RARITY) * w_gem
    a_from_gear = a - a_from_gems
    if a_from_gear <= 0:
        raise ValueError(
            f"SHARE_GEMS={SHARE_GEMS} leaves no curvature for gear. "
            "Lower it, or gear cannot reach the tier 8 anchor.")

    # a_from_gear = GEAR_PIECES * w_gear * upgrade_factor
    w_gear_times_factor = a_from_gear / GEAR_PIECES

    # Gear at tier 8 is 18 pieces of rarity 8 at +10:
    #   SHARE_GEAR * t8 = 18 * 8 * w_gear * (1 + 10 * upgrade_factor)
    gear_t8 = SHARE_GEAR * t8
    w_gear = ((gear_t8 - GEAR_PIECES * MAX_RARITY * MAX_UPGRADE
               * w_gear_times_factor)
              / (GEAR_PIECES * MAX_RARITY))
    if w_gear <= 0:
        raise ValueError(
            f"SHARE_GEAR={SHARE_GEAR} is too small to carry the curvature "
            "left over from gems. Raise it or lower SHARE_GEMS.")

    return {
        "level": w_level,
        "gear": w_gear,
        # How much one +1 upgrade adds, as a fraction of the piece's
        # unupgraded contribution. At 0.25 a +10 piece is 3.5x a +0 piece.
        "upgrade_factor": w_gear_times_factor / w_gear,
        "gem": w_gem,
        "resistance": w_resist,
    }


WEIGHTS = _derive_weights()


# --------------------------------------------------------------------------
# Scoring a real character
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class GearPiece:
    """One equipped item. `rarity` is 1-8, `upgrade` is 0-10."""

    rarity: int
    upgrade: int = 0

    def __post_init__(self) -> None:
        if not 1 <= self.rarity <= MAX_RARITY:
            raise ValueError(f"rarity {self.rarity} outside 1-{MAX_RARITY}")
        if not 0 <= self.upgrade <= MAX_UPGRADE:
            raise ValueError(f"upgrade {self.upgrade} outside 0-{MAX_UPGRADE}")


@dataclass(frozen=True)
class Character:
    """Everything Power Score reads. Nothing here is a vital.

    Health, mana and energy shield are deliberately absent: the design's list of
    Power Score inputs contains none of them, so a character's base stats -- the
    thing issue #77 is about -- are not needed to compute this number.
    """

    level: int
    gear: tuple[GearPiece, ...] = ()
    #: Rarity 1-8 of each socketed gem. Length is how many sockets are filled.
    gems: tuple[int, ...] = ()
    #: Percentage value of each of the eight resistances.
    resistances: tuple[float, ...] = ()

    def __post_init__(self) -> None:
        if not 1 <= self.level <= MAX_LEVEL:
            raise ValueError(f"level {self.level} outside 1-{MAX_LEVEL}")
        if len(self.gear) > GEAR_PIECES:
            raise ValueError(
                f"{len(self.gear)} gear pieces, maximum is {GEAR_PIECES}")
        if len(self.gems) > TOTAL_SOCKETS:
            raise ValueError(
                f"{len(self.gems)} gems, maximum is {TOTAL_SOCKETS} sockets")
        for g in self.gems:
            if not 1 <= g <= MAX_RARITY:
                raise ValueError(f"gem rarity {g} outside 1-{MAX_RARITY}")
        if self.resistances and len(self.resistances) != RESISTANCE_COUNT:
            raise ValueError(
                f"{len(self.resistances)} resistances, expected "
                f"{RESISTANCE_COUNT} or none")


def level_term(c: Character) -> float:
    return WEIGHTS["level"] * c.level


def gear_term(c: Character) -> float:
    """Rarity times upgrade, summed over equipped pieces.

    Upgrade level multiplies rarity rather than adding to it, because a fully
    upgraded Cataclysmic item should be worth far more than a fully upgraded
    Everyday one. This is also the only reason the curve bends.
    """
    f = WEIGHTS["upgrade_factor"]
    return WEIGHTS["gear"] * sum(p.rarity * (1 + f * p.upgrade) for p in c.gear)


def gem_term(c: Character) -> float:
    """Summed over filled sockets, so socket count enters as the term count."""
    return WEIGHTS["gem"] * sum(c.gems)


def resistance_term(c: Character) -> float:
    """Each resistance counts up to the 70% cap and no further."""
    return WEIGHTS["resistance"] * sum(min(r, RESISTANCE_CAP)
                                      for r in c.resistances)


def power_score(c: Character) -> int:
    """The player's Power Score, on the same scale as `scoring.enemy_scores`."""
    total = level_term(c) + gear_term(c) + gem_term(c) + resistance_term(c)
    # Match the reference model's rounding so player and enemy scores round the
    # same way. See scoring._js_round.
    return math.floor(total + 0.5)


# --------------------------------------------------------------------------
# The reference character
# --------------------------------------------------------------------------

def reference_character(tier: int) -> Character:
    """What a player is expected to look like at the END of difficulty tier N.

    This progression is a proposal, and it is the second half of the answer to
    issue #26: the formula alone cannot be checked against the anchors without
    saying what character is being scored.

    Each line is chosen from something the design already fixes:

    * Gear and gem rarity equal the tier. There are eight difficulty tiers and
      eight rarities, and the design says the best upgrade stone that can drop
      is capped by the current difficulty tier -- so tier gates gear quality.
    * Upgrade level is tier + 2, capped at +10, reaching exactly +10 at tier 8.
      This clears every rarity gate the design states: Legendary needs +4 and is
      reached at tier 5 with +7, Cataclysmic needs +10 and is reached at tier 8.
    * Level rises evenly to 100 at the end of tier 8.
    * Filled sockets rise evenly to all 45 at the end of tier 8. Sockets exist
      on the gear from the start; what the player lacks early is gems to fill
      them.
    * Resistances rise evenly to the 70% cap at the end of tier 8.
    """
    if not 1 <= tier <= 8:
        raise ValueError(f"tier {tier} outside 1-8")

    rarity = tier
    upgrade = min(MAX_UPGRADE, tier + 2)
    level = max(1, round(MAX_LEVEL * tier / 8))
    filled = round(TOTAL_SOCKETS * tier / 8)
    resist = RESISTANCE_CAP * tier / 8

    return Character(
        level=level,
        gear=tuple(GearPiece(rarity=rarity, upgrade=upgrade)
                   for _ in range(GEAR_PIECES)),
        gems=tuple([rarity] * filled),
        resistances=tuple([resist] * RESISTANCE_COUNT),
    )


def anchor_report() -> list[tuple[int, int, float, float]]:
    """(tier, predicted score, anchor, percent error) for all eight tiers."""
    out = []
    for tier in range(1, 9):
        predicted = power_score(reference_character(tier))
        anchor = scoring.PLAYER_MAX_SCORES[tier]
        out.append((tier, predicted, anchor, 100.0 * (predicted - anchor) / anchor))
    return out


if __name__ == "__main__":
    a, b = _curve_coefficients()
    print("Power Score -- proposed formula")
    print()
    print(f"  curve through the tier 1 and tier 8 anchors: "
          f"{a:.3f}*T^2 + {b:.3f}*T")
    print()
    print("  weights derived from the shares and the anchors:")
    for name, value in WEIGHTS.items():
        print(f"    {name:<16} {value:.4f}")
    up = WEIGHTS["upgrade_factor"]
    print(f"    -> a +{MAX_UPGRADE} piece is {1 + up * MAX_UPGRADE:.2f}x "
          f"the same piece at +0")
    print()
    print("  contribution shares at level 100 with everything maxed:")
    for name, share in (("level", SHARE_LEVEL), ("gear", SHARE_GEAR),
                        ("gems", SHARE_GEMS), ("resistances", SHARE_RESISTANCES)):
        print(f"    {name:<16} {share:.0%}")
    print()
    print("  the reference character against the fixed anchors:")
    print("    tier   predicted   anchor    error")
    worst = 0.0
    for tier, predicted, anchor, pct in anchor_report():
        worst = max(worst, abs(pct))
        print(f"    T{tier}     {predicted:>7}   {anchor:>6}   {pct:>+6.1f}%")
    print(f"    worst error {worst:.1f}%. Tier 8 is exact; tier 1 is one point")
    print("    low because level and socket count round to whole numbers.")
    print()
    print("  what the reference character looks like:")
    print("    tier  level  gear         upgrade  gems  resist")
    for tier in range(1, 9):
        c = reference_character(tier)
        print(f"    T{tier}    {c.level:>5}  {RARITIES[c.gear[0].rarity - 1]:<12} "
              f"+{c.gear[0].upgrade:<7} {len(c.gems):>4}  "
              f"{c.resistances[0]:>5.1f}%")
