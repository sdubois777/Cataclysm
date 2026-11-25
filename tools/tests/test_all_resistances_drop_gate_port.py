"""The game's drop roll leaves the all-resistances family off a difficulty tier 1 drop, as the simulation's does.

WHY THIS EXISTS. Issue #2853, ruled by the project owner on 2026-11-24, the chosen option quoted: "No all-resist at
tier 1 (Recommended)": the all-resistances affix family cannot roll on a difficulty tier 1 drop; single resistances still
can. The simulation's rule is `affixes.rolls_on_a_drop_at`, asked by `loot.roll_item`; the game's is
`UCataclysmDropRoll::RollsOnADropAt`, asked by `UCataclysmDropRoll::RollItem`. Continuous integration compiles no C++,
so `Cataclysm.Drop.NoAllResistancesAffixRollsOnATierOneDrop` never runs on a pull request; this holds the port's text:

  1. the game's lowest tier is the simulation's;
  2. the game finds the family as the one resistance row covering every damage type, and that row is the simulation's
     all-resistances family, by name and breadth, in `game/Data/Affixes.csv`;
  3. `RollItem` asks the rule of the candidates after `CandidatesFor` builds them and before the draw, as
     `loot.roll_item` does, so their damage types are drawn as before;
  4. nothing else asks it: crafting is not gated (the forge's affix change draws from `CandidatesFor` unfiltered).
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADER = SOURCE / "Items" / "CataclysmDropRoll.h"
ROLL = SOURCE / "Items" / "CataclysmDropRoll.cpp"
AFFIXES = REPO_ROOT / "game" / "Data" / "Affixes.csv"

if str(REPO_ROOT / "sim") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import affixes as af  # noqa: E402


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed, so a mention in a comment cannot satisfy a check."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"\s+", " ", re.sub(r"//[^\n]*", " ", text))


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} is not closed")


def test_the_lowest_tier_is_the_simulations() -> None:
    found = re.search(r"static constexpr int32 AllResistancesLowestDropTier = (\d+);", code(HEADER))
    assert found, "CataclysmDropRoll.h declares no AllResistancesLowestDropTier"
    assert int(found.group(1)) == af.ALL_RESISTANCES_LOWEST_DROP_TIER == 2


def test_the_game_finds_the_family_the_simulation_means() -> None:
    rule = body(code(ROLL), "bool UCataclysmDropRoll::RollsOnADropAt(")
    assert ("IsResistanceFamily(Row) && Row.Breadth >= UCataclysmItemModifiers::DamageTypeNames().Num()" in rule), (
        "RollsOnADropAt no longer finds the family as the resistance row covering every damage type")
    assert "return !bAllResistances || DifficultyTier >= AllResistancesLowestDropTier;" in rule
    with AFFIXES.open(encoding="utf-8", newline="") as handle:
        rows = [row for row in csv.DictReader(handle) if row["AffixKind"] == "Resistance"]
    covering_every_type = [row for row in rows if int(row["Breadth"]) >= len(af.DAMAGE_TYPES)]
    assert [(row["AffixName"], int(row["Breadth"])) for row in covering_every_type] == [
        (af.ALL_RESISTANCE.name, af.ALL_RESISTANCE.breadth)], (
        f"the resistance rows covering every damage type are {covering_every_type}, not the simulation's one "
        f"all-resistances family, so the game would leave out a different family than the simulation does")


def test_the_roll_asks_the_rule_after_building_the_candidates_and_before_the_draw() -> None:
    roll = body(code(ROLL), "bool UCataclysmDropRoll::RollItem(")
    built = roll.find("CandidatesFor(AffixTable, Slot, Each.Key, Stream, Candidates);")
    asked = roll.find("!UCataclysmDropRoll::RollsOnADropAt(*Row, DifficultyTier)")
    removed = roll.find("Candidates.RemoveAll(")
    drawn = roll.find("DrawWithoutRepeatingAGroup(AffixTable, Candidates, Each.Value,")
    assert -1 < built < removed < asked < drawn, (
        "RollItem does not take the left-out family out of the candidates between building them and drawing")


def test_crafting_is_not_gated() -> None:
    """The owner's ruling is about a DROP: the forge may still make the family at tier 1."""
    asking = [path.relative_to(REPO_ROOT).as_posix() for path in SOURCE.rglob("*.cpp")
              if "Tests" not in path.parts and "RollsOnADropAt(" in code(path)]
    assert asking == ["game/Source/Cataclysm/Items/CataclysmDropRoll.cpp"], (
        f"the all-resistances drop rule is asked outside the drop roll: {asking}")
