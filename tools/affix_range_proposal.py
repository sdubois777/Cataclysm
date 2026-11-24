"""A PROPOSAL, NOT A CHANGE: every affix tier as a stated whole-number range.

The project owner, 2026-11-24:

    "take a look at the values and ranges of affixes. Some of them feel
    arbitrarily low. Like 1.2% and stuff like that. I really hate decimals, and
    they should be removed in all cases, but also, each affix tier should have
    it's own range, and that range should be something a PLAYER finds
    reasonable. Not math."

And the owner's rulings on the first version of this proposal, issue #2883,
2026-11-24:

    upgrades   "upgrade everything, once we've fixed the ranges and values, we
               can look at rebalancing enemies to fit."
    overlap    "No overlap (Recommended)"
    small      "Fewer slots (Recommended)"
    damage     "It's fine, keep the 6 slots and upgrade the damage. We'll fix it
               with the enemy balance audit like for the first question."

This module does five things and changes nothing in the game:

  inventory   every affix tier as the game computes it today, from
              `game/Data/Affixes.csv` and the simulation's own `affix_value`
  table       the proposed seven whole-number ranges per affix at +0 and +10,
              hybrids and implicits
  levels      what a player sees at every upgrade level, +0 to +10, for every
              distinct ladder in the table
  measure     the design's derived figures before and after, through the
              simulation's own functions
  check       the proposal's invariants at every upgrade level
  apply       writes the table into a COPY of the design workbook, and refuses
              the real one unless told otherwise in so many words

Run from the repository root:

    python tools/affix_range_proposal.py inventory
    python tools/affix_range_proposal.py table
    python tools/affix_range_proposal.py levels
    python tools/affix_range_proposal.py measure
    python tools/affix_range_proposal.py check
    python tools/affix_range_proposal.py apply --out <a copy>.xlsx

THE RULE. A tier is a range of whole numbers written in the workbook as its +0
value, contiguous with the tiers either side and never overlapping them,
holding at least two values. At an upgrade level each end of the range is
multiplied by 1 + 0.15 x level and rounded to the nearest whole number, a half
going up, always from the +0 end; a two-handed weapon doubles inside that
rounding. A roll is a position in the range, kept when the piece is upgraded,
and the number it gives is one of the whole numbers in the range at the
piece's level, each equally likely, which is the rule enchantments already
roll by. A hybrid at tier N grants each half as that half's own affix at tier
N - 2. Implicits are whole +0 numbers multiplied the same way.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import math
import pathlib
import shutil
import statistics
import sys
from contextlib import contextmanager

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SIM_ROOT = REPO_ROOT / "sim"
AFFIX_CSV = REPO_ROOT / "game" / "Data" / "Affixes.csv"
BASES_CSV = REPO_ROOT / "game" / "Data" / "ItemBases.csv"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

TIERS = (1, 2, 3, 4, 5, 6, 7)
MAX_GEAR_LEVEL = 10
LEVELS = tuple(range(MAX_GEAR_LEVEL + 1))

#: How many whole values a tier must hold at the least. Two is the smallest
#: number that leaves the reroll and perfect crafting materials something to do.
MIN_VALUES_PER_TIER = 2

#: The smallest seven-tier ladder that can exist: seven tiers of two values.
MIN_LADDER_VALUES = len(TIERS) * MIN_VALUES_PER_TIER

#: Where tier 7 starts, as a share of the top: where today's top band starts.
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
    become 52. The design states the half-up rule. Only for figures that are
    not upgrade products; those go through `upgraded`, which is exact.
    """
    if value >= 0:
        return math.floor(value + 0.5)
    return -math.floor(-value + 0.5)


# ---------------------------------------------------------------------------
# The upgrade step, in whole numbers
# ---------------------------------------------------------------------------

def level_hundredths(gear_level: int) -> int:
    """An upgrade level's multiplier in hundredths: +0 is 100, +10 is 250.

    READ FROM THE SIMULATION'S `GEAR_LEVEL_FACTOR` AND REFUSED IF IT IS NOT A
    WHOLE NUMBER OF HUNDREDTHS, because the rounding below is exact only in
    whole hundredths. Floating point is not: 10 x 1.15 is 11.5 on paper and
    11.499999999999998 in a double, which rounds the wrong way. Any engine
    implementation must do this in integers for the same reason.
    """
    af = _sim()
    step = af.GEAR_LEVEL_FACTOR * 100
    if abs(step - round(step)) > 1e-9:
        raise ValueError(f"GEAR_LEVEL_FACTOR {af.GEAR_LEVEL_FACTOR} is not whole "
                         "hundredths, so the upgrade rounding is not exact")
    if not 0 <= gear_level <= MAX_GEAR_LEVEL:
        raise ValueError(f"gear level {gear_level} outside 0-{MAX_GEAR_LEVEL}")
    return 100 + int(round(step)) * gear_level


def upgraded(base: int, gear_level: int, two_handed: bool = False) -> int:
    """What a +0 whole number shows at an upgrade level: rounded once, half up.

    Always from the +0 number, never from the level below, so rounding never
    compounds. A two-handed weapon doubles inside the rounding.
    """
    af = _sim()
    hands = int(af.TWO_HANDED_MULTIPLIER) if two_handed else 1
    scaled = base * level_hundredths(gear_level) * hands
    return (scaled + 50) // 100


def gear_multiplier(gear_level: int) -> float:
    return level_hundredths(gear_level) / 100.0


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


def number_kind(row: dict[str, str]) -> str:
    """What the affix's number measures: an "amount" (a Stat row that is flat
    and whose Percent column says no), an "increase", or percentage "points"
    (every other row: flat percentages, resistances, ailment chances)."""
    if row["AffixKind"] == "Stat" and row["ValueKind"] == "increased":
        return "increase"
    if row["AffixKind"] == "Stat" and row["ValueKind"] == "flat" and not is_percent(row):
        return "amount"
    return "points"


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
        out.append(TodayAffix(row["AffixName"], row["AffixKind"], top, floor, tiers))
    return out


# ---------------------------------------------------------------------------
# The proposal
# ---------------------------------------------------------------------------

#: The least a tier 1 roll may be at +0, by what the number measures. A
#: judgement (decision D7), from the genre's own lowest tiers (Path of Exile:
#: increased attack speed 5-7, all resistances 3-5, item rarity 6-10) and from
#: the floors the owner approved on 2026-09-04 (resistances 10, 7 and 3; leech,
#: damage reduction, penetration and evasion 1).
MIN_START = {"increase": 3, "points": 1, "amount": 2}


@dataclasses.dataclass(frozen=True)
class Override:
    """A stat whose scale or slots the rule alone cannot settle."""

    top: int | None = None              # a new top at +10
    slots: tuple[str, ...] | None = None
    retired_into: str | None = None
    why: str = ""


#: Each one is a judgement, and the proposal document says why. THE SLOT LISTS
#: ARE THE OWNER'S RULING "Fewer slots" (issue #2883): the top rises to what
#: seven whole ranges need, and the slot list narrows to the piece count whose
#: ceiling is nearest today's, rounding down where a cap is close. The pieces
#: chosen keep every hybrid that uses one of these stats able to roll somewhere.
OVERRIDES: dict[str, Override] = {
    # Retired (D5). A flat crit chance affix would be 33-35 points at +10;
    # every skill has a 5% base and four implicits add flat crit, so the
    # increase is the natural affix, and the one Path of Exile uses on gear.
    "Flat critical strike chance": Override(
        retired_into="Increased critical strike chance",
        why="retired: as whole numbers it would be 33-35 points of crit a piece "
            "at +10; folded into the increase, which reads the skill's base"),
    "Increased critical strike chance": Override(
        top=100,
        why="absorbs the flat affix: 100% increased is 5 points at the 5% "
            "default base, the old flat affix's top"),
    "Flat mana regeneration": Override(
        retired_into="Increased mana regeneration",
        why="retired: 0.65 a second would have to become 35, a twentieth of a "
            "level 100 pool a second; every class has a base for the increase"),
    "Flat evasion": Override(
        slots=("Chest",),
        why="one piece (35) instead of fourteen (56): two would be 70, past "
            "evasion's soft cap of 60 before implicits"),
    "Flat block chance": Override(
        slots=("Shoulders", "Belt"),
        why="two pieces instead of fourteen: 70 either way"),
    "Flat damage reduction": Override(
        slots=("Pants",),
        why="one piece (35) instead of fourteen (56): two would be 70 against "
            "a cap of 75, from gear alone"),
    "Flat retaliation": Override(
        slots=("Head", "Shoulders"),
        why="two pieces (70) instead of fourteen (56)"),
    "Flat crowd control resistance": Override(
        slots=("Belt", "Boots"),
        why="two pieces instead of fourteen: 70 either way"),
    "Flat life leech": Override(
        slots=("Weapon",),
        why="the weapon only: 35, or 70 two-handed or with an offhand, "
            "instead of twelve pieces (48)"),
    "Flat mana leech": Override(slots=("Weapon",), why="as life leech"),
    "Flat energy shield leech": Override(slots=("Weapon",), why="as life leech"),
    "Flat penetration": Override(
        slots=("Weapon",),
        why="the weapon only: 35 is the most any enemy resists, so more is "
            "wasted; 70 two-handed or with an offhand"),
    "Flat maximum class resource": Override(
        slots=("Necklace", "Relic"),
        why="two pieces (76) instead of eleven (77)"),
    "Flat health regeneration": Override(
        slots=("Chest", "Belt", "Necklace"),
        why="three pieces (114) instead of twelve (96)"),
    "Flat energy shield regeneration": Override(
        slots=("Head", "Chest", "Shoulders"),
        why="three pieces (114) instead of twelve (120)"),
}


@dataclasses.dataclass(frozen=True)
class ProposedAffix:
    name: str
    row_name: str
    kind: str
    number: str                          # "amount", "increase" or "points"
    tiers: tuple[tuple[int, int], ...]   # seven (low, high) at +0
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
        return upgraded(self.tiers[-1][1], MAX_GEAR_LEVEL)

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
    """Where tier 1 starts at +0: today's worst roll, lifted to the minimum.

    Today's worst roll is the stated floor where there is one, and otherwise a
    tenth of the top (tier 1's share, the bottom of the band, and a +0 piece).
    """
    af = _sim()
    floor = float(row["Floor"] or 0.0)
    today_worst = max(top * af.WORST_MULTIPLIER, floor)
    return max(round_half_up(today_worst), MIN_START[number_kind(row)])


def propose_row(row: dict[str, str]) -> ProposedAffix:
    """One non-hybrid affix under the rule."""
    override = OVERRIDES.get(row["AffixName"], Override())
    old_top = float(row["TopValue"])
    top = float(override.top) if override.top is not None else old_top
    start = _start(row, top)
    high = round_half_up(top / gear_multiplier(MAX_GEAR_LEVEL))
    note = override.why
    if high - start + 1 < MIN_LADDER_VALUES:
        high = start + MIN_LADDER_VALUES - 1
        note = note or "raised: the smallest top that holds seven two-value tiers"
    old_slots = slots_of(row["AllowedSlots"])
    return ProposedAffix(
        name=row["AffixName"], row_name=row["Name"], kind=row["AffixKind"],
        number=number_kind(row), tiers=ladder(start, high), old_top=old_top,
        slots=override.slots or old_slots, old_slots=old_slots,
        retired_into=override.retired_into, note=note)


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
        parts = [out[p].retired_into or p
                 for p in (row["HybridPart1"], row["HybridPart2"])]
        allowed = set(slots_of(row["AllowedSlots"]))
        for part in parts:
            allowed &= set(out[part].slots)
        old_slots = slots_of(row["AllowedSlots"])
        slots = tuple(s for s in old_slots if s in allowed)
        renamed = tuple(parts) != (row["HybridPart1"], row["HybridPart2"])
        out[row["AffixName"]] = ProposedAffix(
            name=row["AffixName"], row_name=row["Name"], kind="Hybrid",
            number="", tiers=(), old_top=0.0, slots=slots,
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


def tier_range(affix: ProposedAffix, tier: int, gear_level: int = 0,
               two_handed: bool = False) -> tuple[int, int]:
    """A tier's range at an upgrade level: each end of the +0 range upgraded.

    Because the multiplier is never below one, two +0 numbers one apart stay at
    least one apart after it, so the tier above always starts above this one
    ends, at every level.
    """
    low, high = affix.tiers[tier - 1]
    return (upgraded(low, gear_level, two_handed),
            upgraded(high, gear_level, two_handed))


def value(affix: ProposedAffix, tier: int, roll: float = 1.0,
          gear_level: int = MAX_GEAR_LEVEL, two_handed: bool = False) -> int:
    """What a rolled affix grants: a whole number inside its tier's range at
    the piece's upgrade level.

    THE ROLL IS A POSITION, NOT A +0 NUMBER. An item stores its roll in [0, 1]
    as it does today, and the number is that position among the whole numbers
    of the tier's range at the item's level, each equally likely. So every
    number a tooltip's range shows can actually roll; multiplying a +0 number
    instead would leave gaps (at +10, a 1-2 tier would show "3-5" and never 4).
    Upgrading never lowers a roll: both ends rise and the position is kept.
    A perfect roll is the top of the range and the worst roll its bottom.
    """
    low, high = tier_range(affix, tier, gear_level, two_handed)
    return roll_value(low, high, roll)


def shown(affix: ProposedAffix, tier: int, gear_level: int,
          two_handed: bool = False) -> list[int]:
    """Every number a tier can show at an upgrade level."""
    low, high = tier_range(affix, tier, gear_level, two_handed)
    return list(range(low, high + 1))


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
#: ladder, whole and separate at every upgrade level by construction.
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
        return upgraded(self.at_zero, gear_level)


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

    1. At +0 every tier is whole, holds at least two values, and starts one
       above the tier below it ends.
    2. AT EVERY UPGRADE LEVEL, +0 TO +10, ON A TWO-HANDED WEAPON OR NOT, every
       number the affix can show is whole, no number appears twice (neither
       inside a tier nor in two tiers), every tier shows at least two numbers,
       and every tier sits wholly above the one below.
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
        for level in LEVELS:
            for two_handed in (False, True):
                where = f"+{level}{' two-handed' if two_handed else ''}"
                every = []
                below = None
                for k in TIERS:
                    numbers = shown(affix, k, level, two_handed)
                    if any(not isinstance(n, int) for n in numbers):
                        out.append(f"{affix.name} T{k} at {where}: not whole")
                    if len(set(numbers)) < MIN_VALUES_PER_TIER:
                        out.append(f"{affix.name} T{k} at {where}: shows one number")
                    if below is not None and min(numbers) <= below:
                        out.append(f"{affix.name}: T{k} reaches T{k - 1} at {where}")
                    below = max(numbers)
                    every.extend(numbers)
                if len(every) != len(set(every)):
                    out.append(f"{affix.name}: a number repeats at {where}")
        for k in TIERS:
            for roll in ROLL_SAMPLES:
                seen = [value(affix, k, roll, level) for level in LEVELS]
                if seen != sorted(seen):
                    out.append(f"{affix.name} T{k}: an upgrade lowers a roll of {roll}")
    out.extend(_pool_problems(table, rows))
    return out


#: Roll positions the monotone check samples: both ends, and between them.
ROLL_SAMPLES = (0.0, 0.1, 0.25, 0.333, 0.5, 0.667, 0.75, 0.9, 0.999, 1.0)


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


def unchanged_steps(table: dict[str, ProposedAffix] | None = None
                    ) -> dict[int, list[int]]:
    """Which +0 range ends in the table show the same number at two adjacent
    upgrade levels, and at which steps.

    NOT A FAULT OF THE RULE, a property of rounding: one upgrade adds 0.15 of
    the +0 number, which is under one for any number below seven, so a small
    range end moves only every second or third upgrade. Keyed by +0 number;
    the value lists the levels whose upgrade left it showing the same.
    """
    table = table if table is not None else proposal()
    numbers = sorted({v for a in table.values() if a.tiers and not a.retired
                      for low, high in a.tiers for v in (low, high)})
    out = {}
    for v in numbers:
        flat = [g for g in LEVELS[1:] if upgraded(v, g) == upgraded(v, g - 1)]
        if flat:
            out[v] = flat
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
            seen = []
            for k in TIERS:
                low, high = part.tiers[k - 1]
                shown_here = {max(1, (7 * v + 5) // 10) for v in range(low, high + 1)}
                if len(shown_here) < MIN_VALUES_PER_TIER:
                    out.append(f"{hybrid.name} / {part_name}: T{k} shows one number")
                if seen and min(shown_here) <= max(seen):
                    out.append(f"{hybrid.name} / {part_name}: T{k} meets T{k - 1}")
                seen = sorted(shown_here)
    return out


def hybrid_shares(table: dict[str, ProposedAffix] | None = None
                  ) -> dict[str, tuple[float, float]]:
    """Each hybrid half's best roll at hybrid tier 7 against its own affix's best
    tier 7 roll, on a +10 piece, and the same at hybrid tier 3 against tier 3.
    The design's figure is 0.70."""
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

    Patches the five methods the simulation reads affix values through, and
    restores them afterwards whatever happens. A retired affix is worth
    nothing, which is what an item still carrying one would grant.
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
        return float(upgraded(at_zero, gear_level, two_handed))

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
    own arithmetic and not a second copy of it. The reference build is left as
    it is written -- six flat damage slots, the owner's ruling -- so these are
    the figures the enemy balance audit (issue #2884) starts from.
    """
    af = _sim()
    from cataclysm_sim import player_damage as pd
    from cataclysm_sim import reference_build as rb

    def figures():
        hero = rb.gear(7, MAX_GEAR_LEVEL)
        hero1 = rb.gear(pd.affix_tier_at(1), pd.gear_level_at(1))
        return {
            "damage per hit by difficulty tier (6 flat + 6 increased)": [
                round(pd.damage_per_hit(t)) for t in range(1, 9)],
            "damage target at tier 8": round(af.damage_target(8)),
            "health crossover base (flat vs increased, +200% increases)":
                round(af.crossover_base(af.HEALTH_AFFIXES, 2.0)),
            "damage crossover base (8 increased in place)":
                round(af.crossover_base(af.DAMAGE_AFFIXES, 8 * 1.25)),
            "weapon base the reference build needs":
                round(af.reference_weapon_base(), 1),
            "slots to cap all eight at tier 8, all-resistance family":
                round(af.slots_to_cap(af.ALL_RESISTANCE, 8, 8), 1),
            "slots to cap one at tier 1, single-resistance family":
                round(af.slots_to_cap(af.SINGLE_RESISTANCE, 1, 1), 1),
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
    with simulated(proposal()):
        after = figures()
    return {key: {"today": before[key], "proposed": after[key]} for key in before}


def headline(rows: list[dict[str, str]] | None = None) -> dict[str, object]:
    """The before-and-after counts the report leads with."""
    rows = rows if rows is not None else read_affix_rows()
    table = proposal(rows)
    singles = [a for a in table.values() if a.kind != "Hybrid"]
    live = [a for a in singles if not a.retired]
    raised = [a for a in live if a.raised]
    factors = [a.top / a.old_top for a in raised]
    worst = today(rows)
    return {
        "affixes (not hybrids)": len(singles),
        "retired": len(singles) - len(live),
        "kept at today's top": len(live) - len(raised),
        "raised": len(raised),
        "median raise": round(statistics.median(factors), 2) if factors else 1.0,
        "largest raise": max(((round(a.top / a.old_top, 2), a.name) for a in raised),
                             default=None),
        "narrowed to fewer slots": sum(1 for a in live if a.slots != a.old_slots),
        "today: affixes with a decimal": sum(a.has_decimal for a in worst),
        "today: affixes whose tiers overlap": sum(a.overlaps > 0 for a in worst),
        "today: tier 1 at +0 of the increased maximum health affix": next(
            f"{_num(a.tiers[0].low0)}-{_num(a.tiers[0].high0)}"
            for a in worst if a.name == "Increased maximum health"),
    }


# ---------------------------------------------------------------------------
# Applying it, to a copy
# ---------------------------------------------------------------------------

TIER_COLUMNS = tuple(f"T{t} {end}" for t in TIERS for end in ("Min", "Max"))
IMPLICIT_COLUMNS = ("Implicit 1 At +0", "Implicit 2 At +0")


class RealWorkbookRefused(RuntimeError):
    pass


def apply(source: pathlib.Path, destination: pathlib.Path, *,
          allow_real_workbook: bool = False) -> dict[str, int]:
    """Write the proposal into `destination`, a copy of `source`.

    What it writes:
      Affixes      fourteen columns T1 Min .. T7 Max, the +0 ranges, on every
                   live row; the narrowed Allowed Slots; a hybrid's renamed half
                   and narrowed slots; and deletes a retired row.
      Item Bases   "Implicit 1 At +0" and "Implicit 2 At +0", whole numbers.

    What it leaves alone: Top Value and Floor, which the game reads today and
    the implementation will retire; every other sheet.

    REFUSES THE REAL WORKBOOK unless `allow_real_workbook` is set, which is for
    the day the change is being made on purpose.
    """
    import openpyxl

    if destination.resolve() == WORKBOOK.resolve() and not allow_real_workbook:
        raise RealWorkbookRefused(
            "this would write the real design workbook; other branches hold "
            "unmerged edits to it")
    if source.resolve() != destination.resolve():
        shutil.copyfile(source, destination)

    table = proposal()
    book = openpyxl.load_workbook(destination)
    counts = {"tier rows": 0, "slot lists": 0, "hybrid halves": 0,
              "retired rows": 0, "implicits": 0}

    sheet = book["Affixes"]
    headers = [c.value for c in sheet[1]]
    column = {h: i + 1 for i, h in enumerate(headers) if h}
    for title in TIER_COLUMNS:
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
    """Every affix, every tier, at +0 (what a drop shows) and tier 7 at +10."""
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


def _ladder_key(affix: ProposedAffix) -> str:
    return " ".join(f"{lo}-{hi}" for lo, hi in affix.tiers)


def table_markdown() -> str:
    """The one full table: each tier as its +0 range and its +10 range."""
    table = proposal()
    lines = ["| Affix | T1 | T2 | T3 | T4 | T5 | T6 | T7 | Top at +10 (today) "
             "| Pieces (today) | Ladder | Change |",
             "| :-- | --: | --: | --: | --: | --: | --: | --: | --: | --: | :-: | :-- |"]
    letters = _ladder_letters(table)
    for affix in table.values():
        if affix.kind == "Hybrid":
            continue
        if affix.retired:
            lines.append(f"| {affix.name} | retired into {affix.retired_into} "
                         f"| | | | | | | -- ({_num(affix.old_top)}) | "
                         f"-- ({affix.old_pieces()}) | | {affix.note} |")
            continue
        cells = " | ".join(
            f"{lo}-{hi} / {upgraded(lo, MAX_GEAR_LEVEL)}-{upgraded(hi, MAX_GEAR_LEVEL)}"
            for lo, hi in affix.tiers)
        pieces = (f"{affix.pieces()} ({affix.old_pieces()})"
                  if affix.slots != affix.old_slots else str(affix.pieces()))
        change = affix.note
        if not change and affix.raised:
            change = (f"rounding: {affix.tiers[-1][1]} is the nearest whole +0 top, "
                      f"and shows {affix.top} at +10")
        lines.append(f"| {affix.name} | {cells} | {affix.top} ({_num(affix.old_top)}) "
                     f"| {pieces} | {letters[_ladder_key(affix)]} | {change} |")
    return "\n".join(lines)


def _ladder_letters(table: dict[str, ProposedAffix]) -> dict[str, str]:
    """A letter for each distinct +0 ladder, in table order."""
    letters: dict[str, str] = {}
    for affix in table.values():
        if affix.tiers and not affix.retired:
            key = _ladder_key(affix)
            if key not in letters:
                letters[key] = chr(ord("A") + len(letters))
    return letters


def levels_markdown() -> str:
    """What a player sees at every upgrade level, for every distinct ladder.

    Every affix sharing a ladder shows exactly the same numbers, so one table
    per ladder is every affix at every level.
    """
    table = proposal()
    letters = _ladder_letters(table)
    users: dict[str, list[str]] = {}
    for affix in table.values():
        if affix.tiers and not affix.retired:
            users.setdefault(_ladder_key(affix), []).append(affix.name)
    out = []
    for key, letter in letters.items():
        example = table[users[key][0]]
        out.append(f"#### Ladder {letter}: {', '.join(users[key])}")
        out.append("")
        out.append("| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |")
        out.append("| :-- | --: | --: | --: | --: | --: | --: | --: | --: |")
        for level in LEVELS:
            cells = " | ".join("{}-{}".format(*tier_range(example, k, level))
                               for k in TIERS)
            out.append(f"| +{level} | {level_hundredths(level) / 100:.2f} | {cells} |")
        out.append("")
    return "\n".join(out)


def hybrid_markdown() -> str:
    table = proposal()
    shares = hybrid_shares(table)
    lines = ["| Hybrid | Halves | Hybrid T3 at +0 | Hybrid T7 at +0 | Hybrid T7 at +10 "
             "| Share of the single affix at T7 | Slots (today) |",
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


def implicit_markdown(changed_only: bool = False) -> str:
    lines = ["| Base | Implicit | Today +0 | Today +10 | Proposed +0 | Proposed +10 |",
             "| :-- | :-- | --: | --: | --: | --: |"]
    for imp in implicit_proposal():
        if changed_only and imp.at(MAX_GEAR_LEVEL) == round(imp.old_stated, 6):
            continue
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
    sub.add_parser("levels", help="every ladder at every upgrade level")
    sub.add_parser("measure", help="the design figures before and after")
    sub.add_parser("check", help="the proposal's invariants")
    do = sub.add_parser("apply", help="write the proposal into a COPY")
    do.add_argument("--source", type=pathlib.Path, default=WORKBOOK)
    do.add_argument("--out", type=pathlib.Path, required=True)
    do.add_argument("--i-mean-the-real-workbook", action="store_true",
                    help="allow --out to be the real workbook")
    args = parser.parse_args(argv)

    if args.command == "inventory":
        print(inventory_markdown())
    elif args.command == "table":
        print(table_markdown())
        print()
        print(hybrid_markdown())
        print()
        print(implicit_markdown(changed_only=True))
    elif args.command == "levels":
        print(levels_markdown())
    elif args.command == "measure":
        for key, item in headline().items():
            print(f"{key}: {item}")
        for key, pair in measure().items():
            print(f"{key}:\n  today    {pair['today']}\n  proposed {pair['proposed']}")
    elif args.command == "check":
        found = problems()
        for line in found:
            print(line)
        print(f"{len(found)} problems, at every upgrade level +0 to +10, one- and "
              "two-handed.")
        steps = unchanged_steps()
        print(f"Information: {len(steps)} of the +0 numbers in the table show the "
              "same number at some pair of adjacent upgrade levels (one upgrade "
              "adds 0.15 of a number, under one below 7):")
        for v, flat in steps.items():
            print(f"  {v}: unchanged by the upgrade to +{', +'.join(map(str, flat))}")
        collisions = seventy_percent_collisions()
        print(f"Not the proposal: today's 70% hybrid rule, rounded, would break the "
              f"new rules in {len(collisions)} places at +0.")
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
