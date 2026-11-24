"""Drop supply at difficulty tier 1, as shipped for issue #2853, against what it was; and the gem curve it chose.

WHY THIS EXISTS. Issue #2853. The owner, 2026-11-23: "I've run one dungeon and I'm already almost double the dungeon
score". After the plain Power Score weights were adopted, the drop supply proposal (`proposal/drop-supply-2853`,
`ea7efabeb`, whose script of this name this one is rebuilt from) measured four supply levers, and the owner ruled on all
four on 2026-11-24, the chosen options quoted:

    1. "Halve gear drops (Recommended)": the whole gear drop column halves.
    2. "Milder: tier 1 gets 1/8 (Recommended)": the gem rate scales by tier, less steeply than halving per tier, so
       tier 1 gets about 1/8 of the rate and tier 8 keeps it; the exact curve to be measured before it ships.
    3. "No all-resist at tier 1 (Recommended)": the all-resistances family cannot roll on a tier 1 drop.
    4. "Per-tier start (Recommended)": the gap rule's cut starts at 1.4, 1.3, 1.1, 1.0, 0.9, 0.9, 0.8, 0.8 tier widths
       at tiers 1 to 8 (was 0.6 everywhere).

All four are now the model (`loot.ENEMY_GEAR_DROPS`, `loot.gem_drop_share`, `affixes.rolls_on_a_drop_at`,
`analyse_power_gap_rewards.RULED`). This script measures, from the model as shipped:

    GEMS: the gem curve, chosen here (a judgement, `docs/DECISIONS.md`): each candidate's share at every tier against
          what a full clear of the tier needs to fill its reference character, and the gems a full clear of 1, 2 and 4
          Outpost dungeons drops at tiers 1 and 4.
    GEAR: how many gear pieces a floor, an Outpost dungeon and a slot get, before and after.
    THE BAND: where the experience curve's character sits against each tier's start of the cut.
    TOGETHER: a fresh tier 1 character after 1, 2 and 4 Outpost dungeons, and after a whole tier 1 campaign, with the
          supply as shipped against the supply before #2853, each with its own gap rule; the score against the
          dungeon's.

THE WALK, A JUDGEMENT CARRIED FROM THE PROPOSAL: Basic dungeons with no sub-type at difficulty tier 1, their depth drawn
from an Outpost's 5 to 10 floors (`config.DUNGEON_SPECS`), `analyse_experience_curve.POPULATION` (258) creatures a
floor of rarities drawn by `game/Data/EnemyRarities.csv`'s spawn weights, every one killed, a Boss on the last floor; the
player's own magic find 0 and loot quantity 100. Every drop is picked up and each worn position keeps the best piece
seen, by rarity, then upgrade level, then total resistance, then sockets; eight Ring positions keep the best eight; gems
fill the worn pieces' sockets, best first. No forging. Experience is each creature's Enemy Score, through the game's
whole-number level costs. Scored by `player_power.power_score`.

THE SUPPLY BEFORE #2853 is the shipped model with its levers put back: the gear column doubled, every tier at tier 8's
gem rate, the all-resistances family allowed on a tier 1 drop, and rule D's 0.6 for the gap rule. `roll_item` mirrors
`loot.roll_item` so that lever can reach inside it; `sim/tests/test_measure_drop_supply.py` checks it rolls the very
item `loot.roll_item` rolls when the lever is not pulled, and that the walk's kill draws as `loot`'s own does.

Run from `sim/`: python -u measure_drop_supply.py     (several minutes; the whole-campaign rows are most of it).
Every walk is seeded, so two runs print the same figures.
"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import random
import statistics
from collections.abc import Callable

from cataclysm_sim import affixes as af
from cataclysm_sim import loot, player_power, scoring
from cataclysm_sim.config import CityTier, DungeonType, TuningConfig

# THE ANALYSIS SCRIPTS PRINT THEIR REPORTS ON IMPORT; only their figures are used here.
with contextlib.redirect_stdout(io.StringIO()):
    import analyse_experience_curve as curve
    import analyse_gem_drops as gem_need
    import analyse_power_gap_rewards as gap_rewards

DUNGEON_TYPE = "Basic"
DUNGEON_SUBTYPE = "None"
#: Seeds a short walk is averaged over; the 10th to 90th percentile is printed beside the mean.
SEEDS = 40
#: Dungeons walked in sequence; read after 1, 2 and 4.
CHECKPOINTS = (1, 2, 4)
#: Seeds a whole campaign is averaged over, few because each is 1,300 floors.
CAMPAIGN_SEEDS = 4

POSITIONS = dict(af.GEAR_SLOTS)
SPAWN = [(rarity, weight) for rarity, weight in curve.WEIGHTS.items() if weight > 0.0]
SPAWN_TOTAL = sum(weight for _, weight in SPAWN)

#: The gap rule as it stood from 2026-11-22 until the per-tier start: the cut began 0.6 tier widths up at every tier.
RULE_D = next(rule for rule in gap_rewards.RULES if rule.name.startswith("D, loot floor 25%"))


# --------------------------------------------------------------------------
# The supply
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Supply:
    """What drops, as levers on the shipped model.

    `gear` multiplies the shipped gear column (so loot quantity, which scales the count, is untouched). `gem_share`
    replaces `loot.gem_drop_share` (None keeps it). `all_resistance_at_tier_one` lifts the shipped gate on the
    all-resistances family. `rule` is the gap rule the walk pays experience and loot by.
    """

    name: str
    gear: float = 1.0
    gem_share: Callable[[int], float] | None = None
    all_resistance_at_tier_one: bool = False
    rule: gap_rewards.Rule = gap_rewards.RULED

    def gems_factor(self, tier: int) -> float:
        """What the loot quantity a kill's gem roll sees is multiplied by, so it draws at `gem_share` instead of the
        shipped share: the roll is a Poisson draw on the column times the shipped share times loot quantity."""
        return 1.0 if self.gem_share is None else self.gem_share(tier) / loot.gem_drop_share(tier)


SHIPPED = Supply("as shipped for #2853")
BEFORE = Supply("before #2853", gear=2.0, gem_share=lambda tier: 1.0, all_resistance_at_tier_one=True, rule=RULE_D)

#: The gem curves measured. The first is the one shipped; each is the share of tier 8's rate a tier drops.
GEM_CURVES: tuple[tuple[str, Callable[[int], float]], ...] = (
    ("three quarters a tier (shipped)", loot.gem_drop_share),
    ("eighths, tier / 8", lambda tier: tier / 8),
    ("halved a tier (the proposal)", lambda tier: 0.5 ** (8 - tier)),
    ("tier 8's at every tier (before)", lambda tier: 1.0),
)


# --------------------------------------------------------------------------
# One item, one kill
# --------------------------------------------------------------------------

def roll_item(slot: str, tier: int, magic_find: float, rng, all_resistance_at_tier_one: bool = False) -> loot.RolledItem:
    """`loot.roll_item`, step for step. `all_resistance_at_tier_one` lifts the gate on the all-resistances family, which
    is what a tier 1 drop was before #2853; with it False this rolls the very item `loot.roll_item` rolls."""
    rarity = loot.roll_rarity(tier, magic_find, rng)
    base = rng.choice(af.bases_for(slot))
    prefixes, suffixes = loot.split_for_a_drop(af.affix_slots_for(rarity), rng)
    drawn: list = []
    for position, count in (("prefix", prefixes), ("suffix", suffixes)):
        if count == 0:
            continue
        candidates = [c for c in loot._candidates_for(slot, position, rng)
                      if all_resistance_at_tier_one or af.rolls_on_a_drop_at(c.affix, tier)]
        drawn.extend(af.draw_without_repeating_a_group(candidates, count, rng, group_of=loot._groups_of_candidate))
    rolled = tuple(loot.RolledAffix(affix=c.affix, tier=af.roll_affix_tier(tier, rng), roll=rng.random(),
                                    covers=c.covers)
                   for c in drawn)
    return loot.RolledItem(base=base, gear_level=loot.gear_level_gate(rarity), affixes=rolled,
                           enchantments=af.enchantments_for(rarity), residue=loot.roll_residue(rarity, rng),
                           sockets=loot.roll_sockets(base, rng))


def resistance_of(item: loot.RolledItem) -> list[float]:
    """The eight resistances one piece grants, from its resistance-family affixes at the piece's upgrade level."""
    out = [0.0] * len(af.DAMAGE_TYPES)
    for rolled in item.affixes:
        if isinstance(rolled.affix, af.AffixFamily):
            value = rolled.affix.value_at(rolled.tier, rolled.roll, item.gear_level)
            for damage_type in rolled.covers:
                out[af.DAMAGE_TYPES.index(damage_type)] += value
    return out


class Walker:
    """A fresh character, level 1 with nothing worn, and what fell for it."""

    def __init__(self, supply: Supply) -> None:
        self.supply = supply
        self.experience = 0
        self.level = 1
        self.worn: dict[str, list[tuple[tuple, loot.RolledItem]]] = {slot: [] for slot in POSITIONS}
        self.gems: list[int] = []
        self.gear_dropped = 0
        self.gems_dropped = 0

    def gain(self, experience: int) -> None:
        self.experience += experience
        while self.level < player_power.MAX_LEVEL and curve.whole_total_to_reach(self.level + 1) <= self.experience:
            self.level += 1

    def wear_if_better(self, item: loot.RolledItem) -> None:
        worn = self.worn[item.base.slot]
        key = (loot.rarity_index(item.rarity), item.gear_level, sum(resistance_of(item)), item.sockets)
        if len(worn) < POSITIONS[item.base.slot]:
            worn.append((key, item))
            return
        worst = min(range(len(worn)), key=lambda i: worn[i][0])
        if key > worn[worst][0]:
            worn[worst] = (key, item)

    def items(self) -> list[loot.RolledItem]:
        return [item for worn in self.worn.values() for _, item in worn]

    def character(self) -> player_power.Character:
        items = self.items()
        sockets = sum(item.sockets for item in items)
        resistances = [0.0] * len(af.DAMAGE_TYPES)
        for item in items:
            for i, value in enumerate(resistance_of(item)):
                resistances[i] += value
        return player_power.Character(
            level=self.level,
            gear=tuple(player_power.GearPiece(rarity=loot.rarity_index(item.rarity), upgrade=item.gear_level)
                       for item in items),
            gems=tuple(sorted(self.gems, reverse=True)[:sockets]),
            resistances=tuple(resistances))

    def by_family(self) -> dict[str, float]:
        """Resistance points each family puts on the character, before the 70 cap."""
        out = {family.name: 0.0 for family in af.RESISTANCE_FAMILIES}
        for item in self.items():
            for rolled in item.affixes:
                if isinstance(rolled.affix, af.AffixFamily):
                    out[rolled.affix.name] += (rolled.affix.value_at(rolled.tier, rolled.roll, item.gear_level)
                                               * len(rolled.covers))
        return out


def spawn_rarity(rng: random.Random) -> str:
    pick = rng.random() * SPAWN_TOTAL
    for rarity, weight in SPAWN:
        pick -= weight
        if pick < 0.0:
            return rarity
    return SPAWN[-1][0]


def kill(c: Walker, rarity: str, experience: float, loot_factor: float, rng: random.Random, tier: int) -> None:
    """A kill as `loot.roll_drops_from_kill` and `loot.roll_gem_drops_from_kill` draw it, the supply's levers pulled."""
    c.gain(round(experience))
    quantity = loot.BASELINE_LOOT_QUANTITY * loot_factor
    count = loot.roll_gear_drop_count(rarity, quantity * c.supply.gear, rng)
    magic_find = loot.magic_find_from(rarity)
    for _ in range(count):
        c.wear_if_better(roll_item(loot.roll_slot(rng), tier, magic_find, rng, c.supply.all_resistance_at_tier_one))
    c.gear_dropped += count
    gems = loot.roll_gem_drops_from_kill(rarity, tier, 0.0, quantity * c.supply.gems_factor(tier), rng)
    c.gems_dropped += len(gems)
    c.gems.extend(loot.rarity_index(gem) for gem in gems)


def dungeon_score(floors: int, tier: int = 1) -> int:
    return scoring.dungeon_score(tier, DUNGEON_TYPE, DUNGEON_SUBTYPE, floors)


def walk(c: Walker, floors: int, rng: random.Random, gap_rule: bool = True, kill_share: float = 1.0,
         tier: int = 1) -> list[float]:
    """Clear one dungeon floor by floor, then its boss; returns each floor's experience factor."""
    score = dungeon_score(floors, tier)
    width = scoring.tier_width(tier)
    killed = round(curve.POPULATION * kill_share)
    factors = []
    exp_factor = loot_factor = 1.0
    for floor in range(1, floors + 1):
        if gap_rule:
            gap = (score - player_power.power_score(c.character())) / width
            exp_factor = c.supply.rule.multiplier(gap, tier=tier)
            loot_factor = c.supply.rule.multiplier(gap, loot=True, tier=tier)
        factors.append(exp_factor)
        enemy = scoring.enemy_scores(tier, DUNGEON_TYPE, DUNGEON_SUBTYPE, floors, floor)
        for _ in range(killed):
            rarity = spawn_rarity(rng)
            kill(c, rarity, enemy[rarity] * exp_factor, loot_factor, rng, tier)
    boss = scoring.final_boss_score(tier, DUNGEON_TYPE, DUNGEON_SUBTYPE, floors)
    kill(c, "Boss", boss * exp_factor, loot_factor, rng, tier)
    return factors


def outpost_floors() -> tuple[int, int]:
    return TuningConfig().spec(DungeonType.BASIC, CityTier.OUTPOST).floors


@dataclasses.dataclass
class Point:
    """One seed's character after N dungeons."""

    dungeons: int
    character: player_power.Character
    dungeon_scores: tuple[int, ...]
    gear_dropped: int
    gems_dropped: int
    by_family: dict[str, float]


def run(supply: Supply, seed: int, gap_rule: bool = True, kill_share: float = 1.0) -> list[Point]:
    rng = random.Random(seed)
    low, high = outpost_floors()
    c = Walker(supply)
    out, scores = [], []
    for n in range(1, max(CHECKPOINTS) + 1):
        floors = rng.randint(low, high)
        scores.append(dungeon_score(floors))
        walk(c, floors, rng, gap_rule, kill_share)
        if n in CHECKPOINTS:
            out.append(Point(n, c.character(), tuple(scores), c.gear_dropped, c.gems_dropped, c.by_family()))
    return out


def run_many(supply: Supply, gap_rule: bool = True, kill_share: float = 1.0, seeds: int = SEEDS) -> dict[int, list]:
    by_n: dict[int, list[Point]] = {n: [] for n in CHECKPOINTS}
    for seed in range(seeds):
        for point in run(supply, seed, gap_rule, kill_share):
            by_n[point.dungeons].append(point)
    return by_n


def campaign(supply: Supply, seed: int) -> tuple[player_power.Character, float]:
    """A whole tier 1 campaign as the experience curve sizes one (26 dungeons of 50 floors), the gap rule on; the
    character and the mean experience factor."""
    rng = random.Random(10_000 + seed)
    c = Walker(supply)
    factors: list[float] = []
    for _ in range(curve.CAMPAIGN_DUNGEONS):
        factors.extend(walk(c, curve.WHOLE_FLOORS, rng))
    return c.character(), statistics.mean(factors)


# --------------------------------------------------------------------------
# Gems: the curve
# --------------------------------------------------------------------------

def need(tier: int) -> float:
    """The gems a gear drop must carry for a full clear of the tier to fill its reference character, against the
    shipped (halved) gear column (`analyse_gem_drops.needed_share`)."""
    return gem_need.needed_share(tier)


def carried(share: Callable[[int], float], tier: int) -> float:
    """The gems a gear drop carries at this tier under a curve."""
    return loot.GEM_DROPS_PER_GEAR_DROP * share(tier)


def gems_in_an_average_dungeon(share: Callable[[int], float], tier: int) -> float:
    return carried(share, tier) * gem_need.gear_drops_per_kill() * gem_need.kills_in_a_tier() / curve.CAMPAIGN_DUNGEONS


def gems_walked(share: Callable[[int], float], tier: int, seed: int) -> list[int]:
    """Gems a full clear of 1, 2 and 4 Outpost dungeons at this tier drops under a curve: the creatures and the boss
    drawn as the walk draws them, only the gem roll made, the gap rule off (the drops alone)."""
    rng = random.Random(seed)
    low, high = outpost_floors()
    factor = share(tier) / loot.gem_drop_share(tier)
    dropped, out = 0, []
    for n in range(1, max(CHECKPOINTS) + 1):
        floors = rng.randint(low, high)
        for _ in range(floors * round(curve.POPULATION)):
            dropped += len(loot.roll_gem_drops_from_kill(spawn_rarity(rng), tier, 0.0, 100.0 * factor, rng))
        dropped += len(loot.roll_gem_drops_from_kill("Boss", tier, 0.0, 100.0 * factor, rng))
        if n in CHECKPOINTS:
            out.append(dropped)
    return out


# --------------------------------------------------------------------------
# The band
# --------------------------------------------------------------------------

LEVELS_AT_TIER_ENDS = curve.whole_levels_at_tier_ends()


def curve_character_score(tier: int) -> int:
    """The character the experience curve produces at the end of a tier: the curve's level, the tier's reference gear."""
    return player_power.power_score(dataclasses.replace(player_power.reference_character(tier),
                                                        level=LEVELS_AT_TIER_ENDS[tier - 1]))


def above_lowest(tier: int, score: float) -> float:
    """How far a score sits above the shallowest dungeon its tier spawns, in tier widths (the rule's unit)."""
    low, _ = gap_rewards.spawn_extremes(tier)
    return (score - low.score) / scoring.tier_width(tier)


def next_tiers_character_factor(tier: int, rule: gap_rewards.Rule) -> float:
    """The experience factor of the next tier's finished character in this tier's average dungeon."""
    dungeon = scoring.dungeon_score(tier, DUNGEON_TYPE, DUNGEON_SUBTYPE, curve.WHOLE_FLOORS)
    return rule.multiplier((dungeon - curve_character_score(tier + 1)) / scoring.tier_width(tier), tier=tier)


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def percentile(values: list[float], share: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def terms(c: player_power.Character) -> tuple[float, float, float, float]:
    return (player_power.level_term(c), player_power.gear_term(c), player_power.gem_term(c),
            player_power.resistance_term(c))


def score_line(points: list[Point]) -> str:
    scores = [player_power.power_score(p.character) for p in points]
    parts = [statistics.mean(terms(p.character)[i] for p in points) for i in range(4)]
    dungeon = statistics.mean(statistics.mean(p.dungeon_scores) for p in points)
    level = statistics.mean(p.character.level for p in points)
    return (f"{statistics.mean(scores):5.0f} ({percentile(scores, 0.1):4.0f}-{percentile(scores, 0.9):4.0f}) "
            f"| {parts[0]:4.0f} {parts[1]:4.0f} {parts[2]:4.0f} {parts[3]:4.0f} | lvl {level:4.1f} "
            f"| dungeon {dungeon:4.0f} | x{statistics.mean(scores) / dungeon:4.2f}")


def supply_line(points: list[Point]) -> str:
    gems = statistics.mean(len(p.character.gems) for p in points)
    resist = statistics.mean(sum(min(r, player_power.RESISTANCE_CAP) for r in p.character.resistances)
                             for p in points)
    return (f"gear dropped {statistics.mean(p.gear_dropped for p in points):6.0f} | gems dropped "
            f"{statistics.mean(p.gems_dropped for p in points):5.1f}, socketed {gems:4.1f} | resistance points "
            f"{resist:5.1f}")


def family_line(points: list[Point]) -> str:
    names = [family.name for family in af.RESISTANCE_FAMILIES]
    means = {name: statistics.mean(p.by_family[name] for p in points) for name in names}
    total = sum(means.values()) or 1.0
    return "  ".join(f"{name} {means[name]:5.1f} ({100 * means[name] / total:2.0f}%)" for name in names)


def section_gems() -> None:
    print("GEMS: THE CURVE, a share of tier 8's rate at each tier. The need is what a full clear of the tier needs to")
    print("  fill its reference character, gems a gear drop at the shipped (halved) gear column.")
    print("  Tier |   need    | " + " | ".join(f"{name[:24]:>24s}" for name, _ in GEM_CURVES))
    for tier in curve.TIERS:
        cells = [f"{share(tier):6.4f} x{carried(share, tier) / need(tier):6.2f} {gems_in_an_average_dungeon(share, tier):5.1f}"
                 for _, share in GEM_CURVES]
        print(f"  {tier:4d} | {need(tier):.7f} | " + " | ".join(f"{cell:>24s}" for cell in cells))
    print("  (each cell: the share, its margin over the need, gems in an average 50-floor dungeon)")
    for name, share in GEM_CURVES:
        steps = [share(t + 1) / share(t) for t in range(1, 8)]
        below = [t for t in curve.TIERS if carried(share, t) < need(t)]
        print(f"  {name:32s}: steps {', '.join(f'{s:.2f}' for s in steps)}; tiers below their need: {below or 'none'}")
    print()
    print(f"  Gems DROPPED by a full clear of 1, 2 and 4 Outpost dungeons, {SEEDS} seeds, the gap rule off: mean "
          "(10th-90th percentile)")
    for tier in (1, 4):
        print(f"  Tier {tier}:")
        for name, share in GEM_CURVES:
            walked = [gems_walked(share, tier, seed) for seed in range(SEEDS)]
            cells = []
            for i, n in enumerate(CHECKPOINTS):
                values = [w[i] for w in walked]
                cells.append(f"after {n}: {statistics.mean(values):6.1f} ({percentile(values, 0.1):4.0f}-"
                             f"{percentile(values, 0.9):4.0f})")
            print(f"    {name:32s} " + "  ".join(cells))
    print()


def section_gear() -> None:
    print("GEAR: drops a kill over what spawns, a floor, an Outpost dungeon, and a slot of it")
    low, high = outpost_floors()
    for supply in (BEFORE, SHIPPED):
        per_kill = gem_need.gear_drops_per_kill() * supply.gear
        floor = per_kill * curve.POPULATION
        outpost = floor * (low + high) / 2
        print(f"  {supply.name:22s}: {per_kill:.3f} a kill, {floor:6.1f} a floor, {outpost:6.0f} an Outpost dungeon, "
              f"{outpost / len(POSITIONS):5.1f} a slot")
    print()


def section_band() -> None:
    print("THE BAND: where each character sits above the shallowest dungeon its tier spawns, in tier widths")
    print("  Tier | curve level | reference | curve character | ruled start | next tier's character: rule D, ruled")
    for tier in curve.TIERS:
        reference = above_lowest(tier, player_power.power_score(player_power.reference_character(tier)))
        mine = above_lowest(tier, curve_character_score(tier))
        over = (f"{next_tiers_character_factor(tier, RULE_D):.2f}, "
                f"{next_tiers_character_factor(tier, gap_rewards.RULED):.2f}" if tier < 8 else "-")
        print(f"  {tier:4d} | {LEVELS_AT_TIER_ENDS[tier - 1]:11d} | {reference:9.2f} | {mine:15.2f} | "
              f"{gap_rewards.RULED.below_at(tier):11.1f} | {over}")
    print()


def section_together() -> None:
    print("TOGETHER: a fresh tier 1 character, Outpost dungeons, full clear, each supply with its own gap rule ON")
    print("  score: mean (10th-90th) | level gear gems resistances | level | the dungeons' mean Dungeon Score | the "
          "score over it")
    walked = {}
    for supply in (BEFORE, SHIPPED):
        walked[supply.name] = run_many(supply)
        print(f"  {supply.name}:")
        for n in CHECKPOINTS:
            points = walked[supply.name][n]
            print(f"    after {n}: {score_line(points)}")
            print(f"             {supply_line(points)}")
            print(f"             resistance by family, before the cap: {family_line(points)}")
    print("  Half the creatures killed:")
    for supply in (BEFORE, SHIPPED):
        half = run_many(supply, kill_share=0.5)
        print(f"  {supply.name}:")
        for n in CHECKPOINTS:
            print(f"    after {n}: {score_line(half[n])} | {supply_line(half[n])}")
    print()
    dungeon = dungeon_score(curve.WHOLE_FLOORS)
    print(f"A WHOLE TIER 1 CAMPAIGN: {curve.CAMPAIGN_DUNGEONS} dungeons of {curve.WHOLE_FLOORS} floors (Dungeon Score "
          f"{dungeon}), full clear, {CAMPAIGN_SEEDS} seeds, each supply with its own gap rule; the tier 1 ceiling is "
          f"{scoring.PLAYER_MAX_SCORES[1]:.0f} and the reference character scores "
          f"{player_power.power_score(player_power.reference_character(1))}")
    print("  score | level gear gems resistances | level | gems socketed, resistance points | x the dungeon "
          "| mean experience factor")
    for supply in (BEFORE, SHIPPED):
        done = [campaign(supply, seed) for seed in range(CAMPAIGN_SEEDS)]
        characters = [c for c, _ in done]
        scores = [player_power.power_score(c) for c in characters]
        parts = [statistics.mean(terms(c)[i] for c in characters) for i in range(4)]
        gems = statistics.mean(len(c.gems) for c in characters)
        resist = statistics.mean(sum(min(r, player_power.RESISTANCE_CAP) for r in c.resistances) for c in characters)
        print(f"  {supply.name:22s}: {statistics.mean(scores):5.0f} | {parts[0]:4.0f} {parts[1]:4.0f} {parts[2]:4.0f} "
              f"{parts[3]:4.0f} | lvl {statistics.mean(c.level for c in characters):4.1f} | {gems:4.1f}, "
              f"{resist:5.1f} | x{statistics.mean(scores) / dungeon:4.2f} | {statistics.mean(f for _, f in done):.2f}")


def main() -> None:
    section_gems()
    section_gear()
    section_band()
    section_together()


if __name__ == "__main__":
    main()
