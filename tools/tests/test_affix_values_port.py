"""The game's whole-number affix rule is the simulation's, and both read the same ranges.

WHY THIS EXISTS. Issue #2883: the owner approved whole-number affix ranges on 2026-11-24 ("Approve as
written (Recommended)"). The ranges live in three places -- the design workbook, `game/Data/Affixes.csv`
and `ItemBases.csv` (which the game reads), and `sim/cataclysm_sim/affix_ranges.py` (which the
simulation reads) -- and the rule lives in two, `UCataclysmItemValues` in
`game/Source/Cataclysm/Items/CataclysmItem.cpp` and `affix_ranges.py`. Continuous integration compiles
no C++, so this check holds the C++ constants and the data to the simulation. Two copies of a number
are two numbers.

WHAT IS CHECKED:

  1. every non-hybrid row of Affixes.csv states the simulation's seven ranges, and every hybrid row
     states none;
  2. every implicit's +0 value in ItemBases.csv is the simulation's `implicit_at_zero` of its stated
     +10 figure;
  3. the C++ step, two-handed factor, highest level, hybrid tier drop and lowest hybrid tier are the
     simulation's;
  4. the C++ rule is the simulation's rule, read from its source: the upgrade adds 50 and divides by
     100, and the roll is cut into High - Low + 1 shares;
  5. the simulation's rule gives the approved proposal's worked figures, so a change to the rule
     itself fails here rather than only moving two copies together.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADER = SOURCE / "Items" / "CataclysmItem.h"
CPP = SOURCE / "Items" / "CataclysmItem.cpp"
DROP_ROLL = SOURCE / "Items" / "CataclysmDropRoll.cpp"

sys.path.insert(0, str(REPO_ROOT / "sim"))
from cataclysm_sim import affix_ranges, affixes  # noqa: E402

TIER_FIELDS = tuple(f"T{t}{end}" for t in range(1, 8) for end in ("Min", "Max"))


def _rows(name: str) -> list[dict[str, str]]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def _constant(text: str, name: str) -> str:
    match = re.search(rf"static constexpr int32 {name} = ([^;]+);", text)
    assert match, f"{name} is not declared as a static constexpr int32 in {HEADER.name}"
    return match.group(1).strip()


def test_every_affix_row_states_the_simulations_seven_ranges():
    rows = _rows("Affixes.csv")
    stated = {}
    for row in rows:
        numbers = tuple(int(row[field]) for field in TIER_FIELDS)
        if row["AffixKind"] == "Hybrid":
            assert set(numbers) == {0}, f"the hybrid {row['AffixName']} states tier ranges"
            continue
        stated[row["AffixName"]] = tuple(zip(numbers[0::2], numbers[1::2], strict=True))
    assert stated == affix_ranges.TIER_RANGES
    assert len(stated) == 73


def test_every_implicit_states_the_simulations_whole_value_at_plus_nought():
    checked = 0
    for row in _rows("ItemBases.csv"):
        for n in (1, 2):
            if not row[f"Implicit{n}Stat"]:
                assert row[f"Implicit{n}AtZero"] == "0"
                continue
            expected = affix_ranges.implicit_at_zero(float(row[f"Implicit{n}Value"]))
            assert int(row[f"Implicit{n}AtZero"]) == expected, (
                f"{row['BaseName']} implicit {n}: {row[f'Implicit{n}AtZero']} at +0, the "
                f"simulation gives {expected} from {row[f'Implicit{n}Value']} at +10")
            checked += 1
    assert checked == 76


def test_the_games_constants_are_the_simulations():
    header = HEADER.read_text(encoding="utf-8")
    assert _constant(header, "GearLevelStepHundredths") == str(affix_ranges.LEVEL_STEP_HUNDREDTHS)
    assert _constant(header, "HybridTierDrop") == str(affix_ranges.HYBRID_TIER_DROP)
    assert _constant(header, "LowestHybridTier") == "HybridTierDrop + 1"
    assert affix_ranges.LOWEST_HYBRID_TIER == affix_ranges.HYBRID_TIER_DROP + 1
    assert _constant(header, "MaxGearLevel") == str(affix_ranges.MAX_GEAR_LEVEL)
    assert _constant(header, "MaxAffixTier") == str(affix_ranges.MAX_TIER)
    assert re.search(r"static constexpr float TwoHandedMultiplier = 2\.0f;", header)
    assert affix_ranges.TWO_HANDED_FACTOR == 2


def _body(text: str, signature: str) -> str:
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[open_brace:index + 1]
    raise AssertionError(f"{signature} has no closing brace")


def _code(body: str) -> str:
    """The body with its comments taken out, so a rule named only in a comment does not pass."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return "\n".join(line.split("//", 1)[0] for line in body.splitlines())


def test_the_games_rule_is_the_simulations_rule():
    cpp = CPP.read_text(encoding="utf-8")
    upgrade = _code(_body(cpp, "int32 UCataclysmItemValues::UpgradedWhole("))
    assert re.search(r"AtZero \* \(100 \+ GearLevelStepHundredths \* Level\) \* Hands", upgrade)
    assert re.search(r"\(Scaled \+ 50\) / 100", upgrade)
    roll = _code(_body(cpp, "int32 UCataclysmItemValues::WholeAtRoll("))
    assert re.search(r"Clamped \* static_cast<double>\(Steps \+ 1\)", roll)
    assert re.search(r"FMath::Min\(", roll) and "Steps);" in roll
    hybrid = _code(_body(cpp, "float UCataclysmItemValues::HybridHalfValue("))
    assert re.search(r"FMath::Clamp\(HybridTier, LowestHybridTier, MaxAffixTier\)", hybrid)
    assert "Tier - HybridTierDrop" in hybrid


def test_a_dropped_hybrid_rolls_tier_three_at_the_least_in_the_game():
    code = _code(DROP_ROLL.read_text(encoding="utf-8"))
    assert re.search(r"Rolled\.Tier = FMath::Max\(Rolled\.Tier, UCataclysmItemValues::LowestHybridTier\)",
                     code)


def test_the_rule_gives_the_approved_proposals_figures():
    # Rule 3, in integers. 7 at +10 is 7 x 2.5 = 17.5, a half, which goes up; 11 x 1.15 is 12.65.
    assert affix_ranges.upgraded(7, 10) == 18
    assert affix_ranges.upgraded(11, 1) == 13
    assert affix_ranges.upgraded(10, 1) == 12  # 11.5, a half, up -- 11.4999 in floating point
    assert affix_ranges.upgraded(1, 10, two_handed=True) == 5
    # Rule 4: a tier of 1 to 2 is two equal shares.
    assert [affix_ranges.roll_value(1, 2, r) for r in (0.0, 0.49, 0.5, 1.0)] == [1, 1, 2, 2]
    # Flat armor's tier 7 at +10, best and worst: 75 x 2.5 = 187.5 -> 188, 100 x 2.5 = 250.
    assert affix_ranges.value("Flat armor", 7, 1.0, 10) == 250
    assert affix_ranges.value("Flat armor", 7, 0.0, 10) == 188
    # D6: a hybrid at tier 7 grants each half at tier 5; at tier 1 it is read as tier 3, so tier 1.
    assert affix_ranges.hybrid_half("Flat armor", 7, 1.0, 0) == 59
    assert affix_ranges.hybrid_half("Flat armor", 1, 0.0, 0) == 25
    # D9: the Greatsword's 78 at +10 is 31 at +0.
    assert affix_ranges.implicit_at_zero(78.0) == 31


def test_every_tier_is_whole_separate_and_at_least_two_values_at_every_level():
    """The proposal's check 2, on the shipped ranges: at every upgrade level, on a two-handed weapon
    or not, every tier shows at least two whole numbers and sits wholly above the tier below."""
    for name in affix_ranges.TIER_RANGES:
        for level in range(affix_ranges.MAX_GEAR_LEVEL + 1):
            for two_handed in (False, True):
                previous = 0
                for tier in range(1, 8):
                    low, high = affix_ranges.range_at(name, tier, level, two_handed)
                    assert high > low, (name, tier, level, two_handed)
                    assert low > previous, (name, tier, level, two_handed)
                    previous = high


def test_the_simulation_values_every_affix_in_the_pool_by_its_ranges():
    for affix in affixes.AFFIX_POOL:
        assert affix.value_at(7, 1.0, 10) == affix_ranges.value(affix.name, 7, 1.0, 10)
        assert affix.value_at(1, 0.0, 0) == affix_ranges.TIER_RANGES[affix.name][0][0]
