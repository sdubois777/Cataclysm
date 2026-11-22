"""What experience and loot scaled by the Power Score gap would do to the level curve and to drops per tier.

WHY THIS EXISTS. Issue #2824, slice c, assigned by the coordinating session on 2026-11-22: "Dungeons within a close
power range provide average exp and loot. Dungeons below the threshold provide less. Dungeons above provide more."
(`docs/Cataclysm_GDD_v2.md`, section IV, "Power Score".) The design states the direction only. Every number here is a
candidate for a ruling, and this script shows what each candidate does BEFORE any game code is written, because the
level curve is fitted so 208 dungeons pay for 99 levels (`analyse_experience_curve.py`) and loot feeds the forge and
the empire tree.

THE GAP: the dungeon's score less the player's, as a share of the dungeon tier's width, the unit Overwhelm already
uses:

    gap = (dungeon score - player score) / tier width

positive when the dungeon is above the player. THE DUNGEON'S SCORE IS THE DESIGN'S DUNGEON SCORE
(`docs/Cataclysm_GDD_v2.md`, "Dungeon Score Formula": the middle floor, rarity-weighted, as `scoring.dungeon_score`
computes it), on the average dungeon of `analyse_experience_curve.py`: the coordinating session's correction of
2026-11-22, in place of the plan's middle-floor Common Enemy Score.

THE RULE, one multiplier for a kill's experience and its loot quantity:

    inside the band (|gap| <= band):    1
    above it (gap > band):              1 + cap_up x min(1, (gap - band) / ramp_up)
    below it (gap < -band):             1 - (1 - floor) x min(1, (-gap - band) / ramp_down)

THE GENRE, as far as a readable source states it (`.claude/windows/research_2824_reward_by_gap.md` in the worktree
that wrote this; restated in the proposal):
    Diablo IV (diablo4.wiki.fextralife.com/Experience, a fan wiki): above the player's level +15%, rising, "capped at
    twenty-five percent"; below it -10% a level, to nothing at ten levels.
    Diablo II (classic.battle.net, the experience basics page): full experience within 5 levels either side; below the
    player it falls to 5% by 10 levels; above it to 2%. No bonus.
    Path of Exile: its penalty formula is an image on poedb.tw and could not be read; it is NOT used here.
    Last Epoch: no rule by the gap. No readable source gives a LOOT rule by the gap.

WHAT A PLAYER'S SCORE DOES DURING A CAMPAIGN, the assumption that decides the level curve: the reference character
(`scoring.PLAYER_MAX_SCORES`) is the score at the END of a tier's campaign; the start is the tier below's. The script
moves the score evenly from one to the other across the campaign's 26 dungeons. A JUDGEMENT: nothing measures how a
real player's score rises.

Run: python analyse_power_gap_rewards.py
"""

from __future__ import annotations

import contextlib
import csv
import dataclasses
import io
import pathlib

from cataclysm_sim import loot, modifiers, scoring
from cataclysm_sim.config import TuningConfig

# THE EXPERIENCE CURVE'S SCRIPT PRINTS ITS WHOLE REPORT ON IMPORT, as every analysis script does; it is read for its
# figures, not its report, so that is discarded.
with contextlib.redirect_stdout(io.StringIO()):
    import analyse_experience_curve as curve

DATA = pathlib.Path(__file__).resolve().parents[1] / "game" / "Data"
TIERS = list(curve.TIERS)
FLOORS = curve.WHOLE_FLOORS


@dataclasses.dataclass(frozen=True)
class Rule:
    """One candidate: the band, the bonus above it and the cut below it, in tier widths and shares."""

    name: str
    band: float
    cap_up: float
    ramp_up: float
    floor: float
    ramp_down: float
    #: The band's other side, when it differs: how far ABOVE the dungeon the player may be with full reward. None is
    #: the same as `band`.
    band_down: float | None = None

    def multiplier(self, gap: float) -> float:
        below = self.band if self.band_down is None else self.band_down
        if gap > self.band:
            return 1.0 + self.cap_up * min(1.0, (gap - self.band) / self.ramp_up)
        if gap < -below:
            return 1.0 - (1.0 - self.floor) * min(1.0, (-gap - below) / self.ramp_down)
        return 1.0


#: THE CANDIDATES. Each figure is a judgement; its source is in the column after it.
RULES = [
    # The plan's Q9: a quarter of a tier width (Overwhelm's unit); Diablo IV's 25% cap above, reached half a width
    # beyond the band; below, Diablo II's 5% floor, reached a whole width beyond the band.
    Rule("A: band 0.25, gentle cut", band=0.25, cap_up=0.25, ramp_up=0.5, floor=0.05, ramp_down=1.0),
    # The same band with Diablo II's own steepness: the cut takes as long as the band is wide.
    Rule("B: band 0.25, Diablo II's steep cut", band=0.25, cap_up=0.25, ramp_up=0.5, floor=0.05, ramp_down=0.25),
    # A band wide enough to hold the reference character at its own tier, 0.51 to 0.54 of a width above the middle
    # floor's Common score, with 0.06 to spare: about the tier's own range either side of the middle floor.
    Rule("C: band 0.6, gentle cut", band=0.6, cap_up=0.25, ramp_up=0.5, floor=0.05, ramp_down=1.0),
    # Uneven: the bonus begins a quarter width above the dungeon, as in A, and the cut 0.6 below it, as in C.
    Rule("D: bonus beyond 0.25, cut beyond 0.6", band=0.25, cap_up=0.25, ramp_up=0.5, floor=0.05, ramp_down=1.0,
         band_down=0.6),
]


def dungeon_score(tier: int) -> float:
    """The design's Dungeon Score of the average dungeon: its middle floor, rarity-weighted."""
    return scoring.dungeon_score(tier, curve.REFERENCE_TYPE, curve.REFERENCE_SUBTYPE, FLOORS)


def gap(tier: int, player: float) -> float:
    return (dungeon_score(tier) - player) / scoring.tier_width(tier)


def reference(tier: int) -> float:
    """The reference character's score at the end of the tier: the tier's maximum."""
    return scoring.PLAYER_MAX_SCORES[tier]


def campaign_start(tier: int) -> float:
    """The score a player starts the tier's campaign with: the tier below's maximum, 0 for tier 1."""
    return scoring.PLAYER_MAX_SCORES[tier - 1] if tier > 1 else 0.0


def campaign_multiplier(rule: Rule, tier: int) -> float:
    """The average multiplier over a campaign whose score rises evenly from the start to the reference."""
    start, end = campaign_start(tier), reference(tier)
    steps = curve.CAMPAIGN_DUNGEONS
    return sum(rule.multiplier(gap(tier, start + (end - start) * (k + 0.5) / steps)) for k in range(steps)) / steps


@dataclasses.dataclass(frozen=True)
class Spawned:
    """One dungeon a tier can spawn, at one end of its range, and its Dungeon Score."""

    score: float
    what: str


def spawn_extremes(tier: int) -> tuple[Spawned, Spawned]:
    """The lowest and the highest Dungeon Score among the dungeons a tier can spawn.

    WHAT IS SPANNED, from the simulation's own tables: every (type, city tier) row of `TuningConfig.DUNGEON_SPECS`, at
    the shortest and the longest floor count of its range; every sub-type the type allows
    (`SUBTYPE_SPAWN_WEIGHTS` less `SUBTYPES_FORBIDDEN_ON`); and modifiers at their least (one per difficulty tier,
    each the smallest in `modifiers.MODIFIERS`, 5) and their most (each the largest, 20, twice as many on a
    Sacrificial dungeon, and the Corrupted Stalker's 20 on top). The empire tree's added floors are left out.
    """
    cfg = TuningConfig()
    smallest = min(score for name, rows in modifiers.MODIFIERS.items() if name != "Generic" for _, score in rows)
    largest = max(score for name, rows in modifiers.MODIFIERS.items() if name != "Generic" for _, score in rows)
    low: Spawned | None = None
    high: Spawned | None = None
    for (dtype, city_tier), spec in cfg.DUNGEON_SPECS.items():
        allowed = [sub for sub in cfg.SUBTYPE_SPAWN_WEIGHTS if sub not in cfg.SUBTYPES_FORBIDDEN_ON.get(dtype, ())]
        for floors in spec.floors:
            for sub in allowed:
                slots = cfg.modifiers_per_tier * tier * (cfg.sacrificial_modifier_multiplier if sub == "Sacrificial" else 1)
                for label, mods in (("fewest modifier points", slots * smallest),
                                    ("most modifier points and the Stalker", slots * largest + modifiers.CORRUPTED_STALKER[1])):
                    # THE SCORING PORT NAMES THE TYPE AS THE REFERENCE DOES, "Fallen City" with a space.
                    kind = "Fallen City" if dtype.value == "FallenCity" else dtype.value
                    score = scoring.dungeon_score(tier, kind, sub, floors, mods)
                    what = f"{kind} on a {city_tier.value}, {floors} floors, {sub}, {label}"
                    if low is None or score < low.score:
                        low = Spawned(score, what)
                    if high is None or score > high.score:
                        high = Spawned(score, what)
    assert low is not None and high is not None
    return low, high


def rates(column: str) -> dict[str, float]:
    with (DATA / "EnemyDrops.csv").open(encoding="utf-8", newline="") as handle:
        return {row["EnemyRarity"]: float(row[column]) for row in csv.DictReader(handle)}


def per_dungeon(column: str) -> float:
    """A full clear of the average dungeon at the baseline: creatures x the spawn-weighted rate, and its boss."""
    rate = rates(column)
    per_creature = sum(rate[rarity] * weight for rarity, weight in curve.WEIGHTS.items())
    return per_creature * curve.POPULATION * FLOORS + rate["Boss"]


def main() -> None:
    print("THE GAP OF THREE PLAYERS AGAINST EACH TIER'S DUNGEON, in tier widths (positive: the dungeon is above)\n")
    print("  The dungeon is the design's Dungeon Score of the average dungeon (50 floors, Basic): its middle")
    print("  floor, rarity-weighted.")
    print("  'At the reference' is the tier's maximum score; 'one tier above' is the next tier's maximum; 'one tier")
    print("  below' is the tier below's maximum, which is also where the tier's campaign starts.\n")
    print("  Tier | Dungeon | Width | Reference | Gap   | One tier above | Gap   | One tier below | Gap")
    for tier in TIERS:
        above = scoring.PLAYER_MAX_SCORES.get(tier + 1, reference(tier) + scoring.tier_width(tier))
        below = campaign_start(tier)
        print(f"  {tier:4d} | {dungeon_score(tier):7.0f} | {scoring.tier_width(tier):5.0f} | {reference(tier):9.0f} | "
              f"{gap(tier, reference(tier)):+.2f} | {above:14.0f} | {gap(tier, above):+.2f} | {below:14.0f} | "
              f"{gap(tier, below):+.2f}")
    print("\n  THE REFERENCE CHARACTER IS ABOUT HALF A TIER WIDTH ABOVE ITS OWN TIER'S DUNGEON. A band narrower than")
    print("  that cuts the reward of a player who is exactly where the design expects them at the end of a tier.\n")

    for rule in RULES:
        below = rule.band if rule.band_down is None else rule.band_down
        print(f"RULE {rule.name}: bonus beyond {rule.band}, up to +{rule.cap_up:.0%} by {rule.ramp_up} further; "
              f"cut beyond {below}, to {rule.floor:.0%} by {rule.ramp_down} further")
        print("  Tier | Reference | One tier above | One tier below | Campaign average (start to reference)")
        averages = {}
        for tier in TIERS:
            above = scoring.PLAYER_MAX_SCORES.get(tier + 1, reference(tier) + scoring.tier_width(tier))
            averages[tier] = campaign_multiplier(rule, tier)
            print(f"  {tier:4d} | {rule.multiplier(gap(tier, reference(tier))):9.2f} | "
                  f"{rule.multiplier(gap(tier, above)):14.2f} | {rule.multiplier(gap(tier, campaign_start(tier))):14.2f} | "
                  f"{averages[tier]:.3f}")
        paid = {tier: curve.PER_DUNGEON[tier] * averages[tier] for tier in TIERS}
        levels = curve.whole_levels_at_tier_ends(per_dungeon=paid)
        print("  Levels at the end of each tier, the curve unchanged:", "  ".join(f"{level:3d}" for level in levels))
        total = sum(paid.values()) / sum(curve.PER_DUNGEON.values())
        print(f"  The whole climb's experience is {total:.3f} times today's; level 100 "
              f"{'arrives' if levels[-1] >= 100 else 'does NOT arrive'} by the end of tier 8.\n")

    print("THE WITHIN-TIER SPREAD: WHAT A TIER ACTUALLY SPAWNS, against the reference and a player halfway through")
    print()
    print("  A dungeon's timer threatens a city, so a player clears shallow and deep dungeons of their own tier alike.")
    lows_and_highs = {tier: spawn_extremes(tier) for tier in TIERS}
    for tier in TIERS:
        low, high = lows_and_highs[tier]
        print(f"  Tier {tier}: lowest {low.score:.0f} ({low.what});")
        print(f"          highest {high.score:.0f} ({high.what})")
    print()
    print("  Gap from the player, in tier widths, and the factor each rule gives (A, C, D):")
    print("  Tier | Player    | Lowest: gap  A     C     D    | Average: gap  A     C     D    | Highest: gap  A     C     D")
    for tier in TIERS:
        low, high = lows_and_highs[tier]
        width = scoring.tier_width(tier)
        for label, player in (("reference", reference(tier)), ("halfway", campaign_start(tier) + width / 2)):
            cells = []
            for score in (low.score, dungeon_score(tier), high.score):
                g = (score - player) / width
                cells.append(f"{g:+.2f}  " + "  ".join(f"{rule.multiplier(g):.2f}" for rule in (RULES[0], RULES[2], RULES[3])))
            print(f"  {tier:4d} | {label:9s} | {cells[0]:24s} | {cells[1]:24s} | {cells[2]}")
    print()

    print("TODAY, for comparison: levels at the end of each tier",
          "  ".join(f"{level:3d}" for level in curve.whole_levels_at_tier_ends()), "\n")

    print("DROPS PER FULL CLEAR OF THE AVERAGE DUNGEON, AT THE BASELINE (no magic find, loot quantity 100%)")
    print("  The rates do not depend on the tier; the multiplier above scales each count if loot is scaled by quantity.")
    for column, label in (("GearDrops", "gear"), ("MaterialDrops", "crafting materials"), ("GemDrops", "gems"),
                          ("GoldDrops", "gold piles")):
        print(f"  {label:18s} {per_dungeon(column):9.1f}")
    print()
    gear = per_dungeon("GearDrops")
    for rule in (RULES[0], RULES[2], RULES[3]):
        print(f"  Under rule {rule.name}, gear per dungeon (today {gear:.0f} for every player):")
        print("  Tier | Reference | One tier above | One tier below")
        for tier in TIERS:
            above = scoring.PLAYER_MAX_SCORES.get(tier + 1, reference(tier) + scoring.tier_width(tier))
            print(f"  {tier:4d} | {gear * rule.multiplier(gap(tier, reference(tier))):9.0f} | "
                  f"{gear * rule.multiplier(gap(tier, above)):14.0f} | "
                  f"{gear * rule.multiplier(gap(tier, campaign_start(tier))):14.0f}")
        print()
    assert loot.ENEMY_GEAR_DROPS["Boss"] == rates("GearDrops")["Boss"], "the model and EnemyDrops.csv disagree"


# PRINTED ON IMPORT, as every analysis script is: `sim/tests/test_analysis_scripts.py` runs each with `runpy.run_path`,
# which does not set `__name__` to `__main__`.
main()
