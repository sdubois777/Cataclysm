"""The Consumption Threshold: how much Worn Residue is too much, at each tier.

Worn Residue is the sum of the Cataclysmic Residue on every equipped item.
Crossing the Consumption Threshold marks the character, and a corrupted double
hunts it on the next dungeon floor; losing to that double ends the run.

THE THRESHOLD IS DERIVED, NOT CHOSEN. Every input already existed and nothing
here is invented:

    what a drop arrives carrying   the Gear Rarity sheet, mirrored in `loot.py`
    what each craft adds           the Crafting sheet, mirrored below
    what a maxed character is      `player_power.reference_character`

THE METHOD, set by the project owner on 2026-08-18. Take the character the
player is expected to have at the end of a difficulty tier. Work out the least
Worn Residue that character could possibly have, using no residue-mitigating
material. Set the threshold below that, at 85% of it, so a player who reaches
the expected build **has** to spend a mitigating material to stay under.

ONE THRESHOLD PER DIFFICULTY TIER, WHICH IS A CHANGE. `docs/Cataclysm_GDD_v2.md`
said "a single fixed number, to be tuned" until this landed. The project owner's
reason, in their words: "Otherwise nobody in the lower tiers would ever cross
it." A single number set against tier 8 gear is unreachable at tier 1, where the
whole loadout carries a tenth as much.

THE CHEAPEST PATH, RATHER THAN THE TYPICAL OR THE WORST ONE. Also the owner's
choice, and it is the only one that makes mitigation compulsory. A threshold set
against an expensive path would leave a player who found good drops under it,
and the mitigating materials optional. See `cheapest_path` for the surprise the
cheapest path turns out to be.

WHAT THIS DELIBERATELY DOES NOT COUNT.

    Affix tier upgrades. The Potency Crystal raises an affix a tier for
    10 CR times its current tier, so taking one affix from T1 to T7 costs 210
    and four of them cost 840 -- more than everything else combined. It is
    excluded because Power Score does not read affix tiers at all
    (`player_power.power_score` reads level, gear rarity times upgrade level,
    gems and resistances), so no affix upgrade is needed to reach the expected
    build. A player who does upgrade affixes goes over the threshold sooner,
    which is the correct direction for a safety limit to be wrong in.

    The two mitigating materials, on purpose. Purified Essence halves
    accumulated residue and Chaos Stabilizer halves one craft's gain. The
    threshold is what they exist to be spent against, so counting them here
    would make it define itself. `mitigation_needed` says how much is enough.

    The Residue Protocols empire node, which ignores 5% of residue per point,
    for the same reason. `points_of_residue_protocols_needed` says how many
    points clear the threshold instead.
"""

from __future__ import annotations

from . import affixes as af
from . import loot, player_power

# --------------------------------------------------------------------------
# What each craft adds, mirrored from the Crafting sheet
# --------------------------------------------------------------------------

#: Residue added by imprinting one enchantment onto a piece.
#:
#: THE WORKBOOK IS AUTHORITATIVE, as it is for every other stored value in this
#: package. The Crafting sheet of `docs/All_Things_Cataclysm.xlsx` states these
#: in its second table, the one under the row whose first cell reads "Action",
#: and `tools/tests/test_craft_residue_costs_match_the_sheet.py` fails when this
#: file and that sheet disagree.
IMPRINT_ENCHANTMENT_RESIDUE = 5.0

#: Residue added by crafting one affix onto a piece, per tier of that affix.
#: The sheet states it as "5 CR per tier of affix", so a T1 affix costs 5.
DETERMINISTIC_AFFIX_RESIDUE_PER_TIER = 5.0

#: Residue added by raising a piece's upgrade level by one, per level.
UPGRADE_LEVEL_RESIDUE_PER_LEVEL = 25.0

#: Residue added by adding one socket to a piece.
ADD_SOCKET_RESIDUE = 15.0

#: Residue added by putting one gem into one socket.
SOCKET_GEM_RESIDUE = 5.0

#: What one use of Purified Essence removes: half of the accumulated total.
PURIFIED_ESSENCE_REMOVES = 0.5

#: What one point in the Residue Protocols empire node ignores.
RESIDUE_PROTOCOLS_IGNORED_PER_POINT = 0.05

# --------------------------------------------------------------------------
# The threshold itself
# --------------------------------------------------------------------------

#: Where the threshold sits, as a share of the cheapest path to the expected
#: build. The project owner asked for 80% to 90% and this is the middle of it.
#:
#: A SHARE RATHER THAN A NUMBER, so the threshold moves on its own when a drop
#: residue band or a craft cost is retuned. There is nothing to keep in step.
THRESHOLD_SHARE_OF_CHEAPEST_PATH = 0.85

#: The lowest and highest share the project owner allowed. Rounding the
#: threshold has to stay inside this, which `_check_every_rounded_threshold_is_
#: still_inside_the_band` asserts.
THRESHOLD_SHARE_BAND = (0.80, 0.90)

#: The threshold is rounded to a multiple of this, so it reads as a designed
#: number rather than as the output of a division. 1,938 and 1,950 are the same
#: number for every purpose a player has.
THRESHOLD_ROUNDING = 50


def promotion_residue(start: str, target: str) -> float:
    """Residue to promote one piece from one rarity to another.

    RARITY IS THE CONTENTS OF THE FOUR SLOTS, so promoting means filling one
    more of them. Below Masterful that is another regular affix; above it, each
    step swaps an affix for an enchantment, which the sheet prices as one
    imprint. `affixes.RARITY_COMPOSITION` is what says which.

    A NEW AFFIX IS COSTED AT T1, the cheapest the Deterministic Affix craft can
    be, because this is the cheapest path and nothing above T1 is needed to
    reach the expected build.
    """
    if start not in af.RARITIES:
        raise ValueError(f"{start!r} is not a rarity")
    if target not in af.RARITIES:
        raise ValueError(f"{target!r} is not a rarity")

    first = af.RARITIES.index(start)
    last = af.RARITIES.index(target)
    if last < first:
        raise ValueError(
            f"cannot promote a {start} down to a {target}: crafting adds to a "
            "piece and there is no craft that removes an enchantment")

    total = 0.0
    for step in range(first, last):
        here = af.RARITY_COMPOSITION[af.RARITIES[step]]
        above = af.RARITY_COMPOSITION[af.RARITIES[step + 1]]
        if above[0] > here[0]:
            total += IMPRINT_ENCHANTMENT_RESIDUE
        else:
            total += DETERMINISTIC_AFFIX_RESIDUE_PER_TIER * 1
    return total


def expected_build(tier: int) -> tuple[str, int, int]:
    """The rarity, upgrade level and filled socket count expected at a tier.

    Read off `player_power.reference_character`, which is where the expected
    progression is already stated and argued. Restating it here would be a
    second copy of a curve.
    """
    character = player_power.reference_character(tier)
    rarity = af.RARITIES[character.gear[0].rarity - 1]
    return rarity, character.gear[0].upgrade, len(character.gems)


def sockets_a_drop_arrives_with() -> float:
    """How many of a full loadout's sockets are already there when it drops.

    Half the maximum, because a drop rolls uniformly from none up to its base's
    maximum. Counted across the whole loadout rather than piece by piece, which
    is an approximation: a socket on one piece cannot be moved to another, so a
    real player adds slightly more than this. It is a rounding error against the
    numbers it sits beside -- 562 of tier 8's 6,592 -- and erring low is the
    correct direction, since this is the CHEAPEST path.
    """
    return player_power.TOTAL_SOCKETS / 2.0


def worn_residue_for_path(tier: int, start: str) -> float:
    """Worn Residue after crafting a full loadout to the expected build.

    @param start  the rarity every piece dropped at, before any crafting
    """
    target, upgrade, gems = expected_build(tier)

    lowest, highest = loot.residue_band(start)
    on_drop = (lowest + highest) / 2.0

    # THE UPGRADE LEVEL A DROP ALREADY HAS is the floor its own rarity forces
    # and nothing more, which is what `loot.roll_item` gives it.
    levels = max(0, upgrade - loot.gear_level_gate(start))

    per_piece = (on_drop
                 + promotion_residue(start, target)
                 + UPGRADE_LEVEL_RESIDUE_PER_LEVEL * levels)

    to_add = max(0.0, gems - sockets_a_drop_arrives_with())
    sockets = ADD_SOCKET_RESIDUE * to_add + SOCKET_GEM_RESIDUE * gems

    return player_power.GEAR_PIECES * per_piece + sockets


def cheapest_path(tier: int) -> tuple[str, float]:
    """The starting rarity that reaches the expected build for the least
    residue, and what it costs.

    IT IS ALWAYS EVERYDAY, AND THAT IS THE INTERESTING PART. Promoting a piece
    all the way from Everyday to Cataclysmic costs 35 residue, because each step
    is one affix or one imprint at 5. A Cataclysmic drop instead arrives
    carrying 300 to 500. So the cheapest route to a maxed character is to take
    the WORST drops and craft them up, and a good drop is a liability to a
    player who intends to max out.

    That is not an accident of these numbers, it is what the design says:
    section VI of `docs/Cataclysm_GDD_v2.md` states that "a better item is
    therefore more expensive to improve", and that a Cataclysmic drop "is not
    simply better than a Masterful one". This is that rule taken to its end.

    The search is over every rarity rather than assuming Everyday, so the answer
    stays right if a craft cost or a residue band is ever retuned.
    """
    target, _upgrade, _gems = expected_build(tier)
    reachable = af.RARITIES[:af.RARITIES.index(target) + 1]

    best_start = reachable[0]
    best_total = worn_residue_for_path(tier, best_start)
    for start in reachable[1:]:
        total = worn_residue_for_path(tier, start)
        if total < best_total:
            best_start, best_total = start, total
    return best_start, best_total


def consumption_threshold(tier: int) -> int:
    """The Worn Residue at which a character is marked, at this difficulty tier.

    85% of the cheapest path to the expected build, rounded to the nearest 50.
    """
    _start, cheapest = cheapest_path(tier)
    exact = cheapest * THRESHOLD_SHARE_OF_CHEAPEST_PATH
    return int(round(exact / THRESHOLD_ROUNDING) * THRESHOLD_ROUNDING)


def bare_drops_worn_residue(tier: int) -> float:
    """Worn Residue from equipping eighteen drops at the tier's expected rarity
    and crafting nothing at all.

    WHY THIS IS WORTH KNOWING SEPARATELY. From tier 4 upward it is already ABOVE
    the threshold, so a player who equips good drops is marked before touching
    the Forge. That is the intended shape rather than a fault -- it is what
    makes the cheap path and the mitigating materials both worth using -- but it
    is sharp enough that it is stated in the design document rather than left to
    be discovered.
    """
    target, _upgrade, _gems = expected_build(tier)
    lowest, highest = loot.residue_band(target)
    return player_power.GEAR_PIECES * (lowest + highest) / 2.0


def mitigation_needed(tier: int) -> bool:
    """Whether one use of Purified Essence clears the threshold at this tier.

    It halves accumulated residue, so it clears the threshold whenever the
    cheapest path is under twice it. Answering this is half of what issue #697
    asked: whether the tools that manage residue are enough at whatever the
    threshold turned out to be.
    """
    _start, cheapest = cheapest_path(tier)
    return cheapest * (1.0 - PURIFIED_ESSENCE_REMOVES) < consumption_threshold(tier)


def points_of_residue_protocols_needed(tier: int) -> int:
    """How many points in the Residue Protocols empire node clear the threshold.

    Each point ignores 5% of residue. This is the other half of what issue #697
    asked.
    """
    _start, cheapest = cheapest_path(tier)
    threshold = consumption_threshold(tier)
    points = 0
    while cheapest * (1.0 - RESIDUE_PROTOCOLS_IGNORED_PER_POINT * points) \
            >= threshold:
        points += 1
        if points > 20:  # 20 points ignores everything; nothing needs that many
            break
    return points


def table() -> list[tuple[int, str, int, float, int]]:
    """One row per difficulty tier, for the design document and for tests.

    (tier, the rarity the cheapest path starts from, the cheapest path's total
    rounded, the exact total, the threshold).
    """
    rows = []
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        start, cheapest = cheapest_path(tier)
        rows.append((tier, start, round(cheapest), cheapest,
                     consumption_threshold(tier)))
    return rows


# --------------------------------------------------------------------------
# Checks that run on import, the same way affixes.py and loot.py do it.
# --------------------------------------------------------------------------

def _check_every_tier_has_a_threshold() -> None:
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        if consumption_threshold(tier) <= 0:
            raise AssertionError(
                f"tier {tier} has a Consumption Threshold of "
                f"{consumption_threshold(tier)}, so a character would be marked "
                "before equipping anything")


def _check_the_threshold_only_ever_rises() -> None:
    """A deeper tier expects better gear, which carries more residue. A
    threshold that fell would mark a better-equipped character sooner."""
    previous = 0
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        here = consumption_threshold(tier)
        if here <= previous:
            raise AssertionError(
                f"the Consumption Threshold at tier {tier} is {here}, not above "
                f"the {previous} at tier {tier - 1}. It has to rise: a deeper "
                "tier expects gear carrying more residue.")
        previous = here


def _check_every_rounded_threshold_is_still_inside_the_band() -> None:
    """Rounding to the nearest 50 must not push the threshold out of the 80% to
    90% the project owner asked for. Without this the rounding could quietly
    change the decision at a tier where the exact figure sits near an end."""
    lowest, highest = THRESHOLD_SHARE_BAND
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        _start, cheapest = cheapest_path(tier)
        share = consumption_threshold(tier) / cheapest
        if not lowest <= share <= highest:
            raise AssertionError(
                f"the Consumption Threshold at tier {tier} is "
                f"{consumption_threshold(tier)}, which is {share:.1%} of the "
                f"{cheapest:.0f} the cheapest path costs. Rounding to "
                f"{THRESHOLD_ROUNDING} has pushed it outside the "
                f"{lowest:.0%} to {highest:.0%} band.")


def _check_the_threshold_is_below_the_path_it_is_derived_from() -> None:
    """The whole point. A threshold at or above the cheapest path would never be
    crossed by a player who reached the expected build, and the mitigating
    materials would have nothing to do."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        _start, cheapest = cheapest_path(tier)
        if consumption_threshold(tier) >= cheapest:
            raise AssertionError(
                f"at tier {tier} the threshold {consumption_threshold(tier)} is "
                f"not below the {cheapest:.0f} the cheapest path to the "
                "expected build costs, so reaching that build would never mark "
                "the character and no mitigation would ever be needed")


def _check_the_cheapest_path_really_is_the_cheapest() -> None:
    """`cheapest_path` searches, so this checks the search rather than trusting
    it: no other starting rarity may beat what it returned."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        start, cheapest = cheapest_path(tier)
        target, _upgrade, _gems = expected_build(tier)
        for other in af.RARITIES[:af.RARITIES.index(target) + 1]:
            if worn_residue_for_path(tier, other) < cheapest:
                raise AssertionError(
                    f"at tier {tier} cheapest_path returned {start} at "
                    f"{cheapest:.0f}, but starting from {other} costs "
                    f"{worn_residue_for_path(tier, other):.0f}")


def _check_one_purified_essence_clears_every_tier() -> None:
    """Half of what issue #697 asked. A threshold no mitigation can clear would
    make being marked unavoidable rather than a decision."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        if not mitigation_needed(tier):
            raise AssertionError(
                f"at tier {tier} one use of Purified Essence does not bring the "
                "cheapest path under the Consumption Threshold, so a player who "
                "reached the expected build could not get back under it with "
                "the tool the design says exists for that.")


_check_every_tier_has_a_threshold()
_check_the_threshold_only_ever_rises()
_check_every_rounded_threshold_is_still_inside_the_band()
_check_the_threshold_is_below_the_path_it_is_derived_from()
_check_the_cheapest_path_really_is_the_cheapest()
_check_one_purified_essence_clears_every_tier()


if __name__ == "__main__":  # pragma: no cover
    print("The Consumption Threshold, derived per difficulty tier")
    print()
    print(f"  {'tier':<6}{'expected build':<26}{'cheapest path':>14}"
          f"{'from':>12}{'threshold':>11}{'share':>8}")
    for tier, start, rounded, exact, threshold in table():
        rarity, upgrade, gems = expected_build(tier)
        build = f"{rarity} +{upgrade}, {gems} gems"
        print(f"  {tier:<6}{build:<26}{rounded:>14}{start:>12}"
              f"{threshold:>11}{threshold / exact:>8.1%}")
    print()
    print("  Equipping eighteen drops at the expected rarity, crafting nothing:")
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        bare = bare_drops_worn_residue(tier)
        threshold = consumption_threshold(tier)
        over = "OVER the threshold" if bare >= threshold else "under it"
        print(f"    tier {tier}: {bare:>6.0f} against {threshold:>5} -- {over}")
    print()
    print("  Residue Protocols points needed to clear the threshold:")
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        print(f"    tier {tier}: {points_of_residue_protocols_needed(tier)}")
