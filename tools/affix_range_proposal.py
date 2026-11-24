"""A PROPOSAL, NOT A CHANGE: every affix tier as a stated whole-number range.

The project owner, 2026-11-24:

    "take a look at the values and ranges of affixes. Some of them feel
    arbitrarily low. Like 1.2% and stuff like that. I really hate decimals, and
    they should be removed in all cases, but also, each affix tier should have
    it's own range, and that range should be something a PLAYER finds
    reasonable. Not math."

This module does four things and changes nothing in the game:

  inventory   every affix tier as the game computes it today, from
              `game/Data/Affixes.csv` and the simulation's own `affix_value`,
              with its decimals, overlaps and widths
  proposal    the proposed table: seven stated whole-number ranges per affix,
              built by the rule in `.claude/windows/proposal_affix_ranges.md`
  check       the proposal's own invariants, so a hand edit to the table cannot
              quietly break one
  apply       writes the table into a COPY of the design workbook. It refuses
              the real workbook unless told otherwise in so many words, because
              other branches hold unmerged edits to it and nothing may be
              applied before the owner rules.

Run from the repository root:

    python tools/affix_range_proposal.py inventory
    python tools/affix_range_proposal.py table
    python tools/affix_range_proposal.py measure
    python tools/affix_range_proposal.py check
    python tools/affix_range_proposal.py apply --out <a copy>.xlsx

THE RULE IN ONE PARAGRAPH. A tier is a range of whole numbers written out in
the workbook, contiguous with the tiers either side and never overlapping them,
holding at least two values. A roll lands on one of those whole numbers, each
equally likely, which is the rule enchantments already roll by. An affix that
adds an AMOUNT (the sheet's Percent column says no: health, mana, shield,
armour, attack damage, class resource, regeneration) is multiplied by the
piece's upgrade level and rounded to a whole number once; its table is the +0
value. An affix that is a PERCENTAGE is the stated number at every upgrade
level. Implicits keep the upgrade lever and are restated as whole +0 values.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import math
import pathlib
import shutil
import sys
from contextlib import contextmanager

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SIM_ROOT = REPO_ROOT / "sim"
AFFIX_CSV = REPO_ROOT / "game" / "Data" / "Affixes.csv"
BASES_CSV = REPO_ROOT / "game" / "Data" / "ItemBases.csv"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

TIERS = (1, 2, 3, 4, 5, 6, 7)
MAX_GEAR_LEVEL = 10

#: How many whole values a tier must hold at the least. Two is the smallest
#: number that leaves the reroll and perfect crafting materials something to do.
MIN_VALUES_PER_TIER = 2

#: The smallest seven-tier ladder that can exist: seven tiers of two values.
MIN_LADDER_VALUES = len(TIERS) * MIN_VALUES_PER_TIER

#: The share of a tier's top its band reaches down to today. Reused for where
#: tier 7 starts, so the top tier reads the way today's top band reads.
TOP_TIER_START = 0.75


def _sim():
    """The simulation's affix module, which is where today's values come from."""
    if str(SIM_ROOT) not in sys.path:
        sys.path.insert(0, str(SIM_ROOT))
    from cataclysm_sim import affixes
    return affixes


def round_half_up(value: float) -> int:
    """A half rounds up, as an attribute does ("How an Attribute Rounds").

    Python's own round() sends a half to the even neighbour, so 52.5 would
    become 52 and 37.5 would become 38. The design states the half-up rule.
    """
    if value >= 0:
        return math.floor(value + 0.5)
    return -math.floor(-value + 0.5)


def gear_multiplier(gear_level: int) -> float:
    af = _sim()
    return af.gear_level_multiplier(gear_level)


# ---------------------------------------------------------------------------
# Today's affixes
# ---------------------------------------------------------------------------

def read_affix_rows(path: pathlib.Path = AFFIX_CSV) -> list[dict[str, str]]:
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_base_rows(path: pathlib.Path = BASES_CSV) -> list[dict[str, str]]:
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def is_percent(row: dict[str, str]) -> bool:
    return row["Percent"].strip().lower() in ("true", "yes", "1")


def is_amount(row: dict[str, str]) -> bool:
    """Whether the affix adds an amount rather than a percentage.

    Read off two columns the sheet already has: a Stat affix that is flat and
    whose Percent column says no. Resistance and Ailment rows are percentages.
    """
    return (row["AffixKind"] == "Stat" and row["ValueKind"] == "flat"
            and not is_percent(row))


def slots_of(text: str) -> tuple[str, ...]:
    return tuple(s.strip() for s in text.split(",") if s.strip())


@dataclasses.dataclass(frozen=True)
class TodayTier:
    tier: int
    low0: float
    high0: float
    low10: float
    high10: float


@dataclasses.dataclass(frozen=True)
class TodayAffix:
    name: str
    kind: str
    top: float
    floor: float
    amount: bool
    tiers: tuple[TodayTier, ...]

    @property
    def has_decimal(self) -> bool:
        return any(not _whole(v) for t in self.tiers
                   for v in (t.low0, t.high0, t.low10, t.high10))

    @property
    def overlaps(self) -> int:
        """How many adjacent tier pairs overlap at +0."""
        return sum(1 for a, b in zip(self.tiers, self.tiers[1:], strict=False)
                   if a.high0 >= b.low0)


def _whole(value: float) -> bool:
    return abs(value - round(value)) < 1e-9


def today(rows: list[dict[str, str]] | None = None) -> list[TodayAffix]:
    """Every non-hybrid affix's seven tiers as the game computes them today."""
    af = _sim()
    rows = rows if rows is not None else read_affix_rows()
    out = []
    for row in rows:
        if row["AffixKind"] == "Hybrid":
            continue
        top = float(row["TopValue"])
        floor = float(row["Floor"] or 0.0)
        tiers = tuple(
            TodayTier(t,
                      af.affix_value(top, t, 0.0, 0, floor),
                      af.affix_value(top, t, 1.0, 0, floor),
                      af.affix_value(top, t, 0.0, MAX_GEAR_LEVEL, floor),
                      af.affix_value(top, t, 1.0, MAX_GEAR_LEVEL, floor))
            for t in TIERS)
        out.append(TodayAffix(row["AffixName"], row["AffixKind"], top, floor,
                              is_amount(row), tiers))
    return out


# ---------------------------------------------------------------------------
# The proposal
# ---------------------------------------------------------------------------

#: Where the lowest tier starts for a PERCENTAGE affix, as a share of its top.
#:
#: Today's worst tier 1 roll on a piece at +3, which is what a difficulty tier 1
#: character carries (`player_damage.gear_level_at(1)`). A percentage no longer
#: grows with upgrades under this proposal, so its ladder has to carry the whole
#: climb by tier alone; starting it where a tier 1 character starts today keeps
#: the bottom of the game where it is. 0.75 x 3^-1 x 1.45 / 2.5 = 0.145.
def percent_start_share() -> float:
    af = _sim()
    return (af.TIER_FRACTIONS[1] * (1.0 - af.ROLL_BAND_FRACTION)
            * af.gear_level_multiplier(3) / af.gear_level_multiplier(MAX_GEAR_LEVEL))


#: The least a tier 1 roll may be, by what the number measures. A judgement,
#: from the genre's own lowest tiers (Path of Exile: increased attack speed
#: 5-7, all resistances 3-5, item rarity 6-10) and from the floors the owner
#: approved on 2026-09-04 (resistances 10, 7 and 3; leech, damage reduction,
#: penetration and evasion 1).
MIN_START_INCREASE = 3
MIN_START_POINTS = 1
MIN_START_AMOUNT = 2


@dataclasses.dataclass(frozen=True)
class Override:
    """A stat whose scale or slots the rule alone cannot settle."""

    top: int | None = None              # a new top, in the table's own terms
    slots: tuple[str, ...] | None = None
    retired_into: str | None = None
    why: str = ""


#: Each one is a judgement, and the proposal document says why.
OVERRIDES: dict[str, Override] = {
    # CHANGE WHAT THE STAT MEASURES. Critical strike chance has a base on every
    # skill (5%) and on four implicits, so an increase is the natural affix and
    # the one Path of Exile uses on gear. 100% increased at the default base is
    # the old flat affix's top, 5 points.
    "Flat critical strike chance": Override(
        retired_into="Increased critical strike chance",
        why="retired: 1.25-5 points cannot hold seven whole ranges; folded into "
            "the increase, which reads the skill's base"),
    "Increased critical strike chance": Override(
        top=100,
        why="absorbs the flat affix: 100% increased is 5 points at the 5% "
            "default base, the old flat top"),
    # Every class has a mana regeneration base, so the increase carries it.
    "Flat mana regeneration": Override(
        retired_into="Increased mana regeneration",
        why="retired: 0.2-0.65 a second has no whole numbers at all; the "
            "increase covers it, since every class has a base"),
    # THE MOVEMENT-SPEED RULE of 2026-09-04: the top and the slot list move
    # together so the most a character can carry does not rise.
    "Flat evasion": Override(
        slots=("Head", "Chest", "Gloves", "Boots"),
        why="top 4 -> 14, four pieces instead of fourteen: 56 either way"),
    "Flat block chance": Override(
        slots=("Head", "Chest", "Shoulders", "Belt", "Pants"),
        why="top 5 -> 14, five pieces instead of fourteen: 70 either way"),
    "Flat damage reduction": Override(
        slots=("Chest", "Shoulders", "Belt", "Pants"),
        why="top 4 -> 14, four pieces instead of fourteen: 56 either way"),
    "Flat retaliation": Override(
        slots=("Head", "Chest", "Shoulders", "Boots"),
        why="top 4 -> 14, four pieces instead of fourteen: 56 either way"),
    "Flat crowd control resistance": Override(
        slots=("Head", "Shoulders", "Belt", "Pants", "Boots"),
        why="top 5 -> 14, five pieces instead of fourteen: 70 either way"),
    "Flat life leech": Override(
        slots=("Weapon", "Gloves", "Necklace"),
        why="top 4 -> 14, three pieces (four with an offhand) instead of twelve"),
    "Flat mana leech": Override(
        slots=("Weapon", "Gloves", "Necklace"),
        why="as life leech"),
    "Flat energy shield leech": Override(
        slots=("Weapon", "Gloves", "Necklace"),
        why="as life leech"),
    "Flat penetration": Override(
        slots=("Weapon", "Gloves", "Relic"),
        why="top 4 -> 14, three pieces instead of twelve; past an enemy's "
            "resistance (35 at most) penetration is wasted anyway"),
    "Flat maximum class resource": Override(
        slots=("Necklace", "Relic"),
        why="top 7 -> 38 at +10, two pieces instead of eleven: 76 against 77"),
    "Flat health regeneration": Override(
        slots=("Chest", "Belt", "Necklace"),
        why="top 8 -> 38 at +10, three pieces instead of twelve: 114 against 96"),
    "Flat energy shield regeneration": Override(
        slots=("Head", "Chest", "Shoulders"),
        why="top 10 -> 38 at +10, three pieces instead of twelve: 114 against 120"),
}


@dataclasses.dataclass(frozen=True)
class ProposedAffix:
    name: str
    row_name: str
    kind: str
    amount: bool                         # multiplied by the upgrade level
    tiers: tuple[tuple[int, int], ...]   # seven (low, high), +0 for an amount
    old_top: float
    slots: tuple[str, ...]
    old_slots: tuple[str, ...]
    retired_into: str | None = None
    hybrid_parts: tuple[str, str] | None = None
    note: str = ""

    @property
    def retired(self) -> bool:
        return self.retired_into is not None

    @property
    def top(self) -> int:
        """The endgame value: tier 7's best roll on a +10 piece."""
        high = self.tiers[-1][1]
        return round_half_up(high * gear_multiplier(MAX_GEAR_LEVEL)) if self.amount else high

    @property
    def raised(self) -> bool:
        return self.top > round_half_up(self.old_top)

    def pieces(self) -> int:
        af = _sim()
        return sum(af.GEAR_SLOTS[s] for s in self.slots)

    def old_pieces(self) -> int:
        af = _sim()
        return sum(af.GEAR_SLOTS[s] for s in self.old_slots)


def _snap(value: float) -> int:
    """A boundary a person would write: fives from 50, tens from 200."""
    if value >= 200:
        return int(round_half_up(value / 10.0) * 10)
    if value >= 50:
        return int(round_half_up(value / 5.0) * 5)
    return round_half_up(value)


def ladder(low: int, high: int) -> tuple[tuple[int, int], ...]:
    """Seven contiguous whole-number ranges from `low` to `high`.

    Tier 7 starts at three quarters of the top, which is where today's top band
    starts. Tiers 1 to 6 share what is left on a constant ratio, so the ladder is
    back-loaded the way today's is. Each tier holds at least two values; where
    the ratio would give fewer, the tier is widened and the ones above it pushed
    up. Boundaries of fifty and more are snapped to fives, and of two hundred and
    more to tens.

    Raises ValueError when `high - low + 1` is under fourteen, because seven
    two-value tiers cannot fit: the caller raises the top.
    """
    if high - low + 1 < MIN_LADDER_VALUES:
        raise ValueError(f"{low}..{high} cannot hold seven two-value tiers")

    top_start = _snap(TOP_TIER_START * high)
    top_start = min(top_start, high - (MIN_VALUES_PER_TIER - 1))
    top_start = max(top_start, low + 6 * MIN_VALUES_PER_TIER)

    ratio = (top_start / low) ** (1.0 / 6.0)
    starts = [low] + [_snap(low * ratio ** k) for k in range(1, 6)] + [top_start]
    for k in range(1, 7):
        starts[k] = max(starts[k], starts[k - 1] + MIN_VALUES_PER_TIER)
    for k in range(5, 0, -1):
        starts[k] = min(starts[k], starts[k + 1] - MIN_VALUES_PER_TIER)

    tiers = [(starts[k], starts[k + 1] - 1) for k in range(6)]
    tiers.append((top_start, high))
    return tuple(tiers)


def _start(row: dict[str, str], top: float) -> int:
    """Where tier 1 starts: today's bottom, lifted to the class minimum."""
    floor = float(row["Floor"] or 0.0)
    af = _sim()
    if is_amount(row):
        today_worst = top * af.WORST_MULTIPLIER
        return max(round_half_up(max(today_worst, floor)), MIN_START_AMOUNT)
    share = top * percent_start_share()
    start = round_half_up(max(share, floor))
    minimum = MIN_START_INCREASE if row["ValueKind"] == "increased" else MIN_START_POINTS
    return max(start, minimum)


def propose_row(row: dict[str, str]) -> ProposedAffix:
    """One non-hybrid affix under the rule."""
    override = OVERRIDES.get(row["AffixName"], Override())
    amount = is_amount(row)
    old_top = float(row["TopValue"])
    top = float(override.top) if override.top is not None else old_top
    start = _start(row, top)

    # An amount's table is its +0 value; a percentage's is its value.
    high = round_half_up(top / gear_multiplier(MAX_GEAR_LEVEL)) if amount else round_half_up(top)
    note = override.why
    if high - start + 1 < MIN_LADDER_VALUES:
        high = start + MIN_LADDER_VALUES - 1
        note = note or "raised: the smallest top that holds seven two-value tiers"

    old_slots = slots_of(row["AllowedSlots"])
    return ProposedAffix(
        name=row["AffixName"], row_name=row["Name"], kind=row["AffixKind"],
        amount=amount, tiers=ladder(start, high), old_top=old_top,
        slots=override.slots or old_slots, old_slots=old_slots,
        retired_into=override.retired_into, note=note)


def top_if_every_affix_scaled(row: dict[str, str]) -> int:
    """The endgame top an affix would need if upgrades multiplied every affix.

    THE ALTERNATIVE THE OWNER IS ASKED ABOUT, priced. If a percentage kept the
    upgrade lever, its table would be its +0 value like an amount's, and the
    +0 ladder needs fourteen whole numbers. This is the smallest +10 top that
    allows it, with tier 1 starting where today's worst +0 roll does (or the
    class minimum), and no slot narrowing.
    """
    af = _sim()
    old_top = float(row["TopValue"])
    floor = float(row["Floor"] or 0.0)
    minimum = (MIN_START_AMOUNT if is_amount(row)
               else MIN_START_INCREASE if row["ValueKind"] == "increased"
               else MIN_START_POINTS)
    start = max(round_half_up(max(old_top * af.WORST_MULTIPLIER, floor)), minimum)
    high = round_half_up(old_top / gear_multiplier(MAX_GEAR_LEVEL))
    high = max(high, start + MIN_LADDER_VALUES - 1)
    return round_half_up(high * gear_multiplier(MAX_GEAR_LEVEL))


def proposal(rows: list[dict[str, str]] | None = None) -> dict[str, ProposedAffix]:
    """Every affix under the rule, hybrids included, keyed by affix name."""
    rows = rows if rows is not None else read_affix_rows()
    out: dict[str, ProposedAffix] = {}
    for row in rows:
        if row["AffixKind"] != "Hybrid":
            out[row["AffixName"]] = propose_row(row)

    for row in rows:
        if row["AffixKind"] != "Hybrid":
            continue
        parts = []
        for part in (row["HybridPart1"], row["HybridPart2"]):
            target = out[part]
            parts.append(target.retired_into or part)
        allowed = set(slots_of(row["AllowedSlots"]))
        for part in parts:
            allowed &= set(out[part].slots)
        old_slots = slots_of(row["AllowedSlots"])
        slots = tuple(s for s in old_slots if s in allowed)
        renamed = tuple(parts) != (row["HybridPart1"], row["HybridPart2"])
        out[row["AffixName"]] = ProposedAffix(
            name=row["AffixName"], row_name=row["Name"], kind="Hybrid",
            amount=False, tiers=(), old_top=0.0, slots=slots,
            old_slots=old_slots, hybrid_parts=(parts[0], parts[1]),
            note="a half was retired and is replaced" if renamed else "")
    return out


def roll_value(low: int, high: int, roll: float) -> int:
    """One of the tier's whole numbers, each equally likely.

    `UCataclysmItemValues::EnchantmentValue`'s rule at a precision of one:
    the roll in [0, 1] is cut into `high - low + 1` equal shares.
    """
    steps = high - low
    index = min(math.floor(min(max(roll, 0.0), 1.0) * (steps + 1)), steps)
    return low + index


def value(affix: ProposedAffix, tier: int, roll: float = 1.0,
          gear_level: int = MAX_GEAR_LEVEL, two_handed: bool = False,
          seventy_percent: bool = False) -> int:
    """What a rolled affix grants under the proposal: always a whole number.

    `seventy_percent` is NOT the proposal. It is today's hybrid rule -- each
    half at 70% of its own affix -- carried over with a rounding step, kept
    only so `seventy_percent_collisions` can show why the proposal does not use
    it.
    """
    af = _sim()
    low, high = affix.tiers[tier - 1]
    base = roll_value(low, high, roll)
    multiplier = gear_multiplier(gear_level) if affix.amount else 1.0
    if two_handed:
        multiplier *= af.TWO_HANDED_MULTIPLIER
    if seventy_percent:
        multiplier *= af.HYBRID_FRACTION
    result = round_half_up(base * multiplier)
    return max(result, 1) if seventy_percent else result


def tier_range(affix: ProposedAffix, tier: int, gear_level: int = 0,
               two_handed: bool = False,
               seventy_percent: bool = False) -> tuple[int, int]:
    return (value(affix, tier, 0.0, gear_level, two_handed, seventy_percent),
            value(affix, tier, 1.0, gear_level, two_handed, seventy_percent))


#: A HYBRID AT TIER N GRANTS EACH HALF AS ITS OWN AFFIX AT TIER N - 2.
#:
#: Two tiers down on today's ladder is 3^(-2/6) = 0.693 of the value: the
#: design's 70% hybrid share, within one per cent, read off a ladder rather than
#: multiplied. Multiplying a whole-number ladder by 0.7 is what this replaces,
#: because two whole numbers one apart are 0.7 apart afterwards and round
#: together: `seventy_percent_collisions` counts where.
#:
#: SO A HYBRID ROLLS TIERS 3 TO 7 ONLY, and its tier 3 grants both halves at
#: their tier 1 ranges. Every hybrid range is then a range of its half's own
#: ladder, whole and separate by construction.
HYBRID_TIER_DROP = 2
HYBRID_TIERS = tuple(t for t in TIERS if t > HYBRID_TIER_DROP)


def hybrid_half(part: ProposedAffix, hybrid_tier: int, roll: float = 1.0,
                gear_level: int = MAX_GEAR_LEVEL, two_handed: bool = False) -> int:
    """One half of a hybrid at a hybrid tier: its own affix two tiers down."""
    if hybrid_tier not in HYBRID_TIERS:
        raise ValueError(f"a hybrid rolls tiers {HYBRID_TIERS[0]} to "
                         f"{HYBRID_TIERS[-1]}, not {hybrid_tier}")
    return value(part, hybrid_tier - HYBRID_TIER_DROP, roll, gear_level, two_handed)


# ---------------------------------------------------------------------------
# Implicits
# ---------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class ProposedImplicit:
    base: str
    slot: str
    stat: str
    kind: str
    old_stated: float          # today's +10 figure
    at_zero: int               # the proposed +0 figure, whole
    two_handed: bool

    @property
    def old_at_zero(self) -> float:
        return self.old_stated / gear_multiplier(MAX_GEAR_LEVEL)

    def at(self, gear_level: int) -> int:
        multiplier = gear_multiplier(gear_level)
        return round_half_up(self.at_zero * multiplier)


def implicit_proposal(rows: list[dict[str, str]] | None = None) -> list[ProposedImplicit]:
    """Every implicit restated as a whole +0 value, at least one."""
    rows = rows if rows is not None else read_base_rows()
    out = []
    for row in rows:
        for n in (1, 2):
            stat = row[f"Implicit{n}Stat"]
            if not stat:
                continue
            stated = float(row[f"Implicit{n}Value"])
            at_zero = max(1, round_half_up(stated / gear_multiplier(MAX_GEAR_LEVEL)))
            out.append(ProposedImplicit(row["BaseName"], row["Slot"], stat,
                                        row[f"Implicit{n}Kind"], stated, at_zero,
                                        row["Hands"] == "2"))
    return out


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def problems(table: dict[str, ProposedAffix] | None = None,
             rows: list[dict[str, str]] | None = None) -> list[str]:
    """Everything wrong with the proposal. Empty when it holds.

    1. Every tier is whole, holds at least two values, and starts one above the
       tier below it ends: contiguous, never overlapping.
    2. At every upgrade level, on a two-handed weapon or not, every tier's best
       roll is below the next tier's worst. The rounding cannot merge them
       because the multiplier is at least one, and this checks it rather than
       trusting that.
    3. A hybrid's slots sit inside both halves' slots, and are not empty.
    4. Every slot can still fill two prefixes and two suffixes.
    5. A retired affix names a live one to fold into.
    """
    table = table if table is not None else proposal(rows)
    rows = rows if rows is not None else read_affix_rows()
    out = []
    for affix in table.values():
        if affix.kind == "Hybrid":
            for part in affix.hybrid_parts or ():
                if part not in table or table[part].retired:
                    out.append(f"{affix.name}: half {part!r} is not a live affix")
                elif not set(affix.slots) <= set(table[part].slots):
                    out.append(f"{affix.name}: rolls where {part} cannot")
            if not affix.slots:
                out.append(f"{affix.name}: can roll on no slot")
            continue
        if affix.retired:
            target = table.get(affix.retired_into or "")
            if target is None or target.retired:
                out.append(f"{affix.name}: retired into {affix.retired_into!r}, "
                           "which is not a live affix")
            continue
        if len(affix.tiers) != len(TIERS):
            out.append(f"{affix.name}: {len(affix.tiers)} tiers")
            continue
        for k, (low, high) in enumerate(affix.tiers, start=1):
            if not (isinstance(low, int) and isinstance(high, int)):
                out.append(f"{affix.name} T{k}: not whole")
            if high - low + 1 < MIN_VALUES_PER_TIER:
                out.append(f"{affix.name} T{k}: {low}-{high} holds one value")
            if k > 1 and low != affix.tiers[k - 2][1] + 1:
                out.append(f"{affix.name} T{k}: starts at {low}, the tier "
                           f"below ends at {affix.tiers[k - 2][1]}")
        for level in range(MAX_GEAR_LEVEL + 1):
            for two_handed in (False, True):
                for k in TIERS[:-1]:
                    best = tier_range(affix, k, level, two_handed)[1]
                    worst_above = tier_range(affix, k + 1, level, two_handed)[0]
                    if best >= worst_above:
                        out.append(f"{affix.name}: T{k} reaches T{k + 1} at "
                                   f"+{level}{' two-handed' if two_handed else ''}")
    out.extend(_pool_problems(table, rows))
    return out


def _pool_problems(table: dict[str, ProposedAffix],
                   rows: list[dict[str, str]]) -> list[str]:
    af = _sim()
    position = {r["AffixName"]: r["Position"] for r in rows}
    out = []
    for slot in af.GEAR_SLOTS:
        for pos, need in (("prefix", af.PREFIXES_PER_PIECE),
                          ("suffix", af.SUFFIXES_PER_PIECE)):
            live = [a for a in table.values()
                    if not a.retired and position[a.name] == pos and slot in a.slots]
            if len(live) < need:
                out.append(f"{slot}: only {len(live)} {pos}es")
    return out


def seventy_percent_collisions(table: dict[str, ProposedAffix] | None = None
                               ) -> list[str]:
    """Where today's hybrid rule, 70% then rounded, would break the new rules.

    THE EVIDENCE FOR `HYBRID_TIER_DROP`, not a check on the proposal. Two whole
    numbers a step apart are 0.7 apart after the multiplier and can round
    together, so adjacent tiers share a value and a two-value tier becomes one
    value. Listed at +0, which is what a drop shows.
    """
    table = table if table is not None else proposal()
    out = []
    for hybrid in table.values():
        if hybrid.kind != "Hybrid":
            continue
        for part_name in hybrid.hybrid_parts or ():
            part = table[part_name]
            for k in TIERS:
                low, high = tier_range(part, k, 0, seventy_percent=True)
                if low == high:
                    out.append(f"{hybrid.name} / {part_name}: T{k} is the single "
                               f"value {low} at +0")
                if k < TIERS[-1]:
                    worst_above = tier_range(part, k + 1, 0, seventy_percent=True)[0]
                    if high >= worst_above:
                        out.append(f"{hybrid.name} / {part_name}: T{k} and "
                                   f"T{k + 1} share {high} at +0")
    return out


def hybrid_shares(table: dict[str, ProposedAffix] | None = None
                  ) -> dict[str, tuple[float, float]]:
    """Each hybrid half's best roll at hybrid tier 7 against its own affix's best
    tier 7 roll, on a +10 piece: (that share, the share at hybrid tier 3 against
    tier 3). The design's figure is 0.70."""
    table = table if table is not None else proposal()
    out = {}
    for hybrid in table.values():
        if hybrid.kind != "Hybrid":
            continue
        for part_name in hybrid.hybrid_parts or ():
            part = table[part_name]
            top = hybrid_half(part, 7) / value(part, 7)
            low = hybrid_half(part, 3) / value(part, 3)
            out[f"{hybrid.name} / {part_name}"] = (round(top, 2), round(low, 2))
    return out


# ---------------------------------------------------------------------------
# What it does to the design's rules, measured in the simulation
# ---------------------------------------------------------------------------

@contextmanager
def simulated(table: dict[str, ProposedAffix] | None = None):
    """Run the simulation with every affix and implicit valued by the proposal.

    Patches the four `value_at`-shaped methods the simulation reads values
    through, and restores them afterwards whatever happens. A retired affix is
    worth nothing, which is what an item still carrying one would grant.
    """
    af = _sim()
    table = table if table is not None else proposal()
    saved = (af.StatAffix.value_at, af.AffixFamily.value_at,
             af.Implicit.value_at, af.AilmentAffix.chance_at,
             af.HybridAffix.value_of)

    def stat_value(self, tier, roll=1.0, gear_level=MAX_GEAR_LEVEL, two_handed=False):
        affix = table[self.name]
        return 0.0 if affix.retired else float(value(affix, tier, roll, gear_level,
                                                     two_handed))

    def family_value(self, tier, roll=1.0, gear_level=MAX_GEAR_LEVEL):
        return float(value(table[self.name], tier, roll, gear_level))

    def ailment_chance(self, tier, roll=1.0, gear_level=MAX_GEAR_LEVEL, two_handed=False):
        return float(value(table[self.name], tier, roll, gear_level, two_handed))

    def implicit_value(self, gear_level=MAX_GEAR_LEVEL, two_handed=False):
        at_zero = max(1, round_half_up(self.value / gear_multiplier(MAX_GEAR_LEVEL)))
        multiplier = gear_multiplier(gear_level) * (af.TWO_HANDED_MULTIPLIER
                                                    if two_handed else 1.0)
        return float(round_half_up(at_zero * multiplier))

    def hybrid_value(self, part, tier=7, roll=1.0, gear_level=MAX_GEAR_LEVEL):
        affix = table[part.name]
        if affix.retired:
            affix = table[affix.retired_into]
        if tier not in HYBRID_TIERS:
            return 0.0
        return float(hybrid_half(affix, tier, roll, gear_level))

    af.StatAffix.value_at = stat_value
    af.AffixFamily.value_at = family_value
    af.Implicit.value_at = implicit_value
    af.AilmentAffix.chance_at = ailment_chance
    af.HybridAffix.value_of = hybrid_value
    try:
        yield af
    finally:
        (af.StatAffix.value_at, af.AffixFamily.value_at,
         af.Implicit.value_at, af.AilmentAffix.chance_at,
         af.HybridAffix.value_of) = saved


def measure() -> dict[str, dict]:
    """The design figures the proposal moves, today against proposed.

    Each entry is computed twice through the same simulation code, once as it
    stands and once inside `simulated()`, so the comparison is the simulation's
    own arithmetic and not a second copy of it.
    """
    af = _sim()
    from cataclysm_sim import player_damage as pd
    from cataclysm_sim import reference_build as rb

    def figures():
        hero = rb.gear(7, MAX_GEAR_LEVEL)
        hero1 = rb.gear(pd.affix_tier_at(1), pd.gear_level_at(1))
        return {
            "damage per hit by difficulty tier": [round(pd.damage_per_hit(t))
                                                  for t in range(1, 9)],
            "damage per hit, reference spending 4 flat damage affixes": [
                round(pd.damage_per_hit(t, flat_affixes=4)) for t in range(1, 9)],
            "damage per hit, reference spending 3 flat damage affixes": [
                round(pd.damage_per_hit(t, flat_affixes=3)) for t in range(1, 9)],
            "damage target at tier 8": round(af.damage_target(8)),
            "health crossover base (flat vs increased, +200% increases)":
                round(af.crossover_base(af.HEALTH_AFFIXES, 2.0)),
            "damage crossover base (8 increased in place)":
                round(af.crossover_base(af.DAMAGE_AFFIXES, 8 * 1.25)),
            "weapon base the reference build needs":
                round(af.reference_weapon_base(), 1),
            "slots to cap all eight at tier 8, all-resistance family":
                round(af.slots_to_cap(af.ALL_RESISTANCE, 8, 8), 1),
            "reference build at tier 8, flat stats from gear": {
                k: round(v, 1) for k, v in sorted(hero.flat.items())
                if not k.startswith("resistance_") or k == "resistance_war"},
            "reference build at tier 8, increases from gear": {
                k: round(v * 100) for k, v in sorted(hero.increased.items())},
            "reference build at tier 1, flat stats from gear": {
                k: round(v, 1) for k, v in sorted(hero1.flat.items())
                if not k.startswith("resistance_") or k == "resistance_war"},
            "reference build at tier 1, increases from gear": {
                k: round(v * 100) for k, v in sorted(hero1.increased.items())},
        }

    before = figures()
    table = proposal()
    with simulated(table):
        after = figures()
    out = {key: {"today": before[key], "proposed": after[key]} for key in before}

    # THE OTHER ANSWER FOR FLAT DAMAGE, decision D3: keep its top at 22 and let
    # it ignore upgrades like a percentage, which leaves room for seven tiers.
    tier_only = dict(table)
    tier_only["Flat damage"] = dataclasses.replace(
        table["Flat damage"], amount=False, tiers=ladder(3, 22))
    with simulated(tier_only):
        out["damage per hit, flat damage 3-22 and not upgraded (D3 option b)"] = {
            "today": before["damage per hit by difficulty tier"],
            "proposed": [round(pd.damage_per_hit(t)) for t in range(1, 9)]}
    return out


# ---------------------------------------------------------------------------
# Applying it, to a copy
# ---------------------------------------------------------------------------

TIER_COLUMNS = tuple(f"T{t} {end}" for t in TIERS for end in ("Min", "Max"))
SCALES_COLUMN = "Scales With Upgrade"
IMPLICIT_COLUMNS = ("Implicit 1 At +0", "Implicit 2 At +0")


class RealWorkbookRefused(RuntimeError):
    pass


def apply(source: pathlib.Path, destination: pathlib.Path, *,
          allow_real_workbook: bool = False) -> dict[str, int]:
    """Write the proposal into `destination`, a copy of `source`.

    What it writes:
      Affixes      fourteen columns T1 Min .. T7 Max and "Scales With Upgrade"
                   on every live row; the narrowed Allowed Slots; a hybrid's
                   renamed half and narrowed slots; and deletes a retired row.
      Item Bases   "Implicit 1 At +0" and "Implicit 2 At +0", whole numbers.

    What it leaves alone: Top Value and Floor, which the game reads today and
    the implementation will retire; every other sheet, byte for byte in value.

    REFUSES THE REAL WORKBOOK unless `allow_real_workbook` is set, which is for
    the day the owner has ruled and the change is being made on purpose.
    """
    import openpyxl

    if destination.resolve() == WORKBOOK.resolve() and not allow_real_workbook:
        raise RealWorkbookRefused(
            "this would write the real design workbook; other branches hold "
            "unmerged edits to it and the owner has not ruled")
    if source.resolve() != destination.resolve():
        shutil.copyfile(source, destination)

    table = proposal()
    book = openpyxl.load_workbook(destination)
    counts = {"tier rows": 0, "slot lists": 0, "hybrid halves": 0,
              "retired rows": 0, "implicits": 0}

    sheet = book["Affixes"]
    headers = [c.value for c in sheet[1]]
    column = {h: i + 1 for i, h in enumerate(headers) if h}
    for title in (*TIER_COLUMNS, SCALES_COLUMN):
        if title not in column:
            column[title] = sheet.max_column + 1
            sheet.cell(row=1, column=column[title], value=title)

    retired_rows = []
    for r in range(2, sheet.max_row + 1):
        name = sheet.cell(row=r, column=column["Affix Name"]).value
        if not name or name not in table:
            continue
        affix = table[name]
        if affix.retired:
            retired_rows.append(r)
            continue
        if affix.slots != affix.old_slots:
            sheet.cell(row=r, column=column["Allowed Slots"],
                       value=", ".join(sorted(affix.slots)))
            counts["slot lists"] += 1
        if affix.kind == "Hybrid":
            for n, part in enumerate(affix.hybrid_parts or (), start=1):
                cell = sheet.cell(row=r, column=column[f"Hybrid Part {n}"])
                if cell.value != part:
                    cell.value = part
                    counts["hybrid halves"] += 1
            continue
        for t, (low, high) in zip(TIERS, affix.tiers, strict=True):
            sheet.cell(row=r, column=column[f"T{t} Min"], value=low)
            sheet.cell(row=r, column=column[f"T{t} Max"], value=high)
        sheet.cell(row=r, column=column[SCALES_COLUMN],
                   value="yes" if affix.amount else "no")
        counts["tier rows"] += 1

    for r in reversed(retired_rows):
        sheet.delete_rows(r)
        counts["retired rows"] += 1

    bases = book["Item Bases"]
    headers = [c.value for c in bases[1]]
    bcol = {h: i + 1 for i, h in enumerate(headers) if h}
    for title in IMPLICIT_COLUMNS:
        if title not in bcol:
            bcol[title] = bases.max_column + 1
            bases.cell(row=1, column=bcol[title], value=title)
    by_base = {}
    for implicit in implicit_proposal():
        by_base.setdefault(implicit.base, []).append(implicit)
    for r in range(2, bases.max_row + 1):
        name = bases.cell(row=r, column=bcol["Base Name"]).value
        for n, implicit in enumerate(by_base.get(name, ()), start=1):
            bases.cell(row=r, column=bcol[f"Implicit {n} At +0"],
                       value=implicit.at_zero)
            counts["implicits"] += 1

    book.save(destination)
    return counts


# ---------------------------------------------------------------------------
# Printing
# ---------------------------------------------------------------------------

def _num(v: float) -> str:
    return str(int(round(v))) if _whole(v) else f"{v:.2f}".rstrip("0").rstrip(".")


def whole_numbers_inside(low: float, high: float) -> int:
    """How many whole numbers a band contains: what a whole-number display of it
    could show at the most."""
    return max(0, math.floor(high + 1e-9) - math.ceil(low - 1e-9) + 1)


def inventory_markdown() -> str:
    """Every affix, every tier, at +0 (what a drop shows) and tier 7 at +10.

    A band is continuous today, so no tier is a single value; the column that
    says how coarse a tier is counts the whole numbers it contains, which is
    the most a whole-number display could ever show of it.
    """
    lines = ["| Affix | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T7 at +10 | decimals "
             "| overlapping pairs | narrowest width | fewest whole numbers in a tier |",
             "| :-- | --: | --: | --: | --: | --: | --: | --: | --: | :-: | --: | --: | --: |"]
    for a in today():
        cells = " | ".join(f"{_num(round(t.low0, 2))}-{_num(round(t.high0, 2))}"
                           for t in a.tiers)
        t7 = a.tiers[-1]
        narrowest = min(t.high0 - t.low0 for t in a.tiers)
        fewest = min(whole_numbers_inside(t.low0, t.high0) for t in a.tiers)
        lines.append(
            f"| {a.name} | {cells} | {_num(round(t7.low10, 2))}-{_num(t7.high10)} | "
            f"{'yes' if a.has_decimal else 'no'} | {a.overlaps} of 6 | "
            f"{_num(round(narrowest, 2))} | {fewest} |")
    return "\n".join(lines)


def table_markdown() -> str:
    table = proposal()
    rows = {r["AffixName"]: r for r in read_affix_rows()}
    lines = ["| Affix | Upgrades? | T1 | T2 | T3 | T4 | T5 | T6 | T7 | Top at +10 "
             "(was) | Pieces (was) | If upgrades multiplied every affix | Change |",
             "| :-- | :-: | --: | --: | --: | --: | --: | --: | --: | --: | --: | --: "
             "| :-- |"]
    for affix in table.values():
        if affix.kind == "Hybrid":
            continue
        alternative = top_if_every_affix_scaled(rows[affix.name])
        if affix.retired:
            lines.append(f"| {affix.name} | | retired into {affix.retired_into} "
                         f"| | | | | | | -- ({_num(affix.old_top)}) | "
                         f"-- ({affix.old_pieces()}) | {alternative} | {affix.note} |")
            continue
        cells = " | ".join(f"{lo}-{hi}" for lo, hi in affix.tiers)
        pieces = (f"{affix.pieces()} ({affix.old_pieces()})"
                  if affix.slots != affix.old_slots else str(affix.pieces()))
        change = affix.note or ("" if not affix.raised else "raised to make room")
        lines.append(f"| {affix.name} | {'yes' if affix.amount else 'no'} | {cells} | "
                     f"{affix.top} ({_num(affix.old_top)}) | {pieces} | {alternative} "
                     f"| {change} |")
    return "\n".join(lines)


def hybrid_markdown() -> str:
    table = proposal()
    shares = hybrid_shares(table)
    lines = ["| Hybrid | Halves | Hybrid T3 at +0 | Hybrid T7 at +0 | Hybrid T7 at +10 "
             "| Share of the single affix at T7 | Slots (was) |",
             "| :-- | :-- | --: | --: | --: | --: | :-- |"]
    for hybrid in table.values():
        if hybrid.kind != "Hybrid":
            continue
        a, b = (table[p] for p in hybrid.hybrid_parts or ())

        def span(part, tier, level):
            return (f"{hybrid_half(part, tier, 0.0, level)}-"
                    f"{hybrid_half(part, tier, 1.0, level)}")
        slots = ", ".join(hybrid.slots)
        if hybrid.slots != hybrid.old_slots:
            slots += f" ({', '.join(hybrid.old_slots)})"
        share = "; ".join(f"{shares[f'{hybrid.name} / {p.name}'][0]:.2f}" for p in (a, b))
        lines.append(
            f"| {hybrid.name} | {a.name}; {b.name} | {span(a, 3, 0)}; {span(b, 3, 0)} | "
            f"{span(a, 7, 0)}; {span(b, 7, 0)} | {span(a, 7, 10)}; {span(b, 7, 10)} | "
            f"{share} | {slots} |")
    return "\n".join(lines)


def implicit_markdown() -> str:
    lines = ["| Base | Implicit | Today +0 | Today +10 | Proposed +0 | Proposed +10 |",
             "| :-- | :-- | --: | --: | --: | --: |"]
    for imp in implicit_proposal():
        two = " (x2 two-handed)" if imp.two_handed else ""
        lines.append(f"| {imp.base}{two} | {imp.stat} {imp.kind} | "
                     f"{_num(round(imp.old_at_zero, 2))} | {_num(imp.old_stated)} | "
                     f"{imp.at_zero} | {imp.at(MAX_GEAR_LEVEL)} |")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("inventory", help="today's tiers, as markdown")
    sub.add_parser("table", help="the proposed tiers, as markdown")
    sub.add_parser("measure", help="the design figures before and after")
    sub.add_parser("check", help="the proposal's invariants")
    do = sub.add_parser("apply", help="write the proposal into a COPY")
    do.add_argument("--source", type=pathlib.Path, default=WORKBOOK)
    do.add_argument("--out", type=pathlib.Path, required=True)
    do.add_argument("--i-mean-the-real-workbook", action="store_true",
                    help="allow --out to be the real workbook; only after the "
                         "owner has ruled")
    args = parser.parse_args(argv)

    if args.command == "inventory":
        print(inventory_markdown())
    elif args.command == "table":
        print(table_markdown())
        print()
        print(hybrid_markdown())
        print()
        print(implicit_markdown())
    elif args.command == "measure":
        for key, pair in measure().items():
            print(f"{key}:\n  today    {pair['today']}\n  proposed {pair['proposed']}")
    elif args.command == "check":
        found = problems()
        for line in found:
            print(line)
        collisions = seventy_percent_collisions()
        print(f"{len(found)} problems.")
        print(f"Not the proposal: today's 70% hybrid rule, rounded, would break the "
              f"new rules in {len(collisions)} places at +0:")
        for line in collisions:
            print("  " + line)
        return 1 if found else 0
    elif args.command == "apply":
        try:
            counts = apply(args.source, args.out,
                           allow_real_workbook=args.i_mean_the_real_workbook)
        except RealWorkbookRefused as error:
            print(f"refused: {error}", file=sys.stderr)
            return 2
        print(f"wrote {args.out}: " + ", ".join(f"{v} {k}" for k, v in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
