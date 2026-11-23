"""Every change to a loot filter goes through the one check that refuses a built-in, and the file stays out of saves.

WHY THIS EXISTS. Issue #2846, slice LF2. The coordinating session's condition of 2026-11-22: the three built-in
filters' names must be impossible to overwrite. `UCataclysmLootFilterStore` protects them three ways, and the
automation tests `Cataclysm.LootFilterScreen.` drive each change through it. What no test can see is a NEW change
function added later that forgets the check, so this reads the source: every function that changes a filter's rules,
name or existence asks `EditableFilter`, which refuses a built-in first.

WHAT IS CHECKED:

  1. `AddRule`, `RemoveRule`, `MoveRule`, `SetRule`, `RenameFilter` and `DeleteFilter` each call `EditableFilter`, and
     `EditableFilter` refuses a built-in before it looks for the player's filter;
  2. reading the file sanitises it: `Load` goes through `FromJson`, and `FromJson` through `Sanitised`, which renames a
     stored filter whose name is taken -- a built-in's included;
  3. the screen's editor refuses a built-in before it edits, and writes the file after every change;
  4. the preferences are outside the save records (the owner's ruling O4): no file under `game/Source/Cataclysm/Save`
     names the preferences or a loot filter, and the file's path is under Saved/Preferences.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
PREFERENCES = SOURCE / "Items" / "CataclysmLootFilterPreferences.cpp"


def code(path: pathlib.Path) -> str:
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
            return re.sub(r"\s+", " ", text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


CHANGES = ("AddRule", "RemoveRule", "MoveRule", "SetRule", "RenameFilter", "DeleteFilter")


def test_every_change_asks_the_one_check_that_refuses_a_built_in() -> None:
    text = code(PREFERENCES)
    for change in CHANGES:
        assert "EditableFilter(Preferences," in body(text, f"bool UCataclysmLootFilterStore::{change}("), change
    check = body(text, "FCataclysmLootFilter* EditableFilter(")
    refuse = check.find("UCataclysmLootFilterStore::IsBuiltInName(Name)")
    look = check.find("OwnFilter(Preferences, Name)")
    assert refuse != -1 and look != -1 and refuse < look, "EditableFilter must refuse a built-in before it looks"
    assert "OutRefusal = UCataclysmLootFilterStore::BuiltInRefusal;" in check


def test_reading_the_file_sanitises_it() -> None:
    text = code(PREFERENCES)
    assert "return FromJson(Json, Out);" in body(text, "bool UCataclysmLootFilterStore::Load(")
    assert "Out = Sanitised(Read);" in body(text, "bool UCataclysmLootFilterStore::FromJson(")
    assert "NameTaken(Out, Name) ? FreeName(Out) : Name" in body(
        text, "FCataclysmLootFilterPreferences UCataclysmLootFilterStore::Sanitised(")
    assert "UCataclysmLootFilterStore::IsBuiltInName(Name)" in body(text, "bool NameTaken(")


def test_the_screen_refuses_a_built_in_and_writes_after_every_change() -> None:
    press = body(code(SOURCE / "Interface" / "CataclysmLootFilterWidget.cpp"), "bool UCataclysmLootFilterWidget::Press(")
    edit = press[press.find('Kind == TEXT("Edit")'):]
    assert edit.find("FPrefs::IsBuiltInName(Picked)") < edit.find("UCataclysmLootFilterLayout::EditRule(Rule, Value)")
    assert "if (bChanged) { SaveIfReal(); }" in press


def test_the_preferences_are_outside_the_save_records() -> None:
    for path in (SOURCE / "Save").rglob("*.*"):
        text = path.read_text(encoding="utf-8", errors="replace")
        assert "LootFilter" not in text, f"{path.relative_to(REPO_ROOT)} names a loot filter; O4 keeps them out of saves"
    file_path = body(code(PREFERENCES), "FString UCataclysmLootFilterStore::FilePath(")
    assert 'FPaths::ProjectSavedDir(), TEXT("Preferences"), TEXT("LootFilters.json")' in file_path
