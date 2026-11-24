"""What a tier 1 character's Power Score is after 1, 2 and 4 dungeons, and what round weights would make of it.

WHY THIS EXISTS. Issue #2853, part 2. The owner, 2026-11-23: "I've run one dungeon and I'm already almost double the
dungeon score". The owner, 2026-11-24: "maybe put more points on your level instead of gear", and "why would I think a
level is worth 6.3 points? It would make sense as 1, 2, 5, 10, 25, 50, 100, but 6.3 makes no sense at all." The
coordinating session's direction from those: choose round per-unit weights first (points per level, per rarity step of a
gear piece, per upgrade level, per gem, per resistance percent) and let the shares fall out; the eight DungeonSimulator
ceilings (`scoring.PLAYER_MAX_SCORES`) stay, and a finished character lands near them rather than on them.

This is a MEASUREMENT FOR A PROPOSAL. It changes nothing in the model: `cataclysm_sim/player_power.py` and the game's
`CataclysmPowerScore` are untouched. It re-scores characters under candidate weights.

WHAT IT SIMULATES, every input from the simulation's own tables:

    THE DROPS: `loot.roll_drops_from_kill` and `loot.roll_gem_drops_from_kill` at difficulty tier 1, with the rarity cap
    of 2026-11-24 in place (`loot.highest_droppable_rarity(1)` is Masterful), the player's own magic find 0 and loot
    quantity 100, so every drop is Everyday to Masterful at +0.

    THE CREATURES: `analyse_experience_curve.POPULATION` a floor (258, the middle of the measured 0.24-a-cell
    populations), each of a rarity drawn by `game/Data/EnemyRarities.csv`'s spawn weights, every one killed (the full
    clear the experience curve assumes, issue #925), and one Boss on the last floor.

    THE DUNGEONS: Basic, no sub-type, no modifiers, at difficulty tier 1. Their depth is drawn from a city tier's floor
    range in `config.DUNGEON_SPECS`: an Outpost's 5 to 10 by default, since a surge prefers Outposts
    (`SURGE_TARGET_WEIGHT` 5 against a Bulwark's 3) and they are the shortest walk.

    EXPERIENCE: each creature's Enemy Score on its floor (the owner's 2026-08-24 rule: an enemy's Power Score IS the
    experience it grants), the final boss's score, and the game's whole-number level costs
    (`analyse_experience_curve.whole_level_cost`, the 2026-11-19 opening included).

    THE GAP RULE OF 2026-11-22 (`analyse_power_gap_rewards.RULED`), ON BY DEFAULT because the game has it: once a
    floor's player score is far above the dungeon's, that floor's experience and loot quantity fall. It is evaluated
    once a floor with the score of the weights under test, so a weight set that scores the character lower gives it more
    loot and experience. `gap_rule=False` measures what the drops alone produce.

THE EQUIP RULE, A JUDGEMENT: every drop is picked up, and each worn position keeps the best piece seen, compared by
rarity, then upgrade level, then total resistance, then socket count. Eight Ring positions keep the best eight rings. The
hands are one position. Gems fill the worn pieces' sockets, best first. No forging, no gem combining, no magic find on
gear, no passive tree. It is the most a player who wears the rarest thing they find gets out of the drops, which is what
the owner describes doing ("mostly Masterful", the "Hide worse than worn" filter).

Run from `sim/`: python -u measure_power_score_weights.py     (about eight minutes; the whole-campaign section is
most of it). Every walk is seeded, so two runs print the same figures.
"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import math
import random
import statistics

from cataclysm_sim import affixes as af
from cataclysm_sim import loot, player_power, scoring
from cataclysm_sim.config import CityTier, DungeonType, TuningConfig

# THE TWO ANALYSIS SCRIPTS PRINT THEIR REPORTS ON IMPORT, as every analysis script does; only their figures are used.
with contextlib.redirect_stdout(io.StringIO()):
    import analyse_experience_curve as curve
    import analyse_power_gap_rewards as gap_rewards

TIER = 1
DUNGEON_TYPE = "Basic"
DUNGEON_SUBTYPE = "None"
#: Seeds a scenario is averaged over. The spread is printed beside the mean.
SEEDS = 40
#: How many dungeons are walked in sequence; scores are read after 1, 2 and 4.
CHECKPOINTS = (1, 2, 4)


# --------------------------------------------------------------------------
# Weights
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class WeightSet:
    """Points per unit for each thing the Power Score reads.

    A GEAR PIECE IS WORTH `rarity x (per_rarity + per_upgrade x upgrade)`, the shape `player_power.gear_term` has today:
    upgrade level multiplies rarity. Written as two per-unit numbers rather than a weight and a factor, so each is a
    number a player can read: `per_rarity` is what one rarity step is worth on a +0 piece, and `per_upgrade` is what one
    upgrade level adds to each of the piece's rarity steps. `flat_upgrade` adds points per upgrade level whatever the
    rarity, which only the additive candidate uses.
    """

    name: str
    level: float
    per_rarity: float
    per_upgrade: float
    gem: float
    resistance: float
    flat_upgrade: float = 0.0

    def piece(self, rarity: int, upgrade: int) -> float:
        return rarity * (self.per_rarity + self.per_upgrade * upgrade) + self.flat_upgrade * upgrade

    def terms(self, c: Parts) -> tuple[float, float, float, float]:
        """Level, gear, gems and resistances, unrounded."""
        return (self.level * c.level,
                sum(self.piece(r, u) for r, u in c.pieces),
                self.gem * sum(c.gems),
                self.resistance * sum(min(r, player_power.RESISTANCE_CAP) for r in c.resistances))

    def score(self, c: Parts) -> int:
        # floor(x + 0.5), the rounding player_power.power_score and the enemy model use.
        return math.floor(sum(self.terms(c)) + 0.5)


#: Today's weights, read from `player_power.WEIGHTS` rather than retyped: 6.233 a rarity step and 6.233 x 0.2525 an
#: upgrade level on each step.
TODAY = WeightSet("today (10/50/30/10 shares)",
                  level=player_power.WEIGHTS["level"],
                  per_rarity=player_power.WEIGHTS["gear"],
                  per_upgrade=player_power.WEIGHTS["gear"] * player_power.WEIGHTS["upgrade_factor"],
                  gem=player_power.WEIGHTS["gem"],
                  resistance=player_power.WEIGHTS["resistance"])

#: THE RECOMMENDATION. Every number a plain one; the shares fall out of them (see the proposal for the reasoning).
RECOMMENDED = WeightSet("recommended", level=10, per_rarity=2, per_upgrade=2, gem=5, resistance=1)

#: THE ALTERNATIVE the proposal offers if the weights alone are to carry more of the early fix: lower after one
#: dungeon and closer to the ceilings, at the cost of two numbers off the owner's list and gems worth less.
ALTERNATIVE = WeightSet("alternative (15/1/2/3/1)", level=15, per_rarity=1, per_upgrade=2, gem=3, resistance=1)

#: The candidates the proposal compares the recommendation with. Each is a judgement; the proposal says why each lost.
CANDIDATES = (
    TODAY,
    RECOMMENDED,
    WeightSet("today rounded (6/6/1.5/5/1)", level=6, per_rarity=6, per_upgrade=1.5, gem=5, resistance=1),
    WeightSet("closest fit (10/3/2/4/1)", level=10, per_rarity=3, per_upgrade=2, gem=4, resistance=1),
    ALTERNATIVE,
    WeightSet("level 20, gear 1+2, gem 2, resist 0.5", level=20, per_rarity=1, per_upgrade=2, gem=2,
              resistance=0.5),
    WeightSet("level 10, gear 4+1", level=10, per_rarity=4, per_upgrade=1, gem=5, resistance=1),
    WeightSet("level 5, gear 2+2", level=5, per_rarity=2, per_upgrade=2, gem=5, resistance=1),
    WeightSet("level 25, gear 1+1", level=25, per_rarity=1, per_upgrade=1, gem=5, resistance=1),
    WeightSet("additive: 5 a rarity step, 5 an upgrade", level=10, per_rarity=5, per_upgrade=0, gem=5,
              resistance=1, flat_upgrade=5),
)


# --------------------------------------------------------------------------
# What a character is, as the score reads it
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Parts:
    """Everything a Power Score reads: level, (rarity, upgrade) per scored piece, gem rarities, eight resistances."""

    level: int
    pieces: tuple[tuple[int, int], ...]
    gems: tuple[int, ...]
    resistances: tuple[float, ...]


def parts_of(c: player_power.Character) -> Parts:
    return Parts(c.level, tuple((p.rarity, p.upgrade) for p in c.gear), tuple(c.gems), tuple(c.resistances))


def reference_parts(tier: int, level: int | None = None) -> Parts:
    """`player_power.reference_character(tier)`, optionally at another level (the experience curve's)."""
    c = parts_of(player_power.reference_character(tier))
    return c if level is None else dataclasses.replace(c, level=level)


#: The level at the end of each tier's campaign on the decided experience curve: 42 59 70 79 85 91 95 100.
LEVELS_AT_TIER_ENDS = curve.whole_levels_at_tier_ends()


# --------------------------------------------------------------------------
# The walk
# --------------------------------------------------------------------------

#: How many pieces of each slot are worn: seven armour pieces, eight rings, a necklace, a relic and the hands.
POSITIONS = dict(af.GEAR_SLOTS)
SPAWN = [(rarity, weight) for rarity, weight in curve.WEIGHTS.items() if weight > 0.0]
SPAWN_TOTAL = sum(weight for _, weight in SPAWN)


def resistance_of(item: loot.RolledItem) -> list[float]:
    """The eight resistances one piece grants, from its resistance-family affixes at the piece's upgrade level."""
    out = [0.0] * len(af.DAMAGE_TYPES)
    for rolled in item.affixes:
        if isinstance(rolled.affix, af.AffixFamily):
            value = rolled.affix.value_at(rolled.tier, rolled.roll, item.gear_level)
            for damage_type in rolled.covers:
                out[af.DAMAGE_TYPES.index(damage_type)] += value
    return out


def equip_key(item: loot.RolledItem) -> tuple:
    return (loot.rarity_index(item.rarity), item.gear_level, sum(resistance_of(item)), item.sockets)


class Character:
    """A tier 1 character from level 1 with nothing worn."""

    def __init__(self) -> None:
        self.experience = 0
        self.level = 1
        self.worn: dict[str, list[tuple[tuple, loot.RolledItem]]] = {slot: [] for slot in POSITIONS}
        self.gems: list[int] = []

    def wear_if_better(self, item: loot.RolledItem) -> None:
        slot = item.base.slot
        worn = self.worn[slot]
        key = equip_key(item)
        if len(worn) < POSITIONS[slot]:
            worn.append((key, item))
            return
        worst = min(range(len(worn)), key=lambda i: worn[i][0])
        if key > worn[worst][0]:
            worn[worst] = (key, item)

    def gain(self, experience: int) -> None:
        self.experience += experience
        while (self.level < player_power.MAX_LEVEL
               and curve.whole_total_to_reach(self.level + 1) <= self.experience):
            self.level += 1

    def parts(self) -> Parts:
        items = [item for worn in self.worn.values() for _, item in worn]
        sockets = sum(item.sockets for item in items)
        resistances = [0.0] * len(af.DAMAGE_TYPES)
        for item in items:
            for i, value in enumerate(resistance_of(item)):
                resistances[i] += value
        return Parts(level=self.level,
                     pieces=tuple((loot.rarity_index(item.rarity), item.gear_level) for item in items),
                     gems=tuple(sorted(self.gems, reverse=True)[:sockets]),
                     resistances=tuple(resistances))


def spawn_rarity(rng: random.Random) -> str:
    pick = rng.random() * SPAWN_TOTAL
    for rarity, weight in SPAWN:
        pick -= weight
        if pick < 0.0:
            return rarity
    return SPAWN[-1][0]


def dungeon_score(floors: int) -> int:
    return scoring.dungeon_score(TIER, DUNGEON_TYPE, DUNGEON_SUBTYPE, floors)


def kill(c: Character, rarity: str, experience: float, loot_factor: float, rng: random.Random) -> None:
    c.gain(round(experience))
    quantity = loot.BASELINE_LOOT_QUANTITY * loot_factor
    for item in loot.roll_drops_from_kill(rarity, TIER, 0.0, quantity, rng):
        c.wear_if_better(item)
    for gem in loot.roll_gem_drops_from_kill(rarity, TIER, 0.0, quantity, rng):
        c.gems.append(loot.rarity_index(gem))


def walk(c: Character, floors: int, weights: WeightSet, rng: random.Random, gap_rule: bool,
         kill_share: float = 1.0) -> None:
    """Clear one dungeon of `floors` floors, floor by floor, and its boss."""
    score = dungeon_score(floors)
    killed = round(curve.POPULATION * kill_share)
    for floor in range(1, floors + 1):
        exp_factor = loot_factor = 1.0
        if gap_rule:
            gap = (score - weights.score(c.parts())) / scoring.tier_width(TIER)
            exp_factor = gap_rewards.RULED.multiplier(gap)
            loot_factor = gap_rewards.RULED.multiplier(gap, loot=True)
        enemy = scoring.enemy_scores(TIER, DUNGEON_TYPE, DUNGEON_SUBTYPE, floors, floor)
        for _ in range(killed):
            rarity = spawn_rarity(rng)
            kill(c, rarity, enemy[rarity] * exp_factor, loot_factor, rng)
    boss = scoring.final_boss_score(TIER, DUNGEON_TYPE, DUNGEON_SUBTYPE, floors)
    kill(c, "Boss", boss * exp_factor, loot_factor, rng)


@dataclasses.dataclass(frozen=True)
class Scenario:
    name: str
    city_tier: CityTier = CityTier.OUTPOST
    kill_share: float = 1.0
    gap_rule: bool = True


def floor_range(city_tier: CityTier) -> tuple[int, int]:
    return TuningConfig().spec(DungeonType.BASIC, city_tier).floors


@dataclasses.dataclass
class Checkpoint:
    """One seed's character after N dungeons: its parts and the dungeons it walked."""

    dungeons: int
    parts: Parts
    dungeon_scores: tuple[int, ...]


def run(scenario: Scenario, weights: WeightSet, seed: int) -> list[Checkpoint]:
    rng = random.Random(seed)
    low, high = floor_range(scenario.city_tier)
    c = Character()
    out, scores = [], []
    for n in range(1, max(CHECKPOINTS) + 1):
        floors = rng.randint(low, high)
        scores.append(dungeon_score(floors))
        walk(c, floors, weights, rng, scenario.gap_rule, scenario.kill_share)
        if n in CHECKPOINTS:
            out.append(Checkpoint(n, c.parts(), tuple(scores)))
    return out


def run_many(scenario: Scenario, weights: WeightSet, seeds: int = SEEDS) -> dict[int, list[Checkpoint]]:
    by_n: dict[int, list[Checkpoint]] = {n: [] for n in CHECKPOINTS}
    for seed in range(seeds):
        for point in run(scenario, weights, seed):
            by_n[point.dungeons].append(point)
    return by_n


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def percentile(values: list[float], share: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def describe(points: list[Checkpoint], weights: WeightSet) -> str:
    scores = [weights.score(p.parts) for p in points]
    terms = [weights.terms(p.parts) for p in points]
    mean_terms = [statistics.mean(t[i] for t in terms) for i in range(4)]
    dungeon = statistics.mean(statistics.mean(p.dungeon_scores) for p in points)
    level = statistics.mean(p.parts.level for p in points)
    return (f"{statistics.mean(scores):6.0f} ({percentile(scores, 0.1):4.0f}-{percentile(scores, 0.9):4.0f}) | "
            f"{mean_terms[0]:5.0f} {mean_terms[1]:5.0f} {mean_terms[2]:4.0f} {mean_terms[3]:4.0f} | "
            f"lvl {level:4.1f} | dungeon {dungeon:4.0f} | x{statistics.mean(scores) / dungeon:4.2f}")


def gear_mix(points: list[Checkpoint]) -> str:
    counts = {rarity: 0 for rarity in af.RARITIES}
    for p in points:
        for rarity, _ in p.parts.pieces:
            counts[af.RARITIES[rarity - 1]] += 1
    total = sum(counts.values())
    gems = statistics.mean(len(p.parts.gems) for p in points)
    gem_rarity = statistics.mean(statistics.mean(p.parts.gems) if p.parts.gems else 0 for p in points)
    resist = statistics.mean(sum(min(r, 70.0) for r in p.parts.resistances) for p in points)
    return ("  ".join(f"{name[:4]} {counts[name] / len(points):4.1f}" for name in af.RARITIES[:4] if total)
            + f" | gems {gems:4.1f} of rarity {gem_rarity:3.1f} | resistance points {resist:5.1f}")


def shares(weights: WeightSet, c: Parts) -> str:
    terms = weights.terms(c)
    total = sum(terms)
    return "/".join(f"{100 * t / total:.0f}" for t in terms)


def reference_table(weights: WeightSet) -> None:
    print(f"  {weights.name}: level {weights.level:g}, a rarity step {weights.per_rarity:g} + {weights.per_upgrade:g} "
          f"an upgrade level, flat upgrade {weights.flat_upgrade:g}, gem {weights.gem:g}, resistance {weights.resistance:g}")
    print("    Tier | Ceiling | Reference (design table)  | Reference at the curve's level  | shares at the reference")
    worst = 0.0
    for tier in range(1, 9):
        ceiling = scoring.PLAYER_MAX_SCORES[tier]
        ref = weights.score(reference_parts(tier))
        lev = weights.score(reference_parts(tier, LEVELS_AT_TIER_ENDS[tier - 1]))
        worst = max(worst, abs(ref - ceiling) / ceiling)
        print(f"    T{tier}   | {ceiling:7.0f} | {ref:6d} {100 * (ref - ceiling) / ceiling:+6.1f}% "
              f"           | level {LEVELS_AT_TIER_ENDS[tier - 1]:3d}: {lev:6d} {100 * (lev - ceiling) / ceiling:+6.1f}% "
              f"      | {shares(weights, reference_parts(tier))}")
    print(f"    worst distance of the design's reference from its ceiling: {100 * worst:.1f}%")


#: A whole tier 1 campaign as the experience curve sizes one: 26 dungeons of its average 50 floors, the walk that puts
#: the curve's character at level 42 by the end of tier 1. Few seeds, because each is 1,300 floors.
CAMPAIGN_DUNGEONS = curve.CAMPAIGN_DUNGEONS
CAMPAIGN_FLOORS = curve.WHOLE_FLOORS
CAMPAIGN_SEEDS = 4


def campaign(weights: WeightSet, seed: int, gap_rule: bool) -> Parts:
    rng = random.Random(10_000 + seed)
    c = Character()
    for _ in range(CAMPAIGN_DUNGEONS):
        walk(c, CAMPAIGN_FLOORS, weights, rng, gap_rule)
    return c.parts()


def describe_parts(parts: list[Parts], weights: WeightSet, against: float) -> str:
    scores = [weights.score(p) for p in parts]
    terms = [statistics.mean(weights.terms(p)[i] for p in parts) for i in range(4)]
    level = statistics.mean(p.level for p in parts)
    return (f"{statistics.mean(scores):6.0f} | {terms[0]:5.0f} {terms[1]:5.0f} {terms[2]:4.0f} {terms[3]:4.0f} | "
            f"lvl {level:4.1f} | x{statistics.mean(scores) / against:4.2f} the dungeon")


def main() -> None:
    print("THE REFERENCE CHARACTER AT THE END OF EACH TIER, AGAINST THE EIGHT CEILINGS")
    print("  'Reference' is player_power.reference_character: level 12.5 a tier, gear and gems of the tier's own")
    print("  rarity at +tier+2, 45/8 sockets and 70/8 % each resistance a tier. 'At the curve's level' is the same")
    print(f"  character at the level the decided experience curve reaches: {LEVELS_AT_TIER_ENDS}.")
    print("  Shares are level/gear/gems/resistances, in per cent.\n")
    for weights in CANDIDATES:
        reference_table(weights)
        print()

    print(f"A TIER 1 CHARACTER AFTER 1, 2 AND 4 DUNGEONS, {SEEDS} seeds each, the rarity cap in place")
    print("  score: mean (10th-90th percentile) | level gear gems resistances | mean level | the mean Dungeon Score")
    print("  of the dungeons walked | the score as a multiple of it.\n")

    print("  EVERY CANDIDATE ON ONE WALK: Outpost dungeons (5-10 floors), full clear, the gap rule OFF, so the")
    print("  character is the same for every weight set and only the scoring differs.")
    shared = run_many(Scenario("shared", gap_rule=False), TODAY)
    for n in CHECKPOINTS:
        print(f"    worn after {n}: {gear_mix(shared[n])}")
    for weights in CANDIDATES:
        for n in CHECKPOINTS:
            print(f"    {weights.name[:40]:40s} after {n}: {describe(shared[n], weights)}")
    print()

    scenarios = (Scenario("Outpost dungeons (5-10 floors), full clear, gap rule on"),
                 Scenario("Outpost dungeons, HALF the creatures killed, gap rule on", kill_share=0.5),
                 Scenario("Bulwark dungeons (15-25 floors), full clear, gap rule on", city_tier=CityTier.BULWARK))
    print("  THE GAP RULE ON, as the game has it. Each weight set is its own walk, because the rule reads the score.")
    for scenario in scenarios:
        print(f"  {scenario.name}")
        for weights in (TODAY, RECOMMENDED, ALTERNATIVE):
            walked = run_many(scenario, weights)
            for n in CHECKPOINTS:
                print(f"    {weights.name[:26]:26s} after {n}: {describe(walked[n], weights)}")
        print()

    dungeon = dungeon_score(CAMPAIGN_FLOORS)
    print(f"A WHOLE TIER 1 CAMPAIGN: {CAMPAIGN_DUNGEONS} dungeons of {CAMPAIGN_FLOORS} floors (Dungeon Score {dungeon}),")
    print(f"  full clear, {CAMPAIGN_SEEDS} seeds. The experience curve puts this character at level "
          f"{LEVELS_AT_TIER_ENDS[0]}; the tier 1 ceiling is {scoring.PLAYER_MAX_SCORES[1]:.0f}.")
    print("  score | level gear gems resistances | mean level | the score as a multiple of the dungeon")
    off = [campaign(TODAY, seed, gap_rule=False) for seed in range(CAMPAIGN_SEEDS)]
    for weights in (TODAY, RECOMMENDED, ALTERNATIVE):
        print(f"    gap rule OFF, {weights.name[:26]:26s}: {describe_parts(off, weights, dungeon)}")
    for weights in (TODAY, RECOMMENDED, ALTERNATIVE):
        on = [campaign(weights, seed, gap_rule=True) for seed in range(CAMPAIGN_SEEDS)]
        print(f"    gap rule on,  {weights.name[:26]:26s}: {describe_parts(on, weights, dungeon)}")
    print()

    print("THE RECOMMENDATION, PER UNIT AND AS SHARES OF THE TIER 8 REFERENCE CHARACTER")
    w = RECOMMENDED
    print(f"  {w.level:g} a level; a piece is rarity x ({w.per_rarity:g} + {w.per_upgrade:g} x upgrade level); "
          f"{w.gem:g} a gem rarity step; {w.resistance:g} a resistance percent")
    for label, rarity, upgrade in (("Everyday +0", 1, 0), ("Masterful +0", 4, 0), ("Everyday +3", 1, 3),
                                   ("Legendary +4", 5, 4), ("Mythical +6", 6, 6), ("Cataclysmic +10", 8, 10)):
        print(f"    {label:16s} {w.piece(rarity, upgrade):5.0f}   (today {TODAY.piece(rarity, upgrade):6.1f})")
    print(f"  shares at tier 8 (level/gear/gems/resistances): {shares(w, reference_parts(8))}; "
          f"today {shares(TODAY, reference_parts(8))}")


if __name__ == "__main__":
    main()
