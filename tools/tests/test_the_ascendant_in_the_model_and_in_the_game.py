"""The Ascendant, the Celestial Cataclysm's Quest dungeon, in the model and in the game.

**WHAT THE TWO HALVES SHARE, AND WHAT THE GAME HOLDS AGAINST THE DESIGN.**

The design is `docs/Cataclysm_GDD_v2.md`, under Heaven's Wrath, "The Ascendant,
Heaven's Wrath's Quest dungeon (its gate)": one more Divine Edict a floor up to
four, each 10% stronger a floor deeper, half as strong again on the Core's floor
while the Heavenly Core stands, and one Edict left on its city for 30 days. The
model has had the Ascendant since 2026-10-19 as an approximation -- one more
average modifier a floor, down to `TuningConfig.ascendant_extra_modifiers_cap`
-- and has no Edicts. The game has had it since 2026-10-22:
`ECataclysmDungeonSubType::Ascendant`, `UCataclysmSurgeScheduler::QuestSubTypeFor`,
the Edict rules in `UCataclysmDungeonModifierRules`, the left Edict on
`FCataclysmCity`, and the Silence cap in `UCataclysmDungeonModifierEffects`.

WHAT IS COMPARED, AND HOW EACH CHECK FAILS:

- **The mapping.** Both halves make Celestial's Quest dungeons Ascendants: the
  model's `QUEST_SUBTYPE_BY_CATACLYSM["Celestial"]` and the game's
  `QuestSubTypeFor` case. Fails when either moves;
  `test_the_mapping_check_sees_a_changed_case` shows it does.
- **At most four.** The model's cap and the game's `AscendantMostEdicts`, and
  the design's "floor 4 and every floor after it four". Fails when any of the
  three moves; `test_the_cap_check_sees_a_changed_cap` shows it does.
- **The game's numbers against the design's.** A tenth a floor, half again on
  the Core's floor, 30 days, and the Edict of Silence's 67.5 second cap, each
  read out of the header that holds it and out of the design's own sentence.
  Fails when either side moves; `test_the_numbers_check_sees_a_changed_number`
  shows it does.
- **The even draw.** The coordinating session ruled on 2026-10-22 that the
  owner's ruling of 2026-09-07 -- a modifier's Weight is a danger score and never
  sets how often it is drawn -- holds for the Edicts. So neither the danger nor
  any weight is read in the three functions that draw them, the draw is `Draw`'s
  own, and the design no longer says "by their weights". Fails when any of those
  moves; `test_the_draw_check_sees_a_danger_read` shows it does.
- **The rows.** Every Edict the game lists is a Celestial row of
  `game/Data/DungeonModifiers.csv`.

THE MODEL DOES NOT HAVE THE EDICTS, THE CORE OR THE LEFT EDICT, so nothing else
can be compared; that is recorded in `docs/DECISIONS.md`, 2026-10-19 and
2026-10-22. A LINE THAT IS A COMMENT DOES NOT COUNT.
"""

from __future__ import annotations

import csv
import io
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

# THE MODULE AND NOT ITS CLASSES. A `Test` class imported by name would be
# collected and run a second time from this file.
import test_dungeon_subtype_port as subtype_port  # noqa: E402

EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
MODIFIER_H = EMPIRE / "CataclysmDungeonModifier.h"
MODIFIER_CPP = EMPIRE / "CataclysmDungeonModifier.cpp"
MAP_H = EMPIRE / "CataclysmEmpireMap.h"
SURGE_CPP = EMPIRE / "CataclysmSurge.cpp"
EFFECTS_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
             / "CataclysmDungeonModifierEffects.h")
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
TABLE = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"

ASCENDANT_HEADING = "**The Ascendant, Heaven's Wrath's Quest dungeon (its gate).**"

#: The functions that draw the Edicts, by the line that opens each.
DRAWS = (
    "TArray<FName> UCataclysmDungeonModifierRules::EdictOrderFor(int32 WalkSeed)",
    "TArray<FName> UCataclysmDungeonModifierRules::EdictsOnFloor(",
    "FName UCataclysmDungeonModifierRules::EdictLeftBy(",
)

NUMBER_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_bytes().decode("utf-8").replace("\r\n", "\n")


def code_lines(text: str) -> str:
    """Every line that is not a `//` comment line. The functions read here
    hold no block comments."""
    return "\n".join(line for line in text.splitlines()
                     if not line.strip().startswith("//"))


def cpp_code_of(text: str, opening: str) -> str:
    """One C++ function's code, from the line that opens it to its closing
    brace at the start of a line, comment lines left out."""
    count = text.count(opening)
    assert count == 1, f"the source holds {opening!r} {count} times"
    start = text.index(opening)
    end = text.find("\n}\n", start)
    assert end != -1, f"{opening!r} is not closed"
    return code_lines(text[start:end])


def header_number(text: str, name: str) -> float:
    """A `static constexpr` number by name, as written."""
    found = re.search(rf"static constexpr \w+ {name} = ([\d.]+)f?;", text)
    assert found, f"no 'static constexpr ... {name}' in the header; renamed?"
    return float(found.group(1))


def ascendant_section(design: str) -> str:
    """The design's Ascendant paragraph, up to the next `## ` heading,
    whitespace collapsed."""
    assert design.count(ASCENDANT_HEADING) == 1, (
        "docs/Cataclysm_GDD_v2.md no longer has one Ascendant paragraph under "
        "Heaven's Wrath")
    rest = design[design.index(ASCENDANT_HEADING):]
    end = rest.find("\n## ")
    return " ".join((rest if end < 0 else rest[:end]).split())


# ---------------------------------------------------------------------------
# What each check asks, as functions, so each can be asked of an altered copy
# ---------------------------------------------------------------------------

def mapping_problems(model_map: dict[str, str], surge_cpp: str) -> list[str]:
    cases, _ = subtype_port.the_games_quest_sub_types(surge_cpp)
    problems = []
    if model_map.get("Celestial") != "Ascendant":
        problems.append(f"the model's Celestial Quest sub-type is {model_map.get('Celestial')}")
    if cases.get("Celestial") != "Ascendant":
        problems.append(f"the game's Celestial Quest sub-type is {cases.get('Celestial')}")
    return problems


def cap_problems(model_cap: int, modifier_h: str, section: str) -> list[str]:
    game = int(header_number(modifier_h, "AscendantMostEdicts"))
    found = re.search(r"floor 4 and every floor after it (\w+)", section)
    design = NUMBER_WORDS.get(found.group(1)) if found else None
    problems = []
    if not (model_cap == game == design == 4):
        problems.append(f"model {model_cap}, game {game}, design {design}; each should be four")
    return problems


def number_problems(modifier_h: str, map_h: str, effects_h: str, section: str) -> list[str]:
    """The game's four numbers against the design's sentences."""
    problems = []

    per_floor = re.search(r"\(1 \+ ([\d.]+) x \(N - 1\)\)", section)
    if not per_floor or abs(float(per_floor.group(1))
                            - header_number(modifier_h, "EdictStrongerPerFloor")) > 1e-9:
        problems.append("the tenth a floor")

    core = re.search(r"the Edicts on the Core's floor are (\d+)% stronger", section)
    if not core or abs(1 + int(core.group(1)) / 100
                       - header_number(modifier_h, "EdictStrongerWhileTheCoreStands")) > 1e-9:
        problems.append("half again on the Core's floor")

    days = re.search(r"on that city for (\d+) days", section)
    if not days or int(days.group(1)) != int(header_number(map_h, "LeftEdictDays")):
        problems.append("the thirty days")

    cap = re.search(r"lasts at most ([\d.]+) seconds of its (\d+) second cycle", section)
    if (not cap
            or float(cap.group(1)) != header_number(effects_h, "EdictOfSilenceMostLastsSeconds")
            or float(cap.group(2)) != header_number(effects_h, "EdictOfSilenceEverySeconds")):
        problems.append("the Edict of Silence's cap")
    return problems


def draw_problems(modifier_cpp: str, section: str) -> list[str]:
    problems = []
    for opening in DRAWS:
        code = cpp_code_of(modifier_cpp, opening)
        for word in ("Danger", "Weight"):
            if word in code:
                problems.append(f"{opening.split('::')[1].split('(')[0]} reads {word}")
    order = cpp_code_of(modifier_cpp, DRAWS[0])
    if "Draw(" not in order:
        problems.append("EdictOrderFor no longer draws with Draw, the even draw without repeats")
    # THE OLD SENTENCE, NOT THE WORDS: the design now quotes them to say what it said until 2026-10-22.
    if "not already active, by their weights" in section:
        problems.append("the design still says the Edicts are drawn by their weights")
    if "drawn evenly from the Edicts not already active" not in section:
        problems.append("the design no longer says the Edicts are drawn evenly")
    return problems


# ---------------------------------------------------------------------------
# The checks
# ---------------------------------------------------------------------------

def the_model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def test_both_halves_make_celestials_quest_dungeons_ascendants() -> None:
    problems = mapping_problems(dict(the_model().QUEST_SUBTYPE_BY_CATACLYSM), read(SURGE_CPP))
    assert problems == [], "; ".join(problems)


def test_the_mapping_check_sees_a_changed_case() -> None:
    """**THE CONTROL.** The same check of a copy whose Celestial case answers Hollow."""
    source = read(SURGE_CPP)
    case = "case ECataclysmType::Celestial:\t\treturn ECataclysmDungeonSubType::Ascendant;"
    assert source.count(case) == 1, "the Celestial case is not written as this control expects"
    altered = source.replace(case, case.replace("Ascendant", "Hollow"))
    assert mapping_problems(dict(the_model().QUEST_SUBTYPE_BY_CATACLYSM), altered)


def test_both_halves_and_the_design_hold_at_most_four() -> None:
    problems = cap_problems(the_model().ascendant_extra_modifiers_cap, read(MODIFIER_H),
                            ascendant_section(read(DESIGN)))
    assert problems == [], "; ".join(problems)


def test_the_cap_check_sees_a_changed_cap() -> None:
    """**THE CONTROL.** The game's cap written as five, and the model's as three."""
    header = read(MODIFIER_H)
    line = "static constexpr int32 AscendantMostEdicts = 4;"
    assert header.count(line) == 1, "AscendantMostEdicts is not written as this control expects"
    section = ascendant_section(read(DESIGN))
    assert cap_problems(4, header.replace(line, line.replace("4", "5")), section)
    assert cap_problems(3, header, section)


def test_the_games_numbers_are_the_designs() -> None:
    problems = number_problems(read(MODIFIER_H), read(MAP_H), read(EFFECTS_H),
                               ascendant_section(read(DESIGN)))
    assert problems == [], (
        "the game and the design disagree on: " + "; ".join(problems)
        + ". The design is authoritative; change the game, or record a ruling "
          "in docs/DECISIONS.md and change the design first.")


def test_the_numbers_check_sees_a_changed_number() -> None:
    """**THE CONTROL.** Each of the four numbers changed in a copy is noticed."""
    modifier_h, map_h, effects_h = read(MODIFIER_H), read(MAP_H), read(EFFECTS_H)
    section = ascendant_section(read(DESIGN))
    changes = (
        ("modifier", "EdictStrongerPerFloor = 0.10f;", "EdictStrongerPerFloor = 0.20f;"),
        ("modifier", "EdictStrongerWhileTheCoreStands = 1.5f;", "EdictStrongerWhileTheCoreStands = 1.25f;"),
        ("map", "LeftEdictDays = 30;", "LeftEdictDays = 20;"),
        ("effects", "EdictOfSilenceMostLastsSeconds = 67.5f;", "EdictOfSilenceMostLastsSeconds = 75.0f;"),
    )
    for where, old, new in changes:
        texts = {"modifier": modifier_h, "map": map_h, "effects": effects_h}
        assert texts[where].count(old) == 1, f"{old!r} is not written as this control expects"
        texts[where] = texts[where].replace(old, new)
        assert number_problems(texts["modifier"], texts["map"], texts["effects"], section), (
            f"the comparison did not notice {old!r} becoming {new!r}")
    assert number_problems(modifier_h, map_h, effects_h, section.replace("30 days", "40 days"))


def test_the_edicts_are_drawn_evenly_and_read_no_danger() -> None:
    problems = draw_problems(read(MODIFIER_CPP), ascendant_section(read(DESIGN)))
    assert problems == [], "; ".join(problems)


def test_the_draw_check_sees_a_danger_read() -> None:
    """**THE CONTROL.** A copy whose floor draw reads the danger, and a design
    that says "by their weights" again, are each noticed."""
    source = read(MODIFIER_CPP)
    section = ascendant_section(read(DESIGN))
    anchor = "const int32 Wanted = FMath::Clamp(FloorNumber, 0, AscendantMostEdicts);"
    assert source.count(anchor) == 1, "EdictsOnFloor is not written as this control expects"
    reads = source.replace(anchor, anchor + "\n\tconst float Heavier = Pool[0].Danger;")
    assert draw_problems(reads, section)
    assert draw_problems(source, section.replace(
        "drawn evenly from the Edicts not already active",
        "drawn from the Edicts not already active, by their weights"))


def test_every_edict_the_game_lists_is_a_celestial_row() -> None:
    source = code_lines(read(MODIFIER_CPP))
    body = source[source.index("UCataclysmDungeonModifierRules::EdictKeys()"):]
    body = body[:body.index("return Keys;")]
    keys = re.findall(r'TEXT\("(\w+)"\)', body)
    assert keys == ["Celestial_Edict_of_Silence", "Celestial_Forced_Tithes"], (
        f"EdictKeys lists {keys}; the two Edicts that are rows are the Edict of "
        "Silence and Forced Tithes, in that order. G7b adds six")
    rows = {row["Name"]: row for row in csv.DictReader(
        io.StringIO(TABLE.read_bytes().decode("utf-8-sig")))}
    for key in keys:
        assert key in rows, f"{key} is not a row of {TABLE.name}"
        assert rows[key]["CataclysmType"] == "Celestial", f"{key} is not a Celestial row"
