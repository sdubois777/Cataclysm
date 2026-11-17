"""A modifier name belongs to one table, not two.

WHY THIS EXISTS. `game/Data/DungeonModifiers.csv` holds floor-wide rules and
`game/Data/EnemyModifiers.csv` holds per-enemy ones. Three names were in both, and
described **different mechanics in each** -- one said 20 stacks and the other 5, one
reduced maximum health and mana while the other reduced damage output.

WHAT THAT COST, AND WHY A COUNT WOULD NOT HAVE FOUND IT. A survey of which dungeon
rows have code asked whether each row's key appears in the game source.
`Demonic_Infernal_Brand` does -- `UCataclysmEnemyModifiers::InfernalBrandRow` is exactly
that string. **The code belongs to the enemy modifier, and the dungeon row would have
been counted as built.** Three of 117 rows would have been classified wrong, and the
classification is what decides what gets built next.

WHY THE GENERATOR DID NOT CATCH IT. Both files are generated from
`docs/All_Things_Cataclysm.xlsx` by `tools/generate_datatables.py`, which deduplicates
names **within one sheet** -- it appends `_1`, `_2` to repeats it sees in a single pass.
A name appearing once on each of two sheets is unique in both passes, so nothing looked.

THE RULING. The project owner decided both sets stay and the dungeon side is renamed,
because the collision is the only defect and the six descriptions are all plausible
designs. The key is not a column: `row_name(cataclysm, name)` builds it from the
player-facing modifier name, so renaming the key means renaming what a player reads.

THIS RUNS IN THE FAST SUITE AND BUILDS NO C++, which is the point -- the Unreal
automation tests run only when somebody asks for them, and this has to fail on a pull
request.
"""

from __future__ import annotations

import csv
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DUNGEON = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"
ENEMY = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"


def names(path: pathlib.Path) -> dict[str, str]:
    """Every row's `Name`, mapped to what that row says it does."""
    with path.open(newline="", encoding="utf-8") as handle:
        return {row["Name"]: row.get("Description", "") or ""
                for row in csv.DictReader(handle)}


def test_both_tables_are_readable_and_not_empty() -> None:
    """A positive control, because the real check passes on two empty files.

    WRITTEN OUT RATHER THAN ASSUMED. If a path were wrong or a header renamed,
    `names()` would return nothing, the intersection would be empty, and the check
    below would report the tables clean. **A test that passes when it can see nothing
    is worse than no test**, because it is quoted as evidence.
    """
    dungeon, enemy = names(DUNGEON), names(ENEMY)

    assert len(dungeon) > 100, (
        f"{DUNGEON.name} yielded {len(dungeon)} rows, which is too few to be the real "
        "table. Check the path and the `Name` column header.")
    assert len(enemy) > 50, (
        f"{ENEMY.name} yielded {len(enemy)} rows, which is too few to be the real "
        "table. Check the path and the `Name` column header.")


def test_no_modifier_name_appears_in_both_tables() -> None:
    """The check itself.

    THE FAILURE NAMES THE ROWS AND SHOWS BOTH DESCRIPTIONS, deliberately. A guard whose
    failure says only "3 collisions" makes the next person re-derive which three and
    then go and read both tables to see whether the two rows mean the same thing. They
    almost certainly do not -- that is the whole reason this matters.
    """
    dungeon, enemy = names(DUNGEON), names(ENEMY)
    shared = sorted(set(dungeon) & set(enemy))

    if shared:
        lines = [
            f"{len(shared)} name(s) appear in BOTH {DUNGEON.name} and {ENEMY.name}.",
            "A name is a DataTable row key, so one of the two rows is unreachable by "
            "that key and code referring to it silently gets the other.",
            "",
        ]
        for name in shared:
            lines.append(f"  {name}")
            lines.append(f"    dungeon table: {' '.join(dungeon[name].split())[:150]}")
            lines.append(f"    enemy table:   {' '.join(enemy[name].split())[:150]}")
            lines.append("")
        lines.append(
            "Rename the DUNGEON side. The key is generated from the player-facing "
            "modifier name by `row_name(cataclysm, name)` in "
            "tools/generate_datatables.py, so this is a change to "
            "docs/All_Things_Cataclysm.xlsx followed by a regenerate, not an edit to "
            "the CSV.")
        raise AssertionError("\n".join(lines))
