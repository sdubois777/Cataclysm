"""The Edicts of Temperance, Patience and Stillness: the game's numbers against the design's sentences.

**WHAT IS HELD, AND WHERE EACH SIDE IS.** The design is `docs/Cataclysm_GDD_v2.md`, Heaven's Wrath: the Edict table
gives each Edict's base effect, and the Ascendant's paragraph "Each Edict scales its one number, up to a cap" gives
each cap, ruled by the coordinating session on 2026-10-22 under the owner's delegation. The game holds them in
`UCataclysmDungeonModifierEffects` (`CataclysmDungeonModifierEffects.h`); the rows are in
`game/Data/DungeonModifiers.csv`, generated from the design workbook since 2026-10-22 (G7b).

WHAT IS COMPARED, AND HOW EACH CHECK FAILS:

- **The base numbers.** Temperance's "halved" is 50, Patience's "50% longer" is 50, Stillness's "40% for 3 seconds"
  is 40 and 3, read from the design's table and from each row's description. Fails when either side moves;
  `test_the_base_check_sees_a_changed_number` shows it does.
- **The caps.** Temperance at most 90, Stillness at most 90 with its 3 seconds fixed, Patience with no cap: a
  `Most` constant for the first two, the scaling function holding at it, and none for Patience. Fails when a cap
  moves, is lost, or Patience gains one; `test_the_cap_check_sees_a_changed_cap` shows it does.
- **Temperance's leech removal does not scale**, ruled 2026-10-22: the removal is written from the field being above
  nought, never from a strength. `test_the_leech_check_sees_a_scaled_removal` shows it fails when it is.
- **The strength reaches the game's callers.** `PlayerEffectsFor`'s three callers in the game mode pass the Edict
  strength; a caller left at the default would leave an Ascendant's Temperance and Patience at base.
  `test_the_caller_check_sees_a_caller_left_at_the_default` shows it fails when one is.

A LINE THAT IS A COMMENT DOES NOT COUNT, where code is read.
"""

from __future__ import annotations

import csv
import io
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DUNGEON = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
EFFECTS_H = DUNGEON / "CataclysmDungeonModifierEffects.h"
EFFECTS_CPP = DUNGEON / "CataclysmDungeonModifierEffects.cpp"
MODE_CPP = DUNGEON / "CataclysmDungeonGameMode.cpp"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
TABLE = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"

ROWS = {
    "Temperance": "Celestial_Edict_of_Temperance",
    "Patience": "Celestial_Edict_of_Patience",
    "Stillness": "Celestial_Edict_of_Stillness",
}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_bytes().decode("utf-8").replace("\r\n", "\n")


def flat(text: str) -> str:
    return " ".join(text.split())


def code_lines(text: str) -> str:
    """Every line that is not a `//` comment line."""
    return "\n".join(line for line in text.splitlines() if not line.strip().startswith("//"))


def header_number(text: str, name: str) -> float | None:
    found = re.search(rf"static constexpr float {name} = ([\d.]+)f;", text)
    return float(found.group(1)) if found else None


def function_code(text: str, opening: str) -> str:
    count = text.count(opening)
    assert count == 1, f"the source holds {opening!r} {count} times"
    start = text.index(opening)
    end = text.find("\n}\n", start)
    assert end != -1, f"{opening!r} is not closed"
    return code_lines(text[start:end])


def design_row(design: str, edict: str) -> str:
    """The base effect cell of the design's Edict table row for this Edict."""
    rows = [line for line in design.splitlines() if line.startswith(f"| Edict of {edict} |")]
    assert len(rows) == 1, f"the design has {len(rows)} table rows for the Edict of {edict}"
    return rows[0].split("|")[4].strip()


def descriptions() -> dict[str, str]:
    rows = {row["Name"]: row for row in csv.DictReader(io.StringIO(read(TABLE)))}
    return {edict: flat(rows[key]["Description"]) for edict, key in ROWS.items() if key in rows}


# ---------------------------------------------------------------------------
# What each check asks, as functions, so each can be asked of an altered copy
# ---------------------------------------------------------------------------

def base_problems(header: str, design: str, words: dict[str, str]) -> list[str]:
    problems = []
    temperance = header_number(header, "EdictOfTemperanceHealingLessPercent")
    if "halved" not in design_row(design, "Temperance") or temperance != 50.0:
        problems.append(f"Temperance's base is {temperance}, and the design says halved")
    if "halved" not in words.get("Temperance", "") or "life leech does nothing" not in words.get("Temperance", ""):
        problems.append("Temperance's row no longer says halved and that life leech does nothing")

    patience = header_number(header, "EdictOfPatienceCooldownLongerPercent")
    for where, text in (("design", design_row(design, "Patience")), ("row", words.get("Patience", ""))):
        found = re.search(r"(\d+)% longer", text)
        if not found or float(found.group(1)) != patience:
            problems.append(f"Patience's {where} says {found.group(0) if found else 'no lengthening'}; "
                            f"the game {patience}")

    slow = header_number(header, "EdictOfStillnessSlowPercent")
    seconds = header_number(header, "EdictOfStillnessSeconds")
    for where, text in (("design", design_row(design, "Stillness")), ("row", words.get("Stillness", ""))):
        found = re.search(r"by (\d+)% for (\d+) seconds", text)
        if not found or (float(found.group(1)), float(found.group(2))) != (slow, seconds):
            problems.append(f"Stillness's {where} says {found.group(0) if found else 'nothing'}; "
                            f"the game {slow} for {seconds}")
    return problems


def cap_problems(header: str, source: str, design: str) -> list[str]:
    problems = []
    section = flat(design)
    temperance = re.search(r"the Edict of Temperance withholds at most (\d+)% of healing", section)
    if not temperance or float(temperance.group(1)) != header_number(header, "EdictOfTemperanceMostHealingLessPercent"):
        problems.append("Temperance's cap")
    stillness = re.search(r"the Edict of Stillness slows by at most (\d+)%, its (\d+) seconds fixed", section)
    if (not stillness
            or float(stillness.group(1)) != header_number(header, "EdictOfStillnessMostSlowPercent")
            or float(stillness.group(2)) != header_number(header, "EdictOfStillnessSeconds")):
        problems.append("Stillness's cap or its seconds")
    if "the Edict of Patience has no cap" not in section:
        problems.append("the design no longer says Patience has no cap")
    if re.search(r"static constexpr float EdictOfPatienceMost\w* =", header):
        problems.append("the game declares a cap for Patience")

    held = {
        "float UCataclysmDungeonModifierEffects::EdictOfTemperanceHealingLessPercentAt(float Strength)":
            "EdictOfTemperanceMostHealingLessPercent",
        "float UCataclysmDungeonModifierEffects::EdictOfStillnessSlowPercentAt(float Strength,":
            "EdictOfStillnessMostSlowPercent",
    }
    for opening, most in held.items():
        code = function_code(source, opening)
        if "FMath::Min(" not in code or most not in code:
            problems.append(f"{opening.split('::')[1].split('(')[0]} is not held at {most}")
    stillness_code = function_code(
        source, "float UCataclysmDungeonModifierEffects::EdictOfStillnessSlowPercentAt(float Strength,")
    if not re.search(r"SecondsSinceMovementSkill >= EdictOfStillnessSeconds", stillness_code):
        problems.append("Stillness's seconds are not compared unscaled")
    patience_code = function_code(
        source, "float UCataclysmDungeonModifierEffects::EdictOfPatienceCooldownLongerPercentAt(float Strength)")
    if "FMath::Min(" in patience_code:
        problems.append("Patience's scaling is held at a cap")
    return problems


def leech_problems(source: str) -> list[str]:
    code = code_lines(source)
    found = re.findall(r"DungeonModifierEffectsRemoveStat\(Modifiers, DungeonModifierEffectsLifeLeechStat,\s*([^;]*);",
                       code)
    if len(found) != 1:
        return [f"life leech is removed {len(found)} times in StatModifiersFor, not once"]
    argument = found[0]
    problems = []
    if "Strength" in argument:
        problems.append("the life leech removal reads a strength")
    if "Effects.TemperanceHealingLessPercent > 0.0f" not in argument:
        problems.append(f"the life leech removal is not written from Temperance's field: {argument!r}")
    return problems


def caller_problems(mode: str) -> list[str]:
    code = code_lines(mode)
    calls = re.findall(r"PlayerEffectsFor\(\s*([^;]*?)\)", code)
    problems = []
    if len(calls) != 3:
        problems.append(f"the game mode calls PlayerEffectsFor {len(calls)} times, not three")
    for call in calls:
        arguments = [a.strip() for a in call.split(",")]
        if len(arguments) != 4 or not arguments[3].startswith("EdictStrength"):
            problems.append(f"a call passes no Edict strength: PlayerEffectsFor({call})")
    return problems


# ---------------------------------------------------------------------------
# The checks
# ---------------------------------------------------------------------------

def test_the_three_rows_are_celestial_rows_of_the_table() -> None:
    rows = {row["Name"]: row for row in csv.DictReader(io.StringIO(read(TABLE)))}
    for key in ROWS.values():
        assert key in rows, f"{key} is not a row of {TABLE.name}"
        assert rows[key]["CataclysmType"] == "Celestial", f"{key} is not a Celestial row"


def test_the_base_numbers_are_the_designs_and_the_rows() -> None:
    problems = base_problems(read(EFFECTS_H), read(DESIGN), descriptions())
    assert problems == [], "; ".join(problems)


def test_the_base_check_sees_a_changed_number() -> None:
    """**THE CONTROL.** Each base number changed in a copy of the header, and a row's words changed, is noticed."""
    header, design, words = read(EFFECTS_H), read(DESIGN), descriptions()
    for old, new in (("EdictOfTemperanceHealingLessPercent = 50.0f;", "EdictOfTemperanceHealingLessPercent = 40.0f;"),
                     ("EdictOfPatienceCooldownLongerPercent = 50.0f;", "EdictOfPatienceCooldownLongerPercent = 60.0f;"),
                     ("EdictOfStillnessSlowPercent = 40.0f;", "EdictOfStillnessSlowPercent = 30.0f;"),
                     ("EdictOfStillnessSeconds = 3.0f;", "EdictOfStillnessSeconds = 4.0f;")):
        assert header.count(old) == 1, f"{old!r} is not written as this control expects"
        assert base_problems(header.replace(old, new), design, words), f"{old!r} -> {new!r} went unnoticed"
    changed = dict(words, Patience=words["Patience"].replace("50% longer", "40% longer"))
    assert base_problems(header, design, changed), "a row saying 40% longer went unnoticed"


def test_the_caps_are_the_designs_and_patience_has_none() -> None:
    problems = cap_problems(read(EFFECTS_H), read(EFFECTS_CPP), read(DESIGN))
    assert problems == [], "; ".join(problems)


def test_the_cap_check_sees_a_changed_cap() -> None:
    """**THE CONTROL.** A changed cap, a lost hold, and a cap given to Patience are each noticed."""
    header, source, design = read(EFFECTS_H), read(EFFECTS_CPP), read(DESIGN)
    for old, new in (("EdictOfTemperanceMostHealingLessPercent = 90.0f;",
                      "EdictOfTemperanceMostHealingLessPercent = 80.0f;"),
                     ("EdictOfStillnessMostSlowPercent = 90.0f;", "EdictOfStillnessMostSlowPercent = 95.0f;"),
                     ("EdictOfStillnessSeconds = 3.0f;",
                      "EdictOfStillnessSeconds = 3.0f;\n\tstatic constexpr float EdictOfPatienceMostLongerPercent = 300.0f;")):
        assert header.count(old) == 1, f"{old!r} is not written as this control expects"
        assert cap_problems(header.replace(old, new), source, design), f"{old!r} -> {new!r} went unnoticed"
    unheld = ("return FMath::Min(EdictOfStillnessSlowPercent * FMath::Max(0.0f, Strength), "
              "EdictOfStillnessMostSlowPercent);")
    assert source.count(unheld) == 1, "Stillness's hold is not written as this control expects"
    assert cap_problems(header, source.replace(unheld, "return EdictOfStillnessSlowPercent * Strength;"), design)
    assert cap_problems(header, source, design.replace("the Edict of Patience has no cap",
                                                        "the Edict of Patience is held at 300%"))


def test_temperances_leech_removal_does_not_scale() -> None:
    problems = leech_problems(read(EFFECTS_CPP))
    assert problems == [], "; ".join(problems)


def test_the_leech_check_sees_a_scaled_removal() -> None:
    """**THE CONTROL.** A copy whose removal is written from a strength, and one with no removal, are noticed."""
    source = read(EFFECTS_CPP)
    written = "Effects.TemperanceHealingLessPercent > 0.0f);"
    assert source.count(written) == 1, "the removal is not written as this control expects"
    assert leech_problems(source.replace(written, "EdictStrength > 1.0f);"))
    assert leech_problems(source.replace("DungeonModifierEffectsRemoveStat(Modifiers, DungeonModifierEffectsLifeLeechStat",
                                         "DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsLifeLeechStat"))


def test_every_game_caller_passes_the_edict_strength() -> None:
    problems = caller_problems(read(MODE_CPP))
    assert problems == [], "; ".join(problems)


def test_the_caller_check_sees_a_caller_left_at_the_default() -> None:
    """**THE CONTROL.** The log line's call with its strength taken away is noticed."""
    mode = read(MODE_CPP)
    call = "FloorBrief.Modifiers, FloorBrief.FloorNumber, FloorBrief.RuleOfChaosChange, EdictStrengthHere()));"
    assert mode.count(call) == 1, "the log line's call is not written as this control expects"
    assert caller_problems(mode.replace(call, "FloorBrief.Modifiers, FloorBrief.FloorNumber, "
                                              "FloorBrief.RuleOfChaosChange));"))
