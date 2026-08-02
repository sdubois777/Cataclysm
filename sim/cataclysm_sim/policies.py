"""Player decision policies.

These exist to answer one question: does it matter which dungeon you pick?

Every policy now chooses between three things, not two:
  * run a dungeon (defends a city, earns loot and materials, slow power gain)
  * go to the forge (fast power gain, defends nothing, burns materials)
  * do nothing

If a careless policy and a careful policy survive equally long, the empire
layer is not a game, it is a slideshow. The gap between the best and worst
policy is therefore the primary design signal this rig produces.
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

        if d.dtype is DungeonType.QUEST:
            remaining = max(1, cfg.quest_objectives_required - sim.objectives)
            value = 14.0 * (1.0 - 0.6 * danger) * (1.0 + 1.0 / remaining)
            score = value / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if d.dtype is DungeonType.FALLEN_CITY:
            value = TIER_VALUE[d.city_tier] * 4.0
            score = value / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if not d.resolves:
            continue

        bite = d.defense_bite * city.max_defense
        fatal = bite >= city.defense
        value = TIER_VALUE[d.city_tier] * (1.0 + (1.0 - city.defense_frac) * 3.0)
        if fatal:
            value *= 5.0

        # Entering pauses this dungeon's timer, so the only question is whether
        # it survives long enough for the player to arrive at all.
        if d.resolve_in <= 0:
            value *= 0.2

        score = value / max(1, d.run_days)
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

        if d.dtype is DungeonType.QUEST:
            remaining = max(1, cfg.quest_objectives_required - sim.objectives)
            value = 14.0 * (1.0 - 0.7 * peril) * (1.0 + 1.0 / remaining)
            score = value / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if d.dtype is DungeonType.FALLEN_CITY:
            # Retaking re-seals a lane. Worth most when it is THE lane.
            reseal = 1 + 8 * crit.get(city.cid, 0)
            value = 6.0 * reseal * (1.0 + 2.0 * peril)
            score = value / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if not d.resolves:
            continue

        # A city off every shortest lane is worth little no matter its tier.
        lane = 1.0 + 9.0 * crit.get(city.cid, 0)
        fatal = d.defense_bite * city.max_defense >= city.defense
        value = lane * (1.0 + (1.0 - city.defense_frac) * 2.0) * (1.0 + 2.0 * peril)
        if fatal:
            value *= 4.0
        if d.resolve_in <= 0:
            value *= 0.2

        score = value / max(1, d.run_days)
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
