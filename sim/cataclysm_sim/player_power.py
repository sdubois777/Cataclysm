"""The player's Power Score.

`scoring.py` gives the score of every enemy and dungeon. This module gives the
score of the player, on the same scale, so the two can be compared. That
comparison is what the whole difficulty system runs on: a dungeon near the
player's score gives average rewards, below it gives less, above it gives more.

WHAT THIS IS. `docs/Cataclysm_GDD_v2.md` names the inputs to Power Score --
level, gear level, gem quality, socket count, gear quality and resistances --
and its section IV gives the formula this module implements. Unlike `scoring.py`,
which is a port of an authoritative file in another repository and must never be
hand-edited, this file is an original and the numbers in it are open to change.
The game's copy is `UCataclysmPowerScore` in
`game/Source/Cataclysm/Player/CataclysmPowerScore.h` and `.cpp`.

THE WEIGHTS ARE FIVE PLAIN NUMBERS, RULED BY THE PROJECT OWNER ON 2026-11-24
(issue #2853, "Adopt, and look at drop supply (Recommended)"): 10 points a level,
2 points a rarity step of a gear piece with each upgrade level adding the piece's
+0 worth again (a piece is 2 x rarity x (1 + upgrade level)), 5 points a gem
rarity step in a filled socket, and 1 point a resistance percent up to the cap.
The owner's reason, 2026-11-24: "why would I think a level is worth 6.3 points?
It would make sense as 1, 2, 5, 10, 25, 50, 100, but 6.3 makes no sense at all."

UNTIL THAT RULING THE WEIGHTS WERE DERIVED, NOT CHOSEN: four shares (10% level,
50% gear, 30% gems, 10% resistances of a finished character) and the tier 1 and
tier 8 anchors were solved for five weights (6.327 a level, 6.2330 a rarity
step, an upgrade factor of 0.2525, 5.2725 a gem rarity step and 1.1298 a
resistance percent), and the reference character landed exactly on every anchor
above tier 1. Now THE SHARES FALL OUT of the weights instead: 15% level, 49%
gear, 28% gems and 9% resistances at tier 8 (`shares_of`).

HOW NEAR THE ANCHORS IT LANDS. `scoring.PLAYER_MAX_SCORES` fixes the score a
player is expected to have reached at the end of each of the eight difficulty
tiers, from 385 at tier 1 to 6327 at tier 8. Those anchors are authoritative
and did not move. The reference character of `reference_character()` lands
near them rather than on them: from 364 against 385 at tier 1 (-5.5%) to 6528
against 6327 at tier 8 (+3.2%), every tier within 6%. `anchor_report` prints
the eight; the proposal measured them on branch
`proposal/power-score-shares-2853` before the owner ruled.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from . import damage, scoring

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
# shoulders, gloves, pants, boots, belt), 8 rings, a necklace, a relic, and what
# the hands hold.
#
# WHAT THE HANDS HOLD IS ONE PIECE FOR SCORING, WHATEVER IT IS. All four legal
# loadouts count once: one two-handed weapon, two one-handed weapons, a single
# one-handed weapon, or a one-handed weapon with a Shield. No loadout may be
# worth free Power Score, which is the whole reason the rule exists.
#
# THE SHIELD WAS ADDED TO THAT LIST on 2026-08-15, when the project owner made
# it an offhand rather than a one-handed weapon and said it counts "just like a
# second one-handed weapon". Issue #612. Before then a Shield was a weapon, so a
# character holding one was already inside the dual wielding case by accident.
GEAR_PIECES = 18

# Sockets on one held item, and on a full pair of hands.
#
# A two-handed weapon carries all six on its own. A one-handed item carries
# three, so two one-handed weapons make six and so does a one-handed weapon with
# a Shield. That equality is the same rule as the piece count above and exists
# for the same reason.
SOCKETS_PER_ONE_HANDED_ITEM = 3
SOCKETS_IN_BOTH_HANDS = 6

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
#
# Re-exported from damage.py, which owns it, rather than written out again here.
# It is NOT one of the ported constants: this module is an original, and
# sim/verify_scoring_port.py checks scoring.py, which does not mention
# resistance. Issue #228.
RESISTANCE_CAP = damage.RESISTANCE_CAP

# --------------------------------------------------------------------------
# The design decision: what each thing the score reads is worth
# --------------------------------------------------------------------------

# FIVE PLAIN NUMBERS, ruled by the project owner on 2026-11-24 (issue #2853):
# "Adopt, and look at drop supply (Recommended)". Each is a number a player can
# read and add up, which was the owner's direction: "It would make sense as 1, 2,
# 5, 10, 25, 50, 100, but 6.3 makes no sense at all."
#
# They are chosen, and the shares a finished character draws from each source
# fall out of them (`shares_of`): 15% level, 49% gear, 28% gems and 9%
# resistances at tier 8, against the 10/50/30/10 the weights used to be derived
# from. Floats, so `affixes.POWER_SCORE_UPGRADE_FACTOR` is this very object and
# `sim/tests/test_affixes.py` can tell it from a retyped copy.

#: Points for each character level. Was 6.327 (10% of tier 8's 6,327 over 100
#: levels); a level 6 character's level part is 60.
LEVEL_WEIGHT = 10.0

#: Points for each rarity step of a gear piece at +0 (Everyday is 1, Cataclysmic
#: 8): an Everyday +0 piece is 2, a Masterful +0 piece 8.
GEAR_WEIGHT = 2.0

#: What each upgrade level adds, as a multiple of the piece's +0 worth: 1, so a
#: piece at +k is worth (1 + k) times itself at +0 and a +10 piece 11 times. Put
#: in a player's terms: each upgrade level adds another GEAR_WEIGHT for every
#: rarity step, so a Masterful piece gains 8 a level.
#:
#: WHY UPGRADE LEVEL STILL MULTIPLIES RARITY rather than adding to it, a judgement
#: the eight anchors force: they grow 16.4 times from tier 1 to tier 8, so about
#: 59% of the tier 8 anchor has to come from terms where two rising things
#: multiply. With gear additive only gems do, and every additive set within 5% of
#: the anchors puts 58 to 61% of the tier 8 score on gems.
UPGRADE_FACTOR = 1.0

#: Points for each rarity step of a socketed gem: an Everyday gem is 5, a
#: Cataclysmic one 40. Socket count has no weight of its own.
GEM_WEIGHT = 5.0

#: Points for each resistance percent, each of the eight counted up to the cap.
RESISTANCE_WEIGHT = 1.0

#: The five, under the keys every caller reads.
WEIGHTS: dict[str, float] = {
    "level": LEVEL_WEIGHT,
    "gear": GEAR_WEIGHT,
    "upgrade_factor": UPGRADE_FACTOR,
    "gem": GEM_WEIGHT,
    "resistance": RESISTANCE_WEIGHT,
}


def _check_every_loadout_fills_both_hands_with_the_same_sockets() -> None:
    """No loadout may be worth free Power Score.

    Two one-handed items must give exactly what one two-handed weapon gives.
    Stated in the design document under Power Score, and extended to the Shield
    by the project owner on 2026-08-15. Issue #612.
    """
    paired = SOCKETS_PER_ONE_HANDED_ITEM * 2
    if paired != SOCKETS_IN_BOTH_HANDS:
        raise ValueError(
            f"two one-handed items give {paired} sockets and a two-handed "
            f"weapon gives {SOCKETS_IN_BOTH_HANDS}, so one loadout is worth "
            "free Power Score")


_check_every_loadout_fills_both_hands_with_the_same_sockets()


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


def shares_of(c: Character) -> dict[str, float]:
    """What fraction of the unrounded score each of the four sources supplies.

    AN OUTCOME OF THE WEIGHTS SINCE 2026-11-24, NOT AN INPUT. Before the owner's
    ruling on issue #2853 the four shares were chosen (10/50/30/10) and the
    weights derived from them; now the weights are chosen and this says what
    shares they give. For the finished tier 8 character it is about 15% level,
    49% gear, 28% gems and 9% resistances.
    """
    terms = {"level": level_term(c), "gear": gear_term(c),
             "gems": gem_term(c), "resistances": resistance_term(c)}
    total = sum(terms.values())
    if total <= 0:
        raise ValueError("a character that scores nothing has no shares")
    return {name: value / total for name, value in terms.items()}


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
    print("Power Score -- the owner's plain weights of 2026-11-24 (#2853)")
    print()
    print("  the five weights:")
    for name, value in WEIGHTS.items():
        print(f"    {name:<16} {value:g}")
    up = WEIGHTS["upgrade_factor"]
    print(f"    -> a +{MAX_UPGRADE} piece is {1 + up * MAX_UPGRADE:g}x "
          f"the same piece at +0")
    print()
    print("  the shares they give the finished tier 8 character:")
    for name, share in shares_of(reference_character(8)).items():
        print(f"    {name:<16} {share:.0%}")
    print()
    print("  the reference character against the fixed anchors:")
    print("    tier   predicted   anchor    error")
    worst = 0.0
    for tier, predicted, anchor, pct in anchor_report():
        worst = max(worst, abs(pct))
        print(f"    T{tier}     {predicted:>7}   {anchor:>6}   {pct:>+6.1f}%")
    print(f"    worst error {worst:.1f}%. The weights are chosen, not fitted, so")
    print("    the character lands near the anchors rather than on them.")
    print()
    print("  what the reference character looks like:")
    print("    tier  level  gear         upgrade  gems  resist")
    for tier in range(1, 9):
        c = reference_character(tier)
        print(f"    T{tier}    {c.level:>5}  {RARITIES[c.gear[0].rarity - 1]:<12} "
              f"+{c.gear[0].upgrade:<7} {len(c.gems):>4}  "
              f"{c.resistances[0]:>5.1f}%")
