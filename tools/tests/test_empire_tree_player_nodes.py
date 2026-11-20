"""The empire upgrade tree's player nodes say in words what the game counts in C++.

WHY THIS EXISTS. Issue #2741 slice 5a makes fifteen nodes act on the player inside
a dungeon. Their figures per point are in `EMPIRE_TREE_EFFECTS`, which
`test_empire_tree_effects_match_their_texts.py` checks against each node's words.
Three numbers have no column in that table and live in C++ instead:

  Speed Runner          "per 2 full days"          `SpeedRunnerDaysAStep`
  Dungeon Legend        "per 5 unique dungeons"    `DungeonLegendClearsAStep`
  One-Day Specialist    "doubled"                  `OneDaySpecialistFactor`

and a constant typed in two places is a copy, which this project has seen drift.

WHAT ELSE IS CHECKED:
  one reader each       every effect kind is read by exactly one of
                        `UCataclysmEmpireTree::CityBonusesFor` and
                        `PlayerBonusesFor`, so a kind added to the table and the enum
                        but to neither switch -- a node that can be bought and does
                        nothing -- fails here rather than in play
  the doubling's scope  every node whose loot grows with the dungeon or the run is
                        in the Explorer branch, because `PlayerBonusesFor` holds those
                        figures whole and One-Day Specialist doubles them whole; the
                        flat figures are split by branch in C++ (The Hoarder, a
                        centre capstone's option, is the one outside it)
  issue #1428           Speed Runner says "per 2 full days" (the coordinating
                        session's D4 of 2026-11-19), and the tree file no longer says
                        "per 2 days" anywhere

WHAT IS NOT: what the game does with the figures. That is the Unreal group
`Cataclysm.EmpireTreeLoot.`.
"""

from __future__ import annotations

import csv
import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_FILE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
TREE_HEADER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player" / "CataclysmEmpireTree.h"
TREE_SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player" / "CataclysmEmpireTree.cpp"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: THE PLAYER KINDS WHOSE FIGURE GROWS WITH THE DUNGEON OR THE RUN. `PlayerBonusesFor`
#: holds each in one field, not split by branch, and One-Day Specialist doubles it.
GROWING_KINDS = {"LootQuantityPerActiveType", "LootQuantityPerTwoDaysRemoved",
                 "LootQuantityPerDayRemoved", "MagicFindPerFloorRemoved",
                 "MagicFindPerModifierActive", "MagicFindPerFloorCleared",
                 "MagicFindPerFiveClears", "LootQuantityPerExplorerCity",
                 "MagicFindPerExplorerCity", "LootQuantityPerCleanClear"}


def texts() -> dict[str, str]:
    tree = json.loads(TREE_FILE.read_text(encoding="utf-8"))
    out = {}
    for node in tree["nodes"]:
        body = node.get("data", {})
        out[gen.clean(body.get("name", ""))] = gen.clean(body.get("description", ""))
    return out


def constant(name: str) -> str:
    found = re.search(rf"static constexpr \w+ {name} = ([0-9.]+)f?;", TREE_HEADER.read_text(encoding="utf-8"))
    assert found, f"CataclysmEmpireTree.h declares no {name}"
    return found.group(1)


def body_of(source: str, signature: str) -> str:
    """The text between a function's opening brace and its matching closing one."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"{signature!r} has no closing brace")


def cased(body: str) -> set[str]:
    return set(re.findall(r"case ECataclysmEmpireTreeEffect::(\w+):", body))


def test_the_three_counted_words_are_in_their_nodes_texts() -> None:
    words = texts()
    days = constant("SpeedRunnerDaysAStep")
    clears = constant("DungeonLegendClearsAStep")
    factor = float(constant("OneDaySpecialistFactor"))
    assert f"per {days} full days" in words["Speed Runner"], (days, words["Speed Runner"])
    assert f"per {clears} unique dungeons" in words["Dungeon Legend"], (clears, words["Dungeon Legend"])
    assert factor == 2.0 and "doubled" in words["One-Day Specialist"], (factor, words["One-Day Specialist"])
    # AND SLICE 5B'S: The High Roller's "For every 10 floors added".
    floors = constant("HighRollerFloorsAStep")
    assert f"For every {floors} floors added" in words["The High Roller"], (floors, words["The High Roller"])


def test_every_effect_kind_has_exactly_one_reader() -> None:
    source = TREE_SOURCE.read_text(encoding="utf-8")
    city = cased(body_of(source, "FCataclysmEmpireTreeBonuses UCataclysmEmpireTree::CityBonusesFor("))
    player = cased(body_of(source, "FCataclysmEmpireTreePlayerBonuses UCataclysmEmpireTree::PlayerBonusesFor("))
    kinds = set(gen.EMPIRE_TREE_EFFECT_KINDS)
    # THE CONTROL: both switches were found and each reads something, so an empty
    # answer below cannot come from a body this test failed to find.
    assert city and player, "a switch was not found; the function names may have changed"
    assert not city & player, f"read by both: {sorted(city & player)}"
    assert city | player == kinds, (
        f"read by neither: {sorted(kinds - (city | player))}; read but not in the table: "
        f"{sorted((city | player) - kinds)}")


def test_every_growing_figure_belongs_to_an_explorer_node() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        branch = {row["NodeName"]: row["Branch"] for row in csv.DictReader(handle)}
    growing = {name for name, effects in gen.EMPIRE_TREE_EFFECTS.items()
               for effect in effects if effect[0] in GROWING_KINDS}
    # THE CONTROL: ten kinds, nine nodes (The Cartographer's Blessing carries two); Relentless Pursuit's since
    # slice 5b.
    assert len(growing) == 9, sorted(growing)
    outside = sorted(name for name in growing if branch.get(name) != "Explorer")
    assert not outside, (
        f"{outside} grow with the dungeon and are not Explorer nodes, so One-Day Specialist "
        f"would double them; split their figure by branch in PlayerBonusesFor first")


def test_speed_runner_says_full_days_and_nothing_says_per_2_days() -> None:
    raw = TREE_FILE.read_text(encoding="utf-8")
    assert "per 2 full days" in texts()["Speed Runner"]
    assert "per 2 days" not in raw, "the tree file still says \"per 2 days\" somewhere (issue #1428)"
