"""The dungeon modifier rules built in C++ name real rows, and their rows still
say what the rules take.

WHY THIS EXISTS. Issue #41. `game/Source/Cataclysm/Dungeon/
CataclysmDungeonModifierEffects.h` and `.cpp` give two of the 117 rows of
`game/Data/DungeonModifiers.csv` a rule: Starvation takes 1% of maximum health
and energy shield a floor up to 60%, and Dehydration 1% of maximum mana a floor.
The C++ automation tests prove the rules do that, and they build every number
they check by hand, so all of them would keep passing through two changes that
break the game:

  - the row key the rule looks for is renamed in the design workbook, after
    which no floor ever carries it and the rule never fires;
  - the row's description changes its number, after which the game takes one
    share and the floor panel shows the player another.

This file runs on every pull request as part of `python -m pytest`. The C++
tests run only when somebody runs `python tools/unreal_build.py tests`.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"
EFFECTS_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
EFFECTS_HEADER = EFFECTS_DIR / "CataclysmDungeonModifierEffects.h"
EFFECTS_SOURCE = EFFECTS_DIR / "CataclysmDungeonModifierEffects.cpp"

#: A dungeon modifier row key written as a C++ string literal. The first word is
#: one of the eight Cataclysms or Generic, which is how every key in the table
#: begins.
ROW_KEY = re.compile(
    r'TEXT\("((?:Celestial|Chaos|Death|Demonic|Famine|Pestilence|Void|War|Generic)'
    r'_\w+)"\)')


def rows() -> dict[str, dict[str, str]]:
    with TABLE.open(newline="", encoding="utf-8") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def flat(text: str) -> str:
    """The text with every run of whitespace made one space."""
    return " ".join(text.split())


def constant(name: str) -> float:
    """A `static constexpr float` from the effects header, by name."""
    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    found = re.search(rf"\b{re.escape(name)}\s*=\s*([0-9]+(?:\.[0-9]+)?)f\s*;", text)
    assert found, f"{name} is not declared in {EFFECTS_HEADER.name}"
    return float(found.group(1))


def test_the_key_search_finds_keys_in_text_built_to_hold_them():
    """A positive control: the search this file relies on is not blind.

    WRITTEN OUT HERE RATHER THAN READ FROM THE C++, because a search that is
    silently broken finds nothing in the real file too, and "nothing named" would
    then read as "nothing wrong".
    """
    sample = ('x = TEXT("Famine_Starvation"); y = TEXT("Chaos_Unstable_Dimensions");'
              ' z = TEXT("max_health");')
    assert ROW_KEY.findall(sample) == ["Famine_Starvation", "Chaos_Unstable_Dimensions"]


def test_every_row_key_the_rules_name_is_a_row_of_the_table():
    named = ROW_KEY.findall(EFFECTS_SOURCE.read_text(encoding="utf-8"))
    assert named, f"no dungeon modifier row key found in {EFFECTS_SOURCE.name}"

    missing = sorted(set(named) - set(rows()))
    assert not missing, (
        f"{EFFECTS_SOURCE.name} names {missing}, which "
        f"{TABLE.relative_to(REPO_ROOT).as_posix()} does not hold. A rule keyed "
        f"by a name that is not a row never fires.")


def test_starvation_still_says_what_the_code_takes():
    words = flat(rows()["Famine_Starvation"]["Description"])
    per_floor = constant("StarvationPercentPerFloor")
    most = constant("StarvationMostPercent")

    assert f"reduced by {per_floor:g}%" in words, words
    assert f"Up to {most:g}%" in words, words


def test_dehydration_still_says_what_the_code_takes():
    words = flat(rows()["Famine_Dehydration"]["Description"])
    per_floor = constant("DehydrationPercentPerFloor")

    assert f"reduced by {per_floor:g}%" in words, words


def test_dehydrations_cap_is_still_a_judgement_and_not_the_rows():
    """Dehydration stops at 60% because Starvation does, and its row says nothing.

    IF THE ROW EVER STATES A CAP, this fails, so the C++ constant is checked
    against it and `docs/DECISIONS.md` stops calling it a judgement.
    """
    words = flat(rows()["Famine_Dehydration"]["Description"]).lower()
    assert "up to" not in words, (
        "The Dehydration row now states a cap. Check "
        "DehydrationMostPercent against it and update docs/DECISIONS.md.")
