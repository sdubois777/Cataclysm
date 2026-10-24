"""The Edicts of Abstinence, Vigil and Humility, and the divine laws that wake an Angelic Warden: the game against the
design's sentences.

**WHAT IS HELD, AND WHERE EACH SIDE IS.** The design is `docs/Cataclysm_GDD_v2.md`, Heaven's Wrath: the Edict table
gives each Edict's base effect, the Ascendant's paragraph "Each Edict scales its one number, up to a cap" gives each
cap, and "breaking a punishing Edict counts as violating a divine law" is what wakes an Angelic Warden. The game holds
the numbers in `UCataclysmDungeonModifierEffects` (`CataclysmDungeonModifierEffects.h`), the two stats in
`UCataclysmGameplayAbility` and `UCataclysmDamageCalculation`, and the violations in
`ACataclysmDungeonGameMode::NoteDivineLawViolated`; the rows are in `game/Data/DungeonModifiers.csv`, generated from the
design workbook since 2026-10-23 (G7c). The coordinating session ruled the readings on 2026-10-23.

WHAT IS COMPARED, AND HOW EACH CHECK FAILS:

- **The base numbers.** Abstinence's "a quarter" is 25; Vigil's "3 seconds" and "10% of the player's maximum health"
  are 3 and 10, and its telegraph is Wings of the Host's; Humility's "deals no extra damage" is a multiplier held at
  100, which keeps a critical strike a critical strike. Read from the design's table and from each row's description.
  `test_the_base_check_sees_a_changed_number` shows a moved number is noticed.
- **The caps.** Abstinence at most 100, Vigil at most 50 with its 3 seconds fixed, Humility not scaled: a `Most`
  constant held by `FMath::Min` for the first two, the seconds compared unscaled, and Humility's field written from its
  flag and never from a strength. `test_the_cap_check_sees_a_changed_cap` shows it fails when one moves or is lost.
- **The two stats** are the names the floor writes, the ability system reads and the exemption list names.
  `test_the_stat_check_sees_a_misspelled_name` shows a name that stopped matching is noticed.
- **The divine laws.** `NoteDivineLawViolated` is called from exactly the four places ruled -- Abstinence's payment,
  Vigil's smite being called, a movement skill under Stillness and a skill with a cooldown under Patience -- and from
  nothing for Tithe, Silence, Temperance or Humility. `test_the_law_check_sees_a_caller_added_or_lost` shows it fails
  when one is lost or Tithe gains one.
- **Humility's hold and Abstinence's floor**: the hold is a `FMath::Min` against 100 where the multiplier is asked
  for; Abstinence's share is taken from what the other two health costs leave, above the last point of health.
  `test_the_hold_and_floor_check_sees_them_gone` shows each is noticed when it is not.

A LINE THAT IS A COMMENT DOES NOT COUNT, where code is read.
"""

from __future__ import annotations

import csv
import io
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
EFFECTS_H = SOURCE / "Dungeon" / "CataclysmDungeonModifierEffects.h"
EFFECTS_CPP = SOURCE / "Dungeon" / "CataclysmDungeonModifierEffects.cpp"
MODE_CPP = SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp"
ABILITY_CPP = SOURCE / "AbilitySystem" / "CataclysmGameplayAbility.cpp"
TEMPLATE_CPP = SOURCE / "AbilitySystem" / "CataclysmSkillTemplate.cpp"
DAMAGE_H = SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.h"
DAMAGE_CPP = SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.cpp"
VITAL_CPP = SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"
CLASS_STATS_CPP = SOURCE / "Character" / "CataclysmPlayerClassStats.cpp"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
TABLE = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"

ROWS = {
    "Abstinence": "Celestial_Edict_of_Abstinence",
    "Vigil": "Celestial_Edict_of_Vigil",
    "Humility": "Celestial_Edict_of_Humility",
}

ABSTINENCE_STAT = "mana_cost_also_taken_from_health_percent"
HUMILITY_STAT = "critical_strikes_deal_no_extra_damage"


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
    end = (text + "\n").find("\n}\n", start)
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

def base_problems(header: str, damage: str, design: str, words: dict[str, str]) -> list[str]:
    problems = []
    quarter = header_number(header, "EdictOfAbstinenceManaAlsoAsHealthPercent")
    for where, text in (("design", design_row(design, "Abstinence")), ("row", words.get("Abstinence", ""))):
        if "a quarter of a skill's mana cost is also taken from health" not in text.lower() or quarter != 25.0:
            problems.append(f"Abstinence's {where} says {text!r}; the game takes {quarter}%")

    still = header_number(header, "EdictOfVigilStillSeconds")
    smite = header_number(header, "EdictOfVigilMaxHealthPercent")
    for where, text in (("design", design_row(design, "Vigil")), ("row", words.get("Vigil", ""))):
        found = re.search(r"standing still for (\d+) seconds calls a smite on that spot for (\d+)% of the player's "
                          r"maximum health", text.lower())
        if not found or (float(found.group(1)), float(found.group(2))) != (still, smite):
            problems.append(f"Vigil's {where} says {found.group(0) if found else 'nothing'}; the game {still} s and "
                            f"{smite}%")
    if "with the usual telegraph" not in design_row(design, "Vigil"):
        problems.append("the design no longer names Vigil's telegraph")
    for name in ("EdictOfVigilWarningSeconds = WingsOfTheHostWarningSeconds;",
                 "EdictOfVigilMarkRadiusCm = WingsOfTheHostFeatherRadiusCm;"):
        if f"static constexpr float {name}" not in header:
            problems.append(f"Vigil's telegraph is not Wings of the Host's: {name}")

    held = header_number(damage, "NoExtraCriticalMultiplier")
    for where, text in (("design", design_row(design, "Humility")), ("row", words.get("Humility", ""))):
        if "a critical strike deals no extra damage" not in text.lower():
            problems.append(f"Humility's {where} no longer says a critical strike deals no extra damage: {text!r}")
    if held != 100.0:
        problems.append(f"Humility holds the multiplier at {held}, not 100")
    flag = header_number(header, "EdictOfHumilityValue")
    if flag is None or flag <= 0.0:
        problems.append("Humility's flag is not above nought")
    return problems


def cap_problems(header: str, source: str, design: str) -> list[str]:
    problems = []
    section = flat(design)
    abstinence = re.search(r"the Edict of Abstinence takes at most (\d+)% of the mana cost from health", section)
    if not abstinence or float(abstinence.group(1)) != header_number(
            header, "EdictOfAbstinenceMostManaAlsoAsHealthPercent"):
        problems.append("Abstinence's cap")
    vigil = re.search(r"the Edict of Vigil smites for at most (\d+)% of maximum health, its (\d+) seconds fixed",
                      section)
    if (not vigil
            or float(vigil.group(1)) != header_number(header, "EdictOfVigilMostMaxHealthPercent")
            or float(vigil.group(2)) != header_number(header, "EdictOfVigilStillSeconds")):
        problems.append("Vigil's cap or its seconds")
    if "the Edict of Humility does not scale" not in section:
        problems.append("the design no longer says Humility does not scale")

    held = {
        "float UCataclysmDungeonModifierEffects::EdictOfAbstinenceManaAlsoAsHealthPercentAt(float Strength)":
            "EdictOfAbstinenceMostManaAlsoAsHealthPercent",
        "float UCataclysmDungeonModifierEffects::EdictOfVigilSmitePercentAt(float Strength)":
            "EdictOfVigilMostMaxHealthPercent",
    }
    for opening, most in held.items():
        code = function_code(source, opening)
        if "FMath::Min(" not in code or most not in code:
            problems.append(f"{opening.split('::')[1].split('(')[0]} is not held at {most}")
    due = function_code(source, "bool UCataclysmDungeonModifierEffects::EdictOfVigilIsDue(float SecondsStill)")
    if not re.search(r"return SecondsStill >= EdictOfVigilStillSeconds;", due):
        problems.append("Vigil's seconds are not compared unscaled")
    humility = re.findall(r"Effects\.HumilityNoExtraCriticalDamageValue = ([^;]*);", code_lines(source))
    if humility != ["EdictOfHumilityValue"]:
        problems.append(f"Humility's field is written from {humility}, not from its flag alone")
    return problems


def stat_problems(ability: str, damage_cpp: str, class_stats: str, effects: str) -> list[str]:
    problems = []
    if (f'UCataclysmGameplayAbility::ManaCostAlsoTakenFromHealthPercentStat =\n\tTEXT("{ABSTINENCE_STAT}");'
            not in ability):
        problems.append(f"the ability does not name {ABSTINENCE_STAT}")
    if (f'UCataclysmDamageCalculation::CriticalStrikesDealNoExtraDamageStat =\n\tTEXT("{HUMILITY_STAT}");'
            not in damage_cpp):
        problems.append(f"the damage calculation does not name {HUMILITY_STAT}")
    exempt = class_stats[class_stats.index("UCataclysmPlayerClassStats::StatsWithNoAttribute()"):]
    exempt = code_lines(exempt[:exempt.index("};")])
    for stat in (ABSTINENCE_STAT, HUMILITY_STAT):
        if f'TEXT("{stat}")' not in exempt:
            problems.append(f"{stat} is not on the list of stats with no attribute")
    code = code_lines(effects)
    for stat, field in (("UCataclysmGameplayAbility::ManaCostAlsoTakenFromHealthPercentStat",
                         "Effects.AbstinenceManaAlsoAsHealthPercent"),
                        ("UCataclysmDamageCalculation::CriticalStrikesDealNoExtraDamageStat",
                         "Effects.HumilityNoExtraCriticalDamageValue")):
        if not re.search(rf"DungeonModifierEffectsAddFlat\(Modifiers, {re.escape(stat)},\s*{re.escape(field)}\);",
                         code):
            problems.append(f"StatModifiersFor does not write {stat} from {field}")
    return problems


def law_problems(mode: str, template: str) -> list[str]:
    problems = []
    code = code_lines(mode)
    calls = len(re.findall(r"(?<![:\w])NoteDivineLawViolated\(", code))
    if calls != 3:
        problems.append(f"the game mode calls NoteDivineLawViolated {calls} times, not the three ruled")
    vigil = function_code(mode, "void ACataclysmDungeonGameMode::StepEdictOfVigil(")
    if vigil.count("NoteDivineLawViolated(") != 1:
        problems.append("Vigil's smite being called does not break a divine law once")
    laws = function_code(mode, "void ACataclysmDungeonGameMode::NoteSkillUseForDivineLaws(")
    if not re.search(r"Notice\.Slot == ECataclysmAbilitySlot::Movement && FloorBrief\.Modifiers\.Contains\("
                     r"FName\(Effects::EdictOfStillnessKey\)\)\)\s*\{\s*NoteDivineLawViolated\(", laws):
        problems.append("a movement skill under Stillness does not break a divine law")
    if not re.search(r"Notice\.bHasCooldown && FloorBrief\.Modifiers\.Contains\(FName\(Effects::EdictOfPatienceKey\)\)\)"
                     r"\s*\{\s*NoteDivineLawViolated\(", laws):
        problems.append("a skill with a cooldown under Patience does not break a divine law")
    for forbidden in ("EdictOfSilenceKey", "EdictOfTemperanceKey", "EdictOfHumilityKey", "ForcedTithesKey"):
        if forbidden in laws:
            problems.append(f"the skill-use laws read {forbidden}, which breaks no divine law")
    tithes = function_code(mode, "void ACataclysmDungeonGameMode::StepForcedTithes(")
    if "NoteDivineLawViolated" in tithes:
        problems.append("paying a tithe breaks a divine law; it was ruled out")
    template_code = code_lines(template)
    pay = function_code(template_code, "void UCataclysmSkillTemplate::PayHealthCost()")
    if not re.search(r"if \(AbstinenceOwed > 0\.0f\)\s*\{[^}]*NoteDivineLawViolated\(", pay):
        problems.append("Abstinence's payment does not break a divine law")
    if len(re.findall(r"NoteDivineLawViolated\(", template_code)) != 1:
        problems.append("the skill template breaks a divine law somewhere other than Abstinence's payment")
    return problems


def hold_and_floor_problems(vital: str, template: str) -> list[str]:
    problems = []
    code = code_lines(vital)
    if not re.search(r"Hit\.CritMultiplier = FMath::Min\(Hit\.CritMultiplier,\s*"
                     r"UCataclysmDamageCalculation::NoExtraCriticalMultiplier\);", code):
        problems.append("Humility does not hold the critical multiplier at 100 with FMath::Min")
    if not re.search(r"FName\(UCataclysmDamageCalculation::CriticalStrikesDealNoExtraDamageStat\), AssetTags,\s*"
                     r"0\.0f\) > 0\.0f", code):
        problems.append("Humility's flag is not asked with the blow's tags and read as above nought")
    pay = function_code(code_lines(template), "void UCataclysmSkillTemplate::PayHealthCost()")
    if not re.search(r"FromAbstinence = FMath::Min\(\s*AbstinenceOwed,\s*FMath::Max\(0\.0f, Current - "
                     r"LeastHealthAfterCurrentHealthCost - FromCurrent - Added\)\);", pay):
        problems.append("Abstinence's share is not floored to leave a point of health after the other costs")
    if "FromCurrent + Added + FromAbstinence" not in pay:
        problems.append("Abstinence's share is not part of the health cost the rest of PayHealthCost sees")
    return problems


# ---------------------------------------------------------------------------
# The checks
# ---------------------------------------------------------------------------

def test_the_three_rows_are_celestial_rows_of_the_table() -> None:
    rows = {row["Name"]: row for row in csv.DictReader(io.StringIO(read(TABLE)))}
    for key in ROWS.values():
        assert key in rows, f"{key} is not a row of {TABLE.name}"
        assert rows[key]["CataclysmType"] == "Celestial", f"{key} is not a Celestial row"
        assert float(rows[key]["Weight"]) == 15.0, f"{key} is not at the design's weight, 15"


def test_the_base_numbers_are_the_designs_and_the_rows() -> None:
    problems = base_problems(read(EFFECTS_H), read(DAMAGE_H), read(DESIGN), descriptions())
    assert problems == [], "; ".join(problems)


def test_the_base_check_sees_a_changed_number() -> None:
    """**THE CONTROL.** Each base number changed in a copy, and a row's words changed, is noticed."""
    header, damage, design, words = read(EFFECTS_H), read(DAMAGE_H), read(DESIGN), descriptions()
    for old, new in (("EdictOfAbstinenceManaAlsoAsHealthPercent = 25.0f;",
                      "EdictOfAbstinenceManaAlsoAsHealthPercent = 20.0f;"),
                     ("EdictOfVigilStillSeconds = 3.0f;", "EdictOfVigilStillSeconds = 4.0f;"),
                     ("EdictOfVigilMaxHealthPercent = 10.0f;", "EdictOfVigilMaxHealthPercent = 12.0f;"),
                     ("EdictOfVigilWarningSeconds = WingsOfTheHostWarningSeconds;",
                      "EdictOfVigilWarningSeconds = 2.0f;")):
        assert header.count(old) == 1, f"{old!r} is not written as this control expects"
        assert base_problems(header.replace(old, new), damage, design, words), f"{old!r} -> {new!r} went unnoticed"
    held = "NoExtraCriticalMultiplier = 100.0f;"
    assert damage.count(held) == 1, "the held multiplier is not written as this control expects"
    assert base_problems(header, damage.replace(held, "NoExtraCriticalMultiplier = 0.0f;"), design, words)
    changed = dict(words, Vigil=words["Vigil"].replace("for 10% of", "for 15% of"))
    assert base_problems(header, damage, design, changed), "a row saying 15% went unnoticed"


def test_the_caps_are_the_designs_and_humility_does_not_scale() -> None:
    problems = cap_problems(read(EFFECTS_H), read(EFFECTS_CPP), read(DESIGN))
    assert problems == [], "; ".join(problems)


def test_the_cap_check_sees_a_changed_cap() -> None:
    """**THE CONTROL.** A changed cap, a lost hold, scaled seconds and a scaled Humility are each noticed."""
    header, source, design = read(EFFECTS_H), read(EFFECTS_CPP), read(DESIGN)
    for old, new in (("EdictOfAbstinenceMostManaAlsoAsHealthPercent = 100.0f;",
                      "EdictOfAbstinenceMostManaAlsoAsHealthPercent = 90.0f;"),
                     ("EdictOfVigilMostMaxHealthPercent = 50.0f;", "EdictOfVigilMostMaxHealthPercent = 60.0f;")):
        assert header.count(old) == 1, f"{old!r} is not written as this control expects"
        assert cap_problems(header.replace(old, new), source, design), f"{old!r} -> {new!r} went unnoticed"
    unheld = ("return FMath::Min(EdictOfVigilMaxHealthPercent * FMath::Max(0.0f, Strength), "
              "EdictOfVigilMostMaxHealthPercent);")
    assert source.count(unheld) == 1, "Vigil's hold is not written as this control expects"
    assert cap_problems(header, source.replace(unheld, "return EdictOfVigilMaxHealthPercent * Strength;"), design)
    unscaled = "return SecondsStill >= EdictOfVigilStillSeconds;"
    assert source.count(unscaled) == 1, "Vigil's seconds are not written as this control expects"
    assert cap_problems(header, source.replace(unscaled, "return SecondsStill >= EdictOfVigilStillSeconds * 2.0f;"),
                        design)
    flag = "Effects.HumilityNoExtraCriticalDamageValue = EdictOfHumilityValue;"
    assert source.count(flag) == 1, "Humility's field is not written as this control expects"
    assert cap_problems(header, source.replace(flag, "Effects.HumilityNoExtraCriticalDamageValue = EdictStrength;"),
                        design)


def test_the_two_stats_are_named_alike_everywhere() -> None:
    problems = stat_problems(read(ABILITY_CPP), read(DAMAGE_CPP), read(CLASS_STATS_CPP), read(EFFECTS_CPP))
    assert problems == [], "; ".join(problems)


def test_the_stat_check_sees_a_misspelled_name() -> None:
    """**THE CONTROL.** Each name misspelled on one side only is noticed."""
    ability, damage, class_stats, effects = (read(ABILITY_CPP), read(DAMAGE_CPP), read(CLASS_STATS_CPP),
                                             read(EFFECTS_CPP))
    assert stat_problems(ability.replace(f'TEXT("{ABSTINENCE_STAT}")', 'TEXT("mana_cost_also_from_health")'),
                         damage, class_stats, effects)
    assert stat_problems(ability, damage, class_stats.replace(f'TEXT("{HUMILITY_STAT}")', 'TEXT("humility")'),
                         effects)
    assert stat_problems(ability, damage, class_stats,
                         effects.replace("Effects.HumilityNoExtraCriticalDamageValue);",
                                         "Effects.AbstinenceManaAlsoAsHealthPercent);"))


def test_the_divine_laws_are_the_four_ruled() -> None:
    problems = law_problems(read(MODE_CPP), read(TEMPLATE_CPP))
    assert problems == [], "; ".join(problems)


def test_the_law_check_sees_a_caller_added_or_lost() -> None:
    """**THE CONTROL.** Vigil's call lost, Patience's lost, a call given to Tithe, and Abstinence's lost are each
    noticed."""
    mode, template = read(MODE_CPP), read(TEMPLATE_CPP)
    vigil = "\t// AND CALLING IT BREAKS A DIVINE LAW, ruled 2026-10-23.\n\tNoteDivineLawViolated(Where);\n"
    assert mode.count(vigil) == 1, "Vigil's call is not written as this control expects"
    assert law_problems(mode.replace(vigil, ""), template)
    patience = "if (Notice.bHasCooldown && FloorBrief.Modifiers.Contains(FName(Effects::EdictOfPatienceKey)))"
    assert mode.count(patience) == 1, "Patience's law is not written as this control expects"
    assert law_problems(mode.replace(patience, "if (Notice.bHasCooldown && false)"), template)
    tithes = "void ACataclysmDungeonGameMode::StepForcedTithes(ACataclysmPlayerCharacter* Player)\n{\n"
    assert mode.count(tithes) == 1, "StepForcedTithes is not written as this control expects"
    assert law_problems(mode.replace(tithes, tithes + "\tNoteDivineLawViolated(FVector::ZeroVector);\n"), template)
    owed = "if (AbstinenceOwed > 0.0f)"
    assert template.count(owed) == 1, "Abstinence's call is not written as this control expects"
    assert law_problems(mode, template.replace(owed, "if (AbstinenceOwed < 0.0f)"))


def test_humilitys_hold_and_abstinences_floor_are_in_place() -> None:
    problems = hold_and_floor_problems(read(VITAL_CPP), read(TEMPLATE_CPP))
    assert problems == [], "; ".join(problems)


def test_the_hold_and_floor_check_sees_them_gone() -> None:
    """**THE CONTROL.** The hold written as a Max, and the floor taken away, are each noticed."""
    vital, template = read(VITAL_CPP), read(TEMPLATE_CPP)
    hold = "Hit.CritMultiplier = FMath::Min(Hit.CritMultiplier,"
    assert vital.count(hold) == 1, "Humility's hold is not written as this control expects"
    assert hold_and_floor_problems(vital.replace(hold, "Hit.CritMultiplier = FMath::Max(Hit.CritMultiplier,"),
                                   template)
    floor = "FMath::Max(0.0f, Current - LeastHealthAfterCurrentHealthCost - FromCurrent - Added));"
    assert template.count(floor) == 1, "Abstinence's floor is not written as this control expects"
    assert hold_and_floor_problems(vital, template.replace(floor, "Current);"))
