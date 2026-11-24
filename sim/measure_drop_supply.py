"""Drop supply at difficulty tier 1: gear quantity, gems, resistance, and the level the gap rule leaves a character.

WHY THIS EXISTS. Issue #2853. The owner ruled on 2026-11-24, choosing "Adopt, and look at drop supply (Recommended)":
adopt the plain Power Score weights (10 a level; a piece 2 x rarity x (1 + upgrade level); 5 a gem rarity step; 1 a
resistance percent) and separately investigate drop supply at tier 1, the larger cause of a tier 1 character
outscoring its dungeons. The owner said earlier that the common rarities feel right ("I don't think we should nerf the
loot too much"), so every rarity chance here is left as it is; what is measured is how MANY things drop and what they
carry.

This is a MEASUREMENT FOR A PROPOSAL. It changes nothing in the model: `cataclysm_sim/loot.py`, `affixes.py`,
`player_power.py` and the game are untouched. Four questions, one section each:

    1. GEAR QUANTITY: a factor on the Enemy Drops sheet's gear column, every enemy rarity alike, so the column keeps
       its shape. What it does to the drops per dungeon, the best piece per slot and the score after 1, 2, 4 dungeons.
    2. GEMS: `loot.GEM_DROPS_PER_GEAR_DROP` is one figure for every tier, set by tier 8's need. A factor on the gem
       column for each difficulty tier, held apart from the gear factor, so gems per kill are what is chosen.
    3. RESISTANCE: the three resistance families' floors (issue #1230) and whether the all-resistances family can roll
       on a tier 1 drop.
    4. LEVEL: where a character ends each tier on the experience curve with the gap rule of 2026-11-22 on, and where
       the character play produces sits against the rule's band.

THE WALK IS `measure_power_score_weights.py`'s (Outpost dungeons of 5 to 10 floors, Basic, tier 1, 258 creatures a
floor, every one killed, one Boss on the last floor, the equip rule there), re-implemented here only where a supply
lever has to reach inside it: the gear count, the gem count, the item roll (to leave a family out) and the resistance a
piece grants (to read other floors). `roll_item` mirrors `loot.roll_item`, and a test checks that with nothing left out
it rolls the very same item from the same seed, so it cannot drift from the model unnoticed.

EVERY SCORE IS UNDER THE RULED WEIGHTS (`measure_power_score_weights.RECOMMENDED`, adopted 2026-11-24).

Run from `sim/`: python -u measure_drop_supply.py     (about eight minutes; the whole-campaign rows are most of it).
Every walk is seeded, so two runs print the same figures.
"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import math
import random
import statistics

from cataclysm_sim import affixes as af
from cataclysm_sim import loot, scoring
from cataclysm_sim.config import CityTier

import measure_power_score_weights as mw

# THE ANALYSIS SCRIPTS PRINT THEIR REPORTS ON IMPORT; only their figures are used here.
with contextlib.redirect_stdout(io.StringIO()):
    import analyse_gem_drops as gem_need

curve = mw.curve
gap_rewards = mw.gap_rewards

TIER = mw.TIER
WEIGHTS = mw.RECOMMENDED
SEEDS = 40
CHECKPOINTS = mw.CHECKPOINTS
CAMPAIGN_SEEDS = 4


# --------------------------------------------------------------------------
# The supply levers
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Supply:
    """What tier 1 drops, as factors on today's tables and the resistance families' state.

    `gear` multiplies every row of the Enemy Drops sheet's gear column (so loot quantity, which scales the count, is
    untouched). `gems` multiplies the gem column at tier 1, independently of `gear`: gems per kill is what it sets.
    `floors` replaces the single, two and all resistances families' floors; None keeps #1230's 10, 7 and 3.
    `all_resistance` False leaves the all-resistances family out of a tier 1 drop's draw; another suffix takes its
    place, as the draw already does for any affix it cannot use.
    """

    name: str
    gear: float = 1.0
    gems: float = 1.0
    floors: tuple[float, float, float] | None = None
    all_resistance: bool = True


TODAY = Supply("today")


def families(supply: Supply) -> dict[str, af.AffixFamily]:
    """The resistance families a supply's pieces are read with, by name."""
    if supply.floors is None:
        return {family.name: family for family in af.RESISTANCE_FAMILIES}
    return {family.name: dataclasses.replace(family, floor=floor)
            for family, floor in zip(af.RESISTANCE_FAMILIES, supply.floors, strict=True)}


def excluded(supply: Supply) -> tuple:
    return () if supply.all_resistance else (af.ALL_RESISTANCE,)


# --------------------------------------------------------------------------
# One item, one kill
# --------------------------------------------------------------------------

def roll_item(slot: str, tier: int, magic_find: float, rng, exclude: tuple = ()) -> loot.RolledItem:
    """`loot.roll_item`, step for step, with the affixes in `exclude` taken out of the draw.

    The candidates are built first and filtered after, so the random stream is consumed exactly as `loot.roll_item`
    consumes it until the draw itself; with nothing excluded the two return the same item from the same seed, which
    `test_the_item_roll_is_the_models_own_when_nothing_is_left_out` checks.
    """
    rarity = loot.roll_rarity(tier, magic_find, rng)
    base = rng.choice(af.bases_for(slot))
    prefixes, suffixes = loot.split_for_a_drop(af.affix_slots_for(rarity), rng)
    drawn: list = []
    for position, count in (("prefix", prefixes), ("suffix", suffixes)):
        if count == 0:
            continue
        candidates = [c for c in loot._candidates_for(slot, position, rng)
                      if not any(c.affix is gone for gone in exclude)]
        drawn.extend(af.draw_without_repeating_a_group(candidates, count, rng, group_of=loot._groups_of_candidate))
    rolled = tuple(loot.RolledAffix(affix=c.affix, tier=af.roll_affix_tier(tier, rng), roll=rng.random(),
                                    covers=c.covers)
                   for c in drawn)
    return loot.RolledItem(base=base, gear_level=loot.gear_level_gate(rarity), affixes=rolled,
                           enchantments=af.enchantments_for(rarity), residue=loot.roll_residue(rarity, rng),
                           sockets=loot.roll_sockets(base, rng))


def resistance_of(item: loot.RolledItem, read: dict[str, af.AffixFamily]) -> list[float]:
    """The eight resistances one piece grants, each family read with the supply's floors."""
    out = [0.0] * len(af.DAMAGE_TYPES)
    for rolled in item.affixes:
        if isinstance(rolled.affix, af.AffixFamily):
            value = read[rolled.affix.name].value_at(rolled.tier, rolled.roll, item.gear_level)
            for damage_type in rolled.covers:
                out[af.DAMAGE_TYPES.index(damage_type)] += value
    return out


class Character(mw.Character):
    """`measure_power_score_weights.Character`, reading resistance with a supply's floors, and counting what fell."""

    def __init__(self, supply: Supply) -> None:
        super().__init__()
        self.supply = supply
        self.read = families(supply)
        self.exclude = excluded(supply)
        self.gear_dropped = 0
        self.gems_dropped = 0

    def key(self, item: loot.RolledItem) -> tuple:
        return (loot.rarity_index(item.rarity), item.gear_level, sum(resistance_of(item, self.read)), item.sockets)

    def wear_if_better(self, item: loot.RolledItem) -> None:
        worn = self.worn[item.base.slot]
        key = self.key(item)
        if len(worn) < mw.POSITIONS[item.base.slot]:
            worn.append((key, item))
            return
        worst = min(range(len(worn)), key=lambda i: worn[i][0])
        if key > worn[worst][0]:
            worn[worst] = (key, item)

    def items(self) -> list[loot.RolledItem]:
        return [item for worn in self.worn.values() for _, item in worn]

    def parts(self) -> mw.Parts:
        items = self.items()
        sockets = sum(item.sockets for item in items)
        resistances = [0.0] * len(af.DAMAGE_TYPES)
        for item in items:
            for i, value in enumerate(resistance_of(item, self.read)):
                resistances[i] += value
        return mw.Parts(level=self.level,
                        pieces=tuple((loot.rarity_index(item.rarity), item.gear_level) for item in items),
                        gems=tuple(sorted(self.gems, reverse=True)[:sockets]),
                        resistances=tuple(resistances))

    def masterful_singles(self) -> float:
        """The share of the ten single-position slots (every slot but the eight Rings) worn at Masterful."""
        singles = [worn for slot, worn in self.worn.items() if mw.POSITIONS[slot] == 1]
        return sum(1 for worn in singles if worn and worn[0][1].rarity == "Masterful") / len(singles)

    def resistance_by_family(self) -> dict[str, float]:
        """Resistance points each family puts on the character, before the 70 cap."""
        out = dict.fromkeys(self.read, 0.0)
        for item in self.items():
            for rolled in item.affixes:
                if isinstance(rolled.affix, af.AffixFamily):
                    out[rolled.affix.name] += (self.read[rolled.affix.name].value_at(rolled.tier, rolled.roll,
                                                                                    item.gear_level)
                                               * len(rolled.covers))
        return out


def kill(c: Character, rarity: str, experience: float, loot_factor: float, rng: random.Random) -> None:
    """`measure_power_score_weights.kill` with the supply's gear and gem factors, in `loot`'s own order of draws."""
    c.gain(round(experience))
    quantity = loot.BASELINE_LOOT_QUANTITY * loot_factor
    count = loot.roll_gear_drop_count(rarity, quantity * c.supply.gear, rng)
    magic_find = loot.magic_find_from(rarity)
    for _ in range(count):
        c.wear_if_better(roll_item(loot.roll_slot(rng), TIER, magic_find, rng, c.exclude))
    c.gear_dropped += count
    gems = loot.roll_gem_drops_from_kill(rarity, TIER, 0.0, quantity * c.supply.gems, rng)
    c.gems_dropped += len(gems)
    c.gems.extend(loot.rarity_index(gem) for gem in gems)


def walk(c: Character, floors: int, rng: random.Random, rule=None, kill_share: float = 1.0) -> list[float]:
    """Clear one dungeon floor by floor, then its boss; returns each floor's gap factor (experience)."""
    score = mw.dungeon_score(floors)
    width = scoring.tier_width(TIER)
    killed = round(curve.POPULATION * kill_share)
    factors = []
    for floor in range(1, floors + 1):
        exp_factor = loot_factor = 1.0
        if rule is not None:
            gap = (score - WEIGHTS.score(c.parts())) / width
            exp_factor = rule.multiplier(gap)
            loot_factor = rule.multiplier(gap, loot=True)
        factors.append(exp_factor)
        enemy = scoring.enemy_scores(TIER, mw.DUNGEON_TYPE, mw.DUNGEON_SUBTYPE, floors, floor)
        for _ in range(killed):
            rarity = mw.spawn_rarity(rng)
            kill(c, rarity, enemy[rarity] * exp_factor, loot_factor, rng)
    boss = scoring.final_boss_score(TIER, mw.DUNGEON_TYPE, mw.DUNGEON_SUBTYPE, floors)
    kill(c, "Boss", boss * exp_factor, loot_factor, rng)
    return factors


@dataclasses.dataclass
class Point:
    """One seed's character after N dungeons."""

    dungeons: int
    parts: mw.Parts
    dungeon_scores: tuple[int, ...]
    gear_dropped: int
    gems_dropped: int
    by_family: dict[str, float]
    masterful_singles: float


def run(supply: Supply, seed: int, rule=None, kill_share: float = 1.0) -> list[Point]:
    rng = random.Random(seed)
    low, high = mw.floor_range(CityTier.OUTPOST)
    c = Character(supply)
    out, scores = [], []
    for n in range(1, max(CHECKPOINTS) + 1):
        floors = rng.randint(low, high)
        scores.append(mw.dungeon_score(floors))
        walk(c, floors, rng, rule, kill_share)
        if n in CHECKPOINTS:
            out.append(Point(n, c.parts(), tuple(scores), c.gear_dropped, c.gems_dropped, c.resistance_by_family(),
                             c.masterful_singles()))
    return out


def run_many(supply: Supply, rule=None, seeds: int = SEEDS, kill_share: float = 1.0) -> dict[int, list[Point]]:
    by_n: dict[int, list[Point]] = {n: [] for n in CHECKPOINTS}
    for seed in range(seeds):
        for point in run(supply, seed, rule, kill_share):
            by_n[point.dungeons].append(point)
    return by_n


def campaign(supply: Supply, seed: int, rule=None) -> tuple[mw.Parts, float]:
    """A whole tier 1 campaign as the experience curve sizes one; the parts and the mean experience factor."""
    rng = random.Random(10_000 + seed)
    c = Character(supply)
    factors: list[float] = []
    for _ in range(curve.CAMPAIGN_DUNGEONS):
        factors.extend(walk(c, curve.WHOLE_FLOORS, rng, rule))
    return c.parts(), statistics.mean(factors)


# --------------------------------------------------------------------------
# Question 1: gear quantity
# --------------------------------------------------------------------------

#: The factors on the gear column measured. 1 is today: Common 0.16, Elite 0.5, Legendary 1, Herald 2, Boss 5.
GEAR_FACTORS = (1.0, 0.5, 0.25, 0.125)


def gear_drops_per_kill() -> float:
    """Expected gear drops a kill at today's column, over what spawns (0.476)."""
    return gem_need.gear_drops_per_kill()


def masterful_share() -> float:
    """The share of tier 1 gear drops that are Masterful, the enemies' own magic find counted, weighted by drops."""
    total = masterful = 0.0
    for rarity, share in gem_need.kill_shares().items():
        dropped = share * loot.expected_gear_drops(rarity)
        masterful += dropped * loot.rarity_distribution(TIER, loot.magic_find_from(rarity))["Masterful"]
        total += dropped
    return masterful / total


def outpost_floors() -> float:
    low, high = mw.floor_range(CityTier.OUTPOST)
    return (low + high) / 2


def slot_masterful_chance(drops_in_slot: float) -> float:
    """The chance a worn single-position slot holds a Masterful after this many drops for it."""
    return 1.0 - (1.0 - masterful_share()) ** drops_in_slot


# --------------------------------------------------------------------------
# Question 2: gems by tier
# --------------------------------------------------------------------------

def gem_need_share(tier: int) -> float:
    """The gems a gear drop must carry for a full clear of the tier to fill its reference character
    (`analyse_gem_drops.needed_share`)."""
    return gem_need.needed_share(tier)


def halving_share(tier: int) -> float:
    """THE PROPOSAL: today's tier 8 figure, halved for each tier below tier 8."""
    return loot.GEM_DROPS_PER_GEAR_DROP / 2 ** (8 - tier)


def gems_in_a_campaign(share: float) -> float:
    return share * gear_drops_per_kill() * gem_need.kills_in_a_tier()


# --------------------------------------------------------------------------
# Question 4: the level the gap rule leaves, on the expected experience of whole campaigns
# --------------------------------------------------------------------------

LEVEL_TOTALS = [curve.whole_total_to_reach(level) for level in range(0, mw.player_power.MAX_LEVEL + 2)]


def level_of(experience: float) -> int:
    level = 1
    while level < mw.player_power.MAX_LEVEL and LEVEL_TOTALS[level + 1] <= experience:
        level += 1
    return level


def non_level(tier: int) -> float:
    """The reference character's gear, gems and resistances at the end of a tier; 0 before tier 1."""
    if tier == 0:
        return 0.0
    _, gear, gems, resistances = WEIGHTS.terms(mw.reference_parts(tier))
    return gear + gems + resistances


def score_at_curve_level(tier: int) -> float:
    """The character play produces at the end of a tier: the curve's level, the tier's reference gear."""
    return WEIGHTS.score(mw.reference_parts(tier, mw.LEVELS_AT_TIER_ENDS[tier - 1]))


def above_lowest(tier: int, score: float) -> float:
    """How far a score sits above the shallowest dungeon its tier spawns, in tier widths (the rule's unit)."""
    low, _ = gap_rewards.spawn_extremes(tier)
    return (score - low.score) / scoring.tier_width(tier)


#: The margin the ruled 0.6 had over the reference it was sized on (0.48 to 0.50).
BAND_MARGIN = 0.1


def proposed_band_down(tier: int) -> float:
    """THE PROPOSAL: the cut begins where the character on the curve, wearing its tier's reference gear, sits above
    the shallowest dungeon its tier spawns, plus the ruled band's own margin, rounded up to a tenth; never below 0.6."""
    needed = above_lowest(tier, score_at_curve_level(tier)) + BAND_MARGIN
    return max(gap_rewards.RULED.band_down, math.ceil(needed * 10 - 1e-9) / 10)


def proposed_rule(tier: int) -> gap_rewards.Rule:
    return dataclasses.replace(gap_rewards.RULED, name=f"proposed band at tier {tier}",
                               band_down=proposed_band_down(tier))


def ruled_rule(_tier: int) -> gap_rewards.Rule:
    return gap_rewards.RULED


def climb(rule_for=None, floors: int = curve.WHOLE_FLOORS) -> list[tuple[int, float, float]]:
    """Eight campaigns of expected experience, the reference's gear rising evenly across each tier (the 2026-11-22
    rule's own assumption) and the level fed back into the score. Per tier: level at its end, the lowest and the mean
    experience factor."""
    population = curve.POPULATION
    experience = 0.0
    out = []
    for tier in curve.TIERS:
        rule = rule_for(tier) if rule_for else None
        dungeon = scoring.dungeon_score(tier, mw.DUNGEON_TYPE, mw.DUNGEON_SUBTYPE, floors)
        width = scoring.tier_width(tier)
        per_floor = [population * curve.creature_experience(tier, f, floors, curve.WEIGHTS)
                     for f in range(1, floors + 1)]
        boss = scoring.final_boss_score(tier, mw.DUNGEON_TYPE, mw.DUNGEON_SUBTYPE, floors)
        start, end = non_level(tier - 1), non_level(tier)
        factors = []
        factor = 1.0
        for d in range(curve.CAMPAIGN_DUNGEONS):
            gear = start + (end - start) * (d + 0.5) / curve.CAMPAIGN_DUNGEONS
            for f in range(floors):
                score = WEIGHTS.level * level_of(experience) + gear
                factor = rule.multiplier((dungeon - score) / width) if rule else 1.0
                factors.append(factor)
                experience += per_floor[f] * factor
            experience += boss * factor
        out.append((level_of(experience), min(factors), statistics.mean(factors)))
    return out


def one_tier_over(tier: int, rule: gap_rewards.Rule) -> float:
    """The experience factor of the NEXT tier's finished character (curve level, reference gear) in this tier's
    average dungeon: what the rule is for."""
    dungeon = scoring.dungeon_score(tier, mw.DUNGEON_TYPE, mw.DUNGEON_SUBTYPE, curve.WHOLE_FLOORS)
    return rule.multiplier((dungeon - score_at_curve_level(tier + 1)) / scoring.tier_width(tier))


def refit_rate_levels(rate: float) -> tuple[list[int], float]:
    """THE OTHER CONSISTENT ANSWER, measured for comparison: the curve's rate changed, the opening (levels 2 to 7)
    kept, the anchor refit so level 100 still arrives at the end of tier 8. Tier-end levels and what level 8 then costs
    in floors of an average tier 1 dungeon."""
    available = curve.CAMPAIGN_DUNGEONS * sum(curve.PER_DUNGEON.values())
    opening = [curve.OPENING_SCALE * c for c in curve.POE_OPENING_COSTS]
    tail = sum((1 + rate) ** (level - 2) for level in range(curve.LAST_OPENING_LEVEL + 1, 101))
    scale = (available - sum(opening)) / tail

    def cost(level: int) -> float:
        if level <= curve.LAST_OPENING_LEVEL:
            return opening[level - 2]
        return scale * (1 + rate) ** (level - 2)

    floor_experience = (sum(curve.creature_experience(1, f, curve.WHOLE_FLOORS, curve.WEIGHTS)
                            for f in range(1, curve.WHOLE_FLOORS + 1)) / curve.WHOLE_FLOORS * curve.POPULATION)
    return curve.whole_levels_at_tier_ends(cost=cost), cost(curve.LAST_OPENING_LEVEL + 1) / floor_experience


# --------------------------------------------------------------------------
# The recommendation
# --------------------------------------------------------------------------

#: THE RECOMMENDED SUPPLY. Each lever a judgement; the proposal gives the reasons and the genre sources.
#:   gear: the gear column halved (Common 0.08, Elite 0.25, Legendary 0.5, Herald 1, Boss 2.5, Cataclysm Boss 6);
#:   gems: today's tier 8 figure halved for each tier below 8, so 1/128 of today's at tier 1;
#:   resistance: #1230's floors kept; the all-resistances family left out of tier 1 drops.
RECOMMENDED = Supply("recommended", gear=0.5, gems=halving_share(1) / loot.GEM_DROPS_PER_GEAR_DROP,
                     all_resistance=False)

GEM_OPTIONS = (
    ("today, one figure for all tiers", 1.0),
    ("halved for each tier below 8 (recommended)", halving_share(1) / loot.GEM_DROPS_PER_GEAR_DROP),
    ("each tier's own need, full clear", gem_need_share(1) / loot.GEM_DROPS_PER_GEAR_DROP),
)

#: Floors of the single, two and all resistances families.
FLOOR_OPTIONS = (
    ("#1230 floors, a quarter of the top (10, 7, 3)", None, True),
    ("an eighth of the top (5, 3.5, 1.5)", (5.0, 3.5, 1.5), True),
    ("no floors, the derived tenth (4, 2.8, 1.2)", (0.0, 0.0, 0.0), True),
    ("#1230 floors, all resistances not on a tier 1 drop (recommended)", None, False),
)


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def percentile(values: list[float], share: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def score_line(points: list[Point]) -> str:
    scores = [WEIGHTS.score(p.parts) for p in points]
    terms = [statistics.mean(WEIGHTS.terms(p.parts)[i] for p in points) for i in range(4)]
    dungeon = statistics.mean(statistics.mean(p.dungeon_scores) for p in points)
    level = statistics.mean(p.parts.level for p in points)
    return (f"{statistics.mean(scores):5.0f} ({percentile(scores, 0.1):4.0f}-{percentile(scores, 0.9):4.0f}) "
            f"| {terms[0]:4.0f} {terms[1]:4.0f} {terms[2]:4.0f} {terms[3]:4.0f} | lvl {level:4.1f} "
            f"| x{statistics.mean(scores) / dungeon:4.2f}")


def worn_line(points: list[Point]) -> str:
    counts = dict.fromkeys(af.RARITIES[:4], 0)
    for p in points:
        for rarity, _ in p.parts.pieces:
            counts[af.RARITIES[rarity - 1]] += 1
    n = len(points)
    singles = statistics.mean(p.masterful_singles for p in points)
    return (" ".join(f"{name[:4]} {counts[name] / n:4.1f}" for name in af.RARITIES[:4])
            + f" | single-position slots Masterful {100 * singles:3.0f}%")


def supply_line(points: list[Point]) -> str:
    gems = statistics.mean(len(p.parts.gems) for p in points)
    resist = statistics.mean(sum(min(r, 70.0) for r in p.parts.resistances) for p in points)
    dropped = statistics.mean(p.gear_dropped for p in points)
    gems_dropped = statistics.mean(p.gems_dropped for p in points)
    return (f"gear dropped {dropped:6.0f} | gems dropped {gems_dropped:5.1f}, socketed {gems:4.1f} "
            f"| resistance points {resist:5.1f}")


def family_line(points: list[Point]) -> str:
    names = [family.name for family in af.RESISTANCE_FAMILIES]
    means = {name: statistics.mean(p.by_family[name] for p in points) for name in names}
    total = sum(means.values()) or 1.0
    return "  ".join(f"{name} {means[name]:5.1f} ({100 * means[name] / total:2.0f}%)" for name in names)


def section_gear() -> None:
    print("1. GEAR QUANTITY: a factor on the whole gear column, every enemy rarity alike")
    per_kill = gear_drops_per_kill()
    print(f"  Gear drops a kill today, over what spawns: {per_kill:.3f}. Masterful share of tier 1 gear drops: "
          f"{100 * masterful_share():.2f}%.")
    print(f"  Creatures a floor {curve.POPULATION:.0f}; an Outpost dungeon averages {outpost_floors():.1f} floors; "
          f"the average dungeon {curve.WHOLE_FLOORS}.")
    print("  factor | a floor | an Outpost dungeon | a slot, an Outpost | a 50-floor dungeon | a slot, 50 floors "
          "| a single slot Masterful after 1, 2, 4 Outposts")
    for factor in GEAR_FACTORS:
        floor = per_kill * factor * curve.POPULATION
        outpost = floor * outpost_floors()
        slots = len(mw.POSITIONS)
        print(f"  {factor:6.3f} | {floor:7.1f} | {outpost:18.0f} | {outpost / slots:18.1f} | "
              f"{floor * curve.WHOLE_FLOORS:18.0f} | {floor * curve.WHOLE_FLOORS / slots:17.0f} | "
              + ", ".join(f"{100 * slot_masterful_chance(outpost / slots * n):3.0f}%" for n in CHECKPOINTS))
    print()
    print("  Walked, gems and resistance as today, the gap rule OFF (the drops alone). Score: mean (10th-90th) "
          "| level gear gems resistances | level | the score over the dungeon's")
    for factor in GEAR_FACTORS:
        walked = run_many(dataclasses.replace(TODAY, name=f"gear x{factor}", gear=factor))
        print(f"  gear x{factor}:")
        for n in CHECKPOINTS:
            print(f"    after {n}: {score_line(walked[n])}")
            print(f"             worn {worn_line(walked[n])}")
            print(f"             {supply_line(walked[n])}")
    print()


def section_gems() -> None:
    print("2. GEMS BY TIER: gems a gear drop at today's gear column, per tier")
    print("  Tier | need (full clear fills the reference) | today | halved below tier 8 | its margin over the need "
          "| gems in a campaign | in an average dungeon")
    for tier in curve.TIERS:
        need = gem_need_share(tier)
        half = halving_share(tier)
        gems = gems_in_a_campaign(half)
        print(f"  {tier:4d} | {need:37.6f} | {loot.GEM_DROPS_PER_GEAR_DROP:5.4f} | {half:19.7f} | {half / need:24.2f} "
              f"| {gems:18.1f} | {gems / curve.CAMPAIGN_DUNGEONS:21.2f}")
    print("  The need rises by "
          + ", ".join(f"{gem_need_share(t + 1) / gem_need_share(t):.2f}" for t in range(1, 8))
          + " from one tier to the next.")
    print()
    print("  Walked at tier 1, gear halved, resistance as today, the gap rule OFF:")
    for label, factor in GEM_OPTIONS:
        walked = run_many(dataclasses.replace(TODAY, name=label, gear=0.5, gems=factor))
        print(f"  gems {label} (x{factor:.4f} of today's column):")
        for n in CHECKPOINTS:
            print(f"    after {n}: {score_line(walked[n])} | {supply_line(walked[n])}")
    print()


def section_resistance() -> None:
    print("3. RESISTANCE: the families' floors, and the all-resistances family at tier 1")
    for tier in (1, 2):
        print(f"  An affix on a +0 piece dropped at difficulty tier {tier} (affix tiers 1 to "
              f"{af.max_affix_tier_on_a_drop(tier)}):")
        for family in af.RESISTANCE_FAMILIES:
            top = af.max_affix_tier_on_a_drop(tier)
            print(f"    {family.name:18s} {family.value_at(1, 0.0, 0):5.2f} to {family.value_at(top, 1.0, 0):5.2f} "
                  f"a type, x{family.breadth} = {family.value_at(1, 0.0, 0) * family.breadth:5.1f} to "
                  f"{family.value_at(top, 1.0, 0) * family.breadth:5.1f} resistance points")
    print("  Walked at tier 1, gear halved, gems as recommended, the gap rule OFF:")
    for label, floors, all_resistance in FLOOR_OPTIONS:
        supply = dataclasses.replace(RECOMMENDED, name=label, floors=floors, all_resistance=all_resistance)
        walked = run_many(supply)
        print(f"  {label}:")
        for n in CHECKPOINTS:
            print(f"    after {n}: {score_line(walked[n])} | {supply_line(walked[n])}")
            print(f"             by family, before the cap: {family_line(walked[n])}")
    print()


def section_level() -> None:
    print("4. LEVEL: the experience curve, the reference's levels and the gap rule")
    print(f"  Curve, tier ends: {mw.LEVELS_AT_TIER_ENDS}. Reference: "
          f"{[mw.reference_parts(t).level for t in curve.TIERS]}.")
    print("  Where each character sits above the shallowest dungeon its own tier spawns, in tier widths "
          "(the ruled cut begins at 0.6):")
    print("  Tier | reference, ruled weights | curve level, ruled weights | reference, today | curve level, today "
          "| proposed band | one tier over: ruled, proposed")
    for tier in curve.TIERS:
        ref_now = above_lowest(tier, mw.TODAY.score(mw.reference_parts(tier)))
        cur_now = above_lowest(tier, mw.TODAY.score(mw.reference_parts(tier, mw.LEVELS_AT_TIER_ENDS[tier - 1])))
        over = (f"{one_tier_over(tier, gap_rewards.RULED):.2f}, {one_tier_over(tier, proposed_rule(tier)):.2f}"
                if tier < 8 else "-")
        print(f"  {tier:4d} | {above_lowest(tier, WEIGHTS.score(mw.reference_parts(tier))):24.2f} | "
              f"{above_lowest(tier, score_at_curve_level(tier)):26.2f} | {ref_now:16.2f} | {cur_now:18.2f} | "
              f"{proposed_band_down(tier):13.1f} | {over}")
    print()
    print("  Eight campaigns of expected experience, the reference's gear, gems and resistances rising evenly over each")
    print("  tier and the level fed back: level at each tier's end (lowest / mean experience factor over the tier)")
    for label, rule_for in (("no gap rule", None), ("the ruled rule", ruled_rule), ("the proposed band", proposed_rule)):
        rows = climb(rule_for)
        print(f"  {label:18s}: " + "  ".join(f"{lvl}({low:.2f}/{mean:.2f})" for lvl, low, mean in rows))
    print()
    print("  The other consistent answer, for comparison: the curve's rate changed so its tier-end levels follow the")
    print("  reference, the opening kept, level 100 still at the end of tier 8:")
    for rate in (0.03, 0.04, 0.05, curve.DECIDED_RATE):
        levels, floors = refit_rate_levels(rate)
        print(f"    rate {100 * rate:4.1f}%: tier ends {levels}; level 8 costs {floors:5.1f} floors of an average "
              "tier 1 dungeon")
    print()


def section_together() -> None:
    print("TOGETHER: today's supply against the recommended, the ruled weights, the gap rule ON as the game has it")
    for supply in (TODAY, RECOMMENDED):
        walked = run_many(supply, rule=gap_rewards.RULED)
        print(f"  {supply.name}:")
        for n in CHECKPOINTS:
            print(f"    after {n}: {score_line(walked[n])} | {supply_line(walked[n])}")
    print("  Half the creatures killed, the gap rule on:")
    for supply in (TODAY, RECOMMENDED):
        walked = run_many(supply, rule=gap_rewards.RULED, kill_share=0.5)
        print(f"  {supply.name}:")
        for n in CHECKPOINTS:
            print(f"    after {n}: {score_line(walked[n])} | {supply_line(walked[n])}")
    print()
    dungeon = mw.dungeon_score(curve.WHOLE_FLOORS)
    print(f"A WHOLE TIER 1 CAMPAIGN: {curve.CAMPAIGN_DUNGEONS} dungeons of {curve.WHOLE_FLOORS} floors (Dungeon Score "
          f"{dungeon}), full clear, {CAMPAIGN_SEEDS} seeds; ceiling {scoring.PLAYER_MAX_SCORES[1]:.0f}, reference "
          f"{WEIGHTS.score(mw.reference_parts(1))}")
    print("  score | level gear gems resistances | level | gems socketed, resistance points | x the dungeon "
          "| mean experience factor")
    proposed = proposed_rule(TIER)
    for supply in (TODAY, RECOMMENDED):
        for label, rule in (("gap rule off", None), ("ruled rule", gap_rewards.RULED),
                            ("proposed band", proposed)):
            if supply is TODAY and rule is proposed:
                continue
            done = [campaign(supply, seed, rule) for seed in range(CAMPAIGN_SEEDS)]
            parts = [p for p, _ in done]
            scores = [WEIGHTS.score(p) for p in parts]
            terms = [statistics.mean(WEIGHTS.terms(p)[i] for p in parts) for i in range(4)]
            gems = statistics.mean(len(p.gems) for p in parts)
            resist = statistics.mean(sum(min(r, 70.0) for r in p.resistances) for p in parts)
            print(f"  {supply.name:11s} {label:13s}: {statistics.mean(scores):5.0f} | {terms[0]:4.0f} {terms[1]:4.0f} "
                  f"{terms[2]:4.0f} {terms[3]:4.0f} | lvl {statistics.mean(p.level for p in parts):4.1f} | "
                  f"{gems:4.1f}, {resist:5.1f} | x{statistics.mean(scores) / dungeon:4.2f} | "
                  f"{statistics.mean(f for _, f in done):.2f}")


def main() -> None:
    section_gear()
    section_gems()
    section_resistance()
    section_level()
    section_together()


if __name__ == "__main__":
    main()
