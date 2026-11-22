"""The empire upgrade tree's walk and depth nodes are the simulation's, and the game reads them where the rulings put them.

WHY THIS EXISTS. Issue #2741 slice 4, ruled by the coordinating session on 2026-11-19 (W1 to W9). The game's effect
table (`EMPIRE_TREE_EFFECTS` in `tools/generate_datatables.py`) states what each node does; the simulation's
`TREE_EXPLORER_AS_DESIGNED` states what the full Explorer branch does to a walk and a depth. They are two copies of the
same nodes, and this project has recorded copies drifting (`CLAUDE.md`).
`tools/tests/test_the_explorer_preset_matches_the_tree.py` holds the simulation's copy to the tree file; this holds the
game's to the simulation's.

WHY THE PLACES ARE CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeWalk.`
automation tests never run on a pull request. And a few rules are about where a number is read, which a source read
shows and a test of one call cannot: that the game mode charges the run's effective walk and not the dungeon's stored
one; that the boss is not given Infinite Depths; that The Warlord takes no draw from the run's streams.

WHAT IS CHECKED:
  1. the share nodes compound to `run_days_mult`, Sovereign's Haste is `run_days_flat_per_type` to its cap, the flat
     floor nodes at full are `floor_delta`, and Infinite Depths is `floor_delta_per_type`;
  2. the game mode charges `Run->WalkDaysPerFloorFor(...)`;
  3. the boss's floors are counted with `FloorsAddedTo(/*bTheBoss*/ true, ...)` and every other dungeon's with false;
  4. Veteran's Edge multiplies the award on the line after `EmpirePointsFor`;
  5. The Warlord's roll is a stream of its own and it removes through `RemoveDungeon`.
"""

from __future__ import annotations

import csv
import math
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "sim"))

import generate_datatables as gen  # noqa: E402
from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED  # noqa: E402


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


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


def max_points() -> dict[str, int]:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["NodeName"]: int(row["MaxPoints"]) for row in csv.DictReader(handle) if row["MaxPoints"]}


def effects_of(kind: str) -> list[tuple[str, tuple]]:
    return [(name, effect) for name, effects in gen.EMPIRE_TREE_EFFECTS.items() for effect in effects
            if effect[0] == kind]


def test_the_tables_walk_and_floor_nodes_are_the_simulations_explorer_preset() -> None:
    points = max_points()
    share = math.prod((1.0 - effect[1]) ** points[name] for name, effect in effects_of("WalkTimeCompounded"))
    assert math.isclose(share, TREE_EXPLORER_AS_DESIGNED.run_days_mult, rel_tol=1e-9), (
        f"the table's share nodes leave {share:.6f} of a walk; the simulation's preset {TREE_EXPLORER_AS_DESIGNED.run_days_mult:.6f}")

    (haste, effect), = effects_of("WalkDaysFewerPerActiveType")
    assert haste == "Sovereign's Haste"
    assert effect[1] * points[haste] == TREE_EXPLORER_AS_DESIGNED.run_days_flat_per_type
    assert effect[4] == TREE_EXPLORER_AS_DESIGNED.run_days_flat_per_type_cap

    # THE FLAT FLOOR NODES OF THE EXPLORER BRANCH at full: Architect of Greed, Deep Boring and Exclusionary Mapping. The
    # Collector's Decree is a capstone option and Architectural Insight counts Architect points; the preset, a pure
    # Explorer build, holds neither (config.py, beside `TREE_EXPLORER_AS_DESIGNED`).
    branch = sum(effect[1] * points[name] for name, effect in effects_of("FloorsAdded") if name in points)
    assert branch == TREE_EXPLORER_AS_DESIGNED.floor_delta, (branch, TREE_EXPLORER_AS_DESIGNED.floor_delta)

    (depths, effect), = effects_of("FloorsAddedPerActiveType")
    assert depths == "Infinite Depths"
    assert effect[1] * points[depths] == TREE_EXPLORER_AS_DESIGNED.floor_delta_per_type


def test_the_game_mode_charges_the_runs_effective_walk() -> None:
    charge = body(code(GAME_MODE), "void ACataclysmDungeonGameMode::SpendFloorTimeInTheEmpire()")
    assert "Run->SpendFloorTime(Run->WalkDaysPerFloorFor(*Dungeon))" in charge, (
        "the game mode no longer charges a floor at the run's effective walk, so the tree's walk nodes would not reach it")
    assert "Dungeon->WalkDaysPerFloor()" not in charge


def test_the_boss_is_not_given_infinite_depths_and_every_other_dungeon_is() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    boss = body(run, "int32 UCataclysmEmpireRun::CataclysmDungeonFloorsNow() const")
    assert re.search(r"FloorsAddedTo\(\s*true\s*,", boss), "the boss's floors no longer count the tree's flat floors"
    others = re.findall(r"FloorsTheTreeAdds\s*=\s*\w+\.FloorsAddedTo\(\s*(\w+)\s*,", run)
    assert others and set(others) == {"false"}, f"the scheduler's floors are set with {others}"
    bonuses = body(code(EMPIRE / "CataclysmEmpireTreeBonuses.cpp"),
                   "int32 FCataclysmEmpireTreeBonuses::FloorsAddedTo(bool bTheBoss, int32 ActiveCataclysmTypes) const")
    assert re.search(r"bTheBoss\s*\?\s*0\s*:", bonuses), "FloorsAddedTo gives the boss Infinite Depths"


def test_veterans_edge_multiplies_the_award_after_empire_points_for() -> None:
    clear = body(code(EMPIRE / "CataclysmEmpireRun.cpp"), "bool UCataclysmEmpireRun::ClearDungeon(int32 DungeonId)")
    awarded = clear.find("LastEmpirePointsAwarded = EmpirePointsFor(")
    # AND REINVESTMENT'S SHARE ADDED TO IT since issue #2741 slice 9 (V3), on the same line.
    raised = clear.find("LastEmpirePointsAwarded *= 1.0 + static_cast<double>(TreeBonuses.EmpirePointsIncrease)")
    assert 0 <= awarded < raised, "Veteran's Edge is not applied to the award after EmpirePointsFor"


def test_the_warlord_rolls_a_stream_of_its_own_and_removes_through_remove_dungeon() -> None:
    clear = body(code(EMPIRE / "CataclysmEmpireRun.cpp"), "bool UCataclysmEmpireRun::ClearDungeon(int32 DungeonId)")
    start = clear.find("TreeBonuses.RemoveAnotherDungeonChance > 0.0f")
    assert start != -1, "ClearDungeon no longer has The Warlord"
    warlord = body(clear[start:], "")
    # THROUGH THE SEED MIX since issue #2749's placement seeding slice (it was `MixedSeed`, which is linear, so one
    # run's dungeons drew related rolls); still a stream of its own, keyed on the dungeon and the rule's salt.
    assert "FRandomStream Roll(CataclysmSeedMix::Mix(WalkSeedMask ^ (DungeonId + 1), WarlordSalt, 0));" in warlord
    for stream in ("Stream.", "CataclysmStream.", "ModifierStream."):
        assert not re.search(rf"(?<![\w]){re.escape(stream)}", warlord), (
            f"The Warlord draws from the run's {stream.rstrip('.')}, which moves every later draw on it")
    assert "RemoveDungeon(Soonest);" in warlord and "ClearDungeon(" not in warlord
    assert "TheWarlordsDungeonOn(CityId)" in warlord
    target = body(code(EMPIRE / "CataclysmEmpireRun.cpp"), "int32 UCataclysmEmpireRun::TheWarlordsDungeonOn(int32 CityId) const")
    assert "ECataclysmDungeonType::FallenCity" in target, "The Warlord can remove a Fallen City dungeon"
    # THE BOSS STANDS ON NO CITY, so the rule's own guard keeps it out; naming its kind here would be a second mention
    # of the Cataclysm kind in `ClearDungeon`, which `test_surge_port.py` holds to one, the win.
    assert "CityId != INDEX_NONE" in clear[clear.rfind("if", 0, start):start + 80]
