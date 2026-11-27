"""A Vengeful Wraith is saved as a wraith and comes back from a load as one, with the run record at version 10.

WHY THIS EXISTS. Issue #2725: a saved creature carried no field saying it was a Vengeful Wraith, and
`ACataclysmDungeonGameMode::RestoreCreatureRoles` had a branch for every other rule creature and none for a wraith, so
after a load a wraith took every hit in full, hit 20% less hard, moved at its kind's speed, and was not in the rule's
list, so a later rung change did not put its figures back either. The coordinating session approved the fix on
2026-11-19: `FCataclysmSavedCreature::bIsVengefulWraith`, written by `CreatureRolesInto` and read back by
`RestoreCreatureRoles`, at RUN RECORD version 10 -- the run record's own number, reached the same day as the character
record's own 10 (issue #2764).

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the automation tests
`Cataclysm.SaveLoad.AVengefulWraithComesBackAWraithAfterALoad` and
`Cataclysm.SaveLoad.ARunFileFromVersion9MigratesWithNoWraith` never run on a pull request. Reading the source and the
committed save files as text does.

WHAT IS CHECKED:

  1. `Run_v10.json` is `Run_v9.json` at version 10 with `bIsVengefulWraith` true on its first creature and false on its
     second, and nothing else changed;
  2. the saved creature carries the field, the run record is at version 10 or later, and `Migrate_9_to_10` writes false
     on every creature of the floor and is in the chain;
  3. `CreatureRolesInto` writes the field from the creature's mark and the rule's list, and `RestoreCreatureRoles` puts
     a saved wraith in the list BEFORE it asks for the figures, which `ApplyVengefulWraithFigures` writes only for a
     creature in the list;
  4. every test that runs an older run file through the whole chain adds what `Migrate_9_to_10` writes, and no test
     reads `Run_v9.json` but the one that migrates it;
  5. the fixtures README lists `Run_v10.json`.

WHAT IS NOT: what a restored wraith does in a fight. The automation tests check that.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE = SOURCE / "Cataclysm" / "Save"
TESTS = SOURCE / "Cataclysm" / "Tests"
GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"

#: The one test that reads `Run_v9.json` since the run record went to version 10: it migrates it.
MIGRATION_TEST = "ARunFileFromVersion9MigratesWithNoWraith"


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


def test_the_game_mode_writes_the_mark_and_puts_a_saved_wraith_back_in_the_list_first() -> None:
    mode = code(GAME_MODE)
    gather = body(mode, "void ACataclysmDungeonGameMode::CreatureRolesInto(", "the dungeon game mode")
    assert re.search(r"Record\.bIsVengefulWraith\s*=\s*Creature\.bIsVengefulWraith\s*&&\s*VengefulWraiths\.Contains\(",
                     gather), "CreatureRolesInto does not write the wraith mark from the creature's flag and the list"

    restore = body(mode, "void ACataclysmDungeonGameMode::RestoreCreatureRoles(", "the dungeon game mode")
    branch = restore.find("if (Saved.bIsVengefulWraith)")
    assert branch != -1, "RestoreCreatureRoles has no branch for a saved wraith"
    wraith = braced(restore, branch, "RestoreCreatureRoles' wraith branch")
    listed = wraith.find("VengefulWraiths.Add(&Creature);")
    figures = wraith.find("ApplyVengefulWraithFigures(&Creature);")
    assert listed != -1 and figures != -1 and listed < figures, (
        "a saved wraith is not put in the rule's list before its figures are asked for; ApplyVengefulWraithFigures "
        "writes nothing for a creature outside the list")


