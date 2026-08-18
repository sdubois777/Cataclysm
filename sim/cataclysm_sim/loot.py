"""What a drop is: which of the eight rarities it rolls.

A dungeon produces items and nothing decided what they were. This is the first
part of issue #44.

WHAT THIS DOES NOT DECIDE. Which item base drops, which affixes it rolls, how
many items a kill or a floor produces, and what upgrade level a drop arrives at
beyond the floor its own rarity forces. Those are the later parts of #44.

THE ROLL IS A CASCADE FROM THE RAREST DOWN, which is what the genre does. Path of
Exile "rolls for rare, then magic, and any remaining items will drop normal", and
Diablo 2 checks hierarchically with a fallback when the quality it rolled is not
available. Magic find multiplies the chance at each step rather than changing the
outcome, which is what Path of Exile states plainly: +100% increased item rarity
gives "twice as many magic items, twice as many rares and twice as many uniques".

THE RARITY IS ROLLED FIRST AND THE CONTENTS FOLLOW FROM IT. Asked by the project
owner on 2026-08-18: "Gear drops, rolls for rarity, then picks however many
affixes/enchantments based on that?" That is right, and it is also the only
workable order. `affixes.rarity_of` accepts eight combinations of enchantment and
affix counts and raises for every other, so rolling the two counts independently
would produce something that is not a rarity most of the time. Rolling the rarity
and reading `affixes.RARITY_COMPOSITION` for its contents cannot.

It does not conflict with rarity being computed rather than stored. An ITEM still
carries no rarity field; the generator uses the label as a step and stores only
the contents, and `affixes.rarity_of` recovers the same label from them.

THE CAP IS ONE RARITY ABOVE THE DIFFICULTY TIER, WHICH IS NOT A NEW MECHANISM. `docs/Cataclysm_GDD_v2.md` says the difficulty tier is the design's
own gate three times over -- gear and gem rarity equal it, the best upgrade stone
that can drop is capped by it, and a weapon rolls damage types up to it -- and
`affixes.max_affix_tier_on_a_drop` is the fourth, gating affix tiers at tier plus
one. This is the fifth use of the same shape.

The cap RAISES the ceiling rather than lifting the floor, which is the point of
it, quoted from the affix version: every tier at or below the cap "stays in the
pool, so a deep drop is better on average without being predictable, which is
what makes a drop worth reading". Path of Exile gates modifier tiers on item
level and Last Epoch on area level, and both expand which tiers are available
rather than removing the low ones.

How the rarities inside that cap are weighted against each other is a separate
question, and it is the one tunable described below. They are all equal today,
which is what makes the distribution flat.

THERE IS EXACTLY ONE TUNABLE HERE AND IT LIVES IN THE WORKBOOK. The Loot Rarity
sheet of `docs/All_Things_Cataclysm.xlsx` gives each rarity a drop weight, and
`RARITY_DROP_WEIGHT` below mirrors it the way every other sheet is mirrored into
this package. All eight weights are 1 today, which is what makes the distribution
flat.

**FLAT IS GENEROUS AND IS THE FIRST NUMBER TO REVISIT.** At difficulty tier 8 with
no magic find at all it makes one drop in eight Cataclysmic, which is far above
what the genre does with its top rarities. It is shipped anyway, for two reasons.
It is the shape the design already applies to affix tiers rather than a curve
invented here, and the lever that really controls how much good gear a player sees
is how many items drop, which is not built yet -- so tuning the split between
rarities before the quantity exists would be tuning half a system. Changing it is
a column in the workbook and no code change. Issue #81 and the project owner's
standing rule both say balance numbers wait until the systems around them can be
played.

WHAT IS DELIBERATELY NOT BUILT. Diminishing returns on magic find. Diablo 2 has
them, per rarity and weaker the rarer the tier -- uniques (MF*250)/(MF+250), sets
(MF*500)/(MF+500), rares (MF*600)/(MF+600), with ordinary magic items not
diminished at all -- but every one of those is a chosen constant, and this module
has none. Saturating at 1 is the only ceiling, and whether that is enough is a
question for play.

Sources: Drop rate, PoE Wiki, https://www.poewiki.net/wiki/Drop_rate ; Rarity,
PoE Wiki, https://www.poewiki.net/wiki/Rarity ; Magic find diminishing returns,
Diablo Wiki, https://diablo2.diablowiki.net/Magic_find_diminishing_returns .
"""

from __future__ import annotations

from . import affixes as af

#: How far above the difficulty tier's own rarity a drop may roll.
#:
#: THE SAME ONE-ABOVE THE AFFIX TIER GATE USES, and for the reason the project
#: owner gave for that one in issue #241: with the cap sitting exactly on the
#: tier, the best thing a dungeon can produce is something the player can already
#: make, so the only reason to run one is quantity.
DROP_RARITIES_ABOVE_DIFFICULTY = 1

#: How heavily each rarity is weighted on a drop, mirroring the Drop Weight
#: column of the Loot Rarity sheet in `docs/All_Things_Cataclysm.xlsx`.
#:
#: THE WORKBOOK IS AUTHORITATIVE, as it is for every other stored value in this
#: package; `tools/tests/test_loot_sheet_matches_the_model.py` fails when the two
#: disagree, and the fix is to change this table rather than the sheet.
#:
#: WHAT A WEIGHT MEANS. Its share of every reachable rarity's weight. Equal
#: weights therefore give a flat distribution, which is what all eight being 1
#: does today. See the module docstring for why flat is generous and why it ships
#: anyway.
RARITY_DROP_WEIGHT: dict[str, float] = {
    "Everyday":    1.0,
    "Quality":     1.0,
    "Superb":      1.0,
    "Masterful":   1.0,
    "Legendary":   1.0,
    "Mythical":    1.0,
    "Ascendant":   1.0,
    "Cataclysmic": 1.0,
}

#: The upgrade level a piece must be before it can be a given rarity.
#:
#: Stated in the rarity table of `docs/Cataclysm_GDD_v2.md` section VI and
#: implemented nowhere until now: Legendary needs +4, Mythical +6, Ascendant +8
#: and Cataclysmic +10. The lower four rarities have no gate.
#:
#: THIS IS A FLOOR ON A DROP, NOT A FILTER. A drop that rolls Legendary arrives at
#: +4 or better rather than being downgraded, which is what lets magic find do its
#: job at a low difficulty tier. Rolling the rarity first is what makes that
#: possible; deciding the upgrade level first would have fixed it before the
#: rarity was known.
RARITY_GEAR_LEVEL_GATE: dict[str, int] = {
    "Everyday":    0,
    "Quality":     0,
    "Superb":      0,
    "Masterful":   0,
    "Legendary":   4,
    "Mythical":    6,
    "Ascendant":   8,
    "Cataclysmic": 10,
}


def rarity_index(rarity: str) -> int:
    """Where a rarity sits on the ladder. 1 is Everyday and 8 is Cataclysmic.

    One-based so it lines up with the difficulty tier directly: the design says
    gear rarity equals the difficulty tier, and there are eight of each.
    """
    if rarity not in af.RARITIES:
        raise ValueError(f"{rarity!r} is not a rarity; expected one of "
                         f"{list(af.RARITIES)}")
    return af.RARITIES.index(rarity) + 1


def rarity_at_index(index: int) -> str:
    """The rarity at a position on the ladder, 1 to 8."""
    if not 1 <= index <= len(af.RARITIES):
        raise ValueError(f"index {index} is outside 1 to {len(af.RARITIES)}")
    return af.RARITIES[index - 1]


def best_rarity_on_a_drop(tier: int) -> str:
    """The highest rarity a drop may roll at a difficulty tier.

    Gear rarity equals the difficulty tier, plus the one-above that makes a drop
    worth reading, capped at Cataclysmic. So tiers 7 and 8 both reach
    Cataclysmic, the same way affix tiers 6, 7 and 8 all reach T7.
    """
    if not 1 <= tier <= af.DIFFICULTY_TIERS:
        raise ValueError(f"tier {tier} is outside 1 to {af.DIFFICULTY_TIERS}")
    return rarity_at_index(
        min(len(af.RARITIES), tier + DROP_RARITIES_ABOVE_DIFFICULTY))


def gear_level_gate(rarity: str) -> int:
    """The upgrade level a piece must reach before it can be this rarity."""
    if rarity not in RARITY_GEAR_LEVEL_GATE:
        raise ValueError(f"{rarity!r} is not a rarity; expected one of "
                         f"{list(af.RARITIES)}")
    return RARITY_GEAR_LEVEL_GATE[rarity]


def rarity_step_chance(index: int, magic_find: float = 0.0) -> float:
    """The chance the cascade stops at this rung, given that it reached it.

    THE RUNG'S WEIGHT AS A SHARE OF EVERYTHING AT OR BELOW IT. That is what turns
    a table of weights into a cascade, and the arithmetic is worth stating because
    it is not obvious: stopping at rung R with chance w[R]/S[R], where S[R] is the
    weight of rungs 1 to R, leaves S[R-1]/S[R] to carry on. Multiply those down
    from the top and every rung ends up with w[R]/S[N] -- its own share of the
    whole reachable ladder, which is what a weight is supposed to mean.

    With every weight equal this is one over the rung's position, and the
    distribution is flat.

    MAGIC FIND MULTIPLIES IT, which is Path of Exile's stated behaviour: +100%
    increased item rarity gives twice as many of every rarity above the floor.
    Saturating at 1 is the only ceiling; see the module docstring for the
    diminishing returns that are deliberately not built.
    """
    if index < 1:
        raise ValueError(f"index {index} is below 1")
    if magic_find < 0.0:
        raise ValueError(
            f"magic find is {magic_find}; it is an added percentage with a "
            "baseline of zero and cannot be negative")

    at_or_below = sum(RARITY_DROP_WEIGHT[rarity_at_index(rung)]
                      for rung in range(1, index + 1))
    if at_or_below <= 0.0:
        raise ValueError(
            f"every rarity up to {rarity_at_index(index)} has a drop weight of "
            "zero, so there is nothing for the cascade to choose between")

    share = RARITY_DROP_WEIGHT[rarity_at_index(index)] / at_or_below
    return min(1.0, share * (1.0 + magic_find / 100.0))


def rarity_distribution(tier: int, magic_find: float = 0.0) -> dict[str, float]:
    """What fraction of drops at this tier is each rarity.

    EXACT RATHER THAN SAMPLED, so a test can check the shape without running ten
    thousand rolls and then arguing about noise. `roll_rarity` draws from exactly
    this distribution, and a test compares the two.

    Every rarity is a key, including the ones this tier cannot reach, which carry
    zero. A caller asking about a rarity out of reach should get 0.0 rather than
    a KeyError.
    """
    best = rarity_index(best_rarity_on_a_drop(tier))

    out = {rarity: 0.0 for rarity in af.RARITIES}
    left = 1.0
    for index in range(best, 1, -1):
        chance = rarity_step_chance(index, magic_find)
        out[rarity_at_index(index)] = left * chance
        left *= 1.0 - chance

    # WHATEVER FELL THROUGH EVERY RUNG IS THE FLOOR, which is what makes this a
    # cascade rather than a table of weights that has to sum to one by hand.
    out[af.RARITIES[0]] = left
    return out


def roll_rarity(tier: int, magic_find: float, rng) -> str:
    """The rarity one drop rolls. `rng` is a `random.Random`.

    Walks the rungs from the rarest down and stops at the first that succeeds,
    which is the cascade itself rather than a lookup into `rarity_distribution`.
    Written that way on purpose: the test that the two agree is what proves the
    exact distribution above describes what actually happens.
    """
    best = rarity_index(best_rarity_on_a_drop(tier))

    for index in range(best, 1, -1):
        if rng.random() < rarity_step_chance(index, magic_find):
            return rarity_at_index(index)
    return af.RARITIES[0]


# --------------------------------------------------------------------------
# Checks that run on import, the same way affixes.py does it.
# --------------------------------------------------------------------------

def _check_every_rarity_has_a_gear_level_gate() -> None:
    missing = set(af.RARITIES) - set(RARITY_GEAR_LEVEL_GATE)
    extra = set(RARITY_GEAR_LEVEL_GATE) - set(af.RARITIES)
    if missing or extra:
        raise AssertionError(
            "the gear level gate table does not match the rarity ladder: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_the_gates_only_ever_rise() -> None:
    """A rarer piece never requires a lower upgrade level than a weaker one."""
    gates = [RARITY_GEAR_LEVEL_GATE[rarity] for rarity in af.RARITIES]
    for lower, higher in zip(gates, gates[1:], strict=False):
        if higher < lower:
            raise AssertionError(
                f"the gear level gates fall somewhere along the ladder: {gates}")


def _check_no_gate_is_out_of_reach() -> None:
    """Nothing requires an upgrade level a piece cannot be brought to."""
    for rarity, gate in RARITY_GEAR_LEVEL_GATE.items():
        if not 0 <= gate <= af.MAX_GEAR_LEVEL:
            raise AssertionError(
                f"{rarity} requires gear level {gate}, which is outside 0 to "
                f"{af.MAX_GEAR_LEVEL}")


def _check_every_rarity_has_a_drop_weight() -> None:
    missing = set(af.RARITIES) - set(RARITY_DROP_WEIGHT)
    extra = set(RARITY_DROP_WEIGHT) - set(af.RARITIES)
    if missing or extra:
        raise AssertionError(
            "the drop weight table does not match the rarity ladder: "
            f"missing {sorted(missing)}, unexpected {sorted(extra)}")


def _check_no_drop_weight_is_negative() -> None:
    for rarity, weight in RARITY_DROP_WEIGHT.items():
        if weight < 0.0:
            raise AssertionError(
                f"{rarity} has a drop weight of {weight}; a weight is a share "
                "and cannot be negative")


def _check_the_distribution_matches_the_weights() -> None:
    """Each rarity's share of drops is its weight's share of the reachable ones.

    This is the claim the whole module rests on -- that the cascade in
    `rarity_step_chance` really does turn a table of weights into the
    distribution those weights describe. It is not obvious from the arithmetic,
    and getting it wrong would bias the drops in a way nobody chose and nothing
    else would notice.
    """
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        spread = rarity_distribution(tier)
        reachable = rarity_index(best_rarity_on_a_drop(tier))
        total = sum(RARITY_DROP_WEIGHT[rarity_at_index(rung)]
                    for rung in range(1, reachable + 1))

        for index in range(1, reachable + 1):
            rarity = rarity_at_index(index)
            expected = RARITY_DROP_WEIGHT[rarity] / total
            if abs(spread[rarity] - expected) > 1e-9:
                raise AssertionError(
                    f"at tier {tier}, {rarity} is {spread[rarity]:.6f} of "
                    f"drops against the {expected:.6f} its weight asks for")


def _check_nothing_drops_above_the_tier_cap() -> None:
    """Not even with a magic find nothing could reach."""
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        spread = rarity_distribution(tier, magic_find=1000.0)
        reachable = rarity_index(best_rarity_on_a_drop(tier))

        for index in range(reachable + 1, len(af.RARITIES) + 1):
            if spread[rarity_at_index(index)] != 0.0:
                raise AssertionError(
                    f"tier {tier} can drop {rarity_at_index(index)}, which is "
                    f"above its cap of {best_rarity_on_a_drop(tier)}")


def _check_a_distribution_always_sums_to_one() -> None:
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for magic_find in (0.0, 50.0, 100.0, 400.0, 1000.0):
            total = sum(rarity_distribution(tier, magic_find).values())
            if abs(total - 1.0) > 1e-9:
                raise AssertionError(
                    f"at tier {tier} with {magic_find}% magic find the "
                    f"distribution sums to {total}, not 1")


_check_every_rarity_has_a_gear_level_gate()
_check_the_gates_only_ever_rise()
_check_no_gate_is_out_of_reach()
_check_every_rarity_has_a_drop_weight()
_check_no_drop_weight_is_negative()
_check_the_distribution_matches_the_weights()
_check_nothing_drops_above_the_tier_cap()
_check_a_distribution_always_sums_to_one()
