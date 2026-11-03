"""The rung the Cataclysm Boss is placed at is the rarity table's own.

WHY THIS EXISTS. Issue #1324 slice 6d, 2026-11-03. The last floor of the
Cataclysm boss dungeon puts its Gatekeeper at the Cataclysm Boss rung rather than
drawing one, because the design says so in section VIII of
`docs/Cataclysm_GDD_v2.md`: "A Cataclysm Boss is placed, not drawn". The rung is
the constant `ACataclysmDungeonGameMode::CataclysmBossRung`, and the rarity it
means is a row of `game/Data/EnemyRarities.csv`. Two numbers that must agree and
live in two files are the shape that has drifted in this project before.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmBossFloorTests.cpp`
never run on a pull request. Reading the source as text does.

WHAT IS NOT CHECKED HERE. That the game places the boss at the rung, that the
stairs are sealed while it stands, and what the floor panel says: those are the
`Cataclysm.BossFloor.` automation tests.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_MODE_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
               / "CataclysmDungeonGameMode.h")
PANEL_LAYOUT_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
                  / "CataclysmFloorModifierPanelLayout.h")
RARITIES_CSV = REPO_ROOT / "game" / "Data" / "EnemyRarities.csv"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. The "
            f"Cataclysm boss floor is issue #1324 slice 6d; if a file was "
            f"renamed, rename it here too.")
    return path.read_text(encoding="utf-8")


def constant(text: str, pattern: str, what: str) -> str:
    found = re.findall(pattern, text)
    if len(found) != 1:
        pytest.fail(f"expected exactly one {what}, found {len(found)}")
    return found[0]


def rarity_rows() -> list[dict[str, str]]:
    with RARITIES_CSV.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def test_the_cataclysm_boss_rung_is_the_tables_cataclysm_boss_step():
    rung = int(constant(read(GAME_MODE_H),
                        r"static constexpr int32 CataclysmBossRung = (\d+);",
                        "CataclysmBossRung"))
    named = [row for row in rarity_rows() if row["RarityName"] == "Cataclysm Boss"]
    assert len(named) == 1, (
        "game/Data/EnemyRarities.csv no longer holds exactly one row named "
        "'Cataclysm Boss'")
    assert int(named[0]["Step"]) == rung, (
        f"ACataclysmDungeonGameMode::CataclysmBossRung is {rung}, but the rarity "
        f"table puts the Cataclysm Boss at step {named[0]['Step']}: the last "
        f"floor of the Cataclysm boss dungeon would place its boss at the wrong "
        f"rarity")


def test_the_cataclysm_boss_rung_is_the_top_of_the_ladder_and_cannot_be_drawn():
    rung = int(constant(read(GAME_MODE_H),
                        r"static constexpr int32 CataclysmBossRung = (\d+);",
                        "CataclysmBossRung"))
    rows = rarity_rows()
    assert rung == max(int(row["Step"]) for row in rows), (
        "the Cataclysm Boss rung is not the top of the rarity ladder")
    at_rung = [row for row in rows if int(row["Step"]) == rung]
    assert float(at_rung[0]["SpawnWeight"]) == 0.0, (
        "the rung the Cataclysm Boss is placed at can now be drawn, so a boss "
        "could appear on an ordinary floor; the design says it is placed, not "
        "drawn")


def test_the_design_still_says_a_cataclysm_boss_is_placed_not_drawn():
    flat = " ".join(read(GDD).split())
    assert "**A Cataclysm Boss is placed, not drawn**" in flat, (
        "docs/Cataclysm_GDD_v2.md no longer says 'A Cataclysm Boss is placed, "
        "not drawn', which is the sentence the Cataclysm boss floor is built "
        "on (issue #1324 slice 6d)")


def test_the_seal_key_and_the_panel_key_are_the_same_text():
    seal = constant(read(GAME_MODE_H),
                    r'CataclysmBossSealKey = TEXT\("([^"]+)"\);',
                    "CataclysmBossSealKey")
    panel = constant(read(PANEL_LAYOUT_H),
                     r'CataclysmBossKey = TEXT\("([^"]+)"\);',
                     "CataclysmBossKey")
    assert seal == panel, (
        f"the key StairsSealedBy names the Cataclysm Boss by ({seal!r}) and the "
        f"key the floor panel lists its line by ({panel!r}) have drifted apart; "
        f"both headers say they are the same text")
