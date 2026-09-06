"""Player decision policies.

These exist to answer one question: does it matter which dungeon you pick?

Every policy now chooses between three things, not two:
  * run a dungeon (defends a city, earns loot and materials, slow power gain)
  * go to the forge (fast power gain, defends nothing, burns materials)
  * do nothing

If a careless policy and a careful policy survive equally long, the empire
layer is not a game, it is a slideshow. The gap between the best and worst
policy is therefore the primary design signal this rig produces.

WHICH POLICIES SEE A SIEGE, AND WHY NOT ALL OF THEM. Issue #1340. A Siege's
damage happens every day it stands rather than when its timer fires, so it was
invisible to the only term any policy had for what a dungeon costs a city --
`d.defense_damage`, what ONE RESOLVE costs. The project owner ruled on
2026-09-06, verbatim: "Fix the policies soon, before more figures accumulate
(Recommended)".

`triage` and `lane_aware` are the two policies that actually weigh damage
prevented, and they are the two that were fixed. **The others are controls and
were deliberately left blind.** `random_pick` has to stay random, `greedy_loot`
has to go on ignoring the empire and `always_craft` has to go on letting cities
burn, or the gap this rig exists to measure closes because the floor came up
rather than because the ceiling did. `never_craft` and `nearest_deadline` sort
by a timer and compute no damage at all, so there is no calculation in them to
repair; teaching either one about a Siege would be giving it a new capability,
which is the thing the owner's ruling did not ask for.
"""

from __future__ import annotations

from .config import CityTier, DungeonType
from .engine import CRAFT

TIER_VALUE = {
    CityTier.OUTPOST: 1.0,
    CityTier.BULWARK: 3.0,
    CityTier.SANCTUARY: 8.0,
    CityTier.PILLAR: 100.0,
}


def _safe(sim, dungeons):
    """Dungeons the player can attempt without an unacceptable death risk."""
    tol = sim.cfg.death_risk_tolerance
    return [d for d in dungeons if sim.death_chance(d) <= tol]


def _endgame(dungeons):
    """The Cataclysm dungeon ends the run. A real player takes that shot at any
    odds rather than sitting at the capital watching the empire burn, so it
    bypasses the risk filter entirely."""
    for d in dungeons:
        if d.dtype is DungeonType.CATACLYSM:
            return d
    return None


def _stuck(sim, dungeons) -> bool:
    """Nothing is safely attemptable -- power is the bottleneck, not time."""
    return bool(dungeons) and not _safe(sim, dungeons)


def siege_daily_damage(sim, d, city) -> float:
    """Points of defence this dungeon takes from its city EVERY DAY it stands.

    0.0 for anything that is not a Siege, which is what keeps this change out
    of every other dungeon's score.

    A MIRROR OF `Simulation._apply_siege_damage` AND NOT A SECOND OPINION. The
    flat share, the growth of 2.5 points a day, the day the growth counts from,
    the damage-reduction multiplier and the rule that a fallen city is not
    bitten again all have to match the engine, or a policy would be reasoning
    about damage the day loop does not actually deal.
    `tests/test_policies_see_sieges.py` drives the two against each other for
    that reason rather than restating the arithmetic. **The growth was ten
    points a day until the owner cut it on issue #1349 on 2026-09-06**, and this
    sentence still said ten afterwards, which is half of what issue #1364 is
    about. `tests/test_the_siege_prose_in_policies_is_true.py` now reads the
    figure out of this docstring and compares it against
    `TuningConfig.siege_damage_growth_per_day`.

    ANY DUNGEON TYPE CAN CARRY THE SUB-TYPE, not only the ordinary ones:
    `_roll_subtype` runs for every `_make_dungeon`. Measured on 2026-09-06 over
    10,000 campaigns at the settings `siege_urgency` names below, the 89,652
    Sieges that reached the map were 78% Basic, 11% Quest, 10% Fallen City and
    1% Cataclysm, about 8.97 a campaign. That is why the callers below apply
    this in all three scoring branches instead of only the ordinary one.

    THE RATE FELL FROM 10.6 A CAMPAIGN AND THE FOUR SHARES DID NOT MOVE AT ALL.
    Issue #1357 changed when the Cataclysm dungeon opens, so campaigns end
    sooner and fewer dungeons of every kind reach the map. Which kinds carry a
    Siege is a property of `_roll_subtype` and not of how long a campaign runs,
    which is why one number moved and the other four did not -- and it is the
    shares rather than the rate that the sentence above exists to justify.

    THE COUNTS THAT USED TO BE HERE -- 114 Basic, 28 Fallen City, 11 Quest and 3
    Cataclysm over twelve campaigns -- were taken before two changes of the same
    day. The owner halved the Siege spawn weight on issue #1349, and a surge
    began rolling Quest dungeons on issue #1324. Twelve campaigns could not have
    separated Quest from Fallen City in any case, and the order was wrong.
    """
    if d.subtype != "Siege" or city is None or city.fallen:
        return 0.0
    cfg = sim.cfg
    days_stood = max(0, sim.day - d.spawned_day)
    grown = cfg.siege_damage_growth_per_day * days_stood
    return ((city.max_defense * cfg.siege_defence_bite_per_day + grown)
            * cfg.tree.city_damage_mult)


def siege_damage_during_the_walk(sim, d, city) -> float:
    """Defence the Siege takes from its city between now and the player
    finishing this dungeon. 0.0 for anything that is not a Siege.

    ENTERING A SIEGE DOES NOT PAUSE IT. `Simulation.step` applies the daily
    bite before any timer and wherever the player is, so a Siege goes on eating
    the city for every day of the walk and the walk is part of the cost of
    answering it. That is the opposite of every other dungeon, whose timer
    stops while the player is inside it.

    The growth is included, because it does not stop either: over `k` days the
    bite rises by `siege_damage_growth_per_day` each day, which is the closed
    form below rather than `k` times today's bite.
    """
    per_day = siege_daily_damage(sim, d, city)
    if per_day <= 0.0:
        return 0.0
    cfg = sim.cfg
    k = max(1, d.run_days)
    growth = cfg.siege_damage_growth_per_day * cfg.tree.city_damage_mult
    return k * per_day + growth * k * (k - 1) / 2.0


def siege_urgency(sim, d, city, fatal_mult: float) -> float:
    """How much more this dungeon is worth clearing because a Siege stands on
    it. Exactly 1.0 -- no change at all -- for everything that is not one.

    AN UNATTENDED SIEGE ALWAYS KILLS ITS CITY. That is not a risk, it is the
    sub-type's definition: 25 days for an Outpost, 39 for a Bulwark, 55 for a
    Sanctuary, 70 for the Pillar. So "will this city die?" is settled before
    the policy is asked, and the only live question is **whether the player can
    still get there in time**. This returns the policy's own `fatal` multiplier
    when they can and 1.0 when they cannot.

    THOSE FOUR WERE 14 / 23 / 34 / 47 until the owner cut the growth from ten
    points a day to 2.5 on issue #1349 on 2026-09-06. They are not targets: the
    constant moved and they followed it. The same day loop still returns the old
    four when the growth is put back to ten, which is how the derivation is
    checked -- `tests/test_the_siege_prose_in_policies_is_true.py` drives both.

    WHY IT IS THAT SHAPE AND NOT A SLIDING SCALE. The first version of this
    scaled with the share of the city the Siege would eat during the walk, and
    it was backwards: it gave FULL weight to Sieges that were already
    unwinnable, because they eat all of it, and the LEAST weight to a Siege
    that had just arrived, which is the only moment it can still be answered.
    Measured over 600 campaigns it moved the acceptance ratio from 1.01 to
    1.03, which is to say barely at all.

    WHY THE DISTINCTION STILL MATTERS NOW THAT THE MARGIN IS NOT TIGHT. At
    difficulty tier 1 -- one active Cataclysm -- with no empire tree, `triage`,
    and static surges of five dungeons every 120 days, the median walk is 14
    days to an Outpost, 23 to a Bulwark, 37 to a Sanctuary and 123 to the
    Pillar. Against the 25 / 39 / 55 days a fresh Siege leaves those three
    sizes, the slack is 11 / 16 / 18 days. Measured on 2026-09-06 over 10,000
    campaigns and 1,250,908 dungeons, each recorded at the moment it was made;
    issue #1364.

    QUOTE THOSE TO THE DAY AND NO FINER, AND RE-MEASURE RATHER THAN CARRYING
    THEM FORWARD. The walk lengths are nearly uniform where the median sits --
    49.0% of Outpost dungeons walk in 13 days or fewer and 56.2% in 14 or fewer
    -- so the median is a coin flip between two adjacent days, and a block of
    200 campaigns lands on either side of it at random. That is how this file
    came to state 12 / 20 / 33 while issue #1364 measured 14 / 22 / 33 on the
    same code: neither was reproducible, and neither was wrong by much.
    `tests/test_the_siege_prose_in_policies_is_true.py` re-measures all four and
    allows a day either way.

    IT MOVED WHILE THIS PARAGRAPH WAS BEING WRITTEN, WHICH IS THE ARGUMENT FOR
    THE GUARD. Issue #1369 put the Cow Level spawn weight back to 7 from the 7.6
    the Siege rescale of issue #1349 had lifted it to, and a Cow Level walks in
    twice the days. The Sanctuary median fell from 35 to 34 between the first
    draft of this paragraph and the second, and the guard failed in continuous
    integration rather than a reader finding it a month later. Re-measured on
    `e8b33c2`.

    AND IT MOVED AGAIN, FOR A REASON THAT IS NOT ABOUT WALKS AT ALL. Issue #1357
    changed WHEN A CAMPAIGN ENDS: the Cataclysm dungeon used to open at a flat
    total of 8 quest objectives and now opens when half the active Cataclysms,
    rounded up, have each met their own count -- at tier 1 that is the one
    active Cataclysm's own number, which is 5, 8 or 10 depending on which one
    the character drew. Campaigns therefore run to different lengths, the mix of
    city sizes a surge has left to hit changes with them, and every figure above
    is an average over that mix. The three medians rose by 1, 1 and 3 days and
    the sample fell from 1,488,436 dungeons to 1,250,908. **Nothing about a walk
    changed.** Re-measured over 10,000 campaigns on 2026-09-06; the guard failed
    first, which is what it is for.

    THE OUTPOST MEDIAN NOW SITS HIGHER INSIDE ITS OWN BIN THAN IT DID, and that
    is worth knowing before the next change moves it again. 56.2% of Outpost
    dungeons walk in the stated 14 days or fewer, where the figure was 50.5% at
    the stated 13. The guard asks for that share to be between 44% and 58%, so
    the headroom above is now under two points rather than seven and a half. It
    passes; the next thing that lengthens a campaign is likely to trip it, and
    the answer then is to re-measure rather than to widen the range. Issue #1389
    carries what to look at when it does.

    THE SETTINGS ARE PART OF THE FIGURE. Five dungeons a surge is what the
    balance report uses and NOT `TuningConfig.surge_dungeon_count`, which is 4.
    The two are different worlds, and at the larger city sizes the medians
    differ by more than the day of wobble above. Issue #1286 names the block.

    THAT MAKES THE ANSWER REACHABLE AND NOT AUTOMATIC. A Siege that has already
    stood for a while is still hopeless, and this returns 1.0 for it. So is
    every Siege on the Pillar, and that one is exact rather than typical. A
    surge cannot target the Pillar, so the only dungeons that spawn there are
    the Cataclysm at 100 to 150 floors and, once the Pillar falls, its own
    Fallen City at 80 to 120 -- and both openers only ever ADD floors to a
    Cataclysm. The shortest walk to any of them is therefore 80 days against the
    70 a fresh Siege leaves, so a Pillar Siege can never be answered: not at the
    median, and not at the best roll the model can produce. What changed is that
    the reachable case is now common instead of vanishing, which is why the
    policy has to be able to tell the two apart at all.

    WHY IT IS ANCHORED TO THE POLICY'S OWN `fatal` MULTIPLIER RATHER THAN TO A
    NEW CONSTANT. Each policy already has a number for "the damage about to
    land kills this city": triage multiplies by 5, lane_aware by 4. A Siege the
    player can still reach is that same statement about a different clock, so
    it earns the same weight and no more. Inventing a separate weight would
    have been a third balance number to tune with nothing anchoring it.
    """
    during = siege_damage_during_the_walk(sim, d, city)
    if during <= 0.0:
        return 1.0
    if during >= city.defense:
        # The city falls before the player could arrive, so there is nothing
        # left to save by going. A policy that chased it anyway would spend the
        # walk losing the city it went to defend AND whatever it walked past.
        return 1.0
    return fatal_mult


# ---------------------------------------------------------------------------

def random_pick(sim, dungeons):
    """Baseline incompetence. Never crafts."""
    safe = _safe(sim, dungeons)
    if not safe:
        return CRAFT if sim.can_craft() else None
    return sim.rng.choice(safe)


def never_craft(sim, dungeons):
    """Pure defence. Refuses the forge entirely -- the control group for
    whether crafting time is worth its cost."""
    safe = _safe(sim, dungeons)
    if not safe:
        return None
    live = [d for d in safe if d.resolves]
    return min(live, key=lambda d: d.resolve_in) if live else max(
        safe, key=lambda d: d.floors)


def always_craft(sim, dungeons):
    """The other extreme: forge whenever materials allow, let cities burn."""
    if sim.can_craft():
        return CRAFT
    safe = _safe(sim, dungeons)
    if not safe:
        return None
    return max(safe, key=lambda d: d.floors)


def nearest_deadline(sim, dungeons):
    """Always answer the fire that is about to go off. Crafts only when
    genuinely blocked."""
    end = _endgame(dungeons)
    if end is not None:
        return end
    safe = _safe(sim, dungeons)
    if not safe:
        return CRAFT if sim.can_craft() else None
    live = [d for d in safe if d.resolves]
    if not live:
        return max(safe, key=lambda d: d.floors)
    return min(live, key=lambda d: d.resolve_in)


def greedy_loot(sim, dungeons):
    """Ignore the empire, farm the deepest thing you can survive."""
    safe = _safe(sim, dungeons)
    if not safe:
        return CRAFT if sim.can_craft() else None
    return max(safe, key=lambda d: d.floors)


def triage(sim, dungeons):
    """The policy a good player would converge on.

    Weighs three things against each other: damage prevented per day, progress
    toward the win, and the fact that power is itself a prerequisite for both.
    Crafts when the empire is stable enough to afford the detour, or when it
    is underpowered and slogging on would be worse.
    """
    cfg = sim.cfg
    end = _endgame(dungeons)
    if end is not None:
        return end

    safe = _safe(sim, dungeons)
    danger = sim.empire.breach_depth() / 3.0

    # Power bottleneck: nothing safely runnable, or the Cataclysm dungeon is
    # sitting there out of reach. Forge time is the only way out.
    if sim.can_craft():
        if _stuck(sim, dungeons):
            return CRAFT
        # Comfortable empire and materials to burn -> bank power now, because
        # the endgame dungeon needs far more than looting alone will supply.
        if danger == 0.0 and sim.materials >= cfg.craft_material_cost * 1.5:
            hot = [d for d in safe if d.resolves and d.resolve_in <= cfg.craft_days]
            if not hot:
                return CRAFT

    if not safe:
        return None

    best, best_score = None, float("-inf")
    for d in safe:
        city = sim.empire.cities[d.city_id]
        # A Siege bites daily whatever KIND of dungeon carries it, so this is
        # worked out once and applied in all three branches below. Issue #1340.
        siege = siege_urgency(sim, d, city, 5.0)

        if d.dtype is DungeonType.QUEST:
            # THIS IS STALE AND IT IS ISSUE #1388. Both halves of the
            # subtraction stopped meaning what they say when the owner ruled on
            # 2026-09-06: `quest_objectives_required` is now only a fallback for
            # a Cataclysm the roster does not name, and the gate reads
            # `Simulation.cataclysms_complete` rather than this total. It is a
            # heuristic and it still steers a campaign, so it was left alone
            # rather than moved in the same change that moved the rule.
            remaining = max(1, cfg.quest_objectives_required - sim.objectives)
            value = 14.0 * (1.0 - 0.6 * danger) * (1.0 + 1.0 / remaining)
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if d.dtype is DungeonType.FALLEN_CITY:
            value = TIER_VALUE[d.city_tier] * 4.0
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if not d.resolves:
            continue

        # NEITHER THE DEPTH SCALE NOR THE TREE IS APPLIED, which was true of
        # the fraction this replaced as well. A policy guesses; issue #1327
        # changed the shape of the number and deliberately not the guess.
        bite = d.defense_damage
        fatal = bite >= city.defense
        value = TIER_VALUE[d.city_tier] * (1.0 + (1.0 - city.defense_frac) * 3.0)
        if fatal:
            value *= 5.0

        # Entering pauses this dungeon's timer, so the only question is whether
        # it survives long enough for the player to arrive at all.
        #
        # A SIEGE STILL WORTH ANSWERING IS EXEMPT FROM THAT DISCOUNT, and it is
        # the same defect as the one above rather than a second change. The
        # discount says a dungeon whose timer has already fired has already
        # spent its damage; a Siege has spent none of it, because its damage is
        # not at resolve time. Discounting it would rank a Siege LOWEST exactly
        # while it is doing the most harm. Issue #1340.
        if d.resolve_in <= 0 and siege <= 1.0:
            value *= 0.2

        score = value * siege / max(1, d.run_days)
        if score > best_score:
            best, best_score = d, score

    if best is None:
        return max(safe, key=lambda d: d.floors) if safe else None
    return best


def lane_aware(sim, dungeons):
    """Triage, but scoring cities by what losing them does to the MAP rather
    than by their tier.

    The run does not end when you have lost a lot of cities; it ends when three
    fall in a line. So a city is worth defending in proportion to how much its
    loss would shorten the Cataclysm's path to the Pillar -- and cities off
    every shortest lane are, structurally, free to give away.
    """
    cfg = sim.cfg
    end = _endgame(dungeons)
    if end is not None:
        return end

    safe = _safe(sim, dungeons)
    crit = sim.empire.lane_criticality()
    d2d = sim.empire.distance_to_defeat()
    # 3 = untouched, 0 = the Last Stand is firing.
    peril = (3 - d2d) / 3.0

    if sim.can_craft():
        if _stuck(sim, dungeons):
            return CRAFT
        # Forge only while no lane is genuinely threatened.
        if d2d >= 3 and sim.materials >= cfg.craft_material_cost * 1.5:
            hot = [d for d in safe if d.resolves and d.resolve_in <= cfg.craft_days]
            if not hot:
                return CRAFT

    if not safe:
        return None

    best, best_score = None, float("-inf")
    for d in safe:
        city = sim.empire.cities[d.city_id]
        # As in `triage`, and for the same reason: the sub-type rides on every
        # kind of dungeon, so this cannot live in the ordinary branch alone.
        # The weight is this policy's own `fatal` multiplier. Issue #1340.
        siege = siege_urgency(sim, d, city, 4.0)

        if d.dtype is DungeonType.QUEST:
            # THIS IS STALE AND IT IS ISSUE #1388. Both halves of the
            # subtraction stopped meaning what they say when the owner ruled on
            # 2026-09-06: `quest_objectives_required` is now only a fallback for
            # a Cataclysm the roster does not name, and the gate reads
            # `Simulation.cataclysms_complete` rather than this total. It is a
            # heuristic and it still steers a campaign, so it was left alone
            # rather than moved in the same change that moved the rule.
            remaining = max(1, cfg.quest_objectives_required - sim.objectives)
            value = 14.0 * (1.0 - 0.7 * peril) * (1.0 + 1.0 / remaining)
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if d.dtype is DungeonType.FALLEN_CITY:
            # Retaking re-seals a lane. Worth most when it is THE lane.
            reseal = 1 + 8 * crit.get(city.cid, 0)
            value = 6.0 * reseal * (1.0 + 2.0 * peril)
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if not d.resolves:
            continue

        # A city off every shortest lane is worth little no matter its tier.
        lane = 1.0 + 9.0 * crit.get(city.cid, 0)
        fatal = d.defense_damage >= city.defense
        value = lane * (1.0 + (1.0 - city.defense_frac) * 2.0) * (1.0 + 2.0 * peril)
        if fatal:
            value *= 4.0
        # A Siege still worth answering is exempt from the fired-timer discount
        # -- it has not spent its damage, it is still spending it. Issue #1340.
        if d.resolve_in <= 0 and siege <= 1.0:
            value *= 0.2

        score = value * siege / max(1, d.run_days)
        if score > best_score:
            best, best_score = d, score

    if best is None:
        return max(safe, key=lambda d: d.floors) if safe else None
    return best


ALL = {
    "random": random_pick,
    "greedy_loot": greedy_loot,
    "never_craft": never_craft,
    "always_craft": always_craft,
    "nearest_deadline": nearest_deadline,
    "triage": triage,
    "lane_aware": lane_aware,
}
