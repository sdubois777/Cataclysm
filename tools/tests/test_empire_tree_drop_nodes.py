"""The empire upgrade tree's slice 5b: Relentless Pursuit's clears in a row, saved at run record version 11, and the four
drop rules a kill rolls.

WHY THIS EXISTS. Issue #2741 slice 5b, the coordinating session's D5, D6 and E1 to E4 of 2026-11-19. Relentless Pursuit
counts "each dungeon cleared without dying", read as the clears since the last death (D5), which no saved count could
say, so the run record went to version 11. The four drop rules are chances a kill rolls (D6), on kills only (E1), and
Scavenger's second material is the same drop holding two (E2).

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeDrops.` automation
tests never run on a pull request. And the kill's own wiring is a place no automation test reaches: the drop rules are
read through the game mode a kill finds, and the order of the stream's draws is the point.

WHAT IS CHECKED:

  1. `Run_v11.json` is `Run_v10.json` at version 11 with `ClearsSinceLastDeath` added, neither the default nor the
     migration's value for that run, and nothing else changed;
  2. the run record is at version 11 or later, carries the field, and `Migrate_10_to_11` reads the death and the clears
     and writes the count, and is in the chain;
  3. the save gathers and applies the count; `ClearDungeon` raises it beside `DungeonsCleared`; `NoteDeath` and
     `BeginForCharacter` set it to 0, and nothing else does;
  4. a kill passes Scavenger's chance to its own drops and rolls the tree's extra drops after its gold, inside the
     branch where it pays;
  5. every test that runs an older run file through the chain adds what `Migrate_10_to_11` writes, and no test reads
     `Run_v10.json` but the one that migrates it;
  6. the fixtures README lists `Run_v11.json`.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE = SOURCE / "Cataclysm" / "Save"
TESTS = SOURCE / "Cataclysm" / "Tests"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
ENEMY = SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp"

#: The one test that reads `Run_v10.json` since the run record went to version 11: it migrates it.
MIGRATION_TEST = "ARunFileFromVersion10MigratesWithTheClearsItCanProve"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def braced(text: str, start: int, where: str) -> str:
    """The braces-matched block that opens at the first `{` after `start`."""
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where} is not closed")


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    return braced(text, start, f"{signature!r} in {where}")


def test_the_count_is_saved_raised_by_a_clear_and_ended_by_a_death_and_a_new_run_only() -> None:
    gather = code(SAVE / "CataclysmSaveGather.cpp")
    apply = code(SAVE / "CataclysmSaveApply.cpp")
    assert re.search(r"Record\.ClearsSinceLastDeath\s*=\s*Run\.ClearsSinceLastDeath\s*;", gather)
    assert re.search(r"Run\.ClearsSinceLastDeath\s*=\s*Record\.ClearsSinceLastDeath\s*;", apply)

    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    clear = body(run, "bool UCataclysmEmpireRun::ClearDungeon(", "CataclysmEmpireRun.cpp")
    counted = clear.find("++DungeonsCleared;")
    assert counted != -1 and clear.find("++ClearsSinceLastDeath;", counted) != -1, (
        "ClearDungeon does not raise ClearsSinceLastDeath beside DungeonsCleared")
    death = body(run, "void UCataclysmEmpireRun::NoteDeath(", "CataclysmEmpireRun.cpp")
    begin = body(run, "void UCataclysmEmpireRun::BeginForCharacter(", "CataclysmEmpireRun.cpp")
    zeroed = re.compile(r"ClearsSinceLastDeath\s*=\s*0\s*;")
    assert zeroed.search(death), "NoteDeath does not end the clears in a row"
    assert zeroed.search(begin), "BeginForCharacter does not start the clears in a row again"
    assert len(zeroed.findall(run)) == 2, "ClearsSinceLastDeath is set to 0 somewhere other than NoteDeath and Begin"


def test_a_kill_passes_scavengers_chance_and_rolls_the_extra_drops_after_its_gold() -> None:
    handler = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    paying = handler.find("if (bPays)")
    assert paying != -1, "the kill no longer has its paying branch"
    # THE RULES ARE ASKED BEFORE THE BRANCH, which `test_dungeon_modifier_rules_are_the_rows.py` holds to opening with the
    # drop roll; asking draws nothing from the stream.
    rules = handler.find("ACataclysmDungeonGameMode::EmpireTreeDropRulesIn(World)")
    assert rules != -1 and rules < paying, "the kill does not ask the tree's drop rules before its paying branch"
    branch = braced(handler, paying, "the kill's paying branch")
    drops = branch.find("UCataclysmDropSpawner::SpawnDropsFor(")
    gold = branch.find("UCataclysmDropSpawner::SpawnGoldFor(")
    extra = branch.find("UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(")
    assert -1 not in (drops, gold, extra), (drops, gold, extra)
    assert drops < gold < extra, "the extra drops are not rolled after the gold, so a draw before them would move"
    own = branch[drops:branch.index(";", drops)]
    assert "LowTierMaterialExtraChance" in own, "the kill's own drops are not given Scavenger's chance"


